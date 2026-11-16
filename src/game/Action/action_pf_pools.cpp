// src/game/Action/action_pf_pools.cpp
// Pathfinder/Action node-pool grow + dynamic-array resize family.
//
// Faithful 1:1 ports of the A*/pathfinder node-pool free-list grow helpers and the
// associated dynamic-array (vector) resize/copy/free helpers.
// None of this is on the autostart path, so a *wrong* port is undetectable
// latent corruption — every line below is transcribed literally from the Ghidra
// decompile (cross-checked against the disassembly for the intricate index math). Do
// NOT "improve" or simplify.
//
// B.4 ACT-a: the bodies read their records by name (src/game/Action/PathSearch.h).
// Allocation: the original passes (size, align) to the static
// System::MemoryAllocator::MemAlloc and frees through MemoryAllocator::MemFree; so do we.
//
// The three pool allocators (0x14001a9b0, 0x14001d730, 0x14001df40) carve nodes out of
// a raw chunk: the first qword of a chunk links the chunk list, the nodes follow, and
// a free node's first qword is the free-list link (PathPoolLink). Those casts are the
// pool's own raw memory becoming a node (each carries an elem_stride marker).
//
// (The GeometryBatch array bodies 0x14001fa80 / 0x14001ee40 / 0x14001f8d0 that sat
// here are Collection::Array<T>::SetSize on GeometryBatch's typed arrays, B.4 H5-P2.)
#include "Action/exe_bodies.h"
#include "Action/PathSearch.h"

#include "Game/exe_bodies.h"
#include "Game/WorldGeometryElements.h"
#include "system/MemoryAllocator.h"   // System::MemoryAllocator::MemAlloc / MemFree
#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace {
// The chunk size every pool grow allocates: growCount * stride + 0xf (the chunk link
// and the rounding), rounded down to 8 with retail's signed-division idiom.
static inline uint64_t PoolChunkBytes(const PathNodePool* pool) {
    int bytes = pool->stride * pool->growCount + 0xf;
    return static_cast<uint64_t>(static_cast<int>((bytes + (bytes >> 0x1f & 7U)) & static_cast<int>(0xfffffff8)));
}
} // namespace

// ============================================================
// 0x14001a9b0 — Map's cell queue: take a node (growing the pool first).
// ============================================================
// If the free list is empty, double growCount, allocate a chunk of growCount nodes
// (stride 0x18 when unset), link it onto the chunk list and push each new node onto
// the free list. Then pop one node, zero next/prev, write the cell (one DWORD), return
// it. Null if the pop yielded null.
template <>
MapCellQueue::Node* MapCellQueue::NewNode(const PathCellIndex* value) {
    PathNodePool* p = &pool;
    if (p->freeList == nullptr) {
        if (p->stride == 0) {
            p->stride = 0x18;
        }
        p->growCount = p->growCount * 2;
        PathPoolLink* chunk = static_cast<PathPoolLink*>(System::MemoryAllocator::MemAlloc(PoolChunkBytes(p), 8));
        int iVar4 = 0;
        chunk->next = p->chunks;
        int iVar2 = p->growCount;
        p->chunks = chunk;
        p->total = p->total + iVar2;
        p->inUse = p->inUse + iVar2;
        PathPoolLink* link = chunk + 1;
        if (0 < iVar2) {
            do {
                p->inUse = p->inUse + -1;
                p->available = p->available + 1;
                link->next = p->freeList;
                p->freeList = link;
                iVar4 = iVar4 + 1;
                link = reinterpret_cast<PathPoolLink*>(reinterpret_cast<uint8_t*>(link) + p->stride);  // CAST-OK: elem_stride
            } while (iVar4 < p->growCount);
        }
    }
    PathPoolLink* link = p->freeList;
    p->inUse = p->inUse + 1;
    p->available = p->available + -1;
    p->freeList = link->next;
    if (link != nullptr) {
        Node* node = reinterpret_cast<Node*>(link);  // CAST-OK: elem_stride
        int32_t cell = value->cell;
        node->next = nullptr;
        node->prev = nullptr;
        node->value.cell = cell;
        return node;
    }
    return nullptr;
}

// ============================================================
// 0x14001d730 — the open set: take a node (growing the pool first).
// ============================================================
// Same grow as 0x14001a9b0 with stride 0x30. The popped node gets the key's cell and
// seq, the parent, zeroed children and balance/height, then the key's fcost.
PathOpenNode* PathOpenSet::NewNode(const PathOpenKey* key, PathOpenNode* parent) {
    PathNodePool* p = &pool;
    int iVar4 = 0;
    if (p->freeList == nullptr) {
        if (p->stride == 0) {
            p->stride = 0x30;
        }
        p->growCount = p->growCount * 2;
        PathPoolLink* chunk = static_cast<PathPoolLink*>(System::MemoryAllocator::MemAlloc(PoolChunkBytes(p), 8));
        chunk->next = p->chunks;
        int iVar2 = p->growCount;
        p->chunks = chunk;
        p->total = p->total + iVar2;
        p->inUse = p->inUse + iVar2;
        PathPoolLink* link = chunk + 1;
        if (0 < iVar2) {
            do {
                p->inUse = p->inUse + -1;
                p->available = p->available + 1;
                link->next = p->freeList;
                p->freeList = link;
                iVar4 = iVar4 + 1;
                link = reinterpret_cast<PathPoolLink*>(reinterpret_cast<uint8_t*>(link) + p->stride);  // CAST-OK: elem_stride
            } while (iVar4 < p->growCount);
        }
    }
    PathPoolLink* link = p->freeList;
    p->inUse = p->inUse + 1;
    p->available = p->available + -1;
    p->freeList = link->next;
    if (link == nullptr) {
        return nullptr;
    }
    PathOpenNode* node = reinterpret_cast<PathOpenNode*>(link);  // CAST-OK: elem_stride
    node->cell = key->cell;
    node->seq = key->seq;
    int32_t fcost = key->fcost;
    node->parent = parent;
    node->left = nullptr;
    node->right = nullptr;
    node->balance = 0;                     // +0x28 / +0x2a: one dword store (H7: two stores)
    node->height = 0;
    node->fcost = fcost;
    return node;
}

// ============================================================
// 0x14001df40 — the region graph's (x, y) queue: take a node (growing the pool first).
// ============================================================
// Identical grow + pop to 0x14001a9b0; the popped node gets the whole (x, y) qword.
template <>
RegionCellQueue::Node* RegionCellQueue::NewNode(const PathCellXY* value) {
    PathNodePool* p = &pool;
    if (p->freeList == nullptr) {
        if (p->stride == 0) {
            p->stride = 0x18;
        }
        p->growCount = p->growCount * 2;
        PathPoolLink* chunk = static_cast<PathPoolLink*>(System::MemoryAllocator::MemAlloc(PoolChunkBytes(p), 8));
        int iVar4 = 0;
        chunk->next = p->chunks;
        int iVar2 = p->growCount;
        p->chunks = chunk;
        p->total = p->total + iVar2;
        p->inUse = p->inUse + iVar2;
        PathPoolLink* link = chunk + 1;
        if (0 < iVar2) {
            do {
                p->inUse = p->inUse + -1;
                p->available = p->available + 1;
                link->next = p->freeList;
                p->freeList = link;
                iVar4 = iVar4 + 1;
                link = reinterpret_cast<PathPoolLink*>(reinterpret_cast<uint8_t*>(link) + p->stride);  // CAST-OK: elem_stride
            } while (iVar4 < p->growCount);
        }
    }
    PathPoolLink* link = p->freeList;
    p->inUse = p->inUse + 1;
    p->available = p->available + -1;
    p->freeList = link->next;
    if (link != nullptr) {
        Node* node = reinterpret_cast<Node*>(link);  // CAST-OK: elem_stride
        PathCellXY xy = *value;
        node->next = nullptr;
        node->prev = nullptr;
        node->value = xy;
        return node;
    }
    return nullptr;
}

// ============================================================
// 0x14001d400 — give an open-set subtree back to the pool.
// ============================================================
// Post-order: the left child's subtree, the right child's, then this node (children
// freed before the node).
void PathOpenSet::FreeSubtree(PathOpenNode* node) {
    if (node != nullptr) {
        FreeSubtree(node->left);
        FreeSubtree(node->right);
        pool.Push(node);
    }
}

// ============================================================
// 0x14001d830 — resize the search-node array (element 0x18), copy + fill.
// ============================================================
// Grow (3x on a +1 growth from non-empty) via MemAlloc(cap*0x18, 8), copy existing
// elements, and give new slots the empty node {openNode 0, parent -1, 0, 0, 0}.
// MemFree the old buffer. The new-slot init writes parent as a dword, then one qword
// over openStamp / closedStamp and a dword gCost — two named stores for the qword (H7).
void SearchNodesResize(RawArray<PathSearchNode>* nodes, int newSize) {
    int iVar1 = nodes->size;
    if (newSize != iVar1) {
        PathSearchNode* old = nodes->data;
        nodes->size = newSize;
        if (nodes->capacity < newSize) {
            nodes->capacity = newSize;
            if ((newSize == iVar1 + 1) && (iVar1 != 0)) {
                nodes->capacity = newSize * 3;
            }
            nodes->data = static_cast<PathSearchNode*>(System::MemoryAllocator::MemAlloc(static_cast<uint64_t>(nodes->capacity) * 0x18, 8));
        }
        int iVar3 = 0;
        if (old == nodes->data) {
            iVar3 = iVar1;
        }
        int64_t lVar6 = iVar3;
        if (lVar6 < newSize) {
            do {
                PathSearchNode* dst = &nodes->data[lVar6];
                if (lVar6 < iVar1) {
                    if (dst != nullptr) {
                        *dst = old[lVar6];
                    }
                } else {
                    if (dst != nullptr) {
                        dst->openNode = nullptr;
                        dst->parent = -1;
                        dst->openStamp = 0;
                        dst->closedStamp = 0;
                        dst->gCost = 0;
                    }
                }
                lVar6 = lVar6 + 1;
            } while (lVar6 < newSize);
        }
        if (old != nodes->data) {
            System::MemoryAllocator::MemFree(old);
        }
    }
}

// ============================================================
// 0x14001db40 — free the path arrays of `count` result slots.
// ============================================================
// Each slot owns its path buffer: MemFree it, then zero data and size/capacity.
// Retail's RCX (a count-shaped value the caller leaves there) is never read.
void PathResultsFree(PathResult* first, int count) {
    if (0 < count) {
        int64_t lVar2 = count;
        PathResult* r = first;
        do {
            System::MemoryAllocator::MemFree(r->path._data);
            r->path._data = nullptr;
            r->path._size = 0;             // +0x10 / +0x14: one qword store (H7: two stores)
            r->path._capacity = 0;
            r = r + 1;
            lVar2 = lVar2 + -1;
        } while (lVar2 != 0);
    }
}

// ============================================================
// 0x14001e490 — free the two int arrays of `count` region nodes.
// ============================================================
// borderCells (+0x20) first, then neighbours (+0x10); each: MemFree, zero data, one
// qword over size/capacity. Retail's RCX is never read.
void RegionNodesFree(RegionNode* first, int count) {
    if (0 < count) {
        int64_t lVar2 = count;
        RegionNode* n = first;
        do {
            System::MemoryAllocator::MemFree(n->borderCells._data);
            n->borderCells._data = nullptr;
            n->borderCells._size = 0;      // one qword store (H7: two stores)
            n->borderCells._capacity = 0;
            System::MemoryAllocator::MemFree(n->neighbours._data);
            n->neighbours._data = nullptr;
            n->neighbours._size = 0;       // one qword store (H7: two stores)
            n->neighbours._capacity = 0;
            n = n + 1;
            lVar2 = lVar2 + -1;
        } while (lVar2 != 0);
    }
}

// (0x14001ee40 / 0x14001f8d0, a GeometryBatch's vertex-array SetSize in its two
// forms, are Collection::Array<GeometryBatchVertex>::SetSize on the typed member,
// Game/WorldGeometryElements.h: each body was step for step rtcoll::SetSizeCore
// over the trait row of GeometryBatchVertex.)

// ============================================================
// 0x14001d940 — resize the result-slot array (element 0x18, owns its path array).
// ============================================================
// Grow (3x on +1 from non-empty) via MemAlloc(cap*0x18, 8). Each slot is copied from the
// old buffer (or the fill for appended slots): ready (a byte), token, then the path
// array zeroed and deep-copied (0x14000f9e0). On an in-place shrink the tail slots' paths
// are freed (0x14001db40); on a realloc the whole old buffer's paths are freed (inline
// loop) and the old buffer is MemFreed.
void PathResultsResize(RawArray<PathResult>* results, int newSize, const PathResult* fill) {
    int iVar1 = results->size;
    int64_t lVar8 = iVar1;
    if (newSize != iVar1) {
        PathResult* old = results->data;
        results->size = newSize;
        if (results->capacity < newSize) {
            results->capacity = newSize;
            if ((newSize == iVar1 + 1) && (iVar1 != 0)) {
                results->capacity = newSize * 3;
            }
            results->data = static_cast<PathResult*>(System::MemoryAllocator::MemAlloc(static_cast<uint64_t>(results->capacity) * 0x18, 8));
        }
        int iVar4 = 0;
        if (old == results->data) {
            iVar4 = iVar1;
        }
        int64_t lVar12 = iVar4;
        if (iVar4 < newSize) {
            const PathResult* src9 = old + lVar12;
            uint64_t uVar10 = static_cast<uint64_t>(static_cast<uint32_t>(newSize - iVar4));
            do {
                PathResult* dst = &results->data[lVar12];
                if (dst != nullptr) {
                    const PathResult* src = fill;
                    if (lVar12 < lVar8) {
                        src = src9;
                    }
                    dst->ready = src->ready;
                    dst->token = src->token;
                    dst->path._data = nullptr;
                    dst->path._size = 0;   // +0x10 / +0x14: one qword store (H7: two stores)
                    dst->path._capacity = 0;
                    IntArrayAssign(&dst->path, &src->path);
                }
                src9 = src9 + 1;
                lVar12 = lVar12 + 1;
                uVar10 = uVar10 - 1;
            } while (uVar10 != 0);
        }
        if (old == results->data) {
            if (newSize < iVar1) {
                PathResultsFree(old + newSize, iVar1 - newSize);   // 0x14001db40
            }
        } else {
            if (0 < lVar8) {
                PathResult* r = old;
                do {
                    System::MemoryAllocator::MemFree(r->path._data);
                    r->path._data = nullptr;
                    r->path._size = 0;     // one qword store (H7: two stores)
                    r->path._capacity = 0;
                    r = r + 1;
                    lVar8 = lVar8 + -1;
                } while (lVar8 != 0);
            }
            System::MemoryAllocator::MemFree(old);
        }
    }
}

// (0x14001fa80, Array<GeometryBatch>::SetSize(n, fill), is GeometryBatchArray::
// SetSize over kSpec_GeometryBatch: Game/serialize_struct60b.cpp.)

// ============================================================
// 0x14001e210 — resize the region-node array (element 0x30, owns two int arrays).
// ============================================================
// Grow (3x on +1 from non-empty) via MemAlloc(cap*0x30, 8). Each node gets x, y, block
// from the old buffer (or the fill), then both arrays zeroed and deep-copied
// (0x14000f9e0). On an in-place shrink the tail's arrays are freed (0x14001e490); on a
// realloc the whole old buffer's (0x14001e490), then the buffer is MemFreed.
void RegionNodesResize(RawArray<RegionNode>* regions, int newSize, const RegionNode* fill) {
    int uVar1 = regions->size;
    if (newSize != uVar1) {
        RegionNode* old = regions->data;
        regions->size = newSize;
        if (regions->capacity < newSize) {
            regions->capacity = newSize;
            if ((newSize == uVar1 + 1) && (uVar1 != 0)) {
                regions->capacity = newSize * 3;
            }
            regions->data = static_cast<RegionNode*>(System::MemoryAllocator::MemAlloc(static_cast<uint64_t>(regions->capacity) * 0x30, 8));
        }
        int iVar4 = 0;
        if (old == regions->data) {
            iVar4 = uVar1;
        }
        int64_t lVar8 = iVar4;
        if (iVar4 < newSize) {
            const RegionNode* src9 = old + lVar8;
            uint32_t uVar5 = static_cast<uint32_t>(newSize - iVar4);
            do {
                RegionNode* dst = &regions->data[lVar8];
                if (dst != nullptr) {
                    const RegionNode* src = fill;
                    if (lVar8 < uVar1) {
                        src = src9;
                    }
                    dst->x = src->x;
                    dst->y = src->y;
                    dst->block = src->block;
                    dst->neighbours._data = nullptr;
                    dst->neighbours._size = 0;       // one qword store (H7: two stores)
                    dst->neighbours._capacity = 0;
                    IntArrayAssign(&dst->neighbours, &src->neighbours);
                    dst->borderCells._data = nullptr;
                    dst->borderCells._size = 0;      // one qword store (H7: two stores)
                    dst->borderCells._capacity = 0;
                    IntArrayAssign(&dst->borderCells, &src->borderCells);
                }
                src9 = src9 + 1;
                lVar8 = lVar8 + 1;
                uVar5 = uVar5 - 1;
            } while (uVar5 != 0);
        }
        if (old == regions->data) {
            if (newSize < uVar1) {
                RegionNodesFree(old + newSize, uVar1 - newSize);   // 0x14001e490
            }
        } else {
            RegionNodesFree(old, uVar1);   // 0x14001e490
            System::MemoryAllocator::MemFree(old);
        }
    }
}

// ============================================================
// 0x14000f9e0 — Collection::Array<int>'s deep assign (dst <- src), MemAlloc.
// ============================================================
// Frees dst's old buffer, resets ptr/count/cap to 0, sets count=src.count, and (if
// count>0) cap=count + allocates count*4 bytes (align 4), then copies each element from
// src. No-op if dst==src. Returns dst.
Collection::Array<int32_t>* IntArrayAssign(Collection::Array<int32_t>* dst,
                                           const Collection::Array<int32_t>* src) {
    if (dst != src) {
        System::MemoryAllocator::MemFree(dst->_data);
        dst->_size = 0;                    // one qword store over size / capacity (H7)
        dst->_capacity = 0;
        dst->_data = nullptr;
        int iVar1 = src->_size;
        dst->_size = iVar1;
        if (0 < iVar1) {
            dst->_capacity = iVar1;
            dst->_data = static_cast<int32_t*>(System::MemoryAllocator::MemAlloc(static_cast<uint64_t>(iVar1) << 2, 4));
        }
        int64_t i = 0;
        if (0 < dst->_size) {
            int32_t uVar4;
            do {
                int32_t* slot = &dst->_data[i];
                if (slot != nullptr) {
                    *slot = src->_data[i];
                }
                uVar4 = static_cast<int32_t>(i) + 1;
                i = i + 1;
            } while (uVar4 < dst->_size);
        }
        return dst;
    }
    return dst;
}
