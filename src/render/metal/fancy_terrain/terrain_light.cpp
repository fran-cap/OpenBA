// ============================================================================
// src/render/metal/fancy_terrain/terrain_light.cpp -- ext.fancy_terrain's terrain light: not supported on Metal
// ============================================================================
// NOT RETAIL. The contract is src/render/common/fancy_terrain/terrain_light_backend.h. The pass is DX11's and
// Vulkan's (src/render/{dx11,vulkan}/fancy_terrain/terrain_light.cpp): here Supported() answers false and Draw
// is a no-op, so the terrain draws as retail lights it.
// ============================================================================
#include "fancy_terrain/terrain_light_backend.h"

namespace TerrainLightBackend {

bool Supported() { return false; }

void Request(bool /*on*/) {}

void Draw(Video::Context& /*context*/, const Textures& /*textures*/, const Frame& /*frame*/) {}

}  // namespace TerrainLightBackend
