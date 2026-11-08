// src/game/Entity/citizenbehavior.cpp
// Clean reimplementation of CitizenBehavior (population manager).
//
// CitizenBehavior (0x1C0 bytes) — manages all citizens, worker assignment,
// profession statistics, resource shortage notifications, and path distance graphs.
// Lives at GameUpdateLoop+0xF0 (m_citizenList).
//
// Profile source path: Entity\CitizenBehavior.cpp
//
// Decompiled from Application-x64.exe using Ghidra.

#include "Entity/CitizenBehavior.h"
#include "Game/Map.h"                          // Map::size / cells / PathDistance
#include "Component/ComponentDescription.h"   // StorageComponent::Item::_resource sub-descriptions
#include "Component/Description.h"
#include <new>
#include "object/TypeInfo.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "system/MemoryAllocator.h"   // System::MemoryAllocator::MemAlloc / MemFree
#include "Game/exe_constants.h"   // kRdata_*
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "system/Value.h"      // ENDGAME B.3.4b System callers: System::Random
#include "Component/StorageComponent.h"
#include "Component/RawMaterialDescription.h"
#include "Component/ResidenceComponent.h"
#include "Component/CitizenDescription.h"
#include "Component/CitizenComponent.h"
#include "Component/Component.h"
#include "Entity/ComponentEntity.h"
#include "Game/ProfessionList.h"
#include "Entity/exe_bodies.h"
#include "Action/exe_bodies.h"      // Pf_IsDstReachable_f810, Pf_EntityAnchorCell_7fb0, PathPairs_Clear_25b30
#include "Action/PathAvl.h"         // PathCostTreeInsert (0x140025a10)
#include "Component/exe_bodies.h"   // CitizenEmploy_Strat_c4b0, PathGraph_ReassignWorkers, CitizenAssignCitizens, CitizenComp_AssignHousing
#include "diag/stress_harness.h"     // g_pfProfOn, the [Stress::pathgraph] drain line
#include "Game/exe_bodies.h"        // EventLog_AddEvent, ResizeStride0x20
#include "Work/exe_bodies.h"        // WorkBase_TreeNodeFree_e320, WorkBase_CalcDistance_8830
#include "Game/EventLog.h"   // EventLog::AddEvent (0x140095580)
#include "Action/region_search.h"   // RegionSearch::Stats (the [Stress::pairs] line)
#include "diag/err_line.h"          // ErrLine: one write per diagnostic line
#include "Entity/path_cost_index.h"  // PathCostIndex (HITCH_PAIRSPREAD): the m_bst map's storage
#include "ext/drain_split/drain_split.h"   // EXT SEAM (NOT RETAIL, docs/extensions/handins/HITCH_DRAINSPLIT.md): ext.drain_split (class B), the drain's AssignCitizens owed a frame
#include <chrono>

// Tick-driven Builder pool (builder_pool_driver.cpp). No-op unless BANISHED_BUILDER_NATURAL
// is set. Called at the end of CitizenBehavior_Update.
extern "C" void BuilderPool_Tick();


// ============================================================
// Internal helpers, called by name (B3R)
// ============================================================
//   ??0Time@System@@QEAA@M@Z  System::Time::Time(float) -> RtSystem_Time_Ctor
//   (system/Value.h). [MANGLING FIX 2026-07-25] this file once asked for
//   "?Time@Time@System@@QEAA@M@Z", which does not exist; the lookup fell through
//   to an ordinal-798 fallback ("??2GroupDescription@UI@@SAPEAX_KPEAX@Z", the
//   PLACEMENT operator new, which writes nothing), so every Time read back 0:
//   the 300 s path-graph branch and CollectStats ran EVERY tick.
//   .rdata 0x140172234 (300.0f, the path-graph interval) and 0x1401721fc (75.0f,
//   the CollectStats interval): exe_constants.h.

// ============================================================
// CitizenBehavior::Update (0x14008c2b0)
// ============================================================
// Main per-frame tick. Called from GameUpdateLoop::Update.
//
// Logic:
//   1. Advance timers: pathAccumTimer += delta, statsTimer -= delta, notifyTimer -= delta
//   2. Call CountResources (tallies resources per profession)
//   3. Call CheckThresholds (fires shortage notifications)
//   4. If pathAccumTimer >= pathGraphInterval:
//      a. If ProcessPaths returns false: call BuildPathGraph
//      b. Else if pending index < 0 and timer > interval: clamp timer
//   5. If statsTimer <= 0: call CollectStats, reset timer
//   6. If notifyTimer < 0: clear notifyFlags, reset timer to 0
//
// Original: Entity_impl_c2b0
void CitizenBehavior::Update(const System::Time* deltaTime) {
    // Advance timers
    pathGraphTimer._ticks += deltaTime->_ticks;       // pathAccumTimer
    statsTimer._ticks -= deltaTime->_ticks;       // statsTimer
    diseaseRiskTimer._ticks -= deltaTime->_ticks;       // notifyTimer

    // CountResources â€” Entity_helper_9ed0(self)
    CountResources();

    // CheckThresholds â€” Entity_helper_c100(self, deltaTime)
    CheckThresholds(deltaTime);

    // Path graph processing
    // DAT_140172234 is the path graph interval (float seconds)
    // System::Time::Time constructs a Time value from float
    System::Time pathIntervalTime;
    pathIntervalTime._ticks = 0;
    RtSystem_Time_Ctor(&pathIntervalTime, kRdata_140172234);   // 0x140172234
    const int64_t pathInterval = pathIntervalTime._ticks;


    // EXT SEAM (NOT RETAIL, docs/extensions/handins/HITCH_DRAINSPLIT.md): ext.drain_split (class B, src/ext/drain_split/drain_split.h): the
    // AssignCitizens a split drain owes runs here, at the drain's place in the first Update of the
    // next frame (or now, if the path-graph branch below is due: it may rebuild the cost map).
    // g_pending is never set while the row is off.
    if (Ext::DrainSplit::g_pending) Ext::DrainSplit::RunDue(pathGraph, pathInterval <= pathGraphTimer._ticks);

    if (pathInterval <= pathGraphTimer._ticks) {
        // Try to process pending paths. The null tests on the path graph are the
        // tree's (the ctor always allocates it; retail has none): listed, kept.
        bool pathsDone = false;
        PathGraph* pg = pathGraph;
        pathsDone = pg != nullptr && pg->ProcessPaths();

        if (!pathsDone) {
            // Build new path graph
            if (pg) pg->BuildPathGraph();
        } else if (pg && pg->m_pendingIdx < 0 && pathInterval < pathGraphTimer._ticks) {
            // Clamp accumulated time: subtract intervals until within one interval.
            // Guard against pathInterval <= 0 (BANISHED_PATHGRAPH_FAST forces 0, or a failed
            // interval resolve) â€” `acc -= 0` would spin forever. This branch is reached only
            // when ProcessPaths returns true (pairs drained), which requires townMgr+0x100 to
            // be non-empty (e.g. a registered construction site) â€” so it stayed latent until
            // the natural builder flow first populated that BST.
            if (pathInterval > 0) {
                int64_t acc = pathGraphTimer._ticks;
                do {
                    acc -= pathInterval;
                } while (pathInterval < acc);
                pathGraphTimer._ticks = acc;
            } else {
                pathGraphTimer._ticks = 0;   // no positive interval to modulo by -> reset the accumulator
            }
        }
    }

    // Stats collection (periodic)
    if (statsTimer._ticks < 1) {
        CollectStats();
        // Reset timer from DAT_1401721fc (float seconds)
        {
            System::Time newTime;
            newTime._ticks = 0;
            RtSystem_Time_Ctor(&newTime, kRdata_1401721fc);   // 0x1401721fc
            statsTimer._ticks = newTime._ticks;
        }
    }

    // Notification timer reset: retail stores a dword 0 into +0x1A8 (the float's bits)
    if (diseaseRiskTimer._ticks < 0) {
        diseaseRisk = 0.0f;
        diseaseRiskTimer._ticks = 0;
    }

    // Tick-driven Builder pool (BANISHED_BUILDER_NATURAL, opt-in): keep registered
    // construction sites staffed by seating idle Laborers as Builders via the real
    // FindAvailableBuilding cascade, nearest-first, capped by site capacity. No-op unless
    // the natural flow is enabled. This is the headless stand-in for the player's town-hall
    // Builder count â€” the engine has no player-free first-builder seater (the periodic
    // employment allocator only rebalances already-staffed sites). See builder_pool_driver.cpp.
    BuilderPool_Tick();
}


// ============================================================
// NOT RETAIL -- HITCH_PAIRSPREAD: the PathGraph region-pair map (Entity/path_cost_index.h).
// The index holds the map; retail's tree in m_bst is kept only at COMPPROF=4, where every
// operation runs on both and every lookup's answer is compared.
// ============================================================
namespace {
PathCostNode* RetailTreeFind(PathCostNode* node, int high, int low) {
    while (node != nullptr) {
        if (high < node->key0)      node = node->left;
        else if (high > node->key0) node = node->right;
        else if (low < node->key1)  node = node->left;
        else if (low > node->key1)  node = node->right;
        else return node;
    }
    return nullptr;
}
}  // namespace

int32_t* PathGraph::CostFind(int high, int low) {
    int32_t* slot = PathCostIndex::Find(this, high, low);
    if (g_verifyOn) {
        const PathCostNode* node = RetailTreeFind(m_bst.root, high, low);
        const bool ok = (slot == nullptr) == (node == nullptr) && (slot == nullptr || *slot == node->key2);
        StressHarness_Verify("PathCostIndex", ok,
                             (static_cast<long long>(high) << 32) | static_cast<uint32_t>(low));
    }
    return slot;
}

void PathGraph::CostSet(int high, int low, int32_t cost) {
    *PathCostIndex::Insert(this, high, low, cost) = cost;
    if (g_verifyOn) {
        const int32_t key[3] = { high, low, 0 };
        PathCostNode* node = nullptr;
        PathCostTreeInsert(&m_bst, &node, key);   // 0x140025a10
        node->key2 = cost;
    }
}

void PathGraph::CostAdd(int high, int low, int32_t cost) {
    PathCostIndex::Insert(this, high, low, cost);
    if (g_verifyOn) {
        const int32_t key[3] = { high, low, cost };
        PathCostNode* node = nullptr;
        PathCostTreeInsert(&m_bst, &node, key);   // 0x140025a10
    }
}

void PathGraph::CostClear() {
    PathCostIndex::Clear(this);
    PathCostNode* oldBst = m_bst.root;   // null unless the COMPPROF=4 shadow filled it
    if (oldBst != nullptr) {
        WorkBase_TreeNodeFree(&m_bst.root, oldBst->left);    // 0x14000e320
        WorkBase_TreeNodeFree(&m_bst.root, oldBst->right);
        System::MemoryAllocator::MemFree(oldBst);
    }
    m_bst.root = nullptr;
    m_bst.count = 0;
}

// ============================================================
// CitizenBehavior::ProcessPaths (0x1400d6f00)
// ============================================================
// Processes up to 7 pending path distance calculations per call.
// For each pair (cellA, cellB), computes path cost via fn_Map_CalcPathCost.
// If cost < 2 (unreachable), falls back to Euclidean grid distance.
// Stores result in a BST keyed by (max(regionA, regionB), min(regionA, regionB)).
// When all pairs are processed (pendingIdx < 0), triggers worker reassignment
// via fn_PathGraph_ReassignWorkers and fn_PathGraph_AssignCitizens.
//
// Original: FUN_1400d6f00
// NOT RETAIL -- HITCH_PAIRSPREAD diagnostic (only while g_pfProfOn): one [Stress::pairs]
// line per drain with the rebuild window's split of ProcessPaths' pair loop (the
// PathDistance call vs the rest: region lookup, tree search, insert, store) and the
// PathLength memo's counts over the window and since the previous drain.
namespace {
struct PairWindow {
    long long searchTicks = 0, restTicks = 0, calls = 0;
    long long buildAnchors = 0, buildFree = 0, buildPairs = 0;   // BuildPathGraph's steps 1 / 2 / 3-4
    long long tscStart = 0;
    std::chrono::steady_clock::time_point wallStart{};
    RegionSearch::MemoStats atBuild{}, atLastDrain{};
    bool open = false;
};
PairWindow s_pairWin;

void PairWindowOpen(long long anchors, long long freeTree, long long pairsGen) {
    PairWindow& w = s_pairWin;
    w.searchTicks = w.restTicks = w.calls = 0;
    w.buildAnchors = anchors;
    w.buildFree = freeTree;
    w.buildPairs = pairsGen;
    w.tscStart = CompProf_Now();
    w.wallStart = std::chrono::steady_clock::now();
    w.atBuild = RegionSearch::Stats();
    w.open = true;
}

void PairWindowReport(int pairs) {
    PairWindow& w = s_pairWin;
    const RegionSearch::MemoStats now = RegionSearch::Stats();
    if (w.open) {
        const long long tsc = CompProf_Now() - w.tscStart;
        const double ns = (double)std::chrono::duration_cast<std::chrono::nanoseconds>(
                              std::chrono::steady_clock::now() - w.wallStart).count();
        const double msPerTick = tsc > 0 ? ns / 1e6 / (double)tsc : 0.0;
        ErrLine("[Stress::pairs] pairs=%d build: anchors=%.3fms free=%.3fms pairs=%.3fms "
                "calls=%lld search=%.3fms rest=%.3fms "
                "memo_lookups=%lld memo_hits=%lld graph_changes=%lld memo_full=%lld "
                "since_last_drain: lookups=%lld hits=%lld graph_changes=%lld\n",
                pairs, (double)w.buildAnchors * msPerTick, (double)w.buildFree * msPerTick,
                (double)w.buildPairs * msPerTick, w.calls, (double)w.searchTicks * msPerTick, (double)w.restTicks * msPerTick,
                now.lookups - w.atBuild.lookups, now.hits - w.atBuild.hits,
                now.graphChanges - w.atBuild.graphChanges, now.full - w.atBuild.full,
                now.lookups - w.atLastDrain.lookups, now.hits - w.atLastDrain.hits,
                now.graphChanges - w.atLastDrain.graphChanges);
    }
    w.atLastDrain = now;
    w.open = false;
}
}  // namespace

bool PathGraph::ProcessPaths() {
    PathGraph* pg = this;
    int pendingIdx = pg->m_pendingIdx;

    // Nothing to process
    if (pendingIdx < 0) {
        return false;
    }

    // Process at most 7 pairs per call (indices 0..6)
    int remaining = pendingIdx;
    if (remaining > 6) {
        remaining = 6;
    }

    // The world map: g_pGameWorldSingleton + 0xf8
    Map* map = GameUpdateLoop::_singleton->m_game;

    // Pairs array: each entry is {int32 cellA, int32 cellB} = 8 bytes
    PathCellPair* pairs = pg->m_pairs;

    for (int i = remaining; i >= 0; i--) {
        int idx = pg->m_pendingIdx;
        int32_t cellA = pairs[idx].cellA;
        int32_t cellB = pairs[idx].cellB;

        // Compute path cost between cells
        uint32_t cost = 0;
        const long long tp0 = g_pfProfOn ? CompProf_Now() : 0;   // [Stress::pairs]
        cost = (uint32_t)map->PathDistance((uint32_t)cellA, (uint32_t)cellB, 0);   // 0x14009f810
        const long long tp1 = tp0 ? CompProf_Now() : 0;

        // If cost < 2 (unreachable or adjacent), fall back to Euclidean grid distance
        if (cost < 2) {
            int mapWidth = map->size;
            int dx = cellA / mapWidth - cellB / mapWidth;
            int dy = cellA % mapWidth - cellB % mapWidth;
            float dist = sqrtf((float)(dx * dx + dy * dy));
            cost = (uint32_t)dist;
        }

        // Region IDs from the map's cell array (+0x80, MapCell stride 0x30, regionId +0x18)
        MapCell* cells = map->cells.GetData();
        int regionA = cells[(int64_t)cellA].regionId;
        int regionB = cells[(int64_t)cellB].regionId;

        // BST key: (max, min) of region IDs
        int keyHigh = regionA;
        int keyLow = regionB;
        if (regionA < regionB) {
            keyHigh = regionB;
            keyLow = regionA;
        }

        // The tree search, the insert of {keyHigh, keyLow, 0} on a miss
        // (PathCostTreeInsert 0x140025a10) and the store of the cost into the node: one
        // set on the region-pair map (HITCH_PAIRSPREAD, Entity/path_cost_index.h).
        pg->CostSet(keyHigh, keyLow, static_cast<int32_t>(cost));

        // Decrement pending index
        pg->m_pendingIdx = pg->m_pendingIdx - 1;
        if (tp0) {
            s_pairWin.searchTicks += tp1 - tp0;
            s_pairWin.restTicks += CompProf_Now() - tp1;
            ++s_pairWin.calls;
        }
    }

    // If all pairs processed, run post-processing
    if (pg->m_pendingIdx < 0) {
        // [Stress::pathgraph] (HITCH_PATHGRAPH): one g_pfProfOn read when the harness is off.
        const long long t0 = g_pfProfOn ? CompProf_Now() : 0;
        // Worker reassignment based on computed distances
        pg->ReassignWorkers();   // 0x1400d62a0
        const long long t1 = t0 ? CompProf_Now() : 0;
        // EXT SEAM (NOT RETAIL, docs/extensions/handins/HITCH_DRAINSPLIT.md): ext.drain_split (class B, src/ext/drain_split/drain_split.h):
        // AssignCitizens is owed to the next frame (CitizenBehavior::Update runs it, and prints
        // this drain's [Stress::pathgraph] line). Off: one test of g_on per drain.
        const bool split = Ext::DrainSplit::g_on && Ext::DrainSplit::DeferAssign(pg, pg->m_pairCount, t1 - t0);
        // Citizen assignment sweep
        if (!split) pg->AssignCitizens();   // 0x1400d43b0
        if (t0 && split) {
            PairWindowReport(pg->m_pairCount);
        } else if (t0) {
            const PathGraphCostCounts c = g_pathGraphCost;
            StressHarness_PathGraphDrain(pg->m_pairCount, t1 - t0, CompProf_Now() - t1,
                                         c.calls, c.hits, c.misses, c.direct, c.differ);
            g_pathGraphCost = PathGraphCostCounts{ 0, 0, 0, 0, -1 };
            PairWindowReport(pg->m_pairCount);
        }
        // Clear the pairs array
        PathPairs_Clear_25b30(pg);   // 0x140025b30
    }

    return true;
}

// ============================================================
// CitizenBehavior::BuildPathGraph (0x1400d70c0)
// ============================================================
// Rebuilds the path distance graph when no computation is pending.
// 1. Collects grid cell IDs for all buildings
// 2. Clears old BST distance cache
// 3. Clears pairs array
// 4. For each profession group, for each entity, generates (entity_cell, building_cell)
//    pairs with all buildings, skipping pairs that already have a BST entry
// 5. Sets pendingIdx = pairCount - 1 to begin processing
//
// Original: Entity_impl_70c0
void PathGraph::BuildPathGraph() {
    PathGraph* pg = this;
    // Only rebuild when not currently processing
    if (pg->m_pendingIdx >= 0) {
        return;
    }

    int dbgEntities = 0, dbgEntRegionOk = 0, dbgPairsAdded = 0;

    // Get key pointers from the game world singleton
    GameUpdateLoop* world = GameUpdateLoop::_singleton;
    Map* map = world->m_game;
    CitizenBehavior* town = world->m_citizenList;
    ProfessionList* buildingList = world->m_professionList;

    const long long tb0 = g_pfProfOn ? CompProf_Now() : 0;   // [Stress::pairs]
    // ---- Step 1: Collect grid cell IDs for all buildings ----
    // Retail (0x1400d70c0): lVar3 = *(gw+0xf0), loop over *(int*)(lVar3+0x28)
    // entries of *(longlong*)(lVar3+0x20) — the CitizenBehavior/town RESIDENCE
    // list, NOT gw+0xb0 (that is the ProfessionList, used only in Step 4 keys).
    int buildingCount = town->residences._size;
    Object::InternalPtr<ComponentEntity>* buildingArray = town->residences._data;

    // Dynamic array for building grid IDs
    int* buildingCells = nullptr;
    int cellCount = 0;
    int cellCapacity = 0;

    // Retail appends through Array<int>::Add's inlined SetSize(count + 1) (0x1400d7162 -
    // 0x1400d71f9): the "size unchanged" test compares the new size with the COUNT
    // ([rbp-0x61]), so it never skips, and the store grows to (count + 1) * 3 (1 for the
    // first element) only when the CAPACITY ([rbp-0x5d]) is exceeded. The port tested the
    // new size against the capacity, which stopped the list at 5 elements (new size 6 ==
    // capacity 6 from then on): BuildPathGraph paired the workplaces with 5 residences of
    // the town's 278 in the stress town, and AssignCitizens missed the tree for the rest
    // (HITCH_PATHGRAPH). The per-element placement-new null tests of the copy are dropped.
    for (int i = 0; i < buildingCount; i++) {
        ComponentEntity* entityIBP = buildingArray[i].Get();
        const int gridId = EntityAnchorCell(&entityIBP, nullptr);   // 0x140087fb0

        const int newCount = cellCount + 1;
        if (newCount > cellCapacity) {
            const int newCap = cellCount != 0 ? newCount * 3 : newCount;
            int* newArr = static_cast<int*>(
                System::MemoryAllocator::MemAlloc((int64_t)newCap * 4, 4));   // 0x1400d71a2
            for (int j = 0; j < cellCount; j++) newArr[j] = buildingCells[j];
            if (buildingCells) System::MemoryAllocator::MemFree(buildingCells);   // 0x1400d71f9
            buildingCells = newArr;
            cellCapacity = newCap;
        }
        buildingCells[cellCount] = gridId;
        cellCount = newCount;
    }

    const long long tb1 = tb0 ? CompProf_Now() : 0;
    // ---- Step 2: Clear old BST ----
    // Retail frees the tree node by node (0x14000e320 on both subtrees, then the root) and
    // zeroes root and count: PathGraph::CostClear, which does that to m_bst and drops the
    // index (HITCH_PAIRSPREAD).
    pg->CostClear();

    const long long tb2 = tb0 ? CompProf_Now() : 0;
    // ---- Step 3: Clear pairs array ----
    PathPairs_Clear_25b30(pg);   // 0x140025b30

    // ---- Step 4: Generate pairs from profession entities x buildings ----
    // The map's cell array, for region lookups
    MapCell* cells = map->cells.GetData();

    // Building list at citizenBehavior+0xb0 (via game world +0xb0)
    int profGroupCount = buildingList->professions.GetSize();


    for (int gi = 0; gi < profGroupCount; gi++) {
        // Get profession key from building list
        Profession* profKey = buildingList->professions.GetData()[gi];

        // Look up profession BST at citizenBehavior+0x100
        ProfessionBucket* profNode = town->workplacesByProfession._root;
        int entityCount = 0;
        while (profNode != nullptr) {
            if (profKey < profNode->element.key) {
                profNode = profNode->left;
            } else if (profKey > profNode->element.key) {
                profNode = profNode->right;
            } else {
                entityCount = profNode->element.entities._size;
                break;
            }
        }

        // For each entity in this profession group
        for (int64_t ei = 0; ei < entityCount; ei++) {
            // Re-lookup profession BST (same pattern as decompiled code)
            ProfessionBucket* lookupNode = town->workplacesByProfession._root;
            ProfessionBucket* foundNode = nullptr;
            while (lookupNode != nullptr) {
                if (profKey < lookupNode->element.key) {
                    lookupNode = lookupNode->left;
                } else if (profKey > lookupNode->element.key) {
                    lookupNode = lookupNode->right;
                } else {
                    foundNode = lookupNode;
                    break;
                }
            }
            if (!foundNode) break;

            // Get entity IBP from the profession node's entity array
            ComponentEntity* entityIBP = foundNode->element.entities._data[ei].Get();

            // Get grid cell ID for this entity
            int entityCell = -1;
            entityCell = EntityAnchorCell(&entityIBP, nullptr);   // 0x140087fb0

            // Get entity's region ID. The index is sign-extended, as retail's
            // MOVSXD R11,EAX at 0x1400d732a does on EntityAnchorCell's result.
            int entityRegion = cells[(int64_t)entityCell].regionId;
            ++dbgEntities;
            if (entityRegion >= 0) ++dbgEntRegionOk;

            // Pair with each building cell
            if (entityRegion >= 0 && buildingCells != nullptr) {
                for (int bi = 0; bi < cellCount; bi++) {
                    int buildingCell = buildingCells[bi];
                    int buildingRegion = cells[(int64_t)buildingCell].regionId;

                    if (buildingRegion < 0) continue;

                    // BST key: (max, min) of region IDs
                    int keyHigh = entityRegion;
                    int keyLow = buildingRegion;
                    if (entityRegion < buildingRegion) {
                        keyHigh = buildingRegion;
                        keyLow = entityRegion;
                    }

                    // Check if this pair already exists in BST (the map, HITCH_PAIRSPREAD)
                    const bool found = pg->CostFind(keyHigh, keyLow) != nullptr;

                    if (found) continue;

                    // Add pair to pairs array: {max, min} of the two cells (8 bytes)
                    PathCellPair pairData = MakePathCellPair(entityCell, buildingCell);
                    int curPairCount = (int)pg->m_pairCount;
                    int newPairCount = curPairCount + 1;

                    // Grow pairs array (same dynamic array pattern)
                    PathCellPair* oldPairs = pg->m_pairs;
                    pg->m_pairCount = newPairCount;
                    int pairCap = pg->m_pairCapacity;
                    if (pairCap < newPairCount) {
                        int newCap = newPairCount;
                        if (curPairCount != 0) {
                            newCap = newPairCount * 3;
                        }
                        pg->m_pairCapacity = newCap;
                        PathCellPair* newArr = static_cast<PathCellPair*>(
                            System::MemoryAllocator::MemAlloc((int64_t)newCap * 8, 4));
                        pg->m_pairs = newArr;
                    }

                    // Copy existing entries + new
                    int copyStart = (oldPairs == pg->m_pairs) ? curPairCount : 0;
                    for (int k = copyStart; k < newPairCount; k++) {
                        PathCellPair* dest = pg->m_pairs;
                        if (dest) {
                            if (k < curPairCount && oldPairs) {
                                dest[k] = oldPairs[k];
                            } else {
                                dest[k] = pairData;
                            }
                        }
                    }

                    if (oldPairs != pg->m_pairs && oldPairs) {
                        System::MemoryAllocator::MemFree(oldPairs);
                    }
                    ++dbgPairsAdded;

                    // Insert placeholder into BST to prevent duplicate pairs: the key
                    // {(max, min) region, 0} (PathCostTreeInsert 0x140025a10), absent here.
                    pg->CostAdd(keyHigh, keyLow, 0);
                }
            }
        }
    }

    // ---- Step 5: Set pending index ----
    pg->m_pendingIdx = (int)pg->m_pairCount - 1;

    if (tb0 && pg->m_pendingIdx >= 0) PairWindowOpen(tb1 - tb0, tb2 - tb1, CompProf_Now() - tb2);   // [Stress::pairs]

    // Free temporary building cells array
    if (buildingCells) {
        System::MemoryAllocator::MemFree(buildingCells);
    }
}
// ============================================================
// CitizenBehavior::CountResources (0x140089ed0)
// ============================================================
// Iterates two entity arrays (workplaces at +0x00/+0x08 and buildings
// at +0x10/+0x18), finds StorageComponent on each entity, then iterates
// stored items to find RawMaterialDescription and tally resource counts
// by profession type (RawMaterialFlags at +0x5c of the description).
//
// Results are accumulated into the ProfessionResourceEntry[21] array
// at +0x170 (profResources). Before counting, previousCount is set to
// currentCount and currentCount is zeroed for each entry.
//
// Also tracks food vs durable-goods totals (flags & 0x1c40 split) in the
// notification summary at +0x180 (param_1[0x30] in Ghidra longlong* indexing).
//
// The RawMaterialFlags -> entry mapping (the bit->offset chain below is
// DISASM-FAITHFUL — verified against 0x140089ed0 on 2026-07-19. NOTE: the
// flag/profession NAMES were derived from the old wrong toolkit-order flag
// table; the real bit 5 = EDIBLE (not Fuel), bit 6 = Fuel — and bit 6 is not
// tallied here at all. See engine/types/RawMaterialDescription.h.):
//   bit 5  (Edible)    -> entry 5                  offset 0xa0
//   bit 7  (Wood)      -> entry 3  (Fisherman)    offset 0x60
//   bit 8  (Stone)     -> entry 9  (Stonecutter)  offset 0x120
//   bit 9  (Iron)      -> entry 4  (Hunter)       offset 0x80
//   bit 10 (Health)    -> entry 0  (Laborer)       offset 0x00
//   bit 11 (Clothing)  -> entry 1  (Builder)       offset 0x20
//   bit 12 (Textile)   -> entry 2  (Farmer)        offset 0x40
//   bit 13 (Alcohol)   -> entry 6  (Forester)      offset 0xc0
//   bit 14 (Custom0)   -> entry 7  (Woodcutter)    offset 0xe0
//   bit 17 (Custom3)   -> entry 8  (Blacksmith)    offset 0x100
//   bit 16 (Custom2)   -> entry 10 (Tailor)        offset 0x140
//   bit 18 (Custom4)   -> entry 11 (Doctor)        offset 0x160
//   bit 19 (Custom5)   -> entry 12 (Teacher)       offset 0x180
//   bit 20 (Custom6)   -> entry 13 (Cleric)        offset 0x1a0
//   bit 21 (Custom7)   -> entry 14 (Innkeeper)     offset 0x1c0
//   bit 22 (Custom8)   -> entry 15 (Vendor)        offset 0x1e0
//   bit 23 (Custom9)   -> entry 16 (Miner)         offset 0x200
//   bit 24             -> entry 17 (Pastor)        offset 0x220
//   bit 25             -> entry 18 (Trader)        offset 0x240
//   bit 26             -> entry 19 (Herdsman)      offset 0x260
//   bit 27             -> entry 20 (Farmer2)       offset 0x280
//
// Original: Entity_helper_9ed0
void CitizenBehavior::CountResources() {
    // --- Phase 0: Shift currentCount -> previousCount and zero currentCount ---
    // The profResources array is 21 entries of 0x20 bytes each (total 0x2A0).
    ProfessionResourceEntry* profRes = professionResources._data;
    if (profRes) {
        for (int e = 0; e < 21; e++) {
            profRes[e].previousCount = profRes[e].currentCount;
            profRes[e].currentCount = 0;
        }
    }

    // --- Zero notification summary at +0x180 (the two ResourceNotifyEntry records:
    // [0] = normal resources, [1] = tools / health / clothing / textile) ---
    ResourceNotifyEntry* notifySummary = notifyEntries._data;
    if (notifySummary) {
        notifySummary[0].maxCapacity = 0;  // +0x04: normal resource worker count
        notifySummary[0].current = 0;      // +0x00: normal resource total
        notifySummary[1].maxCapacity = 0;  // +0x14: dead resource worker count
        notifySummary[1].current = 0;      // +0x10: dead resource total
    }

    // Helper lambda: find a component on an entity by type ID.
    // Returns the found component, or nullptr. The null tests are the tree's.
    auto findComponent = [](ComponentEntity* entity, uint32_t targetId) -> Component* {
        if (!entity) return nullptr;
        int compCount = entity->_components._size;
        for (int i = 0; i < compCount; i++) {
            Component* comp = entity->_components[i].Get();
            if (!comp) continue;
            // virtual slot 2 (+0x10): Type()
            int compTypeId = static_cast<int>(comp->Type());
            if (static_cast<uint32_t>(compTypeId) == targetId) {
                if (::Object::IsDerivedFromNullSafe(comp, targetId)) {
                    return comp;
                }
                break;
            }
        }
        return nullptr;
    };

    // Helper lambda: given RawMaterialFlags, add itemCount to the correct
    // profession entry in profResources.
    auto addToProfession = [&](uint32_t flags, int32_t itemCount) {
        int entry = -1;
        if (flags & 0x20) {
            // bit 5 (Edible) -> entry 5, offset 0xa0
            entry = 5;
        } else if (flags & 0x80) {
            // bit 7 (Wood) -> entry 3, offset 0x60
            entry = 3;
        } else if (flags & 0x100) {
            // bit 8 (Stone) -> entry 9, offset 0x120
            entry = 9;
        } else if (flags & 0x200) {
            // bit 9 (Iron) -> entry 4, offset 0x80
            entry = 4;
        } else if (flags & 0x400) {
            // bit 10 (Health) -> entry 0, offset 0x00
            entry = 0;
        } else if (flags & 0x800) {
            // bit 11 (Clothing) -> entry 1, offset 0x20
            entry = 1;
        } else if (flags & 0x1000) {
            // bit 12 (Textile) -> entry 2, offset 0x40
            entry = 2;
        } else if (flags & 0x2000) {
            // bit 13 (Alcohol) -> entry 6, offset 0xc0
            entry = 6;
        } else if (flags & 0x4000) {
            // bit 14 (Custom0) -> entry 7, offset 0xe0
            entry = 7;
        } else if (flags & 0x20000) {
            // bit 17 (Custom3) -> entry 8, offset 0x100
            entry = 8;
        } else if (flags & 0x10000) {
            // bit 16 (Custom2) -> entry 10, offset 0x140
            entry = 10;
        } else if (flags & 0x40000) {
            // bit 18 (Custom4) -> entry 11, offset 0x160
            entry = 11;
        } else if (flags & 0x80000) {
            // bit 19 (Custom5) -> entry 12, offset 0x180
            entry = 12;
        } else if (flags & 0x100000) {
            // bit 20 (Custom6) -> entry 13, offset 0x1a0
            entry = 13;
        } else if (flags & 0x200000) {
            // bit 21 (Custom7) -> entry 14, offset 0x1c0
            entry = 14;
        } else if (flags & 0x400000) {
            // bit 22 (Custom8) -> entry 15, offset 0x1e0
            entry = 15;
        } else if (flags & 0x800000) {
            // bit 23 (Custom9) -> entry 16, offset 0x200
            entry = 16;
        } else if (flags & 0x1000000) {
            // bit 24 -> entry 17, offset 0x220
            entry = 17;
        } else if (flags & 0x2000000) {
            // bit 25 -> entry 18, offset 0x240
            entry = 18;
        } else if (flags & 0x4000000) {
            // bit 26 -> entry 19, offset 0x260
            entry = 19;
        } else if (flags & 0x8000000) {
            // bit 27 -> entry 20, offset 0x280
            entry = 20;
        }

        if (entry >= 0 && profRes) {
            profRes[entry].currentCount += itemCount;
        }
    };

    // --- Phase 1: Iterate first entity array (+0x00/+0x08) ---
    // These entities have StorageComponent; their items are tallied for both
    // profession resource counts AND notification summaries.
    int64_t firstCount = static_cast<int64_t>(storageLocations._size);
    for (int64_t idx = 0; idx < firstCount; idx++) {
        // Find StorageComponent on this entity
        Component* storageComp = findComponent(storageLocations._data[idx].Get(), StorageComponent::TypeId());
        // (found by the type id and IsDerivedFrom above: B10)
        StorageComponent* storage = static_cast<StorageComponent*>(storageComp);

        // Read item count from storage (+0x30)
        int32_t itemCount = storage->storageSlots._size;
        if (itemCount <= 0) continue;

        bool hadNormalResources = false;
        bool hadDeadResources = false;

        // Iterate storage items: array at +0x28, stride 0x10
        // Each item: [0x00] = the stored material's template, [0x08] = quantity
        for (int32_t j = 0; j < itemCount; j++) {
            StorageComponent::Item* itemEntry = &storage->storageSlots[j];

            // Find RawMaterialDescription among the template's sub-descriptions
            // (ComponentDescription +0x28 / count +0x30)
            ComponentDescription* itemEntity = itemEntry->_resource;
            RawMaterialDescription* rawMat = nullptr;
            if (itemEntity) {
                int subCompCount = itemEntity->_subDescCount;
                uint32_t rawMatTypeId = RawMaterialDescription::TypeId();
                for (int k = 0; k < subCompCount; k++) {
                    Description* subComp = itemEntity->_subDescs[k];
                    if (!subComp) continue;
                    int compTypeId = static_cast<int>(subComp->Type());
                    if (static_cast<uint32_t>(compTypeId) == rawMatTypeId) {
                        if (::Object::IsDerivedFromNullSafe(subComp, rawMatTypeId)) {
                            rawMat = static_cast<RawMaterialDescription*>(subComp);
                        }
                        break;
                    }
                }
            }
            if (!rawMat) continue;

            // Weight per unit (+0x48) times the item quantity
            int32_t weightPerUnit = rawMat->_weight;
            int32_t quantity = itemEntry->_count;
            int32_t totalAmount = weightPerUnit * quantity;

            // RawMaterialFlags (+0x5c)
            uint32_t flags = rawMat->_flags;

            // Update notification summary: split by flag 0x1c40
            // 0x1c40 = Tool(6) | Health(10) | Clothing(11) | Textile(12)
            if (notifySummary) {
                if ((flags & 0x1c40) == 0) {
                    // Normal resources (food, fuel, etc.)
                    notifySummary[0].current += totalAmount;
                    hadNormalResources = true;
                } else {
                    // Tools/health/clothing/textile
                    notifySummary[1].current += totalAmount;
                    hadDeadResources = true;
                }
            }

            // Categorize by profession flag
            addToProfession(flags, itemEntry->_count);
        }

        // Update notification worker counts from storage+0x38 (capacity/worker count)
        if (hadDeadResources && notifySummary) {
            notifySummary[1].maxCapacity += storage->capacity;
        }
        if (hadNormalResources && notifySummary) {
            notifySummary[0].maxCapacity += storage->capacity;
        }
    }

    // --- Phase 2: Iterate second entity array (+0x10/+0x18) ---
    // Same logic as Phase 1 but WITHOUT notification summary updates.
    int64_t secondCount = static_cast<int64_t>(marketLocations._size);
    for (int64_t idx = 0; idx < secondCount; idx++) {
        // Find StorageComponent on this entity
        Component* storageComp = findComponent(marketLocations._data[idx].Get(), StorageComponent::TypeId());
        StorageComponent* storage = static_cast<StorageComponent*>(storageComp);

        // Read item count from storage (+0x30)
        int32_t itemCount = storage->storageSlots._size;
        if (itemCount <= 0) continue;

        // Iterate storage items
        for (int32_t j = 0; j < itemCount; j++) {
            StorageComponent::Item* itemEntry = &storage->storageSlots[j];

            // Find RawMaterialDescription among the template's sub-descriptions
            ComponentDescription* itemEntity = itemEntry->_resource;
            RawMaterialDescription* rawMat = nullptr;
            if (itemEntity) {
                uint32_t rawMatTypeId = RawMaterialDescription::TypeId();
                int subCompCount = itemEntity->_subDescCount;
                for (int k = 0; k < subCompCount; k++) {
                    Description* subComp = itemEntity->_subDescs[k];
                    if (!subComp) continue;
                    int compTypeId = static_cast<int>(subComp->Type());
                    if (static_cast<uint32_t>(compTypeId) == rawMatTypeId) {
                        if (::Object::IsDerivedFromNullSafe(subComp, rawMatTypeId)) {
                            rawMat = static_cast<RawMaterialDescription*>(subComp);
                        }
                        break;
                    }
                }
            }
            if (!rawMat) continue;

            // RawMaterialFlags (+0x5c); categorize by profession flag (no notification
            // update in Phase 2)
            addToProfession(rawMat->_flags, itemEntry->_count);
        }
    }
}

// ============================================================
// CitizenBehavior::CheckThresholds (0x14008c100)
// ============================================================
// Checks resource shortage thresholds for each profession and fires
// notification events when resources drop critically low.
//
// Logic:
//   Part 1 — Profession resources (21 entries at +0x170, 0x20 bytes each):
//     For each entry where timer <= 0 AND index < 6:
//       - threshold = maxCount / 20 (5% of max)
//       - For index 3 (Fisherman): combine with profession 9 (Stonecutter) values
//       - If currentCount < threshold AND threshold <= previousCount:
//           fire notification type (index + 0x11), severity 3
//           reset timer to 120 seconds
//
//   Part 2 — Resource notify entries (2 at +0x180, 0x10 bytes each):
//     For each entry where timer <= 0:
//       - If current > (maxCapacity / 20) * 19 (95% full):
//           fire notification type 0x17 (entry 0) or 0x18 (entry 1), severity 3
//           reset timer to 120 seconds
//
// Original: Entity_helper_c100
void CitizenBehavior::CheckThresholds(const System::Time* deltaTime) {
    float notifyInterval = 120.0f; // DAT_140172210 = 120.0f seconds
    // GameUpdateLoop +0x138 is the event notifier (still void* in GameUpdateLoop.h)
    EventLog* notifMgr = GameUpdateLoop::_singleton->m_eventNotifier;

    // Part 1: Check profession resource thresholds (21 entries)
    for (int i = 0; i < 21; i++) {
        int64_t* timer = &professionResources._data[i].timer._ticks;

        // Decrement timer
        *timer -= deltaTime->_ticks;

        if (*timer < 1 && static_cast<uint32_t>(i) < 6) {
            // Reset timer to zero (will be set to interval if notification fires)
            *timer = 0;

            int32_t maxCount = professionResources._data[i].maxCount;
            int32_t currentCount = professionResources._data[i].currentCount;
            int32_t previousCount = professionResources._data[i].previousCount;

            // Profession 3 (Fisherman): combine with profession 9 (Stonecutter)
            if (i == 3) {
                maxCount += professionResources._data[9].maxCount;
                currentCount += professionResources._data[9].currentCount;
                previousCount += professionResources._data[9].previousCount;
            }

            int32_t threshold = maxCount / 20; // 5% threshold
            if (currentCount < threshold && threshold <= previousCount) {
                // Fire shortage notification (with a null entity InternalBasePtr)
                {
                    Object::InternalBasePtr nullIBP(nullptr);
                    notifMgr->AddEvent(i + 0x11, 3, &nullIBP);   // 0x140095580
                }
                // Reset timer to notification cooldown (120 seconds)
                {
                    System::Time newTime;
                    newTime._ticks = 0;
                    RtSystem_Time_Ctor(&newTime, notifyInterval);
                    *timer = newTime._ticks;
                }
            }
        }
    }

    // Part 2: Check resource notify entries (2 entries, food/firewood storage).
    // [DEREF FIX] +0x180 is a POINTER to the heap-allocated 2-entry array
    // (fn_SummaryArray_Init(self->data + 0x180, 2) in the ctor; original:
    // `lVar3 = *(longlong*)(param_1 + 0x180)`). The previous code used the field
    // address itself as the array, so "current" read the pointer's low dword
    // (huge -> 95%-full always true) and the "timer" writes stomped +0x188 â€”
    // that was the type-24 event spam (thousands of storage-full posts/minute).
    for (int i = 0; i < 2; i++) {
        ResourceNotifyEntry* notifyBase = notifyEntries._data;
        if (!notifyBase) break;
        int64_t* timer = &notifyBase[i].timer._ticks;

        // Decrement timer
        *timer -= deltaTime->_ticks;

        if (*timer < 1) {
            *timer = 0;

            int32_t current = notifyBase[i].current;
            int32_t maxCapacity = notifyBase[i].maxCapacity;

            // Check if storage is > 95% full: current > (max/20)*19
            if ((maxCapacity / 20) * 19 < current) {
                // Notification type: 0x17 for food (entry 0), 0x18 for firewood (entry 1)
                int notifType = 0x17 + (i != 0 ? 1 : 0);
                {
                    Object::InternalBasePtr nullIBP(nullptr);
                    notifMgr->AddEvent(notifType, 3, &nullIBP);   // 0x140095580
                }
                {
                    System::Time newTime;
                    newTime._ticks = 0;
                    RtSystem_Time_Ctor(&newTime, notifyInterval);
                    *timer = newTime._ticks;
                }
            }
        }
    }
}


// ============================================================
// CitizenBehavior::CollectStats (0x14008a730)
// ============================================================
// Iterates all citizens, collects population statistics (alive, dead,
// educated, working, idle, happiness, health averages), then pushes
// ~30+ stat values to the stats history arrays at +0x150.
//
// Stats history layout (at *(param_1+0x150)):
//   +0x00..+0x70: 8 population stats (each is a 0x10-byte sub-array struct)
//     +0x00: total citizen count
//     +0x10: adults (alive, age >= adult)
//     +0x20: dead citizens
//     +0x30: children (alive, age < adult)
//     +0x40: happiness percentage (0-100)
//     +0x50: educated percentage (0-100)
//     +0x60: health percentage (0-100)
//     +0x70: working percentage (0-100)
//   +0x80..+0x490: 21 profession entries, each 0x30 bytes with 3 sub-arrays:
//     +0x00: currentCount (profRes entry +0x00)
//     +0x10: field_0C (profRes entry +0x0C) — accumulated per-tick resource delta
//     +0x20: field_10 (profRes entry +0x10) — accumulated resource count
//
// After pushing all stats, clears profRes +0x0C and +0x10 for each of 21 entries.
//
// Original: FUN_14008a730
void CitizenBehavior::CollectStats() {
    // -- Phase 1: Iterate all citizens, accumulate counters --
    // Field names are CitizenComponent.h's (W0-H1). The tree's local names are kept
    // where the header names the field differently: `happinessSum` sums +0x50
    // (`health`), `healthSum` sums +0x80 (`happiness`), `dead` counts a non-null
    // +0x208 (`school`), `working` a non-zero +0x1E4 (`clothingLevel`).
    int citizenCount = this->citizens._size;
    Object::InternalPtr<CitizenComponent>* citizenArray = citizens._data;

    float happinessSum = 0.0f;
    float healthSum = 0.0f;
    int adults = 0;    // alive, age >= adult age
    int children = 0;  // alive, age < adult age
    int dead = 0;      // +0x208 != 0
    int educated = 0;  // education >= threshold AND adult AND alive
    int working = 0;   // +0x1E4 != 0

    for (int i = 0; i < citizenCount; i++) {
        CitizenComponent* c = citizenArray[i].Get();

        // Read citizen fields
        float age = c->age;
        float adultAge = c->description->adultAge;
        const bool atSchool = c->school != nullptr;

        // Classify: child (age < adult AND no +0x208), +0x208 set, or adult
        if (age < adultAge || atSchool) {
            if (!atSchool) {
                children++;
            } else {
                dead++;
            }
        } else {
            adults++;
        }

        // Accumulate +0x50 and +0x80
        happinessSum += c->health;
        healthSum += c->happiness;

        // Check educated: education level >= threshold AND adult AND +0x208 null
        int educThreshold = c->description->maxEducation;
        int educLevel = c->education;
        if (educLevel >= educThreshold) {
            float citizenAge = c->age;
            float citizenAdultAge = c->description->adultAge;
            if (citizenAdultAge <= citizenAge && c->school == nullptr) {
                educated++;
            }
        }

        // Check working: +0x1E4 != 0
        if (c->clothingLevel != 0) {
            working++;
        }
    }

    // -- Phase 1b: Compute percentage stats --
    // Uses first citizen's CitizenDescription for the two maxima.
    int happinessPct = 0;
    int healthPct = 0;
    int educatedPct = 0;
    int workingPct = 0;

    if (citizenCount > 0) {
        // Get CitizenDescription from first citizen's +0x18 via IsDerivedFrom.
        // Retail (0x14008a730): local_res8 = **(ulonglong**)(param_1+0xa0) â€”
        // the array base, ONE deref gives array[0] (the citizen), then its
        // Component +0x18 description.
        Description* descBase = citizens._data[0]->_description;
        CitizenDescription* citizenDesc = nullptr;
        if (::Object::IsDerivedFromNullSafe(descBase, CitizenDescription::TypeId())) {
            citizenDesc = static_cast<CitizenDescription*>(descBase);   // after the type check (B10)
        }

        // Read CitizenDescription+0xA4
        float maxHealth = 0.0f;
        if (citizenDesc) {
            maxHealth = citizenDesc->maxHappiness;
        }

        // Re-read first citizen for CitizenDescription+0x70 (same single-deref as above).
        descBase = citizens._data[0]->_description;
        citizenDesc = nullptr;
        if (::Object::IsDerivedFromNullSafe(descBase, CitizenDescription::TypeId())) {
            citizenDesc = static_cast<CitizenDescription*>(descBase);   // after the type check (B10)
        }

        float maxHappiness = 0.0f;
        if (citizenDesc) {
            maxHappiness = citizenDesc->maxHealth;
        }

        float fCount = static_cast<float>(citizenCount);
        // DAT_140172208 = 100.0f â€” scale to percentage
        if (maxHappiness != 0.0f) {
            happinessPct = static_cast<int>(
                ((happinessSum / fCount) / maxHappiness) * 100.0f);
        }
        if (maxHealth != 0.0f) {
            healthPct = static_cast<int>(
                ((healthSum / fCount) / maxHealth) * 100.0f);
        }
        educatedPct = EducatedPercent(educated, adults);
        workingPct = static_cast<int>(
            (static_cast<float>(working) / fCount) * 100.0f);
    }

    // -- Phase 2: Push profession resource stats to history --
    // Stats history at +0x150: each sub-array is a 0x10-byte struct
    //   { void* data, int count, int capacity }
    // 21 professions, 3 values each, at hist + 0x80 + profIdx*0x30 + {0, 0x10, 0x20}
    // The +0x150 data is an array of Collection::Array<int32_t> (0x10 each): [0..7]
    // the population series, then three series per profession entry.
    Collection::Array<int32_t>* hist = statHistory._data;

    for (int prof = 0; prof < 21; prof++) {
        Collection::Array<int32_t>* histSlot = &hist[8 + prof * 3];   // hist + 0x80 + prof * 0x30
        const ProfessionResourceEntry& profEntry = professionResources._data[prof];

        // Every push below is Collection::Array<T>::SetSize(n, fill) — B.3.2,
        // retail 0x140003600, stride 4 align 4, no init arm (every new slot takes
        // the fill): Array<int32_t>'s trait-derived spec is that row. Elements are
        // the int32 history samples.

        // Push currentCount (+0x00)
        int curArrayCount = histSlot[0].GetSize();
        histSlot[0].SetSize(curArrayCount + 1, profEntry.currentCount);

        // Push consumedCount (+0x0C)
        int f0CArrayCount = histSlot[1].GetSize();
        histSlot[1].SetSize(f0CArrayCount + 1, profEntry.consumedCount);

        // Push producedCount (+0x10)
        int f10ArrayCount = histSlot[2].GetSize();
        histSlot[2].SetSize(f10ArrayCount + 1, profEntry.producedCount);
    }

    // -- Phase 3: Push population stats --
    // 8 computed stats at hist + 0x00..0x70 (each 0x10-byte sub-array)
    // Same helper as Phase 2: Collection::Array<T>::SetSize(n, fill) — B.3.2,
    // retail 0x140003600, stride 4 align 4, no init arm (every new slot takes the fill).
    hist[0].SetSize(hist[0].GetSize() + 1, citizenCount);
    hist[1].SetSize(hist[1].GetSize() + 1, adults);
    hist[2].SetSize(hist[2].GetSize() + 1, dead);
    hist[3].SetSize(hist[3].GetSize() + 1, children);
    hist[4].SetSize(hist[4].GetSize() + 1, happinessPct);
    hist[6].SetSize(hist[6].GetSize() + 1, healthPct);
    hist[5].SetSize(hist[5].GetSize() + 1, educatedPct);
    hist[7].SetSize(hist[7].GetSize() + 1, workingPct);

    // -- Phase 4: Clear per-tick accumulators in profResources --
    // Clear +0x0C and +0x10 of each ProfessionResourceEntry
    for (int e = 0; e < 21; e++) {
        professionResources._data[e].consumedCount = 0;  // +0x0C of entry
        professionResources._data[e].producedCount = 0;    // +0x10 of entry
    }
}

// ============================================================
// The element rows of CitizenBehavior's hand-derived arrays (B4 X5-COMP)
// ============================================================
// Moved here from Game/citizenlist_serialize.cpp (and the second port of the
// same three addresses in Game/gameupdateloop_load.cpp, deleted): they are the
// rows of the class's own members now (CitizenBehaviorRows in the header). Each
// is one `Collection::Array<>::SetSize` over the erased core `rtcoll::SetSizeCore`
// (exact-equal early return, `_size` stored first, the treble on a single append
// to a non-empty array, copy the preserved prefix only when the block moved,
// init the new tail, destroy the truncated tail / the old buffer, then free).
namespace {

// FUN_14000f9e0 — the inner Array<int32> deep copy (operator=) that 0x140017640
// runs when the outer array reallocates. As retail: MemFree unconditionally, the
// size / capacity qword and the data cleared, size = the source's, a fresh block when
// non-empty (no allocator test), then the copy with only the destination SLOT's
// address tested (0x14000fa49) -- the source is read unguarded.
void DeepCopyInner04Array(Collection::Array<int32_t>* d, const Collection::Array<int32_t>* s) {
    if (d == s) return;
    System::MemoryAllocator::MemFree(d->_data);
    d->_size = 0;             // [d+8] qword 0 (H7: two stores)
    d->_capacity = 0;
    d->_data = nullptr;
    int32_t cnt = s->_size;
    d->_size = cnt;
    if (cnt > 0) {
        d->_capacity = cnt;
        d->_data = static_cast<int32_t*>(System::MemoryAllocator::MemAlloc(static_cast<uint64_t>(cnt) << 2, 4));
    }
    for (int32_t i = 0; i < d->_size; ++i) {
        int32_t* slot = &d->_data[i];
        if (slot != nullptr) *slot = s->_data[i];
    }
}

// 0x140017640 element: one inner Array<int32>, stride 0x10, align 8. Copy-construct
// = zero the header then the 0x14000f9e0 deep copy; init = zero the header;
// destroy = free the inner buffer and zero the header (retail's qword zero over
// +0x08..+0x0F is the two named int32 stores, H7).
void CopyIntAssocOuter(Collection::Array<int32_t>* d, const Collection::Array<int32_t>* s) {
    d->_data = nullptr;       // +0x00
    d->_size = 0;             // +0x08 / +0x0C (H7: two stores)
    d->_capacity = 0;
    DeepCopyInner04Array(d, s);
}
void ZeroIntAssocOuter(Collection::Array<int32_t>* d) {
    d->_data = nullptr;
    d->_size = 0;             // (H7)
    d->_capacity = 0;
}
void DestroyIntAssocOuters(Collection::Array<int32_t>* elems, int count) {
    for (int i = 0; i < count; ++i) {
        Collection::Array<int32_t>* e = &elems[i];
        System::MemoryAllocator::MemFree(e->_data);
        e->_data = nullptr;
        e->_size = 0;         // (H7)
        e->_capacity = 0;
    }
}
void AssignIntAssocOuter(uint8_t* d, const uint8_t* s) {
    rtcoll::AssignAs<Collection::Array<int32_t>, &CopyIntAssocOuter>(d, s);
}
void InitIntAssocOuter(uint8_t* d) {
    rtcoll::InitAs<Collection::Array<int32_t>, &ZeroIntAssocOuter>(d);
}

// 0x1400177c0 element: ProfessionResourceEntry, stride 0x20, align 8. Copy = the
// whole stride as four qwords; init = {0, 100, 0, 0} written as u64@0, u64@8,
// u32@0x10, u64@0x18, so +0x14..+0x17 is left untouched; trivially destructible.
void ZeroProfResEntry(ProfessionResourceEntry* e) {
    e->currentCount  = 0;       // +0x00 / +0x04: retail's u64 0 (H7: two stores)
    e->previousCount = 0;
    e->maxCount      = 100;     // +0x08 / +0x0C: retail's u64 100 (H7: two stores)
    e->consumedCount    = 0;
    e->producedCount      = 0;       // +0x10 u32; +0x14 left untouched
    e->timer._ticks  = 0;       // +0x18 u64
}
void AssignProfResEntry(uint8_t* d, const uint8_t* s) {
    rtcoll::AssignAs<uint64_t, &rtcoll::CopyWords<uint64_t, 4>>(d, s);   // the whole stride, four qwords
}
void InitProfResEntry(uint8_t* d) {
    rtcoll::InitAs<ProfessionResourceEntry, &ZeroProfResEntry>(d);
}

// 0x140017f60 element: WaterCellPair, stride 8, align 4. Copy = one qword (retail
// moves the pair as one qword: the word-wise adapter at retail's width), init =
// zero, trivially destructible.
void ZeroWord8(uint64_t* w) { *w = 0; }
void AssignIntPair(uint8_t* d, const uint8_t* s) {
    rtcoll::AssignAs<uint64_t, &rtcoll::CopyWords<uint64_t, 1>>(d, s);
}
void InitIntPair(uint8_t* d) { rtcoll::InitAs<uint64_t, &ZeroWord8>(d); }

}  // namespace

namespace CitizenBehaviorRows {
const rtcoll::ArraySpec kSpec_IntAssocOuter = {          // exe 0x140017640
    "Array<Array<int32>>", 0x10, 8,
    &AssignIntAssocOuter, &InitIntAssocOuter, &rtcoll::DestroyAs<Collection::Array<int32_t>, &DestroyIntAssocOuters>, false,
    /*assign unwritten*/ 0, 0,
    /*init   unwritten*/ 0, 0,
    /*seed zero       */ 0, 0 };
const rtcoll::ArraySpec kSpec_ProfResEntry = {           // exe 0x1400177c0
    "Array<ProfessionResourceEntry>", 0x20, 8,
    &AssignProfResEntry, &InitProfResEntry, nullptr, true,
    /*assign unwritten*/ 0,    0,
    /*init   unwritten*/ 0x14, 0x18,
    /*seed zero       */ 0,    0 };
const rtcoll::ArraySpec kSpec_IntPair = {                // exe 0x140017f60
    "Array<{int32,int32}>", 8, 4,
    &AssignIntPair, &InitIntPair, nullptr, true,
    /*assign unwritten*/ 0, 0,
    /*init   unwritten*/ 0, 0,
    /*seed zero       */ 0, 0 };
}  // namespace CitizenBehaviorRows

// ============================================================
// CitizenBehavior::CitizenBehavior (0x140090a30)
// ============================================================
// The members' ctors are retail's first stores: each Array's data qword and its
// size + capacity qword (0x140090a4d..0x140090af5, +0x110..+0x158, +0x170 /
// +0x180), each Set's root qword and dword count (+0x100 at 0x140090b08 /
// 0x140090b0b, +0x160 at 0x140090b60 / 0x140090b63). The body is the rest.
CitizenBehavior::CitizenBehavior() {
    pathGraphTimer._ticks = 0;                   // 0x140090b82
    statsTimer._ticks = 0;                   // 0x140090b89
    diseaseRiskTimer._ticks = 0;                   // 0x140090b90
    totalTradeValue = 0;                          // 0x140090b97: one qword store over +0x1AC / +0x1B0 (H7: two stores)
    totalNomadsAccepted = 0;
    professionResources.SetSize(0x15);        // 0x1400177c0
    ProfessionResourceEntry* prices = professionResources._data;
    if (prices) {
        static const int32_t kDefaultPrices[21] = {
            200, 200, 200, 200, 50, 5000, 100, 50, 500, 200, 200,
            1000, 1000, 1000, 1000, 1000, 1000, 1000, 1000, 1000, 1000
        };
        for (int i = 0; i < 21; ++i)
            prices[i].maxCount = kDefaultPrices[i];
    }
    notifyEntries.SetSize(2);                 // 0x14000f090
    statHistory.SetSize(0x47);                // 0x140017640
    diseaseRisk = 0.0f;                       // retail's dword store of 0 (the same bits)
    PathGraph* sub = static_cast<PathGraph*>(System::MemoryAllocator::MemAlloc(0x28, 4));
    if (sub) {
        sub->m_pairs = nullptr;
        sub->m_pairCount = 0;                 // +0x08 / +0x0C: one qword store (H7: two stores)
        sub->m_pairCapacity = 0;
        sub->m_pendingIdx = -1;
        sub->m_bst.root = nullptr;
        sub->m_bst.count = 0;
        PathCostIndex::Clear(sub);   // NOT RETAIL (HITCH_PAIRSPREAD): the m_bst map starts empty
    }
    pathGraph = sub;
}

// ============================================================
// CitizenBehavior::~CitizenBehavior (0x1400907b0)
// ============================================================
// The body is retail's first step: the PathGraph's members (0x1400171b0), then
// its block (pathGraph is not zeroed). The members are then destroyed in
// reverse declaration order, which is retail's sequence from 0x1400907e9 on.
CitizenBehavior::~CitizenBehavior() {
    if (pathGraph) {
        ArrayPlusTree_Clear_171b0(pathGraph);
        System::MemoryAllocator::MemFree(pathGraph);
    }
}
