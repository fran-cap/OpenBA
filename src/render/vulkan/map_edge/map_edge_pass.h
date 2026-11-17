#pragma once
// ============================================================================
// src/render/vulkan/map_edge/map_edge_pass.h -- the device's end of ext.map_edge_fade's rim fade
// ============================================================================
// NOT RETAIL. The pass itself is map_edge_pass.cpp (the contract: src/render/common/map_edge/
// map_edge_backend.h).
// ============================================================================

namespace VideoVulkan {
namespace MapEdge {

// DeviceData::Shutdown, the device idle: the pass's pipelines, layouts, pools, ring and shader modules
// are destroyed (the depth copy goes in DepthCopy::Shutdown); the next device makes them again
// (MapEdgeBackend::Supported, then the next Draw).
void Shutdown();

}  // namespace MapEdge
}  // namespace VideoVulkan
