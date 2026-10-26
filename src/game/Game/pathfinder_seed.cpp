// src/game/Game/pathfinder_seed.cpp
// Phase 3 Keystone D / task #34 — seed the map PATHFINDER worker object at game+0x68.
//
// The pathfinder is a System::Thread subclass (size 0x1f8, vtable 0x14015ef18) that the
// real game allocates during worldgen inside CreateNaturalResource_impl_sub_e8c0
// (0x14009e8c0) and stores at gameObject+0x68. The Game ctor (FUN_14009ee70) only ZEROES
// +0x68, so in standalone it stays null and MoveTo's path setup
// (PathSetup_c8c10 -> SetTerrainCosts_e550 -> Pf_EnqueueCmd1_a6a30) derefs null +0x170.
//
// This function replicates the pathfinder-relevant subset of e8c0 (alloc + the a67f0 ctor +
// the a6110 grid ctor inline + the search-flag / cost-table / per-cell-flag priming), then
// stores it at gameObject+0x68. As retail, the ctor leaves +0x5c (runInline) 0, so the
// priming commands are QUEUED and the worker thread is started by the first one (row 962).
// Field offsets verified 1:1 vs the Ghidra decompiles of FUN_1400a67f0 / FUN_1400a6110 /
// CreateNaturalResource_impl_sub_e8c0.
//
// ENDGAME B.3.8 block LOOP: the object is the REAL class `PathfinderThread`
// (src/game/Game/PathfinderThread.h); its worker loop is `Run()` and slot 1 is
// System::Thread's OnCompleted, inherited. Gone: the hand-built 2-slot
// `g_pf_vtable` and its no-op OnCompleted stub (the same RET body).

#include "Game/PathfinderThread.h"
#include "Game/Map.h"
#include "Action/PathSearch.h"
#include "system/MemoryAllocator.h"   // System::MemoryAllocator::MemAlloc / MemFree
#include "Action/exe_bodies.h"   // Pf_WorkerApplyCmd_a6430, Pf_EnqueueCmd1_a6a30, Pf_ArrayResize18_d830
#include <cstdint>
#include <new>
#include <cstdio>
#include <cstdlib>
#include "system/Thread.h"     // ENDGAME B.3.4b System caller batch (CriticalSection/Thread)
#include "ext/path_workers/path_workers.h"   // EXT SEAM (NOT RETAIL, docs/extensions/handins/PATHWORKERS.md): Ext::PathWorkers::Engaged / Dispatch

// A* / pathfinder helpers (reimplemented + registered elsewhere) — called by name through
// Action/exe_bodies.h: Pf_WorkerApplyCmd_a6430 (0x1400a6430), Pf_EnqueueCmd1_a6a30
// (0x1400a6a30), Pf_ArrayResize18_d830 (0x14001d830).
// Runtime-DLL functions, called by name: ??0CriticalSection@System@@QEAA@XZ is the
// +0x30 member's own ctor (B6 row 50); ?MemFree@MemoryAllocator@System@@ ->
// System::MemoryAllocator::MemFree.


// --- Pathfinder System::Thread vtable (2 slots) ------------------------------------------
// The pathfinder is a System::Thread subclass; its real vtable (0x14015ef18) is
//   slot 0 = the worker run-loop (FUN_1400a6360),
//   slot 1 = System::Thread::OnCompleted (Runtime 0x18000c590 — a bare RET, no-op).
// System::Thread::Thread(...) already created the OS worker thread (suspended→resumed) inside
// the ctor we call below; System::Thread::ThreadFunc parks it on the "start" event. The
// enqueue helpers (a6a30/a6b20/a6c10) call Thread::Start when +0x5c==0 and the run-flag
// (+0x8 & 1) is clear; Start signals the event, ThreadFunc wakes and invokes slot 0 (drain
// the queue) then slot 1, then clears the run-flag and re-parks. (Verified vs Ghidra: ctor
// 0x1800ca4e0, Start 0x1800ca610, ThreadFunc 0x1800ca440.)
//
// Worker run-loop — faithful 1:1 port of FUN_1400a6360. Pop the head command node under the
// pathfinder CS (+0x30), unlink + free it, then apply via a6430 (runs the A* for cmd type 5
// and publishes the result slot). a6430's param_3/param_4 are dead for the queued path
// (Pf_AstarRun_ce00 `(void)param_4`s the only consumer), so pass null — matching ThreadFunc,
// which invokes slot 0 with `this` only. The command-node MemAlloc (a6c10/a6240) and this
// MemFree, plus the result-array resize, all run under the now-thread-safe global allocator
// lock (standalone/allocator_lock.cpp), so the worker can't race the main thread on the
// engine free-list. Queue: tail@+0x60, head@+0x68, count@+0x70; node qwords [0]=cmd{type,arg1}
// [1]=cmd{arg2,arg3} [2]=next(+0x10) [3]=prev(+0x18).
void PathfinderThread::Run() {
    int count = queueCount;
    while (count > 0) {
        // EXT SEAM (NOT RETAIL, docs/extensions/handins/PATHWORKERS.md): ext.path_workers -- once per command; the
        // dispatcher drains the rest of the queue (src/ext/path_workers/path_workers.h).
        if (Ext::PathWorkers::Engaged()) { Ext::PathWorkers::Dispatch(*this); return; }
        lock.Lock();
        PathCommand* node = queueHead;
        PathCommandArgs cmd = node->args;                  // cmd {type, arg1, arg2, arg3}
        if (node == queueTail)                             // head == tail?
            queueTail = node->prev;                        // tail = node->prev
        queueHead = node->next;                            // head = node->next
        if (node->next != nullptr) node->next->prev = node->prev;
        if (node->prev != nullptr) node->prev->next = node->next;
        node->next = nullptr;
        node->prev = nullptr;
        queueCount = queueCount - 1;
        System::MemoryAllocator::MemFree(node);
        lock.Unlock();
        ApplyCommand(cmd);                                 // 0x1400a6430
        count = queueCount;
    }
}

namespace {
// FUN_1400a6110 — the pathfinder's cell-grid ctor (PathfinderThread::grid). dim = map width.
static void GridCtor(GridPathSearch& g, int dim) {
    g.search.generation = 1;
    g.search.nodes.data = nullptr;          // node-array {ptr, count, cap} header
    g.search.nodes.size = 0;
    g.search.nodes.capacity = 0;
    g.search.open.root = nullptr;
    g.search.open.count = 0;
    g.search.open.pool.chunks = nullptr;
    g.search.open.pool.freeList = nullptr;
    g.search.open.pool.growCount = 8;       // node block size (one qword: 8, stride 0; H7)
    g.search.open.pool.stride = 0;
    g.search.open.pool.total = 0;
    g.search.open.pool.inUse = 0;
    g.search.open.pool.available = 0;
    SearchNodesResize(&g.search.nodes, dim * dim);   // 0x14001d830: reserve dim*dim A* nodes
    g.passableTypes = 0;
    g.allowDiagonal = 0;                    // +0xd4 one word (H7)
    g.manhattan = 0;
    g.respectBlocked = 0;
    g.cellFlags = static_cast<uint8_t*>(System::MemoryAllocator::MemAlloc(
        static_cast<uint64_t>(dim) * static_cast<uint64_t>(dim), 4));   // per-cell terrain-flag bytes
    g.width = dim;                          // grid width
    for (int i = 0; i < 0x20; ++i)          // grid cost array (all = 1)
        g.stepCost[i] = 1;
}
} // namespace

// Build + link the pathfinder. gameObj = the Map (GameUpdateLoop::m_game); dim = map width.
extern "C" void Pathfinder_Seed(Map* game, int dim) {
    if (!game || dim <= 0) return;
    if (game->pathfinder != nullptr) return;   // already seeded

    void* pfV = System::MemoryAllocator::MemAlloc(0x1f8, 4);   // 0x14009e8e6

    // --- FUN_1400a67f0 ctor (inline) ---
    // ENDGAME B.3.9 ENG W2: the REAL class ctor on System::Thread(0x10000) --
    // creates + parks the worker thread; the 0x14015ef18 stamp is the compiler's.
    // B6 row 50: that ctor constructs the +0x30 lock as a member
    // (0x1400a6829 / 0x1400a682d: ??0CriticalSection right after the stamp).
    // 0x14009e8f3 TEST RAX,RAX; JZ 0x14009e908: retail's placement-new shape -- the ctor
    // 0x1400a67f0 runs only on a non-null allocation; a null is stored at +0x68 (0x14009e90b)
    // and the priming below reads it unguarded (B7 audit F2: 84b58b58f's "no null test" was wrong).
    PathfinderThread* pf = nullptr;
    if (pfV) {
    pf = ::new (pfV) PathfinderThread(0x10000);
    pf->nextToken  = 1;          // next result-slot generation id
    pf->runInline  = 0;          // 0x1400a683b: retail's ctor stores 0 -- the priming below QUEUES (row 962)
    pf->queueTail  = nullptr;    // queue tail
    pf->queueHead  = nullptr;    // queue head
    pf->queueCount = 0;          // queue count
    GridCtor(pf->grid, dim);     // FUN_1400a6110
    pf->results.data = nullptr;  // result-slot array base
    pf->results.size = 0;        // +0x168: one qword over size and capacity (H7)
    pf->results.capacity = 0;
    pf->stagedPassableTypes = 0;
    pf->stagedDiagonal = 0;      // +0x1f4 one word (H7)
    pf->stagedManhattan = 0;
    pf->stagedRespectBlocked = 0;
    for (int i = 0; i < 0x20; ++i)                       // staged terrain-cost array (all = 1)
        pf->stagedStepCost[i] = 1;
    }   // end of the ctor arm (0x14009e908 joins with RBX = 0)

    // Link into the map (0x14009e90b, the null included).
    game->pathfinder = pf;

    // --- e8c0 priming (row 962, B7 RES: retail's statements in retail's order) ---
    // 0x14009e90f-0x14009e9db is SetSearchFlags(1, 0, 0) (0x1400a68b0) inlined: the staged
    // flags compared (+0x1f4 == 1, +0x1f5 == 0, +0x1f6 == 0), then stored (word +0x1f4 = 1,
    // byte +0x1f6 = 0), command {4, 1, 0, 0}; runInline is 0, so ONE node is queued under
    // the lock (0x14009e96c-0x14009e9cc) and the worker Started when not running
    // (0x14009e9d2-0x14009e9db).
    pf->SetSearchFlags(1, 0, 0);

    // 0x14009e9e1-0x14009ea98: the eleven step costs through 0x1400a6a30, each QUEUED
    // (runInline 0; every staged cost is 1 from the ctor, so each one differs).
    static const int kCosts[11] = { 3, 3, 4, 8, 0x10, 0x10, 0x10, 0x10, 0x10, 3, 4 };
    for (int i = 0; i < 11; ++i)
        pf->SetStepCost(i, kCosts[i]);

    // per-cell terrain flags: low 7 bits = the cell's terrainType (+0x29),
    // bit 7 = blocked (the cell's blocker, +0x00, set). The map's cells, stride 0x30.
    // 0x14009eaa3-0x14009eb16: no test on the flag bytes or the cell array.
    uint8_t* flags    = pf->grid.cellFlags;
    MapCell* cellGrid = game->cells.GetData();
    int      cellN    = game->cells.GetSize();
    for (int i = 0; i < cellN; ++i) {
        uint8_t  type    = cellGrid[i].terrainType;
        bool     blocked = cellGrid[i].blocker != nullptr;
        flags[i] = static_cast<uint8_t>((flags[i] & 0x80) | type);
        flags[i] = static_cast<uint8_t>((blocked ? 0x80 : 0) | (flags[i] & 0x7f));
    }

    fprintf(stderr, "[PfSeed] pathfinder seeded @ game+0x68 = %p (dim=%d, cells=%d, ASYNC worker-thread)\n",
            pfV, dim, cellN);
    fflush(stderr);
}
