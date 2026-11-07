// ============================================================================
// src/ext/winter_ice/open_mask.h -- where the ice stays open: round buildings in the water, and the
// trade boats' channel (pure)
// ============================================================================
// NOT RETAIL. docs/extensions/handins/WINTERICE.md. Plain arithmetic, no engine include (unit test:
// tests/unit/ext_winter_ice_test.cpp).
//
// The mask is one byte per map cell: 255 = open water, 0 = the ice may form. Two kinds of stamp, each a
// soft disc (full within `full` cells of its centre, falling to 0 at `reach`):
//
//   * a building in the water. A fishing dock or a trading post stands on water cells and blocks them
//     (its MapDescription's '#' cells keep their water type, terrain 7 / 8). A bridge does not count:
//     it writes a land type into its cells (MapDescription::_cellType).
//   * the trade boats' channel, while the town has a trading post: retail's boat route
//     (BoatComponent::SetDestination 0x1400456e0) sails in along the world's polyline 0
//     (WorldGeometry::_contourOutlines record 0), across the map, past the dock and out again, so the
//     in-map stretch of that line stays open, with a spur from each trading post to its nearest point.
//     Drawing only: citizens still walk where they walked, and the boats sail where they sailed.
// ============================================================================
#pragma once

#include <cmath>
#include <cstdint>
#include <vector>

namespace Ext {
namespace WinterIce {

inline constexpr float kOpenFull = 1.0f;       // cells: round a building, fully open this close
inline constexpr float kOpenReach = 3.5f;      // cells: ...no effect from here on
inline constexpr float kChannelFull = 1.46f;   // cells: the boat channel's open half width (the user's set, 2026-11-06)
inline constexpr float kChannelEdge = 2.0f;    // cells: ...and how far past it the edge closes
inline constexpr float kChannelStep = 0.5f;    // cells between the channel's stamps along a line

// A disc to stamp, in cell coordinates (a cell's centre is its index + 0.5).
struct OpenStamp {
    float x;
    float y;
    float full;
    float reach;
};

// The stamps of a polyline's segments (cell coordinates), one every kChannelStep cells, open within
// `halfWidth` cells of the line and closed kChannelEdge further out.
inline void AddChannel(const std::vector<float>& xy, std::vector<OpenStamp>& out, float halfWidth = kChannelFull) {
    for (size_t i = 0; i + 3 < xy.size(); i += 2) {
        const float x0 = xy[i];
        const float y0 = xy[i + 1];
        const float dx = xy[i + 2] - x0;
        const float dy = xy[i + 3] - y0;
        const float len = std::sqrt(dx * dx + dy * dy);
        const int steps = static_cast<int>(len / kChannelStep) + 1;
        for (int k = 0; k <= steps; ++k) {
            const float t = static_cast<float>(k) / static_cast<float>(steps);
            out.push_back(OpenStamp{x0 + dx * t, y0 + dy * t, halfWidth, halfWidth + kChannelEdge});
        }
    }
}

// `out` becomes the size * size mask of the stamps.
inline void BuildOpenMask(const std::vector<OpenStamp>& stamps, int size, std::vector<uint8_t>& out) {
    out.assign(static_cast<size_t>(size > 0 ? size : 0) * static_cast<size_t>(size > 0 ? size : 0), 0);
    if (size <= 0) return;
    for (const OpenStamp& s : stamps) {
        const int x0 = static_cast<int>(std::floor(s.x - s.reach));
        const int x1 = static_cast<int>(std::ceil(s.x + s.reach));
        const int y0 = static_cast<int>(std::floor(s.y - s.reach));
        const int y1 = static_cast<int>(std::ceil(s.y + s.reach));
        for (int y = y0 < 0 ? 0 : y0; y <= y1 && y < size; ++y) {
            for (int x = x0 < 0 ? 0 : x0; x <= x1 && x < size; ++x) {
                const float dx = static_cast<float>(x) + 0.5f - s.x;
                const float dy = static_cast<float>(y) + 0.5f - s.y;
                const float dist = std::sqrt(dx * dx + dy * dy);
                float v = 0.0f;
                if (dist <= s.full) {
                    v = 1.0f;
                } else if (dist < s.reach) {
                    v = 1.0f - (dist - s.full) / (s.reach - s.full);
                }
                const int value = static_cast<int>(v * 255.0f + 0.5f);
                uint8_t& cell = out[static_cast<size_t>(y) * static_cast<size_t>(size) + static_cast<size_t>(x)];
                if (value > cell) cell = static_cast<uint8_t>(value);
            }
        }
    }
}

// A building's blocked water cell (row * size + col) as its stamp.
inline OpenStamp BuildingStamp(int index, int size) {
    return OpenStamp{static_cast<float>(index % size) + 0.5f, static_cast<float>(index / size) + 0.5f, kOpenFull,
                     kOpenReach};
}

}  // namespace WinterIce
}  // namespace Ext
