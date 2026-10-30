// src/game/Action/action_pf_road.cpp
// Wave 3 (Action) — pathfinding / road-mesh-rebuild / geometry-wrapper / entity-resolve
// closure batch (the "PF-ROAD" family).
//
// Faithful 1:1 ports of 11 exe helpers (decompiled + disassembly-verified in Ghidra). The big
// one (0x1400af370 road-mesh rebuild) had two index-math details pinned against the disassembly:
// the 3x3 mask -> ae180 arg order and the per-vertex AO-uv overwrite offsets (see its block).
//   0x1400acaa0  Pf_RoadVtxFill_caa0       — fill one road-mesh vertex (uv/height/blend)
//   0x1400acc10  Pf_RoadQuad_cc10          — build the 4 verts of one road-tile quad
#include "Action/exe_bodies.h"
//   0x1400af370  Pf_RoadMeshRebuild_f370   — rebuild the whole road mesh (vert/index buffers)
//   (0x1400aaa20 WorldGeometry::CellQuadRay and 0x1400b0f90 HeightUnderPoint are in
//   Game/worldgeometry_ray.cpp)
//   0x1400ab150  Pf_RegionDirty4_b150      — enqueue up-to-4 coarse cells into the dirty AVL
//   0x1400a8c90  Pf_FineCellDirty_a8c90    — mark a fine cell + its 3x3 neighbourhood dirty
//   0x140122e20  Pf_EntityResolvePos_2e20  — recursive child-entity transform/position resolve
//   0x14004edb0  Pf_PickReachableCell_edb0 — pick a random nearby reachable cell
//   0x140087fb0  Pf_EntityAnchorCell_7fb0  — compute an entity's spawn/anchor cell + worldpos
//
// All sub-callees that are themselves in the broader closure are called by name (B3R):
//   1fa80 acc10 ae180 1f8d0 aa810 ae270 a8420 123070 1a4f0  (+ engine members).
// The registration point RegisterPfRoadBatch() (standalone/reg_pf_road.cpp) stays for readers
// other blocks have.
//
// Faithfulness is everything: none of this is runtime-exercised on the autostart path, so a
// wrong port is undetectable silent corruption. Each function is a literal transcription of the
// decompile, cross-checked against disassemble_function for index math, member-ABI, the road-mesh
// dynamic buffer offsets/strides, and the implicit-arg traps. Do NOT "improve" or simplify.
//

#include "object/TypeInfo.h"
#include "Action/PathSearch.h"   // RegionGraph
#include "Game/Map.h"
#include "Game/WorldGeometry.h"
#include "Game/WorldGeometryElements.h"   // GeometryBatchVertex, GeometryBatch
#include "graphics/Material.h"            // Graphics::MaterialInstance (an Object::External)
#include "Action/PathAvl.h"           // AvlInsert28 (0x14001a4f0), AvlTreeHead, AvlNode28
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "Work/exe_bodies.h"          // WorkBase_FindComponent_2ed0, WorkBase_ValidateCell_a00f0, WorkBase_CalcDistance_8830
#include "Component/exe_bodies.h"     // FloorComponent_FindResData
#include "Game/exe_bodies.h"          // Reimpl_TradeWork_helper_e270, Reimpl_MapGetTerrainPos
#include "Game/exe_constants.h"       // kRdata_*
#include "system/MemoryAllocator.h"   // System::MemoryAllocator::MemAlloc / MemFree
#include <cstring>
#include "math/Math.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include "Object/factory_registration.h"   // ::Object::GetTypeIdByName (M-D scoped finder fallback)
#include "system/Value.h"      // ENDGAME B.3.4b System callers
#include "Component/ModelComponent.h"   // ENDGAME B.3.8 CB3: ModelComponent::TypeId()
#include "Component/InteractDescription.h"
#include "Component/InteractComponent.h"
#include "Component/MapComponent.h"
#include "Entity/ComponentEntity.h"

namespace {

// caa0's packed vertex (0x14 bytes; bytes +0x10..+0x13 are never written).
struct RoadVertexScratch {
    int16_t  x;              // +0x00 x - half
    int16_t  y;              // +0x02 y - half
    int16_t  height;         // +0x04 the clamped heightmap sample
    uint16_t blend;          // +0x06
    int16_t  aoU;            // +0x08
    int16_t  aoV;            // +0x0A
    int8_t   normal[3];      // +0x0C (normal + 1) * 127.5
    uint8_t  normalW;        // +0x0F 0
    uint8_t  _pad10[4];      // +0x10
};
static_assert(sizeof(RoadVertexScratch) == 0x14, "caa0 vertex is 0x14");
static_assert(offsetof(RoadVertexScratch, aoU) == 0x08, "RoadVertexScratch::aoU @ +0x08");
static_assert(offsetof(RoadVertexScratch, normal) == 0x0C, "RoadVertexScratch::normal @ +0x0C");
static_assert(offsetof(RoadVertexScratch, normalW) == 0x0F, "RoadVertexScratch::normalW @ +0x0F");

// The road-mesh vertex (0x14 bytes), as cc10 scatters it and f370 pushes it, is
// Game/WorldGeometryElements.h's GeometryBatchVertex: retail's serializer 0x14001f9c0
// reads +0x00..+0x0E as two short[4] (signed: blend +0x06, the mask-atlas corner
// +0x0C / +0x0E) and +0x10 as uchar[4]; cc10 (0x1400acc10) and f370 only copy the
// fields (MOVZX / MOV word, byte), so no read's value depends on the signedness.
static_assert(alignof(GeometryBatchVertex) == 2, "GeometryBatchVertex align (MemAlloc(.., 2))");
static_assert(std::is_trivially_copyable<GeometryBatchVertex>::value, "GeometryBatchVertex byteCopy");
static_assert(std::is_trivially_destructible<GeometryBatchVertex>::value, "GeometryBatchVertex destroy-free");
static_assert(std::is_trivially_default_constructible<GeometryBatchVertex>::value,
              "GeometryBatchVertex must stay uninitialised on the new-tail arm (SpecFor: "
              "init = !is_trivially_default_constructible)");

// The 0x60-byte mesh batch record (0x14001f250 / 0x14001fa80 stride 0x60): f370's working
// array and the WorldGeometry +0x200 array it flushes into, is GeometryBatch.

// ------------------------------------------------------------
// Exe sub-helpers + .rdata constants, called / read by name (B3R).
// ------------------------------------------------------------
//   ae270 Reimpl_TradeWork_helper_e270 (road normal)    ae180 Pf_TileShapeLut_ae180
//   fa80 / f250  GeometryBatchArray::SetSize (fill / 2-arg; GeometryBatch_Resize)
//   1a4f0 AvlInsert28
//   a8420 Pf_DirtyCoarseCellB_8420                       123070 Pf_RecursiveChildSum_123070
//   002ed0 WorkBase_FindComponent_2ed0                   003f60 ActCNR_FindComponent_3f60
//   004680 FloorComponent_FindResData                    122e20 Pf_EntityResolvePos_2e20 (self-rec)
//   a00f0 WorkBase_ValidateCell_a00f0                    008830 WorkBase_CalcDistance_8830
//   9f3e0 Reimpl_MapGetTerrainPos (f3e0, grid -> world)
// 021d0 (int-array SetSize), c890 (uint16 SetSize) and 1f8d0/ee40 (0x14-byte-element
// SetSize/SetSizeFill) are the exe-side Collection::Array<T> monomorphisations --
// ENDGAME B.3.2 step 2 converted their call sites below to Collection::Array<T>
// member calls, so the resolved-pointer stand-ins for them are gone.
// B.3.1: Math::Quaternion::Rotate [0x1401525f0] (3-arg — R8 = input vec; Ghidra dropped it),
// Math::Vector3::Heading [0x1401512d8], Math::Transform::operator*(Vector3) [0x140151470]
// and the Math::Vector3 splat ctor [0x140151320] are direct members of the real classes.
//
// .rdata constants (Game/exe_constants.h): 0x140172264 (caa0 ao-uv scale), 0x140172114
// (7fb0 half-cell offset).
// (This file also resolved 0x1401720b8, 0x140172214 and 0x14017226c into pointers
// nothing read; those dead reads are gone.)

// The .rdata float constants that are stored as bit patterns.
inline float RdataF(uint32_t bits) { float f; std::memcpy(&f, &bits, sizeof f); return f; }

static inline float f172264()  { return RdataF(kRdata_140172264); }   // 0x140172264
static inline float f172114()  { return kRdata_140172114; }           // 0x140172114

static inline int ClampCell(int v, int w) {
    if (v < 0) v = 0;
    if (v >= w) v = w - 1;
    return v;
}

} // namespace

// ============================================================
// FUN_1400acaa0 — fill one road-mesh vertex.
// ============================================================
// `geo`: the WorldGeometry. _gridSize = tile span (>>1 = half), _fieldD0 = heightmap width,
// _arrayD8 = the heightmap (int16). Computes the surface normal (e270) into a local Vector3,
// then writes the packed vertex: x-half, y-half, the clamped heightmap sample, the blend,
// the normal as three bytes, the AO uv shorts.
// Bit-exact transcription (disassembly-verified for the IMUL using the heightmap width).
static void FillRoadVertex(WorldGeometry* geo, int x, int y, RoadVertexScratch* outVtx, uint8_t blend) {
    int half = geo->_gridSize >> 1;

    const Math::Vector3 normal = geo->TerrainNormal(x, y);   // 0x1400ae270 into local_18/local_14/local_10

    short hSv = static_cast<short>(half);
    outVtx->x = static_cast<short>(static_cast<short>(x) - hSv);
    outVtx->y = static_cast<short>(static_cast<short>(y) - hSv);

    // Terrain-normal -> rgba8 encode (disasm 0x1400acb1a..0x1400acba6):
    //   byte = (normal_component + 1.0) * 127.5
    // where 1.0 = DAT_14017214c (XMM3, the +1 UNORM offset) and 127.5 = DAT_140172214
    // (XMM2, the [-1,1]->[0,255] scale). Hardcoded: an earlier port read them through
    // accessors that fell back to 0.0 (the encode collapsed to ~0 -> garbage normal ->
    // washed-out decal lighting) and ALSO swapped the two constants.
    const float addOff = 1.0f;    // DAT_14017214c
    const float encScl = 127.5f;  // DAT_140172214
    int hmW = geo->_fieldD0;

    int cx = (x < 0) ? 0 : x;
    if (hmW <= cx) cx = hmW - 1;
    int cy = (y < 0) ? 0 : y;
    if (hmW <= cy) cy = hmW - 1;

    const int16_t* hm = geo->_heights.data;
    short hVal = hm[static_cast<int64_t>(hmW) * cy + cx];

    // normal encode: byte = (normal + 1.0) * 127.5  (all three components)
    float n2 = normal.z + addOff;
    float n0 = normal.x + addOff;
    outVtx->normalW = 0;
    outVtx->blend = static_cast<uint16_t>(blend);
    outVtx->height = hVal;
    outVtx->normal[0] = static_cast<int8_t>(static_cast<int64_t>(n0 * encScl));
    outVtx->normal[1] = static_cast<int8_t>(static_cast<int64_t>((normal.y + addOff) * encScl));
    outVtx->normal[2] = static_cast<int8_t>(static_cast<int64_t>(n2 * encScl));
    // AO uv shorts:
    float span = static_cast<float>(geo->_gridSize);
    outVtx->aoU = static_cast<short>(static_cast<int>((static_cast<float>(x) / span) * f172264()));
    outVtx->aoV = static_cast<short>(static_cast<int>((static_cast<float>(y) / span) * f172264()));
}

// Tiled-decal / road mask-atlas rect LUT, retail .rdata 0x1401c23d0: 47 rows x 4 uint16
// (376 bytes, read word for word 2026-10-11; a pi/2 double follows at 0x1401c2548). One row
// per tile-shape variant 0..46 (kTileShapeVariantLut, action_pf_geom.cpp, maps every
// reachable edge mask into 0..46). Consumer 0x1400acc10 (BuildRoadQuad) indexes it as
// `[0x1401c23d0 + (int64)dirIdx * 8]` (MOVSXD, then RCX + RAX*8) and scatters {t0,t1,t2,t3}
// into the four vertices' texcoord.zw. TiledDecal_ps samples the 'tiles' mask at
// texcoord.zw / 4096: each row is a 256-texel atlas cell's corners, at the cell's centre
// half-texel set (128 + 256 * n).
constexpr uint16_t kRoadMaskAtlasRect[47][4] = {
    {128, 128, 384, 384},   // 0
    {640, 128, 896, 384},   // 1
    {1152, 128, 1408, 384},   // 2
    {1664, 128, 1920, 384},   // 3
    {2176, 128, 2432, 384},   // 4
    {2688, 128, 2944, 384},   // 5
    {3200, 128, 3456, 384},   // 6
    {3712, 128, 3968, 384},   // 7
    {128, 640, 384, 896},   // 8
    {640, 640, 896, 896},   // 9
    {1152, 640, 1408, 896},   // 10
    {1664, 640, 1920, 896},   // 11
    {2176, 640, 2432, 896},   // 12
    {2688, 640, 2944, 896},   // 13
    {3200, 640, 3456, 896},   // 14
    {3712, 640, 3968, 896},   // 15
    {128, 1152, 384, 1408},   // 16
    {640, 1152, 896, 1408},   // 17
    {1152, 1152, 1408, 1408},   // 18
    {1664, 1152, 1920, 1408},   // 19
    {2176, 1152, 2432, 1408},   // 20
    {2688, 1152, 2944, 1408},   // 21
    {3200, 1152, 3456, 1408},   // 22
    {3712, 1152, 3968, 1408},   // 23
    {128, 1664, 384, 1920},   // 24
    {640, 1664, 896, 1920},   // 25
    {1152, 1664, 1408, 1920},   // 26
    {1664, 1664, 1920, 1920},   // 27
    {2176, 1664, 2432, 1920},   // 28
    {2688, 1664, 2944, 1920},   // 29
    {3200, 1664, 3456, 1920},   // 30
    {3712, 1664, 3968, 1920},   // 31
    {128, 2176, 384, 2432},   // 32
    {640, 2176, 896, 2432},   // 33
    {1152, 2176, 1408, 2432},   // 34
    {1664, 2176, 1920, 2432},   // 35
    {2176, 2176, 2432, 2432},   // 36
    {2688, 2176, 2944, 2432},   // 37
    {3200, 2176, 3456, 2432},   // 38
    {3712, 2176, 3968, 2432},   // 39
    {128, 2688, 384, 2944},   // 40
    {640, 2688, 896, 2944},   // 41
    {1152, 2688, 1408, 2944},   // 42
    {1664, 2688, 1920, 2944},   // 43
    {2176, 2688, 2432, 2944},   // 44
    {2688, 2688, 2944, 2944},   // 45
    {3200, 2688, 3200, 2688},   // 46
};

// ============================================================
// FUN_1400acc10 — build the 4 vertices of one road-tile quad.
// ============================================================
// The 4 corners are (x,y),(x+1,y),(x+1,y+1),(x,y+1); each filled via caa0 into a local
// vertex, then scattered into quad[i] (the normal bytes move from +0x0C to +0x10). After the
// 4th, the corners' mask-atlas coordinates come from the (DAT_1401c23d0 + shape*8) table.
void WorldGeometry::BuildRoadQuad(int x, int y, GeometryBatchVertex* quad, int dirIdx, uint8_t blend) {
    RoadVertexScratch localVtx;   // caa0 writes a full 0x14-byte vertex here (local_38..)

    int iVar5 = 0;
    while (true) {
        int iVar2 = x;
        int iVar3 = y;
        if (iVar5 == 1)      { iVar2 = x + 1; }
        else if (iVar5 == 2) { iVar3 = y + 1; iVar2 = x + 1; }
        else if (iVar5 == 3) { iVar3 = y + 1; }
        // iVar5 == 0 -> (x,y)
        FillRoadVertex(this, iVar2, iVar3, &localVtx, blend);   // 0x1400acaa0

        // Scatter the local vertex into quad[iVar5], in retail's store order.
        GeometryBatchVertex& v = quad[iVar5];
        iVar5++;
        v.position[0] = localVtx.x;                         // local_38
        v.normal[0]   = localVtx.normal[0];                 // local_2c
        v.uv[0]       = localVtx.aoU;                       // local_30
        v.position[1] = localVtx.y;                         // local_36
        v.normal[1]   = localVtx.normal[1];                 // local_2b
        v.uv[1]       = localVtx.aoV;                       // local_2e
        v.position[2] = localVtx.height;                    // local_34
        v.normal[2]   = localVtx.normal[2];                 // local_2a
        v.position[3] = localVtx.blend;                     // local_32
        v.normal[3]   = localVtx.normalW;                   // local_29

        if (3 < iVar5) {
            // the 4 corners' mask-atlas texcoord.zw: row dirIdx of kRoadMaskAtlasRect
            // (0x1400acd0b: MOVSXD RAX, dirIdx; LEA RCX,[0x1401c23d0 + RAX*8]).
            const uint16_t* t = kRoadMaskAtlasRect[static_cast<int64_t>(dirIdx)];
            // DAT_1401c23d0=t[0], d2=t[1], d4=t[2], d6=t[3]
            quad[0].uv[2] = t[0];
            quad[0].uv[3] = t[1];
            quad[1].uv[2] = t[2];
            quad[1].uv[3] = t[1];
            quad[2].uv[2] = t[2];
            quad[2].uv[3] = t[3];
            quad[3].uv[2] = t[0];
            quad[3].uv[3] = t[3];
            return;
        }
    }
}

// ============================================================
// FUN_1400ab150 — enqueue up-to-4 coarse cells into the dirty AVL.
// ============================================================
// WorldGeometry::MarkChunksDirty. _chunkSize = cells per chunk edge, _gridSize = the
// heightmap edge, the dirty-chunk tree at +0xe8. Given a cell (x,y), if x (resp. y) lands on
// a chunk boundary and the previous chunk column/row exists, insert the adjacent chunks;
// always insert the owning chunk. Each insert via 0x14001a4f0(tree, &outNode, &key).
void WorldGeometry::MarkChunksDirty(int x, int y) {
    int cs = _chunkSize;                // cells per chunk edge
    int qx = x / cs;
    int qy = y / cs;
    int cw = _gridSize / cs;            // chunks per row

    // The dirty-chunk set (WorldGeometry +0xE8); 0x14001a4f0 takes it as one Set header.
    AvlTreeHead<AvlNode28>* tree = &_dirtyChunks;
    AvlNode28* outNode = nullptr;   // local_res8
    int32_t key[2] = {0, 0};        // local_res10

    if ((x % cs == 0) && (-1 < qx - 1)) {
        key[0] = static_cast<int32_t>(cw * qy - 1 + qx);
        AvlInsert28(tree, &outNode, key);   // 0x14001a4f0
    }
    if ((y % cs == 0) && (-1 < qy - 1)) {
        key[0] = static_cast<int32_t>((qy - 1) * cw + qx);
        AvlInsert28(tree, &outNode, key);   // 0x14001a4f0
    }
    if ((x % cs == 0) && (y % cs == 0) && (-1 < qx - 1) && (-1 < qy - 1)) {
        key[0] = static_cast<int32_t>((qy - 1) * cw - 1 + qx);
        AvlInsert28(tree, &outNode, key);   // 0x14001a4f0
    }
    key[0] = static_cast<int32_t>(cw * qy + qx);
    AvlInsert28(tree, &outNode, key);   // 0x14001a4f0
}


// ============================================================
// FUN_1400a8c90 — mark a fine cell + its 3x3 neighbourhood dirty.
// ============================================================
// self (param_1): a region/dirty grid. +0x58 cellW, +0x5c cellH, +0x60 gridW, +0x64 gridH,
// +0x78 cell array (stride 0x20: [+0]=col, [+4]=row, [+8]=dirtyFlag byte), +0x88 dirty-list
// (int* ptr), +0x90 count, +0x94 cap. If the owning coarse cell isn't already flagged, flag it,
// append its index to the dirty list (grow-by-3 on a non-empty append), then walk the 3x3 fine
// neighbourhood and call a8420 on each in-bounds fine cell.
void RegionGraph::MarkCellDirty(int x, int y) {
    int cellW = blockWidth;
    int cellH = blockHeight;
    int gridW = blocksWide;
    int idx = (y / cellH) * gridW + (x / cellW);
    int64_t lOff = static_cast<int64_t>(idx);
    const RegionBlock* cellArr = blocks.data;

    if (cellArr[lOff].needsRebuild != '\0') return;
    blocks.data[lOff].needsRebuild = 1;

    // append idx to dirtyRebuild
    int oldN = dirtyRebuild._size;
    int newN = oldN + 1;
    if (newN != oldN) {
        int32_t* oldBuf = dirtyRebuild._data;
        dirtyRebuild._size = newN;
        if (dirtyRebuild._capacity < newN) {
            dirtyRebuild._capacity = newN;
            if (oldN != 0) dirtyRebuild._capacity = newN * 3;
            dirtyRebuild._data = static_cast<int32_t*>(System::MemoryAllocator::MemAlloc(
                              static_cast<uint64_t>(dirtyRebuild._capacity) << 2, 4));
        }
        int srcIdx = (oldBuf == dirtyRebuild._data) ? oldN : 0;
        for (int64_t i = srcIdx; i < newN; i++) {
            int* dst = &dirtyRebuild._data[i];
            if (dst) {
                int v = idx;
                if (i < oldN) v = oldBuf[i];
                *dst = v;
            }
        }
        if (oldBuf != dirtyRebuild._data)
            System::MemoryAllocator::MemFree(oldBuf);
    }

    // 3x3 block neighbourhood: rows (row-1 .. row+1), cols (col-1 .. col+1)
    int baseCol = blocks.data[lOff].x;
    int rowStart = blocks.data[lOff].y - 1;
    int gridH = blocksHigh;
    int fineGW = blocksWide;
    for (int r = 0; r < 3; r++) {
        int rr = rowStart + r;
        if (-1 < rr && rr < gridH) {
            int cc = baseCol - 1;
            for (int c = 0; c < 3; c++) {
                if (-1 < cc && cc < fineGW) {
                    MarkAdjacencyDirty(rr * fineGW + cc);   // 0x1400a8420
                }
                cc++;
            }
        }
    }
}


// ============================================================
// FUN_140122e20 — the world position of an interact node.
// ============================================================
// Member-style hidden-return ABI (disassembly-verified): RCX=self, RDX=retVec3, R8=kind(p3),
// R9=index(p4), [stack]=heading(p5). Returns retVec3.
// The component's InteractDescription holds the node arrays (0 use, 1 build, 2 create). If
// this component's own `kind` count <= index, walk the sub-locations accumulating their
// counts (0x140123070) and recurse into the owning one with (index - running count).
// Otherwise resolve the node through the entity's ModelComponent transform: optionally
// rotate the node's direction to get a heading, then transform the node's offset.
Math::Vector3* InteractComponent::NodePosition(Math::Vector3* retVec3, int p3, int p4, float* p5) const {
    Description* buildRaw = _description;

    // B6 (14d G49): 0x140122e4f IsDerivedFrom(+0x18, cell 0x1401c7938), 0x140122e5f CMOVNZ to 0,
    // 0x140122e6e reads the node count through it with no test.
    uint32_t tid = InteractDescription::TypeId();
    const InteractDescription* build = nullptr;
    if (buildRaw->IsDerivedFrom(tid)) build = static_cast<const InteractDescription*>(buildRaw);

    const InteractDescription::NodeArray& nodes = (&build->_useNodes)[p3];
    int local_68 = nodes.count;

    if (local_68 <= p4) {
        int childN = useLocations._size;
        int i = 0;
        if (0 < childN) {
            do {
                int n = useLocations[i].Get()->NodeCount(p3);   // 0x140123070
                if (local_68 <= p4 && p4 < n + local_68) {
                    useLocations[i].Get()->NodePosition(retVec3, p3, p4 - local_68, p5);   // 0x140122e20
                    return retVec3;
                }
                i++;
                local_68 += n;
            } while (i < useLocations._size);
        }
        Math::Vector3 zero(Math::Scalar(0.0f));   // Math::Vector3::Vector3(ret,&scalar0)
        retVec3->x = zero.x;
        retVec3->y = zero.y;
        retVec3->z = zero.z;
        return retVec3;
    }

    // 0x140122e8f FindComponent<ModelComponent> (0x140003f60); its transform is read at
    // 0x140122ecc.. / 0x140122f58.. with no test (B6 14d G49: the exact-type fallback scan and
    // the zero-vector return are gone).
    ModelComponent* model = _entity->FindComponent<ModelComponent>();   // 0x140003f60

    // The node (kind, index) — disasm-verified (0x140122ea2..0x140122f42): node stride
    // 0x18 = {offset.xyz @0x0, dir.xyz @0xc}. The heading branch rotates node.direction and
    // the tail transforms node.offset.
    const InteractDescription::Node* node = &nodes.data[p4];   // 0x140122eb1 / 0x140122f3d, no test

    if (p5 != nullptr) {
        // local_50..local_38 = the 7 dwords of the ModelComponent's Math::Transform
        Math::Transform xform = model->transform;
        // local_60 (Vector3 out) = quaternion.Rotate(node.dir)
        Math::Vector3 dir = xform.rotation.Rotate(node->direction);
        float h = dir.Heading();
        *p5 = h;
    }

    Math::Transform xform2 = model->transform;
    Math::Vector3 pos = xform2 * node->offset;
    retVec3->x = pos.x;
    retVec3->y = pos.y;
    retVec3->z = pos.z;
    return retVec3;
}

// ============================================================
// FUN_14004edb0 — pick a random node of a target's interact tree and its reachable cost.
// ============================================================
// `target`: a component on the target entity. Finds the entity's InteractComponent
// (002ed0), gets its node count (123070), rolls a random index, resolves that node's world
// XY (122e20), converts to a grid cell, then validates reachability (a00f0 with path type
// 0xe1f) — if reachable, returns the distance (008830), else -1.
int Component::PickReachableCell(uint32_t param_2, float* param_3) const {
    GameUpdateLoop* world = GameUpdateLoop::_singleton;
    Map* map = world->m_game;

    InteractComponent* interact = _entity->FindComponent<InteractComponent>();   // 0x140002ed0

    uint32_t cnt = static_cast<uint32_t>(interact->NodeCount(0));   // 0x140123070
    uint32_t rnd = world->m_random->Rand(static_cast<uint32_t>(cnt));

    Math::Vector3 pos;   // local_28/24/20
    interact->NodePosition(&pos, 0, static_cast<int>(rnd), nullptr);   // 0x140122e20
    param_3[0] = pos.x;
    param_3[1] = pos.y;
    param_3[2] = pos.z;

    int w = map->size;
    int gy = static_cast<int>(static_cast<float>(w >> 1) + param_3[1]);
    int gx = static_cast<int>(static_cast<float>(w >> 1) + param_3[0]);
    gx = ClampCell(gx, w);
    gy = ClampCell(gy, w);
    uint32_t cell = static_cast<uint32_t>(w * gy + gx);

    // LEFT: 0x1400a00f0 / 0x140008830's C declarations (Work/exe_bodies.h, WORK-a's header)
    // take the Map as int*.
    Map* mapGrid = map;
    uint64_t ok = mapGrid->ValidateCell(0xe1f, param_2, static_cast<int>(cell));   // 0x1400a00f0
    if (static_cast<char>(ok) == '\0') return -1;
    return mapGrid->CalcDistance(param_2, cell);   // 0x140008830
}

// ============================================================
// FUN_140087fb0 — compute an entity's anchor cell + world position.
// ============================================================
// `entityIBP` = the entity's IBP, `param_2` = optional out world pos (float[3]). Returns the
// grid cell index (or -1). If the entity has an InteractComponent (002ed0) with >0 nodes,
// optionally roll a random node index (result discarded), resolve node 0 (122e20), and map
// world XY -> grid cell. Else fall back to the MapComponent footprint (004680): tileX0 /
// tileY0 (col/row) plus half of tileX1 / tileY1. If param_2 given and cell>=0, fill the
// world pos via GridToWorld (f3e0).
int EntityAnchorCell(ComponentEntity* const* entityIBP, float* param_2) {
    int iVar4 = -1;

    // B6 (14d G50): 0x140087fb0 CheckPtr(param_1) then FindComponent<InteractComponent> on
    // *param_1 with no test of the entity (no gate run ever printed the old [EmployCrash]
    // diagnostic this replaces); only the two finder results are tested, as below.
    ComponentEntity* resolvedAnchorEntity = *entityIBP;
    GameUpdateLoop* world = GameUpdateLoop::_singleton;
    InteractComponent* interact = resolvedAnchorEntity->FindComponent<InteractComponent>();   // 0x140002ed0

    bool handled = false;
    if (interact != nullptr) {
        int n = interact->NodeCount(0);   // 0x140123070
        if (0 < n) {
            int n2 = interact->NodeCount(0);
            if (1 < n2) {
                uint32_t n3 = static_cast<uint32_t>(interact->NodeCount(0));
                world->m_random->Rand(static_cast<uint32_t>(n3)); // result discarded
            }
            Math::Vector3 pos;   // local_28/24/20
            interact->NodePosition(&pos, 0, 0, nullptr);   // 0x140122e20
            int w = world->m_game->size;
            int gx = static_cast<int>(pos.x + static_cast<float>(w >> 1));
            int gy = static_cast<int>(pos.y + static_cast<float>(w >> 1));
            gx = ClampCell(gx, w);
            gy = ClampCell(gy, w);
            iVar4 = w * gy + gx;
            handled = true;
        }
    }

    if (!handled) {
        const MapComponent* footprint = (*entityIBP)->FindComponent<MapComponent>();   // 0x140004680
        if (footprint != nullptr) {
            int w = world->m_game->size;
            int row = static_cast<int>(footprint->tileY0);
            int offR = static_cast<int>(footprint->tileY1) >> 1;
            int col = static_cast<int>(footprint->tileX0);
            int offC = static_cast<int>(footprint->tileX1) >> 1;
            iVar4 = (offR + row) * w + offC + col;
        }
    }

    if (param_2 != nullptr && -1 < iVar4) {
        Map* map = world->m_game;
        float half = static_cast<float>(map->size >> 1) - f172114();   // DAT_140172114
        float local[3];
        local[0] = static_cast<float>(iVar4 % map->size) - half;
        local[1] = static_cast<float>(iVar4 / map->size) - half;
        local[2] = 0.0f;
        float wp[3] = {0, 0, 0};   // local_28..local_20
        map->GetTerrainPos(wp, local, '\0', nullptr);   // 0x14009f3e0
        param_2[0] = wp[0];
        param_2[1] = wp[1];
        param_2[2] = wp[2];
    }
    return iVar4;
}


// ============================================================
// FUN_1400af370 — rebuild the whole road mesh (per-tile verts/indices).
// ============================================================
// `map`: the Map (its size and cells). The WorldGeometry arrays it uses:
//   +0x200 (_arrays[6]) the mesh batches (RoadBatch, stride 0x60) — owned by the f250 / fa80
//                       grow helpers;
//   +0x210 (_arrays[7]) the per-batch source table (a qword per batch; its count is the
//                       batch count);
//   +0x220 (_arrays[8]) the per-batch fill counters (int).
// _gridSize / _fieldC4 = tile span / inset; (span - inset) >> 1 = vtxOff. _chunkSize = the
// block dimension. MapCell::field_0x2A = batchId, field_0x2B = blendId.
//
// Phases: (1) sync the counter array length to the batch count (021d0) then zero it; (2)
// f250-size a local working batch array; per block, seed each batch header (source, culling
// AABB) and reset its two buffers (c890/ee40); (3) per fine cell read batchId/blendId, build
// the 3x3 corner-occupancy mask, fold it to a tile shape (ae180), build the quad (cc10),
// append 6 corner indices to the index buffer (AppendIndex), and push the 4 verts into the
// vertex buffer (1f8d0) after overwriting their AO-uv slots with the per-corner
// (fineCol,fineRow)+offset tile coords; (4) per batch with vertices, append the batch to the
// mesh batches (fa80); (5) free both buffers per batch + the working array.
//
// Index details — BOTH pinned against the disassembly (0x1400af588..0xaff6e):
//   * mask -> ae180 arg order (p2..p9) = mask[3],[5],[1],[7],[0],[2],[6],[8] (slot = rowDelta*3+col,
//     rowDelta/col in {-1,0,1}; center slot[4] forced 1).  Verified at 0xaf61f..0xaf662.
//   * the vertex push overwrites aoU with local_c8[8+i*2]+fineCol and aoV with
//     local_c8[i*2]+fineRow, i.e. corner (xOff,yOff)+tile.  local_c8 (16 shorts) =
//     {0,0,0,0,1,0,1,0, 0,0,1,0,1,0,0,0}.
//
// AppendIndex: the inline "append one short to a batch's index buffer" grow+write block,
// identical across the 8 grow-blocks (they differ only by the value written to a freshly-grown
// slot). Reads count fresh each call; new = old+1; cap rule "cap=new; if new==old+1 && old!=0
// cap=new*3".
// B.4 H5-P2: that block is step for step Collection::Array<uint16_t>::SetSize(n + 1,
// value) (rtcoll::SetSizeCore over the trait row: stride 2, align 2, byte copy, no
// init / destroy; the exact-equal test can never fire), so it is that call on the
// typed GeometryBatch::indices.
static void AppendIndex(Collection::Array<uint16_t>* a, short valueForNew) {
    a->SetSize(a->_size + 1, static_cast<uint16_t>(valueForNew));
}

void WorldGeometry::RebuildRoadMesh(Map* map) {
    GeometryBatchArray* meshBatches = &_roadBatches;         // +0x200 (local_a8)
    const RawArray<Graphics::MaterialInstance*>* sources = &_batchMaterials; // +0x210
    Collection::Array<int32_t>* counters = &_batchCounters;  // +0x220
    GeometryBatch_Resize(*meshBatches, 0);                   // f250(local_a8, 0)

    int vtxOff = (_gridSize - _fieldC4) >> 1; // iVar12

    // Phase 1: counter array length sync (vs the batch count) then zero every counter.
    if (counters->_size != sources->size) {
        // FUN_1400021d0 == Collection::Array<int32>::SetSize: the grown tail is left as
        // the allocation found it, exactly as retail's helper does at 1400af3df. Safe:
        // the counter-zeroing loop right below (retail 1400af400) writes all the
        // elements before anything reads one.
        counters->SetSize(sources->size);
    }
    {
        int cnt = counters->_size;
        for (int i = 0; i < cnt; i++)
            counters->_data[i] = 0;
    }

    // Phase 2: local working batch array, sized to the batch count.
    // Its ctor is retail's two qword zero stores (0x1400af419 / 0x1400af41e); its
    // destructor at the end of the body is retail's inlined ~Array (0x1400b0053..
    // 0x1400b00b3: 0x14001fb80's body per batch, MemFree, the header zeroed).
    GeometryBatchArray batches;                              // local_108
    GeometryBatch_Resize(batches, static_cast<uint32_t>(sources->size));   // 0x14001f250

    int batchCount = batches._size;                           // (int)local_100
    int gridDim = map->size;                                  // MOVSXD R13,[RDI]
    int cellSize = _chunkSize;                                // +0xc8 (block dim)

    // local_c8 corner-offset table (16 shorts), used by the per-vertex push.
    static const short kCornerOff[16] = {0,0,0,0,1,0,1,0, 0,0,1,0,1,0,0,0};

    int local_f8 = 0;
    if (0 < gridDim) {
        do {                                   // outer block row (local_f8)
            int local_f4 = 0;
            do {                               // outer block col (local_f4)
                // ---- seed each batch header for this block ----
                if (0 < batchCount) {
                    int seedColMin = local_f4 + vtxOff;   // iVar26 (R15D = local_f4+vtxOff)
                    int seedRowMin = local_f8 + vtxOff;   // iVar16 (R12D = local_f8+vtxOff)
                    for (int b = 0; b < batchCount; b++) {
                        GeometryBatch& rec = batches._data[b];
                        // The +0x210 table holds each batch's material (BackfillBatchMaterials copies
                        // GeometryBatch::material into it): a qword copy in retail.
                        rec.material = sources->data[b];
                        // FUN_14001c890 == Array<uint16>::SetSize on +0x18 (0x1400af495),
                        // then FUN_14001ee40 == Array<GeometryBatchVertex>::SetSize on +0x08
                        // (0x1400af49f), retail's order (B.4 H5-P2: the tree had them the
                        // other way round, each under the other's element type). Both
                        // shrink to 0, so neither body's new-tail arm is exercised.
                        rec.indices.SetSize(0);
                        rec.vertices.SetSize(0);
                        // Culling AABB in tile coords, read by the TerrainGenerator cull pass
                        // (0x1400ae490 -> Volume::Frustum::Contains). `slot` (+0x28, f250
                        // seeds -1) must NOT be written here.
                        rec.minX = seedColMin;
                        rec.minY = seedRowMin;
                        rec.maxX = _chunkSize + local_f4 + vtxOff;
                        rec.maxY = _chunkSize + local_f8 + vtxOff;
                    }
                }

                // ---- per fine cell in this block ----
                int local_d4 = 0;
                if (0 < cellSize) {
                    do {                       // fine row within block (local_d4)
                        if (0 < cellSize) {
                            int fineRow = local_f8 + local_d4;     // local_dc (R8)
                            int rowBase = fineRow * gridDim;       // local_d0
                            int local_e0 = 0;
                            do {               // fine col within block (local_e0)
                                int fineCol = local_e0 + local_f4; // iVar20 (local_cc)
                                uint64_t cellIdx = static_cast<uint64_t>(rowBase + fineCol);
                                uint8_t batchId = map->cells.GetData()[cellIdx].field_0x2A;
                                uint8_t blendId = map->cells.GetData()[cellIdx].field_0x2B;

                                if (batchId != 0xff) {
                                    if (blendId == 0xff) {
                                        counters->_data[batchId] += 1;
                                    }

                                    // ---- 3x3 corner-occupancy mask local_f0[0..8] ----
                                    char local_f0[9];
                                    {
                                        int slot = 0;
                                        int row = fineRow - 1;          // R9D (iVar26)
                                        for (int rd = 0; rd < 3; rd++) {   // rowDelta = -1,0,1 (lVar19 / lVar9=3)
                                            int rowDelta = rd - 1;
                                            int64_t rowOff = static_cast<int64_t>(row) * gridDim; // lVar18
                                            for (int cd = -1; cd < 2; cd++) {   // iVar17 = -1,0,1
                                                if (cd == 0 && rowDelta == 0) {
                                                    local_f0[slot] = 1;
                                                } else {
                                                    int iVar1 = cd + fineCol;
                                                    if (iVar1 < 0 || iVar1 >= gridDim || row < 0 || row >= gridDim) {
                                                        local_f0[slot] = 0;
                                                    } else {
                                                        local_f0[slot] = (map->cells.GetData()[static_cast<int64_t>(iVar1) + rowOff].field_0x2A != 0xff) ? 1 : 0;
                                                    }
                                                }
                                                slot++;
                                            }
                                            row++;
                                        }
                                    }

                                    int blendVal = Pf_TileShapeLut_ae180(nullptr,   // 0x1400ae180
                                                               local_f0[3], local_f0[5], local_f0[1], local_f0[7],
                                                               local_f0[0], local_f0[2], local_f0[6], local_f0[8]);

                                    // build the quad (cc10) into the local scratch (local_98).
                                    GeometryBatchVertex quad[4];
                                    BuildRoadQuad(fineCol + vtxOff, fineRow + vtxOff,
                                                  quad, blendVal, blendId);   // 0x1400acc10

                                    GeometryBatch& rec = batches._data[batchId];
                                    short sVar21 = static_cast<short>(rec.vertices._size);

                                    // ---- push the 6 corner indices (parity split on (rowSeed+vtxOff + colSeed+vtxOff)) ----
                                    if (((fineRow + vtxOff + fineCol + vtxOff) & 1) == 0) {
                                        AppendIndex(&rec.indices, sVar21);       // +0
                                        AppendIndex(&rec.indices, sVar21 + 3);   // +3
                                        AppendIndex(&rec.indices, sVar21 + 1);   // +1
                                        AppendIndex(&rec.indices, sVar21 + 1);   // +1
                                        AppendIndex(&rec.indices, sVar21 + 3);   // +3
                                        AppendIndex(&rec.indices, sVar21 + 2);   // +2
                                    } else {
                                        AppendIndex(&rec.indices, sVar21);       // +0
                                        AppendIndex(&rec.indices, sVar21 + 2);   // +2
                                        AppendIndex(&rec.indices, sVar21 + 1);   // +1
                                        AppendIndex(&rec.indices, sVar21);       // +0
                                        AppendIndex(&rec.indices, sVar21 + 3);   // +3
                                        AppendIndex(&rec.indices, sVar21 + 2);   // +2
                                    }

                                    // ---- push the 4 quad verts (1f8d0), overwriting their AO-uv
                                    //      slots with the per-corner tile coords. ----
                                    for (int vi = 0; vi < 4; vi++) {
                                        short cX = static_cast<short>(kCornerOff[8 + vi * 2] + static_cast<short>(fineCol)); // local_c8[+0x10 + i*4]
                                        short cY = static_cast<short>(kCornerOff[vi * 2]     + static_cast<short>(fineRow)); // local_c8[+0 + i*4]
                                        quad[vi].uv[0] = cX;   // AO-u slot
                                        quad[vi].uv[1] = cY;   // AO-v slot
                                        int curCount = rec.vertices._size;  // (int)plVar15[1]
                                        // FUN_14001f8d0 (0x1400aff5a) == Array<GeometryBatchVertex>::
                                        // SetSize(n, fill) (3-arg form) -- appends the vertex as
                                        // the new tail element.
                                        rec.vertices.SetSize(curCount + 1, quad[vi]);
                                    }
                                }
                                local_e0++;
                            } while (local_e0 < _chunkSize);
                        }
                        local_d4++;
                    } while (local_d4 < _chunkSize);
                }

                // ---- append each batch with vertices to the mesh batches (fa80) ----
                if (0 < batchCount) {
                    for (int b = 0; b < batchCount; b++) {
                        GeometryBatch& rec = batches._data[b];
                        if (0 < rec.vertices._size) {
                            meshBatches->SetSize(meshBatches->_size + 1, rec);   // 0x14001fa80
                        }
                    }
                }

                local_f4 += _chunkSize;
            } while (local_f4 < gridDim);
            local_f8 += _chunkSize;
        } while (local_f8 < gridDim);
    }

    // Phase 5 (0x1400b0053..0x1400b00b3): `batches`' destructor -- every batch's
    // index buffer, then its vertex buffer (each MemFree, then zeroed), then the
    // array's buffer and header.
}

// ===========================================================================
// B4 FORWARD: the C spellings other blocks' files still call, one line each.
// ===========================================================================

// The exported names of the two file-static bodies (callers: Component/quarrycomponent_deform.cpp,
// Player/tool_base_ray.cpp; Player/tool_base_tilerender.cpp).
// (Pf_EntityResolvePos_2e20, the void* forwarder to InteractNodePosition, went in B6 once its
// four callers were typed: rows 643 and the citizenai scans.)


// B4 FORWARD: outside callers in ~20 files (Component, Entity, Game, Player, Work)
