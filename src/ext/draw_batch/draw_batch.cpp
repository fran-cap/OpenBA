// ============================================================================
// src/ext/draw_batch/draw_batch.cpp -- ext.draw_batch (class A): the DrawItemList body
// ============================================================================
// NOT RETAIL. draw_batch.h has the contract, docs/extensions/handins/DRAWBATCH.md the design.
//
// ExtDrawItemList is retail's Renderer::DrawItemList (batchc_gfx_leaves.cpp, 0x180105240)
// line for line -- the same camera matrices, the same per-item program test, the same
// GenerateTextures / Setup / instancing batch / Geometry::Draw calls -- with one addition at the
// top of the per-item step: when the item starts a run that MergeDecals or MergeSmoke can draw as
// one, the run is drawn merged and the loop skips past it. Every item no run takes is drawn by the copy.
//
// A DECAL RUN (WorldGeometry::Draw, geometry_draw.cpp, modes 0x2000 decals / 0x4000 roads) is a
// maximal sequence of consecutive items, at least two, that all
//   * draw (the material's pass program test) with the same MaterialInstance and Material,
//   * have no render texture (so GenerateTextures, shader 0 only, never runs for any of them),
//   * are the same WorldGeometry with the same mode and _arg8 <= 1 (no instancing batch),
//   * name a batch with at least one vertex that retail's capacity tests accept on its own,
// cut where the merged request would fail retail's capacity tests or pass 65536 vertices.
// Faithfully each item runs Setup, then Draw's SetTransform(0, lerp(_transform)) and SetColor(1,
// 1, 1, lerp(opacity)), then one Begin / copy / End. Setup, the transform and the colour take the
// same values for every item of the run (one material instance, one geometry, one alpha), so the
// faithful second and later calls change nothing; the merged run makes them once and copies every
// batch into one Begin / End (batch_fill.h). The vertex bytes, their order and the triangles'
// order are the faithful ones; only the dynamic-buffer offsets and the Context's draw counters
// differ.
//
// A SMOKE RUN (ParticleInstance::Draw, wave27_lane_gfxpart.cpp; Idea B) is a maximal sequence of
// consecutive particle items, at least two groups, with the same MaterialInstance and Material, no
// render texture, whose pass's vertex program has its rewritten copy (smoke_vs_rewrite.h: the
// emitter's transform slot is read from the vertex's +0x0C as well as SV_InstanceID). The items are
// cut into retail's instancing groups by retail's own rule (DrawItemList's batch loop: the same
// instance, _arg5, _userData and _arg10, at most _arg8, not past the list's end), and a group is
// added while the run's slots stay <= 52 and its vertices pass the capacity tests and 65536.
// Faithfully each group is one draw: Setup, the group's transforms in slots 0..n-1 (GetInstanceTransform
// + SetTransform; a group of one gets the same matrix from Draw's SetTransformScale), then the head's
// particles written once and drawn n instances. The merged run gives group g the slots s_g..s_g+n-1
// with the same matrices, writes the head's particles n times in instance order with k = s_g + j at
// +0x0C, and draws once (one instance, so SV_InstanceID is 0 and the VS reads transforms[k]). A D3D
// instanced draw rasterises all of instance 0 before instance 1, so the triangle order is retail's.
// ============================================================================
// The runtime type headers first: Volume.h's global `struct Rectangle` (geometry_draw.cpp's note).
#include "Game/WorldGeometry.h"
#include "Game/WorldGeometryElements.h"
#include "graphics/Geometry.h"
#include "graphics/Camera.h"
#include "graphics/Material.h"
#include "graphics/InterpolatedState.h"
#include "video/Context.h"
#include "video/Buffers.h"
#include "video/VideoProgram.h"
#include "video/ShaderProgram.h"
#include "Game/exe_constants.h"   // kRdata_14017214c, WorldGeometry::Draw's 1.0f
#include "graphics/ParticleInstance.h"
#include "draw_batch/smoke_vs_backend.h"   // src/render/common: SmokeVsBackend (Idea B)

#include "ext/draw_batch/draw_batch.h"
#include "ext/draw_batch/batch_fill.h"
#include "ext/registry.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <typeinfo>

namespace Ext {
namespace DrawBatch {

namespace {

using Graphics::Camera;
using Graphics::Geometry;
using Graphics::Material;
using Graphics::MaterialInstance;
using Graphics::Renderer;
using Graphics::RendererDrawGroup;
using Graphics::RendererDrawItem;

constexpr int32_t  kDecalStride = 0x14;     // WorldGeometry::Draw's hard-coded stride
constexpr uint32_t kModeMask    = 0xE000u;  // WorldGeometry::Draw's mode bits of the item key
constexpr uint32_t kModeDecal   = 0x2000u;
constexpr uint32_t kModeRoad    = 0x4000u;
constexpr int32_t  kMaxRunItems = 4096;     // the span scratch per run (a run is cut there)
constexpr int32_t  kSmokeStride = 0x34;     // ParticleInstance::Draw's vertex stride
constexpr int32_t  kSmokeSlots  = 52;       // Context::_transforms[52], the VS's cb of 208 float4
// The instancing batch prefetches the geometry this many items ahead (P13_RENDER.md: the vptr load of the
// virtual GetInstanceTransform missed once per instanced item on the scattered MeshInstances).
constexpr int64_t  kInstanceAhead = 4;

bool g_installed = false;

// ---------------------------------------------------------------------------
// The geometry classes a run can take, by dynamic type. typeid(*g) is one load through the
// object's vtable; its type_info's address is looked up in this short table, and the first
// object of each class pays the type_info compares. Only the emitting thread uses it (the
// render thread with ext.render_pipe, the main thread otherwise; never both at once: the
// hand-off orders them).
// ---------------------------------------------------------------------------
enum class Kind : uint8_t { Other, World, Particle };

struct KindCache {
    static constexpr int kSlots = 32;
    const std::type_info* type[kSlots] = {};
    Kind                  kind[kSlots] = {};
    int                   count        = 0;
};
KindCache g_kinds;

Kind Classify(const Geometry* g) {
    const std::type_info& t = typeid(*g);
    for (int i = 0; i < g_kinds.count; ++i) {
        if (g_kinds.type[i] == &t) return g_kinds.kind[i];
    }
    // The particle classes: ParticleInstance and ParticleSystemInstance share Draw (slot 2),
    // GetInstanceTransform (slot 6) and Geometry's SetSharedInstanceData (slot 7, `return 0`).
    Kind k = Kind::Other;
    if (t == typeid(WorldGeometry)) {
        k = Kind::World;
    } else if (t == typeid(Graphics::ParticleInstance) || t == typeid(Graphics::ParticleSystemInstance)) {
        k = Kind::Particle;
    }
    if (g_kinds.count < KindCache::kSlots) {
        g_kinds.type[g_kinds.count] = &t;
        g_kinds.kind[g_kinds.count] = k;
        ++g_kinds.count;
    }
    return k;
}

// Retail's per-item draw test (DrawItemList: the five silent drops).
bool ProgramDraws(const Material* material, int shaderIndex) {
    if (shaderIndex < 0 || shaderIndex >= material->_passes._size) return false;
    const Video::VideoProgram* const prog = material->_passes._data[shaderIndex].Get();
    const Video::VertexProgram* const vp = prog->_vertexProgram.Get();
    const Video::PixelProgram*  const pp = prog->_pixelProgram.Get();
    return prog->_vertexLayout.Get() != nullptr &&
           vp != nullptr && vp->_byteCode._size > 0 &&
           pp != nullptr && pp->_byteCode._size > 0;
}

// The batch a decal / road item draws (WorldGeometry::Draw: key = arg5 & ~0xE000), or null
// when the item is not one a run can take.
const GeometryBatch* DecalBatch(const RendererDrawItem& item, uint32_t* mode) {
    if (item._arg8 > 1) return nullptr;
    if (item._instance->_hasRenderTexture != 0) return nullptr;
    const uint32_t key = static_cast<uint32_t>(static_cast<int32_t>(item._arg5));
    const uint32_t m   = key & kModeMask;
    if (m != kModeDecal && m != kModeRoad) return nullptr;
    if (Classify(item._geometry) != Kind::World) return nullptr;
    const WorldGeometry* const world = static_cast<const WorldGeometry*>(item._geometry);
    const GeometryBatch* const base = (m == kModeDecal) ? world->_decals._data : world->_roadBatches._data;
    if (base == nullptr) return nullptr;
    *mode = m;
    return &base[static_cast<int>(key & 0xFFFF1FFFu)];
}

Span g_spans[kMaxRunItems];

// Draws the decal run starting at items[first] merged, and returns the items it took; 0 when
// fewer than two consecutive items qualify (the caller then draws items[first] faithfully).
int32_t MergeDecals(const Renderer& renderer, Video::Context& context,
                    const Collection::Array<RendererDrawItem>& items, int64_t first, int shaderIndex) {
    const RendererDrawItem& head = items._data[first];
    uint32_t headMode = 0;
    const GeometryBatch* const headBatch = DecalBatch(head, &headMode);
    if (headBatch == nullptr) return 0;

    const int32_t capacity = context._dynamicVertexBuffer->size;
    const auto alone = [&](const GeometryBatch& b) {
        return b.vertices._size > 0 &&
               RetailAccepts(capacity, b.vertices._size, kDecalStride, b.indices._size);
    };
    if (!alone(*headBatch)) return 0;

    // ---- the run --------------------------------------------------------------------------
    const int32_t count = items._size;
    int32_t n = 0;
    int64_t runVertices = 0;
    int64_t runIndices  = 0;
    for (int64_t i = first; i < count && n < kMaxRunItems; ++i) {
        const RendererDrawItem& item = items._data[i];
        if (i != first) {
            if (item._geometry != head._geometry) break;
            if (item._instance != head._instance) break;
            if (item._material != head._material) break;
        }
        uint32_t mode = 0;
        const GeometryBatch* const b = (i == first) ? headBatch : DecalBatch(item, &mode);
        if (b == nullptr) break;
        if (i != first && mode != headMode) break;
        if (!alone(*b)) break;
        if (!RunFits(capacity, kDecalStride, runVertices, runIndices, b->vertices._size, b->indices._size)) break;
        runVertices += b->vertices._size;
        runIndices  += b->indices._size;
        Span& s = g_spans[n];
        s.vertices    = b->vertices._data;
        s.vertexCount = b->vertices._size;
        s.indices     = b->indices._data;
        s.indexCount  = b->indices._size;
        ++n;
    }
    if (n < 2) return 0;

    // ---- the faithful per-item calls, once ------------------------------------------------
    // DrawItemList: Setup (the draw test passed for the head; the run shares its material).
    head._instance->Setup(context, shaderIndex);

    // WorldGeometry::Draw steps 1-3 (geometry_draw.cpp), with the float DrawItemList hands it.
    const WorldGeometry* const world = static_cast<const WorldGeometry*>(head._geometry);
    const float param5 = renderer._alphaT;
    const Math::Transform lerpedTransform = world->_transform.Lerp(Math::Scalar(param5));
    Math::Matrix matrixBuf = lerpedTransform.AsMatrix();
    context.SetTransform(0, matrixBuf);
    const float uvScale = kRdata_14017214c;
    const Math::Vector4 color(1.0f, 1.0f, 1.0f,
                              (uvScale - param5) * world->_opacityPrev + param5 * world->_opacity);
    context.SetColor(color);

    // ---- one Begin / fill / End -----------------------------------------------------------
    // Draw's step 4 for the whole run: the Begin is retail's (its tests passed above), the copy
    // is batch_fill.h's, the End is retail's DrawIndexed of every index.
    const int32_t vertCount = static_cast<int32_t>(runVertices);
    const int32_t idxCount  = static_cast<int32_t>(runIndices);
    if (context.BeginIndexedVertices(vertCount, kDecalStride, idxCount)) {
        FillRun(g_spans, n, context._dynamicVertexStride, context._dynamicVertexCursor, context._dynamicIndexCursor);
        context._dynamicVertexCursor += static_cast<size_t>(vertCount) * static_cast<size_t>(context._dynamicVertexStride);
        context._dynamicIndexCursor += static_cast<size_t>(idxCount);
        context.EndIndexedVertices(0);
    }
    return n;
}

// ---------------------------------------------------------------------------
// Idea B: smoke runs.
// ---------------------------------------------------------------------------
struct SmokeGroup {
    int64_t first   = 0;   // the group's first item (its geometry draws the group's particles)
    int32_t members = 0;   // retail's batchCount
    int32_t count   = 0;   // the head's live particles
};
SmokeGroup g_groups[kSmokeSlots];

bool SmokeItem(const RendererDrawItem& item) {
    // ParticleInstance::AddDrawItems' item: _arg5 0 and the pool as _userData (a quick reject of
    // the meshes before the class test).
    if (item._arg5 != 0 || item._userData == nullptr) return false;
    if (item._instance->_hasRenderTexture != 0) return false;
    return Classify(item._geometry) == Kind::Particle;
}

// Retail's instancing group starting at items[first] (DrawItemList's batch loop, with
// SetSharedInstanceData's 0 for the particle classes): the items it would consume.
int32_t RetailGroupSize(const Collection::Array<RendererDrawItem>& items, int64_t first) {
    const RendererDrawItem& head = items._data[first];
    if (head._arg8 <= 1) return 1;
    const int64_t count = items._size;
    int32_t batch = 0;
    int64_t at = first;
    for (;;) {
        ++batch;
        ++at;
        if (at >= count) break;
        if (batch >= head._arg8) break;
        const RendererDrawItem& nxt = items._data[at];
        if (nxt._instance != head._instance) break;
        if (nxt._arg5 != head._arg5) break;
        if (nxt._userData != head._userData) break;
        if (nxt._arg10 != head._arg10) break;
    }
    return batch;
}

// Draws the smoke run starting at items[first] merged, and returns the items it took; 0 when fewer
// than two groups qualify (the caller then draws items[first] faithfully).
int32_t MergeSmoke(const Renderer& renderer, Video::Context& context,
                   const Collection::Array<RendererDrawItem>& items, int64_t first, int shaderIndex) {
    const RendererDrawItem& head = items._data[first];
    if (!SmokeItem(head)) return 0;
    const Video::VideoProgram* const prog = head._material->_passes._data[shaderIndex].Get();
    if (!SmokeVsBackend::Rewritten(prog->_vertexProgram.Get())) return 0;

    const int32_t capacity = context._dynamicVertexBuffer->size;
    const int32_t count = items._size;
    int32_t groups = 0;
    int32_t slots = 0;
    int64_t runVertices = 0;
    int64_t runIndices = 0;
    int64_t at = first;
    while (at < count && groups < kSmokeSlots) {
        const RendererDrawItem& item = items._data[at];
        if (at != first) {
            if (item._instance != head._instance) break;
            if (item._material != head._material) break;
            if (!SmokeItem(item)) break;
        }
        const int32_t members = RetailGroupSize(items, at);
        const Graphics::ParticleInstance* const pi = static_cast<const Graphics::ParticleInstance*>(item._geometry);
        const int32_t live = pi->_boundsPtr->_liveCount;
        const int64_t v = 4ll * live * members;
        const int64_t idx = 6ll * live * members;
        if (slots + members > kSmokeSlots) break;
        // The group alone is a request retail accepts (else its faithful draw is skipped: the copy does it).
        if (!RetailAccepts(capacity, 4ll * live, kSmokeStride, 6ll * live)) break;
        if (!RunFits(capacity, kSmokeStride, runVertices, runIndices, v, idx)) break;
        SmokeGroup& g = g_groups[groups];
        g.first   = at;
        g.members = members;
        g.count   = live;
        ++groups;
        slots += members;
        runVertices += v;
        runIndices += idx;
        at += members;
    }
    if (groups < 2) return 0;

    // ---- Setup once, then every group's transforms in its slots ----------------------------
    head._instance->Setup(context, shaderIndex);
    Math::Matrix scratch;
    int32_t slot = 0;
    for (int32_t gi = 0; gi < groups; ++gi) {
        const SmokeGroup& g = g_groups[gi];
        for (int32_t j = 0; j < g.members; ++j) {
            const RendererDrawItem& cur = items._data[g.first + j];
            const Math::Matrix* const m = cur._geometry->GetInstanceTransform(&scratch, cur._arg5, renderer._alphaT);
            context.SetTransform(slot, *m);
            ++slot;
        }
    }

    // ---- one Begin / fill / End -------------------------------------------------------------
    const int32_t vertCount = static_cast<int32_t>(runVertices);
    const int32_t idxCount  = static_cast<int32_t>(runIndices);
    if (context.BeginInstancedIndexedVertices(vertCount, kSmokeStride, idxCount, 1)) {
        uint8_t*  vb = context._dynamicVertexCursor;
        uint16_t* ib = context._dynamicIndexCursor;
        uint32_t vertex = 0;
        slot = 0;
        for (int32_t gi = 0; gi < groups; ++gi) {
            const SmokeGroup& g = g_groups[gi];
            const Graphics::ParticleInstance* const pi =
                static_cast<const Graphics::ParticleInstance*>(items._data[g.first]._geometry);
            const RtGraphics::ParticleRecord* const records = pi->_boundsPtr->_records._data;
            for (int32_t j = 0; j < g.members; ++j) {
                const int32_t written = WriteParticles(records, g.count, renderer._alphaT, static_cast<float>(slot),
                                                       vertex, vb, ib);
                vb += static_cast<size_t>(written) * kSmokeStride;
                ib += static_cast<size_t>(g.count) * 6;
                vertex += static_cast<uint32_t>(written);
                ++slot;
            }
        }
        context._dynamicVertexCursor = vb;
        context._dynamicIndexCursor = ib;
        context.EndInstancedIndexedVertices(0);
    }
    return static_cast<int32_t>(at - first);
}

// ---------------------------------------------------------------------------
// The replacement body. Retail's DrawItemList (batchc_gfx_leaves.cpp) with the merge test at the
// top of each drawn item; the comments there hold the evidence for every line kept here.
// ---------------------------------------------------------------------------
void ExtDrawItemList(const Renderer& renderer, Video::Context& context, int groupIndex, int listIndex,
                     int shaderIndex) {
    const RendererDrawGroup& group = renderer._drawGroups._data[groupIndex];
    const Camera& cam = group._cullCamera;

    Math::Matrix view;
    {
        const Math::Transform3& t = cam._cameraToWorld;
        view.m[0]  = t.axisX.x;       view.m[1]  = t.axisX.y;       view.m[2]  = t.axisX.z;       view.m[3]  = 0.0f;
        view.m[4]  = t.axisY.x;       view.m[5]  = t.axisY.y;       view.m[6]  = t.axisY.z;       view.m[7]  = 0.0f;
        view.m[8]  = t.axisZ.x;       view.m[9]  = t.axisZ.y;       view.m[10] = t.axisZ.z;       view.m[11] = 0.0f;
        view.m[12] = t.translation.x; view.m[13] = t.translation.y; view.m[14] = t.translation.z;
        view.m[15] = 1.0f;
    }
    const Math::Matrix proj = cam._projection;

    context.SetCamera(view, proj);

    const Collection::Array<RendererDrawItem>& items = group._items[listIndex];

    const int32_t count = items._size;
    if (count <= 0) {
        return;
    }

    int32_t emitted = 0;
    int64_t i = 0;
    while (i < count) {
        int64_t itemIndex = i;
        const RendererDrawItem& item = items._data[itemIndex];

        int32_t step = 1;
        int32_t batchCount = 1;

        const Material* const material = item._material;
        const bool draw = ProgramDraws(material, shaderIndex);

        if (draw) {
            // NOT RETAIL: the merged run (the file banner). Every item it takes is one the
            // faithful loop would draw with exactly these calls, one item at a time.
            int32_t merged = MergeDecals(renderer, context, items, i, shaderIndex);
            if (merged == 0) merged = MergeSmoke(renderer, context, items, i, shaderIndex);
            if (merged > 0) {
                emitted += merged;
                i += merged;
                continue;
            }

            if (shaderIndex == 0 && item._instance->_hasRenderTexture != 0) {
                item._instance->GenerateTextures(context, renderer._frameIndex);
            }
            item._instance->Setup(context, shaderIndex);

            if (item._arg8 > 1) {
                int32_t slotIndex = item._geometry->SetSharedInstanceData(context, item._arg5, renderer._alphaT);
                const int32_t base = emitted - slotIndex;

                batchCount = 0;
                for (;;) {
                    const RendererDrawItem& cur = items._data[itemIndex];
                    // A hint only (never faults, changes nothing): the vptr, the transform pair, _mesh /
                    // _animation / _nodes and _color lines of a MeshInstance (0x120 bytes, Mesh.h).
                    if (itemIndex + kInstanceAhead < count) {
                        const auto* const next = items._data[itemIndex + kInstanceAhead]._geometry;
                        const char* ahead = reinterpret_cast<const char*>(next);   // CAST-OK: a prefetch address
                        __builtin_prefetch(ahead);
                        __builtin_prefetch(ahead + 0x80);
                        __builtin_prefetch(ahead + 0xc0);
                        __builtin_prefetch(ahead + 0x100);
                    }
                    ++batchCount;
                    ++itemIndex;

                    const Math::Matrix* const m =
                        cur._geometry->GetInstanceTransform(&view, cur._arg5, renderer._alphaT);
                    context.SetTransform(slotIndex, *m);

                    ++slotIndex;
                    if (base + slotIndex >= count) break;
                    if (batchCount >= item._arg8) break;

                    const RendererDrawItem& nxt = items._data[itemIndex];
                    if (nxt._instance != item._instance) break;
                    if (nxt._arg5 != item._arg5) break;
                    if (nxt._userData != item._userData) break;
                    if (nxt._arg10 != item._arg10) break;
                }
                step = batchCount;
            } else {
                batchCount = 1;
                step = 1;
            }

            item._geometry->Draw(
                context,
                group._cullCamera,
                Graphics::ExternalPtrHandle{ const_cast<MaterialInstance*>(item._instance) },  // CAST-OK: const_api (DrawItemList's own, batchc_gfx_leaves.cpp)
                renderer._alphaT,
                static_cast<int32_t>(item._arg5),
                shaderIndex,
                batchCount);
        }

        emitted += step;
        i += step;
    }
}

}  // namespace

void Sync() {
    const bool want = Ext::Ext().draw_batch;
    if (want == g_installed) return;
    Graphics::Renderer::_extDrawItemList = want ? &ExtDrawItemList : nullptr;
    SmokeVsBackend::Request(want);   // Idea B: the particle VS copies (on: now; off: at the next VS bind)
    g_installed = want;
    std::fprintf(stderr, "[DrawBatch] %s\n", want ? "installed: Renderer::DrawItemList merges runs"
                                                  : "removed: retail DrawItemList");
}

bool Installed() { return g_installed; }

}  // namespace DrawBatch
}  // namespace Ext
