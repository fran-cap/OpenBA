// ============================================================================
// src/render/metal/integration.mm -- the Metal backend's side of the backend-neutral seams
// ============================================================================
// NOT RETAIL. The bodies of the interface headers in src/render/common (AoBackend::, SmokeVsBackend::), the
// finish seam (Ext::RenderPipe::) and the AO-after-opaque seam (Graphics::ExtAoAfterOpaque),
// plus the Objective-C++ constructor the plain C++ entry points call
// (render_backend_metal.cpp). Nothing here names another backend's namespace.
// ============================================================================

#include "VideoMetalData.h"
#include "metal_entry.h"
#include "video/Context.h"
#include "video/Device.h"
#include "ao/ao_backend.h"
#include "better_trees/foliage_aa_backend.h"
#include "better_trees/billboard_shadow_backend.h"
#include "async_gpu_create/async_gpu_create_backend.h"
#include "draw_batch/smoke_vs_backend.h"
#include "ext/render_pipe/finish_split.h"
#include "graphics/renderer_ext_ao.h"
#include "passes/assao_pass.h"
#include "passes/tree_passes.h"

namespace VideoMetal {

Video::DeviceData* NewDeviceData(Video::Device* device) {
    return new DeviceData(device);
}

}  // namespace VideoMetal

namespace AoBackend {

// Ambient occlusion runs only when a Metal device is live AND every ASSAO shader converted.
bool Supported() {
    if (!VideoMetal::DeviceLive()) {
        return false;
    }
    return VideoMetal::AssaoReady();
}

// The Metal depth is created readable already, so the request changes nothing.
void RequestReadableDepth(bool on) {
    (void)on;
}

bool DepthReadable() {
    return VideoMetal::DepthReadable();
}

}  // namespace AoBackend

// ext.better_trees: the extension's requests go to the Metal tree passes (passes/tree_passes.mm),
// which rewrite the retail cutout and billboard programs from the user's own bytecode. Metal
// supports every mode the interface carries, so there is no "not supported" answer to give.
namespace FaaBackend {

void Request(int mode, float fillK, float fillFloor, const BbsRewrite::Params* dither, bool naturalGrowth) {
    VideoMetal::TreePasses::SetFoliage(mode, fillK, fillFloor, dither, mode > 0 && naturalGrowth);
}

}  // namespace FaaBackend

namespace BbsBackend {

void Request(bool on, bool naturalGrowth) {
    VideoMetal::TreePasses::SetBillboardShadow(on, on && naturalGrowth);
}

}  // namespace BbsBackend

// ext.async_gpu_create: the Metal backend has no worker path (its resources are created where the
// loader makes them), so the request is ignored and the answer is "not supported". MTLDevice's
// resource creation is thread-safe, so a worker path would need no device lock: the open work is the
// backend's own state (the buffer bookkeeping under stateMutex, ImageBufferData's members) and the
// await before a deferred object's first use, as src/render/dx11/async_gpu_create/ does.
namespace AsyncGpuCreateBackend {

void Request(bool on) {
    (void)on;
}

bool Supported() {
    return false;
}

}  // namespace AsyncGpuCreateBackend

// ext.compress_ao, ext.share_gpu_data and ext.create_on_demand: their bodies are compress_ao/bc4_create.mm,
// share_gpu_data/share_create.mm and create_on_demand/lazy_create.mm (the DX11 layout).

// ext.draw_batch, Idea B (merged smoke runs): the extension needs a rewritten particle vertex shader
// that writes an emitter slot into each vertex. Metal has no such rewrite, so it answers "not
// supported" through the interface: the stage stays off, Rewritten() is false for every program,
// and the extension draws the faithful path.
namespace SmokeVsBackend {

void Request(bool on) {
    (void)on;
}

bool Rewritten(const Video::VertexProgram* vp) {
    (void)vp;
    return false;
}

}  // namespace SmokeVsBackend

namespace Ext {
namespace RenderPipe {

int32_t FinishPresent(Video::Context& ctx) {
    ctx._contextData->Finish();
    return 0;
}

void FinishRecover(Video::Device& device, int32_t state) {
    (void)device;
    (void)state;
}

}  // namespace RenderPipe
}  // namespace Ext

namespace Graphics {

void ExtAoAfterOpaque(Video::Context& context, const ExtAoFrame& frame, const RendererSceneEntry& entry) {
    VideoMetal::Assao::AfterOpaque(context, frame, entry);
}

}  // namespace Graphics
