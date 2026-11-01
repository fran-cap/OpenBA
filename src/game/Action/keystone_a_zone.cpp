// src/game/Action/keystone_a_zone.cpp
// Keystone A (zone + road enqueue cluster) — faithful 1:1 ports of the exe
// zone footprint rasterizer (0x14009d360), the RoadTool path query (0x14009ce00)
// + its helper (0x1400a6e30), and the command-3 worker-queue enqueue
// (0x1400a6f00). These feed the PathfinderThread command queue whose core
// (ApplyCommand / the enqueuers / TakePath) is in action_pf_astar.cpp and
// action_base_move.cpp.
//
// NONE of this is exercised on the autostart path, so a wrong transcription is
// undetectable silent corruption — every field below is cross-checked against the
// Ghidra decompile AND disassembly. Do NOT "improve" or simplify.
//
// Types (B.4 ACT-b): the worker is PathfinderThread (Game/PathfinderThread.h), the
// command node PathCommand / PathCommandArgs and the result slot PathResult / PathTicket
// (Action/PathSearch.h), the grid the Map (Game/Map.h: size, pathfinder, cells) and the
// footprint stamp MapDescription::_footprint (a System::String).
// ============================================================================

#include "Action/exe_bodies.h"
#include <new>
#include "system/MemoryAllocator.h"   // System::MemoryAllocator::MemAlloc
#include <cstdint>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "system/Thread.h"     // ENDGAME B.3.4b System callers
#include "system/String.h"     // System::String (the footprint stamp)
#include "application/Application.h"   // ENDGAME B.3.7 callers (K2)
#include "Action/PathSearch.h"         // PathCommand, PathCommandArgs, PathResult, PathTicket
#include "Game/Map.h"                  // Map, MapCell
#include "Game/PathfinderThread.h"     // PathfinderThread
#include "Entity/ComponentEntity.h"   // B4 S4 RT2: InternalPtr<ComponentEntity>::Get() needs the class complete
#include "ext/registry.h"                           // EXT SEAM: Ext::Fix().path_query_first
#include "ext/path_query_first/path_query_first.h"  // EXT SEAM: fix.path_query_first (RunPathQuery)
#include "stress_harness.h"   // PFBENCH: PfMainTimer (pf_sync_wait_ms; stress harness only)
#include "Game/cell_stamp.h"   // AMBMEMO: CellStamp::NoteWrite after a cell store
#include "cell_trace.h"   // red square: BANISHED_CELL_TRACE (src/diag/cell_trace.h)

// Match the action_pf_*.cpp decompile token aliases.
typedef unsigned int  uint;

namespace {

// ---- The enqueue tail every enqueuer inlines: identical to action_pf_astar.cpp's
// EnqueueNode (file-local there too).
static void EnqueueNode(PathfinderThread* self, const PathCommandArgs& cmd) {
    self->lock.Lock();
    PathCommand* node = static_cast<PathCommand*>(System::MemoryAllocator::MemAlloc(0x20, 8));
    if (node != nullptr) {
        node->args.type = cmd.type;
        node->args.arg1 = cmd.arg1;
        node->args.arg2 = cmd.arg2;
        node->args.arg3 = cmd.arg3;
        node->next = nullptr;
        node->prev = nullptr;
    }
    if (self->queueTail == nullptr) {
        self->queueHead = node;
    } else {
        self->queueTail->next = node;
        node->prev = self->queueTail;
    }
    self->queueCount = self->queueCount + 1;
    self->queueTail = node;
    self->lock.Unlock();
    if ((self->_runFlags & 1) == 0) {
        self->Start();
    }
}

// ============================================================================
// 0x1400a6e30 — RoadTool worker helper. (Ghidra: RoadTool_helper_6e30.)
//   Sets the passable-type mask, requests a path, then spin-waits (yielding to the
//   worker thread) until the requested result slot is ready, then copies the path out.
// ============================================================================
// ⚠ RETURNS THE PATH LENGTH. Ghidra types both this and its tail call `void`, but
// 1400a6edf is a tail call into 0x1400a6d50, whose EAX is min(maxLen, cells found) —
// and the boat route builder (boatcomponent_route.cpp) tests exactly that value to
// decide whether a leg found anything. See reference_discarded_return_reenables_gated_branch.
static int RunPathQuery(PathfinderThread* pf, int start, int goal, int passableTypes,
                        int32_t* path, int maxNodes) {
    PfMainTimer pfTimer(PF_MAIN_SYNCWAIT);   // [PFBENCH] the whole request + spin (stress harness only)
    // The request ticket is TWO ADJACENT ints, not two independent locals: RequestPath ->
    // AllocResult (0x1400a6240) writes BOTH the slot index (retail [RSP+0x30]) and the
    // slot token ([RSP+0x34]) through the single pointer it is handed. Declaring them as
    // separate locals left the token write landing on whatever the compiler placed next,
    // so the wait below NEVER matched — an unbounded spin that hung the whole game on the
    // first road cost query.
    PathTicket ticket{0, 0};

    pf->SetPassableTypes(passableTypes);          // 0x1400a6b20
    // EXT SEAM (NOT RETAIL, docs/extensions/handins/PATHQUERYFIRST.md): fix.path_query_first queues
    // this query at the head of the worker's queue instead of behind every queued citizen search.
    if (!(Ext::Fix().path_query_first && Ext::PathQueryFirst::RequestFirst(pf, &ticket, start, goal))) {
        pf->RequestPath(&ticket, start, goal);    // 0x1400a6c10
    }

    do {
        if (ticket.slot < pf->results.size) {
            const PathResult& result = pf->results.data[ticket.slot];
            if (result.token == ticket.token && result.ready != 0) {
                return pf->TakePath(&ticket, path, maxNodes);   // 0x1400a6d50
            }
        }
        ::Application::Application::_singleton->YieldToThreads();
        pf->lock.Lock();
        if (pf->queueCount > 0 && (pf->_runFlags & 1) == 0) {
            pf->Start();
        }
        pf->lock.Unlock();
    } while (true);
}

// ============================================================================
// 0x1400a6f00 — enqueue command 3 (a cell's blocked byte). (Ghidra: FUN_1400a6f00.)
//   Same shape as 0x1400a6fe0 but type 3, arg2 = the blocked flag. Retail's R9 (the
//   Map) is not read.
// ============================================================================
static void SetCellBlocked(PathfinderThread* pf, uint32_t cell, uint32_t blocked) {
    PathCommandArgs local;
    local.type = 3;
    local.arg1 = static_cast<int32_t>(cell);
    local.arg2 = static_cast<int32_t>(blocked);
    local.arg3 = 0;
    if (pf->runInline == 0) {
        EnqueueNode(pf, local);
    } else {
        pf->ApplyCommand(local);   // 0x1400a6430 — apply directly on the worker
    }
}

// ============================================================================
// 0x14009d360 — zone footprint rasterizer. (Ghidra: ZoneComponent_sub_d360.)
//   Walks a (w x h) tile rectangle at (x, y), and for each cell stores either the
//   owner (a non-'.' stamp cell, or no stamp at all) or null (a '.' stamp cell) as
//   the cell's blocker. Each cell write enqueues a command 3 (SetCellBlocked).
//   `rotation` selects the stamp rotation (0x5a / 0xb4 / 0x10e). For a '.' cell the
//   cell's footprintCount moves by +1 (owner set) or -1 (owner null).
// ============================================================================
static void StampZoneFootprint(Map* map, const Object::InternalPtr<ComponentEntity>* owner, int x, int y,
                               uint w, int h, const System::String* footprint, int rotation) {
    uint64_t uVar7 = static_cast<uint64_t>(static_cast<uint>(h));
    uint64_t uVar12 = static_cast<uint64_t>(static_cast<uint>(static_cast<int>(w)));
    int iVar17 = 0;
    uint64_t uVar8 = uVar12;
    int iVar6 = h;
    if (0 < h) {
        do {
            iVar6 = iVar6 + -1;
            int iVar13 = 0;
            int iVar14 = static_cast<int>(uVar8);
            if (0 < iVar14) {
                do {
                    iVar14 = iVar14 + -1;
                    MapCell* cellsAtStart = map->cells.GetData();   // grid base, read before the store
                    int iVar15 = (iVar17 + y) * map->size + iVar13 + x;
                    const char* stamp = footprint->_data;
                    bool bVar18;
                    if ((stamp == nullptr) || (footprint->GetLength() == 0)) {
                        bVar18 = true;
                    } else {
                        bVar18 = false;
                    }
                    bVar18 = !bVar18;
                    ComponentEntity* stored;
                    if (bVar18) {
                        int iVar11 = iVar14;
                        int iVar4 = iVar17;
                        uint64_t uVar16;
                        int iVar10;
                        if ((rotation == 0x5a) ||
                            ((uVar8 = uVar7, uVar16 = uVar12, iVar10 = iVar6,
                              rotation != 0xb4 &&
                              (iVar10 = iVar17, iVar11 = iVar13, iVar4 = iVar6,
                               rotation == 0x10e)))) {
                            uVar8 = uVar12;
                            uVar16 = uVar7;
                            iVar10 = iVar11;
                            iVar11 = iVar4;
                        }
                        bVar18 = stamp[static_cast<int64_t>(iVar11) +
                                       (uVar16 + 1) * ((uVar8 - static_cast<int64_t>(iVar10)) - 1)] == '.';
                        if (!bVar18) goto LAB_clear;
                        // retail: `InternalBasePtr(nullptr)` constructed into a stack slot
                        stored = nullptr;
                    } else {
LAB_clear:
                        stored = owner->Get();
                    }
                    map->cells.GetData()[iVar15].blocker = stored;
                    CellStamp::NoteWrite(&map->cells.GetData()[iVar15]);   // [AMBMEMO] cell write clock
                    SetCellBlocked(map->pathfinder, static_cast<uint32_t>(iVar15),
                                   static_cast<uint32_t>(stored != nullptr));   // 0x1400a6f00
                    if (bVar18) {
                        char cVar5 = -1;
                        if (owner->Get() != nullptr) cVar5 = '\x01';
                        uint8_t& count = cellsAtStart[iVar15].footprintCount;
                        CellTrace::NoteCount(map, &cellsAtStart[iVar15], count, cVar5, "footprint");
                        count = static_cast<uint8_t>(count + cVar5);
                    }
                    uVar8 = static_cast<uint64_t>(w);
                    iVar13 = iVar13 + 1;
                } while (iVar13 < static_cast<int>(w));
            }
            iVar17 = iVar17 + 1;
        } while (iVar17 < h);
    }
}

// ============================================================================
// 0x14009ce00 — RoadTool path query. (Ghidra: RoadTool_sub_ce00.)
//   Sets the eleven step costs (terrain 0..10, cost 1) and two search-flag commands,
//   then tail-calls 0x1400a6e30 to run the A* and copy the resulting path.
// RETURNS THE PATH LENGTH (B3R Leftovers, read in retail): the body ends in a tail
// JMP 0x1400a6e30 at 0x14009cf41, so EAX at its return is 0x1400a6e30's EAX. Its
// readers (Player/roadtool.cpp, roadtool_calcpath.cpp) use it as the path length.
// Retail's 5th argument is never read (its stack slot is overwritten at 0x14009cf22
// with the path buffer for the tail call).
// ============================================================================
static int RoadPathQuery(Map* map, uint passableTypes, uint start, uint64_t goal,
                         int32_t* path, uint8_t manhattan, uint8_t respectBlocked) {
    PathfinderThread* pTVar1 = map->pathfinder;
    {
        pTVar1->SetStepCost(0, 1);
        pTVar1->SetStepCost(1, 1);
        pTVar1->SetStepCost(2, 1);
        pTVar1->SetStepCost(3, 1);
        pTVar1->SetStepCost(4, 1);
        pTVar1->SetStepCost(5, 1);
        pTVar1->SetStepCost(6, 1);
        pTVar1->SetStepCost(7, 1);
        pTVar1->SetStepCost(8, 1);
        pTVar1->SetStepCost(9, 1);
        pTVar1->SetStepCost(10, 1);
    }
    map->pathfinder->SetSearchFlags(0, 0, 0);                        // 0x1400a68b0
    map->pathfinder->SetSearchFlags(0, manhattan, respectBlocked);   // 0x1400a68b0
    return RunPathQuery(map->pathfinder, static_cast<int>(start),
                        static_cast<int>(goal & 0xffffffff),
                        static_cast<int>(passableTypes), path, 0x800);   // 0x1400a6e30
}

} // namespace

// ============================================================================
// The exported spellings of the three file-local bodies (callers:
// Component/boatcomponent_route.cpp, Component/zonecomponent.cpp, Player/roadtool.cpp).
// ============================================================================

int PathfinderThread::QueryPath(int start, int goal, int passableTypes,
                         int32_t* path, int maxNodes) {
    return RunPathQuery(this, start, goal, passableTypes, path, maxNodes);
}

void Map::StampFootprint(const Object::InternalPtr<ComponentEntity>* owner, int x, int y,
                            uint w, int h, const System::String* footprint, int rotation) {
    StampZoneFootprint(this, owner, x, y, w, h, footprint, rotation);
}

int Map::FindRoadPath(uint passableTypes, uint start, uint64_t goal,
                      void* unused5, int32_t* path, uint8_t manhattan, uint8_t respectBlocked) {
    (void)unused5;
    return RoadPathQuery(this, passableTypes, start, goal, path, manhattan, respectBlocked);
}
