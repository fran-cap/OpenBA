// ============================================================================
// src/ext/custom_maps/custom_maps.cpp -- ext.custom_maps (class A), the pure functions and the state
// ============================================================================
// See custom_maps.h. Standard C++ only; no engine header.
// ============================================================================
#include "ext/custom_maps/custom_maps.h"

#include <algorithm>
#include <utility>

namespace Ext {
namespace CustomMaps {

namespace {

// Process-wide. The pending slot has one writer (the main thread's New Game dialog) before the world is
// built; the active slot is written by the terrain stage and read by the later stages of the same creation.
// Never destroyed at exit: nothing may run then that a late seam still reads.
Map*& PendingSlot() {
    static Map* slot = nullptr;
    return slot;
}

Map*& ActiveSlot() {
    static Map* slot = nullptr;
    return slot;
}

int g_coverageStart = -1;

}  // namespace

std::vector<Feature> CoverageFeatures(const Map& map) {
    std::vector<Feature> features;
    if (map.heights.size() != HeightCount(map.mapSize) || map.mapSize == 0) {
        return features;
    }
    const int half  = static_cast<int>(map.mapSize);    // the grid runs from -half to +half
    const int side  = 2 * half + 1;                     // vertices per row
    const int tiles = (2 * half + kTileSide - 1) / kTileSide;
    for (int ty = 0; ty < tiles; ++ty) {
        for (int tx = 0; tx < tiles; ++tx) {
            const int x0 = -half + tx * kTileSide;
            const int y0 = -half + ty * kTileSide;
            // The cells of the tile are x0 .. x0 + 199; their corners are the vertices x0 .. x0 + 200.
            const int xa = std::max(x0, -half);
            const int xb = std::min(x0 + kTileSide, half);
            const int ya = std::max(y0, -half);
            const int yb = std::min(y0 + kTileSide, half);
            bool wet = false;
            for (int y = ya; y <= yb && !wet; ++y) {
                const size_t row = static_cast<size_t>(y + half) * side;
                for (int x = xa; x <= xb; ++x) {
                    if (map.heights[row + (x + half)] <= kUnderwaterRaw) {
                        wet = true;
                        break;
                    }
                }
            }
            if (wet) {
                Feature f;
                f.x      = static_cast<float>(x0 + kTileSide / 2);
                f.y      = static_cast<float>(y0 + kTileSide / 2);
                f.radius = static_cast<float>(kFeatureRadius);
                features.push_back(f);
            }
        }
    }
    return features;
}

int SizeRowFor(uint32_t mapSize, const uint32_t* rowSizes, int rowCount) {
    for (int i = 0; i < rowCount; ++i) {
        if (rowSizes[i] == mapSize) {
            return i;
        }
    }
    return -1;
}

void SetPending(Map&& map) {
    Map*& slot = PendingSlot();
    delete slot;
    slot = new Map(std::move(map));
}

const Map* Pending() {
    return PendingSlot();
}

void ClearPending() {
    Map*& slot = PendingSlot();
    delete slot;
    slot = nullptr;
}

void ActivatePending() {
    ClearActive();
    ActiveSlot() = PendingSlot();
    PendingSlot() = nullptr;
}

void ClearActive() {
    Map*& slot = ActiveSlot();
    delete slot;
    slot = nullptr;
}

const Map* Active() {
    return ActiveSlot();
}

void SetCoverageStart(int first) {
    g_coverageStart = first;
}

bool IsCoverageFeature(int index) {
    return g_coverageStart >= 0 && index >= g_coverageStart;
}

int CoverageZ() {
    return g_coverageStart - kBaseZ;
}

}  // namespace CustomMaps
}  // namespace Ext
