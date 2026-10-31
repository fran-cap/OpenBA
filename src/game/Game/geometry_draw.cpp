// src/game/Game/geometry_draw.cpp
//
// Real implementations of the 9 vtable slots at exe 0x14015f110 ("derived
// Graphics::Geometry" / TerrainGenerator). Decompiled from the original exe.
//
// ENDGAME B.3.8 block LOOP: these are the members of the REAL class
// `WorldGeometry` (src/game/Game/WorldGeometry.h) now, with a compiler vtable.
// Gone: the hand-built 9-slot `g_Geometry_Vtable_Standalone` (serialize_arrays.cpp)
// and the five GeomDraw_* forwarders for slots 4..8 — the class inherits
// Graphics::Geometry's, which is exactly what they called.
//
// Vtable layout (verified via Ghidra read of 0x14015f110):
//   +0x00  DeletingDtor                (exe 0x14001ECC0)  — see below
//   +0x08  Update (chunk rebuild)      (exe 0x1400AED40 → JMP 0x1400AAD50)
//   +0x10  Draw (per item)             (exe 0x1400AEA10)  per-item GPU draw
//   +0x18  AddDrawItems                (exe 0x1400AE490)  frustum cull + AddDrawItem
//   +0x20  inherited UpdateAttached    (Runtime export)
//   +0x28  inherited GetSubTransform   (Runtime export, hidden retval)
//   +0x30  inherited GetInstanceTransform (Runtime export, hidden retval)
//   +0x38  inherited SetSharedInstanceData (Runtime export)
//   +0x40  inherited Clean             (Runtime export)
//

// ENDGAME B.3.5 batch K1: the runtime type headers come FIRST — they reach
// src/engine/volume/Volume.h, whose global-scope `struct Rectangle` is hidden by
// <wingdi.h>'s `Rectangle()` once <windows.h> has been seen through resolve.h.
#include "Game/WorldGeometry.h"
#include "Game/WorldGeometryElements.h"
#include "graphics/Geometry.h"
#include "graphics/Camera.h"     // Graphics::Camera, Graphics::Renderer
#include "graphics/Material.h"   // Graphics::MaterialInstance
#include "graphics/InterpolatedState.h"   // ENDGAME B.3.5 closing pass --
                                          // Graphics::InterpolatedState<Transform>
#include "video/Context.h"
#include "video/ImageBuffer.h"   // B.3.x W0: ~WorldGeometry deletes _heightImage by name
#include "system/Value.h"
#include "object/External.h"
#include "Game/exe_bodies.h"
#include "Work/exe_bodies.h"      // WorkBase_TreeNodeFree_14c30 (0x140014c30)
#include "math/Math.h"
#include "volume/Volume.h"        // RtVolume_Frustum_MulTransform3
#include "volume/frustum_cull.h"  // CULLROUTE: the prepared table (Contains, retail 0x1800f8e80)
#include "Game/exe_constants.h"   // kRdata_*
#include "ext/render_culling/render_culling.h"
#include "ext/terrain_batch/terrain_batch.h"   // ext.terrain_batch: the row merge + the shadow chunk cull
#include "ext/better_water/better_water.h"     // ext.better_water: the shore pass after a water range
#include "object/Internal.h"   // Object::detail::InternalMemFree
#include <atomic>
#include <cstdint>
#include <new>
#include <cstdio>
#include <cstring>
#include <cstdlib>

// ============================================================
// Exe-resident constants (read once and embedded — these are float
// literals in the original exe .rdata, accessed via PC-relative loads).
// ============================================================
//
// 0x14017228c → bytes 00 00 80 c1 = -16.0f  (chunk Z-min)
// 0x1401721d0 → bytes 00 00 00 42 =  32.0f  (chunk Z-max delta)
//
// In standalone these addresses live in our own PE which doesn't carry the
// original exe's .rdata, so we embed the values as constexpr.
namespace {
constexpr float kChunkZMin   = -16.0f;  // exe 0x14017228c
constexpr float kChunkZRange =  32.0f;  // exe 0x1401721d0

// One-shot log latches (namespace scope, atomic: B6-5).
std::atomic<bool> s_collectLogged{false};
std::atomic<bool> s_itemDrawLogged{false};
}  // namespace

// EXT SEAM (NOT RETAIL, docs/extensions/handins/TERRAINBATCH.md): null unless ext.terrain_batch
// is registered for this run (registry_boot.cpp); then the row's live bool.
const bool* WorldGeometry::_extTerrainBatch = nullptr;


// ============================================================
// Slot +0x00 — destructor (B38 D9 #12)
// ============================================================
//
// +0x1F0 / +0x200 (0x1400aedd3..0x1400aee28): 0x14001fb80 over the elements
// (retail passes the header's low dword as the count), then the inlined ~Array:
// GeometryBatchArray's own destructor (kSpec_GeometryBatch, B.4 H5-P2).

// +0xE8 (0x1400aeee0..0x1400aef18): free both subtrees, then the root node;
// root = 0, the dword count = 0.
DirtyChunkSet::~DirtyChunkSet() {
    if (AvlNode28* top = root) {
        WorkBase_TreeNodeFree(&root, top->left);    // +0x10
        WorkBase_TreeNodeFree(&root, top->right);   // +0x18
        Object::detail::InternalMemFree(top);
    }
    root = nullptr;
    count = 0;
}

// Body 0x1400aed50 (read in Ghidra). The vptr restamp on entry and the tail JMP
// to Graphics::Geometry::~Geometry are the compiler's. The body releases +0x148
// and +0x140; everything else retail releases after them (+0x220 down to +0x1a0,
// the buffers +0x180 / +0x160 / +0x120 / +0x100 through the imported
// ~IndexBuffer / ~VertexBuffer, the +0xe8 set, +0xd8) is a member whose
// destructor runs after this body in reverse declaration order, which is
// retail's order (WorldGeometry.h).
WorldGeometry::~WorldGeometry() {
    // EXT SEAM (NOT RETAIL, docs/extensions/handins/TERRAINBATCH.md): ext.terrain_batch frees
    // this geometry's run index buffer.
    if (_extTerrainBatch != nullptr) Ext::TerrainBatch::Forget(*this);
    // +0x148: the height ImageBuffer, deleted through its own slot 0 with
    // flags 1 (retail: MOV RAX,[RCX]; MOV EDX,1; CALL [RAX]).
    if (_heightImage) {
        delete _heightImage;   // slot 0, free
    }
    Object::detail::InternalMemFree(_heightBytes);          // +0x140 (not zeroed)
}

// ============================================================
// The device-lost pair (GameUpdateLoop slots [8] / [9] call these)
// ============================================================
//
// 0x1400b0be0 (read in Ghidra): the four buffers' companions are released in
// address order through the imported PlatformDestruct (0x140152670 vertex,
// 0x140152678 index), then the height image is deleted through its own slot 0
// with flags 1. Retail does NOT null +0x148: the pointer dangles until
// OnVideoRestart's UploadHeightImage stores the new image over it. Nothing
// reads it in between (Engine::MainLoop::OnVideoError destroys the Context
// right after the loop's slot [8], and no frame is drawn before slot [9]).
void WorldGeometry::OnVideoError() {
    _vertexBuffer.PlatformDestruct();        // +0x100
    _indexBuffer.PlatformDestruct();         // +0x120
    _waterVertexBuffer.PlatformDestruct();   // +0x160
    _waterIndexBuffer.PlatformDestruct();    // +0x180
    if (_heightImage) {
        delete _heightImage;   // slot 0, free (MOV EDX,1; JMP [RAX]); not nulled
    }
    // EXT SEAM (NOT RETAIL, docs/extensions/handins/TERRAINBATCH.md): ext.terrain_batch's run
    // index buffer loses its device half too (the restart's BuildTerrainMesh rebuilds it).
    if (_extTerrainBatch != nullptr) Ext::TerrainBatch::OnVideoError(*this);
}

// 0x1400b0c50 (read in Ghidra): new companions on the new device through the
// imported PlatformConstruct (0x140152680 vertex, 0x140152688 index), in the
// same address order; then the terrain mesh is rebuilt and re-uploaded
// (0x1400ac1e0, which Creates +0x100 / +0x120 itself), the height image is
// re-allocated and re-uploaded (0x1400ac780, stores the new +0x148), and the
// water mesh is re-Created from the retained contour arrays: index buffer
// first (+0x1B8 count * 2 bytes from +0x1B0), then vertex buffer (+0x1A8
// count << 4 bytes from +0x1A0), both not dynamic -- the same two calls the
// world build ends with (water_contour_mesh.cpp).
void WorldGeometry::OnVideoRestart() {
    _vertexBuffer.PlatformConstruct();        // +0x100
    _indexBuffer.PlatformConstruct();         // +0x120
    _waterVertexBuffer.PlatformConstruct();   // +0x160
    _waterIndexBuffer.PlatformConstruct();    // +0x180
    BuildTerrainMesh();                       // 0x1400ac1e0
    UploadHeightImage();                      // 0x1400ac780
    _waterIndexBuffer.Create(_contourIndices.size * 2, false, _contourIndices.data);
    _waterVertexBuffer.Create(_contourVertices.size << 4, false, _contourVertices.data);
}

// ============================================================
// Slot +0x08 — chunk-streaming / heightmap rebuild
// ============================================================
//
// Original is a 5-byte JMP-rel32 thunk (exe 0x1400AED40 → 0x1400AAD50). The
// target walks the binary tree of dirty chunks at self+0xE8, regenerates each
// chunk's vertices via FUN_1400ac880, and uploads them into the master
// VertexBuffer at self+0x100 via Video::VertexBuffer::Set.
//
// Body: WorldGeometry::RebuildDirtyChunks (0x1400aad50), in
// serialize_heightmap_mesh.cpp next to the related helpers.
void WorldGeometry::Update(const System::Time& /*time*/) {
    RebuildDirtyChunks();
}

// ============================================================
// Slot +0x18 — collect-phase Draw (frustum cull + queue draw items)
// ============================================================
//
// Decompiled from exe 0x1400AE490 (~300 instructions). Takes (self, scene, renderer);
// the original ignores the scene. Walks the heightmap chunk grid, frustum-culls
// each chunk, and calls Renderer::AddDrawItem for each visible chunk + the
// contour ranges and the two batch arrays.
void WorldGeometry::AddDrawItems(const Graphics::Scene& /*scene*/,
                                 Graphics::Renderer& renderer) const {

    if (!s_collectLogged.exchange(true)) {
        printf("[GeomDraw_CollectDraw] first call self=%p renderer=%p\n",
               (const void*)this, (void*)&renderer);
        fflush(stdout);
    }

    int32_t gridSize   = _gridSize;
    int32_t chunkSize  = _chunkSize;
    int     chunkCount = (chunkSize > 0) ? (gridSize / chunkSize) : 0;

    // 1. The cull camera's camera-to-world Transform3.
    const Graphics::Camera& camera = renderer.GetCullCamera();
    Math::Transform3 cameraTransform3 = camera._cameraToWorld;

    // 2. Geometry-space frustum:
    //    frustum_local = (camera_frustum * cameraToWorld * geometry_inverseTransform)
    const Math::Transform selfTransform = GetTransform();
    Math::Transform  selfInvTransform  = selfTransform.Inverse();
    Math::Transform3 selfInvTransform3 = selfInvTransform.AsTransform3();

    const ::Frustum cameraFrustum = camera.GetFrustum();
    ::Frustum worldFrustum = {};
    ::Frustum localFrustum = {};
    // worldFrustum = cameraFrustum * cameraTransform3
    RtVolume_Frustum_MulTransform3(&cameraFrustum, &worldFrustum, &cameraTransform3);
    Ext::RenderCulling::SetWorldFrustum(&worldFrustum);   // [PerfX] the sim-side off-screen test uses this frame's planes
    // localFrustum = worldFrustum * geometryInverseTransform3
    RtVolume_Frustum_MulTransform3(&worldFrustum, &localFrustum, &selfInvTransform3);

    // CULLROUTE (NOT RETAIL, faithful perf work; volume/frustum_cull.h,
    // docs/extensions/handins/CULLROUTE.md): the local frustum's prepared table,
    // built after its last writer (the MulTransform3 just above; the `= {}`
    // initialiser is the other). Nothing below writes `localFrustum`: its address
    // reaches only Prepare (as const), and the AddDrawItem calls never see it.
    // So the chunk, decal and road tests get RtVolume_Frustum_Contains's answer
    // bit for bit. `cullVerify` (BANISHED_STRESS_COMPPROF=4 only) is read once.
    FrustumCullPlanes localPlanes;
    FrustumCull_Prepare(&localFrustum, &localPlanes);
    const bool cullVerify = g_frustumCullVerify != 0;

    // 3. Cached values.
    float    uvScale     = kRdata_14017214c;   // 0x14017214c
    float    fHalfGrid   = static_cast<float>(gridSize >> 1);
    uint16_t materialKey = static_cast<uint16_t>(_detailId);   // +0x28 (low word)
    const Graphics::MaterialInstance* matInst0 = _terrainMaterial;   // +0xF8
    float    chunkSizeF  = static_cast<float>(chunkSize);

    // 4. Per-chunk frustum cull + AddDrawItem.
    //   xmin = j * chunkSize - halfGrid          xmax = xmin + chunkSize
    //   ymin = i * chunkSize - halfGrid          ymax = ymin + chunkSize
    //   zmin = -16.0f                             zmax = -16.0f + 32.0f = 16.0f
    float zMin = kChunkZMin;
    float zMax = kChunkZMin + kChunkZRange;

    // EXT SEAM (NOT RETAIL, docs/extensions/handins/TERRAINBATCH.md): with ext.terrain_batch on
    // and its run buffer ready, the same chunk tests emit one item per run of visible chunks in
    // a row, and the shadow pass also tests each chunk's shadow reach against the view.
    // Otherwise retail's loop below, unchanged.
    const bool terrainBatch = _extTerrainBatch != nullptr && *_extTerrainBatch &&
                              Ext::TerrainBatch::Ready(*this);
    if (terrainBatch) {
        Ext::TerrainBatch::AddChunkItems(*this, renderer, localPlanes, cullVerify, localFrustum,
                                         selfInvTransform3, materialKey, uvScale);
    } else {
        for (int i = 0; i < chunkCount; ++i) {
            for (int j = 0; j < chunkCount; ++j) {
                float xmin = static_cast<float>(j) * chunkSizeF - fHalfGrid;
                float ymin = static_cast<float>(i) * chunkSizeF - fHalfGrid;
                float xmax = xmin + chunkSizeF;
                float ymax = ymin + chunkSizeF;
                const ::Box box = { { xmin, ymin, zMin }, { xmax, ymax, zMax } };

                uint32_t r = FrustumCull_Test(&localPlanes, &box, 0x3F, cullVerify);
                if (r != 0x40) {
                    uint16_t key2 = static_cast<uint16_t>((i << 6) | j);
                    renderer.AddDrawItem(this, matInst0, materialKey, key2, 0.0f, uvScale, 1, nullptr, 0);
                }
            }
        }
    }
    // CULLROUTE: the stress report's per-site count, once per call.
    FrustumCull_CountSite(kFrustumCullSiteTerrainChunks,
                          chunkCount > 0 ? static_cast<uint64_t>(chunkCount) * static_cast<uint64_t>(chunkCount) : 0u);

    // 5. If the renderer's shader != 1 (i.e., not the shadow pass), emit
    //    the auxiliary draw items for the contour ranges and the batches.
    if (renderer.GetShader() == 1) return;

    // The b260 contour mesh occupies VB+0x160/IB+0x180 and is drawn via the
    // +0x1c0/+0x1d0 range tables (modes 0x6000/0x8000).
    int countA = _contourRangesA.size;
    const Graphics::MaterialInstance* matInst1 = _waterMaterialA;    // +0x150
    for (int idx = 0; idx < countA; ++idx) {
        uint16_t key1 = static_cast<uint16_t>(idx | 0x3900);
        uint16_t key2 = static_cast<uint16_t>(idx | 0x6000);
        renderer.AddDrawItem(this, matInst1, key1, key2, 0.0f, uvScale, 1, nullptr, 0);
    }

    int countB = _contourRangesB.size;
    const Graphics::MaterialInstance* matInst2 = _waterMaterialB;    // +0x158
    for (int idx = 0; idx < countB; ++idx) {
        uint16_t key1 = static_cast<uint16_t>(idx | 0x4000);
        uint16_t key2 = static_cast<uint16_t>(idx | 0x8000);
        renderer.AddDrawItem(this, matInst2, key1, key2, 0.0f, uvScale, 1, nullptr, 0);
    }

    // +0x1F0 — the decals.
    const GeometryBatchArray& decals = _decals;
    uint64_t decalBoxes = 0;   // CULLROUTE: the per-site count
    for (int idx = 0; idx < decals._size; ++idx) {
        const GeometryBatch& b = decals._data[idx];
        if (b.material == nullptr) continue;

        float xmax = static_cast<float>(b.maxX) - fHalfGrid;
        float ymax = static_cast<float>(b.maxY) - fHalfGrid;
        float xmin = static_cast<float>(b.minX) - fHalfGrid;
        float ymin = static_cast<float>(b.minY) - fHalfGrid;
        const ::Box box = { { xmin, ymin, 0.0f }, { xmax, ymax, 0.0f } };

        ++decalBoxes;
        uint32_t r = FrustumCull_Test(&localPlanes, &box, 0x3F, cullVerify);
        if (r == 0x40) continue;

        // key1 = 1 normally; 3 if +0x5D is set.
        uint16_t key1 = b.alternateKey ? 3 : 1;
        uint16_t key2 = static_cast<uint16_t>(idx | 0x2000);
        renderer.AddDrawItem(this, b.material,
                             key1, key2, 0.0f, uvScale, 1, nullptr, 0);
    }

    // +0x200 — the road batches; also skipped when the vertex count (+0x10) is 0, key1 = 2.
    const GeometryBatchArray& roads = _roadBatches;
    uint64_t roadBoxes = 0;   // CULLROUTE: the per-site count
    for (int idx = 0; idx < roads._size; ++idx) {
        const GeometryBatch& b = roads._data[idx];
        if (b.material == nullptr) continue;
        if (b.vertices._size == 0) continue;

        float xmax = static_cast<float>(b.maxX) - fHalfGrid;
        float ymax = static_cast<float>(b.maxY) - fHalfGrid;
        float xmin = static_cast<float>(b.minX) - fHalfGrid;
        float ymin = static_cast<float>(b.minY) - fHalfGrid;
        const ::Box box = { { xmin, ymin, 0.0f }, { xmax, ymax, 0.0f } };

        ++roadBoxes;
        uint32_t r = FrustumCull_Test(&localPlanes, &box, 0x3F, cullVerify);
        if (r == 0x40) continue;

        uint16_t key2 = static_cast<uint16_t>(idx | 0x4000);
        renderer.AddDrawItem(this, b.material,
                             2, key2, 0.0f, uvScale, 1, nullptr, 0);
    }
    FrustumCull_CountSite(kFrustumCullSiteTerrainDecals, decalBoxes);   // CULLROUTE, once per call
    FrustumCull_CountSite(kFrustumCullSiteTerrainRoads, roadBoxes);
}

// ============================================================
// Slot +0x10 — per-item Draw (called back per AddDrawItem invocation)
// ============================================================
//
// Decompiled from exe 0x1400AEA10 (~170 instructions). Signature:
//   (self, Context&, Camera& (unused), material (unused), float opacity, int a, ...)
// where opacity (param5) is the interpolation alpha (Scalar) and a (param6) is the per-item
// key (the 2nd ushort key passed to AddDrawItem above, packed with extra bits
// in 0xE000 to choose draw mode).
void WorldGeometry::Draw(Video::Context& context, const Graphics::Camera& /*camera*/,
                         Graphics::ExternalPtrHandle /*material*/,
                         float param5, int a, int /*b*/, int /*c*/) const {
    const uint32_t param6 = static_cast<uint32_t>(a);

    if (!s_itemDrawLogged.exchange(true)) {
        printf("[GeomDraw_ItemDraw] first call self=%p ctx=%p alpha=%f key=0x%x\n",
               (const void*)this, (void*)&context, param5, param6);
        fflush(stdout);
    }

    // 1. Lerp the InterpolatedState<Transform> at +0x84 (Geometry::_transform)
    //    with the alpha.
    const Math::Transform lerpedTransform =
        _transform.Lerp(Math::Scalar(param5));

    // 2. Convert to Matrix and SetTransform(0, matrix).
    Math::Matrix matrixBuf = lerpedTransform.AsMatrix();
    context.SetTransform(0, matrixBuf);

    // 3. SetColor(1,1,1, lerp(_opacityPrev, _opacity)).
    float uvScale = kRdata_14017214c;   // 0x14017214c
    const Math::Vector4 color(1.0f, 1.0f, 1.0f,
                              (uvScale - param5) * _opacityPrev + param5 * _opacity);
    context.SetColor(color);

    // 4. Branch on the high 3 bits (0xE000) of param6.
    uint32_t key = param6 & 0xFFFF1FFFu;     // local index
    uint32_t mode = param6 & 0xE000u;        // draw-mode selector

    // Indexed draw of one batch's vertices and indices through the context's
    // dynamic buffers (the 0x2000 and 0x4000 branches).
    auto IndexedDrawFromBatch = [&](const GeometryBatch& batch) {
        int32_t vertCount  = batch.vertices._size;
        int32_t vertStride = 0x14;             // hardcoded in original
        int32_t idxCount   = batch.indices._size;
        bool ok = context.BeginIndexedVertices(vertCount, vertStride, idxCount);
        if (!ok) return;

        size_t vertBytes = static_cast<size_t>(vertCount) * static_cast<size_t>(context._dynamicVertexStride);
        std::memcpy(context._dynamicVertexCursor, batch.vertices._data, vertBytes);
        context._dynamicVertexCursor += vertBytes;

        // Context +0x20f0, the dynamic index cursor (16-bit indices).
        size_t idxBytes = static_cast<size_t>(idxCount) * 2;
        std::memcpy(context._dynamicIndexCursor, batch.indices._data, idxBytes);
        context._dynamicIndexCursor += static_cast<size_t>(idxCount);   // idxBytes / 2

        context.EndIndexedVertices(0);
    };

    if (mode == 0x2000) {
        GeometryBatch* base = _decals._data;
        if (!base) return;
        IndexedDrawFromBatch(base[static_cast<int>(key)]);
        return;
    }
    if (mode == 0x4000) {
        GeometryBatch* base = _roadBatches._data;
        if (!base) return;
        IndexedDrawFromBatch(base[static_cast<int>(key)]);
        return;
    }

    // Binds an index buffer the way the inlined SetIndexBuffer does: dirty bit
    // 0x10 when the bound buffer or its offset differs.
    auto BindIndexBuffer = [&](const Video::IndexBuffer* ib) {
        if (context._indexBuffer != ib || context._indexOffset != 0) {
            context._dirty |= 0x10;
            context._indexBuffer = ib;
            context._indexOffset = 0;
        }
    };

    // The contour vertex/index buffers, then DrawIndexed from one range record.
    auto DrawContourRange = [&](const GeometryDrawRange* ranges) {
        context.SetVertexBuffer(&_waterVertexBuffer, 0, 0, 0x10);
        BindIndexBuffer(&_waterIndexBuffer);
        const GeometryDrawRange& range = ranges[static_cast<int>(key)];
        // DrawIndexed(context, primType=0, [1], [2], [0], [3], 1)
        // Original order at LAB_1400aed0e: (param_2, 0, iVar7, iVar8, iVar9, iVar10, 1)
        //   iVar7 = arr[1], iVar8 = arr[2], iVar9 = arr[0], iVar10 = arr[3].
        context.DrawIndexed(0, range.values[1], range.values[2], range.values[0], range.values[3], 1);
        // EXT SEAM (NOT RETAIL, docs/extensions/handins/BETTERWATER.md): ext.better_water draws the
        // range once more with its shore pixel program over retail's water, then rebinds the water's.
        if (Ext::BetterWater::Enabled()) {
            const Ext::BetterWater::Kind kind =
                mode == 0x8000 ? Ext::BetterWater::Kind::Lake : Ext::BetterWater::Kind::River;
            Ext::BetterWater::DrawShore(context, kind, range.values[1], range.values[2], range.values[0],
                                        range.values[3]);
        }
    };

    if (mode == 0x8000) {
        const GeometryDrawRange* ranges = _contourRangesB.data;   // +0x1D0
        if (!ranges) return;
        DrawContourRange(ranges);
        return;
    }
    if (mode == 0x6000) {
        const GeometryDrawRange* ranges = _contourRangesA.data;   // +0x1C0
        if (!ranges) return;
        DrawContourRange(ranges);
        return;
    }

    // EXT SEAM (NOT RETAIL, docs/extensions/handins/TERRAINBATCH.md): a run item of
    // ext.terrain_batch (key bits 0xC000 set, a draw mode retail never emits): the chunk draw
    // below for each chunk of one grid row's run, in one DrawIndexed.
    if (Ext::TerrainBatch::IsRunKey(param6)) {
        Ext::TerrainBatch::DrawRun(*this, context, param6);
        return;
    }

    int32_t chunkSize     = _chunkSize;
    int32_t scale         = _chunkIndexStride;   // +0xD4
    int32_t gridSize      = _gridSize;
    int32_t primCount     = chunkSize * chunkSize * 6;
    int32_t chunksPerSide = (chunkSize > 0) ? (gridSize / chunkSize) : 0;
    int32_t row           = static_cast<int32_t>((key >> 6) & 0x3F);
    int32_t col           = static_cast<int32_t>(key & 0x3F);
    int32_t start         = (chunksPerSide * row + col) * scale;

    if (mode == 0xA000) {
        // Water overlay chunk — same chunk draw as terrain but from the water
        // VB/IB (+0x160/+0x180) and WITHOUT the height-image bind.
        context.SetVertexBuffer(&_waterVertexBuffer, 0, 0, 0x10);
        BindIndexBuffer(&_waterIndexBuffer);
        context.DrawIndexed(0, start, scale, 0, primCount, 1);
        return;
    }

    // Default branch: terrain chunk draw.
    //   SetImageBuffer(_heightImage, 6), SetVertexBuffer(+0x100, 0, 0, 0x10),
    //   bind the IB at +0x120, DrawIndexed(0, start, scale, 0, primCount, 1).
    context.SetImageBuffer(_heightImage, 6);
    context.SetVertexBuffer(&_vertexBuffer, 0, 0, 0x10);
    BindIndexBuffer(&_indexBuffer);
    context.DrawIndexed(0, start, scale, 0, primCount, 1);
}
