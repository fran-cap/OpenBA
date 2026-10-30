// src/game/Player/tool_base_tilerender.cpp
// Player wave — "tilerender" sub-batch. The PlacementTool (and RoadTool) footprint
// VALIDITY OVERLAY: the green/red (and yellow "existing") tiles drawn on the terrain
// under the building ghost. This is the last piece of the placement UX — it shows
// whether the current spot is buildable AND which way the building's entry faces.
//
// Closure (resolved via ResolveAddr(Module::Exe, 0xADDR) from placementtool.cpp's
// Draw slot):
//   0x140101380  sub_1380  — build the per-cell tile-record array (col,row,variant,
//                            color,angle) validating each cell via f5d0, then hand it
//                            to 08c0 to tessellate + submit.  [ported here]
//   0x1401008c0  08c0 (CharacterComponent_impl_08c0) — Video::Context overlay render:
//                            FindOrLoad "Terrain/Selection.rsc" material, then for each
//                            record emit 6 verts (terrain-conformed quad, rotated-atlas
//                            UV, per-record RGBA color).  [ported here]
//   0x1400b10e0  b10e0     — sample a tile's 6 terrain corners (18 floats) from the
//                            heightfield with the correct even/odd triangulation. [ported here]
//   0x14009f5d0  f5d0      — single-cell terrain/occupancy validity test. ALREADY
//                            registered (reg_player_path.cpp → PlayerPath_CharComp_f5d0);
//                            resolved via ResolveAddr here.
//   0x1400b0f90  0f90      — vertical-down-ray terrain height sampler, RETURNS the hit
//                            height as a float (Ghidra types it void; see the disassembly
//                            quoted at its definition in action_pf_road.cpp).
//                            ⚠ The FIRST loop below is NOT dead. It is empty on the
//                            building-placement path (sub_1380 passes {0,0}), but
//                            PathTool::Draw fills param_4 with worker-route SEGMENTS, and
//                            that loop is the only consumer of this height. While 0f90 was
//                            registered as void-returning, every segment got a garbage Z
//                            and the Paths overlay drew a bar floating above the map.
//                            Do not re-describe this loop as dead.
//
// Each function is a 1:1 port of the Ghidra decompile + disassembly. Registration is in
// standalone/reg_player_tilerender.cpp (RegisterPlayerTileRenderBatch), wired into
// RegisterCodeFunctions().

// The runtime type headers come FIRST: they pull src/engine/volume/Volume.h,
// whose global-scope `struct Rectangle` is hidden by <wingdi.h>'s `Rectangle()`
// once <windows.h> has been seen through resolve.h.  ENDGAME B.3.5 batch I2.
#include "video/Context.h"
#include "graphics/Material.h"   // ENDGAME B.3.5 batch K1 — same include-order reason
#include "graphics/Camera.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "math/Math.h"
#include "crt/msvc_libm.h"   // MsvcLibm::Sinf / Cosf (the exe's sinf 0x140131c90 / cosf 0x140131090)
#include "system/Filename.h"
#include "object/External.h"   // ENDGAME B.3.3 step 6A
#include "collection/Collection.h"
#include <cstdint>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include "Player/exe_bodies.h"
#include "Action/exe_bodies.h"
#include "Game/exe_bodies.h"
#include "object/TypeInfo.h"
#include "Player/Tool.h"
#include "Component/CreatePlacedDescription.h"
#include "Game/Map.h"
#include "Game/WorldGeometry.h"

namespace {

// ---- Bit-exact .rdata float constants (raw .rdata constants are NULL via
// ResolveAddr in standalone — reference_standalone_rdata_null_fallback — so inline
// the verified values). ----------------------------------------------------------
static inline float C_half()    { return 0.5f;         }  // DAT_140172114
static inline float C_uvScale() { return 0.125f;       }  // DAT_1401720ec (0x3e000000)
static inline float C_deg2rad() { return 0.017453292f; }  // DAT_1401720cc (0x3c8efa35)
static inline float C_hScale()  { return 0.00390625f;  }  // DAT_1401720bc (0x3b800000 = 1/256)

// ENDGAME B.3.5 batch K1: the `s_matSetup` cache (exe dispatch slot 0x140152920,
// with a `?Setup@MaterialInstance@Graphics@@QEBAXAEAVContext@Video@@H@Z` fallback)
// went with its one caller below, which calls the member. THE DOOR DECIDES THE
// OVERLOAD: that mangled name is `Graphics::MaterialInstance::Setup` (0x180102c90,
// ported), NOT `Graphics::Material::Setup` (0x1801027e0, declared-undefined).

// The record array is owned/freed by sub_1380. Uses the proven engine_funcs
// allocator pair (fn_MemAlloc(singleton,size,align) + fn_MemFreeStatic(ptr)) — same
// heap the rest of the reimpl uses; the raw static-MemAlloc export doesn't resolve
// reliably in standalone.

// Overlay Z handling (standalone). DEFAULT = faithful: the grid conforms exactly to the
// collision terrain (verified: b10e0 == the game's own aa20 terrain ray), which renders
// correctly and is visible. On this map the placed building anchors at Z=0 (sub_da00
// forces pos.z=0) and sits ~0.06 below the rendered ground, so the faithful grid can read
// as "above the (sunk) model" — a model-Z / terrain-baseline issue, NOT an overlay bug.
//

// Called by name: 0x14009f5d0 PlayerPath_CharComp_f5d0 (single-cell validity test,
// Player/exe_bodies.h), 0x1400b0f90 Pf_CellQuadRayWrap_0f90 (terrain Z under a point,
// Action/exe_bodies.h), 0x14009f3e0 Reimpl_MapGetTerrainPos (grid -> world,
// Game/exe_bodies.h).

// The vertex 0x1401008c0 streams (stride 0x18): position, uv, ARGB.
struct OverlayVertex {
    float    x, y, z;   // +0x00
    float    u, v;      // +0x0C
    uint32_t color;     // +0x14
};
static_assert(sizeof(OverlayVertex) == 0x18, "the 0x18-byte overlay vertex (BeginVertices stride)");

} // namespace

// ============================================================
// 0x1400b10e0 — sample a tile's 6 terrain corners (18 floats)
// ============================================================
// Reads the two triangles (6 verts) of the terrain quad at grid (col,row) from the
// heightfield, choosing the even/odd triangulation by (col+row) parity. Each corner is
// (worldX, worldY, height) where worldX/Y are cell coords recentred on the map and
// height = (int16)heightmap[hdim*clamp(row) + clamp(col)] * (1/256).
//   WorldGeometry +0xc0 = grid width, +0xc4, +0xd0 = heightmap dimension (hdim),
//   +0xd8 = int16 heightmap data pointer.
// 1:1 port of the Ghidra decompile (clamp bound hdim is constant, so both branches use
// the same sampler; corner (posX,posY,col,row) tuples transcribed per branch).
extern "C" void PlayerTileRender_TerrainCorners_b10e0(const WorldGeometry* param_1, int col, int row,
                                                      float* out /*18 floats*/) {
    const float hScale = C_hScale();
    int base = (param_1->_gridSize - param_1->_fieldC4) >> 1;
    int c0 = base + col;          // iVar11
    int r0 = base + row;          // iVar3
    int c1 = c0 + 1;              // iVar10
    int r1 = r0 + 1;              // iVar8
    float center = (float)(param_1->_gridSize >> 1);
    int hdim = param_1->_fieldD0;
    const int16_t* hmap = param_1->_heights.data;

    float pC0 = (float)c0 - center;   // fVar15 (world X at c0)
    float pR0 = (float)r0 - center;   // fVar13 (world Y at r0)
    float pC1 = (float)c1 - center;   // fVar14
    float pR1 = (float)r1 - center;   // fVar12

    auto sampleH = [&](int cc, int rr) -> float {
        if (cc < 0) cc = 0; else if (cc >= hdim) cc = hdim - 1;
        if (rr < 0) rr = 0; else if (rr >= hdim) rr = hdim - 1;
        return (float)(int)hmap[(int64_t)hdim * rr + cc] * hScale;
    };
    auto put = [&](int i, float x, float y, int cc, int rr) {
        float z = sampleH(cc, rr);
        Math::Vector3 v(x, y, z);
        out[i * 3 + 0] = v.x; out[i * 3 + 1] = v.y; out[i * 3 + 2] = v.z;
    };

    if (((c0 + r0) & 1) == 0) {
        // EVEN triangulation
        put(0, pC0, pR0, c0, r0);
        put(1, pC0, pR1, c0, r1);
        put(2, pC1, pR0, c1, r0);
        put(3, pC1, pR0, c1, r0);
        put(4, pC0, pR1, c0, r1);
        put(5, pC1, pR1, c1, r1);
    } else {
        // ODD triangulation
        put(0, pC0, pR0, c0, r0);
        put(1, pC1, pR1, c1, r1);
        put(2, pC1, pR0, c1, r0);
        put(3, pC0, pR0, c0, r0);
        put(4, pC0, pR1, c0, r1);
        put(5, pC1, pR1, c1, r1);
    }
}

// ============================================================
// 0x1401008c0 — Video::Context terrain overlay render
// ============================================================
// Loads the "Terrain/Selection.rsc" MaterialInstance, sets an identity transform, opens
// a 0x18-byte-stride vertex stream, and for each tile record emits 6 verts:
//   position = terrain corner (b10e0), uv = a per-vertex atlas template rotated by the
//   record angle and offset by the record's texture-variant column, color = record RGBA.
// param_3 = the tile records (ToolTileRecord, 0x14 bytes each).
// param_4 = the segment records (ToolSegmentRecord, 0x20 bytes each). Empty on the
// building-placement path, but PathTool::Draw fills it with the worker-route segments —
// this loop is live there.
// RCX is never read. 1:1 port of the Ghidra decompile + disassembly (vertex layout
// pinned against 0x140100fc0 / 0x140101130).
extern "C" void PlayerTileRender_DrawOverlay_08c0(const void* /*param_1*/, Video::Context* ctx,
                                                  const Collection::Array<ToolTileRecord>* param_3,
                                                  const Collection::Array<ToolSegmentRecord>* param_4) {
    const float half = C_half();
    const float uvScale = C_uvScale();     // fVar3 = 0.125 (XMM13)
    const float deg2rad = C_deg2rad();

    GameUpdateLoop* gw = GameUpdateLoop::_singleton;
    WorldGeometry* heightfield = gw->m_worldGeometry;   // local_1e8
    Map* grid = gw->m_game;                             // local_1e0


    // --- FindOrLoad "Terrain/Selection.rsc" -> MaterialInstance, validated against
    //     Graphics::MaterialInstance's type id (.rdata slot 0x140151350). ---
    const Graphics::MaterialInstance* material = nullptr;
    {
        System::Filename fname("Terrain/Selection.rsc");
        ::Object::ExternalFactory* efactory = ::Object::ExternalFactory::_singleton;
        const Object::External* obj = efactory ? efactory->FindOrLoad(fname) : nullptr;
        if (obj && Object::IsDerivedFromNullSafe(obj, Graphics::MaterialInstance::TypeId()))
            material = static_cast<const Graphics::MaterialInstance*>(obj);
    }

    if (material)
        material->Setup(*ctx, 0);
    {
        Math::Matrix matrix = Math::Matrix::Identity();
        ctx->SetTransform(0, matrix);
    }

    // Guard: never iterate a record set whose base pointer is null (a failed alloc in
    // the builder would otherwise leave count>0 with a null base -> null-record deref).
    int count3 = (param_3 && param_3->_data) ? param_3->_size : 0;
    int count4 = (param_4 && param_4->_data) ? param_4->_size : 0;
    bool began = ctx->BeginVertices((count3 + count4) * 6, 0x18);
    if (!began) return;

    // Even/odd tile UV templates (6 (u,v) pairs each), verbatim from the decompile.
    static const float TMPL_ODD[12]  = {0,0, 1,1, 1,0, 0,0, 0,1, 1,1};   // local_2b0
    // local_1c8: verts 3,4 are (1,0),(0,1) — NOT (0,0),(1,1). Getting these wrong makes an
    // EVEN tile's 2nd triangle map to a degenerate UV (v4==v5==(1,1)) → the arrow texel
    // smears along the diagonal = the stray "|\/" sliver on even-parity entry tiles.
    static const float TMPL_EVEN[12] = {0,0, 0,1, 1,0, 1,0, 0,1, 1,1};   // local_1c8

    // --- Loop 1: the segment records (param_4). Empty on the placement path. ---
    for (int rIdx = 0; rIdx < count4; rIdx++) {
        const ToolSegmentRecord& rec = param_4->_data[rIdx];
        float variantCol = (float)rec.variant * uvScale;            // local_364
        float angle = (float)rec.angle * deg2rad;                   // rec+0x10
        float sinA = MsvcLibm::Sinf(angle);   // 0x140131c90 sinf
        float cosA = MsvcLibm::Cosf(angle);   // 0x140131090 cosf
        int w = grid->size;
        float rc = (float)(w >> 1) - half;
        // corner A (rec+0,+4)
        int idxA = rec.row1 * w + rec.col1;
        float inA[3] = { (float)(idxA % w) - rc, (float)(idxA / w) - rc, 0.0f };
        float wA[3] = {0,0,0}; { float* r = grid->GetTerrainPos(wA, inA, 0, nullptr); wA[0]=r[0]; wA[1]=r[1]; }
        float pA3[3] = { wA[0], wA[1], 50.0f };
        float hA = heightfield->HeightUnderPoint(pA3, nullptr);
        float A[3] = { wA[0], wA[1], hA };
        // corner B (rec+0x14,+0x18)
        int idxB = rec.row2 * w + rec.col2;
        float inB[3] = { (float)(idxB % w) - rc, (float)(idxB / w) - rc, 0.0f };
        float wB[3] = {0,0,0}; { float* r = grid->GetTerrainPos(wB, inB, 0, nullptr); wB[0]=r[0]; wB[1]=r[1]; }
        float pB3[3] = { wB[0], wB[1], 50.0f };
        float hB = heightfield->HeightUnderPoint(pB3, nullptr);
        float B[3] = { wB[0], wB[1], hB };
        // dir = normalize(B - A); perp offset = (-dir.y, dir.x, 0) * width (rec+0x1c)
        float dir[3] = { B[0]-A[0], B[1]-A[1], B[2]-A[2] };
        { float len = std::sqrt(dir[0]*dir[0] + dir[1]*dir[1] + dir[2]*dir[2]);
          if (len > 1e-6f) { dir[0]/=len; dir[1]/=len; dir[2]/=len; } }
        float wdt = rec.width;                                       // rec+0x1c (float)
        float off[3] = { -dir[1]*wdt, dir[0]*wdt, 0.0f };
        // 6 quad corners: A+off, B-off, A-off, A+off, B+off, B-off
        float quad[6][3] = {
            { A[0]+off[0], A[1]+off[1], A[2]+off[2] },
            { B[0]-off[0], B[1]-off[1], B[2]-off[2] },
            { A[0]-off[0], A[1]-off[1], A[2]-off[2] },
            { A[0]+off[0], A[1]+off[1], A[2]+off[2] },
            { B[0]+off[0], B[1]+off[1], B[2]+off[2] },
            { B[0]-off[0], B[1]-off[1], B[2]-off[2] },
        };
        for (int k = 0; k < 6; k++) {
            float ut = TMPL_ODD[k * 2];
            float vt = TMPL_ODD[k * 2 + 1];
            OverlayVertex vtx;
            vtx.x = quad[k][0]; vtx.y = quad[k][1]; vtx.z = quad[k][2];
            vtx.u = ((vt - half) * sinA + (ut - half) * cosA + half) * uvScale + variantCol;
            vtx.v = ((vt - half) * cosA - (ut - half) * sinA) + half;
            vtx.color = rec.color;                                   // rec+0xc
            ctx->AddVertex(&vtx);
        }
    }

    // --- Loop 2: placement tile records (param_3). THE LIVE overlay path. ---
    for (int rIdx = 0; rIdx < count3; rIdx++) {
        const ToolTileRecord& rec = param_3->_data[rIdx];
        float corners[18];
        PlayerTileRender_TerrainCorners_b10e0(heightfield, rec.col, rec.row, corners);
        const float* tmpl = (((rec.col + rec.row) & 1) == 0) ? TMPL_EVEN : TMPL_ODD;
        float variantCol = (float)rec.variant * uvScale;            // XMM8
        float angle = (float)rec.angle * deg2rad;                   // rec+0x10 * deg2rad
        float sinA = MsvcLibm::Sinf(angle);   // 0x140131c90 sinf (was mislabeled cos → arrows misrotated)
        float cosA = MsvcLibm::Cosf(angle);   // 0x140131090 cosf
        for (int k = 0; k < 6; k++) {
            float ut = tmpl[k * 2];
            float vt = tmpl[k * 2 + 1];
            OverlayVertex vtx;
            vtx.x = corners[k * 3 + 0];
            vtx.y = corners[k * 3 + 1];
            vtx.z = corners[k * 3 + 2];
            vtx.u = ((vt - half) * sinA + (ut - half) * cosA + half) * uvScale + variantCol;
            vtx.v = ((vt - half) * cosA - (ut - half) * sinA) + half;
            vtx.color = rec.color;                                   // rec+0xc
            ctx->AddVertex(&vtx);
        }
    }

    ctx->EndVertices(0);
}

// ============================================================
// 0x140101380 — build the per-cell tile-record array + draw
// ============================================================
// Walks the (param_3..param_5)/(param_4..param_6) footprint rectangle, and for each cell:
//   - computes a rotation-remapped index into two char maps (param_15 footprint,
//     param_9 code map indexing the description's code array),
//   - runs f5d0 to test validity for that cell,
//   - picks the tile color: green (0xff00ff00) if valid, red (0xffff0000) if invalid or
//     forceRed (param_13), yellow (0xffffff00) if isExisting (param_18),
//   - picks the texture-variant (0/1/2) from the footprint flag + param_17 sign,
//   - appends a record (col,row,variant,color,angle=param_16+param_17).
// Then hands the record array to 08c0 and frees it.
// 1:1 port of the Ghidra decompile (0x140101380); the rotation index switch (0x5a/0xb4/
// 0x10e via param_16) and the growth loop (×3 realloc) are reproduced exactly.
// Retail's param_8 is the address of the description's embedded code Array (+0x30);
// CreatePlacedDescription (block COMP-c) declares it as its raw triple, so the
// description itself is passed and the triple read by name (as for 0x14009d580).
// RCX is never read (retail reads the grid from the singleton).
extern "C" void PlayerTileRender_BuildRecords_1380(
        const void* /*param_1*/, Video::Context* ctx, int param_3, int param_4, uint32_t param_5,
        int param_6, uint32_t param_7, const CreatePlacedDescription* param_8,
        const System::String* param_9, float param_10, float param_11,
        float param_12, char param_13, char param_14, const System::String* param_15,
        int param_16, int param_17, char param_18, const ComponentDescription* param_19) {
    Map* grid = GameUpdateLoop::_singleton->m_game;   // local_b8

    // Growing record array, owned here and released by ~Array at scope exit (the
    // function has no early return past this point).
    Collection::Array<ToolTileRecord> records;   // pvVar6 / local_c8 = _data,
                                                 // low32(local_c0) = _size, pvVar11 = _capacity

    int rowEnd = param_6;        // last row (param_4..param_6 inclusive)
    int colEnd = (int)param_5;   // last col (param_3..param_5)
    int iVar21 = ((int)param_5 - param_3) + 1;   // width  (for rotation remap)
    int iVar13 = (param_6 - param_4) + 1;        // height

    if (param_4 <= rowEnd) {
        int iVar12 = param_6 - param_4;          // (height-1) countdown seed
        for (int drow = param_4; drow <= rowEnd; drow++, iVar12--) {
            if (param_3 > colEnd) continue;
            int angleSum = param_16 + param_17;  // local_a8
            int iVar20 = (int)param_5 - param_3; // col countdown seed
            for (int dcol = param_3; dcol <= colEnd; dcol++, iVar20--) {
                bool isRoad = false;
                // --- rotation-remapped flat index into the char maps ---
                // Four cases from the decompile's short-circuit switch (param_16 = the
                // orientation code 0x5a=90 / 0xb4=180 / 0x10e=270 / else=identity):
                //   iVar15 = column term, iVar5 = row term, (iVar14,iVar7) = strides.
                //   flatIdx = (iVar14 + 1) * ((iVar7 - iVar5) - 1) + iVar15.
                int relRow = drow - param_4;     // local_a4 + local_d8
                int relCol = dcol - param_3;     // local_a0 + iVar24
                int colCd  = iVar20;             // colEnd - dcol (col countdown)
                int rowCd  = iVar12;             // param_6 - drow (row countdown)
                int iVar15, iVar5, iVar7, iVar14;
                if (param_16 == 0x5a) {          // 90°
                    iVar15 = relRow; iVar5 = colCd;  iVar7 = iVar21; iVar14 = iVar13;
                } else if (param_16 == 0xb4) {   // 180°
                    iVar15 = colCd;  iVar5 = rowCd;  iVar7 = iVar13; iVar14 = iVar21;
                } else if (param_16 == 0x10e) {  // 270°
                    iVar15 = rowCd;  iVar5 = relCol; iVar7 = iVar21; iVar14 = iVar13;
                } else {                          // identity
                    iVar15 = relCol; iVar5 = relRow; iVar7 = iVar13; iVar14 = iVar21;
                }
                int flatIdx = (iVar14 + 1) * ((iVar7 - iVar5) + -1) + iVar15;

                const char* roadMap = param_15->_data;
                if (roadMap != nullptr && param_15->GetLength() != 0) {
                    isRoad = roadMap[flatIdx] == '.';
                }
                uint32_t code = param_7;
                if (param_8->_placeArray._size > 0) {
                    const char* codeMap = param_9->_data;
                    if (codeMap != nullptr && param_9->GetLength() != 0) {
                        // data - 0xc0 + c*4: the code array indexed by the char - '0'.
                        code = param_8->_placeArray._data[(int64_t)codeMap[flatIdx] - 0x30];
                    }
                }

                bool ok = grid->TestPlacement(dcol, drow, code, param_10, param_11, param_12,
                                           param_14, isRoad ? 1 : 0, param_19);

                uint32_t color;
                if (param_13 != '\0' || !ok) color = 0xffff0000u;   // red
                else                          color = 0xff00ff00u;   // green
                if (param_18 != '\0')         color = 0xffffff00u;   // yellow (existing)

                int variant = 0;
                if (isRoad) variant = ((-1 < param_17) ? 1 : 0) + 1;

                // --- append the record (grow ×3 like the original) ---
                ToolTileRecord rec;
                rec.col = dcol; rec.row = drow; rec.variant = variant;
                rec.color = color; rec.angle = angleSum;
                records.SetSize(records._size + 1, rec);
            }
        }
    }

    // Hand the records to 08c0 (param_4 = empty), then free.
    Collection::Array<ToolSegmentRecord> empty;
    PlayerTileRender_DrawOverlay_08c0(nullptr, ctx, &records, &empty);
    // `records` is released by ~Array at scope exit.
}
