#pragma once
// ============================================================================
// src/ext/river_channel/river_channel_math.h -- ext.river_channel: a least channel along thin rivers (pure)
// ============================================================================
// NOT RETAIL. docs/extensions/handins/RIVERCHANNEL.md. No engine include: unit-tested.
//
// Retail's height pipeline carves a river along each feature polyline, then erodes and smooths. Along a
// thin river what is left under the waterline is often one or two height vertices wide (a game tile is two
// height cells), or a row of separate dips:
// the water shows as the cut of the water plane through the terrain's triangles, a staircase on every
// diagonal run. The water mesh over it is several units wide; the ground under it is what is thin.
//
// The carve walks each polyline in steps of kStep. At a step where any height vertex within kHalfWidth of
// the centreline is dry (as the heights were before the carve), every vertex within kHalfWidth is lowered
// to a parabolic bed: kDepth below the waterline on the centreline, the waterline at kHalfWidth. A height is
// only ever lowered. A step whose vertices are all under the waterline already is a river at least that
// wide and is left exactly as it is, so wide rivers and lakes do not change.
// ============================================================================

#include <cmath>
#include <cstdint>
#include <vector>

namespace Ext {
namespace RiverChannel {

constexpr float kWaterline = -0.75f;   // GetUnderWaterAmount's 0 (functions.srsl): the water surface
constexpr float kHalfWidth = 2.5f;     // height vertices to each side of the centreline: five vertices across,
                                       // four height cells = two game tiles (a tile is two height cells)
constexpr float kDepth = 0.35f;        // the bed below the waterline on the centreline
constexpr float kStep = 0.25f;         // the walk along the polyline

struct Point {
    float x;   // grid coordinates: column - half, row - half (the feature polylines' own)
    float y;
};

// Lowers `heights` (edge * edge floats, row-major, world = index - half) along one polyline. `before` is the
// same grid as it was before any carve (the dry test reads it). Returns the number of heights lowered.
inline int CarvePath(float* heights, const float* before, int32_t edge, int32_t half, const Point* points,
                     int32_t count) {
    if (heights == nullptr || before == nullptr || points == nullptr || edge < 2) return 0;
    const int32_t reach = static_cast<int32_t>(std::ceil(kHalfWidth));
    int lowered = 0;
    for (int32_t i = 0; i + 1 < count; ++i) {
        const float dx = points[i + 1].x - points[i].x;
        const float dy = points[i + 1].y - points[i].y;
        const float length = std::sqrt(dx * dx + dy * dy);
        int32_t steps = static_cast<int32_t>(std::ceil(length / kStep));
        if (steps < 1) steps = 1;
        // The segment's last step is the next segment's first, except on the last segment.
        const int32_t end = i + 2 < count ? steps - 1 : steps;
        for (int32_t k = 0; k <= end; ++k) {
            const float t = static_cast<float>(k) / static_cast<float>(steps);
            const float px = points[i].x + dx * t;
            const float py = points[i].y + dy * t;
            const int32_t col0 = static_cast<int32_t>(std::floor(px)) + half;
            const int32_t row0 = static_cast<int32_t>(std::floor(py)) + half;
            bool dry = false;
            for (int pass = 0; pass < 2; ++pass) {
                // pass 0: is any vertex within the half width dry? pass 1: lower them all.
                if (pass == 1 && !dry) break;
                for (int32_t row = row0 - reach; row <= row0 + reach + 1; ++row) {
                    if (row < 0 || row >= edge) continue;
                    for (int32_t col = col0 - reach; col <= col0 + reach + 1; ++col) {
                        if (col < 0 || col >= edge) continue;
                        const float ox = static_cast<float>(col - half) - px;
                        const float oy = static_cast<float>(row - half) - py;
                        const float dist = std::sqrt(ox * ox + oy * oy);
                        if (dist >= kHalfWidth) continue;
                        const size_t at = static_cast<size_t>(row) * static_cast<size_t>(edge) + static_cast<size_t>(col);
                        if (pass == 0) {
                            if (before[at] >= kWaterline) dry = true;
                            continue;
                        }
                        const float share = dist / kHalfWidth;
                        const float bed = kWaterline - kDepth * (1.0f - share * share);
                        if (bed < heights[at]) {
                            if (heights[at] == before[at]) ++lowered;
                            heights[at] = bed;
                        }
                    }
                }
            }
        }
    }
    return lowered;
}

}  // namespace RiverChannel
}  // namespace Ext
