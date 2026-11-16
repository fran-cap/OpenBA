// src/game/Action/region_graph_build.cpp
// Pathfinder coarse region-connectivity graph — constructor + coarse-cell (block) resize.
//
// Faithful 1:1 ports of three exe functions that build the pathfinder's coarse
// region-connectivity graph (RegionGraph, Action/PathSearch.h). Faithfulness is
// everything (a wrong field is silent corruption). The same MemAlloc/MemFree
// convention as src/game/Action/action_pf_region.cpp.
//
//   0x1400a8670  RegionGraph::Init        — the region graph constructor's body
//   0x14001e060  RegionBlocksResize       — Array<RegionBlock>::SetSize (stride 0x20)
//   0x14001e420  RegionBlocksFreeTail     — frees the dropped blocks' region lists
//
// Sub-callees:
//   0x14000f9e0  IntArrayAssign (src/game/Action/action_pf_pools.cpp)
//
// RegionBlock (stride 0x20): x, y, needsRebuild, needsAdjacency, the Array<int> of its
// region ids at +0x10. RegionBlocksResize deep-copies the +0x10 array of each surviving
// element via IntArrayAssign; the shrink path frees the dropped elements' arrays via
// RegionBlocksFreeTail. In the fresh-build path (Init's only call, growing an empty
// array) NEITHER is reached.

#include "Action/exe_bodies.h"
#include "Action/PathSearch.h"        // RegionGraph, RegionBlock, IntArrayAssign
#include "system/MemoryAllocator.h"   // System::MemoryAllocator::MemAlloc / MemFree
#include <cstdint>
#include <new>

namespace {

// MemAlloc shorthand matching the decompile's
// System::MemoryAllocator::MemAlloc(size, align) free-function form.
static inline void* MemAlloc(int64_t size, int align) {
    return System::MemoryAllocator::MemAlloc(size, align);
}

} // namespace

// ============================================================
// 0x14001e420 — RegionBlocksFreeTail(unused, first, count)
// ============================================================
// MemFree the +0x10 region-id array of `count` blocks starting at `first`, zeroing its
// data pointer and its {size, capacity} pair as it goes. The first argument is unused
// (retail's RCX). Leaf.
static void RegionBlocksFreeTail(RegionBlock* first, int count) {
    if (0 < count) {
        int64_t remaining = (int64_t)count;
        RegionBlock* block = first;
        do {
            System::MemoryAllocator::MemFree(block->regions._data);
            block->regions._data = nullptr;
            block->regions._size = 0;        // H7: retail clears size and capacity with one qword
            block->regions._capacity = 0;
            block++;
            remaining = remaining + -1;
        } while (remaining != 0);
    }
}

// ============================================================
// 0x14001e060 — RegionBlocksResize(blocks, newCount)
// ============================================================
// Resize the block array (element stride 0x20). On grow-with-realloc, allocate cap*0x20
// bytes and copy/zero-init each element (deep-copying the +0x10 int array via
// IntArrayAssign for surviving elements, zero-init for new ones); on in-place shrink free
// the dropped elements' arrays; on realloc free the OLD buffer's arrays (inline loop)
// then MemFree the old buffer.
static void RegionBlocksResize(RawArray<RegionBlock>* blocks, int newCount) {
    int64_t lVar11 = (int64_t)newCount;
    int oldCount = blocks->size;
    int64_t lVar10 = (int64_t)oldCount;
    if (newCount != oldCount) {
        RegionBlock* oldData = blocks->data;
        blocks->size = newCount;
        if (blocks->capacity < newCount) {
            blocks->capacity = newCount;
            if ((newCount == oldCount + 1) && (oldCount != 0)) {
                blocks->capacity = newCount * 3;
            }
            blocks->data = static_cast<RegionBlock*>(MemAlloc((int64_t)blocks->capacity << 5, 8));
        }
        int start = 0;
        if (oldData == blocks->data) {
            start = oldCount;
        }
        int64_t lVar9 = (int64_t)start;
        if (lVar9 < lVar11) {
            do {
                RegionBlock* dst = &blocks->data[lVar9];
                if (lVar9 < lVar10) {
                    if (dst != nullptr) {
                        const RegionBlock* src = &oldData[lVar9];
                        dst->x = src->x;
                        dst->y = src->y;
                        dst->needsRebuild = src->needsRebuild;
                        dst->needsAdjacency = src->needsAdjacency;
                        dst->regions._data = nullptr;
                        dst->regions._size = 0;        // H7: one qword store in retail
                        dst->regions._capacity = 0;
                        IntArrayAssign(&dst->regions, &src->regions);   // 0x14000f9e0
                    }
                } else {
                    if (dst != nullptr) {
                        dst->x = 0;                    // H7: one qword store in retail
                        dst->y = 0;
                        dst->needsRebuild = 0;         // H7: one word store in retail
                        dst->needsAdjacency = 0;
                        dst->regions._data = nullptr;
                        dst->regions._size = 0;        // H7: one qword store in retail
                        dst->regions._capacity = 0;
                    }
                }
                lVar9 = lVar9 + 1;
            } while (lVar9 < lVar11);
        }
        if (oldData == blocks->data) {
            if (newCount < oldCount) {
                RegionBlocksFreeTail(oldData + lVar11, oldCount - newCount);   // 0x14001e420
            }
        } else {
            if (0 < oldCount) {
                RegionBlock* block = oldData;
                do {
                    System::MemoryAllocator::MemFree(block->regions._data);
                    block->regions._data = nullptr;
                    block->regions._size = 0;        // H7: one qword store in retail
                    block->regions._capacity = 0;
                    block++;
                    lVar10 = lVar10 + -1;
                } while (lVar10 != 0);
            }
            System::MemoryAllocator::MemFree(oldData);
        }
    }
}

// ============================================================
// 0x1400a8670 — the RegionGraph constructor's body (graph, map, _, _, blocksWide, blocksHigh)
// ============================================================
// Zero-init the region graph, store the Map, blockWidth / blockHeight 8, the block
// counts, then size the block array to blocksWide*blocksHigh via 0x14001e060. The double
// loop initialises each block's x / y and dirty flags and appends its index onto the two
// dirty lists (dirtyRebuild / dirtyAdjacency) with the standard exe grow-and-copy.
// Finally grows the 0x40-entry label array the same way. Retail stores most pairs of
// fields with one qword; they are written as named stores here (H7).
void RegionGraph::Init(Map* mapArg, uint32_t blocksWideArg, int blocksHighArg) {
    search.generation = 1;
    search.nodes.data = nullptr;
    search.nodes.size = 0;
    search.nodes.capacity = 0;
    search.open.root = nullptr;
    search.open.count = 0;
    search.open.pool.chunks = nullptr;
    search.open.pool.freeList = nullptr;
    search.open.pool.growCount = 8;
    search.open.pool.stride = 0;
    search.open.pool.total = 0;
    search.open.pool.inUse = 0;
    search.open.pool.available = 0;
    map = mapArg;
    blockWidth = 8;
    blockHeight = 8;
    uint64_t uVar10 = (uint64_t)(int)blocksWideArg;
    blocksWide = static_cast<int32_t>(blocksWideArg);
    blocksHigh = blocksHighArg;
    regions.data = nullptr;
    regions.size = 0;
    regions.capacity = 0;
    blocks.data = nullptr;
    blocks.size = 0;
    blocks.capacity = 0;
    dirtyRebuild._data = nullptr;
    dirtyRebuild._size = 0;
    dirtyRebuild._capacity = 0;
    dirtyAdjacency._data = nullptr;
    dirtyAdjacency._size = 0;
    dirtyAdjacency._capacity = 0;
    freeRegions._data = nullptr;
    freeRegions._size = 0;
    freeRegions._capacity = 0;
    labels._data = nullptr;
    labels._size = 0;
    labels._capacity = 0;
    queue.tail = nullptr;
    queue.head = nullptr;
    queue.count = 0;
    queue.pool.chunks = nullptr;
    queue.pool.freeList = nullptr;
    queue.pool.growCount = 8;
    queue.pool.stride = 0;
    queue.pool.total = 0;
    queue.pool.inUse = 0;
    queue.pool.available = 0;
    RegionBlocksResize(&blocks, static_cast<int>(blocksWideArg * blocksHighArg));   // 0x14001e060
    int row = 0;
    int iVar14 = 0;
    if (0 < blocksHighArg) {
        int64_t rowStart = 0;
        int iVar15 = 0;
        do {
            int64_t index = rowStart;
            int iVar9 = 0;
            if (0 < (int)uVar10) {
                do {
                    RegionBlock& block = blocks.data[index];
                    block.x = iVar9;
                    blocks.data[index].y = row;
                    blocks.data[index].needsAdjacency = 1;
                    blocks.data[index].needsRebuild = 1;

                    // dirtyRebuild.append(iVar15 + iVar9) — the inlined Array<int>::SetSize(n, fill)
                    int iVar11 = dirtyRebuild._size + 1;
                    int iVar3 = dirtyRebuild._size;
                    if (iVar11 != iVar3) {
                        int32_t* oldData = dirtyRebuild._data;
                        dirtyRebuild._size = iVar11;
                        if (dirtyRebuild._capacity < iVar11) {
                            dirtyRebuild._capacity = iVar11;
                            if ((iVar11 == iVar3 + 1) && (iVar3 != 0)) {
                                dirtyRebuild._capacity = iVar11 * 3;
                            }
                            dirtyRebuild._data = static_cast<int32_t*>(MemAlloc((int64_t)dirtyRebuild._capacity << 2, 4));
                        }
                        int iVar6 = iVar14;
                        if (oldData == dirtyRebuild._data) {
                            iVar6 = iVar3;
                        }
                        for (int64_t i = (int64_t)iVar6; i < iVar11; i = i + 1) {
                            int32_t* slot = &dirtyRebuild._data[i];
                            if (slot != nullptr) {
                                *slot = (i < iVar3) ? oldData[i] : iVar15 + iVar9;
                            }
                        }
                        if (oldData != dirtyRebuild._data) {
                            System::MemoryAllocator::MemFree(oldData);
                        }
                    }

                    // dirtyAdjacency.append(iVar15 + iVar9)
                    iVar11 = dirtyAdjacency._size + 1;
                    iVar3 = dirtyAdjacency._size;
                    if (iVar11 != iVar3) {
                        int32_t* oldData = dirtyAdjacency._data;
                        dirtyAdjacency._size = iVar11;
                        if (dirtyAdjacency._capacity < iVar11) {
                            dirtyAdjacency._capacity = iVar11;
                            if ((iVar11 == iVar3 + 1) && (iVar3 != 0)) {
                                dirtyAdjacency._capacity = iVar11 * 3;
                            }
                            dirtyAdjacency._data = static_cast<int32_t*>(MemAlloc((int64_t)dirtyAdjacency._capacity << 2, 4));
                        }
                        int iVar6 = iVar14;
                        if (oldData == dirtyAdjacency._data) {
                            iVar6 = iVar3;
                        }
                        for (int64_t i = (int64_t)iVar6; i < iVar11; i = i + 1) {
                            int32_t* slot = &dirtyAdjacency._data[i];
                            if (slot != nullptr) {
                                *slot = (i < iVar3) ? oldData[i] : iVar15 + iVar9;
                            }
                        }
                        if (oldData != dirtyAdjacency._data) {
                            System::MemoryAllocator::MemFree(oldData);
                        }
                    }
                    iVar9 = iVar9 + 1;
                    uVar10 = (uint64_t)blocksWideArg;
                    index = index + 1;
                } while (iVar9 < (int)blocksWideArg);
            }
            row = row + 1;
            iVar15 = iVar15 + (int)uVar10;
            rowStart = rowStart + (int64_t)uVar10;
        } while (row < blocksHighArg);
    }

    // labels.SetSize(0x40) — the inlined Array<int>::SetSize(n) (no fill for new elements)
    int oldSize = labels._size;
    if (oldSize != 0x40) {
        int32_t* oldData = labels._data;
        labels._size = 0x40;
        if (labels._capacity < 0x40) {
            labels._capacity = 0x40;
            if (oldSize == 0x3f) {
                labels._capacity = 0xc0;
            }
            labels._data = static_cast<int32_t*>(MemAlloc((int64_t)labels._capacity << 2, 4));
        }
        if (oldData == labels._data) {
            iVar14 = oldSize;
        }
        for (int64_t i = (int64_t)iVar14; i < 0x40; i = i + 1) {
            int32_t* slot;
            if ((i < oldSize) && (slot = &labels._data[i], slot != nullptr)) {
                *slot = oldData[i];
            }
        }
        if (oldData != labels._data) {
            System::MemoryAllocator::MemFree(oldData);
        }
    }
}
