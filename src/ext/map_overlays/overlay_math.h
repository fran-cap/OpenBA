#pragma once
// ============================================================================
// src/ext/map_overlays/overlay_math.h -- ext.map_overlays: blur and colour (standard C++ only)
// ============================================================================
// NOT RETAIL. docs/extensions/handins/MAPOVERLAYS.md §5. Unit tested (tests/unit/ext_map_overlays_test.cpp).
// ============================================================================
#include <cstdint>
#include <vector>

namespace Ext {
namespace MapOverlays {

// The scale slider (0..1) as a blur radius in tiles: 0 = no blur (exact per tile), 1 = kMaxBlurTiles.
constexpr float kMaxBlurTiles = 24.0f;
float BlurRadiusTiles(float scale);

// Separable box-approximated Gaussian blur of a size*size grid in place; radius 0 leaves it untouched.
// The total over the grid is kept (a flow is spread, not lost), apart from what crosses the map edge.
void Blur(std::vector<float>& grid, int size, float radiusTiles);

// The value shown at full colour: a high percentile of the non-zero tiles (so one barn does not wash
// the rest out); 0 when every tile is 0.
float RangeMax(const std::vector<float>& grid);

// One colour of the heat ramp for t in 0..1 (clamped), 0xAABBGGRR; alpha rises with t, 0 at t == 0.
uint32_t Ramp(float t);

// grid -> RGBA with Ramp(value / maxValue).
void Colourise(const std::vector<float>& grid, float maxValue, std::vector<uint32_t>& out);

}  // namespace MapOverlays
}  // namespace Ext
