#pragma once
// ============================================================================
// src/ext/better_water/river_mouth.h -- ext.better_water: the lake's water fades out where a river leaves it
// ============================================================================
// NOT RETAIL. docs/extensions/handins/BETTERWATER.md section 9.
//
// Retail's lake water (phase 2, WorldGeometry::BuildWaterShores, the 0x8000 ranges) covers every cell of a
// box round the lake (centre +/- radius + 2) that has a corner under water, with a per-vertex alpha that
// falls with the distance from the lake but is still about 1 - 2 / (radius + 2) at the box's side. Where a
// river channel crosses that side the lake's water stops there in one step, over the river's own water: a
// straight seam across the channel between the lake's slow broad ripples and the river's fast ones.
//
// The fade: the lake range's vertices on the side of its extent that are under water are where the box cut
// the water (a vertex on the mesh's shore edge is dry: a cell is meshed only when a corner is wet, so the
// corners it shares with an unmeshed cell are all dry). Every vertex of the range has its alpha byte
// multiplied by smoothstep(0, fade, distance to the nearest such cut vertex), so the lake hands over to the
// river across `fade` tiles. A range with no cut vertex is unchanged (a lake no river leaves), as is every
// river vertex. Pure: the caller uploads the copy (the saved mesh is never edited; it is serialized).
// ============================================================================

#include "Game/WorldGeometryElements.h"

#include <cmath>
#include <cstdint>
#include <vector>

namespace Ext {
namespace BetterWater {
namespace RiverMouth {

constexpr float kPosScale = 48.0f;       // water_contour_mesh.cpp: position = tile * 48
constexpr float kUnderwater = -0.75f;    // water_contour_mesh.cpp: a corner at or below is wet
constexpr float kHeightStep = 1.0f / 256.0f;
constexpr float kFadeTiles = 6.0f;       // the hand-over's width

// One lake range: {first index, base vertex, vertex count, index count} (GeometryDrawRange::values).
struct Range {
    int32_t vertexStart;
    int32_t vertexCount;
};

inline float Smooth(float t) {
    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;
    return t * t * (3.0f - 2.0f * t);
}

// The height (tiles) of the heightmap at the world tile (col, row), clamped as BuildWaterShores samples it.
inline float HeightAt(const int16_t* heights, int stride, int halfGrid, int col, int row) {
    int c = col + halfGrid;
    int r = row + halfGrid;
    if (c < 0) c = 0;
    if (c >= stride) c = stride - 1;
    if (r < 0) r = 0;
    if (r >= stride) r = stride - 1;
    return static_cast<float>(heights[static_cast<size_t>(r) * static_cast<size_t>(stride) + c]) * kHeightStep;
}

// `out` = `vertices` with every lake range's alpha faded toward its cut sides. Returns the number of vertices
// whose alpha changed (0: the copy equals the mesh and need not be drawn instead of it).
inline int Fade(const TerrainVertex* vertices, int32_t count, const Range* ranges, int32_t rangeCount,
                const int16_t* heights, int stride, int halfGrid, float fadeTiles, std::vector<TerrainVertex>& out) {
    out.assign(vertices, vertices + (count > 0 ? count : 0));
    if (heights == nullptr || stride <= 0 || fadeTiles <= 0.0f) return 0;
    int changed = 0;
    std::vector<float> cutX;
    std::vector<float> cutY;
    for (int32_t ri = 0; ri < rangeCount; ++ri) {
        const int32_t first = ranges[ri].vertexStart;
        const int32_t n = ranges[ri].vertexCount;
        if (first < 0 || n <= 0 || first > count - n) continue;
        int minX = INT32_MAX, maxX = INT32_MIN, minY = INT32_MAX, maxY = INT32_MIN;
        for (int32_t i = first; i < first + n; ++i) {
            const int x = vertices[i].position[0];
            const int y = vertices[i].position[1];
            if (x < minX) minX = x;
            if (x > maxX) maxX = x;
            if (y < minY) minY = y;
            if (y > maxY) maxY = y;
        }
        cutX.clear();
        cutY.clear();
        for (int32_t i = first; i < first + n; ++i) {
            const int x = vertices[i].position[0];
            const int y = vertices[i].position[1];
            if (x != minX && x != maxX && y != minY && y != maxY) continue;
            const int col = static_cast<int>(std::lround(static_cast<float>(x) / kPosScale));
            const int row = static_cast<int>(std::lround(static_cast<float>(y) / kPosScale));
            if (HeightAt(heights, stride, halfGrid, col, row) > kUnderwater) continue;
            cutX.push_back(static_cast<float>(x) / kPosScale);
            cutY.push_back(static_cast<float>(y) / kPosScale);
        }
        if (cutX.empty()) continue;
        const float reach2 = fadeTiles * fadeTiles;
        for (int32_t i = first; i < first + n; ++i) {
            const float x = static_cast<float>(vertices[i].position[0]) / kPosScale;
            const float y = static_cast<float>(vertices[i].position[1]) / kPosScale;
            float best = reach2;
            for (size_t k = 0; k < cutX.size(); ++k) {
                const float dx = x - cutX[k];
                const float dy = y - cutY[k];
                const float d2 = dx * dx + dy * dy;
                if (d2 < best) best = d2;
            }
            if (best >= reach2) continue;
            const float f = Smooth(std::sqrt(best) / fadeTiles);
            const uint8_t a = vertices[i].normal[2];
            const uint8_t faded = static_cast<uint8_t>(static_cast<float>(a) * f + 0.5f);
            if (faded != a) {
                out[i].normal[2] = faded;
                ++changed;
            }
        }
    }
    return changed;
}

}  // namespace RiverMouth
}  // namespace BetterWater
}  // namespace Ext
