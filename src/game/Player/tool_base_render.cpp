// src/game/Player/tool_base_render.cpp
// Entity SELECTION-OUTLINE render chain — faithful clean-room port of the
// HighlightComponent EdgeMesh tessellation family (Application-x64.exe).
//
// This is the "yellow selection outline" that retail draws around a selected/
// followed entity. The select->highlight hookup already exists
// (src/game/Dialog/edittool_select.cpp resolves s_fn_c9e0 = 0x1400fc9e0 and
// s_fn_cc90 = 0x1400fcc90); previously those were unregistered in standalone so
// the calls no-op'd. This file provides the whole closure so, when the outline
// is enabled (BANISHED_SELECTION_OUTLINE, default OFF — see
// standalone/reg_player_render.cpp), the select path rebuilds and submits a
// terrain-conforming Graphics::EdgeMesh to the live Graphics::Scene.
//
// Functions ported (bottom-up), each verified against the Ghidra decompile AND
// the disassembly (the EdgeMesh::Set 25-arg call + the inlined array-growth
// strides were confirmed instruction-by-instruction):
//   thunk_helper_d250_sub_7080  0x140007080   FindComponent(HighlightComponent) loop
//   TradeWork_impl_sub_74a0     0x1400074a0   16-byte EdgeVertex array grow
//   FUN_1400b10e0               0x1400b10e0   6-corner terrain tile sample (18 floats)
//   TradeWork_impl_b440         0x14004b440   one edge "ribbon" segment (4 EdgeVertex + 6 idx)
//   TradeWork_impl_b930         0x14004b930   the full EdgeMesh builder (outline + fill)
//   Entity_impl_c4a0            0x14004c4a0   rebuild EdgeMeshInstance + (re)add to Scene
//   Entity_impl_cb40            0x1400fcb40   clear highlight (+0x11c=0) over UIComponents
//   Entity_impl_cc90            0x1400fcc90   deselect: cb40 + close the info dialog
//   Entity_impl_c9e0            0x1400fc9e0   select: cb40 + set highlight (+0x11c=1) + repaint
//
// The four inlined growth loops share ONE canonical helper (GrowTo) that is a
// 1:1 transcription of TradeWork_impl_sub_74a0's grow idiom (append-one with the
// count==old+1 -> capacity*3 doubling rule). This deliberately avoids restating
// the decompiler's CONCAT44-packed count/capacity locals as ad-hoc pointer math,
// which was the transposition hazard that got the whole chain DEFERRED before.
// ENDGAME B.3.2 step 2: GrowTo is gone — that helper was a hand-written copy of
// rtcoll::SetSizeCore, and the four locals are now real Collection::Array objects
// (see the spec rows below the exe-helper table).
//
// Runtime methods go through engine_funcs fn_* pointers (resolved by mangled
// name from Runtime-x64.dll — see engine/funcs/engine_funcs.cpp): EdgeMesh
// ctor/Set/SetMaterials, TriangleVertex/EdgeVertex ctors, EdgeMeshInstance
// SetMesh/ctor, Box ctor/Extend/operator=, Quaternion ctor, Vector3 ctors,
// static MemAlloc/MemFree, Scene Add/RemoveObject, Geometry::SetTransform, Time.
// Exe helpers (already registered) go through direct ResolveAddr(Module::Exe).
//
// STANDALONE liveness notes (both guarded/lazy, so nothing here runs on the
// default byte-clean path — the chain is only registered when the env is set):
//   * The exe overrides the new EdgeMesh's vtable with an exe .rdata vtable
//     (0x140159a60, an EdgeMesh subclass). That address is not valid in the
//     standalone image, so we KEEP the Runtime base EdgeMesh vtable set by the
//     ctor (#ifndef STANDALONE_EXE guards the override). The base vtable is a
//     complete, valid Runtime vtable — adequate for live rendering (we never
//     serialize the mesh, which is the only thing the subclass overrides).
//   * The HighlightComponent's embedded EdgeMeshInstance at +0x28 is built by its
//     ctor (0x14004c930) and destroyed by its dtor (0x14004c8c0), as in retail.
//   * c4a0's ModelComponent lookup (retail: Entity_ClearPathQueue 0x140003f60)
//     yields the model-variant indices (+0x164/+0x166) that pick the per-variant
//     prebuilt outline mesh — the path retail uses for CITIZENS (which have no
//     terrain footprint, so the build branch can't fire). The shared registered
//     3f60 reads its type-id from .rdata (DWORD_1401c7a50), NULL in standalone,
//     so it always returned null there -> variant read as (-1,-1) -> build branch
//     -> bail. We do that FindComponent inline (FindModelComp) with the
//     GetTypeIdByName fallback so the citizen's ModelComponent is actually found.
//     (Still gated: whether the citizen HighlightDescription's +0x20 per-variant
//     mesh table loaded in SA is reported by BANISHED_OUTLINE_TRACE's
//     "PREBUILT-branch ... cellMesh=" line.)

// The runtime type headers come FIRST: they pull src/engine/volume/Volume.h,
// whose global-scope `struct Rectangle` is hidden by <wingdi.h>'s `Rectangle()`
// once <windows.h> has been seen through resolve.h.  ENDGAME B.3.5 batch I2.
#include "graphics/Scene.h"
#include "graphics/Mesh.h"
#include "graphics/Material.h"   // ENDGAME B.3.5 batch K1 — Graphics::MaterialInstance
#include "object/External.h"
#include "graphics/Camera.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "Game/WorldGeometry.h"    // m_worldGeometry: the heightfield SampleTile reads
#include "Game/Map.h"              // m_game: the tile grid
#include "Game/exe_constants.h"   // kRdata_* (B3R Leftovers: the .rdata constants)
#include "math/Math.h"
#include "Object/factory_registration.h"
#include "Component/HighlightComponent.h"
#include "Component/HighlightDescription.h"
#include "Component/ModelComponent.h"
#include "Component/MapComponent.h"
#include "Component/MultiObjectComponent.h"
#include "Entity/ComponentEntity.h"
#include "Player/EditTool.h"
#include "collection/Collection.h"
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <new>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "object/VSlot.h"      // B.3.x W0: ReportBadSelectionRef's slot probe
#include "system/Value.h"      // System::Time (Geometry::Update by name)
#include "Player/exe_bodies.h"
#include "Game/exe_bodies.h"
#include "Component/exe_bodies.h"
#include "Action/exe_bodies.h"
#include "volume/Volume.h"
#include "system/MemoryAllocator.h"
#include "object/TypeInfo.h"

// [diag] last entities committed into EditTool+0x30 (edittool_select.cpp step 6).
extern "C" void* g_LastSelectStore[4];
extern "C" int   g_LastSelectStoreIdx;

// Gated diagnostic (BANISHED_OUTLINE_TRACE) — reports where the selection-outline
// chain reaches/bails; off unless the env is set.

namespace {

// ---- exe .rdata float constants (NULL in standalone -> real-value fallback) ----
constexpr float kCenterBias = 0.5f;      // DAT_140172114 = 0x3f000000

// ---- exe helper functions, called by name ------------------------------------
//   0x14009f3e0 Reimpl_MapGetTerrainPos (Game/exe_bodies.h) -- cell -> world + height
//   0x140004680 FloorComponent_FindResData -- entity footprint rect (IBP)
//   0x140004740 InteractComponent_FindWorkplaceComponent -- entity component
//               container (IBP) (Component/exe_bodies.h)
//   0x1400c3470 ActHunt_NotifyEntityChanged_c3470 (Action/exe_bodies.h) -- flag-change
//               notify
// (0x140003f60 Entity_ClearPathQueue was resolved here and never called: FindModelComp
// below does that lookup inline. The dead reader is gone.)
// ENDGAME B.3.8 callers: DAT_1401c4410 / DAT_1401c4448 / DWORD_1401c7a50 are
// HighlightComponent / HighlightDescription / ModelComponent's type ids, read through
// the classes (the ResolveTid ti-cell + by-name cache is gone).


// The four growable locals of the outline builder: { data @+0, size @+8, cap @+0xC }.
//
// ENDGAME B.3.2 step 2: these WERE a `GrowArr` header plus `GrowTo`, a 1:1
// transcription of TradeWork_impl_sub_74a0 (0x1400074a0) generalized over element
// stride — i.e. a hand-written copy of rtcoll::SetSizeCore. Compared arm for arm
// before the swap: exact-equal early return; `count` stored before the capacity
// test; `newCount == oldCount+1 && oldCount != 0 ? newCount*3 : newCount`; one
// allocation of cap*stride through the static MemoryAllocator (fn_MemAllocStatic ==
// ?MemAlloc@MemoryAllocator@System@@SAPEAX_KH@Z, the same body rtcoll::MemAlloc
// is); copy the prefix only when the block moved; the new tail memcpy'd from the
// fill; free the old block only when it moved. Every caller appends exactly one
// element with a fill, so each local is now a real Collection::Array:
//   * the two index lists are the trait-derived Array<uint16_t> (stride 2, align 2,
//     byteCopy, init == nullptr, destroy == nullptr) — the same row GrowTo used;
//   * the two vertex lists are trait-derived over blobs at ALIGN 2, because this
//     site allocates BOTH strides with ALIGN 2 (`fn_MemAllocStatic(cap * stride,
//     2)`), which is kept exactly as the transcription had it, not re-derived
//     from retail. byteCopy == true (the arm was a memcpy of the whole stride),
//     init == nullptr (a fill is always supplied), destroy == nullptr (trivial
//     elements).
// ENDGAME B.3.2 closing pass: both vertex rows said only what a C++ type's
// traits say — whole-stride byte copy, `init == nullptr` (retail's empty else
// arm; every caller supplies a fill so the arm is never reached anyway),
// `destroy == nullptr`, no deliberately-untouched extent — so each is now the
// trait-derived `Collection::Array<T>` over a blob of the row's stride at the
// row's align. The blobs carry NO initialiser on purpose: that is what makes
// `SpecFor<T>()` derive `init == nullptr` rather than a zeroing `TInit`.
// `alignas(2)` is the transcription's own allocation alignment
// (`fn_MemAllocStatic(cap * stride, 2)`), kept exactly as it was.
struct alignas(2) TriVertex8  { uint8_t b[8];    };   // exe 0x1400074a0 idiom, stride 8
struct alignas(2) EdgeVertex16 { uint8_t b[0x10]; };  // exe 0x1400074a0 idiom, stride 0x10
static_assert(sizeof(TriVertex8)   == 8,    "TriVertex8 stride");
static_assert(alignof(TriVertex8)  == 2,    "TriVertex8 align");
static_assert(sizeof(EdgeVertex16)  == 0x10, "EdgeVertex16 stride");
static_assert(alignof(EdgeVertex16) == 2,    "EdgeVertex16 align");

using IdxArray     = Collection::Array<uint16_t>;   // EdgeMesh::Set's short indices
using TriVtxArray  = Collection::Array<TriVertex8>;
using EdgeVtxArray = Collection::Array<EdgeVertex16>;

// ===========================================================================
// thunk_helper_d250_sub_7080 (0x140007080)
//   Find the HighlightComponent (type DAT_1401c4410) in the component list at
//   entity+0x30 (array) / +0x38 (count). Writes it (or null) into *outIBP.
// ===========================================================================
// B4 PLAYER: Sub7080 (0x140007080, FindComponent<HighlightComponent>) and the
// inline ModelComponent finder are ComponentEntity::FindComponent<T>() now (the
// class static TypeId() is what both read).

// ===========================================================================
// FUN_1400b10e0 (0x1400b10e0)
//   Sample a 6-corner terrain tile from the heightfield into param_4 (18 floats:
//   6 corners x {x,y,z}). Two parity branches (checkerboard tile orientation).
//   hf fields: +0xc0 dimA, +0xc4 dimB, +0xd0 clampMax, +0xd8 height-sample table.
// ===========================================================================
void SampleTile(const WorldGeometry* hf, int col, int row, float* out) {
    const float hscale = 0.00390625f;  // DAT_1401720bc = 1/256
    int base = (hf->_gridSize - hf->_mapSize) >> 1;
    int cx = base + col;
    int cy = base + row;
    int cx1 = cx + 1;
    int cy1 = cy + 1;
    float half = (float)(hf->_gridSize >> 1);
    int dim = hf->_heightRowStride;
    float fx  = (float)cx  - half;
    float fy  = (float)cy  - half;
    float fx1 = (float)cx1 - half;
    float fy1 = (float)cy1 - half;
    int16_t* tbl = hf->_heights.data;

    auto sample = [&](int a, int b) -> float {
        int ca = a < 0 ? 0 : a; if (dim <= ca) ca = dim - 1;
        int cb = b < 0 ? 0 : b; if (dim <= cb) cb = dim - 1;
        return (float)(int)tbl[(int64_t)dim * cb + ca] * hscale;
    };

    if (((cx + cy) & 1U) == 0) {
        // parity 0: full Vector3 corners (x,y,height)
        out[0]  = fx;  out[1]  = fy;  out[2]  = sample(cx,  cy);
        out[3]  = fx;  out[4]  = fy1; out[5]  = sample(cx,  cy1);
        out[6]  = fx1; out[7]  = fy;  out[8]  = sample(cx1, cy);
        out[9]  = fx1; out[10] = fy;  out[11] = sample(cx1, cy);   // (matches decompile dup)
        out[12] = fx;  out[13] = fy1; out[14] = sample(cx,  cy1);
        out[15] = fx1; out[16] = fy1; out[17] = sample(cx1, cy1);
    } else {
        out[0]  = fx;  out[1]  = fy;  out[2]  = sample(cx,  cy);
        out[3]  = fx1; out[4]  = fy1; out[5]  = sample(cx1, cy1);
        out[6]  = fx1; out[7]  = fy;  out[8]  = sample(cx1, cy);
        out[9]  = fx;  out[10] = fy;  out[11] = sample(cx,  cy);
        out[12] = fx;  out[13] = fy1; out[14] = sample(cx,  cy1);
        out[15] = fx1; out[16] = fy1; out[17] = sample(cx1, cy1);
    }
}

// ===========================================================================
// TradeWork_impl_b440 (0x14004b440)
//   One edge "ribbon" segment between two world points v0,v1: appends 4
//   EdgeVertex (the two endpoints extruded +/-0x200) to `evtx`, then 6 triangle
//   indices {0,1,2,0,2,3} (base-offset by the pre-append vertex count) to `idx`.
// ===========================================================================
void EdgeSegment(const Math::Vector3* v0, const Math::Vector3* v1, IdxArray* idx, EdgeVtxArray* evtx) {
    // ??0TriangleVertex@EdgeMesh@Graphics@@ 0x1800a2f50 / ??0EdgeVertex@EdgeMesh@Graphics@@
    // 0x1800a2df0, called by name (B3R Leftovers; the readiness guard went with the doors).
    int16_t baseIdx = (int16_t)evtx->_size;   // sVar2 = (short)param_5[1]

    RtGraphics::TriangleVertex tvA, tvB;
    RtGraphics::EdgeVertex ev;
    // edge 1: EdgeVertex(tv(v0), tv(v1), +0x200)
    RtGraphics_EdgeMesh_TriangleVertex_Ctor(&tvA, v1);
    RtGraphics_EdgeMesh_TriangleVertex_Ctor(&tvB, v0);
    RtGraphics_EdgeMesh_EdgeVertex_Ctor(&ev, &tvB, &tvA, 0x200);
    evtx->SetSizeFill(evtx->_size + 1, &ev);
    // edge 2: same, -0x200
    RtGraphics_EdgeMesh_TriangleVertex_Ctor(&tvA, v1);
    RtGraphics_EdgeMesh_TriangleVertex_Ctor(&tvB, v0);
    RtGraphics_EdgeMesh_EdgeVertex_Ctor(&ev, &tvB, &tvA, -0x200);
    evtx->SetSizeFill(evtx->_size + 1, &ev);
    // edge 3: swapped endpoints, +0x200
    RtGraphics_EdgeMesh_TriangleVertex_Ctor(&tvA, v0);
    RtGraphics_EdgeMesh_TriangleVertex_Ctor(&tvB, v1);
    RtGraphics_EdgeMesh_EdgeVertex_Ctor(&ev, &tvB, &tvA, 0x200);
    evtx->SetSizeFill(evtx->_size + 1, &ev);
    // edge 4: swapped, -0x200
    RtGraphics_EdgeMesh_TriangleVertex_Ctor(&tvA, v0);
    RtGraphics_EdgeMesh_TriangleVertex_Ctor(&tvB, v1);
    RtGraphics_EdgeMesh_EdgeVertex_Ctor(&ev, &tvB, &tvA, -0x200);
    evtx->SetSizeFill(evtx->_size + 1, &ev);

    static const int16_t offs[6] = {0, 1, 2, 0, 2, 3};
    for (int k = 0; k < 6; k++) {
        uint16_t v = (uint16_t)(int16_t)(baseIdx + offs[k]);
        idx->SetSize(idx->_size + 1, v);
    }
}

// convenience: build a world-relative Vector3 (p - origin) into `dst`
inline void RelVec(Math::Vector3* dst, const float* p, float ox, float oy) {
    dst->x = p[0] - ox;
    dst->y = p[1] - oy;
    dst->z = p[2] - 0.0f;
}

// ===========================================================================
// TradeWork_impl_b930 (0x14004b930)
//   Rebuild the HighlightComponent's EdgeMesh (at self+0xf8): a terrain-conforming
//   outline "ribbon" around the entity footprint (loops A/B via EdgeSegment) plus
//   a filled top-face (per-tile 6-corner triangles), then EdgeMesh::Set + materials.
//   self = HighlightComponent*.
// ===========================================================================
void Impl_b930(HighlightComponent* hc) {
    Map* mapGrid = GameUpdateLoop::_singleton->m_game;

    // Entity footprint rect (MapComponent tileX0 / tileY0 / tileX1 / tileY1).
    MapComponent* rect = nullptr;
    rect = hc->_entity->FindComponent<MapComponent>();   // 0x140004680
    int rx = (int)rect->tileX0;
    int ry = (int)rect->tileY0;
    int rw = (int)rect->tileX1;
    int rh = (int)rect->tileY1;
    // Defensive (standalone): a malformed footprint rect would make the tessellation
    // loops runaway and freeze. Real building footprints are small; bail on garbage.
    if (rw <= 0 || rh <= 0 || rw > 64 || rh > 64) return;

    int g = mapGrid->size;
    // origin = terrain-world position of the footprint centre cell
    int centerIdx = g * ((rh >> 1) + ry) + (rw >> 1) + rx;
    float bias = (float)(g >> 1) - kCenterBias;
    float centerCell[3], centerWorld[3];
    centerCell[0] = (float)(centerIdx % g) - bias;
    centerCell[1] = (float)(centerIdx / g) - bias;
    centerCell[2] = 0.0f;
    mapGrid->GetTerrainPos(centerWorld, centerCell, 0, nullptr);
    float ox = centerWorld[0];
    float oy = centerWorld[1];

    // Quaternion(0,0,0,uv) rotation + centre position -> the Transform at +0x100.
    float uv = kRdata_14017214c;
    Math::Quaternion quat(0.0f, 0.0f, 0.0f, uv);
    hc->transform.rotation = quat;            // +0x100 (16 bytes)
    hc->transform.translation.x = ox;         // +0x110
    hc->transform.translation.y = oy;         // +0x114
    hc->transform.translation.z = 0.0f;       // +0x118 (retail's dword 0)

    IdxArray     edgeIdx;   // local_208 / local_200  (short indices)
    EdgeVtxArray edgeVtx;   // local_218 / local_210  (16-byte EdgeVertex)

    auto cellToRel = [&](int idx, Math::Vector3* rel) {
        float cell[3], world[3];
        cell[0] = (float)(idx % g) - (float)(g >> 1);
        cell[1] = (float)(idx / g) - (float)(g >> 1);
        cell[2] = 0.0f;
        mapGrid->GetTerrainPos(world, cell, 0, nullptr);
        RelVec(rel, world, ox, oy);
    };

    // Loop A — top & bottom edges (iterate columns).
    for (int col = rx, n = 0; n < rw; n++, col++) {
        Vector3Slot p1, p2, p3, p4;   // B.4 S4 GFX2: unconstructed Vector3s (RelVec writes x, y, z)
        cellToRel(g * ry + (col + 1), &p1.v);       // top, col+1
        cellToRel(g * ry + col,       &p2.v);       // top, col
        EdgeSegment(&p2.v, &p1.v, &edgeIdx, &edgeVtx);
        cellToRel(g * (ry + rh) + (col + 1), &p3.v); // bottom, col+1
        cellToRel(g * (ry + rh) + col,       &p4.v); // bottom, col
        EdgeSegment(&p4.v, &p3.v, &edgeIdx, &edgeVtx);
    }
    // Loop B — left & right edges (iterate rows).
    for (int row = ry, n = 0; n < rh; n++, row++) {
        Vector3Slot p1, p2, p3, p4;   // B.4 S4 GFX2: unconstructed Vector3s (RelVec writes x, y, z)
        cellToRel(g * (row + 1) + rx, &p1.v);        // left, row+1
        cellToRel(g * row + rx,       &p2.v);        // left, row
        EdgeSegment(&p2.v, &p1.v, &edgeIdx, &edgeVtx);
        cellToRel(g * (row + 1) + rx + rw, &p3.v);   // right, row+1
        cellToRel(g * row + rx + rw,       &p4.v);   // right, row
        EdgeSegment(&p4.v, &p3.v, &edgeIdx, &edgeVtx);
    }

    // Fill section — per-tile top-face triangles from the heightfield.
    const WorldGeometry* hf = GameUpdateLoop::_singleton->m_worldGeometry;
    IdxArray    fillIdx;   // local_1f0 / local_1e8 (short indices)
    TriVtxArray fillVtx;   // local_268 / local_260 (8-byte TriangleVertex)
    struct Box box;
    RtVolume_Box_Ctor(&box);

    int bottomRow = ry + rh;
    int rightCol  = rx + rw;
    for (int row = ry; row < bottomRow; row++) {
        for (int col = rx; col < rightCol; col++) {
            float scratch[18];
            SampleTile(hf, col, row, scratch);
            const float* pf = &scratch[1];      // pfVar23 = local_c4
            for (int corner = 0; corner < 6; corner++) {
                // 1. append index = current vertex count (before this vertex)
                uint16_t idxVal = (uint16_t)(int16_t)fillVtx._size;
                fillIdx.SetSize(fillIdx._size + 1, idxVal);
                // 2. build corner Vector3 + TriangleVertex (8 bytes)
                Math::Vector3 cv(pf[-1] - ox, pf[0] - oy, pf[1] - 0.0f);
                RtGraphics::TriangleVertex tvtx;
                RtGraphics_EdgeMesh_TriangleVertex_Ctor(&tvtx, &cv);   // 0x1800a2f50
                // 3. append the TriangleVertex
                fillVtx.SetSizeFill(fillVtx._size + 1, &tvtx);
                // 4. extend bounding box by the corner
                {
                    struct Box tmp;
                    RtVolume_Box_Extend(&box, &tmp, &cv);
                    RtVolume_Box_Assign(&box, &tmp);
                }
                pf += 3;
            }
        }
    }

    // Release the previous EdgeMesh (vtable slot 0 = scalar deleting dtor, arg 1 = free).
    Graphics::EdgeMesh* oldMesh = hc->outlineMesh;
    if (oldMesh) {
        delete oldMesh;   // slot 0, free
        hc->outlineMesh = nullptr;
    }

    // Allocate + construct a fresh EdgeMesh.
    Graphics::EdgeMesh* mesh = static_cast<Graphics::EdgeMesh*>(System::MemoryAllocator::MemAlloc(0x180, 8));
    if (mesh) {
        ::new (mesh) Graphics::EdgeMesh();   // B.3.9 GFX: the real ctor (was the ??0 door)
    }
    hc->outlineMesh = mesh;

    // Two GraphicsMesh::TriangleList descriptors { pad, vtxCount@+4, pad, idxCount@+0xc, pad, pad }.
    Graphics::GraphicsMesh::TriangleList triList0 = {0, fillVtx._size, 0, fillIdx._size, 0, 0};
    Graphics::GraphicsMesh::TriangleList triList1 = {0, edgeVtx._size, 0, edgeIdx._size, 0, 0};

    // ?Set@EdgeMesh@Graphics@@QEAA_NHPEBUTriangleList@GraphicsMesh@2@
    // H0HPEAGHPEAXHH1H2HH1H2HH1H2HAEBVBox@Volume@@@Z (0x1800a23e0). The member was
    // deliberately undeclared until the closing pass -- Mesh.h now carries the
    // 25-slot list tokenised out of the mangled name and the body lives on the
    // member. THE ARGUMENTS ARE UNCHANGED, POSITION FOR POSITION: this call site
    // groups them (count, data, vcount, vdata, stride) x4, and the mangled name
    // groups them 6/5/5/4 with each group's trailing int belonging to the NEXT
    // pair; the two spellings describe the same 25 slots and the same bytes, which
    // is why the door's 25 arguments move over verbatim. `box` was a `const void*`
    // at the door and is `const ::Box&` on the member, as the mangled name says.
    // `if (mesh)` is an OBJECT test and stays; only the `fn_EdgeMesh_Set` readiness
    // term came out.
    if (mesh) {
        mesh->Set(
            1, &triList0,
            1, &triList1,
            // fill stream: short indices + 8-byte TriangleVertex, stride 8
            fillIdx._size, fillIdx._data,
            fillVtx._size, fillVtx._data, 8,
            0, nullptr, 0, nullptr, 0,
            // edge stream: short indices + 16-byte EdgeVertex, stride 0x10
            edgeIdx._size, edgeIdx._data,
            edgeVtx._size, edgeVtx._data, 0x10,
            0, nullptr, 0, nullptr, 0,
            box);
    }

    // Materials from the HighlightDescription (self+0x18): +0x30, +0x38 (yellow).
    Description* obj = hc->_description;
    const uint32_t tidDesc = HighlightDescription::TypeId();
    HighlightDescription* desc = Object::IsDerivedFromNullSafe(obj, tidDesc)
        ? static_cast<HighlightDescription*>(obj) : nullptr;
    // the four parameters are `Object::ExternalPtr<MaterialInstance>`
    // BY VALUE, and constructing them from the raw pointers the HighlightDescription
    // holds adds NO refcount traffic the door did not do: `ExternalPtr(T*)` is a bare
    // `_ptr = p` store and its copy/assign/dtor are `= default` and MUST stay trivial
    // (External.h's banner). The door's own body is four of these same constructions,
    // so this reproduces it exactly. Argument order is (mask, edge, skinMask,
    // skinEdge) — see Mesh.h's note; the fields are grouped differently.
    if (mesh && desc) {
        Graphics::MaterialInstance* m0 = desc->_maskMaterial;
        Graphics::MaterialInstance* m1 = desc->_edgeMaterial;
        hc->outlineMesh
            ->SetMaterials(Object::ExternalPtr<Graphics::MaterialInstance>(m0),
                           Object::ExternalPtr<Graphics::MaterialInstance>(m1),
                           Object::ExternalPtr<Graphics::MaterialInstance>(m0),
                           Object::ExternalPtr<Graphics::MaterialInstance>(m1));
    }

    // fillIdx / fillVtx / edgeIdx / edgeVtx are released by ~Array at scope exit —
    // exactly one destruction per path: the function's only early return precedes
    // their declarations, and EdgeMesh::Set copies the streams (the buffers were
    // freed right here before the conversion, so ownership never left this frame).
}

}  // namespace (HighlightComponent::Repaint is a member; the anonymous namespace resumes after it)

// ===========================================================================
// Entity_impl_c4a0 (0x14004c4a0)
//   Rebuild the HighlightComponent's EdgeMeshInstance and (re)add it to the live
//   Graphics::Scene (gameWorld+0x60). Removes the old scene object first, decides
//   between the built EdgeMesh (self+0xf8, when no per-cell mesh applies) and a
//   per-cell prebuilt mesh from the description table. self = HighlightComponent*.
// ===========================================================================
void HighlightComponent::Repaint() {
    Graphics::Scene* scene = GameUpdateLoop::_singleton->m_graphicsScene;
    // 0x14004c4ad: retail reads +0x120 first; the embedded EdgeMeshInstance is
    // the ctor's (0x14004c930), so there is no "never built" arm here.
    Graphics::EdgeMeshInstance& inst = edgeMeshInstance;

    // If we already have a scene object, remove it and clear the "in-scene" flag.
    if (highlightId >= 0) {
        scene->RemoveObject(static_cast<uint32_t>(highlightId));
        uint32_t f0 = _flags;
        uint32_t f1 = f0 & 0xfffffffb;
        _flags = f1;
        if (((f1 ^ f0) & 0xffff) != 0 && _entity != nullptr) {
            _entity.Get()->RefreshEntityFlags();   // 0x1400c3470
        }
    }
    // Release the built EdgeMesh (vtable slot 0).
    {
        Graphics::EdgeMesh* m = outlineMesh;
        highlightId = -1;
        if (m) {
            delete m;   // slot 0, free
            outlineMesh = nullptr;
        }
    }
    if (highlightFlag == 0) return;   // highlight not active

    // Look up per-cell prebuilt mesh indices (charComp +0x164/+0x166) + the
    // "any neighbour already highlighting" test over sibling components.
    ModelComponent* charComp = _entity->FindComponent<ModelComponent>();              // 0x140003f60
    MultiObjectComponent* siblings = _entity->FindComponent<MultiObjectComponent>();  // 0x140004740

    Description* ownerObj = _description;
    const uint32_t tidDesc = HighlightDescription::TypeId();
    HighlightDescription* desc = Object::IsDerivedFromNullSafe(ownerObj, tidDesc)
        ? static_cast<HighlightDescription*>(ownerObj) : nullptr;

    int iVar10 = -1, iVar11 = -1;
    if (charComp != nullptr) {
        iVar10 = (int)charComp->meshGroup;
        iVar11 = (int)charComp->meshIndex;
    }

    bool neighbourActive = false;
    if (siblings != nullptr) {
        int cnt = siblings->children._size;
        if (cnt > 0) {
            int i = 0;
            do {
                ComponentEntity* sib = siblings->children[i].Get();
                HighlightComponent* sibHi = sib->FindComponent<HighlightComponent>();   // 0x140007080
                if (sibHi != nullptr) {
                    bool active = !(sibHi->highlightFlag == 0 ||
                                    sibHi->highlightId < 0 ||
                                    sibHi->outlineMesh != nullptr);
                    neighbourActive = neighbourActive || active;
                }
                i++;
            } while (i < siblings->children._size);
            if (neighbourActive) goto add_prebuilt;
        }
    }

    // No per-cell mesh (both indices < 0) -> build+submit our own EdgeMesh.
    if (iVar10 < 0 || iVar11 < 0) {
        MapComponent* r = _entity->FindComponent<MapComponent>();   // 0x140004680
        if (r && desc && desc->_maskMaterial != nullptr) {
            Impl_b930(this);
            inst.SetMesh(Object::ExternalPtr<Graphics::EdgeMesh>(outlineMesh),
                         nullptr);
            inst.SetTransform(transform);
            System::Time time;
            RtSystem_Time_Ctor(&time, 0.0f);
            inst.Update(time);   // slot 1
            highlightId = static_cast<int32_t>(scene->AddObject(&inst));
            return;
        }
    }

add_prebuilt:
    // Per-variant prebuilt mesh path (how retail outlines citizens / model-variant
    // entities): mesh = descTable[variantIdx][modelVariant], transform from the
    // ModelComponent (charIBP+0x28). descTable = HighlightDescription+0x20.
    if (iVar10 >= 0 && iVar11 >= 0 && desc) {
        HighlightGroup* table = desc->_groups;
        // Standalone guard: if the HighlightDescription's prebuilt-mesh table (or the
        // selected row/entry) never loaded, bail instead of dereferencing null / OOB.
        // Retail guarantees the table is populated + consistent with the model's
        // variant range; a fresh/inert description in SA may leave it empty.
        Graphics::EdgeMesh** row = table ? table[iVar10].refs : nullptr;
        Object::External* cellMesh = row ? row[iVar11] : nullptr;
        if (cellMesh == nullptr) return;   // nothing loaded to draw

        // The third SetMesh argument is a SOURCE MeshInstance (mangled name:
        // ...@@PEAVMeshInstance@2@@Z) — the outline follows its node transforms. It is
        // stored at EdgeMeshInstance+0xc8, and Graphics::EdgeMeshInstance::Draw
        // (Runtime 0x1800a3150) does this with it:
        //
        //   1800a33c7  MOV RCX,[RSI + 0xc8]     ; the source instance
        //   1800a33d9  JZ  0x1800a3469          ; null -> skip, draw without node follow
        //   1800a33e4  CALL GetNodeToWorld      ; else: source->nodes[idx]
        //
        // The SOURCE is null-checked; its NODE ARRAY (MeshInstance+0xf0) is not.
        // MeshInstance::GetNodeToWorld computes `nodeIdx * 0x38 + *(this + 0xf0)` and
        // hands it straight to Math::Transform::Lerp, so a source instance that never
        // got a mesh gives Lerp a null Scalar* and faults on `MOVSS XMM2,[R8+0x10]` —
        // READ of 0x10, which is exactly the manual-play crash recorded in
        // docs/history/notes/bridge_dialog_crash.log (RCX=0x1c = 0+0x1c, R8=0).
        //
        // Retail can rely on the invariant "a ModelComponent's MeshInstance always has
        // its mesh"; standalone cannot. NOT because ModelComponent is inert — it has a
        // real factory and its Startup (vtable +0x20) does run, measured 2026-07-26 with
        // BANISHED_MODEL_TRACE=1. A CONSTRUCTION SITE's ModelDescription carries
        // modelVariant = -1 (+0x34), and retail's Startup deliberately skips SetMesh for
        // that: the frame is set later by the state-8 ramp (BC8_SetMeshFrame). A bridge
        // never reaches state 8 (delivered=0 required=24), so its segments keep an empty
        // MeshInstance forever. Passing nullptr instead just takes the
        // no-source branch above — the outline still draws, it simply does not follow
        // animated node transforms.
        //
        // STILL LOAD-BEARING (verified 2026-07-27): wiring BuildComponent's slot 6
        // (BANISHED_LIVE_BUILD_ONADDED) does NOT remove this state. Slot 6 collects the
        // segments and stamps them build-controlled, but never calls SetMesh — measured
        // 3/3 bridge segments still at +0xf0 == null after it runs. See
        // docs/history/notes/BRIDGE_HIGHLIGHT_DIALOG_BRIEF.md, "the slot-6 verification pass".
        Graphics::MeshInstance* srcInst = &charComp->meshInstance;
        if (srcInst->_nodes._data == nullptr) {
            srcInst = nullptr;
        }
        inst.SetMesh(Object::ExternalPtr<Graphics::EdgeMesh>(static_cast<Graphics::EdgeMesh*>(cellMesh)), srcInst);   // the table holds the EdgeMesh resources

        // The source instance is not only what the outline FOLLOWS, it is what POSITIONS
        // it: retail's PREBUILT branch sets no transform of its own because
        // GetNodeToWorld supplies one. Dropping the source therefore leaves the
        // EdgeMeshInstance on the identity transform its ctor gave it, and every
        // outlined piece renders at the world origin instead of on the entity — observed
        // by hand 2026-07-26 as "imaginary pieces offset from the main bridge" when
        // clicking a bridge FOUNDATION.
        //
        // Place it from the model's own world Transform (ModelComponent+0x148, the
        // 0x20-byte Math::Transform that BridgeTool::CalcPath writes per segment). Same
        // call the BUILD branch above makes with the HighlightComponent's +0x100.
        if (srcInst == nullptr) {
            inst.SetTransform(charComp->transform);
        }
        System::Time time;
        RtSystem_Time_Ctor(&time, 0.0f);
        inst.Update(time);   // slot 1
        uint32_t id = scene->AddObject(&inst);
        uint32_t f0 = _flags;
        highlightId = static_cast<int32_t>(id);
        uint32_t f1 = f0 | 4;
        _flags = f1;
        if (((f1 ^ f0) & 0xffff) != 0 && _entity != nullptr) {
            _entity.Get()->RefreshEntityFlags();   // 0x1400c3470
        }
    }
}

namespace {

// ===========================================================================
// Entity_impl_cb40 (0x1400fcb40) — clear highlight on all of the entity's
//   HighlightComponents (set +0x11c = 0, then repaint via c4a0). self = EditTool*.
// ===========================================================================
// [tripwire] cb40 dereferences self+0x30 as the selected ENTITY. That is only true
// when `self` is an EditTool: on every DragTool-derived tool (FencedTool, RoadTool,
// RemovalTool, BridgeTool, DragPlacementTool) the same offset is the endCell /
// lastEndCell int pair, so an idle build tool reads as the pointer 0xffffffff'ffffffff
// and a mid-drag one as e.g. 0xffffffff00008263 — measured 2026-08-19, the AV then
// lands in the 0x140004740 component finder reading entity+0x38. Root cause was the
// CALLERS handing over whatever tool sat at EntityUI+0x28 (fixed: EditTool_IsEditTool /
// EditTool_HarnessAcquire in edittool.cpp, plus the ToolUI close-button gate). These two
// reporters stay as a tripwire — nothing should reach them now, and if something does,
// the line names the tool class rather than leaving an unexplained AV.
void ReportBadSelectionRef(int64_t block, int64_t ref) {
    static int budget = 8;
    if (budget <= 0) return;
    --budget;
    void** vt = *reinterpret_cast<void***>(block);   // vtable probe (the tripwire reads a possibly-recycled block's vptr)  // CAST-OK: crash_walk
    // Print the raw facts FIRST: if `block` is a freed/recycled block, the virtual
    // GetTypeId below can itself fault, and a dump that never printed proves nothing.
    std::fprintf(stderr, "[HighlightClear] BAD selection ref: tool=%p vtbl=%p self+0x30=0x%llx\n",
                 reinterpret_cast<void*>(block), reinterpret_cast<void*>(vt),
                 static_cast<unsigned long long>(ref));
    const uint64_t* w = reinterpret_cast<const uint64_t*>(block);   // crash_walk raw dump of the tool block  // CAST-OK: crash_walk
    std::fprintf(stderr, "[HighlightClear]   tool[0x00..0x38] = %016llx %016llx %016llx "
                 "%016llx %016llx %016llx %016llx %016llx\n",
                 (unsigned long long)w[0], (unsigned long long)w[1], (unsigned long long)w[2],
                 (unsigned long long)w[3], (unsigned long long)w[4], (unsigned long long)w[5],
                 (unsigned long long)w[6], (unsigned long long)w[7]);
    std::fprintf(stderr, "[HighlightClear]   last 4 entities stored into EditTool+0x30: "
                 "%p %p %p %p (newest first)\n",
                 g_LastSelectStore[(g_LastSelectStoreIdx + 3) & 3],
                 g_LastSelectStore[(g_LastSelectStoreIdx + 2) & 3],
                 g_LastSelectStore[(g_LastSelectStoreIdx + 1) & 3],
                 g_LastSelectStore[g_LastSelectStoreIdx & 3]);
    std::fflush(stderr);
    // Only now the virtual probe (a live EditTool answers; a recycled block may not).
    // (Tripwire probe of possibly-recycled memory: a null vptr, or a null Type()
    // entry in the table it names, skips the call; the slot comes from the member.)
    // `block` stays an integer: as an EditTool* (a final class) GCC devirtualises
    // Type() to EditTool's own id, and this tripwire exists to ask a possibly-recycled
    // block's REAL table (B11, X-G1-3b).
    if (vt && ::Object::SlotAddressOf(reinterpret_cast<void*>(block), &::Object::Base::Type)) {   // vtable probe: `block` stays an integer, which keeps GCC from devirtualising Type() (as an EditTool*, a final class, it would)  // CAST-OK: crash_walk
        uint32_t tid = reinterpret_cast< ::Object::Base*>(block)->Type();   // slot 2  // vtable probe: `block` stays an integer, which keeps GCC from devirtualising Type() (as an EditTool*, a final class, it would)  // CAST-OK: crash_walk
        const char* n = ::Object::GetTypeNameById(tid);
        std::fprintf(stderr, "[HighlightClear]   tool typeId=%u ('%s')\n", tid, n ? n : "?");
        std::fflush(stderr);
    }
}

// [diag] companion of ReportBadSelectionRef for the other failure shape: the stored
// entity pointer is canonical but the object behind it is gone (freed and recycled),
// so the component array/count the finder reads are nonsense.
void ReportDeadSelectedEntity(EditTool* self, const ComponentEntity* ent, uint64_t arr, int32_t cnt) {
    static int budget = 8;
    if (budget <= 0) return;
    --budget;
    std::fprintf(stderr, "[HighlightClear] DEAD selected entity: tool=%p entity=%p "
                 "entity+0x30(compArray)=0x%llx entity+0x38(count)=%d entity+0x40(flags)=0x%x\n",
                 static_cast<void*>(self), static_cast<const void*>(ent),
                 static_cast<unsigned long long>(arr), cnt,
                 ent->_entityFlags);
    std::fprintf(stderr, "[HighlightClear]   last 4 entities stored into EditTool+0x30: "
                 "%p %p %p %p (newest first)\n",
                 g_LastSelectStore[(g_LastSelectStoreIdx + 3) & 3],
                 g_LastSelectStore[(g_LastSelectStoreIdx + 2) & 3],
                 g_LastSelectStore[(g_LastSelectStoreIdx + 1) & 3],
                 g_LastSelectStore[g_LastSelectStoreIdx & 3]);
    std::fflush(stderr);
}

// Set every HighlightComponent of the selected entity and of its MultiObject
// children to `flag`, repainting the ones that change (the shared body of cb40 /
// c9e0 after their own preamble).
void SetSelectionHighlight(ComponentEntity* selected, uint8_t flag) {
    MultiObjectComponent* cont = selected->FindComponent<MultiObjectComponent>();   // 0x140004740
    if (cont != nullptr) {
        int cnt = cont->children._size;
        for (int i = 0; i < cnt; i++) {
            ComponentEntity* sib = cont->children[i].Get();
            HighlightComponent* hi = sib->FindComponent<HighlightComponent>();   // 0x140007080
            if (hi != nullptr) {
                if (hi->highlightFlag != flag) {
                    hi->highlightFlag = flag;
                    hi->Repaint();
                }
            }
            cnt = cont->children._size;
        }
    }
    HighlightComponent* hi2 = selected->FindComponent<HighlightComponent>();   // 0x140007080
    if (hi2 != nullptr) {
        if (hi2->highlightFlag != flag) {
            hi2->highlightFlag = flag;
            hi2->Repaint();
        }
    }
}

}  // namespace

void EditTool::HighlightClear() {
    if (lastEntityRef == nullptr) return;
    {
        const uint64_t ref = reinterpret_cast<uint64_t>(lastEntityRef);   // CAST-OK: ptr_int (the tree's tripwire range probe)
        if (ref < 0x10000 || ref > 0x00007FFFFFFFFFFFULL) {
            ReportBadSelectionRef(reinterpret_cast<int64_t>(this), (int64_t)ref);   // LEFT: FLOOR? vtable probe: ReportBadSelectionRef keeps the tool as an integer so GCC cannot devirtualise its Type() probe (EditTool is final); S4 ruling: no marker (FLOOR section 4 bars a marker on a this / self operand), the cast stays listed
        } else {
            // The pointer itself can look fine while the ENTITY behind it is freed and
            // its allocation recycled — that is the other shape the reported crash can
            // take, since the finder faults on entity+0x30 (component array) rather
            // than on the entity pointer. Screen the two fields the finder reads.
            const ComponentEntity* e = lastEntityRef;
            const uint64_t arr = reinterpret_cast<uint64_t>(e->_components._data);   // CAST-OK: ptr_int
            const int32_t  cnt = e->_components._size;
            const bool badArr = (cnt != 0) && (arr < 0x10000 || arr > 0x00007FFFFFFFFFFFULL);
            if (badArr || cnt < 0 || cnt > 64) ReportDeadSelectedEntity(this, e, arr, cnt);
        }
    }
    SetSelectionHighlight(lastEntityRef, 0);
}

// ===========================================================================
// Entity_impl_c9e0 (0x1400fc9e0) — select finalize: cb40 (clear), then set the
//   highlight (+0x11c = 1) on the entity's HighlightComponents and repaint.
//   self = EditTool*.
// ===========================================================================
void EditTool::HighlightSelect() {
    HighlightClear();
    if (lastEntityRef == nullptr) return;
    SetSelectionHighlight(lastEntityRef, 1);
}

namespace {

// ===========================================================================
// Entity_impl_cc90 (0x1400fcc90) — DESELECT.
//
// Retail cc90 does THREE things: (1) clear the highlight on the currently-
// selected entity (cb40, reads self+0x30), (2) null the tool's selected-dialog
// (self+0x28) and selected-entity (self+0x30) refs, and (3) close the open
// DialogInstance if its controller derives from ToolUI (DWORD_1401c5788) and
// isn't "pinned" (controller+0x40 == 0). d3b0 (EditTool::SelectEntity) calls it
// BEFORE overwriting self+0x30 with the newly-clicked entity, so cb40 clears the
// PREVIOUS entity's outline — which is exactly the "clear on deselect" fix.
//
// *** STANDALONE: we do ONLY step (1), the highlight-clear. ***
// Steps (2)+(3) — nulling self+0x30 and calling UI::CloseDialog — regress the SA
// select path. Our EditTool picking handler (edittool.cpp:629) re-fires d3b0 as
// the cursor/entities move and guards redundant selects with
// `self->lastEntityRef(+0x30) != foundEntity`. Nulling +0x30 defeats that guard,
// and CloseDialog re-enters the deselect path (dialog OnRemove -> EditTool
// deactivate vtable slot 0x1401ee710 -> cc90) while +0x30 is null — so the
// just-opened info panel gets torn down every frame (observed: citizen dialog
// never stays up; [Outline] trace shows cc90->c9e0 looping on one entity). In SA
// the dialog lifecycle is owned by d3b0/OnKey (step 4 reuses/replaces the panel),
// so the retail teardown here is both redundant and harmful. Keeping just cb40
// gives the outline-clear without disturbing the panel. (This body is reached via
// the registered 0x1400fcc90 under BANISHED_SELECTION_OUTLINE; the original exe cc90
// runs the full teardown unmodified.)
// ===========================================================================
// B4 P2 PLAYER: the body (`Impl_cb40(self)`, nothing else) and its forwarder
// HighlightDeselect_Impl had no caller anywhere and are deleted; the deselect paths
// call HighlightClear_Impl (0x1400fcb40) directly.

// ===========================================================================
// HighlightFollow — per-frame transform re-sync so a MOVING selected entity's
//   outline TRACKS it (walking citizens). self = EditTool* (its +0x30 = the
//   currently-selected entity, set by d3b0/edittool_select.cpp:258).
//
// NOT a retail function. Retail relies on the Graphics::Scene ticking
// EdgeMeshInstance::Update (vtable slot [1]) on every scene geometry each frame;
// that Update re-reads the transform from the instance's bound MeshInstance
// (EdgeMeshInstance+0xc8, set by SetMesh's 3rd arg = the ModelComponent's
// MeshInstance at ModelComp+0x28) at +0x84 and Geometry::SetTransform's it — so
// the outline stays glued to the model. In standalone the Graphics::Scene is a
// passive render container: geometry is repositioned EXPLICITLY per frame (the
// citizen move-tick calls Geometry::SetTransform on its own MeshInstance, see
// citizenai_executor.cpp:582). c4a0 calls EdgeMeshInstance::Update only ONCE at
// select, so the outline freezes at the click location while the citizen walks
// away — its skinning still animates because the bone palette lives in the
// shared MeshInstance, but the world transform never re-syncs. This re-runs that
// same Update every frame over the selected entity's active HighlightComponent,
// so the outline follows.
//
// For BUILDING highlights (the c4a0 "build" branch) the instance's bound
// MeshInstance (+0xc8) is null — SetMesh's 3rd arg was 0 — so
// EdgeMeshInstance::Update early-outs; buildings don't move, so this is a
// harmless no-op for them.
// ===========================================================================
void Impl_HighlightFollow(EditTool* self) {
    if (self->lastEntityRef == nullptr) return;      // nothing selected
    const uint64_t entityBits = reinterpret_cast<uint64_t>(self->lastEntityRef);   // CAST-OK: ptr_int (the tree's dead-reference probe)
    // Dead / non-canonical ref (a torn-down entity leaves the -1 sentinel) — the
    // finder would deref it. Treat as "nothing to follow" (mirrors edittool.cpp:328).
    if (entityBits < 0x10000 || entityBits > 0x00007FFFFFFFFFFFULL) return;
    ComponentEntity* entity = self->lastEntityRef;

    HighlightComponent* hc = entity->FindComponent<HighlightComponent>();   // find HighlightComponent
    if (hc == nullptr) return;
    if (hc->highlightFlag == 0) return;          // highlight not active
    if (hc->highlightId < 0) return;             // not currently in the scene

    // --- Rebuild the outline when the selected model's MESH VARIANT changes ---------
    // The building branch of c4a0 bakes an EdgeMesh from the ModelComponent's variant
    // indices (+0x164 style, +0x166 damage stage) ONCE, at select time, and the
    // per-frame Update below only re-syncs the TRANSFORM. Nothing rebuilt the geometry,
    // so a building selected before a fire kept its intact silhouette all the way down
    // the rubble ladder — the collapse step (0x14011d5d0) drives +0x166 DOWN 2->1->0 and
    // the outline never noticed (user-reported 2026-08-23). c4a0 is idempotent: it drops
    // the scene object, releases the built EdgeMesh (+0xf8) and re-derives both from the
    // component's CURRENT variant, so re-running it on change is the whole fix.
    // NOT a retail concern: retail's Graphics::Scene rebuilds highlight geometry as part
    // of its own tick; our scene is a passive render container (see the note above).
    {
        ModelComponent* mdl = entity->FindComponent<ModelComponent>();
        if (mdl) {
            uint32_t key =
                (static_cast<uint32_t>(static_cast<uint16_t>(mdl->meshGroup)) << 16) |
                 static_cast<uint32_t>(static_cast<uint16_t>(mdl->meshIndex));
            // One selection is active at a time (EditTool+0x30), so a single-slot
            // cache keyed on the HighlightComponent is enough; re-arm on a new one.
            static const HighlightComponent* s_lastHi = nullptr;
            static uint32_t s_lastKey = 0;
            if (hc != s_lastHi) {
                s_lastHi = hc;
                s_lastKey = key;
            } else if (key != s_lastKey) {
                s_lastKey = key;
                hc->Repaint();
                return;   // rebuilt this frame; Update re-syncs on the next one
            }
        }
    }

    System::Time time;
    RtSystem_Time_Ctor(&time, 0.0f);
    // vtable slot [1] = EdgeMeshInstance::Update(time): pulls the current transform
    // from the bound MeshInstance (+0xc8 -> +0x84) and Geometry::SetTransform's it.
    hc->edgeMeshInstance.Update(time);   // slot 1

}

}  // namespace

// ===========================================================================
// Exported entry points (registered in standalone/reg_player_render.cpp when
// BANISHED_SELECTION_OUTLINE is set). c9e0/cc90 are what edittool_select.cpp
// resolves: c9e0 sets+paints the outline on the newly-selected entity, cc90
// clears the previous entity's outline and closes its dialog (deselect).
// ===========================================================================
// B4 FORWARD: their callers (Dialog/edittool_select.cpp, Player/edittool.cpp,
// Player/pathtool_setselected.cpp, ...) hand the tool / component over as void*.

// Per-frame follow re-sync (NOT an exe address). Called directly from
// EditTool::Update (edittool.cpp) every frame; self-gates on
// BANISHED_SELECTION_OUTLINE so it is a no-op unless the outline is enabled.
extern "C" void HighlightFollow_Impl(EditTool* self) { Impl_HighlightFollow(self); }
