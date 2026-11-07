// ============================================================================
// src/ext/winter_ice/walk_cells.h -- which cells of a frozen lake walkers may cross (pure)
// ============================================================================
// NOT RETAIL. docs/extensions/handins/WINTERICE.md section 5.1. Plain arithmetic, no engine include: the unit
// test (tests/unit/ext_winter_ice_test.cpp) builds made-up maps.
//
// The water mesh tells a lake from a river: WorldGeometry::_contourRangesB (mode 0x8000, BuildWaterShores)
// is the lakes, one quad per cell; _contourRangesA (mode 0x6000, BuildWaterBodies) is the river strips. The
// caller hands both over as triangles in cell coordinates (a cell's centre is its index + 0.5).
//
//   lake   a cell whose centre lies in a lake triangle
//   river  a cell whose centre lies in a river triangle, widened by kRiverMargin cells (the strip is
//          narrower than the water it runs in: without the margin the set would creep up a river's banks)
//   walk   a water cell (terrain 7 / 8), not river, not open (the open-water mask below one half: a
//          dock's hole, the boat channel), that is a lake cell or lies within kRimReach steps (4-way)
//          of one through such cells: the shallow rim the lake quads leave out, so a walker can step
//          from the bank onto the ice
// A cell under both a lake and a river is river (the user's ruling: rivers never walkable).
// ============================================================================
#pragma once

#include <cmath>
#include <cstdint>
#include <deque>
#include <vector>

#include "Game/PathType.h"   // PathType (standalone: <cstdint> and EnumFlags.h only)

namespace Ext {
namespace WinterIce {

inline constexpr int kRiverMargin = 1;   // cells added round the river strips
inline constexpr int kRimReach = 2;      // steps from a lake cell into the uncovered rim
inline constexpr uint8_t kOpenWalkLimit = 128;   // an open-mask value at or over this is open water

inline bool WalkWater(uint8_t terrainType) {
    const PathType t = static_cast<PathType>(terrainType);
    return t == PathType::Water || t == PathType::DeepWater;
}

// Marks the cells whose centre lies in any triangle of `tris` (x0 y0 x1 y1 x2 y2 per triangle, cell units).
inline void MarkCentres(const std::vector<float>& tris, int size, std::vector<uint8_t>& out) {
    for (size_t t = 0; t + 5 < tris.size(); t += 6) {
        const float ax = tris[t];
        const float ay = tris[t + 1];
        const float bx = tris[t + 2];
        const float by = tris[t + 3];
        const float cx = tris[t + 4];
        const float cy = tris[t + 5];
        const float area = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
        if (!(std::fabs(area) > 1e-6f)) continue;   // degenerate (a quad taken out), NaN
        const int x0 = static_cast<int>(std::floor(std::fmin(ax, std::fmin(bx, cx)) - 0.5f));
        const int x1 = static_cast<int>(std::ceil(std::fmax(ax, std::fmax(bx, cx)) - 0.5f));
        const int y0 = static_cast<int>(std::floor(std::fmin(ay, std::fmin(by, cy)) - 0.5f));
        const int y1 = static_cast<int>(std::ceil(std::fmax(ay, std::fmax(by, cy)) - 0.5f));
        for (int y = y0 < 0 ? 0 : y0; y <= y1 && y < size; ++y) {
            for (int x = x0 < 0 ? 0 : x0; x <= x1 && x < size; ++x) {
                const float px = static_cast<float>(x) + 0.5f;
                const float py = static_cast<float>(y) + 0.5f;
                const float w0 = (bx - ax) * (py - ay) - (by - ay) * (px - ax);
                const float w1 = (cx - bx) * (py - by) - (cy - by) * (px - bx);
                const float w2 = (ax - cx) * (py - cy) - (ay - cy) * (px - cx);
                const bool inside = area > 0.0f ? (w0 >= 0.0f && w1 >= 0.0f && w2 >= 0.0f)
                                                : (w0 <= 0.0f && w1 <= 0.0f && w2 <= 0.0f);
                if (inside) out[static_cast<size_t>(y) * static_cast<size_t>(size) + static_cast<size_t>(x)] = 1;
            }
        }
    }
}

// The walkable set: size * size bytes, 1 = the cell is walkable ice while the lake is solid. `types` and
// `open` are size * size (open may be empty: no open water).
inline std::vector<uint8_t> WalkCells(int size, const uint8_t* types, const std::vector<float>& lakeTris,
                                      const std::vector<float>& riverTris, const std::vector<uint8_t>& open) {
    std::vector<uint8_t> walk;
    if (size <= 0 || types == nullptr) return walk;
    const size_t n = static_cast<size_t>(size) * static_cast<size_t>(size);
    walk.assign(n, 0);
    std::vector<uint8_t> lake(n, 0);
    std::vector<uint8_t> riverCore(n, 0);
    MarkCentres(lakeTris, size, lake);
    MarkCentres(riverTris, size, riverCore);
    std::vector<uint8_t> river(n, 0);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            if (!riverCore[static_cast<size_t>(y) * static_cast<size_t>(size) + static_cast<size_t>(x)]) continue;
            for (int dy = -kRiverMargin; dy <= kRiverMargin; ++dy) {
                for (int dx = -kRiverMargin; dx <= kRiverMargin; ++dx) {
                    const int xx = x + dx;
                    const int yy = y + dy;
                    if (xx < 0 || yy < 0 || xx >= size || yy >= size) continue;
                    river[static_cast<size_t>(yy) * static_cast<size_t>(size) + static_cast<size_t>(xx)] = 1;
                }
            }
        }
    }
    auto candidate = [&](size_t i) {
        return WalkWater(types[i]) && !river[i] && (open.size() != n || open[i] < kOpenWalkLimit);
    };
    // Breadth first from every lake cell, at most kRimReach steps into the rim.
    std::vector<int> depth(n, -1);
    std::deque<size_t> queue;
    for (size_t i = 0; i < n; ++i) {
        if (lake[i] && candidate(i)) {
            depth[i] = 0;
            walk[i] = 1;
            queue.push_back(i);
        }
    }
    while (!queue.empty()) {
        const size_t i = queue.front();
        queue.pop_front();
        if (depth[i] >= kRimReach) continue;
        const int x = static_cast<int>(i % static_cast<size_t>(size));
        const int y = static_cast<int>(i / static_cast<size_t>(size));
        const int dx[4] = {1, -1, 0, 0};
        const int dy[4] = {0, 0, 1, -1};
        for (int k = 0; k < 4; ++k) {
            const int xx = x + dx[k];
            const int yy = y + dy[k];
            if (xx < 0 || yy < 0 || xx >= size || yy >= size) continue;
            const size_t j = static_cast<size_t>(yy) * static_cast<size_t>(size) + static_cast<size_t>(xx);
            if (depth[j] >= 0 || !candidate(j)) continue;
            depth[j] = depth[i] + 1;
            walk[j] = 1;
            queue.push_back(j);
        }
    }
    return walk;
}

}  // namespace WinterIce
}  // namespace Ext
