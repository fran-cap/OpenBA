// ============================================================================
// src/render/vulkan/better_water/scene_depth.cpp -- ext.better_water's scene depth copy on this backend
// ============================================================================
// NOT RETAIL. The contract is src/render/common/better_water/scene_depth_backend.h (docs/extensions/
// handins/BETTERWATER.md section 8).
//
// THE COPY is THE SHARED DEPTH COPY (depth_copy/depth_copy.h; the rule:
// src/render/common/depth_copy/depth_copy_backend.h): a 1x R32F engine Video::ImageBuffer holding the
// main depth (sample 0 of a multisampled one), which the contact pass binds with
// Context::SetImageBuffer(image, 4) like any texture (draw.cpp StageImages: a texture read only with
// Load gets the placeholder sampler).
//
// THE FRAME. At the frame-copy seam: when the AO pass or the cloud shadow made the copy earlier this
// frame and no draw has written the main depth since (the Blended draws before the water test the depth
// and never write it), the copy is taken as it is: no pass ends, the main depth stays in its attachment
// layout. Otherwise draw.cpp's AO bracket: DrawAoBegin ends the pass in progress and moves the main
// depth to SHADER_READ_ONLY, the copy runs in a pass of its own, DrawAoEnd makes the engine's next draw
// rebind everything; that draw (the water) begins a new main pass that loads the depth. The depth is not
// discarded while a reader asks for it (THE DEPTH DISCARD's `allowed`).
// ============================================================================
#include "better_water/scene_depth_backend.h"
#include "better_water/scene_depth_pass.h"

#include "depth_copy/depth_copy.h"
#include "draw.h"
#include "vulkan_entry.h"

#include "video/ImageBuffer.h"

#include <atomic>
#include <cstdint>
#include <mutex>

namespace VideoVulkan {
namespace SceneDepth {

namespace {

std::mutex g_mutex;
std::atomic<bool> g_requested{false};
// The copy this frame's Copy() took, and that frame.
const Video::ImageBuffer* g_image = nullptr;
uint64_t g_copiedFrame = ~0ull;

}  // namespace

void Shutdown() {
    std::lock_guard<std::mutex> guard(g_mutex);
    // The copy itself goes with the device in DepthCopy::Shutdown.
    g_image = nullptr;
    g_copiedFrame = ~0ull;
}

}  // namespace SceneDepth
}  // namespace VideoVulkan

namespace SceneDepthBackend {
BANISHED_RENDER_IMPL_BEGIN

using namespace VideoVulkan::SceneDepth;

bool Supported() {
    return VideoVulkan::DeviceLive() && VideoVulkan::DepthCopy::Supported();
}

void Request(bool on) {
    g_requested.store(on);
    VideoVulkan::DrawRequestReadableDepth(VideoVulkan::kDepthReaderSceneDepth, on);
    VideoVulkan::DepthCopy::Request(VideoVulkan::kDepthReaderSceneDepth, on);
    if (on) return;
    std::lock_guard<std::mutex> guard(g_mutex);
    g_image = nullptr;
    g_copiedFrame = ~0ull;
}

void Copy(void* /*context*/) {
    if (!g_requested.load()) return;
    std::lock_guard<std::mutex> guard(g_mutex);
    const Video::ImageBuffer* copy = VideoVulkan::DepthCopy::Fresh();
    uint64_t frame = VideoVulkan::DrawFrameIndex();
    if (copy == nullptr) {
        // Outside a frame (the screenshot's frame copies), or the frames before the main depth is remade
        // readable: no copy, and Image() answers null for this frame.
        VideoVulkan::AoTargets t;
        if (!VideoVulkan::DrawAoBegin(&t)) return;
        copy = VideoVulkan::DepthCopy::InBracket(t);
        frame = t.frame;
        VideoVulkan::DrawAoEnd();
    }
    if (copy == nullptr) return;
    g_image = copy;
    g_copiedFrame = frame;
}

const Video::ImageBuffer* Image() {
    if (!g_requested.load()) return nullptr;
    std::lock_guard<std::mutex> guard(g_mutex);
    if (g_image == nullptr || g_image->_data == nullptr) return nullptr;
    if (g_copiedFrame != VideoVulkan::DrawFrameIndex()) return nullptr;
    return g_image;
}

BANISHED_RENDER_IMPL_END
}  // namespace SceneDepthBackend
