// ============================================================================
// src/ext/render_pipe/ui_layer.cpp -- SMOOTHREPLAY: the recorded interface (ui_layer.h)
// ============================================================================
// NOT RETAIL. ext.render_pipe (docs/extensions/handins/SMOOTHREPLAY.md section 3).
//
// THE LOCK. The record names each batch's MaterialInstance by pointer, and a step may
// free one while a replayed frame draws (the map window closed, ext.sharp_ui's sheets
// rebuilt on a scale change). Graphics::MaterialInstance::_extBeforeDestroy runs on the
// freeing thread before the instance is touched: under `g_m` it drops a record that
// names the instance. UiLayerDraw holds `g_m` for its whole draw, so the free waits out
// a draw in progress (well under a millisecond) and the next replayed frame finds no
// record and stops. The recording itself (the Flush seam's appends between
// UiLayerBegin and UiLayerEnd) takes no lock: it runs after the emit's gate, where no
// step runs beside it.
// ============================================================================
#include "ext/render_pipe/ui_layer.h"

#include <cstring>
#include <mutex>

#include "graphics/Material.h"      // Graphics::MaterialInstance::Setup, _extBeforeDestroy
#include "math/Math.h"
#include "object/External.h"        // Object::ExternalFactory: the tool overlay's material
#include "object/TypeInfo.h"
#include "system/Filename.h"
#include "ui/SpriteContext.h"       // UI::SpriteContext::_extRecord
#include "ui/SpriteRecord.h"
#include "video/CompanionData.h"    // Video::ContextData (BeginVertices / EndVertices), ImageBufferData
#include "video/Context.h"

namespace Ext {
namespace RenderPipe {
namespace {

std::mutex       g_m;                  // the record against a free on the main thread (the banner)
UI::SpriteRecord g_record;
const void*      g_owner = nullptr;    // the game loop the record was drawn for
bool             g_ready = false;      // a whole, replayable frame is recorded
float            g_endTransform[16];   // Context::_transforms[0] when the interface draw ended

void SetScissor(Video::Context& ctx, const int32_t* s) {
    Video::Viewport& cur = ctx._scissor;
    if (cur.x == s[0] && cur.y == s[1] && cur.width == s[2] && cur.height == s[3]) return;
    cur.x      = s[0];
    cur.y      = s[1];
    cur.width  = s[2];
    cur.height = s[3];
    ctx._dirty |= RtVideo::kDirtyScissor;
}

void SetColor(Video::Context& ctx, const float* c) {
    Math::Vector4& cur = ctx._color;
    if (cur.x == c[0] && cur.y == c[1] && cur.z == c[2] && cur.w == c[3]) return;
    cur.x = c[0];
    cur.y = c[1];
    cur.z = c[2];
    cur.w = c[3];
    ctx._dirty |= RtVideo::kDirtyInstanceConstants;
}

void SetTransform0(Video::Context& ctx, const float* cells) {
    if (std::memcmp(ctx._transforms[0].m, cells, sizeof(ctx._transforms[0].m)) == 0) return;
    Math::Matrix m;
    std::memcpy(m.m, cells, sizeof(m.m));
    ctx.SetTransform(0, m);
}

// SpriteContext::Flush's sampler switch (ext.sharp_ui): the texture slots are bound again.
void SetPointSampling(Video::Context& ctx, bool point) {
    if (Video::ImageBufferData::_extPointSampling == point) return;
    Video::ImageBufferData::_extPointSampling = point;
    ctx._imageBufferDirty |= RtVideo::kAllImageBufferSlotsDirty;
    ctx._dirty |= RtVideo::kDirtyImageBuffers;
}

// Graphics::MaterialInstance::_extBeforeDestroy (the freeing thread).
void BeforeMaterialDestroy(const Graphics::MaterialInstance* instance) {
    std::lock_guard<std::mutex> lk(g_m);
    if (!g_ready) return;
    for (const UI::SpriteRecordBatch& b : g_record.batches) {
        if (b.material == instance) {
            g_ready = false;
            return;
        }
    }
}

}  // namespace

void UiLayerInstall() {
    Graphics::MaterialInstance::_extBeforeDestroy = &BeforeMaterialDestroy;
}

void UiLayerRemove() {
    Graphics::MaterialInstance::_extBeforeDestroy = nullptr;
    UiLayerDrop();
}

void UiLayerBegin() {
    {
        std::lock_guard<std::mutex> lk(g_m);
        g_ready = false;
    }
    g_record.Clear();
    UI::SpriteContext::_extRecord = &g_record;
}

bool UiLayerTakeSaveLoad() {
    const bool drew = !g_record.Empty();
    g_record.Clear();
    return drew;
}

namespace {

// ---- the tool's overlay (Video::Context::_extVertexTap, the render thread) ----
const uint8_t*                    g_tapStart    = nullptr;   // the open stream's first vertex
Graphics::MaterialInstance*       g_tapMaterial = nullptr;   // Terrain/Selection.rsc, looked up per tool draw
bool                              g_tapLooked   = false;
int32_t                           g_tapStreams  = 0;

// The material every tool overlay is drawn with: the lookup PlayerTileRender_DrawOverlay_08c0 made
// a moment ago on this thread, so it is a cache hit.
Graphics::MaterialInstance* SelectionMaterial() {
    if (g_tapLooked) return g_tapMaterial;
    g_tapLooked = true;
    System::Filename name("Terrain/Selection.rsc");
    ::Object::ExternalFactory* const factory = ::Object::ExternalFactory::_singleton;
    const Object::External* const obj = factory != nullptr ? factory->FindOrLoad(name) : nullptr;
    if (obj != nullptr && Object::IsDerivedFromNullSafe(obj, Graphics::MaterialInstance::TypeId()))
        g_tapMaterial = const_cast<Graphics::MaterialInstance*>(static_cast<const Graphics::MaterialInstance*>(obj));
    return g_tapMaterial;
}

void TapBegin(Video::Context& ctx) {
    g_tapStart = ctx._dynamicVertexCursor;
}

void TapEnd(Video::Context& ctx, int32_t primitiveType) {
    const uint8_t* const start = g_tapStart;
    g_tapStart = nullptr;
    Graphics::MaterialInstance* const material = SelectionMaterial();
    if (start == nullptr || material == nullptr || ctx._dynamicVertexCursor < start) return;
    const size_t bytes = static_cast<size_t>(ctx._dynamicVertexCursor - start);
    UI::SpriteRecordBatch b;
    b.material   = material;
    b.point      = Video::ImageBufferData::_extPointSampling;
    b.scissor[0] = ctx._scissor.x;
    b.scissor[1] = ctx._scissor.y;
    b.scissor[2] = ctx._scissor.width;
    b.scissor[3] = ctx._scissor.height;
    std::memcpy(b.transform, ctx._transforms[0].m, sizeof(b.transform));
    b.color[0]  = ctx._color.x;
    b.color[1]  = ctx._color.y;
    b.color[2]  = ctx._color.z;
    b.color[3]  = ctx._color.w;
    b.primitive = primitiveType;
    b.stride    = ctx._dynamicVertexStride;
    // The one read of the stream the tool just wrote (a few hundred vertices at most).
    uint8_t* const to = g_record.BeginChunk(bytes);
    std::memcpy(to, start, bytes);
    g_record.EndChunk(to + bytes, b);
    ++g_tapStreams;
}

const Video::Context::ExtVertexTap kTap = {&TapBegin, &TapEnd};

}  // namespace

void UiLayerToolBegin(const float* view, const float* proj) {
    g_record.AddCamera(view, proj);
    g_tapStart   = nullptr;
    g_tapLooked  = false;
    g_tapStreams = 0;
    Video::Context::_extVertexTap = &kTap;
}

int32_t UiLayerToolEnd() {
    Video::Context::_extVertexTap = nullptr;
    return g_tapStreams;
}

void UiLayerEnd(Video::Context& ctx, const void* owner, bool usable) {
    UI::SpriteContext::_extRecord = nullptr;
    std::memcpy(g_endTransform, ctx._transforms[0].m, sizeof(g_endTransform));
    if (!usable) g_record.Clear();
    std::lock_guard<std::mutex> lk(g_m);
    g_owner = owner;
    g_ready = usable;
}

void UiLayerDrop() {
    UI::SpriteContext::_extRecord = nullptr;
    Video::Context::_extVertexTap = nullptr;
    std::lock_guard<std::mutex> lk(g_m);
    g_ready = false;
    g_owner = nullptr;
}

bool UiLayerReady(const void* owner) {
    std::lock_guard<std::mutex> lk(g_m);
    return g_ready && g_owner == owner;
}

bool UiLayerDraw(Video::Context& ctx) {
    std::lock_guard<std::mutex> lk(g_m);
    if (!g_ready) return false;
    int32_t camera = -1;
    for (const UI::SpriteRecordBatch& b : g_record.batches) {
        if (b.material == nullptr || b.vertices <= 0) continue;
        if (b.camera != camera && b.camera >= 0) {
            const UI::SpriteRecordCamera& c = g_record.cameras[static_cast<size_t>(b.camera)];
            Math::Matrix view;
            Math::Matrix proj;
            std::memcpy(view.m, c.view, sizeof(view.m));
            std::memcpy(proj.m, c.proj, sizeof(proj.m));
            ctx.SetCamera(view, proj);
            camera = b.camera;
        }
        SetColor(ctx, b.color);
        SetTransform0(ctx, b.transform);
        SetScissor(ctx, b.scissor);
        b.material->Setup(ctx, 0);
        SetPointSampling(ctx, b.point);

        Video::ContextData* const cd = ctx._contextData;
        if (!cd->BeginVertices(b.vertices, b.stride)) continue;   // as Flush: the chunk is skipped
        const size_t bytes = static_cast<size_t>(b.vertices) * static_cast<size_t>(b.stride);
        std::memcpy(ctx._dynamicVertexCursor, g_record.bytes.data() + b.firstByte, bytes);
        ctx._dynamicVertexCursor += bytes;
        cd->EndVertices(b.primitive);
    }
    if (!g_record.batches.empty()) SetTransform0(ctx, g_endTransform);
    return true;
}

}  // namespace RenderPipe
}  // namespace Ext
