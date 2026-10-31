// src/game/Action/action_pf_astar.cpp
// Wave 3 (Action) — KEYSTONE batch: A* pathfinder core + worker-thread command
// queue/dispatcher + region flood-fill. Faithful 1:1 ports of the exe pathfinding
// closure. NONE of this is exercised on the autostart path, so a wrong transcription
// is undetectable silent corruption — every offset below is cross-checked against the
// Ghidra decompile AND disassembly. Do NOT "improve" or simplify.
//
// B.4 ACT-a: the records are named (src/game/Action/PathSearch.h, Game/PathfinderThread.h,
// Game/Map.h) and the bodies are their members:
//   PathOpenSet::Delete 0x14001d550, ::Insert 0x14001d450; PathSearch::Relax 0x14001d320;
//   GridPathSearch::FindPath 0x14001d040, ::Run 0x14001ce00; RegionGraph::FindPath
//   0x14001dcd0, ::Run 0x14001dc00; PathfinderThread::ApplyCommand 0x1400a6430 and the
//   enqueuers 0x1400a6fe0 / 0x1400a68b0 / 0x1400a6a30 / 0x1400a6b20 / 0x1400a6c10 /
//   0x1400a6240; Map::FillZone 0x14009c170.
// The pathfinder runs on its OWN THREAD (PathfinderThread::Run drains the queue): the
// order of the stores to the queue, the result slots and the lock is the tree's, unchanged.
//
// A* SEARCH (PathSearch, the first 0x50 bytes of both searches):
//   +0x00 generation, +0x04 sequence (open-set tie-break), +0x08 the per-vertex node
//   array (stride 0x18: open-set entry, parent, openStamp, closedStamp, gCost), +0x18 the
//   open set (an AVL tree ordered by (fcost, seq), nodes from its own pool).
// GRID SEARCH (PathfinderThread +0x78): +0x50 step cost per terrain type, +0xd0 passable
//   mask, +0xd4 diagonal, +0xd5 Manhattan, +0xd6 respect-blocked, +0xd8 width, +0xe0 flags.
// REGION GRAPH (Map +0x70): +0x68 the region nodes (centroid, neighbour list).
// COMMAND (0x10 on the enqueuer's stack; 0x20 when queued): type, arg1..arg3, next, prev.

#include "Action/exe_bodies.h"
#include "Action/PathSearch.h"
#include "Action/PathAvl.h"
#include "Game/PathfinderThread.h"
#include "Game/Map.h"
#include "Work/exe_bodies.h"          // WorkImpl_Sqrtf_132010
#include "system/MemoryAllocator.h"   // System::MemoryAllocator::MemAlloc / MemFree
#include "system/Thread.h"     // ENDGAME B.3.4b System callers
#include "stress_harness.h"   // PFBENCH: g_pfProfOn / PfProf_* (stress harness only; one branch when off)
#include "ext/fast_paths/fast_paths.h"   // EXT SEAM (NOT RETAIL, docs/extensions/handins/FASTPATHS.md): ext.fast_paths, cases 2 and 5 below
#include "ext/path_query_first/path_query_first.h"   // EXT SEAM (NOT RETAIL): fix.path_query_first, command 6
#include <atomic>      // std::atomic_signal_fence (ApplyCommand case 5: retail's per-iteration reloads)
#include <cstdint>
#include <cstdio>    // [FLOODDIAG] diagnostic reporting
#include <cstdlib>

// Ghidra decompile uses `uint`/`ushort` for the unsigned grid-index math; alias them
// so the literal transcription below matches the decompile token-for-token.
typedef unsigned int  uint;
typedef unsigned short ushort;

// ============================================================
// 0x14001d550 — open-set delete + rebalance + node recycle. (Ghidra:
// TradeDescription_impl_d550.) Standard AVL deletion (0/1/2-child), then rebalance via
// 0x140013480, and the deleted node goes back to the set's pool.
// ============================================================
void PathOpenSet::Delete(PathOpenNode* node) {
    PathOpenNode* lVar1;
    PathOpenNode* lVar2;
    PathOpenNode* lVar3;
    short sVar4;
    PathOpenNode** plVar5;
    short sVar6;
    short sVar7;
    PathOpenNode* local_res8;
    PathOpenNode* local_res10;

    local_res8 = node->parent;
    count = count + -1;
    lVar1 = node->right;
    if (lVar1 == nullptr) {
        lVar1 = node->parent;
        lVar2 = node->left;
        if (lVar1 == nullptr) {
            root = lVar2;
        } else if (lVar1->left == node) {
            lVar1->left = lVar2;
        } else {
            lVar1->right = lVar2;
        }
        local_res10 = node->left;
    } else {
        lVar2 = node->left;
        lVar3 = lVar2;
        if (lVar2 != nullptr) {
            do {
                local_res10 = lVar3;
                lVar3 = local_res10->right;
            } while (local_res10->right != nullptr);
            local_res8 = nullptr;
            if (local_res10 != lVar2) {
                lVar1 = local_res10->left;
                local_res8 = local_res10->parent;
                local_res8->right = lVar1;
                if (lVar1 != nullptr) {
                    lVar1->parent = local_res8;
                }
                local_res10->left = node->left;
                if (local_res8->left == nullptr) {
                    sVar7 = 0;
                } else {
                    sVar7 = local_res8->left->height + 1;
                }
                if (local_res8->right == nullptr) {
                    sVar6 = 0;
                } else {
                    sVar6 = local_res8->right->height + 1;
                }
                sVar4 = sVar6;
                if (sVar6 < sVar7) {
                    sVar4 = sVar7;
                }
                local_res8->balance = sVar6 - sVar7;
                local_res8->height = sVar4;
            }
            local_res10->right = node->right;
            if (local_res10->left != nullptr) {
                local_res10->left->parent = local_res10;
            }
            if (local_res10->right != nullptr) {
                local_res10->right->parent = local_res10;
            }
            lVar1 = node->parent;
            local_res10->parent = lVar1;
            if (node->parent == nullptr) {
                root = local_res10;
                local_res10->parent = nullptr;
                local_res10 = root;
            } else if (lVar1->left == node) {
                lVar1->left = local_res10;
            } else {
                lVar1->right = local_res10;
            }
            pool.Push(node);
            plVar5 = &local_res8;
            if (local_res8 == nullptr) {
                plVar5 = &local_res10;
            }
            AvlRebalance(&root, plVar5);   // 0x140013480
            return;
        }
        lVar2 = node->parent;
        if (lVar2 == nullptr) {
            root = lVar1;
            local_res10 = node->right;
        } else if (lVar2->left == node) {
            lVar2->left = lVar1;
            local_res10 = node->right;
        } else {
            lVar2->right = lVar1;
            local_res10 = node->right;
        }
    }
    if (local_res10 != nullptr) {
        local_res10->parent = node->parent;
    }
    pool.Push(node);
    if (local_res10 == nullptr) {
        AvlRebalance(&root, &local_res8);   // 0x140013480
        return;
    }
    AvlRebalance(&root, &local_res10);   // 0x140013480
}

// ============================================================
// 0x14001d450 — open-set insert (returns the matching / new node via `out`).
// (Ghidra: BuildStructure_helper_d450.) Ordered by fcost, then seq. Walks the tree; on
// an empty subtree allocates via 0x14001d730 and rebalances via 0x140013480; on an exact
// match returns the existing node.
// ============================================================
PathOpenNode** PathOpenSet::Insert(PathOpenNode** out, const PathOpenKey* key) {
    int iVar1;
    int iVar2;
    PathOpenNode* lVar3;
    PathOpenNode* lVar4;
    PathOpenNode* puVar5;
    PathOpenNode* local_res8;

    if (root == nullptr) {
        puVar5 = NewNode(key, nullptr);   // 0x14001d730
        count = count + 1;
        root = puVar5;
        *out = puVar5;
        return out;
    }
    iVar1 = key->fcost;
    lVar4 = root;
    do {
        while (local_res8 = lVar4, lVar3 = local_res8,
               iVar2 = local_res8->fcost,
               iVar1 < iVar2 ||
               (iVar1 <= iVar2 && local_res8->seq < key->seq)) {
            lVar4 = local_res8->left;
            if (local_res8->left == nullptr) {
                puVar5 = NewNode(key, local_res8);   // 0x14001d730
                lVar3->left = puVar5;
                goto LAB_14001d519;
            }
        }
        if (iVar1 <= iVar2 &&
            (iVar1 < iVar2 || local_res8->seq <= key->seq)) {
            *out = local_res8;
            return out;
        }
        lVar4 = local_res8->right;
    } while (local_res8->right != nullptr);
    puVar5 = NewNode(key, local_res8);   // 0x14001d730
    lVar3->right = puVar5;
LAB_14001d519:
    AvlRebalance(&root, &local_res8);   // 0x140013480
    count = count + 1;
    *out = puVar5;
    return out;
}

// ============================================================
// 0x14001d320 — A* relax: set the vertex node's parent / g-cost, mark it open, then
// insert it into the open set keyed on f-cost. (Ghidra: ..._sub_d320.)
//   If the vertex was already open (openStamp == generation), find and delete its old
//   open-set entry first (the inner walk -> 0x14001d550).
// ============================================================
void PathSearch::Relax(int vertex, int parentVertex, int gCost, int fCost) {
    PathSearchNode* plVar1;
    int iVar2;
    int iVar3;
    PathOpenNode* lVar4;
    PathOpenNode* plVar5;
    PathOpenNode* local_res8;
    int local_24;
    // The AVL key is ONE contiguous 3-int record {cell, seq, fcost}: 0x14001d450 reads
    // key->fcost / key->seq and 0x14001d730 stores all three into the node.
    PathOpenKey keybuf;

    plVar1 = &nodes.data[vertex];
    if (plVar1->openStamp == generation) {
        plVar5 = open.root;
        lVar4 = plVar1->openNode;   // this vertex's open-set entry
        if (plVar5 != nullptr) {
            iVar2 = lVar4->fcost;
            do {
                iVar3 = plVar5->fcost;
                if (iVar2 < iVar3 ||
                    (iVar2 <= iVar3 && plVar5->seq < lVar4->seq)) {
                    plVar5 = plVar5->left;
                } else {
                    if (iVar2 <= iVar3 &&
                        (iVar2 < iVar3 || plVar5->seq <= lVar4->seq)) {
                        open.Delete(plVar5);   // 0x14001d550
                        break;
                    }
                    plVar5 = plVar5->right;
                }
            } while (plVar5 != nullptr);
        }
    }
    iVar2 = generation;
    plVar1->closedStamp = plVar1->closedStamp + -1;
    plVar1->parent = parentVertex;
    plVar1->openStamp = iVar2;
    plVar1->gCost = gCost;
    local_24 = sequence;
    sequence = local_24 + 1;
    keybuf.cell = vertex;
    keybuf.seq = local_24;
    keybuf.fcost = fCost;
    PathOpenNode** entry = open.Insert(&local_res8, &keybuf);   // 0x14001d450
    plVar1->openNode = *entry;
}

// ============================================================
// 0x14001d040 — A* CORE over the grid. (Ghidra: FUN_14001d040.)
//   On entry gives a stale open set back to the pool, seeds the open set with `start`,
//   then loops: pop the min-f open node (leftmost), if == goal return 1, else delete it,
//   close the vertex, expand 4 or 8 neighbours, tentative g (10 ortho / 14 diag * step
//   cost) + heuristic h, relax via 0x14001d320.
// Returns retail's RAX: bit 0 = found (the caller 0x14001ce00 tests the low byte).
// ============================================================
uint64_t GridPathSearch::FindPath(int start, int goal) {
    int iVar6;
    PathOpenNode* plVar7;
    PathOpenNode* plVar8;
    PathOpenNode* plVar9;
    int iVar10;
    int iVar11;
    int iVar12;
    uint64_t uVar13;
    uint uVar14;
    uint uVar15;
    uint uVar16;
    uint uVar17;
    int iVar18;
    int iVar19;
    int iVar20;
    int iVar21;

    PathOpenNode* puVar3 = search.open.root;
    if (puVar3 != nullptr) {
        search.open.FreeSubtree(puVar3->left);    // 0x14001d400
        search.open.FreeSubtree(puVar3->right);   // 0x14001d400
        search.open.pool.Push(puVar3);
    }
    search.open.root = nullptr;                  // two dword stores in retail (H7)
    search.open.count = 0;
    search.sequence = 0;
    // Row and column are carried, not divided out per neighbour (retail divides four times
    // per neighbour; the values are the same): the expanded cell is split once per pop and
    // the goal once per search, and Neighbour hands back the neighbour's pair, whose
    // division by `width` gives back exactly that pair (PathSearch.h). `width` is written
    // only by the command dispatcher (0x1400a6430), never during a search.
    const int goalRow = goal / width;
    const int goalCol = goal % width;
    iVar10 = Heuristic(start, goal);             // 0x1400a6030
    search.Relax(start, -1, 0, iVar10);          // 0x14001d320
    uVar13 = 0;  // d320's RAX-on-exit only ever flows to the masked (&...ff00) return below
    iVar10 = search.open.count;
    do {
        if (iVar10 < 1) {
            return uVar13 & 0xffffffffffffff00ULL;
        }
        plVar9 = search.open.root;
        plVar8 = search.open.root;
        while (plVar7 = plVar9, plVar7 != nullptr) {
            uVar13 = 0;
            plVar8 = plVar7;
            plVar9 = plVar7->left;               // descend leftmost
        }
        iVar10 = plVar8->cell;
        if (iVar10 == goal) {
            return (uVar13 & 0xffffffffffffff00ULL) | 1;
        }
        search.open.Delete(plVar8);              // 0x14001d550
        uVar14 = static_cast<uint>(search.generation);
        uVar13 = uVar14;
        PathSearchNode* cur = &search.nodes.data[iVar10];
        cur->openStamp = cur->openStamp + -1;
        cur->closedStamp = static_cast<int>(uVar14);
        const int curRow = iVar10 / width;
        const int curCol = iVar10 % width;
        iVar18 = 4;
        if (static_cast<char>(allowDiagonal) != '\0') {
            iVar18 = 8;
        }
        iVar19 = 0;
        if (iVar18 != 0) {
            do {
                int nbrRow = curRow;
                int nbrCol = curCol;
                iVar11 = Neighbour(nbrRow, nbrCol, iVar19);   // 0x1400a5ec0
                uVar13 = static_cast<uint64_t>(static_cast<uint32_t>(iVar11));
                if (-1 < iVar11) {
                    uVar15 = curCol - nbrCol;
                    uVar14 = curRow - nbrRow;
                    uVar16 = static_cast<int>(uVar15) >> 0x1f;
                    uVar17 = static_cast<int>(uVar14) >> 0x1f;
                    uVar13 = static_cast<uint64_t>(cellFlags[iVar11] & 0x7f);
                    if (((uVar15 ^ uVar16) - uVar16) + ((uVar14 ^ uVar17) - uVar17) == 1) {
                        iVar21 = stepCost[static_cast<int>(uVar13)] * 10;
                    } else {
                        iVar21 = stepCost[static_cast<int>(uVar13)] * 0xe;
                    }
                    PathSearchNode* nodes5 = search.nodes.data;
                    iVar21 = nodes5[iVar10].gCost + iVar21;
                    uVar13 = static_cast<uint64_t>(static_cast<uint>(search.generation));
                    if (static_cast<uint>(nodes5[iVar11].closedStamp) == static_cast<uint>(search.generation)) {
                        if (nodes5[iVar11].gCost <= iVar21) goto LAB_14001d298;
                        nodes5[iVar11].closedStamp = nodes5[iVar11].closedStamp + -1;
                    }
                    uVar13 = static_cast<uint64_t>(static_cast<uint>(search.generation));
                    if ((static_cast<uint>(search.nodes.data[iVar11].openStamp) != static_cast<uint>(search.generation)) ||
                        (iVar21 < search.nodes.data[iVar11].gCost)) {
                        uVar15 = nbrCol - goalCol;
                        uVar16 = nbrRow - goalRow;
                        uVar14 = static_cast<int>(uVar15) >> 0x1f;
                        iVar20 = (uVar15 ^ uVar14) - uVar14;
                        uVar14 = static_cast<int>(uVar16) >> 0x1f;
                        iVar12 = (uVar16 ^ uVar14) - uVar14;
                        if (static_cast<char>(allowDiagonal) == '\0') {
                            if (static_cast<char>(manhattan) == '\0') {
                                if (iVar12 < iVar20) {
                                    iVar12 = iVar20;
                                }
                            } else {
                                iVar12 = iVar12 + iVar20;
                            }
                            iVar12 = iVar12 * 10;
                        } else {
                            iVar6 = iVar12;
                            if (iVar20 < iVar12) {
                                iVar6 = iVar20;
                            }
                            iVar12 = (iVar12 + iVar20) * 10 + iVar6 * -6;
                        }
                        search.Relax(iVar11, iVar10, iVar21, iVar12 + iVar21);   // 0x14001d320
                    }
                }
LAB_14001d298:
                iVar19 = iVar19 + 1;
            } while (iVar19 < iVar18);
        }
        iVar10 = search.open.count;
    } while (true);
}

// ============================================================
// 0x14001ce00 — run the grid A* + reconstruct the path. (Ghidra: FUN_14001ce00.)
//   Bumps the generation, runs the core; on success and if a path buffer is given,
//   counts the parent chain length from goal, then walks the parent links writing
//   cell indices into `path` (cap 0x4000), reversed (index 0 = start). Returns the
//   path length (clamped to 0x4000). Retail's R9 (the fourth argument slot) is never
//   read: 0x1400a6430 leaves it unset (0x1400a6558).
// ============================================================
int GridPathSearch::Run(int start, int goal, int32_t* path) {
    PathSearchNode* lVar1;
    int iVar2;
    uint64_t uVar3;
    int64_t lVar4;
    int iVar5;

    search.generation = search.generation + 1;
    uVar3 = FindPath(start, goal);
    if (static_cast<char>(uVar3) == '\0') {
        return 0;
    }
    iVar5 = 0;
    if (path != nullptr) {
        if (goal != -1) {
            iVar2 = goal;
            do {
                iVar5 = iVar5 + 1;
                iVar2 = search.nodes.data[iVar2].parent;
            } while (iVar2 != -1);
        }
        lVar4 = static_cast<int64_t>(iVar5 + -1);
        if (-1 < iVar5 + -1) {
            lVar1 = search.nodes.data;
            do {
                if (lVar4 < 0x4000) {
                    path[lVar4] = goal;
                }
                lVar4 = lVar4 + -1;
                goal = lVar1[goal].parent;
            } while (-1 < lVar4);
        }
        if (0x4000 < iVar5) {
            iVar5 = 0x4000;
        }
        return iVar5;
    }
    if (goal != -1) {
        do {
            iVar5 = iVar5 + 1;
            goal = search.nodes.data[goal].parent;
        } while (goal != -1);
    }
    return iVar5;
}

// ============================================================
// 0x14001dcd0 — A* over the REGION graph (Euclidean h between centroids). (Ghidra:
// ..._dcd0.) Same open-set machinery as 0x14001d040; the graph is the region node
// array (centroid +0x00 / +0x04, neighbour list +0x10). h = sqrtf(dx^2+dy^2) via
// 0x140132010.
// PATHDIST (2026-10-12): this is RETAIL'S BODY, VERBATIM, over the AVL PathOpenSet, kept
// as RegionGraph::FindPathAvl for the COMPPROF=4 verification shadow (VerifyPathLength
// below) and nothing else. The production RegionGraph::FindPath (the name Run 0x14001dc00
// calls) is action_pf_region_search.cpp: the same search over a bucket open set that pops
// in this body's order (proof there and in docs/extensions/handins/PATHDIST.md).
// ============================================================
uint64_t RegionGraph::FindPathAvl(int start, int goal) {
    PathOpenNode* plVar3;
    RegionNode* lVar4;
    PathOpenNode* plVar6;
    PathOpenNode* plVar7;
    int iVar8;
    int iVar9;
    uint64_t uVar10;
    uint64_t uVar11;
    int iVar12;
    int iVar13;
    int64_t lVar14;
    int64_t lVar15;
    int iVar16;
    uint64_t uVar18;
    int64_t lVar19;
    float fVar20;

    PathOpenNode* puVar2 = search.open.root;
    uVar18 = static_cast<uint64_t>(static_cast<int>(goal));
    if (puVar2 != nullptr) {
        search.open.FreeSubtree(puVar2->left);    // 0x14001d400
        search.open.FreeSubtree(puVar2->right);   // 0x14001d400
        search.open.pool.Push(puVar2);
    }
    search.open.root = nullptr;                  // two dword stores in retail (H7)
    search.open.count = 0;
    RegionNode* regions14 = regions.data;
    search.sequence = 0;
    const int64_t lVar17 = static_cast<int64_t>(uVar18);   // the goal, fixed at entry (retail's lVar17)
    iVar8 = regions14[start].y - regions14[lVar17].y;
    iVar12 = regions14[start].x - regions14[lVar17].x;
    fVar20 = __builtin_sqrtf(static_cast<float>(iVar8 * iVar8 + iVar12 * iVar12));   // PERF: 0x140132010 is our sqrtf; bit-identical inline
    search.Relax(start, -1, 0, static_cast<int>(fVar20));   // 0x14001d320
    uVar10 = 0;  // d320's RAX-on-exit only ever flows to the masked (&...ff00) return below
    iVar8 = search.open.count;
    do {
        if (iVar8 < 1) {
            return uVar10 & 0xffffffffffffff00ULL;
        }
        plVar3 = search.open.root;
        plVar7 = plVar3;
        while (plVar6 = plVar3, plVar6 != nullptr) {
            uVar10 = 0;
            plVar7 = plVar6;
            plVar3 = plVar6->left;
        }
        iVar8 = plVar7->cell;
        lVar14 = static_cast<int64_t>(iVar8);
        if (iVar8 == static_cast<int>(uVar18)) {
            return (uVar10 & 0xffffffffffffff00ULL) | 1;
        }
        search.open.Delete(plVar7);   // 0x14001d550
        iVar12 = search.generation;
        PathSearchNode* cur = &search.nodes.data[lVar14];
        lVar15 = 0;
        cur->openStamp = cur->openStamp + -1;
        cur->closedStamp = iVar12;
        uVar11 = static_cast<uint64_t>(regions.data[lVar14].neighbours._size);
        uVar10 = uVar11;
        if (0 < static_cast<int64_t>(uVar11)) {
            do {
                lVar4 = regions.data;
                // Retail leaves the neighbour-array pointer in RAX here; it can reach only
                // the upper bytes of the return value, which the caller never reads.
                const int32_t* nb = lVar4[lVar14].neighbours._data;
                iVar12 = nb[lVar15];
                lVar19 = static_cast<int64_t>(iVar12);
                if (-1 < iVar12) {
                    PathSearchNode* nodes5 = search.nodes.data;
                    iVar9 = lVar4[lVar14].y - lVar4[lVar19].y;
                    iVar13 = lVar4[lVar14].x - lVar4[lVar19].x;
                    fVar20 = __builtin_sqrtf(static_cast<float>(iVar9 * iVar9 + iVar13 * iVar13));   // PERF: inline, bit-identical
                    iVar9 = static_cast<int>(fVar20) + nodes5[lVar14].gCost;
                    uVar10 = static_cast<uint64_t>(static_cast<uint>(search.generation));
                    if (static_cast<uint>(nodes5[lVar19].closedStamp) == static_cast<uint>(search.generation)) {
                        if (nodes5[lVar19].gCost <= iVar9) goto LAB_14001def2;
                        nodes5[lVar19].closedStamp = nodes5[lVar19].closedStamp + -1;
                    }
                    uVar10 = static_cast<uint64_t>(static_cast<uint>(search.generation));
                    if ((static_cast<uint>(search.nodes.data[lVar19].openStamp) != static_cast<uint>(search.generation)) ||
                        (iVar9 < search.nodes.data[lVar19].gCost)) {
                        lVar4 = regions.data;
                        iVar13 = lVar4[lVar19].y - lVar4[lVar17].y;
                        iVar16 = lVar4[lVar19].x - lVar4[lVar17].x;
                        fVar20 = WorkImpl_Sqrtf_132010(static_cast<float>(iVar13 * iVar13 + iVar16 * iVar16));   // 0x140132010
                        search.Relax(iVar12, iVar8, iVar9, static_cast<int>(fVar20) + iVar9);   // 0x14001d320
                        uVar10 = 0;  // d320 return only used as dead loop accumulator
                    }
                }
LAB_14001def2:
                lVar15 = lVar15 + 1;
            } while (lVar15 < static_cast<int64_t>(uVar11));
            uVar18 = static_cast<uint64_t>(static_cast<uint>(goal));
        }
        iVar8 = search.open.count;
    } while (true);
}

// ============================================================
// 0x14001dc00 — run the region A* + extract the region path. (Ghidra: ..._dc00.)
//   Mirror of 0x14001ce00 on the region graph; path cap 0x2000 (not 0x4000).
// ============================================================
int RegionGraph::Run(int start, int goal, int32_t* path) {
    PathSearchNode* lVar1;
    uint uVar2;
    uint64_t uVar3;
    int64_t lVar4;
    int iVar5;
    uint goalU = static_cast<uint>(goal);

    search.generation = search.generation + 1;
    uVar3 = FindPath(start, goal);
    if (static_cast<char>(uVar3) == '\0') {
        return 0;
    }
    iVar5 = 0;
    if (path != nullptr) {
        if (goalU != 0xffffffff) {
            uVar2 = goalU;
            do {
                iVar5 = iVar5 + 1;
                uVar2 = static_cast<uint>(search.nodes.data[static_cast<int>(uVar2)].parent);
            } while (uVar2 != 0xffffffff);
        }
        lVar4 = static_cast<int64_t>(iVar5 + -1);
        if (-1 < iVar5 + -1) {
            lVar1 = search.nodes.data;
            do {
                if (lVar4 < 0x2000) {
                    path[lVar4] = static_cast<int32_t>(goalU);
                }
                lVar4 = lVar4 + -1;
                goalU = static_cast<uint>(lVar1[static_cast<int>(goalU)].parent);
            } while (-1 < lVar4);
        }
        if (0x2000 < iVar5) {
            iVar5 = 0x2000;
        }
        return iVar5;
    }
    if (goalU != 0xffffffff) {
        do {
            iVar5 = iVar5 + 1;
            goalU = static_cast<uint>(search.nodes.data[static_cast<int>(goalU)].parent);
        } while (goalU != 0xffffffff);
    }
    return iVar5;
}

// ============================================================
// NOT RETAIL -- the PATHDIST verification shadow (BANISHED_STRESS_COMPPROF=4, g_verifyOn).
// PathLength 0x1400a8ba0 calls it after its sum when g_verifyOn is set: rerun the same
// (start, goal) through retail's AVL search (FindPathAvl, under a fresh generation as Run
// gives it), extract the path as Run 0x14001dc00 does, sum it as PathLength does, and
// compare count, every path entry, the sum, `search.sequence` (relaxations) and
// `search.open.count` with what the bucket search just produced. The extra generation and
// the AVL nodes it leaves in search.open are invisible to the next bucket search (stamps
// compare for equality only; the AVL tree is released by FindPathAvl's own entry).
// ============================================================
__attribute__((noinline))
void RegionGraph::VerifyPathLength(int start, int goal, int count, const int32_t* path, int length) {
    const int seqFast = search.sequence;
    const int openFast = search.open.count;
    search.generation = search.generation + 1;
    const uint64_t found = FindPathAvl(start, goal);
    int32_t ref[0x2000];
    int n = 0;
    if (static_cast<char>(found) != '\0') {
        for (int v = goal; v != -1; v = search.nodes.data[v].parent) {
            n = n + 1;
        }
        int v = goal;
        for (int i = n - 1; i >= 0; --i) {
            if (i < 0x2000) ref[i] = v;
            v = search.nodes.data[v].parent;
        }
        if (n > 0x2000) n = 0x2000;
    }
    int sum = 0;
    for (int i = 0; i + 1 < n; ++i) {
        const int dy = regions.data[ref[i]].y - regions.data[ref[i + 1]].y;
        const int dx = regions.data[ref[i]].x - regions.data[ref[i + 1]].x;
        sum = sum + static_cast<int>(WorkImpl_Sqrtf_132010(static_cast<float>(dy * dy + dx * dx)));
    }
    bool ok = n == count && sum == length && search.sequence == seqFast && search.open.count == openFast;
    for (int i = 0; ok && i < n; ++i) {
        ok = ref[i] == path[i];
    }
    StressHarness_Verify("PathDistance", ok,
                         (static_cast<long long>(start) << 32) | static_cast<uint32_t>(goal));
}

// ============================================================
// WORKER-THREAD COMMAND DISPATCHER + ENQUEUERS
// ============================================================

// 0x1400a6430 — apply a command (on the worker thread, or inline while runInline).
// (Ghidra: FUN_1400a6430.) switch(type) 0..5:
//   0: grid.passableTypes = arg1
//   1: grid.stepCost[arg1] = arg2
//   2: grid.cellFlags[arg1] = keep-hi-bit | arg2
//   3: grid.cellFlags[arg1] = (arg2 ? 0x80 : 0) | keep-lo-7-bits
//   4: allowDiagonal = arg1 != 0, manhattan = arg2 != 0, respectBlocked = arg3 != 0
//   5: run the grid A* into a 64KB int buffer, size result slot arg3's path under the
//      lock, then (unlocked) copy the path in and flag it ready.
// Retail reads RCX and RDX only (0x1400a6443 / 0x1400a6449).
void PathfinderThread::ApplyCommand(const PathCommandArgs& cmd) {
    int iVar1;
    int iVar2;
    uint8_t* pbVar3;
    int64_t lVar4;
    int64_t lVar5;
    // B6 row 48: the 0x10000-byte path buffer is a LOCAL, on the stack of whichever
    // thread applies the command (0x1400a6436 MOV EAX,0x10030; CALL __chkstk;
    // SUB RSP,RAX: the buffer at RSP+0x30, 0x4000 ints). The worker's stack is
    // created with a 0x10000 COMMIT, not a reservation (Thread::Start), so the
    // process default reserve applies and the probe grows it, as in retail.
    int32_t auStack_10008[0x4000];

    switch (cmd.type) {
    case 0:
        grid.passableTypes = static_cast<uint32_t>(cmd.arg1);
        return;
    case 1:
        grid.stepCost[cmd.arg1] = cmd.arg2;
        return;
    case 2:
        pbVar3 = &grid.cellFlags[cmd.arg1];
        if (Ext::FastPaths::Engaged()) Ext::FastPaths::NoteCellTerrain(*pbVar3, cmd.arg2);   // EXT SEAM (NOT RETAIL, docs/extensions/handins/FASTPATHS.md): ext.fast_paths' type counts
        *pbVar3 = static_cast<uint8_t>((*pbVar3 & 0x80) | static_cast<uint8_t>(cmd.arg2));
        return;
    case 3:
        pbVar3 = &grid.cellFlags[cmd.arg1];
        *pbVar3 = static_cast<uint8_t>((static_cast<uint8_t>(-(cmd.arg2 != 0)) & 0x80U) | (*pbVar3 & 0x7f));
        return;
    case 4:
        iVar2 = cmd.arg2;
        iVar1 = cmd.arg1;
        grid.respectBlocked = cmd.arg3 != 0;
        grid.manhattan = iVar2 != 0;
        grid.allowDiagonal = iVar1 != 0;
        return;
    case 5: {
        // [PFBENCH] stress harness only: bracket the search body and read the expansion
        // count from the search's own state AFTER the run (inserts minus the entries still
        // open; see docs/extensions/handins/PFBENCH.md). Nothing inside FindPath is touched;
        // with the harness off this is one load and branch of g_pfProfOn each side.
        const long long pfT0 = g_pfProfOn ? CompProf_Now() : 0;
        iVar2 = Ext::FastPaths::Engaged() ? Ext::FastPaths::Run(grid, cmd.arg1, cmd.arg2, auStack_10008)   // EXT SEAM (NOT RETAIL, docs/extensions/handins/FASTPATHS.md): ext.fast_paths
                                     : grid.Run(cmd.arg1, cmd.arg2, auStack_10008);   // 0x14001ce00
        if (pfT0 != 0) {
            // The fast search reports its own pops (Ext::FastPaths::LastRunExpanded); retail's
            // figure is its upper bound (PFBENCH.md doubt 1).
            PfProf_Search(cmd.arg3, pfT0, CompProf_Now(), iVar2,
                          Ext::FastPaths::Engaged() ? Ext::FastPaths::LastRunExpanded()
                                               : grid.search.sequence - grid.search.open.count);
        }
        // B6 row 47: retail's lock covers the resize ONLY: 0x1400a6564 Lock, 0x1400a6582
        // CALL 0x1400021d0 (Collection::Array<int32>::SetSize, the grown tail left as the
        // allocation found it), 0x1400a658b Unlock; the cell loop (0x1400a65b0..0x1400a65d3)
        // and the ready-byte store (0x1400a65e3) run unlocked.
        //
        // RETAIL'S RACE, KEPT: the main thread's AllocResult 0x1400a6240 grows this same
        // results table under the lock (0x14001d940 copies each slot, then frees the old
        // buffers). A grow landing between the worker's reload of the slot's path pointer
        // and its store writes into a freed block. Retail's loop RELOADS `results` and the
        // slot's path pointer on EVERY iteration (0x1400a65b0..0x1400a65c1), so the window
        // is three instructions; the B4-era tree held the lock over the whole commit
        // because GCC had hoisted those two loads out of the loop (an int store cannot
        // alias a pointer field), stretching the window to the whole copy (the
        // intermittent TreeSuccessor / PostUpdate AV). The signal fence below emits no
        // instruction; it only stops that hoist, so the loop reloads as retail's does.
        const uint64_t slot = static_cast<uint64_t>(static_cast<uint>(cmd.arg3));
        lock.Lock();
        results.data[slot].path.SetSize(iVar2);
        lock.Unlock();
        lVar4 = 0;
        if (0 < static_cast<int64_t>(iVar2)) {
            do {
                std::atomic_signal_fence(std::memory_order_seq_cst);   // retail's per-iteration reloads
                lVar5 = lVar4 + 1;
                results.data[slot].path._data[lVar5 - 1] = auStack_10008[lVar4];
                lVar4 = lVar5;
            } while (lVar5 < iVar2);
        }
        results.data[slot].ready = 1;
        if (g_pfProfOn) PfProf_Ready(slot);   // [PFBENCH] e2e split: publish stamp (stress harness only)
        return;
    }
    case Ext::PathQueryFirst::kCommand:
        // EXT SEAM (NOT RETAIL, docs/extensions/handins/PATHQUERYFIRST.md): fix.path_query_first's
        // head-of-queue query; retail queues no command 6.
        Ext::PathQueryFirst::Apply(*this, cmd);
        return;
    }
}

// ---- The enqueue tail every enqueuer inlines: link a fresh 0x20-byte command node
// onto the tail of the worker queue under the lock, then start the thread if it is not
// running. Mirrors the identical tail in a6fe0/a68b0/a6a30/a6b20/a6c10 EXACTLY.
namespace {
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
} // namespace

// 0x1400a6fe0 — command 2: set a cell's terrain type (the low 7 flag bits).
void PathfinderThread::SetCellTerrain(int cell, int terrainType) {
    PathCommandArgs local;
    local.type = 2;
    local.arg1 = cell;
    local.arg2 = terrainType;
    local.arg3 = 0;
    if (runInline == 0) {
        EnqueueNode(this, local);
    } else {
        ApplyCommand(local);
    }
}

// 0x1400a68b0 — command 4: the three search flags. Sent only when one of the staged
// bytes (+0x1f4..+0x1f6) changed.
void PathfinderThread::SetSearchFlags(uint8_t diagonal, uint8_t manhattanFlag, uint8_t respectBlockedFlag) {
    if (stagedDiagonal != diagonal || stagedManhattan != manhattanFlag || stagedRespectBlocked != respectBlockedFlag) {
        stagedDiagonal = diagonal;
        stagedManhattan = manhattanFlag;
        stagedRespectBlocked = respectBlockedFlag;
        PathCommandArgs local;
        local.type = 4;
        local.arg1 = static_cast<int32_t>(diagonal != 0);
        local.arg2 = static_cast<int32_t>(manhattanFlag != 0);
        local.arg3 = static_cast<int32_t>(respectBlockedFlag != 0);
        if (runInline == 0) {
            EnqueueNode(this, local);
        } else {
            ApplyCommand(local);
        }
    }
}

// 0x1400a6a30 — command 1: one terrain type's step cost. Sent only if changed.
void PathfinderThread::SetStepCost(int terrainType, int cost) {
    if (stagedStepCost[terrainType] != cost) {
        stagedStepCost[terrainType] = cost;
        PathCommandArgs local;
        local.type = 1;
        local.arg1 = terrainType;
        local.arg2 = cost;
        local.arg3 = 0;
        if (runInline == 0) {
            EnqueueNode(this, local);
        } else {
            ApplyCommand(local);
        }
    }
}

// 0x1400a6b20 — command 0: the passable-type mask. Sent only if changed. The original
// branches the "apply inline" case FIRST.
void PathfinderThread::SetPassableTypes(int mask) {
    if (stagedPassableTypes != mask) {
        stagedPassableTypes = mask;
        PathCommandArgs local;
        local.type = 0;
        local.arg1 = mask;
        local.arg2 = 0;   // one qword store over arg2 / arg3 in retail (H7)
        local.arg3 = 0;
        if (runInline != 0) {
            ApplyCommand(local);
            return;
        }
        EnqueueNode(this, local);
    }
}

// 0x1400a6c10 — command 5: request a path. (Ghidra: TradeComponent_impl_6c10.)
//   First claims a result slot via 0x1400a6240 (fills the ticket = {slot, token}), then
//   sends {start, goal, slot}. Returns the ticket.
PathTicket* PathfinderThread::RequestPath(PathTicket* ticket, int start, int goal) {
    AllocResult(ticket);   // 0x1400a6240
    int iVar1 = ticket->slot;
    PathCommandArgs local;
    local.type = 5;
    local.arg1 = start;
    local.arg2 = goal;
    local.arg3 = iVar1;
    // [PFBENCH] stress harness only: stamp the slot's enqueue time and the depth of the
    // queue it lands behind (an unlocked relaxed read; the count is only reported).
    if (g_pfProfOn) PfProf_Enqueue(iVar1, __atomic_load_n(&queueCount, __ATOMIC_RELAXED));
    if (runInline == 0) {
        EnqueueNode(this, local);
    } else {
        ApplyCommand(local);
    }
    return ticket;
}

// 0x1400a6240 — claim a result slot (grows the table under the lock). (Ghidra:
// TradeComponent_impl_6240.) Scans for a free slot (token == 0); if none, grows the
// table by one via 0x14001d940 with a zeroed template slot. Gives the slot a fresh
// token (nextToken, never 0). Returns ticket = {slot, token}.
PathTicket* PathfinderThread::AllocResult(PathTicket* ticket) {
    int iVar2;
    uint32_t uVar3;
    int iVar5;

    iVar5 = 0;
    iVar2 = results.size;
    if (0 < iVar2) {
        const PathResult* scan = results.data;
        do {
            if (scan->token == 0) {
                if (-1 < iVar5) goto LAB_1400a62f5;
                break;
            }
            iVar5 = iVar5 + 1;
            scan = scan + 1;
        } while (iVar5 < iVar2);
    }
    lock.Lock();
    {
        // The zeroed 0x18-byte template slot on the stack ({ready, token, path} = 0).
        // Its path array is released at the end of this block, before the unlock: that
        // is retail's inlined ~Array (MemFree(path.data); data = 0; size/capacity = 0).
        PathResult tmpl{};
        PathResultsResize(&results, results.size + 1, &tmpl);   // 0x14001d940
    }
    lock.Unlock();
    iVar5 = iVar2;
LAB_1400a62f5:
    results.data[iVar5].token = nextToken;
    nextToken = nextToken + 1;
    uVar3 = static_cast<uint32_t>(nextToken);
    if (nextToken == 0) {
        uVar3 = 1;
    }
    nextToken = static_cast<int32_t>(uVar3);
    results.data[iVar5].ready = 0;
    ticket->slot = iVar5;
    ticket->token = results.data[iVar5].token;
    return ticket;
}

// ============================================================
// 0x14009c170 — flood-fill a zone label over the Map's walkable cells. (Ghidra:
// FUN_14009c170.) BFS over 4 directions: spreads `zone` across the connected run of
// cells whose zone is 0 and whose terrain type is in the walkable set 0xe1f,
// run-length sweeping each row span then queueing the rows above and below. The work
// queue is `cellQueue` (+0xA8), its nodes from its pool (0x14001a9b0).
// THREE parameters (B3R Leftovers, read in retail): the entry spills R9D to its home slot
// (0x14009c170, [RSP+0x78] after the prologue), but every read of that slot follows a
// write (0x14009c365 / 0x14009c3e8, the neighbour cell handed to 0x14001a9b0 by address),
// and R9D itself is overwritten at 0x14009c26f before any read. So Ghidra's param_4 is
// the local local_res20 below, not an argument; its initial value is never read.
// VERIFIED: index math (iVar13/iVar12 row bounds, the 0xe1fU walkable-class mask) and
// the zone store offset cross-checked against disassembly.
// ============================================================
void Map::FillZone(uint32_t cell, uint16_t zone) {
    MapCellQueue::Node* puVar2;
    uint uVar3;
    int iVar4;
    MapCellQueue::Node* puVar5;
    int iVar7;
    uint uVar8;
    uint64_t uVar9;
    uint uVar10;
    const MapCell* pbVar11;
    int iVar12;
    int iVar13;
    int64_t lVar14;
    PathCellIndex local_res10;
    PathCellIndex local_res20;   // written before each read (0x14009c365 / 0x14009c3e8)

    local_res10.cell = static_cast<int32_t>(cell);
    puVar5 = cellQueue.NewNode(&local_res10);   // 0x14001a9b0
    if (cellQueue.tail == nullptr) {
        cellQueue.head = puVar5;
    } else {
        cellQueue.tail->next = puVar5;
        puVar5->prev = cellQueue.tail;
    }
    cellQueue.count = cellQueue.count + 1;
    cellQueue.tail = puVar5;
    iVar12 = cellQueue.count;
    do {
        if (iVar12 < 1) {
            return;
        }
        puVar2 = cellQueue.head;
        uVar8 = static_cast<uint>(puVar2->value.cell);
        uVar9 = static_cast<uint64_t>(static_cast<int>(uVar8));
        if (puVar2 == cellQueue.tail) {
            cellQueue.tail = puVar2->prev;
        }
        cellQueue.head = puVar2->next;
        if (puVar2->next != nullptr) {
            puVar2->next->prev = puVar2->prev;
        }
        if (puVar2->prev != nullptr) {
            puVar2->prev->next = puVar2->next;
        }
        puVar2->next = nullptr;   // four dword stores in retail (H7)
        puVar2->prev = nullptr;
        cellQueue.count = cellQueue.count + -1;
        cellQueue.pool.Push(puVar2);
        iVar12 = size;
        const MapCell* lVar14c = &cells.GetData()[uVar9];
        iVar4 = static_cast<int>(uVar8) / iVar12;
        iVar13 = iVar12 * iVar4;
        iVar12 = (iVar4 + 1) * iVar12 + -1;
        if ((lVar14c->zone == 0) &&
            ((1 << (lVar14c->terrainType & 0x1f) & 0xe1fU) != 0)) {
            iVar7 = uVar8 - 1;
            uVar10 = uVar8;
            if (iVar13 <= iVar7) {
                pbVar11 = lVar14c - 1;
                do {
                    if ((1 << (pbVar11->terrainType & 0x1f) & 0xe1fU) == 0) break;
                    iVar7 = iVar7 + -1;
                    uVar10 = uVar10 - 1;
                    pbVar11 = pbVar11 - 1;
                } while (iVar13 <= iVar7);
            }
            iVar7 = uVar8 + 1;
            if (iVar7 <= iVar12) {
                pbVar11 = lVar14c + 1;
                do {
                    uVar8 = static_cast<uint>(uVar9);
                    if ((1 << (pbVar11->terrainType & 0x1f) & 0xe1fU) == 0) break;
                    iVar7 = iVar7 + 1;
                    uVar8 = uVar8 + 1;
                    uVar9 = static_cast<uint64_t>(uVar8);
                    pbVar11 = pbVar11 + 1;
                } while (iVar7 <= iVar12);
            }
            lVar14 = static_cast<int64_t>(static_cast<int>(uVar10));
            if (lVar14 <= static_cast<int>(uVar8)) {
                int64_t i = lVar14;
                lVar14 = (static_cast<int>(uVar8) - lVar14) + 1;
                do {
                    cells.GetData()[i].zone = zone;
                    i = i + 1;
                    lVar14 = lVar14 + -1;
                } while (lVar14 != 0);
            }
            if (iVar13 < static_cast<int>(uVar10 - 1)) {
                uVar10 = uVar10 - 1;
            }
            if (static_cast<int>(uVar8 + 1) < iVar12) {
                uVar8 = uVar8 + 1;
            }
            uVar3 = uVar10;
            if (0 < iVar4) {
                for (; static_cast<int>(uVar3) <= static_cast<int>(uVar8); uVar3 = uVar3 + 1) {
                    local_res20.cell = uVar3 - size;
                    const MapCell* c = &cells.GetData()[local_res20.cell];
                    if ((c->zone == 0) &&
                        ((1 << (c->terrainType & 0x1f) & 0xe1fU) != 0)) {
                        puVar5 = cellQueue.NewNode(&local_res20);   // 0x14001a9b0
                        if (cellQueue.tail == nullptr) {
                            cellQueue.head = puVar5;
                        } else {
                            cellQueue.tail->next = puVar5;
                            puVar5->prev = cellQueue.tail;
                        }
                        cellQueue.count = cellQueue.count + 1;
                        cellQueue.tail = puVar5;
                    }
                }
            }
            if (iVar4 < size + -1) {
                for (; static_cast<int>(uVar10) <= static_cast<int>(uVar8); uVar10 = uVar10 + 1) {
                    local_res20.cell = size + uVar10;
                    const MapCell* c = &cells.GetData()[local_res20.cell];
                    if ((c->zone == 0) &&
                        ((1 << (c->terrainType & 0x1f) & 0xe1fU) != 0)) {
                        puVar5 = cellQueue.NewNode(&local_res20);   // 0x14001a9b0
                        if (cellQueue.tail == nullptr) {
                            cellQueue.head = puVar5;
                        } else {
                            cellQueue.tail->next = puVar5;
                            puVar5->prev = cellQueue.tail;
                        }
                        cellQueue.count = cellQueue.count + 1;
                        cellQueue.tail = puVar5;
                    }
                }
            }
        }
        iVar12 = cellQueue.count;
    } while (true);
}

