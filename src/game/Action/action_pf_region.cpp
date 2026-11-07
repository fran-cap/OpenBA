// src/game/Action/action_pf_region.cpp
// Wave 3 (Action) / Cluster A — region-graph build/merge/flood-fill family.
//
// Faithful 1:1 ports of the coarse-region connectivity-graph helpers used by
// long-distance routing. Each body is a literal transcription of the Ghidra decompile
// for the exe function 0x1400aXXXX; none is runtime-exercised on the autostart path,
// so faithfulness is everything (a wrong port is undetectable silent corruption).
//
// B.4 ACT-a: the bodies are RegionGraph's members (src/game/Action/PathSearch.h) and
// read it by name:
//   +0x50 map (size = its edge, cells = the MapCell array: regionId +0x18, terrainType +0x29)
//   +0x58/+0x5c blockWidth / blockHeight (cells per block), +0x68 regions (stride 0x30:
//   centroid x/y, owning block, neighbours, borderCells), +0x78 blocks (stride 0x20: x, y,
//   needsRebuild, needsAdjacency, regions), +0x88/+0x98 the two dirty block lists, +0xa8
//   freeRegions, +0xb8 labels (one int per cell of a block), +0xc8 the label flood fill's
//   work queue.
// The region-pair cache (0x1400a5b30) is RegionPairCache::Store.
//
// Sub-callees are called by name:
//   0x1400021d0  Collection::Array<int>::SetSize     — the arrays' SetSize
//   0x14001e210  RegionNodesResize                   0x14001df40  RegionCellQueue::NewNode
//   0x14001d830  SearchNodesResize                   0x14001ca60  WorkCellBST_Remove_ca60
//   0x14001c940  AvlInsert30                         0x140132010  WorkImpl_Sqrtf_132010
//   0x14001dc00  RegionGraph::Run

#include "Action/exe_bodies.h"
#include "Action/PathSearch.h"
#include "Action/PathAvl.h"
#include "Game/Map.h"
#include "Work/exe_bodies.h"          // WorkCellBST_Remove_ca60, WorkImpl_Sqrtf_132010
#include "system/MemoryAllocator.h"   // System::MemoryAllocator::MemAlloc / MemFree
#include "hw_watch.h"
#include "stress_harness.h"           // g_verifyOn (PATHDIST shadow in PathLength; one branch when off)
#include "Action/region_search.h"     // RegionSearch::Memo* (HITCH_PAIRSPREAD, NOT RETAIL)
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include "ext/winter_ice/walk_type.h"   // EXT SEAM (NOT RETAIL, WINTERICE.md 5.2): ext.walk_on_ice, a frozen lake cell reads as type 9

namespace {

// MemAlloc/MemFree shorthands matching the decompile's
// System::MemoryAllocator::MemAlloc(size, align) / MemFree(ptr) free-function form.
static inline void* MemAlloc(int64_t size, int align) {
    return System::MemoryAllocator::MemAlloc(static_cast<uint64_t>(size), align);
}

// The walkable terrain types the flood fills spread over (retail's 0xe1f mask) on a cell's terrain type
// (+0x29), as ext.walk_on_ice's seam reads it (WINTERICE.md 5.2: a frozen lake cell reads as 9).
inline bool Walkable(const MapCell& cell) {
    return ((1 << (Ext::WalkOnIce::TypeOf(cell) & 0x1f)) & 0xe1fU) != 0;
}

} // namespace

// ============================================================
// 0x1400a8200 — free a region
// ============================================================
// Reset region `region`: block -1, clear its two int arrays via 0x1400021d0(..., 0),
// centroid -1, then append the id to freeRegions with the standard exe grow-and-copy.
void RegionGraph::FreeRegion(int region) {
    int64_t lVar7 = static_cast<int64_t>(region);
    regions.data[lVar7].block = -1;
    // 0x1400021d0 leaves a GROWN tail as the allocation found it (a trivial element);
    // SetSize(0) never grows.
    regions.data[lVar7].neighbours.SetSize(0);    // 0x1400021d0
    regions.data[lVar7].borderCells.SetSize(0);   // 0x1400021d0
    regions.data[lVar7].x = -1;
    regions.data[lVar7].y = -1;
    int iVar2 = freeRegions._size;
    int iVar6 = iVar2 + 1;
    if (iVar6 != iVar2) {
        int32_t* pvVar3 = freeRegions._data;
        freeRegions._size = iVar6;
        if (freeRegions._capacity < iVar6) {
            freeRegions._capacity = iVar6;
            if (iVar2 != 0) {
                freeRegions._capacity = iVar6 * 3;
            }
            freeRegions._data = static_cast<int32_t*>(MemAlloc(static_cast<int64_t>(freeRegions._capacity) << 2, 4));
        }
        int iVar4 = 0;
        if (pvVar3 == freeRegions._data) {
            iVar4 = iVar2;
        }
        for (int64_t lVar7b = static_cast<int64_t>(iVar4); lVar7b < iVar6; lVar7b = lVar7b + 1) {
            int* piVar1 = &freeRegions._data[lVar7b];
            if (piVar1 != nullptr) {
                int v = region;
                if (lVar7b < iVar2) {
                    v = pvVar3[lVar7b];
                }
                *piVar1 = v;
            }
        }
        if (pvVar3 != freeRegions._data) {
            System::MemoryAllocator::MemFree(pvVar3);
        }
    }
}

// ============================================================
// 0x1400a8350 — allocate a region id
// ============================================================
// Pop an id from freeRegions. If it is empty, GROW the region array: id = its size,
// resized (0x14001e210) to id+1 from a stack template region whose two arrays are
// released afterwards. Otherwise pop the last id (0x1400021d0 with size-1).
int RegionGraph::AllocRegion() {
    int iVar1 = freeRegions._size;
    uint32_t uVar2;
    if (iVar1 < 1) {
        uVar2 = static_cast<uint32_t>(regions.size);
        {
            // The stack template: its two arrays start empty (x / y / block are not
            // written). Retail releases borderCells then neighbours at the end of the
            // block: the inlined ~Array of each (MemFree(data); data = 0; size/cap = 0).
            RegionNode t;
            RegionNodesResize(&regions, regions.size + 1, &t);   // 0x14001e210
        }
    } else {
        uVar2 = static_cast<uint32_t>(freeRegions._data[static_cast<int64_t>(iVar1) + -1]);
        // A pop (n-1), a pure shrink: nothing is grown.
        freeRegions.SetSize(iVar1 + -1);   // 0x1400021d0
    }
    return static_cast<int>(uVar2);
}

// ============================================================
// 0x1400a8420 — queue a block for an adjacency rebuild
// ============================================================
// Set the block's needsAdjacency byte; if it was not already set, append the block to
// dirtyAdjacency with the standard exe grow-and-copy. Leaf (MemAlloc/MemFree only).
void RegionGraph::MarkAdjacencyDirty(int block) {
    if (blocks.data[block].needsAdjacency == '\0') {
        blocks.data[block].needsAdjacency = 1;
        int iVar2 = dirtyAdjacency._size;
        int iVar7 = iVar2 + 1;
        if (iVar7 != iVar2) {
            int32_t* pvVar3 = dirtyAdjacency._data;
            dirtyAdjacency._size = iVar7;
            if (dirtyAdjacency._capacity < iVar7) {
                dirtyAdjacency._capacity = iVar7;
                if (iVar2 != 0) {
                    dirtyAdjacency._capacity = iVar7 * 3;
                }
                dirtyAdjacency._data = static_cast<int32_t*>(MemAlloc(static_cast<int64_t>(dirtyAdjacency._capacity) << 2, 4));
            }
            int iVar4 = 0;
            if (pvVar3 == dirtyAdjacency._data) {
                iVar4 = iVar2;
            }
            for (int64_t lVar6 = static_cast<int64_t>(iVar4); lVar6 < iVar7; lVar6 = lVar6 + 1) {
                int* piVar1 = &dirtyAdjacency._data[lVar6];
                if (piVar1 != nullptr) {
                    int v = block;
                    if (lVar6 < iVar2) {
                        v = pvVar3[lVar6];
                    }
                    *piVar1 = v;
                }
            }
            if (pvVar3 != dirtyAdjacency._data) {
                System::MemoryAllocator::MemFree(pvVar3);
            }
        }
    }
}

// ============================================================
// 0x1400a7480 — rebuild the adjacency of a block's regions
// ============================================================
// For each region of the block: clear its neighbour list (0x1400021d0(, 0)); then for
// each of its border cells look up the cell's region id in the Map, and if >= 0 and not
// already listed, append it (standard exe grow-and-copy). The duplicate scan uses the
// `iVar8 < 0` early-out exactly as decompiled.
void RegionGraph::RebuildAdjacency(int block) {
    int local_res10 = 0;
    RegionBlock* lVar4 = &blocks.data[block];
    if (0 < lVar4->regions._size) {
        int64_t local_res18 = 0;
        do {
            RegionNode* lVar11 = &regions.data[lVar4->regions._data[local_res18]];
            // A trivial element; SetSize(0) never grows.
            lVar11->neighbours.SetSize(0);   // 0x1400021d0
            int iVar12 = 0;
            if (0 < lVar11->borderCells._size) {
                int64_t lVar10 = 0;
                do {
                    int iVar1 = map->cells.GetData()[lVar11->borderCells._data[lVar10]].regionId;
                    if (-1 < iVar1) {
                        int iVar9 = lVar11->neighbours._size;
                        int iVar8 = 0;
                        int64_t lVar7 = 0;
                        if (0 < iVar9) {
                            int* piVar5 = lVar11->neighbours._data;
                            do {
                                if (*piVar5 == iVar1) {
                                    if (-1 < iVar8) goto LAB_1400a75f8;
                                    break;
                                }
                                lVar7 = lVar7 + 1;
                                iVar8 = iVar8 + 1;
                                piVar5 = piVar5 + 1;
                            } while (lVar7 < iVar9);
                        }
                        iVar8 = lVar11->neighbours._size;
                        iVar9 = iVar9 + 1;
                        if (iVar9 != iVar8) {
                            int32_t* pvVar2 = lVar11->neighbours._data;
                            lVar11->neighbours._size = iVar9;
                            if (lVar11->neighbours._capacity < iVar9) {
                                lVar11->neighbours._capacity = iVar9;
                                if ((iVar9 == iVar8 + 1) && (iVar8 != 0)) {
                                    lVar11->neighbours._capacity = iVar9 * 3;
                                }
                                lVar11->neighbours._data = static_cast<int32_t*>(MemAlloc(static_cast<int64_t>(lVar11->neighbours._capacity) << 2, 4));
                            }
                            int iVar3 = 0;
                            if (pvVar2 == lVar11->neighbours._data) {
                                iVar3 = iVar8;
                            }
                            for (int64_t lVar7b = static_cast<int64_t>(iVar3); lVar7b < iVar9; lVar7b = lVar7b + 1) {
                                int* piVar5 = &lVar11->neighbours._data[lVar7b];
                                if (piVar5 != nullptr) {
                                    int v = iVar1;
                                    if (lVar7b < iVar8) {
                                        v = pvVar2[lVar7b];
                                    }
                                    *piVar5 = v;
                                }
                            }
                            if (pvVar2 != lVar11->neighbours._data) {
                                System::MemoryAllocator::MemFree(pvVar2);
                            }
                        }
                    }
LAB_1400a75f8:
                    iVar12 = iVar12 + 1;
                    lVar10 = lVar10 + 1;
                } while (iVar12 < lVar11->borderCells._size);
            }
            local_res10 = local_res10 + 1;
            local_res18 = local_res18 + 1;
        } while (local_res10 < lVar4->regions._size);
    }
}

// ============================================================
// 0x1400a7670 — write a block's region ids into the Map
// ============================================================
// For each (row 0..blockHeight, col 0..blockWidth) of the block: read the label grid
// value-1; if < 0 write -1, else map it through the block's region list; destination
// cell = ((row + y*blockHeight) * map->size + col) + x*blockWidth.
//
// NOTE (faithfulness trap): the decompile reuses `iVar4` as both the inner loop bound
// (reloaded from blockWidth each col) and the label; transcribed verbatim.
void RegionGraph::StampRegionIds(int block) {
    int iVar1 = blockHeight;
    int iVar4 = blockWidth;
    int iVar7 = 0;
    int64_t lVar6 = 0;
    const RegionBlock* piVar11 = &blocks.data[block];
    int iVar2 = piVar11->y;
    int iVar5 = piVar11->x * iVar4;
    if (0 < iVar1) {
        do {
            int iVar9 = 0;
            int64_t lVar10 = 0;
            if (0 < iVar4) {
                do {
                    int iVar3 = (iVar7 + iVar2 * iVar1) * map->size + iVar9;
                    iVar4 = labels._data[iVar4 * lVar6 + lVar10] + -1;
                    uint32_t uVar8;
                    if (iVar4 < 0) {
                        uVar8 = 0xffffffff;
                    } else {
                        uVar8 = static_cast<uint32_t>(piVar11->regions._data[iVar4]);
                    }
                    iVar9 = iVar9 + 1;
                    lVar10 = lVar10 + 1;
                    map->cells.GetData()[static_cast<int64_t>(iVar3 + iVar5)].regionId = static_cast<int32_t>(uVar8);
                    iVar4 = blockWidth;
                } while (iVar9 < iVar4);
            }
            iVar7 = iVar7 + 1;
            lVar6 = lVar6 + 1;
        } while (iVar7 < blockHeight);
    }
}

// ============================================================
// 0x1400a7750 — add a cell's outward border cells to a region
// ============================================================
// For the 3x3 ring around (x-1..x+1, y-1..y+1): each ring cell that is on the map and
// OUTSIDE the block [blockX0 .. blockX0+blockWidth) x [blockY0 .. blockY0+blockHeight)
// has its flat index (map->size*y + x) appended to the region's borderCells, with the
// standard grow-and-copy and a duplicate-skip scan. Outer 3 rows via local_64=-1..1,
// inner 3 cols via lVar14=3 countdown. Leaf (MemAlloc/MemFree).
void RegionGraph::AddBorderCells(int region, int param_3, int param_4, int param_5, int param_6) {
    int iVar1 = blockWidth;
    int iVar2 = blockHeight;
    int iVar3 = map->size;
    int local_64 = -1;
    do {
        int64_t lVar14 = 3;
        int iVar13 = param_6 + local_64;
        int iVar15 = param_5 + -1;
        do {
            if (((((-1 < iVar15) && (-1 < iVar13)) && (iVar15 < iVar3)) && (iVar13 < iVar3)) &&
                (((iVar15 < param_3 || (iVar13 < param_4)) ||
                  ((param_3 + -1 + iVar1 < iVar15 || (param_4 + -1 + iVar2 < iVar13)))))) {
                int iVar9 = 0;
                int64_t lVar8 = 0;
                RegionNode* lVar12 = &regions.data[region];
                int iVar11 = map->size * iVar13 + iVar15;
                if (0 < static_cast<int64_t>(lVar12->borderCells._size)) {
                    int* piVar6 = lVar12->borderCells._data;
                    do {
                        if (*piVar6 == iVar11) {
                            if (-1 < iVar9) goto LAB_1400a7906;
                            break;
                        }
                        lVar8 = lVar8 + 1;
                        iVar9 = iVar9 + 1;
                        piVar6 = piVar6 + 1;
                    } while (lVar8 < lVar12->borderCells._size);
                }
                iVar9 = lVar12->borderCells._size;
                int iVar10 = lVar12->borderCells._size + 1;
                if (iVar10 != iVar9) {
                    int32_t* pvVar4 = lVar12->borderCells._data;
                    lVar12->borderCells._size = iVar10;
                    if (lVar12->borderCells._capacity < iVar10) {
                        lVar12->borderCells._capacity = iVar10;
                        if ((iVar10 == iVar9 + 1) && (iVar9 != 0)) {
                            lVar12->borderCells._capacity = iVar10 * 3;
                        }
                        lVar12->borderCells._data = static_cast<int32_t*>(MemAlloc(static_cast<int64_t>(lVar12->borderCells._capacity) << 2, 4));
                    }
                    int iVar5 = 0;
                    if (pvVar4 == lVar12->borderCells._data) {
                        iVar5 = iVar9;
                    }
                    for (lVar8 = static_cast<int64_t>(iVar5); lVar8 < iVar10; lVar8 = lVar8 + 1) {
                        int* piVar6 = &lVar12->borderCells._data[lVar8];
                        if (piVar6 != nullptr) {
                            int v = iVar11;
                            if (lVar8 < iVar9) {
                                v = pvVar4[lVar8];
                            }
                            *piVar6 = v;
                        }
                    }
                    if (pvVar4 != lVar12->borderCells._data) {
                        System::MemoryAllocator::MemFree(pvVar4);
                    }
                }
            }
LAB_1400a7906:
            iVar15 = iVar15 + 1;
            lVar14 = lVar14 + -1;
        } while (lVar14 != 0);
        local_64 = local_64 + 1;
        if (1 < local_64) {
            return;
        }
    } while (true);
}

// ============================================================
// 0x1400a7950 — build one region of a block -> region id
// ============================================================
// Allocate a region id (0x1400a8350), store the block in it, then scan every cell of the
// block; for each cell whose label == `label`, accumulate the centroid sums and, if it
// sits on the block border (col/row == 0 or == width/height-1), add its outward border
// cells (0x1400a7750). Finally write the integer-averaged centroid. Returns the id
// (zero-extended).
//
// Faithfulness traps: the id is used both as the region selector and passed to 0x1400a7750.
// The averages use integer division. `iVar9` is reloaded from blockHeight inside the inner
// loop; transcribed verbatim.
uint64_t RegionGraph::BuildRegion(int block, int label) {
    uint32_t uVar2 = static_cast<uint32_t>(AllocRegion());   // 0x1400a8350
    uint64_t uVar12 = static_cast<uint64_t>(static_cast<int>(uVar2));
    int iVar10 = 0;
    int iVar11 = 0;
    const RegionBlock* piVar5 = &blocks.data[block];
    int iVar16 = 0;
    RegionNode* piVar4 = &regions.data[uVar12];
    int iVar15 = 0;
    int64_t lVar7 = 0;
    piVar4->block = block;
    int iVar9 = blockHeight;
    int iVar13 = piVar5->x * blockWidth;
    int iVar14 = piVar5->y * iVar9;
    if (0 < blockWidth) {
        do {
            int iVar6 = 0;
            int64_t lVar8 = 0;
            if (0 < iVar9) {
                do {
                    if (labels._data[blockWidth * lVar7 + lVar8] == label) {
                        int iVar3 = iVar6 + iVar13;
                        iVar16 = iVar16 + 1;
                        iVar10 = iVar10 + iVar3;
                        int iVar1 = iVar15 + iVar14;
                        iVar11 = iVar11 + iVar1;
                        if ((((lVar8 == 0) || (lVar7 == 0)) || (iVar6 == blockWidth + -1)) ||
                            (iVar15 == iVar9 + -1)) {
                            AddBorderCells(static_cast<int>(uVar2), iVar13, iVar14, iVar3, iVar1);   // 0x1400a7750
                        }
                    }
                    uVar12 = static_cast<uint64_t>(uVar2);
                    iVar9 = blockHeight;
                    iVar6 = iVar6 + 1;
                    lVar8 = lVar8 + 1;
                } while (iVar6 < iVar9);
            }
            iVar15 = iVar15 + 1;
            lVar7 = lVar7 + 1;
        } while (iVar15 < blockWidth);
    }
    piVar4->x = iVar10 / iVar16;
    piVar4->y = iVar11 / iVar16;
    return uVar12 & 0xffffffff;
}

// ============================================================
// 0x1400a7ad0 — flood-fill one label over a block
// ============================================================
// Seed the work queue with (x, y) via 0x14001df40, then pop-and-process until it is
// empty. For each popped (x, y): if its label is 0 and its Map cell is walkable (mask
// 0xe1f on the terrain type), expand the contiguous walkable run [x-... x+...], paint
// that run of labels, then queue the row above (y-1) and below (y+1) for every walkable,
// unlabelled cell in the run. The queue link bookkeeping is inlined exactly as decompiled.
//
// Faithfulness traps: the queued value packs (x low, y high). The run scan walks the Map
// cells by -1 / +1 reading the terrain type. The two enqueue loops re-read param_2/param_3
// from local_res10/local_res18 at each iteration, exactly as the decompiler shows.
void RegionGraph::FloodLabel(int param_2, int param_3, uint32_t param_4, uint32_t param_5, uint16_t param_6) {
    PathCellXY local_res8;
    local_res8.x = static_cast<int32_t>(param_4);
    local_res8.y = static_cast<int32_t>(param_5);
    int local_res10 = param_2;
    int local_res18 = param_3;
    RegionCellQueue::Node* puVar3 = queue.NewNode(&local_res8);   // 0x14001df40
    if (queue.tail == nullptr) {
        queue.head = puVar3;
    } else {
        queue.tail->next = puVar3;
        puVar3->prev = queue.tail;
    }
    queue.count = queue.count + 1;
    queue.tail = puVar3;
    int iVar12 = queue.count;
    do {
        if (iVar12 < 1) {
            return;
        }
        RegionCellQueue::Node* puVar2 = queue.head;
        local_res8 = puVar2->value;
        if (puVar2 == queue.tail) {
            queue.tail = puVar2->prev;
        }
        queue.head = puVar2->next;
        if (puVar2->next != nullptr) {
            puVar2->next->prev = puVar2->prev;
        }
        if (puVar2->prev != nullptr) {
            puVar2->prev->next = puVar2->next;
        }
        puVar2->next = nullptr;
        puVar2->prev = nullptr;
        queue.count = queue.count + -1;
        queue.pool.Push(puVar2);
        int iVar7 = blockWidth + -1;
        uint32_t uVar14 = static_cast<uint32_t>(local_res8.x);
        int64_t lVar10 = static_cast<int64_t>(static_cast<int>(uVar14));
        iVar12 = local_res8.y;
        if (labels._data[static_cast<int64_t>(blockWidth) * static_cast<int64_t>(iVar12) + lVar10] == 0) {
            int64_t lVar13 = static_cast<int64_t>(param_3);
            int64_t lVar15 = static_cast<int64_t>(param_2);
            int64_t lVar11 = static_cast<int64_t>(map->size);
            const MapCell* lVar5 = map->cells.GetData();
            if (Walkable(lVar5[(iVar12 + lVar13) * lVar11 + lVar15 + lVar10])) {
                uint64_t uVar4 = static_cast<uint64_t>(static_cast<uint32_t>(local_res8.x));
                uint32_t uVar6 = uVar14;
                if (-1 < static_cast<int>(uVar14 - 1)) {
                    const MapCell* pbVar9 = &lVar5[(iVar12 + lVar13) * lVar11 + lVar15 + lVar10] - 1;
                    do {
                        uVar6 = static_cast<uint32_t>(uVar4);
                        if (!Walkable(*pbVar9)) break;
                        uVar6 = static_cast<uint32_t>(uVar4) - 1;
                        uVar4 = static_cast<uint64_t>(uVar6);
                        pbVar9 = pbVar9 - 1;
                    } while (0 < static_cast<int>(uVar6));
                }
                int iVar8 = uVar14 + 1;
                uVar4 = static_cast<uint64_t>(static_cast<uint32_t>(local_res8.x)) |
                        (static_cast<uint64_t>(static_cast<uint32_t>(local_res8.y)) << 32);
                if (iVar8 <= iVar7) {
                    const MapCell* pbVar9 = &lVar5[(iVar12 + lVar13) * lVar11 + lVar15 + lVar10] + 1;
                    do {
                        if (!Walkable(*pbVar9)) break;
                        iVar8 = iVar8 + 1;
                        uVar4 = static_cast<uint64_t>(static_cast<int>(uVar4) + 1);
                        pbVar9 = pbVar9 + 1;
                    } while (iVar8 <= iVar7);
                }
                lVar10 = static_cast<int64_t>(static_cast<int>(uVar6));
                iVar8 = static_cast<int>(uVar4);
                if (lVar10 <= iVar8) {
                    do {
                        int64_t lVar5b = static_cast<int64_t>(blockWidth) * static_cast<int64_t>(iVar12) + lVar10;
                        lVar10 = lVar10 + 1;
                        labels._data[lVar5b] = static_cast<int32_t>(static_cast<uint32_t>(param_6));
                    } while (lVar10 <= iVar8);
                }
                if (-1 < static_cast<int>(uVar6 - 1)) {
                    uVar6 = uVar6 - 1;
                }
                if (iVar8 + 1 <= iVar7) {
                    iVar8 = iVar8 + 1;
                }
                if (0 < iVar12) {
                    iVar12 = iVar12 + -1;
                    uVar14 = uVar6;
                    for (lVar10 = static_cast<int64_t>(static_cast<int>(uVar6)); lVar10 <= iVar8; lVar10 = lVar10 + 1) {
                        if ((labels._data[static_cast<int64_t>(blockWidth) * static_cast<int64_t>(iVar12) + lVar10] == 0)
                            && Walkable(map->cells.GetData()[(iVar12 + lVar13) * static_cast<int64_t>(map->size) +
                                                   static_cast<int64_t>(local_res10) + lVar10])) {
                            PathCellXY pack;
                            pack.x = static_cast<int32_t>(uVar14);
                            pack.y = iVar12;
                            puVar3 = queue.NewNode(&pack);   // 0x14001df40
                            if (queue.tail == nullptr) {
                                queue.head = puVar3;
                            } else {
                                queue.tail->next = puVar3;
                                puVar3->prev = queue.tail;
                            }
                            queue.count = queue.count + 1;
                            queue.tail = puVar3;
                        }
                        uVar14 = uVar14 + 1;
                    }
                    iVar12 = local_res8.y;
                }
                param_2 = local_res10;
                param_3 = local_res18;
                if (iVar12 < blockHeight + -1) {
                    iVar12 = iVar12 + 1;
                    for (lVar10 = static_cast<int64_t>(static_cast<int>(uVar6)); ((local_res10 = param_2), (local_res18 = param_3),
                         lVar10 <= iVar8); lVar10 = lVar10 + 1) {
                        if ((labels._data[static_cast<int64_t>(blockWidth) * static_cast<int64_t>(iVar12) + lVar10] == 0)
                            && Walkable(map->cells.GetData()[(iVar12 + lVar13) * static_cast<int64_t>(map->size) +
                                                   static_cast<int64_t>(param_2) + lVar10])) {
                            PathCellXY pack;
                            pack.x = static_cast<int32_t>(uVar6);
                            pack.y = iVar12;
                            puVar3 = queue.NewNode(&pack);   // 0x14001df40
                            if (queue.tail == nullptr) {
                                queue.head = puVar3;
                            } else {
                                queue.tail->next = puVar3;
                                puVar3->prev = queue.tail;
                            }
                            queue.count = queue.count + 1;
                            queue.tail = puVar3;
                        }
                        uVar6 = uVar6 + 1;
                        param_2 = local_res10;
                        param_3 = local_res18;
                    }
                }
            }
        }
        iVar12 = queue.count;
    } while (true);
}

// ============================================================
// 0x1400a7f20 — label every region of a block -> region count
// ============================================================
// Clear the label grid, then sweep every cell of the block; for each walkable,
// unlabelled cell start a flood fill (0x1400a7ad0) with the next label. Returns
// (label-1) = the number of regions found in the block.
int RegionGraph::LabelBlock(int block) {
    int iVar5 = 0;
    if (0 < labels._size) {
        int64_t lVar8 = 0;
        do {
            iVar5 = iVar5 + 1;
            labels._data[lVar8] = 0;
            lVar8 = lVar8 + 1;
        } while (iVar5 < labels._size);
    }
    iVar5 = blockHeight;
    int iVar9 = 0;
    const RegionBlock* piVar1 = &blocks.data[block];
    int64_t lVar8 = 0;
    uint16_t uVar2 = 1;
    int iVar6 = piVar1->x * blockWidth;
    int iVar7 = piVar1->y * iVar5;
    if (0 < blockWidth) {
        do {
            int iVar4 = 0;
            int64_t lVar3 = 0;
            if (0 < iVar5) {
                do {
                    if (Walkable(map->cells.GetData()[(iVar7 + lVar8) * static_cast<int64_t>(map->size) +
                                            static_cast<int64_t>(iVar6) + lVar3]) &&
                        (labels._data[blockWidth * lVar8 + lVar3] == 0)) {
                        FloodLabel(iVar6, iVar7, iVar4, iVar9, uVar2);   // 0x1400a7ad0
                        uVar2 = uVar2 + 1;
                    }
                    iVar5 = blockHeight;
                    iVar4 = iVar4 + 1;
                    lVar3 = lVar3 + 1;
                } while (iVar4 < iVar5);
            }
            iVar9 = iVar9 + 1;
            lVar8 = lVar8 + 1;
        } while (iVar9 < blockWidth);
    }
    return uVar2 - 1;
}

// ============================================================
// 0x1400a80a0 — rebuild one block
// ============================================================
// Free every region of the block (0x1400a8200 each), clear its region list
// (0x1400021d0), label the block (0x1400a7f20), then for each label build its region
// (0x1400a7950) and append the id to the block's list (standard grow-and-copy).
// Finally stamp the region ids into the Map (0x1400a7670).
void RegionGraph::RebuildBlock(uint32_t block) {
    uint64_t uVar13 = static_cast<uint64_t>(static_cast<int>(block));
    int iVar10 = 0;
    RegionBlock* lVar8 = &blocks.data[uVar13];
    if (0 < lVar8->regions._size) {
        int64_t lVar7 = 0;
        do {
            FreeRegion(lVar8->regions._data[lVar7]);   // 0x1400a8200
            iVar10 = iVar10 + 1;
            lVar7 = lVar7 + 1;
        } while (iVar10 < lVar8->regions._size);
    }
    // A trivial element; SetSize(0) never grows.
    lVar8->regions.SetSize(0);   // 0x1400021d0
    iVar10 = LabelBlock(static_cast<int>(block));   // 0x1400a7f20
    int iVar14 = 0;
    uint32_t uVar12 = block;
    if (0 < iVar10) {
        do {
            iVar14 = iVar14 + 1;
            uint64_t uVar5 = BuildRegion(static_cast<int>(uVar13), iVar14);   // 0x1400a7950
            int iVar2 = lVar8->regions._size;
            int iVar11 = iVar2 + 1;
            if (iVar11 != iVar2) {
                int32_t* pvVar3 = lVar8->regions._data;
                lVar8->regions._size = iVar11;
                if (lVar8->regions._capacity < iVar11) {
                    lVar8->regions._capacity = iVar11;
                    if (iVar2 != 0) {
                        lVar8->regions._capacity = iVar11 * 3;
                    }
                    lVar8->regions._data = static_cast<int32_t*>(MemAlloc(static_cast<int64_t>(lVar8->regions._capacity) << 2, 4));
                }
                int iVar4 = 0;
                if (pvVar3 == lVar8->regions._data) {
                    iVar4 = iVar2;
                }
                for (int64_t lVar7 = static_cast<int64_t>(iVar4); lVar7 < iVar11; lVar7 = lVar7 + 1) {
                    int32_t* puVar1 = &lVar8->regions._data[lVar7];
                    if (puVar1 != nullptr) {
                        int32_t uVar9;
                        if (lVar7 < iVar2) {
                            uVar9 = pvVar3[lVar7];
                        } else {
                            uVar9 = static_cast<int32_t>(uVar5);
                        }
                        *puVar1 = uVar9;
                    }
                }
                if (pvVar3 != lVar8->regions._data) {
                    System::MemoryAllocator::MemFree(pvVar3);
                }
                uVar13 = static_cast<uint64_t>(block);
            }
            uVar12 = static_cast<uint32_t>(uVar13);
        } while (iVar14 < iVar10);
    }
    StampRegionIds(static_cast<int>(uVar12));   // 0x1400a7670
}

// ============================================================
// 0x1400a8a90 — rebuild every dirty block
// ============================================================
// dirtyRebuild: for each block RebuildBlock (0x1400a80a0) and clear its needsRebuild.
// dirtyAdjacency: for each block RebuildAdjacency (0x1400a7480) and clear its
// needsAdjacency. Then clear both lists (0x1400021d0(, 0)) and size the search nodes
// to the region count (0x14001d830).
void RegionGraph::Flush() {
    if ((0 < dirtyRebuild._size) || (0 < dirtyAdjacency._size)) {
        uint64_t uVar5 = 0;
        uint64_t uVar3 = uVar5;
        int64_t uVar4 = 0;
        uint32_t uVar2;
        if (0 < dirtyRebuild._size) {
            do {
                RebuildBlock(static_cast<uint32_t>(dirtyRebuild._data[uVar4]));   // 0x1400a80a0
                uVar2 = static_cast<int>(uVar3) + 1;
                blocks.data[dirtyRebuild._data[uVar4]].needsRebuild = 0;
                uVar3 = static_cast<uint64_t>(uVar2);
                uVar4 = uVar4 + 1;
            } while (static_cast<int>(uVar2) < dirtyRebuild._size);
        }
        uVar3 = uVar5;
        int64_t i5 = 0;
        if (0 < dirtyAdjacency._size) {
            do {
                RebuildAdjacency(dirtyAdjacency._data[i5]);   // 0x1400a7480
                uVar2 = static_cast<int>(uVar3) + 1;
                int* piVar1 = &dirtyAdjacency._data[i5];
                i5 = i5 + 1;
                blocks.data[*piVar1].needsAdjacency = 0;
                uVar3 = static_cast<uint64_t>(uVar2);
            } while (static_cast<int>(uVar2) < dirtyAdjacency._size);
        }
        // Both are SetSize(0) of a trivial element — no growth.
        dirtyRebuild.SetSize(0);     // 0x1400021d0
        dirtyAdjacency.SetSize(0);   // 0x1400021d0
        SearchNodesResize(&search.nodes, regions.size);   // 0x14001d830
        // NOT RETAIL (HITCH_PAIRSPREAD): the graph changed, so PathLength's memo is dropped.
        RegionSearch::NoteGraphChanged();
    }
}

// ============================================================
// 0x1400a8ba0 — region path length
// ============================================================
// Flush any dirty blocks, then run the region A* (0x14001dc00) into a LARGE local int
// array local_8008[8190] (declared at full size per the brief), which returns the node
// count of the region path. Sum the Euclidean distances between successive region
// centroids via sqrtf (0x140132010), accumulating into an int. Returns the sum (0 if the
// path has < 2 nodes).
//
// FAITHFULNESS / STACK: local_8008 is declared as int[8190] to match the decompile's
// frame exactly (do NOT shrink). __chkstk/alloca_probe is the compiler's stack-probe
// injection and needs no port. Retail passes its fourth argument through to 0x14001dc00,
// which never reads it.
int RegionGraph::PathLength(int start, uint32_t goal) {
    int local_8008[8190];
    if ((0 < dirtyRebuild._size) || (0 < dirtyAdjacency._size)) {
        Flush();   // 0x1400a8a90
    }
    // NOT RETAIL (HITCH_PAIRSPREAD, region_search.h): the sum depends on (start, goal) and
    // the graph only, and the graph has not changed since the memo was filled (every change
    // goes through the Flush above, which drops it). At COMPPROF=4 a hit runs the search
    // anyway and the two sums are compared below.
    int memoSum = 0;
    const bool memoHit = RegionSearch::MemoFind(this, start, static_cast<int>(goal), &memoSum);
    if (memoHit && !g_verifyOn) {
        return memoSum;
    }
    int iVar2 = Run(start, static_cast<int>(goal), local_8008);   // 0x14001dc00
    int iVar3;
    if (iVar2 < 2) {
        iVar3 = 0;
    } else {
        iVar3 = 0;
        int64_t lVar5 = 0;
        if (0 < iVar2 + -1) {
            const RegionNode* lVar1 = regions.data;
            do {
                int iVar4 = lVar1[local_8008[lVar5]].y - lVar1[local_8008[lVar5 + 1]].y;
                int iVar6 = lVar1[local_8008[lVar5]].x - lVar1[local_8008[lVar5 + 1]].x;
                float fVar7 = WorkImpl_Sqrtf_132010(static_cast<float>(iVar4 * iVar4 + iVar6 * iVar6));   // 0x140132010
                lVar5 = lVar5 + 1;
                iVar3 = iVar3 + static_cast<int>(fVar7);
            } while (lVar5 < iVar2 + -1);
        }
    }
    if (g_verifyOn) {   // NOT RETAIL: PATHDIST's COMPPROF=4 shadow, retail's AVL search beside the bucket one
        VerifyPathLength(start, static_cast<int>(goal), iVar2, local_8008, iVar3);
        if (memoHit) {
            StressHarness_Verify("PathLengthMemo", memoSum == iVar3,
                                 (static_cast<long long>(start) << 32) | goal);
        }
    }
    RegionSearch::MemoStore(this, start, static_cast<int>(goal), iVar3);
    return iVar3;
}

// ============================================================
// 0x1400a5b30 — store a region-pair distance
// ============================================================
// Normalise (a,b) so a>=b, look the pair up in the index: if found, overwrite its ring
// slot's distance and return. Else advance the newest index (wrapping at the ring size);
// if it caught up with the oldest, evict the oldest entry from the index (0x14001ca60)
// and advance the oldest. Write the new entry (stamped with newestTime) and insert it
// into the index (0x14001c940).
//
// Faithfulness traps: the (a,b) normalisation is applied THREE times exactly as decompiled
// (lookup key, slot store, index key).
// B6 row 49: the slot's +0x0C dword is NOT written here. Retail builds each record in a
// stack temporary ({a,b} qword at RSP+0x20, the distance dword at RSP+0x28) and copies it
// as three qwords (0x1400a5c55 / 0x1400a5c5d / 0x1400a5c61 on eviction, 0x1400a5cbb /
// 0x1400a5cc3 / 0x1400a5cc7 on store): the second qword carries RSP+0x2C, a stack dword
// nothing initialised, into +0x0C. There is no value to copy, so the field is left as it
// was; nothing reads it.
void RegionPairCache::Store(int param_2, int param_3, uint32_t param_4) {
    int iVar7 = param_2;
    int iVar8 = param_3;
    if (param_2 < param_3) {
        iVar7 = param_3;
        iVar8 = param_2;
    }
    AvlNode30* piVar6 = index.root;
    AvlNode30* piVar4 = piVar6;
    while (piVar4 != nullptr) {
        int iVar2 = piVar4->key0;
        if (iVar7 < iVar2) {
LAB_1400a5b88:
            piVar4 = piVar4->left;
        } else {
            if (iVar7 <= iVar2) {
                if (iVar8 < piVar4->key1) goto LAB_1400a5b88;
                if ((iVar7 <= iVar2) && ((iVar7 < iVar2 || (iVar8 <= piVar4->key1)))) {
                    if (piVar4 != nullptr) {
                        ring.data[piVar4->key2].distance = static_cast<int32_t>(param_4);
                        return;
                    }
                    break;
                }
            }
            piVar4 = piVar4->right;
        }
    }
    iVar7 = newest;
    iVar8 = iVar7 + 1;
    if (ring.size <= iVar7 + 1) {
        iVar8 = 0;
    }
    newest = iVar8;
    if (newest == oldest) {
        RegionPairEntry* evict = &ring.data[oldest];
        if (piVar6 != nullptr) {
            int iVar8b = evict->regionA;
            do {
                int iVar2 = piVar6->key0;
                if (iVar8b < iVar2) {
LAB_1400a5c1b:
                    piVar6 = piVar6->left;
                } else {
                    if (iVar8b <= iVar2) {
                        int iVar3 = evict->regionB;
                        if (iVar3 < piVar6->key1) goto LAB_1400a5c1b;
                        if ((iVar8b <= iVar2) && ((iVar8b < iVar2 || (iVar3 <= piVar6->key1)))) {
                            WorkCellBST_Remove_ca60(&index, piVar6);   // 0x14001ca60
                            break;
                        }
                    }
                    piVar6 = piVar6->right;
                }
            } while (piVar6 != nullptr);
        }
        RegionPairEntry* e = &ring.data[oldest];
        e->regionA = -1;                   // one qword store of -1 over regionA / regionB
        e->regionB = -1;
        e->distance = -1;
        e->time = 0;
        oldest = oldest + 1;
        int iVar8c = oldest;
        if (ring.size <= oldest) {
            iVar8c = 0;
        }
        oldest = iVar8c;
    }
    int32_t keyHi = param_2;
    int32_t keyLo = param_3;
    if (param_2 < param_3) {
        keyHi = param_3;
        keyLo = param_2;
    }
    int64_t uVar5 = newestTime;
    RegionPairEntry* puVar1 = &ring.data[iVar7];
    puVar1->regionA = keyHi;
    puVar1->regionB = keyLo;
    puVar1->distance = static_cast<int32_t>(param_4);
    puVar1->time = uVar5;
    int local_res8 = param_2;
    int iStackX_c = param_3;
    if (param_2 < param_3) {
        local_res8 = param_3;
        iStackX_c = param_2;
    }
    // The index key {larger, smaller, ring slot}; the out slot is a stack qword.
    int32_t key[3] = { local_res8, iStackX_c, iVar7 };
    AvlNode30* out = nullptr;
    AvlInsert30(&index, &out, key);   // 0x14001c940
}

// ============================================================
// 0x1400a5ac0 — look a region pair up -> distance
// ============================================================
// Orders (a,b) so hi >= lo, descends the index; returns the ring slot's distance, or
// 0xffffffff if the pair is absent.
uint32_t RegionPairCache::Find(int a, int b) const {
    int hi = a, lo = b;
    if (a < b) { hi = b; lo = a; }
    const AvlNode30* node = index.root;
    while (true) {
        if (node == nullptr) return 0xffffffffu;
        int key0 = node->key0;
        if (hi < key0) {                       // go left
            node = node->left;
            continue;
        }
        if (hi > key0) {                       // go right
            node = node->right;
            continue;
        }
        // hi == key0: compare lo against key1
        int key1 = node->key1;
        if (lo < key1) {                       // go left
            node = node->left;
            continue;
        }
        if (lo > key1) {                       // go right
            node = node->right;
            continue;
        }
        // exact match (hi==key0 && lo==key1)
        if (node == nullptr) return 0xffffffffu;
        int idx = node->key2;
        return static_cast<uint32_t>(ring.data[idx].distance);
    }
}
// The B4 FORWARD C spellings of these members are defined in action_pf_astar.cpp.
