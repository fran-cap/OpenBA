#pragma once
// ============================================================================
// src/game/Action/PathSearch.h — ENDGAME B.4 W1 block ACT-a
//
// The record structs the pathfinder bodies (`action_pf_*.cpp`) read: the node
// pool, the pooled cell queue, the A* open set (an AVL tree), the per-cell search
// node, the search context both A* runs share, the grid search the worker thread
// owns (PathfinderThread +0x78), its command and result records, and the coarse
// region graph (Map +0x70).
//
// NAMES. Retail has no symbols for any of these and the profile build lists no
// pathfinder source file of the exe; every struct and field name below is
// DESCRIPTIVE and UNCONFIRMED, taken from what retail does with the field. Offsets,
// widths and signedness are retail's (the instruction that reads / writes each
// field is named on it).
//
// OWNERSHIP. None of these records is constructed or destroyed by a C++
// constructor: retail builds them in raw memory with field stores (the grid
// search in the pathfinder's ctor 0x1400a67f0 / 0x1400a6110, the region graph in
// 0x1400a8670, the nodes by the pool allocators below). The members that are
// embedded in `PathfinderThread` are therefore trivially constructible and
// destructible (its dtor 0x1400a6720 frees them by hand; a member destructor
// would free them twice). Arrays there are declared as retail's three raw fields
// {data, size, capacity} of `Collection::Array<T>`.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include <type_traits>
#include "collection/Collection.h"
#include "Action/PathAvl.h"          // AvlTreeHead, AvlNode30

class Map;

// ---------------------------------------------------------------------------
// Collection::Array<T> as its three raw fields, for arrays embedded in a record
// whose owner frees them by hand (see OWNERSHIP above): no ctor, no dtor.
// ---------------------------------------------------------------------------
template <class T>
struct RawArray {
    T*      data;                // +0x00
    int32_t size;                // +0x08
    int32_t capacity;            // +0x0C
};
static_assert(sizeof(RawArray<int32_t>) == 0x10, "RawArray is Collection::Array's 0x10");
static_assert(offsetof(RawArray<int32_t>, size) == 0x08, "RawArray::size @ +0x08");
static_assert(offsetof(RawArray<int32_t>, capacity) == 0x0C, "RawArray::capacity @ +0x0C");

// B.4 Y GAMEB: the two Collection::Array<T> bodies a RawArray's owner runs by
// hand, as typed wrappers over the one erased core (the Array header is the
// same three fields): the fill-form SetSize (rtcoll::SetSizeCore over T's
// trait-derived row) and, for a trivially destructible T, ~Array (the buffer
// freed, then the three fields zeroed: rtcoll::ArrayDtorImpl).
template <class T>
inline void RawArraySetSize(RawArray<T>& a, int newSize, const T& fill) {
    rtcoll::SetSizeCore(reinterpret_cast<rtcoll::ArrayHdr*>(&a), newSize,  // CAST-OK: elem_stride
                        reinterpret_cast<const uint8_t*>(&fill),           // CAST-OK: elem_stride
                        rtcoll::SpecFor<T>());
}
// The two-argument form, Array<T>::SetSize(n): the grown tail as T's row leaves it (a
// trivially default-constructible T: untouched, as retail's empty else arm). B8 RT2.
template <class T>
inline void RawArraySetSize(RawArray<T>& a, int newSize) {
    rtcoll::SetSizeCore(reinterpret_cast<rtcoll::ArrayHdr*>(&a), newSize,  // CAST-OK: elem_stride
                        nullptr, rtcoll::SpecFor<T>());
}
// The same body over a hand-derived row (an element whose copy / init / destroy are not the
// trait-derived ones, e.g. kSpec_ContourOutline). B8 RT2.
template <class T>
inline void RawArraySetSize(RawArray<T>& a, int newSize, const rtcoll::ArraySpec* row) {
    rtcoll::SetSizeCore(reinterpret_cast<rtcoll::ArrayHdr*>(&a), newSize,  // CAST-OK: elem_stride
                        nullptr, row);
}
template <class T>
inline void RawArrayFree(RawArray<T>& a) {
    static_assert(std::is_trivially_destructible<T>::value, "~Array with no element destroy");
    rtcoll::ArrayDtorImpl(reinterpret_cast<rtcoll::ArrayHdr*>(&a));        // CAST-OK: elem_stride
}

// ---------------------------------------------------------------------------
// The node pool: nodes carved out of chunks, a free list threaded through the
// first qword of each free node. Retail's three allocators 0x14001a9b0 (queue
// node, stride 0x18), 0x14001df40 (queue node, stride 0x18) and 0x14001d730
// (open-set node, stride 0x30) grow it identically: growCount doubles, one chunk
// of growCount * stride bytes (+ the 8-byte chunk link) is allocated and every
// node of it is pushed on the free list.
// ---------------------------------------------------------------------------
struct PathPoolLink {
    PathPoolLink* next;          // +0x00 chunk list / free list link
};
static_assert(sizeof(PathPoolLink) == 8, "PathPoolLink is one qword");
static_assert(offsetof(PathPoolLink, next) == 0x0, "PathPoolLink::next");

struct PathNodePool {
    PathPoolLink* chunks;        // +0x00 chunk list (0x14001a9b0: `*chunk = pool->chunks`)
    PathPoolLink* freeList;      // +0x08 free nodes
    int32_t  growCount;          // +0x10 nodes per new chunk, doubled before each grow (ctor 8)
    int32_t  stride;             // +0x14 node size; 0 until the first grow sets it
    int32_t  total;              // +0x18 nodes ever carved
    int32_t  inUse;              // +0x1C +1 on a pop, -1 on a push back
    int32_t  available;          // +0x20 -1 on a pop, +1 on a push back
    uint8_t  _pad24[4];          // +0x24

    // Give a node back: its first qword becomes the free-list link. Retail inlines
    // these four stores, in this order, at every release (0x14001d550, 0x14001d400,
    // 0x14001d040, 0x14001dcd0, 0x14009c170, 0x1400a7ad0).
    template <class Node>
    void Push(Node* node) {
        inUse = inUse - 1;
        available = available + 1;
        PathPoolLink* link = reinterpret_cast<PathPoolLink*>(node);  // CAST-OK: elem_stride
        link->next = freeList;
        freeList = link;
    }
};
static_assert(sizeof(PathNodePool) == 0x28, "PathNodePool is 0x28");
static_assert(offsetof(PathNodePool, _pad24) == 0x24, "PathNodePool::_pad24");
static_assert(offsetof(PathNodePool, chunks) == 0x00, "PathNodePool::chunks @ +0x00");
static_assert(offsetof(PathNodePool, freeList) == 0x08, "PathNodePool::freeList @ +0x08");
static_assert(offsetof(PathNodePool, growCount) == 0x10, "PathNodePool::growCount @ +0x10");
static_assert(offsetof(PathNodePool, stride) == 0x14, "PathNodePool::stride @ +0x14");
static_assert(offsetof(PathNodePool, total) == 0x18, "PathNodePool::total @ +0x18");
static_assert(offsetof(PathNodePool, inUse) == 0x1C, "PathNodePool::inUse @ +0x1C");
static_assert(offsetof(PathNodePool, available) == 0x20, "PathNodePool::available @ +0x20");
static_assert(std::is_trivially_copyable<PathNodePool>::value, "raw record");

// ---------------------------------------------------------------------------
// The pooled cell queue: a doubly linked FIFO whose nodes come from a pool.
// Pushed at the TAIL (+0x00), popped at the HEAD (+0x08). Two instances:
//   Map +0xA8 (the flood fill 0x14009c170, value = one cell index, the pool
//             allocator 0x14001a9b0 writes only its low dword)
//   RegionGraph +0xC8 (the label flood fill 0x1400a7ad0, value = packed (x, y),
//             allocator 0x14001df40 writes the whole qword)
// ---------------------------------------------------------------------------
template <class T>
struct PathCellQueue {
    struct Node {
        T     value;             // +0x00
        Node* next;              // +0x08 toward the tail
        Node* prev;              // +0x10 toward the head
    };
    Node*    tail;               // +0x00
    Node*    head;               // +0x08
    int32_t  count;              // +0x10
    uint8_t  _pad14[4];          // +0x14
    PathNodePool pool;           // +0x18

    // The ctor Map's ctor 0x14009ee70 inlines (0x14009eec5..eee3): tail, head,
    // count (a dword; +0x14 unwritten), the pool's chunks and free list, one qword
    // of 8 over growCount / stride (H7: two stores), one qword 0 over total /
    // inUse, a dword 0 over available (+0x24 unwritten). The dtor is 0x14001a5f0
    // (Release), which Map's dtor runs on the member after its body (0x14009ee26).
    // RegionGraph's queue (+0xC8) is never built or destroyed by C++: RegionGraph
    // is MemAlloc + Init (0x1400a8670) and ReleaseStorage (0x1400a8570) + MemFree.
    PathCellQueue() : tail(nullptr), head(nullptr), count(0) {
        pool.chunks = nullptr;
        pool.freeList = nullptr;
        pool.growCount = 8;
        pool.stride = 0;
        pool.total = 0;
        pool.inUse = 0;
        pool.available = 0;
    }
    ~PathCellQueue() { Release(); }

    // Take a node from the pool (growing it first when the free list is empty) and
    // initialise it: value, next = prev = 0. One retail body per element
    // (specialised below): 0x14001a9b0 (Map's cell queue), 0x14001df40 (the region
    // graph's (x, y) queue). Null when the pop found no node.
    Node* NewNode(const T* value);
    // 0x14001a5f0 (GAME-b, keystone_a_gamedtor.cpp): every queued node back onto
    // the pool's free list (walking from the tail), tail / head / count zeroed,
    // then the pool's chunks freed. The teardown of both instances.
    void Release();
};

// The (x, y) pair 0x1400a7ad0 queues: x in the low dword, y in the high dword.
struct PathCellXY {
    int32_t x;                   // +0x00
    int32_t y;                   // +0x04
};
static_assert(sizeof(PathCellXY) == 8, "PathCellXY is one qword");
static_assert(offsetof(PathCellXY, x) == 0x0, "PathCellXY::x");
static_assert(offsetof(PathCellXY, y) == 0x4, "PathCellXY::y");

// Map's queue holds one cell index; the node's high dword of the first qword is
// never written (0x14001a9b0 stores a dword).
struct PathCellIndex {
    int32_t  cell;               // +0x00
    uint8_t  _pad04[4];          // +0x04 not written by 0x14001a9b0
};
static_assert(sizeof(PathCellIndex) == 8, "PathCellIndex is one qword");
static_assert(offsetof(PathCellIndex, cell) == 0x0, "PathCellIndex::cell");
static_assert(offsetof(PathCellIndex, _pad04) == 0x4, "PathCellIndex::_pad04");

using MapCellQueue    = PathCellQueue<PathCellIndex>;
using RegionCellQueue = PathCellQueue<PathCellXY>;
template <> MapCellQueue::Node* MapCellQueue::NewNode(const PathCellIndex* value);      // 0x14001a9b0
template <> RegionCellQueue::Node* RegionCellQueue::NewNode(const PathCellXY* value);   // 0x14001df40

static_assert(sizeof(MapCellQueue) == 0x40, "the pooled queue is 0x40 (Map +0xA8..+0xE8)");
static_assert(offsetof(MapCellQueue, tail) == 0x00, "queue tail @ +0x00");
static_assert(offsetof(MapCellQueue, head) == 0x08, "queue head @ +0x08");
static_assert(offsetof(MapCellQueue, count) == 0x10, "queue count @ +0x10");
static_assert(offsetof(MapCellQueue, pool) == 0x18, "queue pool @ +0x18");
static_assert(sizeof(MapCellQueue::Node) == 0x18, "queue node stride 0x18 (0x14001a9b0)");
static_assert(offsetof(MapCellQueue::Node, next) == 0x08, "queue node next @ +0x08");
static_assert(offsetof(MapCellQueue::Node, prev) == 0x10, "queue node prev @ +0x10");
static_assert(sizeof(RegionCellQueue) == 0x40, "the pooled queue is 0x40 (RegionGraph +0xC8)");
static_assert(sizeof(RegionCellQueue::Node) == 0x18, "queue node stride 0x18 (0x14001df40)");

// ---------------------------------------------------------------------------
// The A* open set: an AVL tree ordered by (fcost, seq), nodes from its own pool.
// Node layout from the allocator 0x14001d730 and the delete 0x14001d550.
// ---------------------------------------------------------------------------
struct PathOpenKey {             // the 3-int key 0x14001d450 / 0x14001d730 read
    int32_t cell;                // +0x00
    int32_t seq;                 // +0x04 tie-break (the search's sequence counter)
    int32_t fcost;               // +0x08 sort key
};
static_assert(sizeof(PathOpenKey) == 0x0C, "PathOpenKey is three ints");
static_assert(offsetof(PathOpenKey, cell) == 0x0, "PathOpenKey::cell");
static_assert(offsetof(PathOpenKey, seq) == 0x4, "PathOpenKey::seq");
static_assert(offsetof(PathOpenKey, fcost) == 0x8, "PathOpenKey::fcost");

struct PathOpenNode {
    int32_t       cell;          // +0x00
    int32_t       seq;           // +0x04
    int32_t       fcost;         // +0x08
    uint8_t       _pad0C[4];     // +0x0C not written by 0x14001d730
    PathOpenNode* parent;        // +0x10
    PathOpenNode* left;          // +0x18
    PathOpenNode* right;         // +0x20
    int16_t       balance;       // +0x28 height(right) - height(left)
    int16_t       height;        // +0x2A
    uint8_t       _pad2C[4];     // +0x2C
};
static_assert(sizeof(PathOpenNode) == 0x30, "open-set node stride 0x30 (0x14001d730)");
static_assert(offsetof(PathOpenNode, _pad0C) == 0xC, "PathOpenNode::_pad0C");
static_assert(offsetof(PathOpenNode, _pad2C) == 0x2C, "PathOpenNode::_pad2C");
static_assert(offsetof(PathOpenNode, cell) == 0x00, "PathOpenNode::cell @ +0x00");
static_assert(offsetof(PathOpenNode, seq) == 0x04, "PathOpenNode::seq @ +0x04");
static_assert(offsetof(PathOpenNode, fcost) == 0x08, "PathOpenNode::fcost @ +0x08");
static_assert(offsetof(PathOpenNode, parent) == 0x10, "PathOpenNode::parent @ +0x10");
static_assert(offsetof(PathOpenNode, left) == 0x18, "PathOpenNode::left @ +0x18");
static_assert(offsetof(PathOpenNode, right) == 0x20, "PathOpenNode::right @ +0x20");
static_assert(offsetof(PathOpenNode, balance) == 0x28, "PathOpenNode::balance @ +0x28");
static_assert(offsetof(PathOpenNode, height) == 0x2A, "PathOpenNode::height @ +0x2A");

struct PathOpenSet {
    PathOpenNode* root;          // +0x00
    int32_t       count;         // +0x08
    uint8_t       _pad0C[4];     // +0x0C
    PathNodePool  pool;          // +0x10

    // Bodies in action_pf_pools.cpp (NewNode, FreeSubtree) and action_pf_astar.cpp.
    PathOpenNode*  NewNode(const PathOpenKey* key, PathOpenNode* parent);   // 0x14001d730
    void           FreeSubtree(PathOpenNode* node);                         // 0x14001d400
    PathOpenNode** Insert(PathOpenNode** out, const PathOpenKey* key);      // 0x14001d450
    void           Delete(PathOpenNode* node);                              // 0x14001d550
    void           Clear();   // 0x14001ced0 (GAME-b, keystone_a_gamedtor.cpp): the tree back onto the free list, the chunks freed
};
static_assert(sizeof(PathOpenSet) == 0x38, "PathOpenSet is 0x38");
static_assert(offsetof(PathOpenSet, _pad0C) == 0xC, "PathOpenSet::_pad0C");
static_assert(offsetof(PathOpenSet, root) == 0x00, "PathOpenSet::root @ +0x00");
static_assert(offsetof(PathOpenSet, count) == 0x08, "PathOpenSet::count @ +0x08");
static_assert(offsetof(PathOpenSet, pool) == 0x10, "PathOpenSet::pool @ +0x10");

// ---------------------------------------------------------------------------
// One search node per graph vertex (grid cell or region), stride 0x18; the array
// is resized by 0x14001d830, whose new-element fill is {0, -1, 0, 0, 0}.
// A node is OPEN while openStamp == the search's generation and CLOSED while
// closedStamp == it (0x14001d040 / 0x14001d320 decrement a stamp to clear it).
// ---------------------------------------------------------------------------
struct PathSearchNode {
    PathOpenNode* openNode;      // +0x00 this vertex's open-set entry
    int32_t       parent;        // +0x08 predecessor vertex, -1 for none
    int32_t       openStamp;     // +0x0C
    int32_t       closedStamp;   // +0x10
    int32_t       gCost;         // +0x14 cost so far
};
static_assert(sizeof(PathSearchNode) == 0x18, "search node stride 0x18 (0x14001d830)");
static_assert(offsetof(PathSearchNode, openNode) == 0x00, "PathSearchNode::openNode @ +0x00");
static_assert(offsetof(PathSearchNode, parent) == 0x08, "PathSearchNode::parent @ +0x08");
static_assert(offsetof(PathSearchNode, openStamp) == 0x0C, "PathSearchNode::openStamp @ +0x0C");
static_assert(offsetof(PathSearchNode, closedStamp) == 0x10, "PathSearchNode::closedStamp @ +0x10");
static_assert(offsetof(PathSearchNode, gCost) == 0x14, "PathSearchNode::gCost @ +0x14");

// ---------------------------------------------------------------------------
// The search context both A* variants share: the grid search (0x14001d040, run
// by 0x14001ce00) and the region search (0x14001dcd0, run by 0x14001dc00). Both
// owners start with these 0x50 bytes (ctors 0x1400a6110 and 0x1400a8670 write
// the same stores: generation 1, the pool's growCount 8).
// ---------------------------------------------------------------------------
struct PathSearch {
    int32_t         generation;    // +0x00 bumped before each run
    int32_t         sequence;      // +0x04 open-set tie-break counter
    RawArray<PathSearchNode> nodes;  // +0x08 one per vertex (resized by 0x14001d830)
    PathOpenSet     open;          // +0x18

    // 0x14001d320: (re)open `vertex` with its predecessor and costs (action_pf_astar.cpp).
    void Relax(int vertex, int parent, int gCost, int fCost);
};
static_assert(sizeof(PathSearch) == 0x50, "PathSearch is 0x50");
static_assert(offsetof(PathSearch, generation) == 0x00, "PathSearch::generation @ +0x00");
static_assert(offsetof(PathSearch, sequence) == 0x04, "PathSearch::sequence @ +0x04");
static_assert(offsetof(PathSearch, nodes) == 0x08, "PathSearch::nodes @ +0x08");
static_assert(offsetof(PathSearch, open) == 0x18, "PathSearch::open @ +0x18");
static_assert(std::is_trivially_copyable<PathSearch>::value, "raw record");

// ---------------------------------------------------------------------------
// The grid search the worker thread owns (PathfinderThread +0x78, 0xE8 bytes,
// ctor 0x1400a6110). The command dispatcher 0x1400a6430 writes every field after
// `search`; 0x1400a5ec0 (neighbour), 0x1400a6030 (heuristic) and 0x14001d040
// (A* core) read them.
// ---------------------------------------------------------------------------
struct GridPathSearch {
    PathSearch search;            // +0x00
    int32_t    stepCost[0x20];    // +0x50 per terrain type (command 1)
    uint32_t   passableTypes;     // +0xD0 bit per terrain type (command 0; 0x1400a5ec0 tests it)
    uint8_t    allowDiagonal;     // +0xD4 command 4 arg 1: eight neighbours, octile heuristic
    uint8_t    manhattan;         // +0xD5 command 4 arg 2: Manhattan heuristic
    uint8_t    respectBlocked;    // +0xD6 command 4 arg 3: skip cells whose flag bit 7 is set
    uint8_t    _padD7;            // +0xD7
    int32_t    width;             // +0xD8 grid edge in cells
    uint8_t    _padDC[4];         // +0xDC
    uint8_t*   cellFlags;         // +0xE0 per cell: terrain type (bits 0-6), blocked (bit 7)

    // 0x1400a5ec0, -1 when not walkable (below). Retail takes the cell index and splits it
    // into (row, col) on entry; here the caller passes the split and gets the neighbour's.
    int      Neighbour(int& row, int& col, uint32_t direction) const;
    // action_pf_geom.cpp
    int      Heuristic(int from, int to) const;               // 0x1400a6030
    // action_pf_astar.cpp. FindPath returns retail's RAX: bit 0 = found, the upper
    // bytes whatever the last write left (the caller tests the low byte only).
    uint64_t FindPath(int start, int goal);                   // 0x14001d040
    int      Run(int start, int goal, int32_t* path);          // 0x14001ce00, path cap 0x4000
};
static_assert(sizeof(GridPathSearch) == 0xE8, "GridPathSearch is 0xE8 (PathfinderThread +0x78..+0x160)");
static_assert(offsetof(GridPathSearch, _padD7) == 0xD7, "GridPathSearch::_padD7");
static_assert(offsetof(GridPathSearch, _padDC) == 0xDC, "GridPathSearch::_padDC");
static_assert(offsetof(GridPathSearch, search) == 0x00, "GridPathSearch::search @ +0x00");
static_assert(offsetof(GridPathSearch, stepCost) == 0x50, "GridPathSearch::stepCost @ +0x50");
static_assert(offsetof(GridPathSearch, passableTypes) == 0xD0, "GridPathSearch::passableTypes @ +0xD0");
static_assert(offsetof(GridPathSearch, allowDiagonal) == 0xD4, "GridPathSearch::allowDiagonal @ +0xD4");
static_assert(offsetof(GridPathSearch, manhattan) == 0xD5, "GridPathSearch::manhattan @ +0xD5");
static_assert(offsetof(GridPathSearch, respectBlocked) == 0xD6, "GridPathSearch::respectBlocked @ +0xD6");
static_assert(offsetof(GridPathSearch, width) == 0xD8, "GridPathSearch::width @ +0xD8");
static_assert(offsetof(GridPathSearch, cellFlags) == 0xE0, "GridPathSearch::cellFlags @ +0xE0");
static_assert(std::is_trivially_copyable<GridPathSearch>::value, "raw record");

// ============================================================
// 0x1400a5ec0 — neighbour cell by 8-direction code
// ============================================================
// Retail opens with `row = index / width; col = index % width`. FindPath, the one caller,
// makes that split once per expanded cell instead of once per neighbour and passes (row,
// col) in; the rest is retail's body: apply the direction delta (case 0..7), bounds-check,
// skip a cell whose blocked bit (0x80) is set while respectBlocked, and return the cell
// only if its terrain type's bit is set in passableTypes. On return (row, col) is the
// neighbour's; when the cell is returned, it is width * row + col with 0 <= row, col <
// width, so its `/ width` and `% width` are exactly that row and col (FindPath relies on
// this to carry the pair instead of dividing).
inline int GridPathSearch::Neighbour(int& row, int& col, uint32_t dir) const {
    int w = width;
    switch (dir) {
    case 0: col = col + 1; break;
    case 1: col = col - 1; break;
    case 2: row = row + 1; break;            // case 2: +row
    case 3: row = row - 1; break;            // case 3: -row
    case 4: col = col + 1; row = row + 1; break;
    case 5: col = col + 1; row = row - 1; break;
    case 6: col = col - 1; row = row - 1; break;
    case 7: col = col - 1; row = row + 1; break;
    default: break;
    }
    if ((-1 < col) && (col < w) && (-1 < row) && (row < w)) {
        int cell = w * row + col;
        char gateFlag = static_cast<char>(respectBlocked);
        const uint8_t* byteMap = cellFlags;
        if (gateFlag == 0 || (byteMap[cell] & 0x80) == 0) {
            uint32_t passMask = passableTypes;
            uint8_t cb = byteMap[cell];
            if (((passMask >> (cb & 0x1f)) & 1) != 0)
                return cell;
            return -1;
        }
    }
    return -1;
}

// ---------------------------------------------------------------------------
// A worker command: the 0x10 bytes every enqueuer (0x1400a6fe0, 0x1400a6a30,
// 0x1400a6b20, 0x1400a68b0, 0x1400a6c10) builds on its stack and hands to the
// dispatcher 0x1400a6430 by address (RDX), and the queued node (MemAlloc(0x20,
// 8)) that carries a copy of it until the worker 0x1400a6360 pops and frees it.
// type 0..5, see 0x1400a6430.
// ---------------------------------------------------------------------------
struct PathCommandArgs {
    int32_t type;                // +0x00
    int32_t arg1;                // +0x04
    int32_t arg2;                // +0x08
    int32_t arg3;                // +0x0C
};
static_assert(sizeof(PathCommandArgs) == 0x10, "PathCommandArgs is four ints");
static_assert(offsetof(PathCommandArgs, type) == 0x00, "PathCommandArgs::type @ +0x00");
static_assert(offsetof(PathCommandArgs, arg1) == 0x04, "PathCommandArgs::arg1 @ +0x04");
static_assert(offsetof(PathCommandArgs, arg2) == 0x08, "PathCommandArgs::arg2 @ +0x08");
static_assert(offsetof(PathCommandArgs, arg3) == 0x0C, "PathCommandArgs::arg3 @ +0x0C");

struct PathCommand {
    PathCommandArgs args;        // +0x00
    PathCommand*    next;        // +0x10 toward the tail
    PathCommand*    prev;        // +0x18 toward the head
};
static_assert(sizeof(PathCommand) == 0x20, "PathCommand is MemAlloc(0x20)");
static_assert(offsetof(PathCommand, args) == 0x00, "PathCommand::args @ +0x00");
static_assert(offsetof(PathCommand, next) == 0x10, "PathCommand::next @ +0x10");
static_assert(offsetof(PathCommand, prev) == 0x18, "PathCommand::prev @ +0x18");

// ---------------------------------------------------------------------------
// A path request's result slot (PathfinderThread +0x160, stride 0x18): allocated
// by 0x1400a6240 (token != 0 while claimed), filled by command 5 (0x1400a6430),
// read and released by 0x1400a6d50. The array is resized by 0x14001d940.
// ---------------------------------------------------------------------------
struct PathResult {
    uint8_t  ready;              // +0x00 set to 1 once `path` is written
    uint8_t  _pad01[3];          // +0x01
    int32_t  token;              // +0x04 0 = free slot
    Collection::Array<int32_t> path;   // +0x08 cell indices, start first
};
static_assert(sizeof(PathResult) == 0x18, "result slot stride 0x18 (0x14001d940)");
static_assert(offsetof(PathResult, _pad01) == 0x1, "PathResult::_pad01");
static_assert(offsetof(PathResult, ready) == 0x00, "PathResult::ready @ +0x00");
static_assert(offsetof(PathResult, token) == 0x04, "PathResult::token @ +0x04");
static_assert(offsetof(PathResult, path) == 0x08, "PathResult::path @ +0x08");

// The {slot, token} pair 0x1400a6240 / 0x1400a6c10 hand back to the requester.
struct PathTicket {
    int32_t slot;                // +0x00
    int32_t token;               // +0x04
};
static_assert(sizeof(PathTicket) == 8, "PathTicket is two ints");
static_assert(offsetof(PathTicket, slot) == 0x0, "PathTicket::slot");
static_assert(offsetof(PathTicket, token) == 0x4, "PathTicket::token");

// ---------------------------------------------------------------------------
// The coarse region graph (Map +0x70; MemAlloc(0x108) + ctor 0x1400a8670). The
// map is cut into blocks of blockWidth x blockHeight cells (both 8); each block's
// cells are flood-filled into regions (0x1400a7f20 / 0x1400a7ad0), each region
// is a graph vertex with a centroid and an adjacency list, and the region A*
// (0x14001dc00) runs over them.
// ---------------------------------------------------------------------------
struct RegionNode {                          // stride 0x30 (0x14001e210)
    int32_t x;                               // +0x00 centroid, in cells
    int32_t y;                               // +0x04
    int32_t block;                           // +0x08 owning block, -1 = free (0x1400a8200)
    uint8_t _pad0C[4];                       // +0x0C
    Collection::Array<int32_t> neighbours;   // +0x10 adjacent region ids
    Collection::Array<int32_t> borderCells;  // +0x20 cells just outside the block
};
static_assert(sizeof(RegionNode) == 0x30, "RegionNode stride 0x30 (0x14001e210)");
static_assert(offsetof(RegionNode, _pad0C) == 0xC, "RegionNode::_pad0C");
static_assert(offsetof(RegionNode, x) == 0x00, "RegionNode::x @ +0x00");
static_assert(offsetof(RegionNode, y) == 0x04, "RegionNode::y @ +0x04");
static_assert(offsetof(RegionNode, block) == 0x08, "RegionNode::block @ +0x08");
static_assert(offsetof(RegionNode, neighbours) == 0x10, "RegionNode::neighbours @ +0x10");
static_assert(offsetof(RegionNode, borderCells) == 0x20, "RegionNode::borderCells @ +0x20");

struct RegionBlock {                         // stride 0x20 (0x14001e060)
    int32_t x;                               // +0x00 in blocks
    int32_t y;                               // +0x04
    uint8_t needsRebuild;                    // +0x08 queued on RegionGraph::dirtyRebuild
    uint8_t needsAdjacency;                  // +0x09 queued on RegionGraph::dirtyAdjacency
    uint8_t _pad0A[6];                       // +0x0A
    Collection::Array<int32_t> regions;      // +0x10 region ids in this block
};
static_assert(sizeof(RegionBlock) == 0x20, "RegionBlock stride 0x20 (0x14001e060)");
static_assert(offsetof(RegionBlock, _pad0A) == 0xA, "RegionBlock::_pad0A");
static_assert(offsetof(RegionBlock, x) == 0x00, "RegionBlock::x @ +0x00");
static_assert(offsetof(RegionBlock, y) == 0x04, "RegionBlock::y @ +0x04");
static_assert(offsetof(RegionBlock, needsRebuild) == 0x08, "RegionBlock::needsRebuild @ +0x08");
static_assert(offsetof(RegionBlock, needsAdjacency) == 0x09, "RegionBlock::needsAdjacency @ +0x09");
static_assert(offsetof(RegionBlock, regions) == 0x10, "RegionBlock::regions @ +0x10");

struct RegionGraph {
    PathSearch  search;                      // +0x00
    Map*        map;                         // +0x50
    int32_t     blockWidth;                  // +0x58 cells per block (ctor 8)
    int32_t     blockHeight;                 // +0x5C (ctor 8)
    int32_t     blocksWide;                  // +0x60 blocks per map row
    int32_t     blocksHigh;                  // +0x64
    RawArray<RegionNode>  regions;           // +0x68 resized by 0x14001e210; its size is the id high-water
    RawArray<RegionBlock> blocks;            // +0x78 resized by 0x14001e060
    Collection::Array<int32_t> dirtyRebuild;     // +0x88 blocks whose regions are rebuilt
    Collection::Array<int32_t> dirtyAdjacency;   // +0x98 blocks whose adjacency is rebuilt
    Collection::Array<int32_t> freeRegions;      // +0xA8 recycled region ids
    Collection::Array<int32_t> labels;           // +0xB8 per cell of one block: flood label
    RegionCellQueue queue;                   // +0xC8 the label flood fill's work queue

    // The region A* (Euclidean heuristic between centroids). FindPath is retail's search
    // over a bucket open set that pops in the AVL's order (action_pf_region_search.cpp,
    // PATHDIST); FindPathAvl is retail's body verbatim over the AVL PathOpenSet
    // (action_pf_astar.cpp), run only by the COMPPROF=4 shadow VerifyPathLength.
    uint64_t FindPath(int start, int goal);                   // 0x14001dcd0 (RAX low byte: found)
    uint64_t FindPathAvl(int start, int goal);                // 0x14001dcd0, retail's open set
    int      Run(int start, int goal, int32_t* path);          // 0x14001dc00, path cap 0x2000
    // NOT RETAIL (action_pf_astar.cpp): PathLength's g_verifyOn shadow, FindPathAvl vs FindPath.
    void     VerifyPathLength(int start, int goal, int count, const int32_t* path, int length);

    // action_pf_region.cpp: rebuilding the graph block by block.
    void     FreeRegion(int region);                          // 0x1400a8200
    int      AllocRegion();                                   // 0x1400a8350
    void     MarkAdjacencyDirty(int block);                   // 0x1400a8420
    void     MarkCellDirty(int x, int y);                     // 0x1400a8c90 (action_pf_road.cpp)
    void     RebuildAdjacency(int block);                     // 0x1400a7480
    void     StampRegionIds(int block);                       // 0x1400a7670
    void     AddBorderCells(int region, int blockX0, int blockY0, int x, int y);   // 0x1400a7750
    uint64_t BuildRegion(int block, int label);               // 0x1400a7950 (the id, zero-extended)
    void     FloodLabel(int blockX0, int blockY0, uint32_t x, uint32_t y, uint16_t label);   // 0x1400a7ad0
    int      LabelBlock(int block);                           // 0x1400a7f20
    void     RebuildBlock(uint32_t block);                    // 0x1400a80a0
    void     Flush();                                         // 0x1400a8a90
    int      PathLength(int start, uint32_t goal);            // 0x1400a8ba0

    // region_graph_build.cpp (B.4 ACT-b): the body of the ctor 0x1400a8670 (map, the
    // block counts; retail's R8 / R9 are not read).
    void     Init(Map* map, uint32_t blocksWide, int blocksHigh);   // 0x1400a8670
    // 0x1400a8570 (GAME-b, keystone_a_gamedtor.cpp): retail's destructor body,
    // run by Map's 0x14009ed60 before it MemFrees the graph (the tree never runs
    // a C++ destructor on it, so the body is a named member).
    void ReleaseStorage();
};
static_assert(sizeof(RegionGraph) == 0x108, "RegionGraph is MemAlloc(0x108)");
static_assert(offsetof(RegionGraph, search) == 0x00, "RegionGraph::search @ +0x00");
static_assert(offsetof(RegionGraph, map) == 0x50, "RegionGraph::map @ +0x50");
static_assert(offsetof(RegionGraph, blockWidth) == 0x58, "RegionGraph::blockWidth @ +0x58");
static_assert(offsetof(RegionGraph, blockHeight) == 0x5C, "RegionGraph::blockHeight @ +0x5C");
static_assert(offsetof(RegionGraph, blocksWide) == 0x60, "RegionGraph::blocksWide @ +0x60");
static_assert(offsetof(RegionGraph, blocksHigh) == 0x64, "RegionGraph::blocksHigh @ +0x64");
static_assert(offsetof(RegionGraph, regions) == 0x68, "RegionGraph::regions @ +0x68");
static_assert(offsetof(RegionGraph, blocks) == 0x78, "RegionGraph::blocks @ +0x78");
static_assert(offsetof(RegionGraph, dirtyRebuild) == 0x88, "RegionGraph::dirtyRebuild @ +0x88");
static_assert(offsetof(RegionGraph, dirtyAdjacency) == 0x98, "RegionGraph::dirtyAdjacency @ +0x98");
static_assert(offsetof(RegionGraph, freeRegions) == 0xA8, "RegionGraph::freeRegions @ +0xA8");
static_assert(offsetof(RegionGraph, labels) == 0xB8, "RegionGraph::labels @ +0xB8");
static_assert(offsetof(RegionGraph, queue) == 0xC8, "RegionGraph::queue @ +0xC8");

// ---------------------------------------------------------------------------
// The array resizes and frees of the records above (action_pf_pools.cpp). Each is
// retail's Collection::Array<T>::SetSize / destroy for that element, monomorphised
// into the exe; the element copies and frees are retail's, field by field.
// ---------------------------------------------------------------------------
void SearchNodesResize(RawArray<PathSearchNode>* nodes, int newSize);               // 0x14001d830
void PathResultsResize(RawArray<PathResult>* results, int newSize, const PathResult* fill);   // 0x14001d940
void PathResultsFree(PathResult* first, int count);                                  // 0x14001db40
void RegionNodesResize(RawArray<RegionNode>* regions, int newSize, const RegionNode* fill);   // 0x14001e210
void RegionNodesFree(RegionNode* first, int count);                                  // 0x14001e490
Collection::Array<int32_t>* IntArrayAssign(Collection::Array<int32_t>* dst,
                                           const Collection::Array<int32_t>* src);  // 0x14000f9e0

// ---------------------------------------------------------------------------
// The region-pair distance cache (Map +0x78; MemAlloc(0x38), built by the e8c0
// tail the tree calls Pf_RegionPairCache_Build): a ring of the last 0x20000
// (larger id, smaller id) -> distance results, indexed by an AVL tree of 0x30
// nodes (key0 = larger id, key1 = smaller id, key2 = ring slot). Entries expire
// after `lifetime` (0x1400a5d10).
// ---------------------------------------------------------------------------
struct RegionPairEntry {                     // stride 0x18 (0x14001cca0)
    int32_t regionA;                         // +0x00 the larger id, -1 = empty
    int32_t regionB;                         // +0x04 the smaller id
    int32_t distance;                        // +0x08
    uint32_t field_0x0C;                     // +0x0C never read; 0x1400a5b30 stores a stale stack dword
    int64_t time;                            // +0x10 System::Time ticks of `now` when stored
};
static_assert(sizeof(RegionPairEntry) == 0x18, "RegionPairEntry stride 0x18");
static_assert(offsetof(RegionPairEntry, regionA) == 0x00, "RegionPairEntry::regionA @ +0x00");
static_assert(offsetof(RegionPairEntry, regionB) == 0x04, "RegionPairEntry::regionB @ +0x04");
static_assert(offsetof(RegionPairEntry, distance) == 0x08, "RegionPairEntry::distance @ +0x08");
static_assert(offsetof(RegionPairEntry, field_0x0C) == 0x0C, "RegionPairEntry +0x0C");
static_assert(offsetof(RegionPairEntry, time) == 0x10, "RegionPairEntry::time @ +0x10");

struct RegionPairCache {
    int64_t newestTime;                      // +0x00 System::Time, advanced by 0x1400a5d10
    int64_t lifetime;                        // +0x08 System::Time(120 s)
    int32_t newest;                          // +0x10 ring write index
    int32_t oldest;                          // +0x14 ring expiry index
    RawArray<RegionPairEntry> ring;          // +0x18 (0x20000 entries; its size is the modulus)
    AvlTreeHead<AvlNode30> index;            // +0x28

    uint32_t Find(int a, int b) const;       // 0x1400a5ac0, 0xffffffff when absent
    void     Store(int a, int b, uint32_t distance);   // 0x1400a5b30
    // region_paircache.cpp (B.4 ACT-b): advance the clock by *dt, expire old entries.
    void     Expire(const int64_t* dt);                // 0x1400a5d10
};
static_assert(sizeof(RegionPairCache) == 0x38, "RegionPairCache is MemAlloc(0x38)");
static_assert(offsetof(RegionPairCache, newestTime) == 0x00, "RegionPairCache::newestTime @ +0x00");
static_assert(offsetof(RegionPairCache, lifetime) == 0x08, "RegionPairCache::lifetime @ +0x08");
static_assert(offsetof(RegionPairCache, newest) == 0x10, "RegionPairCache::newest @ +0x10");
static_assert(offsetof(RegionPairCache, oldest) == 0x14, "RegionPairCache::oldest @ +0x14");
static_assert(offsetof(RegionPairCache, ring) == 0x18, "RegionPairCache::ring @ +0x18");
static_assert(offsetof(RegionPairCache, index) == 0x28, "RegionPairCache::index @ +0x28");

// ---------------------------------------------------------------------------
// CharacterGridWalker (B.4 ACT-a): the stack-allocated box walker the
// CharacterGrid (Component/CharacterGrid.h) queries drive. 0x140018dd0 seeds it
// (the root node with the grid's half extent) and 0x140018880 pops the top node,
// pushing its four quadrant children while their depth < the grid's `levels`.
// Layout from those two bodies: node r lives at +0x24 + r * 0x24; the root
// pointer at +0x2428 is the highest byte either touches. The query bodies
// (0x140092260, 0x140092990) build each node's box from center +/- (2ex, 2ey, ez)
// padded by padLo / padHi.
struct CharacterGridWalkerNode {             // stride 0x24
    float    center[3];                      // +0x00
    float    extent[3];                      // +0x0C
    int32_t  cell;                           // +0x18 implicit-quadtree cell (children 4i+1..4i+4)
    int32_t  depth;                          // +0x1C
    uint32_t flag;                           // +0x20 always written 0
};
static_assert(sizeof(CharacterGridWalkerNode) == 0x24, "walker node stride 0x24");
static_assert(offsetof(CharacterGridWalkerNode, center) == 0x0, "CharacterGridWalkerNode::center");
static_assert(offsetof(CharacterGridWalkerNode, extent) == 0x0C, "walker node extent @ +0x0C");
static_assert(offsetof(CharacterGridWalkerNode, cell) == 0x18, "walker node cell @ +0x18");
static_assert(offsetof(CharacterGridWalkerNode, depth) == 0x1C, "walker node depth @ +0x1C");
static_assert(offsetof(CharacterGridWalkerNode, flag) == 0x20, "walker node flag @ +0x20");

class CharacterGrid;   // Component/CharacterGrid.h

struct CharacterGridWalker {
    uint8_t  _pad00[8];                      // +0x00 never written by 0x140018dd0
    int32_t  top;                            // +0x08 index of the top node (-1 = empty)
    float    padLo[3];                       // +0x0C (0, 0, lo)
    float    padHi[3];                       // +0x18 (0, 0, hi)
    CharacterGridWalkerNode nodes[256];      // +0x24
    uint8_t  _pad2424[4];                    // +0x2424
    const CharacterGrid* grid;               // +0x2428

    void Init(const CharacterGrid* root, float lo, float hi);   // 0x140018dd0
    void Subdivide();                                           // 0x140018880
};
static_assert(sizeof(CharacterGridWalker) == 0x2430, "walker: the highest touched byte is +0x2430");
static_assert(offsetof(CharacterGridWalker, _pad00) == 0x0, "CharacterGridWalker::_pad00");
static_assert(offsetof(CharacterGridWalker, _pad2424) == 0x2424, "CharacterGridWalker::_pad2424");
static_assert(offsetof(CharacterGridWalker, top) == 0x08, "walker top @ +0x08");
static_assert(offsetof(CharacterGridWalker, padLo) == 0x0C, "walker padLo @ +0x0C");
static_assert(offsetof(CharacterGridWalker, padHi) == 0x18, "walker padHi @ +0x18");
static_assert(offsetof(CharacterGridWalker, nodes) == 0x24, "walker nodes @ +0x24");
static_assert(offsetof(CharacterGridWalker, grid) == 0x2428, "walker grid @ +0x2428");
static_assert(std::is_trivially_default_constructible<CharacterGridWalker>::value,
              "a stack walker: nothing runs before 0x140018dd0");
