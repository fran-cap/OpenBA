#pragma once
// ============================================================================
// src/render/vulkan/fancy_terrain/terrain_light_pass.h -- the device's end of ext.fancy_terrain's terrain light
// ============================================================================
// NOT RETAIL. The pass itself is terrain_light.cpp (the contract: src/render/common/fancy_terrain/
// terrain_light_backend.h).
// ============================================================================

namespace VideoVulkan {
namespace TerrainLight {

// DeviceData::Shutdown, the device idle: the map images, pipelines, layouts, pools, ring and shader modules
// are destroyed; the next device makes them again (TerrainLightBackend::Supported, then the next Draw).
void Shutdown();

}  // namespace TerrainLight
}  // namespace VideoVulkan
