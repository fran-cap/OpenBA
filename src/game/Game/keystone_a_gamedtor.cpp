// src/game/Game/keystone_a_gamedtor.cpp
// Keystone A (Game::Dtor + worker-thread teardown cluster) — faithful 1:1 ports of
// the exe Game destructor (0x14009ed60) and its private teardown sub-callees:
//   0x1400a6690  worker-queue drain + Thread::Start/WaitForFinish (stop the worker)
//   0x1400a6720  Thread (worker-queue object) destructor
//   0x1400a8570  Game sub-object (+0x70) destructor — frees the path/cmd pools
//   0x14001a450  tree+array destructor (+0x78 of Game)
//   0x14001a5f0  free-list + node-chain destructor (the +0xa8 / a8570+0xc8 pool)
//   0x14001ced0  cell-BST clear (recycles nodes onto the free list, frees the slab chain)
//   0x14001dac0  array-free (stride 0x18; frees element[0] of each, then the block)
//   0x14001e3a0  array-free (stride 0x20; frees element[0] of each, then the block)
//
// This runs ONLY at shutdown, which the autostart A/B path never reaches — so a wrong
// transcription is undetectable silent corruption. Every offset / branch / arg below is
// cross-checked against BOTH the Ghidra decompile AND the disassembly. Do NOT "improve"
// or simplify.
//
// Already-registered sub-callees this cluster reuses (NOT re-ported here):
//   0x140014c30  WorkBase_TreeNodeFree_14c30  (reg_work_base; children +0x10/+0x18)
//   0x14000e320  WorkBase_TreeNodeFree_e320   (reg_work_base; children +0x18/+0x20)
//   0x14001d400  Pf_BstFreeRecursive_d400     (reg_pf_pools; recursive cell-BST node free)
//   0x14001e490  Pf_FreeArray30_e490          (reg_pf_pools; array-free stride 0x30)
//
// Runtime-DLL exports used (called by name since B3R; the import name table behind
// exe IAT dispatch slots 0x14015252x/253x/255x/256x):
//   ?YieldToThreads@Application@1@QEAAXXZ             (slot 0x140152560)
//   ??1CriticalSection@System@@QEAA@XZ               (slot 0x140152530, a6720 inner dtor;
//                                                     the +0x30 member's dtor, B6 row 50)
//   (the a6720 tail-call, ??1Thread@System@@ via slot 0x140152520, is the
//    compiler's base chain since ENDGAME B.3.9 VS: a6720 is ~PathfinderThread)
//   ??1Scene@Picking@@QEAA@XZ                         (slot 0x140151fb8, ed60 tail-call;
//                                                     the implicit Picking::Scene::~Scene)
//
// NOTE on the StackCriticalSection in 0x1400a6690: the original uses a stack-scoped
// RAII guard (ctor=Lock at slot 0x140152538, dtor=Unlock at slot 0x140152548). The
// guard's ctor calls CriticalSection::Lock and stashes the CS*; its dtor calls Unlock.
// We model it directly with raw Lock/Unlock on (queue + 0x30), which is behaviourally
// identical and matches the loop's lock/unlock placement byte-for-byte.
//
// SPIN/WAIT concern: 0x1400a6690 spins (CS-guarded count check + YieldToThreads) until
// the worker queue (count @+0x70) drains to 0, then WaitForFinish on the worker thread.
// It starts the thread (if not already running, flag bit0 @+0x08) so the worker can
// process the queue. In standalone the Runtime Thread infra is real, so this drains
// normally; but it is shutdown-only and not autostart-exercised. See FINAL REPORT.
//
// Registered by standalone/reg_keystone_a_gamedtor.cpp.

// ENDGAME B.3.2 step 2 (COLL-GAME sweep): every `MemFree(data); data = 0; one
// qword over size+capacity = 0` triple below was a hand-inlined
// `Collection::Array<T>::~Array` for a trivially-destructible T
// (`rtcoll::ArrayDtorImpl`, byte for byte) and is now `->~Array()` on the
// header, spelled `rtcoll::ArrayHdr` because none of these element types is
// pinned in this file. In 1dac0 / 1e3a0 only the two `~Array`s are converted
// (the inner header at element+0x08 / +0x10 and the outer block); the
// per-element walk stays as written because the outer element's grow arms are
// not pinned, so no `ArraySpec` row can honestly be written for it yet. Slab
// chains, free lists, node chains, the cell BST and the `MemFree` with no
// header stores (a6720's [thread+0x158]) are not arrays and are untouched.
// B.4 GAME-b: every body is a member of the class whose fields it reads —
// PathCellQueue<T>::Release (0x14001a5f0, both queues), PathOpenSet::Clear
// (0x14001ced0), RegionGraph::ReleaseStorage (0x1400a8570),
// PathfinderThread::~PathfinderThread (0x1400a6720) / DrainQueue (0x1400a6690),
// Map::~Map (0x14009ed60); 0x14001a450 / 0x14001dac0 / 0x14001e3a0 are inlined
// into their one caller each, on the named arrays. Map::pickingScene is a real member
// (B4 wave Y); so is the PathfinderThread lock (B6 row 50). LEFT: Map::regionPairCache is int64_t* (gameupdateloop.cpp hands it to
// a body taking int64_t*).
#include "Game/PathfinderThread.h"
#include "Game/Map.h"
#include "Action/PathSearch.h"
#include "system/MemoryAllocator.h"   // System::MemoryAllocator::MemAlloc / MemFree
#include "picking/Scene.h"
#include "Action/exe_bodies.h"
#include "Work/exe_bodies.h"     // WorkBase_TreeNodeFree_e320 / _14c30
#include "collection/Collection.h"
#include <cstdint>
#include "system/Thread.h"     // ENDGAME B.3.4b System caller batch (CriticalSection/Thread)
#include "application/Application.h"   // ENDGAME B.3.7 callers (K2)
#include "Game/exe_bodies.h"
#include "ext/path_workers/path_workers.h"   // EXT SEAM (NOT RETAIL, docs/extensions/handins/PATHWORKERS.md): Ext::PathWorkers::ReleaseFor

namespace {


// The free-and-zero of a RawArray header: retail's inlined trivial-element
// ~Array (MemFree(data); data = 0; one qword over size + capacity = 0, H7: two
// named stores).
template <class T>
void FreeRawArray(RawArray<T>& a) {
    System::MemoryAllocator::MemFree(a.data);
    a.data = nullptr;
    a.size = 0;
    a.capacity = 0;
}

} // namespace

// ============================================================================
// 0x14001a5f0 — PathCellQueue<T>::Release (the pool's free list + chunk chain).
//   Walks the queued nodes from the tail (link `prev`, node +0x10), pushing each
//   onto the pool's free list (inUse - 1, available + 1). Then zeroes head / tail /
//   count, and frees the chunk chain (next = first qword of each chunk).
// 1:1 port of FUN_14001a5f0; one retail body for both queues (B13).
// ============================================================================
template <class T>
void PathCellQueue<T>::Release() {
    Node* node = tail;
    while (node != nullptr) {
        Node* next = node->prev;
        pool.Push(node);
        node = next;
    }
    head = nullptr;                                             // [+0x08] = 0
    tail = nullptr;                                             // [+0x00] = 0
    count = 0;                                                  // [+0x10] = 0
    // Free the chunk chain: head pool.chunks, next = *chunk.
    while (pool.chunks != nullptr) {
        PathPoolLink* nextChunk = pool.chunks->next;
        System::MemoryAllocator::MemFree(pool.chunks);
        pool.chunks = nextChunk;
    }
}
template void PathCellQueue<PathCellIndex>::Release();   // Map +0xA8
template void PathCellQueue<PathCellXY>::Release();      // RegionGraph +0xC8

// ============================================================================
// 0x14001ced0 — PathOpenSet::Clear.
//   If root != 0: recycles both subtrees onto the free list (0x14001d400,
//   FreeSubtree), pushes the root itself (inUse - 1, available + 1). Then zeroes
//   root / count and frees the chunk chain.
// 1:1 port of BuildStructure_impl_ced0 (FUN_14001ced0).
// ============================================================================
void PathOpenSet::Clear() {
    PathOpenNode* r = root;
    if (r != nullptr) {
        FreeSubtree(r->left);    // 0x14001d400
        FreeSubtree(r->right);
        pool.Push(r);
    }
    root = nullptr;
    count = 0;
    while (pool.chunks != nullptr) {
        PathPoolLink* nextChunk = pool.chunks->next;
        System::MemoryAllocator::MemFree(pool.chunks);
        pool.chunks = nextChunk;
    }
}

// ============================================================================
// 0x1400a8570 — RegionGraph's destructor body.
//   queue.Release()                                   1a5f0(+0xc8)
//   ~Array labels, freeRegions, dirtyAdjacency, dirtyRebuild (+0xb8 .. +0x88)
//   blocks: each block's `regions` array, then the block array (1e3a0(+0x78))
//   regions: RegionNodesFree (0x14001e490) over the elements, then the array
//   search.open.Clear()                                ced0(+0x18)
//   search.nodes freed                                 [+0x08]
// 1:1 port of TradeComponent_impl_8570 (FUN_1400a8570).
// ============================================================================
void RegionGraph::ReleaseStorage() {
    queue.Release();

    labels.~Array();           // MemFree [+0xb8]; [+0xb8]=[+0xc0]=0
    freeRegions.~Array();      // MemFree [+0xa8]
    dirtyAdjacency.~Array();   // MemFree [+0x98]
    dirtyRebuild.~Array();     // MemFree [+0x88]

    // 0x14001e3a0 (stride 0x20): each block's region-id array (+0x10), then the block array.
    for (int32_t i = 0; i < blocks.size; ++i) {
        blocks.data[i].regions.~Array();
    }
    FreeRawArray(blocks);

    RegionNodesFree(regions.data, regions.size);   // 0x14001e490: the elements' two arrays
    FreeRawArray(regions);                         // the outer header

    search.open.Clear();         // 0x14001ced0
    FreeRawArray(search.nodes);  // MemFree [+0x08]; [+0x08]=[+0x10]=0
}

// ============================================================================
// 0x1400a6720 — PathfinderThread::~PathfinderThread.
//   [thread] = 0x14015ef18                            the compiler's
//   results: each result's path array, then the array (0x14001dac0, stride 0x18)
//   MemFree grid.cellFlags   ([thread + 0x158], not zeroed)
//   grid.search.open.Clear() (ced0(thread + 0x90))
//   grid.search.nodes freed  ([thread + 0x80]; zero +0x80/+0x88)
//   walk the command chain from queueTail (link `prev`, node +0x18), MemFree each;
//   zero queueHead, queueTail, queueCount
//   CriticalSection::~CriticalSection(lock)
//   Thread::~Thread()                                 the compiler's chain
// 1:1 port of BuildStructure_impl_6720 (FUN_1400a6720).
// ============================================================================
PathfinderThread::~PathfinderThread() {
    // EXT SEAM (NOT RETAIL, docs/extensions/handins/PATHWORKERS.md): ext.path_workers -- its pool finishes its jobs and
    // its threads are joined before the results and the cell bytes go
    // (src/ext/path_workers/path_workers.h).
    if (Ext::PathWorkers::Engaged()) Ext::PathWorkers::ReleaseFor(this);

    // 0x14001dac0: each result's path (+0x08), then the result array.
    for (int32_t i = 0; i < results.size; ++i) {
        results.data[i].path.~Array();
    }
    FreeRawArray(results);

    System::MemoryAllocator::MemFree(grid.cellFlags);              // MemFree [thread + 0x158]
    grid.search.open.Clear();             // ced0(thread + 0x90)
    FreeRawArray(grid.search.nodes);      // MemFree [thread + 0x80]; [thread + 0x80] = [thread + 0x88] = 0

    // Walk the command chain from the tail; next link @node+0x18 (prev).
    PathCommand* cur = queueTail;
    while (cur != nullptr) {
        PathCommand* next = cur->prev;
        System::MemoryAllocator::MemFree(cur);
        cur = next;
    }
    queueHead = nullptr;                  // [thread + 0x68] = 0
    queueTail = nullptr;                  // [thread + 0x60] = 0
    StoreQueueCount(&queueCount, 0);      // [thread + 0x70] = 0 (NOT RETAIL: atomic, pathfinder_queue_count.h)

    // B6 row 50: the +0x30 lock's ~CriticalSection (0x1400a67bc / 0x1400a67c0)
    // is the member's, run by the compiler right after this body; then the
    // tail-call System::Thread::~Thread() -- the compiler's base chain.
}

// ============================================================================
// 0x1400a6690 — PathfinderThread::DrainQueue: worker-queue drain + worker stop.
//   Loop:
//     Lock(lock)
//     if (queueCount > 0) { if ((_runFlags & 1) == 0) Start(); count = queueCount; }
//     if (count == 0) { Unlock; break; }
//     YieldToThreads(Application::_singleton); Unlock; loop
//   After loop: Unlock; WaitForFinish()   (tail-call)
// 1:1 port of FUN_1400a6690.
//
// SPIN/WAIT: this blocks until the worker thread drains the queue to 0. It explicitly
// starts the worker (Thread::Start) when work is pending and the thread isn't running.
// Shutdown-only; not autostart-exercised.
// ============================================================================
void PathfinderThread::DrainQueue() {
    for (;;) {
        lock.Lock();
        int count = LoadQueueCount(&queueCount);   // NOT RETAIL: atomic (pathfinder_queue_count.h)
        if (count > 0) {
            if (!IsWorking()) {   // the byte at +0x08, bit 0 (running)
                Start();
            }
            count = LoadQueueCount(&queueCount);
        }
        if (count == 0) break;
        ::Application::Application::_singleton->YieldToThreads();
        lock.Unlock();
    }
    lock.Unlock();
    WaitForFinish(); // tail-call
}

// ============================================================================
// 0x14009ed60 — Map::~Map.
//   if (pathfinder) { DrainQueue; if (pathfinder) { ~PathfinderThread; MemFree } }
//   if (regionGraph) { ReleaseStorage; MemFree }
//   if (regionPairCache) { 0x14001a450; MemFree }
//   the +0xE8 tree: children freed through 0x140014c30, the root MemFreed; root / count zeroed
//   members: cellQueue.~PathCellQueue()               1a5f0(+0xa8)
//            cells.~Array()                            [+0x80]; [+0x80]=[+0x88]=0
//            pickingScene.~Scene()                     (tail-call)
// 1:1 port of FUN_14009ed60.
// ============================================================================
Map::~Map() {
    if (pathfinder != nullptr) {
        pathfinder->DrainQueue();
        if (pathfinder != nullptr) {   // re-read [RBX + 0x68]
            pathfinder->~PathfinderThread();   // 0x1400a6720
            System::MemoryAllocator::MemFree(pathfinder);
        }
    }

    if (regionGraph != nullptr) {
        regionGraph->ReleaseStorage();   // 0x1400a8570
        System::MemoryAllocator::MemFree(regionGraph);
    }

    if (regionPairCache != nullptr) {
        RegionPairCache* cache = regionPairCache;
        // 0x14001a450: the index tree (children +0x18 / +0x20 through 0x14000e320),
        // its root / count zeroed, then the ring array freed.
        AvlNode30* root = cache->index.root;
        if (root != nullptr) {
            WorkBase_TreeNodeFree(&cache->index.root, root->left);    // 0x14000e320
            WorkBase_TreeNodeFree(&cache->index.root, root->right);
            System::MemoryAllocator::MemFree(root);
        }
        cache->index.root = nullptr;                            // [+0x28] = 0
        cache->index.count = 0;                                 // [+0x30] = 0
        FreeRawArray(cache->ring);                              // [+0x18]; [+0x18]=[+0x20]=0
        System::MemoryAllocator::MemFree(cache);
    }

    // +0xe8 tree: free children @+0x10/+0x18 via 0x140014c30 (which takes
    // (&slot, child) and recurses children @child+0x10 / child+0x18).
    AvlNode28* node = pendingRoadCells.root;
    if (node != nullptr) {
        WorkBase_TreeNodeFree(&pendingRoadCells, node->left);    // 0x140014c30
        WorkBase_TreeNodeFree(&pendingRoadCells, node->right);
        System::MemoryAllocator::MemFree(node);
    }
    pendingRoadCells.root = nullptr;                                  // [+0xe8] = 0
    pendingRoadCells.count = 0;                                       // [+0xf0] = 0

    // Then the compiler destroys the members in reverse declaration order, as
    // retail does after the body: ~PathCellQueue on cellQueue (0x14001a5f0(+0xa8),
    // 0x14009ee26), ~Array on cells (MemFree [+0x80]; [+0x80] = [+0x88] = 0,
    // 0x14009ee2c..ee40), ~Picking::Scene on +0x08 (the tail-JMP 0x14009ee5a).
}
