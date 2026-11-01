#pragma once
// ============================================================================
// src/render/vulkan/better_water/scene_depth_pass.h -- the scene depth copy's device objects
// ============================================================================
// NOT RETAIL. ext.better_water's scene depth copy on this backend (scene_depth.cpp; the contract is
// src/render/common/better_water/scene_depth_backend.h). Only the device's teardown is declared here.
// ============================================================================

namespace VideoVulkan {
namespace SceneDepth {

// DeviceData::Shutdown, the device idle: the copy (an engine ImageBuffer) is released and the copy
// pass's pipelines, layouts, pools and shader modules are destroyed; the next device makes them again
// (SceneDepthBackend::Supported, then the next Copy).
void Shutdown();

}  // namespace SceneDepth
}  // namespace VideoVulkan
