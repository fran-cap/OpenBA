#pragma once
// ============================================================================
// src/render/vulkan/depth_copy/depth_copy.h -- THE SHARED DEPTH COPY on this backend
// ============================================================================
// NOT RETAIL. The contract and the reuse rule: src/render/common/depth_copy/depth_copy_backend.h. The
// readers: the AO pass (ao/assao_pass.cpp, a multisampled depth: a single-sampled one it reads itself),
// the cloud shadow (better_clouds/cloud_shadow.cpp) and the water's scene depth
// (better_water/scene_depth.cpp). Bodies: depth_copy.cpp.
//
// THE COPY is one fullscreen draw into a 1x R32F engine Video::ImageBuffer (a colour render buffer, so
// the water binds it with Context::SetImageBuffer like any texture): sample 0 of a multisampled main
// depth, a plain copy of a single-sampled one, the tiler's MSAA depth (already sample 0) as a
// single-sampled one. The programs are AO's depth resolve (src/render/dx11/ao/ao_depth_resolve.hlsl,
// the committed DXBC in src/render/common/ao/shaders/) through the backend's DXBC -> SPIR-V converter.
// It is left in SHADER_READ_ONLY.
// ============================================================================

#include "draw.h"

namespace Video {
class ImageBuffer;
}

namespace VideoVulkan {
namespace DepthCopy {

// A reader (draw.h DepthReader bit) wants the copy, or no longer does. Any thread. With no reader left
// the copy's image is released.
void Request(uint32_t reader, bool on);

// The live device can make the copy (the programs converted, the pipelines made). Any thread.
bool Supported();

// Inside DrawAoBegin's bracket (`t` its targets: the main depth in SHADER_READ_ONLY): the copy of the
// main depth as it stands, made now unless the one made earlier this frame is still fresh. Null when it
// cannot be made (logged once).
const Video::ImageBuffer* InBracket(const AoTargets& t);

// Without a bracket: the copy made earlier this frame when it is still the main depth (no main-depth
// write since, depth_copy_backend.h THE RULE); null otherwise.
const Video::ImageBuffer* Fresh();

// The sampled view (SHADER_READ_ONLY) of a copy InBracket or Fresh returned.
VkImageView ViewOf(const Video::ImageBuffer* copy);

// DeviceData::Shutdown, the device idle: the image is released and the pass's objects destroyed; the
// next device makes them again.
void Shutdown();

}  // namespace DepthCopy
}  // namespace VideoVulkan
