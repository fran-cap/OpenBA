// src/game/Action/action_pf_geom.cpp
// Pathfinding / region-graph / geometry / heuristic leaf sub-helpers (PF porter wave).
//
// Faithful 1:1 ports of nine exe sub-helpers that are pure leaves (no NEEDS-PORT
// children — only Math::Vector3 engine exports, System::MemoryAllocator/Random
#include "Action/exe_bodies.h"
// exports, and bit/array/index math). Their callers call them by name through
// Action/exe_bodies.h (B3R); standalone/reg_pf_geom.cpp still registers them for readers
// other blocks have.
//
// Transcribed literally from the Ghidra decompile/disassembly. Do NOT "improve",
// simplify, or reorder. None of these are exercised on the autostart path, so a
// wrong port is undetectable silent corruption — strictly worse than unregistered.
//
//   0x1400a5ec0  neighbour tile index by 8-direction code   (GridPathSearch::Neighbour)
//   0x1400a6030  movement-cost octile/diagonal heuristic    (GridPathSearch::Heuristic)
//   (0x1400a5ac0, the region-pair lookup, is RegionPairCache::Find in action_pf_region.cpp)
//   (0x1400aa810, the ray/triangle intersect, is in Game/worldgeometry_ray.cpp)
//   0x1400ae180  edge-mask -> tile-shape LUT index (DAT_1401c22d0)
//   0x14009d0a0  expanding-ring spiral search for nearest valid region cell     [loop]
//   0x140018880  quadtree node subdivide / push 4 children
//   0x140018dd0  quadtree/box-walker init (zeroes ~0xFF*0x24 stack records)
//   0x140053ca0  pasture/migration target picker (collect+random-pick cells)    [loop]

#include "Action/PathSearch.h"     // GridPathSearch
#include "Game/Map.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "Game/exe_constants.h"   // kRdata_* (B3R Leftovers: the .rdata constants)
#include "system/MemoryAllocator.h"   // System::MemoryAllocator::MemAlloc / MemFree
#include "math/Math.h"
#include "system/Value.h"      // ENDGAME B.3.4b System callers
#include "Component/CharacterGrid.h"
#include "Component/PopulationComponent.h"
#include "Component/PopulationDescription.h"
#include <cstdint>

namespace {

// ---- Math::Vector3 (B.3.1: direct members of the real class; the old dispatch-slot
// pointers were slot 0x140152a30 ctor(x,y,z), 0x140151320 splat ctor, 0x1401526a8 Cross,
// 0x140151298 Dot, 0x1401512c0 Normalize) ----------------------------------------

// System::MemoryAllocator static MemAlloc/MemFree (import slots 0x140152b60 /
// 0x140152b68) are called directly (B3R).

// Float constants (read from exe .rdata in the original; inlined here as bit-exact
// values — verified via read_memory):
//   DAT_140172114 = 0x3f000000 =  0.5f
//   DAT_140172280 = 0xbf800000 = -1.0f
static inline float C_half()   { return 0.5f; }
static inline float C_negone() { return -1.0f; }

} // namespace

// ============================================================
// 0x1400a5ec0 — neighbour cell by 8-direction code
// ============================================================
// Decompose the cell into (row, col) on the square grid of `width`; apply the direction
// delta (case 0..7); bounds-check; skip a cell whose blocked bit (0x80) is set while
// respectBlocked; finally only return the cell if its terrain type's bit is set in
// passableTypes.
int GridPathSearch::Neighbour(int index, uint32_t dir) const {
    int w = width;
    int row = index / w;
    int col = index % w;
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

// ============================================================
// 0x1400a6030 — movement-cost octile / diagonal heuristic
// ============================================================
// dx,dy = |col delta|,|row delta| on the grid of `width`. allowDiagonal: octile
// (10*(dx+dy) - 6*min). manhattan: (dx+dy)*10. Else: Chebyshev*10 (max*10).
int GridPathSearch::Heuristic(int from, int to) const {
    int w = width;
    uint32_t udx = (uint32_t)(from % w - to % w);
    uint32_t udy = (uint32_t)(from / w - to / w);
    uint32_t s;
    s = (uint32_t)((int)udx >> 0x1f);
    int dx = (int)((udx ^ s) - s);          // abs(dx)
    s = (uint32_t)((int)udy >> 0x1f);
    int dy = (int)((udy ^ s) - s);          // abs(dy)
    if (static_cast<char>(allowDiagonal) != 0) {
        int mn = dy;
        if (dx < dy) mn = dx;
        return (dy + dx) * 10 + mn * -6;
    }
    if (static_cast<char>(manhattan) != 0) {
        return (dy + dx) * 10;
    }
    if (dy < dx) dy = dx;
    return dy * 10;
}

// Tile-shape variant LUT, retail .rdata 0x1401c22d0: 256 x int8 (read byte for byte,
// 2026-10-11; the next .rdata item starts at 0x1401c23d0). Consumer 0x1400ae180 indexes it
// with the 8-bit edge/corner mask built below (0x1400ae257: LEA RAX,[0x1401c22d0];
// MOVSX EAX, byte [RCX + RAX]) -- a signed read. -1 (0xFF) marks a mask the diagonal
// clearing makes unreachable; every reachable mask maps to a variant 0..46, the row of
// the mask-atlas rect LUT kRoadMaskAtlasRect (action_pf_road.cpp).
constexpr int8_t kTileShapeVariantLut[256] = {
    0,1,2,5,3,6,7,11,4,8,9,12,10,13,14,19,
    -1,-1,-1,-1,-1,15,-1,20,-1,-1,-1,-1,-1,21,-1,28,
    -1,-1,-1,-1,-1,-1,16,22,-1,-1,-1,-1,-1,-1,23,29,
    -1,-1,-1,-1,-1,-1,-1,30,-1,-1,-1,-1,-1,-1,-1,36,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,17,-1,24,-1,25,-1,31,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,32,-1,37,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,38,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,42,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,18,26,-1,-1,27,33,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,39,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,34,40,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,43,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,35,-1,-1,-1,41,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,44,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,45,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,46,
};

// ============================================================
// 0x1400ae180 — edge-mask -> tile-shape LUT index (DAT_1401c22d0)
// ============================================================
// Eight corner/edge booleans (p2..p9). First, four diagonal flags (p6..p9) are
// cleared unless both adjacent orthogonal edges are present. Then an 8-bit mask
// is assembled (bit weights: p2=1, p3=2, p4=4, p5=8, p6=0x10, p7=0x20, p8=0x40,
// p9=0x80) and used to index the signed-byte table at DAT_1401c22d0.
int Pf_TileShapeLut_ae180(void* /*self*/, char p2, char p3, char p4, char p5,
                          char p6, char p7, char p8, char p9) {
    if ((p6 != 0) && ((p2 == 0) || (p4 == 0))) p6 = 0;
    if ((p7 != 0) && ((p3 == 0) || (p4 == 0))) p7 = 0;
    if ((p8 != 0) && ((p2 == 0) || (p5 == 0))) p8 = 0;
    if ((p9 != 0) && ((p3 == 0) || (p5 == 0))) p9 = 0;

    uint64_t idx =
        (uint64_t)((uint32_t)(-(int)(p9 != 0)) & 0x80) |
        (uint64_t)(p2 != 0 ? 1u : 0u) |
        (uint64_t)((uint32_t)(-(int)(p3 != 0)) & 2) |
        (uint64_t)((uint32_t)(-(int)(p4 != 0)) & 4) |
        (uint64_t)((uint32_t)(-(int)(p8 != 0)) & 0x40) |
        (uint64_t)((uint32_t)(-(int)(p5 != 0)) & 8) |
        (uint64_t)((uint32_t)(-(int)(p6 != 0)) & 0x10) |
        (uint64_t)((uint32_t)(-(int)(p7 != 0)) & 0x20);

    return (int)kTileShapeVariantLut[idx];   // MOVSX byte [0x1401c22d0 + idx]
}

// ============================================================
// 0x14009d0a0 — expanding-ring spiral search for nearest valid region cell
// ============================================================
// param_1[0] = grid width (square). param_1+0x20 (int* arithmetic, = BYTE 0x80) =
// cell-record array base — the SAME array Pf_IsDstReachable_f810 reads at (int*)param_1+0x20.
// (stride 0x30; +0x18 = regionId int). Starting from cell param_2, if that cell's
// regionId < 0, spiral outward in rings of growing radius (1..3) testing the four
// "side" walks of each ring; the first neighbour with regionId >= 0 returns that
// neighbour's regionId. Returns the start cell's regionId if it was already >= 0,
// else -1 after exhausting radius 3.
// Literal transcription of the Ghidra decompile (index aliasing preserved).
int Map::NearestRegionId(int param_2) const {
    int iVar5 = size;
    int64_t lVar16 = (int64_t)iVar5;
    // Ghidra's `param_1 + 0x20` on int* is byte 0x80: the cell array (the same array
    // Pf_IsDstReachable_f810 reads).
    const MapCell* lVar6 = cells.GetData();
    uint32_t uVar8 = (uint32_t)(param_2 / iVar5);
    int iVar11 = param_2 % iVar5;
    uint64_t uVar18 = (uint64_t)(int)uVar8;
    int iVar9 = lVar6[param_2].regionId;
    if (iVar9 < 0) {
        int64_t lVar21 = (int64_t)iVar11;
        iVar9 = 1;
        int64_t lVar14 = 1;
        uint64_t uVar19 = uVar18;
        uint64_t uVar13 = uVar18;
        uint64_t uVar15 = uVar18;
        uint32_t uVar23 = uVar8;
        uint32_t uVar22 = uVar8;
        do {
            uVar22 = uVar22 + 1;
            uVar23 = uVar23 - 1;
            uVar15 = uVar15 - 1;
            uVar13 = uVar13 + 1;
            if ((int64_t)(uVar15 - uVar18) <= lVar14) {
                int64_t lVar4 = (lVar21 - uVar18) + uVar15;
                int iVar1 = (iVar11 - uVar8) + uVar22;
                int iVar2 = (iVar11 - uVar8) + uVar23;
                int64_t lVar10 = lVar4;
                int iVar20 = iVar2;
                do {
                    int64_t lVar17 = (uVar18 - lVar21) + lVar10;
                    int iVar3 = ((int)uVar19 - iVar11) + iVar20;
                    if ((-1 < lVar17) && (iVar3 < iVar5)) {
                        if ((-1 < lVar4) &&
                            ((iVar2 < iVar5 &&
                              (-1 < lVar6[(lVar16 * lVar17 + lVar4)].regionId)))) {
                            return lVar6[(lVar16 * iVar3 + (int64_t)iVar2)].regionId;
                        }
                        int64_t lVar12 = (lVar21 - uVar18) + uVar13;
                        if (((-1 < lVar12) && (iVar1 < iVar5)) &&
                            (-1 < lVar6[(lVar16 * lVar17 + lVar12)].regionId)) {
                            return lVar6[(lVar16 * iVar3 + (int64_t)iVar1)].regionId;
                        }
                    }
                    uint32_t uVar7 = 0;
                    if (((-1 < lVar10) && (iVar20 < iVar5)) &&
                        (((-1 < (int64_t)uVar15 &&
                           (((int)uVar23 < iVar5 &&
                             (uVar7 = uVar23,
                              -1 < lVar6[(lVar16 * (int64_t)uVar15 + lVar10)].regionId)))) ||
                          ((-1 < (int64_t)uVar13 &&
                            (((int)uVar22 < iVar5 &&
                              (uVar7 = uVar22,
                               -1 < lVar6[(lVar16 * (int64_t)uVar13 + lVar10)].regionId)))))))) {
                        return lVar6[((int)uVar7 * lVar16 + (int64_t)iVar20)].regionId;
                    }
                    lVar10 = lVar10 + 1;
                    iVar20 = iVar20 + 1;
                } while (lVar10 - lVar21 <= lVar14);
                uVar19 = (uint64_t)uVar8;
            }
            iVar9 = iVar9 + 1;
            lVar14 = lVar14 + 1;
        } while (iVar9 < 4);
        iVar9 = -1;
    }
    return iVar9;
}

// ============================================================
// 0x140018880 — CharacterGridWalker::Subdivide: pop the top node; if the next depth is
// below the grid's `levels`, push its four quadrant children (cell = parent * 4 +
// 1..4), each with half the parent's x/y extent. DAT_140172114 = 0.5, DAT_140172280 =
// -1.0, g_fUVScale = 1.0. Faithful transcription of the decompile.
// ============================================================
void CharacterGridWalker::Subdivide() {
    const float c_half   = C_half();    // DAT_140172114
    const float c_negone = C_negone();  // DAT_140172280

    int iVar5 = top;
    top = iVar5 - 1;
    const CharacterGridWalkerNode& parent = nodes[iVar5];
    int iVar9 = parent.depth + 1;
    if (iVar9 < grid->levels) {
        int iVar6   = parent.cell;
        float fVar1 = parent.center[2];
        float fVar2 = parent.extent[2];
        float fVar3 = parent.center[0];
        float fVar4 = parent.center[1];
        float fVar13 = parent.extent[0];
        float fVar12 = parent.extent[1] * c_half;
        top = iVar5;
        fVar13 = fVar13 * c_half;
        float fVar10 = fVar12 * c_negone;
        float fVar11 = fVar13 * c_negone;
        CharacterGridWalkerNode* n = &nodes[top];
        n->center[0] = fVar3 + fVar11;
        n->center[1] = fVar4 + fVar10;
        n->center[2] = fVar1 + fVar2;
        n = &nodes[top];
        n->extent[0] = fVar13;
        n->extent[1] = fVar12;
        n->extent[2] = fVar2;
        nodes[top].cell = iVar6 * 4 + 1;
        nodes[top].depth = iVar9;
        nodes[top].flag = 0;

        // Vector3 stack slots local_a8 (A) and local_9c (B) — each a contiguous
        // 3-float record the ctor writes through.
        Math::Vector3 A(fVar13, fVar10, fVar2);
        Math::Vector3 B(A.x + fVar3, A.y + fVar4, A.z + fVar1);
        top = top + 1;
        n = &nodes[top];
        n->center[0] = B.x;
        n->center[1] = B.y;
        n->center[2] = B.z;
        n->extent[0] = fVar13;
        n->extent[1] = fVar12;
        n->extent[2] = fVar2;
        nodes[top].cell = iVar6 * 4 + 2;
        nodes[top].depth = iVar9;
        nodes[top].flag = 0;

        B = Math::Vector3(fVar11, fVar12, fVar2);
        A = Math::Vector3(B.x + fVar3, B.y + fVar4, B.z + fVar1);
        top = top + 1;
        float fVar10b = kRdata_14017214c;  // g_fUVScale loaded here in the original
        n = &nodes[top];
        n->center[0] = A.x;
        n->center[1] = A.y;
        n->center[2] = A.z;
        n->extent[0] = fVar13;
        n->extent[1] = fVar12;
        n->extent[2] = fVar2;
        nodes[top].cell = iVar6 * 4 + 3;
        nodes[top].depth = iVar9;
        nodes[top].flag = 0;

        Math::Vector3 scaleVec(fVar10b, fVar10b, fVar10b);
        B = Math::Vector3(fVar13 * scaleVec.x, fVar12 * scaleVec.y, fVar2 * scaleVec.z);
        A = Math::Vector3(B.x + fVar3, B.y + fVar4, B.z + fVar1);
        top = top + 1;
        n = &nodes[top];
        n->center[0] = A.x;
        n->center[1] = A.y;
        n->center[2] = A.z;
        n->extent[0] = fVar13;
        n->extent[1] = fVar12;
        n->extent[2] = fVar2;
        nodes[top].cell = iVar6 * 4 + 4;
        nodes[top].depth = iVar9;
        nodes[top].flag = 0;
    }
}

// ============================================================
// 0x140018dd0 — CharacterGridWalker::Init: empty stack (top = -1), padLo = (0, 0, lo),
// padHi = (0, 0, hi), every node's center/extent zeroed (256 nodes, three qword clears
// each), the grid stored at +0x2428; then push the root node (center 0, extent half the
// grid's worldSize on x/y, cell 0, depth 0).
// ============================================================
void CharacterGridWalker::Init(const CharacterGrid* root, float lo, float hi) {
    top = -1;
    padLo[2] = lo;
    padLo[0] = 0.0f;
    padLo[1] = 0.0f;
    padHi[0] = 0.0f;
    padHi[1] = 0.0f;
    padHi[2] = hi;
    for (int i = 0; i < 256; ++i) {
        nodes[i].center[0] = 0.0f;
        nodes[i].center[1] = 0.0f;
        nodes[i].center[2] = 0.0f;
        nodes[i].extent[0] = 0.0f;
        nodes[i].extent[1] = 0.0f;
        nodes[i].extent[2] = 0.0f;
    }
    grid = root;

    float local_res18 = 0.0f;
    float fVar4 = root->worldSize * C_half();   // DAT_140172114 = 0.5
    Math::Vector3 local_38((Math::Scalar(local_res18)));   // splat 0 -> (0,0,0)
    top = top + 1;
    CharacterGridWalkerNode& n = nodes[top];
    n.center[0] = local_38.x;
    n.center[1] = local_38.y;
    n.center[2] = local_38.z;
    n.extent[0] = fVar4;
    n.extent[1] = fVar4;
    n.extent[2] = 0.0f;
    n.cell = 0;
    n.depth = 0;
    n.flag = 0;
}

// ============================================================
// 0x140053ca0 — pasture/migration target picker (collect + random-pick cells)
// ============================================================
// The population's herd cell `herdCell` (herdDim per edge) covers a block of
// habitatDim / herdDim habitat cells; collect the ones whose density > 0 into a
// dynamically-grown int array (MemAlloc, growth *3 past the first), pick one uniformly
// (the world's System::Random), and map it to the map tile at the centre of that habitat
// cell (PopulationDescription::_habitatBlock tiles per edge). Returns the tile or -1.
// Frees the temp array. 1:1 transcription.
int PopulationComponent::PickTargetCell(int param_2) const {
    int iVar2 = habitatDim / herdDim;
    int iVar13 = iVar2 * (param_2 % herdDim);
    int iVar12 = iVar2 * (param_2 / herdDim);
    int* pvVar5 = nullptr;
    uint32_t uVar16 = 0;
    uint32_t uVar4 = 0;
    int64_t lVar8 = (int64_t)iVar12;
    int64_t local_88 = 0;
    int iVar2_result = -1;
    if (0 < iVar2) {
        do {
            int64_t lVar15 = 0;
            int* pvVar11 = pvVar5;
            int iVar14 = iVar13;
            do {
                int iVar1 = habitatDim;
                pvVar5 = pvVar11;
                if (0 < cells[(int64_t)iVar1 * (lVar8 + local_88) + iVar13 + lVar15].density) {
                    uint32_t uVar10 = uVar16 + 1;
                    if (uVar10 != uVar16) {
                        if ((int)uVar4 < (int)uVar10) {
                            uVar4 = uVar10;
                            if (uVar16 != 0) {
                                uVar4 = uVar10 * 3;
                            }
                            pvVar5 = static_cast<int*>(System::MemoryAllocator::MemAlloc((uint64_t)(int)uVar4 << 2, 4));
                        }
                        uint32_t uVar6 = 0;
                        if (pvVar11 == pvVar5) {
                            uVar6 = uVar16;
                        }
                        int64_t lVar9 = (int64_t)(int)uVar6;
                        if (lVar9 < (int)uVar10) {
                            int* piVar7 = pvVar5 + lVar9;
                            do {
                                if (piVar7 != nullptr) {
                                    int iVar3 = iVar12 * iVar1 + iVar14;
                                    if (lVar9 < (int)uVar16) {
                                        iVar3 = pvVar11[lVar9];   // the old array's element
                                    }
                                    *piVar7 = iVar3;
                                }
                                lVar9 = lVar9 + 1;
                                piVar7 = piVar7 + 1;
                            } while (lVar9 < (int)uVar10);
                        }
                        uVar16 = uVar10;
                        if (pvVar11 != pvVar5) {
                            System::MemoryAllocator::MemFree(pvVar11);
                        }
                    }
                }
                iVar14 = iVar14 + 1;
                lVar15 = lVar15 + 1;
                pvVar11 = pvVar5;
            } while (lVar15 < iVar2);
            iVar12 = iVar12 + 1;
            local_88 = local_88 + 1;
        } while (local_88 < iVar2);
        if (uVar16 != 0) {
            GameUpdateLoop* game = GameUpdateLoop::_singleton;
            uint32_t uVar4r = game->m_random->Rand(static_cast<uint32_t>(uVar16));
            int picked = pvVar5[(int)uVar4r];
            int cellSize = description->_habitatBlock;
            int worldStride = game->m_game->size;
            iVar2_result =
                worldStride *
                    (cellSize * (picked / habitatDim) + (cellSize >> 1)) +
                cellSize * (picked % habitatDim) + (cellSize >> 1);
        }
    }
    System::MemoryAllocator::MemFree(pvVar5);
    return iVar2_result;
}

// ===========================================================================
// B4 FORWARD: the C spellings other blocks' files still call, one line each.
// ===========================================================================

// (Pf_QuadtreeSubdivide_18880 / Pf_QuadtreeInit_18dd0, the forwarders to
// CharacterGridWalker::Subdivide / Init, had no caller left: deleted, B4 wave X.)
