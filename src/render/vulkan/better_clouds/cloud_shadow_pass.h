#pragma once
// ============================================================================
// src/render/vulkan/better_clouds/cloud_shadow_pass.h -- the device's end of ext.better_clouds' shadow pass
// ============================================================================
// NOT RETAIL. The pass itself is cloud_shadow.cpp (the contract: src/render/common/better_clouds/
// cloud_shadow_backend.h).
// ============================================================================

namespace VideoVulkan {
namespace CloudShadow {

// DeviceData::Shutdown, the device idle: the depth copy (an engine ImageBuffer) is released and the
// pass's pipelines, layouts, pools, ring and shader modules are destroyed; the next device makes them
// again (CloudShadowBackend::Supported, then the next Draw).
void Shutdown();

}  // namespace CloudShadow
}  // namespace VideoVulkan
