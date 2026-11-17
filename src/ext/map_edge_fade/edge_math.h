#pragma once
// ============================================================================
// src/ext/map_edge_fade/edge_math.h -- ext.map_edge_fade's look and its arithmetic (NOT RETAIL)
// ============================================================================
// docs/extensions/handins/MAPEDGE.md. The fade map_edge.hlsl draws, on the CPU: the constants the extension
// hands the pass, and the same darkness for a world position. Standard C++ only:
// tests/unit/ext_map_edge_fade_test.cpp links nothing but this header.
// ============================================================================

#include <algorithm>
#include <cmath>

namespace Ext {
namespace MapEdgeFade {

// The look (world units; a tile is 1).
struct Look {
    float inset = 32.0f;    // the fade starts this far inside the terrain's edge
    float outset = 2.0f;     // and is black this far past it
    float horizon = 24.0f;   // the sky returns over 1 / this of the view ray's downward slope under the horizon
};

// What the pass's constants carry: where the fade starts (from the map's centre) and 1 / its width.
struct Band {
    float start;
    float invWidth;
};

// Half the terrain's side in world units, from the heightmap's row stride (WorldGeometry::_heightRowStride:
// `stride` samples span stride - 1 tiles, centred on the world's origin). The terrain is wider than the
// playable map (Map::size): the border of bare ground around it is part of what retail draws, and the cut
// the fade hides is the terrain's, not the playable area's.
inline float TerrainHalf(int heightRowStride) {
    return heightRowStride > 1 ? static_cast<float>(heightRowStride - 1) * 0.5f : 0.0f;
}

// `half`: half the terrain's side (TerrainHalf): the world's ground runs -half .. +half on x and y.
inline Band BandFor(float half, const Look& look) {
    const float inset = std::max(look.inset, 0.0f);
    const float width = std::max(inset + std::max(look.outset, 0.0f), 1e-3f);
    Band b;
    b.start = std::max(half - inset, 0.0f);
    b.invWidth = 1.0f / width;
    return b;
}

// How dark the pass makes the ground at world (x, y): 0 untouched .. 1 black. The shader's expression.
inline float Darkness(float x, float y, const Band& band) {
    const float qx = std::max(std::fabs(x) - band.start, 0.0f);
    const float qy = std::max(std::fabs(y) - band.start, 0.0f);
    const float t = std::min(std::sqrt(qx * qx + qy * qy) * band.invWidth, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

}  // namespace MapEdgeFade
}  // namespace Ext
