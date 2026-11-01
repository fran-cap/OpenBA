// ============================================================================
// src/render/vulkan/integration.cpp -- the Vulkan backend's side of the backend-neutral seams
// ============================================================================
// NOT RETAIL. The bodies of the interface headers in src/render/common that have no file of their own
// here, and the two seams that live with their callers (Ext::RenderPipe::, Graphics::ExtAoAfterOpaque).
// P4 gave every seam a real answer (docs/vulkan/PARITY.md); the others' bodies live with their stage:
//   FaaBackend / BbsBackend / SmokeVsBackend          shader_rewrites.cpp
//   AsyncGpuCreateBackend                             async_gpu_create/async_create.cpp
//   ShareGpuDataBackend                               share_gpu_data/share_create.cpp
//   CreateOnDemandBackend                             create_on_demand/lazy_create.cpp
//   CompressAoBackend                                 compress_ao/compress_ao.cpp
//   TextureQualityBackend                             texture_quality/texture_quality_vulkan.cpp
//   BootDeviceLostBackend                             boot_device_lost/boot_device_lost.cpp
// Nothing here names another backend's namespace.
// ============================================================================

#include "VideoVulkanData.h"
#include "vulkan_entry.h"
#include "draw.h"
#include "ao/assao_pass.h"

#include "video/Context.h"
#include "video/Device.h"
#include "ao/ao_backend.h"
#include "ext/render_pipe/finish_split.h"
#include "graphics/renderer_ext_ao.h"

#include <cstdio>

// ext.ambient_occlusion: ASSAO's committed DXBC through the same DXBC -> SPIR-V path as the game's
// programs (ao/assao_pass.h), over a main depth made sampled on request (draw.cpp EnsureMainTargets
// rebuilds the main targets at the next frame; no settings change is raised: the main targets are this
// backend's own, not the engine's render buffer).
namespace AoBackend {

bool Supported() {
    return VideoVulkan::DeviceLive() && VideoVulkan::Assao::Ready();
}

void RequestReadableDepth(bool on) {
    // The AO pass's own request (draw.h THE READABLE DEPTH: ext.better_water's scene depth copy is the
    // other reader); logged when it changes.
    if (VideoVulkan::DrawRequestReadableDepth(VideoVulkan::kDepthReaderAo, on)) {
        std::fprintf(stderr, "[AmbientOcclusion] main depth -> %s: rebuilt at the next frame (Vulkan)\n",
                     on ? "readable (sampled view)" : "retail (attachment only, unless another reader asks)");
    }
}

bool DepthReadable() {
    return VideoVulkan::DrawDepthReadable();
}

}  // namespace AoBackend

// ext.render_pipe: the render thread runs PresentFrame (Finish's present half), the main thread the
// device-lost recovery at the top of its next frame (dx11 render_pipe_finish.cpp's split). The state is
// the present's result: 0, or VK_ERROR_DEVICE_LOST.
namespace Ext {
namespace RenderPipe {

int32_t FinishPresent(Video::Context& ctx) {
    return VideoVulkan::PresentFrame(ctx);
}

void FinishRecover(Video::Device& device, int32_t state) {
    if (state == 0) return;
    VideoVulkan::RecoverLostDevice(device, state);
}

}  // namespace RenderPipe
}  // namespace Ext

// ext.ambient_occlusion's pass hook (renderer_ext_ao.h): Renderer::Draw calls it between the Opaque and
// Blended lists of the main entry while the row is on and AoBackend::Supported() is true.
namespace Graphics {

void ExtAoAfterOpaque(Video::Context& context, const ExtAoFrame& frame, const RendererSceneEntry& entry) {
    VideoVulkan::Assao::AfterOpaque(context, frame, entry);
}

}  // namespace Graphics
