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
