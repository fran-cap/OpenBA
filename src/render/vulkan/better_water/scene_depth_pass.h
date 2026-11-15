#pragma once
// ============================================================================
// src/render/vulkan/better_water/scene_depth_pass.h -- the scene depth copy's device objects
// ============================================================================
// NOT RETAIL. ext.better_water's scene depth copy on this backend (scene_depth.cpp; the contract is
// src/render/common/better_water/scene_depth_backend.h). Only the device's teardown is declared here.
// ============================================================================

namespace VideoVulkan {
namespace SceneDepth {

// DeviceData::Shutdown, the device idle: the reference to this frame's copy is dropped (the copy and
// its pass's objects go in DepthCopy::Shutdown); the next device copies again at the next Copy.
void Shutdown();

}  // namespace SceneDepth
}  // namespace VideoVulkan
