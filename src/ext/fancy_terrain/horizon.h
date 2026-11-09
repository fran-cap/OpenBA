#pragma once
// ============================================================================
// src/ext/fancy_terrain/horizon.h -- ext.fancy_terrain's horizon map (NOT RETAIL)
// ============================================================================
// The terrain's self-shadowing and sky visibility come from one precomputed table: for every heightmap
// vertex, the elevation angle of the horizon in kDirections compass directions (horizon mapping). The
// pass (the ground-draw seam, like ext.better_clouds' cloud shadow) reads it per pixel:
//   sun shadow      the sun is below the horizon in its own direction (interpolated between the two
//                   nearest directions, a soft edge around the angle)
//   sky visibility  the mean over the directions of cos^2(horizon): the share of the open sky each
//                   azimuth slice still sees (valleys and the foot of a cliff are darker)
// so a moving sun (a day / night cycle) needs no rebuild, only a terrain edit does.
//
// Pure: no engine types, no allocation in the inner loop. The heightmap is WorldGeometry's (+0xD8): int16
// words, `stride` per row, world height = word * heightScale (1 / 256), one world unit between
// vertices. A terrain edit (WorldGeometry::MarkChunksDirty: quarries, terraforming, the terrain tool)
// changes the horizon of every vertex within `radius` of it, so a rebuild covers the edited rectangle
// grown by the radius (Grow). Compute over any rectangle gives the same bytes as a full build there.
//
// Encoding: one byte per direction, the SIGNED elevation angle of the highest point searched, (angle /
// (pi / 2) + 1) / 2 * 255 rounded: 0 = straight down, 127.5 = level, 255 = straight up. Signed, because the
// pass measures the sky term against the ground's own slope (terrain_light.hlsl): on an incline the downhill
// horizon is below level, and a clamped 0 there would read as an obstruction once the slope is taken off.
// A direction with no vertex to search (the map's edge) stores level. Direction k points at angle
// k * 360 / kDirections from +x (world east), counter-clockwise; plane 0 holds directions 0..3 in RGBA,
// plane 1 directions 4..7.
// ============================================================================

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace Ext {
namespace FancyTerrain {
namespace Horizon {

constexpr int kDirections = 8;

// The integer step of direction k: axis and diagonal steps land on vertices, no interpolation.
inline void DirectionStep(int k, int* dx, int* dy) {
    static const int kDx[kDirections] = { 1, 1, 0, -1, -1, -1, 0, 1 };
    static const int kDy[kDirections] = { 0, 1, 1, 1, 0, -1, -1, -1 };
    *dx = kDx[k];
    *dy = kDy[k];
}

// The step counts searched along a direction: every step near the vertex, then sparser (the horizon
// of a distant ridge changes slowly with distance), up to `radius` steps.
inline std::vector<int> SearchSteps(int radius) {
    std::vector<int> steps;
    int s = 1;
    int stride = 1;
    while (s <= radius) {
        steps.push_back(s);
        if (s >= 16 * stride) stride *= 2;
        s += stride;
    }
    return steps;
}

inline uint8_t EncodeAngle(float radians) {
    const float kHalfPi = 1.57079632679f;
    float v = (radians / kHalfPi + 1.0f) * 0.5f * 255.0f + 0.5f;
    if (v < 0.0f) v = 0.0f;
    if (v > 255.0f) v = 255.0f;
    return static_cast<uint8_t>(v);
}

inline float DecodeAngle(uint8_t b) {
    return (static_cast<float>(b) / 255.0f * 2.0f - 1.0f) * 1.57079632679f;
}

// A rectangle of vertices, [x0, x1) x [y0, y1).
struct Rect {
    int x0 = 0;
    int y0 = 0;
    int x1 = 0;
    int y1 = 0;
    bool Empty() const { return x1 <= x0 || y1 <= y0; }
};

// `r` grown by `by` on every side, clipped to the width x height grid.
inline Rect Grow(Rect r, int by, int width, int height) {
    r.x0 = std::max(0, r.x0 - by);
    r.y0 = std::max(0, r.y0 - by);
    r.x1 = std::min(width, r.x1 + by);
    r.y1 = std::min(height, r.y1 + by);
    return r;
}

// The two planes of a width x height grid, RGBA8 each (4 bytes per vertex, row-major, no padding).
struct Map {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> plane[2];

    void Resize(int w, int h) {
        width = w;
        height = h;
        plane[0].assign(static_cast<size_t>(w) * h * 4, 0);
        plane[1].assign(static_cast<size_t>(w) * h * 4, 0);
    }
    uint8_t At(int x, int y, int k) const {
        return plane[k >> 2][(static_cast<size_t>(y) * width + x) * 4 + (k & 3)];
    }
};

// Fills `map` over `rect` from the heightmap (width x height vertices, `stride` words per row).
// `steps` from SearchSteps(radius).
inline void Compute(const int16_t* heights, int stride, float heightScale, const std::vector<int>& steps,
                    Rect rect, Map* map) {
    const int w = map->width;
    const int h = map->height;
    rect = Grow(rect, 0, w, h);
    for (int y = rect.y0; y < rect.y1; ++y) {
        for (int x = rect.x0; x < rect.x1; ++x) {
            const float z0 = static_cast<float>(heights[static_cast<int64_t>(y) * stride + x]) * heightScale;
            const size_t at = (static_cast<size_t>(y) * w + x) * 4;
            for (int k = 0; k < kDirections; ++k) {
                int dx = 0;
                int dy = 0;
                DirectionStep(k, &dx, &dy);
                const float stepLength = (dx != 0 && dy != 0) ? 1.41421356f : 1.0f;
                float best = 0.0f;   // the largest rise / run seen: tan of the horizon angle
                bool any = false;
                for (int s : steps) {
                    const int sx = x + dx * s;
                    const int sy = y + dy * s;
                    if (sx < 0 || sy < 0 || sx >= w || sy >= h) break;   // off the map: open sky
                    const float z = static_cast<float>(heights[static_cast<int64_t>(sy) * stride + sx]) * heightScale;
                    const float t = (z - z0) / (static_cast<float>(s) * stepLength);
                    if (!any || t > best) best = t;
                    any = true;
                }
                map->plane[k >> 2][at + (k & 3)] = EncodeAngle(std::atan(best));
            }
        }
    }
}

// The pass's sky term at vertex (x, y), on the CPU (terrain_light.hlsl, for the tests): each direction's
// horizon taken relative to the ground's own slope there (the central difference over one vertex each way),
// never below it, then the mean of cos^2 over the directions. 1 = the whole sky a surface of that slope
// sees; a plain incline is 1, a hollow less.
inline float SkyVisibility(const Map& map, const int16_t* heights, int stride, float heightScale, int x, int y) {
    auto h = [&](int cx, int cy) {
        cx = std::min(std::max(cx, 0), map.width - 1);
        cy = std::min(std::max(cy, 0), map.height - 1);
        return static_cast<float>(heights[static_cast<int64_t>(cy) * stride + cx]) * heightScale;
    };
    const float gx = (h(x + 1, y) - h(x - 1, y)) * 0.5f;
    const float gy = (h(x, y + 1) - h(x, y - 1)) * 0.5f;
    float vis = 0.0f;
    for (int k = 0; k < kDirections; ++k) {
        const float a = static_cast<float>(k) * 0.78539816f;
        const float slope = std::atan(gx * std::cos(a) + gy * std::sin(a));
        const float rel = std::max(DecodeAngle(map.At(x, y, k)) - slope, 0.0f);
        vis += std::cos(rel) * std::cos(rel);
    }
    return vis / static_cast<float>(kDirections);
}

}  // namespace Horizon
}  // namespace FancyTerrain
}  // namespace Ext
