// src/game/Component/citizen_assign_citizens.cpp
//
// Full faithful port of CitizenComponent_impl_43b0 (0x1400d43b0) — the town-wide
// EMPLOYMENT ALLOCATOR. Runs once per town tick, reached via
//   CitizenBehavior_Update (0x14008c2b0)
//     -> ProcessPaths (FUN_1400d6f00, resolves BuildStructure paths first)
//       -> CitizenComponent_impl_43b0(ctx)   <-- THIS function
//       -> FUN_140025b30 (clear the path pairs scratch array)
//
// The single argument `ctx` is the PathGraph context. Its only field this function
// touches is the region-pair cost tree at ctx+0x18 that ProcessPaths (0x1400d6f00) filled
// over the preceding ticks; every cost below is read from it through sub_6060
// (PathGraph::RegionPairCost, 0x1400d6060), which also stores a pair it does not find.
//
// ============================================================================
// WHAT IT DOES (6 phases)
//   The retail function is a ~1351-line inlined-STL allocator building four
//   red-black/AVL scratch containers. Nothing outside the function reads their
//   memory layout, so this port represents them as clean std:: containers with the
//   same comparators and the same end taken. The idle set, the profession-count map
//   and the cost trees' ties key on raw pointers (CitizenComponent*, Profession*), so
//   their order follows heap addresses, in retail as here; fix.alloc_tiebreak puts a
//   stable id in front of each pointer (Ext::AllocTiebreak::TieKey, 0 with the row off,
//   docs/retail_bugs.md §14). The priority queue does not: it keys on the need RECORD's address, and the records sit in one array in
//   Phase 2 order, so its tie-break is the need index (Component/need_queue.h).
//   Every interaction with real game state (citizen/townMgr/reg/workplace fields)
//   and every leaf helper is called wholesale and byte-faithful.
//
//   Phase 1  Housing-sync: for each idle+healthy citizen already assigned to a
//            workplace (+0x248) whose required profession (workplaceComp+0x48) no
//            longer matches the citizen's profession (+0x1f0), unassign it —
//            AssignHousing(citizen, null) + set +0x1f0 = default key (reg+0x30).
//   Phase 2  Need-table: for every profession in the registry (reg+0x20 array,
//            reg+0x28 count), look it up in the town workplace map (townMgr+0x100)
//            to get the list of workplace entities for that profession; for each
//            that has a WorkPlaceComponent whose LIVE WORKER ROSTER is non-empty
//            (+0x60 != 0) push NeedEntry{workplaceComp, cellId,
//            remaining=rosterCount, rosterCount=rosterCount, costTree}.
//
//            *** +0x60 IS THE LIVE ROSTER COUNT, NOT THE CAPACITY. ***
//            Retail reads it once and stores it as BOTH the "remaining" and the
//            denominator (0x1400d43b0: `if (*(int *)(local_res20 + 0x60) != 0)`
//            then `local_8c = local_88 = *(local_res20 + 0x60)`). The real capacity
//            lives on the sibling WorkComponent at +0x30 (lazily filled by
//            CalcMaxWorkers 0x14012e5c0) and this function never reads it — see
//            FindAvailableBuilding 0x14008d100, which compares WPC+0x60 < WC+0x30.
//
//            CONSEQUENCE, and the reason this comment is shouting: the `!= 0` gate
//            means the allocator SKIPS EVERY WORKPLACE WITH ZERO SEATED WORKERS. It
//            can only rebalance and refill workplaces that already have at least one
//            worker; it structurally CANNOT seat worker #1 anywhere. In retail the
//            only thing that seats the first worker at a building is the player, via
//            the profession spinner (TownHallUI::OnProfessionSpinner 0x14007dd10 /
//            ProfessionUI 0x1400763c0 -> FindAvailableBuilding + the
//            cbf0/ca20/c5f0/c4b0 cascade). Do not plan around "the allocator will
//            staff it" — it will not. builder_pool_driver.cpp has this right.
//   Phase 3  Idle pass: for each idle+healthy citizen — normalize (same as phase 1),
//            insert into the idle set (NOT laborer-filtered — retail's gate is just
//            adult && no-active-action; an employed citizen standing idle is in it
//            too, verified against 0x1400d43b0), bump the per-profession count map
//            (keyed by +0x1f0), and add a {cost,citizen} candidate (cost from
//            sub_6060) to every NeedEntry's cost-tree.
//   Phase 4  Priority: score each NeedEntry = (int)(minCost * (uvScale - filled)),
//            filled = (rosterCount-remaining)/rosterCount; insert into a max-
//            priority queue, an AVL set keyed (score, need record): highest score
//            first, the lower need index first on a tie.
//   Phase 5A Assign loop: while the queue is non-empty, pop the highest-score need,
//            take its nearest available candidate (min cost), assign the citizen —
//            AssignHousing(citizen, workplaceEntity) + set +0x1f0 = reqProf — then
//            remove the citizen from the idle set, decrement its profession count,
//            drop that citizen from EVERY need's cost-tree, decrement the need's
//            remaining, and re-score any need whose best candidate changed.
//   Phase 5B Cleanup loop: drain the leftover profession-count map, clearing the
//            workplace (AssignHousing null) of each still-idle laborer.
//
// FIELD MAP (verified against Application-x64.exe in Ghidra; see below)
//   gameWorld (*g_pGameWorldSingleton)
//     +0xb0  profession registry   +0xf0  townManager   +0xf8  pathData(int*)
//   registry (reg)
//     +0x20  professionId array (ptr)   +0x28  count(int)   +0x30  default key
//   townManager
//     +0xa0  citizen array (ptr)   +0xa8  citizen count(int)
//     +0x100 workplace map root (key=professionId; node +0x00 key, +0x08 list,
//            +0x10 count, +0x20 left, +0x28 right; list = array of workplace IBPs)
//   citizen (CitizenComponent)
//     +0x3c   health value(float)     +0x1f0 profession key
//     +0x208  busy/action ptr         +0x238 residence entity IBP (source cell)
//     +0x248  workplace-assign IBP    +0x288 health component (+0x44 threshold)
//   workplaceComponent (the c80e0 component FindWorkplaceComponent returns)
//     +0x10   owner entity IBP        +0x48  required profession key
//     +0x58   worker IBP array        +0x60  LIVE WORKER COUNT (not capacity —
//                                            capacity is WorkComponent+0x30)
//
//   AssignHousing (0x140111ee0) writes citizen+0x248: detaches from the old
//   workplace's worker roster (FUN_14012f7e0) and attaches to the new (FUN_14012fa20).
//
// LEAF HELPERS called wholesale (resolved via ResolveAddr(Module::Exe, ...)):
//   sub_6060  0x1400d6060  path-cost record builder; fills {citizen, fromCell,
//             toCell, cost}; the cost comes from the region-pair tree at ctx+0x18
//             (PathGraph::RegionPairCost, below).
//   AssignHousing        0x140111ee0
//   FindWorkplaceComponent 0x140002e10  (member ABI: RCX=entity, RDX=out IBP)
//   Entity_impl_7fb0     0x140087fb0  grid cell id of an entity (Position component)
//   g_fUVScale           0x14017214c  (float const in the score formula)
//
// WIRING NOTE: replaces the docs/history/notes/ scoped stand-in (construction-only Laborer->Builder).
// Registration is in place: RegisterCitizenAssignCitizens() maps 0x1400d43b0 to this
// reimpl (standalone/address_registry.cpp). It is a direct (non-vtable) call, so it only
// executes once a reimplemented caller reaches it — the employment tick FUN_1400d6f00
// (+ its ctx) is the remaining dependency; until that caller is ported it stays dormant.

#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <map>
#include <set>
#include <vector>
#include "Component/exe_bodies.h"
#include "diag/stress_harness.h"   // g_pfProfOn, g_pathGraphCost
#include "Action/exe_bodies.h"
#include "Work/exe_bodies.h"
#include "Game/exe_constants.h"
#include "Game/Map.h"                       // Map::size / cells / PathDistance
#include "Game/ProfessionList.h"            // professions (+0x20), field_0x30 default
#include "Entity/CitizenBehavior.h"         // citizens (+0xA0), field_0x100 bucket tree
#include "Entity/ComponentEntity.h"         // FindComponent<T>
#include "Component/CitizenComponent.h"
#include "Component/CitizenDescription.h"
#include "Component/WorkPlaceComponent.h"
#include "Component/InteractComponent.h"   // HomeNodeCell: NodePosition (0x140122e20)
#include "Action/PathAvl.h"                  // PathCostTreeInsert (0x140025a10)
#include "Component/need_queue.h"             // NeedQueue: the Phase 4 priority queue
#include "ext/registry.h"                     // Ext::Fix().alloc_tiebreak
#include "ext/alloc_tiebreak/alloc_tiebreak.h"   // EntityKey, ProfessionOrder
#include "ext/alloc_tiebreak/tie_key.h"       // TieKey: the scratch trees' keys
#include "ext/commute_cost/commute_cost_rule.h"   // fix.commute_cost: RegionPairCost's not-found answer

namespace {

static const int kUnreachable = 0x1000000;

// The port's former stand-in for sub_6060: the exact per-cell PathDistance cost, with no
// tree. Retail answers from the region-pair tree instead, so this differs whenever two
// cell pairs share a region pair (same-region pairs above all, whose Euclidean fallback
// is per cell). Kept ONLY for the COMPPROF=4 [Stress::pathgraph] `differ` count, which
// says how far the old answers were from retail's; nothing in the game calls it.
int ExactCellCost(Map* map, int fromCell, int toCell) {
    if (fromCell < 0 || toCell < 0) return kUnreachable;
    if (!static_cast<bool>(static_cast<uint8_t>(
            map->ValidateCell(0xe1f, static_cast<uint32_t>(fromCell),
                                        static_cast<int>(static_cast<uint32_t>(toCell))))))
        return kUnreachable;
    int W = map->size;
    if (W <= 0) return kUnreachable;
    auto euclid = [&]() -> int {
        int dx = fromCell / W - toCell / W;
        int dy = fromCell % W - toCell % W;
        int d = static_cast<int>(std::sqrt(static_cast<float>(dx * dx + dy * dy)));
        return d < 0 ? kUnreachable : d;
    };
    MapCell* nav = map->cells.GetData();
    int regionA = nav[fromCell].regionId;
    int regionB = nav[toCell].regionId;
    if (regionA < 0 || regionB < 0) return euclid();
    int cost = map->PathDistance(static_cast<uint32_t>(fromCell),
                                 static_cast<uint32_t>(toCell), 0);   // 0x14009f810
    if (cost < 2) cost = euclid();
    return cost;
}

// The citizen's source cell: its home's InteractComponent node 0, inline in retail
// (0x1400d5390-0x1400d5477: FindComponent<InteractComponent>, NodePosition(0, 0), the
// half-map offset, truncate, clamp). NOT EntityAnchorCell (0x140087fb0), whose multi-node
// arm draws a random number; retail draws none here. The component is read with no test.
int HomeNodeCell(ComponentEntity* home, Map* map) {
    InteractComponent* interact = home->FindComponent<InteractComponent>();   // 0x1400d53a4 scan
    Math::Vector3 pos;
    interact->NodePosition(&pos, 0, 0, nullptr);   // 0x1400d542d
    const int w = map->size;
    const float half = static_cast<float>(w >> 1);
    int gx = static_cast<int>(half + pos.x);
    int gy = static_cast<int>(half + pos.y);
    if (gx < 0) gx = 0;
    if (gx >= w) gx = w - 1;
    if (gy < 0) gy = 0;
    if (gy >= w) gy = w - 1;
    return w * gy + gx;
}


// Read a citizen out of the townMgr citizen array, resolving the lazy IBP.
inline CitizenComponent* CitizenAt(Object::InternalPtr<CitizenComponent>* citArray, int i) {
    CitizenComponent* c = citArray[i].Get();
    return c;
}

// idle + healthy + not-busy gate (identical in phase 1 and phase 3): 0x1400d444d /
// 0x1400d4b9d read +0x288 with no test (B8 RES, row 310).
inline bool IsIdleHealthy(CitizenComponent* c) {
    CitizenDescription* healthComp = c->description;   // +0x288
    if (healthComp->adultAge > c->age)                 // desc +0x44 vs +0x3c
        return false;                                   // sick/starving
    if (c->school != nullptr) return false;             // +0x208 busy in an action
    return true;
}

// Find the workplace component inside an assigned entity (+0x248 value).
// (0x140002e10, the tree's AttendanceComponent_GetJobHostComponent, is this type scan.)
// Every caller hands a non-null entity; retail tests none (B8 RES, row 310).
inline WorkPlaceComponent* FindWorkplaceComp(ComponentEntity* entity) {
    WorkPlaceComponent* comp = entity->FindComponent<WorkPlaceComponent>();   // 0x140002e10
    return comp;
}

// Grid cell id of an entity (its Position component); 0x140087fb0 on a non-null
// entity: Phase 2's workplace (0x1400d4859). Phase 3's home cell is HomeNodeCell.
inline int CellOfEntity(ComponentEntity* entity) {
    ComponentEntity* raw = entity;
    return EntityAnchorCell(&raw, nullptr);   // 0x140087fb0
}

// Phase-1/3 normalization: an idle citizen whose current workplace requires a
// profession different from its own is unassigned back to the default laborer key.
void NormalizeMismatched(CitizenComponent* c, ProfessionList* reg) {
    ComponentEntity* assigned = c->workplace.Get();   // +0x248
    if (!assigned) return;
    WorkPlaceComponent* wpComp = FindWorkplaceComp(assigned);
    Profession* reqProf = wpComp->profession;   // +0x48
    Profession* curProf = c->profession;        // +0x1f0

    // A building can carry MORE THAN ONE WorkPlaceComponent — a construction workplace
    // (so citizens can be assigned to build it) plus the production one. FindWorkplaceComp
    // returns whichever the type scan hits FIRST, so if that is the construction seat the
    // comparison below is against the wrong profession and a correctly-seated worker gets
    // reset to laborer.

    if (reqProf == curProf) return;                     // already matches

    Profession* defaultKey = reg->field_0x30;   // +0x30
    // skip if already reset (default profession AND no workplace)
    if (curProf == defaultKey && c->workplace == nullptr) return;

    c->SetWorkplace(nullptr);   // 0x140111ee0: clear +0x248
    c->profession = defaultKey;
}

// Walk the town workplace map (townMgr+0x100), keyed by profession id.
// Returns {listPtr, count} for `key`, or {0,0} if absent.
struct WorkplaceList { Object::InternalPtr<ComponentEntity>* list; int count; };
WorkplaceList LookupWorkplaces(ProfessionBucket* mapRoot, Profession* key) {
    ProfessionBucket* node = mapRoot;
    while (node) {
        if (key < node->element.key)       node = node->left;    // +0x20
        else if (key > node->element.key)  node = node->right;   // +0x28
        else return { node->element.entities._data,                    // list @ +0x08
                      node->element.entities._size };                     // count @ +0x10
    }
    return { nullptr, 0 };
}

// ---- scratch containers ----------------------------------------------------
using IdleKey = Ext::AllocTiebreak::TieKey<CitizenComponent>;
using ProfKey = Ext::AllocTiebreak::TieKey<Profession>;

struct Candidate {
    int32_t cost;
    int32_t tie;   // the citizen's TieKey id: 0 unless fix.alloc_tiebreak
    CitizenComponent* citizen;
    int32_t fromCell;
    int32_t toCell;
    // Ordered by (cost, citizen) — matches the retail cost-tree comparator; the
    // nearest laborer is begin() (min cost), the priority score reads begin()->cost.
    // `tie` sits between the two and is 0 for every candidate with the row off.
    bool operator<(const Candidate& o) const {
        if (cost != o.cost) return cost < o.cost;
        if (tie != o.tie) return tie < o.tie;
        return citizen < o.citizen;
    }
    IdleKey Key() const { return IdleKey{ tie, citizen }; }
};

struct NeedEntry {
    WorkPlaceComponent* workplaceComp;   // c80e0 component (owner entity @ +0x10, reqProf @ +0x48)
    int   cellId;          // workplace grid cell
    int   remaining;       // seats still to fill this pass (starts = rosterCount)
    // WPC+0x60 = the workplace's LIVE WORKER COUNT, not its capacity. Retail uses it
    // as the denominator anyway (it reads +0x60 once into both slots), so this is
    // faithful — but it is why a workplace with an empty roster is never a "need":
    // Phase 2 skips rosterCount == 0. See the shouting block in the file header.
    int   rosterCount;
    // The need's cost tree (retail: an AVL of {cost, citizen} records, in order of
    // (cost, citizen)) as a vector sorted once after Phase 3 by the same strict order,
    // with LAZY removal: an assigned citizen leaves EVERY need at once, so a need's
    // live candidates are the sorted run from `head` on, minus citizens no longer in
    // the idle set. `head` is kept on a live candidate for every need that still has
    // seats (Advance, below), so best() is retail's leftmost node and empty() its
    // empty tree. (HITCH_PATHGRAPH: the multiset + linear DropCitizen it replaces cost
    // ~3.7 ms per drain at 800 citizens; the old loop is kept as the COMPPROF=4 shadow.)
    std::vector<Candidate> candidates;
    size_t head = 0;

    bool empty() const { return head >= candidates.size(); }
    const Candidate& best() const { return candidates[head]; }
    // Step head past every candidate whose citizen has left the idle set.
    void Advance(const std::set<IdleKey>& idle) {
        while (head < candidates.size() && idle.count(candidates[head].Key()) == 0) ++head;
    }

    // Read with no test, as retail (0x1400d5530, 0x1400d5d36): the tree is never empty
    // while the need has seats (the invariant above AssignCitizens), and Phase 2 keeps
    // only rosterCount != 0.
    int score(float uvScale) const {
        int minCost = best().cost;
        float filled = static_cast<float>(rosterCount - remaining) / static_cast<float>(rosterCount);
        return static_cast<int>(static_cast<float>(minCost) * (uvScale - filled));
    }
};

// ---- COMPPROF=4 shadow: the previous multiset allocator loop ------------------
// Replays Phase 4 + 5A's SELECTION on the old containers (a multiset per need, the
// assigned citizen erased from every need with seats left, linear) and returns the
// (need, citizen) sequence; the selection reads only costs, scores and seats, so it
// needs none of the real loop's side effects. The real loop's sequence must match.
struct ShadowNeed {
    int remaining, rosterCount;
    std::multiset<Candidate> candidates;
    int score(float uvScale) const {
        if (candidates.empty() || rosterCount <= 0) return 0;
        int minCost = candidates.begin()->cost;
        float filled = static_cast<float>(rosterCount - remaining) / static_cast<float>(rosterCount);
        return static_cast<int>(static_cast<float>(minCost) * (uvScale - filled));
    }
};
std::vector<std::pair<size_t, CitizenComponent*>> ShadowAssignSequence(const std::vector<NeedEntry>& needs,
                                                                     float uvScale) {
    std::vector<ShadowNeed> sh(needs.size());
    for (size_t n = 0; n < needs.size(); ++n) {
        sh[n].remaining = needs[n].remaining;
        sh[n].rosterCount = needs[n].rosterCount;
        sh[n].candidates.insert(needs[n].candidates.begin(), needs[n].candidates.end());
    }
    NeedQueue::Queue pq;
    for (size_t n = 0; n < sh.size(); ++n) {
        if (sh[n].candidates.empty() || sh[n].remaining <= 0) continue;
        pq.insert(NeedQueue::Key{ sh[n].score(uvScale), n });
    }
    std::vector<std::pair<size_t, CitizenComponent*>> seq;
    while (!pq.empty()) {
        size_t ni = pq.begin()->need;
        pq.erase(pq.begin());
        ShadowNeed& e = sh[ni];
        if (e.candidates.empty() || e.remaining <= 0) continue;
        CitizenComponent* citizen = e.candidates.begin()->citizen;
        seq.emplace_back(ni, citizen);
        e.remaining -= 1;
        for (size_t f = 0; f < sh.size(); ++f) {
            ShadowNeed& nf = sh[f];
            if (nf.remaining <= 0) continue;
            const int oldScore = nf.score(uvScale);
            bool wasBest = false;
            for (auto it = nf.candidates.begin(); it != nf.candidates.end(); ++it) {
                if (it->citizen == citizen) {
                    wasBest = (it == nf.candidates.begin());
                    nf.candidates.erase(it);
                    break;
                }
            }
            if (wasBest) {
                pq.erase(NeedQueue::Key{ oldScore, f });
                if (!nf.candidates.empty() && nf.remaining > 0)
                    pq.insert(NeedQueue::Key{ nf.score(uvScale), f });
            }
        }
    }
    return seq;
}

}  // namespace

// Build-time roster-only guard (workplacecomponent.cpp). When set, the WPC worker
// attach/detach helpers (fa20/f7e0, reached through AssignHousing 0x140111ee0) skip
// the Graphics::Scene mesh-visual toggle (Scene::Add/RemoveObject on gw+0x60 +
// FUN_1400c3470 re-bucket + component-event broadcast) down to the bare roster-list
// mutation. This allocator runs every town tick once townMgr+0x100 has any employer
// workplace (e.g. a registered construction site), and its Phase 1/3/5A/5B AssignHousing
// churn is O(idle-laborers) per tick — the exact scene churn that deadlocks. We hold the
// guard across the whole allocation body so none of those AssignHousing calls touch the
// scene. Verified sufficient in Ghidra (see reference_builder_profession_bst_flow).
extern bool g_wpcRosterOnly;

// ===========================================================================
// PathGraph::RegionPairCost (0x1400d6060)
// ===========================================================================
// Retail's record builder, minus the record: the cost from fromCell to toCell.
//   fromCell < 0, or the two cells not mutually reachable (ValidateCell 0xe1f) -> 0x1000000
//   either cell outside every region -> the Euclidean grid distance (0x1000000 if negative)
//   else the region pair (max, min) in m_bst:
//     found     -> the stored cost (0x1000000 if negative)
//     not found -> CalcDistance (0x140008830); 0 or 1 -> Euclidean; stored in the tree
//                  (PathCostTreeInsert 0x140025a10) and the query returns 0: the returned
//                  cost is R15D, zeroed at 0x1400d60d2 and written only on the found arm
//                  (0x1400d6213), so a miss reports 0 and only later queries of that pair
//                  see the stored value. ProcessPaths has filled every residence x
//                  workplace region pair before AssignCitizens runs, so a miss needs a
//                  pair the rebuild did not list (a region or a building that changed
//                  while the pairs drained). docs/retail_bugs.md.
int PathGraph::RegionPairCost(Map* map, int fromCell, int toCell) {
    if (fromCell < 0) return kUnreachable;
    if (!static_cast<uint8_t>(map->ValidateCell(0xe1f, static_cast<uint32_t>(fromCell), toCell)))
        return kUnreachable;

    MapCell* cells = map->cells.GetData();   // +0x80
    const int regionA = cells[fromCell].regionId;
    const int regionB = cells[toCell].regionId;
    auto euclid = [&]() -> int {
        const int w = map->size;
        const int dx = fromCell / w - toCell / w;
        const int dy = fromCell % w - toCell % w;
        return static_cast<int>(sqrtf(static_cast<float>(dx * dx + dy * dy)));   // 0x140132010
    };
    if (regionA < 0 || regionB < 0) {
        if (g_pfProfOn) ++g_pathGraphCost.direct;
        const int d = euclid();
        return d < 0 ? kUnreachable : d;
    }

    const int keyHigh = regionA < regionB ? regionB : regionA;
    const int keyLow  = regionA < regionB ? regionA : regionB;
    if (const int32_t* slot = CostFind(keyHigh, keyLow)) {   // the m_bst map (HITCH_PAIRSPREAD)
        if (g_pfProfOn) ++g_pathGraphCost.hits;
        const int cost = *slot;
        return cost < 0 ? kUnreachable : cost;
    }

    if (g_pfProfOn) ++g_pathGraphCost.misses;
    uint32_t cost = static_cast<uint32_t>(map->CalcDistance(static_cast<uint32_t>(fromCell),
                                                            static_cast<uint32_t>(toCell)));   // 0x140008830
    if (cost <= 1) cost = static_cast<uint32_t>(euclid());   // 0x1400d6152 CMP EAX,1 / JA
    CostAdd(keyHigh, keyLow, static_cast<int32_t>(cost));   // PathCostTreeInsert 0x140025a10, {high, low, cost}
    // FIX commute_cost (NOT RETAIL, docs/retail_bugs.md §6): the stored cost instead of retail's 0.
    return Ext::CommuteCost::MissCost(Ext::Fix().commute_cost, static_cast<int32_t>(cost));
}

// ===========================================================================
// CitizenComponent_impl_43b0 (0x1400d43b0)
// ===========================================================================
// Retail reads a need's best candidate with no empty test: in the Phase 4 scoring
// (0x1400d5530), at the pop, and in the re-score after a removal (0x1400d5d36, where
// an emptied tree would be read at null+0x10). That is safe by construction, and the
// port relies on the same invariant: every seat counted in a roster (+0x60) is held by
// a distinct adult citizen with no school, i.e. one Phase 3 puts in `idle` and in every
// need's tree, so the seats still open never outnumber the idle citizens. Each
// assignment takes one of each, so every need with seats left has a live candidate.
void PathGraph::AssignCitizens() {
    // 0x1400d43df-0x1400d4403: the world, the town (+0xf0), the map (+0xf8) and the
    // registry (+0xb0) are read with no test (B8 RES, row 310).
    // this (the PathGraph) is the region-pair cost tree every cost below is read from
    // (RegionPairCost, 0x1400d6060 at 0x1400d54c4).

    GameUpdateLoop*  gw       = GameUpdateLoop::_singleton;
    ProfessionList*  reg      = gw->m_professionList;   // +0xb0
    CitizenBehavior* townMgr  = gw->m_citizenList;      // +0xf0
    Map*             pathData = gw->m_game;             // +0xf8

    Object::InternalPtr<CitizenComponent>* citArray = townMgr->citizens._data;      // +0xa0
    int                citCount = townMgr->citizens._size;  // +0xa8
    ProfessionBucket*  mapRoot  = townMgr->field_0x100._root;   // +0x100
    float     uvScale  = kRdata_14017214c;   // g_fUVScale 0x14017214c

    // Hold the roster-only guard for every AssignHousing call below (no early returns
    // remain past this point). Restored just before the trace/return.
    bool prevRoster = g_wpcRosterOnly;
    g_wpcRosterOnly = true;

    // ---------- Phase 1: housing-sync ----------
    for (int i = 0; i < citCount; ++i) {
        CitizenComponent* c = CitizenAt(citArray, i);
        if (!IsIdleHealthy(c)) continue;
        NormalizeMismatched(c, reg);
    }

    // ---------- Phase 2: need-table ----------
    std::vector<NeedEntry> needs;
    Profession** profArray = reg->professions.GetData();   // +0x20
    int          profCount = reg->professions.GetSize();   // +0x28
    for (int p = 0; p < profCount; ++p) {
        Profession* key = profArray[p];
        WorkplaceList wl = LookupWorkplaces(mapRoot, key);
        for (int j = 0; j < wl.count; ++j) {
            ComponentEntity* entity = wl.list[j].Get();
            WorkPlaceComponent* wpComp = FindWorkplaceComp(entity);   // [+0x60] read untested, 0x1400d483c
            // +0x60 = LIVE WORKER COUNT (capacity is WorkComponent+0x30, never read
            // here). The `== 0` skip is what makes this allocator a rebalancer only:
            // an unstaffed workplace is invisible to it. Faithful to retail.
            int rosterCount = wpComp->workers._size;   // +0x60
            if (rosterCount == 0) continue;
            NeedEntry e;
            e.workplaceComp = wpComp;
            e.cellId       = CellOfEntity(entity);
            e.remaining    = rosterCount;
            e.rosterCount  = rosterCount;
            needs.push_back(std::move(e));
        }
    }

    // ---------- Phase 3: idle set + profession counts + candidates ----------
    // FIX alloc_tiebreak (NOT RETAIL, docs/extensions/handins/ALLOCTIEBREAK.md): the idle set,
    // the profession-count map and the cost trees order equal keys by entity id / profession
    // index instead of by address. Off: every id is 0 and the order is the pointer's.
    const bool stableTies = Ext::Fix().alloc_tiebreak;
    const Ext::AllocTiebreak::ProfessionOrder profOrder(reg);
    auto profKey = [&](Profession* p) { return ProfKey{ stableTies ? profOrder.Rank(p) : 0, p }; };

    std::set<IdleKey> idle;
    std::map<ProfKey, int> profNeed;    // profession key -> idle-laborer count
    for (int i = 0; i < citCount; ++i) {
        CitizenComponent* c = CitizenAt(citArray, i);
        if (!IsIdleHealthy(c)) continue;

        const int32_t tie = stableTies ? Ext::AllocTiebreak::EntityKey(c) : 0;
        idle.insert(IdleKey{ tie, c });
        Profession* prof = c->profession;   // +0x1f0
        ++profNeed[profKey(prof)];

        NormalizeMismatched(c, reg);    // embedded normalization (mirrors LAB_1400d4e57)

        // source cell = the citizen's residence (+0x238) node-0 cell, or -1
        ComponentEntity* residence = c->home.Get();   // +0x238
        int srcCell = residence ? HomeNodeCell(residence, pathData) : -1;

        for (auto& e : needs) {
            if (e.candidates.empty()) e.candidates.reserve(static_cast<size_t>(citCount));
            int cost = RegionPairCost(pathData, srcCell, e.cellId);   // 0x1400d6060
            if (g_pfProfOn) {
                ++g_pathGraphCost.calls;
                if (g_verifyOn) {
                    if (g_pathGraphCost.differ < 0) g_pathGraphCost.differ = 0;
                    if (ExactCellCost(pathData, srcCell, e.cellId) != cost) ++g_pathGraphCost.differ;
                }
            }
            e.candidates.push_back(Candidate{ cost, tie, c, srcCell, e.cellId });
        }
    }
    // The (cost, citizen) order is strict (one candidate per citizen per need), so the
    // sorted vector is the tree's in-order sequence.
    for (auto& e : needs) std::sort(e.candidates.begin(), e.candidates.end());

    // ---------- Phase 4: max-priority queue ----------
    // (score desc, need index asc); begin() is retail's pop (Component/need_queue.h).
    NeedQueue::Queue pq;
    for (size_t n = 0; n < needs.size(); ++n) {
        pq.insert(NeedQueue::Key{ needs[n].score(uvScale), n });
    }
    std::vector<std::pair<size_t, CitizenComponent*>> shadowSeq, realSeq;
    if (g_verifyOn) shadowSeq = ShadowAssignSequence(needs, uvScale);

    int assignedTotal = 0;

    // ---------- Phase 5A: assign loop ----------
    while (!pq.empty()) {
        size_t ni = pq.begin()->need;          // highest score, lowest index on a tie
        pq.erase(pq.begin());
        NeedEntry& e = needs[ni];

        CitizenComponent* citizen = e.best().citizen;   // nearest available laborer
        const IdleKey citizenKey = e.best().Key();
        if (g_verifyOn) realSeq.emplace_back(ni, citizen);
        WorkPlaceComponent* wp = e.workplaceComp;
        Profession*      reqProf  = wp->profession;   // +0x48
        ComponentEntity* wpEntity = wp->_entity.Get();      // +0x10 owner entity IBP value

        CitizenComponent* c = citizen;
        if (c->profession != reqProf ||
            c->workplace != wpEntity) {
            c->SetWorkplace(wpEntity);   // 0x140111ee0: set +0x248 = workplace
            c->profession = reqProf;     // set profession
        }

        // decrement this citizen's profession need
        // Retail decrements the count under the workplace's required profession
        // (= the citizen's post-assignment +0x1f0), which is what we look up here.
        auto pit = profNeed.find(profKey(c->profession));
        if (pit != profNeed.end() && --pit->second == 0) profNeed.erase(pit);
        idle.erase(citizenKey);
        e.remaining -= 1;
        ++assignedTotal;

        // Remove the now-employed citizen from EVERY need's candidate set; re-score
        // any need whose best candidate changed (and still has open slots). Retail
        // re-queries the cost (0x1400d6060 at 0x1400d5b44) to build the {cost, citizen}
        // key, and when the keyed find fails (the pair's first query was a miss and
        // returned 0) walks the tree for the citizen (0x1400d5bb8): the same node the
        // citizen scan below finds. The re-query is always a tree hit (Phase 3 queried
        // every idle citizen x need pair this call), so it changes nothing. The citizen
        // has just left `idle`; a need loses it lazily, and its best changes only
        // when the citizen was its head (Advance moves head to the next live one).
        for (size_t f = 0; f < needs.size(); ++f) {
            NeedEntry& nf = needs[f];
            if (nf.remaining == 0) continue;   // retail: remaining != 0 (0x1400d5b05)
            const bool wasBest = nf.best().citizen == citizen;
            if (wasBest) {
                // Erase the key nf is queued under, scored from the best it was queued
                // with (0x1400d5c50), then re-insert with the new best's score.
                pq.erase(NeedQueue::Key{ nf.score(uvScale), f });
                nf.Advance(idle);
                pq.insert(NeedQueue::Key{ nf.score(uvScale), f });
            }
        }
    }

    if (g_verifyOn) {
        size_t k = 0;
        while (k < shadowSeq.size() && k < realSeq.size() && shadowSeq[k] == realSeq[k]) ++k;
        StressHarness_Verify("pathgraph_assign_seq", k == shadowSeq.size() && k == realSeq.size(),
                             static_cast<long long>(k));
    }

    // ---------- Phase 5B: cleanup — clear workplace of leftover idle laborers ----------
    while (!profNeed.empty() && !idle.empty()) {
        // Both are the leftmost node of their tree: the walks before 0x1400d5e05 follow the
        // profession map down +0x18 (the `key < node` side) and the idle set down +0x10.
        const ProfKey     keyT = profNeed.begin()->first;   // smallest profession key
        Profession*       key  = keyT.ptr;
        CitizenComponent* c    = idle.begin()->ptr;          // smallest idle citizen key

        if (c->profession != key ||
            c->workplace != nullptr) {
            // This arm strips the workplace off ANY citizen in `idle`, including one
            // correctly seated at a building, and that is RETAIL BEHAVIOUR — checked
            // 2026-08-25, do not "fix" it. Retail's Phase 3 gate (0x1400d43b0, the
            // townMgr+0xa0 walk) is exactly
            //     if (*(float*)(desc+0x44) <= *(float*)(cz+0x3c) && *(cz+0x208) == 0)
            // i.e. adult AND no active action, with NO laborer filter — byte-for-byte
            // what IsIdleHealthy() does above. The "idle-laborer set" phrasing in this
            // file's header comment is the thing that is wrong, not the code.
            //
            // This was briefly suspected of causing a woodcutter to lose its worker.
            // It was not: measured, this arm never fired for that case. The real cause
            // was an unguarded b310 in ConsumeProduce_Execute pausing the building
            // (see consumeproduce.cpp).
            c->SetWorkplace(nullptr);   // 0x140111ee0
            c->profession = key;
        }
        idle.erase(idle.begin());
        auto pit = profNeed.find(keyT);
        if (pit != profNeed.end() && --pit->second == 0) profNeed.erase(pit);
    }

    g_wpcRosterOnly = prevRoster;
}
