// ============================================================================
// src/render/metal/map_edge/map_edge_pass.cpp -- ext.map_edge_fade's rim fade: not supported on Metal
// ============================================================================
// NOT RETAIL. The contract is src/render/common/map_edge/map_edge_backend.h
// (docs/extensions/handins/MAPEDGE.md). The pass is DX11's and Vulkan's
// (src/render/{dx11,vulkan}/map_edge/map_edge_pass.cpp): here Supported() answers false and Draw is a
// no-op, so the map's edge stays as retail draws it.
// ============================================================================
#include "map_edge/map_edge_backend.h"

namespace MapEdgeBackend {

bool Supported() { return false; }

void Request(bool /*on*/) {}

void Draw(Video::Context& /*context*/, const Frame& /*frame*/) {}

}  // namespace MapEdgeBackend
