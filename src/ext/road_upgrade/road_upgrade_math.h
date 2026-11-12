#pragma once
// ============================================================================
// src/ext/road_upgrade/road_upgrade_math.h -- ext.road_upgrade's rules (class A; NOT RETAIL)
// ============================================================================
// docs/extensions/handins/ROADUPGRADE.md. Which cells of a dragged rectangle the tool upgrades, and which of
// the roads group's road tools is the one it upgrades to. Standard C++ and Game/PathType.h only, so the unit
// tests link it without the game (tests/unit/ext_road_upgrade_test.cpp).
// ============================================================================

#include "Game/PathType.h"

#include <cstdint>

namespace Ext {
namespace RoadUpgrade {

// MapCell::roadType of a cell with no road (MapCell::kNoRoadBatch).
constexpr uint8_t kNoRoad = 0xFF;

// The rectangle two cells span, in columns and rows of a map `size` cells wide.
struct Rect {
    int minCol = 0;
    int maxCol = -1;
    int minRow = 0;
    int maxRow = -1;

    int Width() const { return maxCol - minCol + 1; }
    int Height() const { return maxRow - minRow + 1; }
};

inline Rect RectOf(int startCell, int endCell, int size) {
    Rect r;
    if (startCell < 0 || endCell < 0 || size <= 0) {
        return r;
    }
    const int c0 = startCell % size;
    const int c1 = endCell % size;
    const int r0 = startCell / size;
    const int r1 = endCell / size;
    r.minCol = c0 < c1 ? c0 : c1;
    r.maxCol = c0 < c1 ? c1 : c0;
    r.minRow = r0 < r1 ? r0 : r1;
    r.maxRow = r0 < r1 ? r1 : r0;
    return r;
}

// Whether the tool upgrades a cell to road type `target`.
//   cellRoad    MapCell::roadType: the road on the cell, built or still queued (kNoRoad = none)
//   terrain     MapCell::terrainType
//   blocked     the cell has a blocker (a building's footprint)
// The road must be there and of a lower tier (a higher number: Map::PlaceRoadCell's own test, which makes a
// same or better road a no-op), and the cell must be one the road tool itself would lay road on: the green
// test of RoadTool::Draw (a RoadRoute type, no blocker).
inline bool Upgradable(uint8_t cellRoad, uint8_t target, PathType terrain, bool blocked) {
    if (cellRoad == kNoRoad) {
        return false;
    }
    if (!(target < cellRoad)) {
        return false;
    }
    if ((Bits(PathBitOf(terrain)) & Bits(PathBits::RoadRoute)) == 0) {
        return false;
    }
    return !blocked;
}

// Which of a toolbar group's items the tool upgrades to. `roadIndex[i]` is the road type item i's road tool
// lays (CreateRoadDescription::_index), or -1 for an item that is not a road tool. The answer is the first
// item with the lowest road type (the best road: stone is 0, dirt 1), or -1 when the group has no upgrade to
// offer: fewer than two different road types.
inline int PickTarget(const int* roadIndex, int n) {
    int best = -1;
    bool differs = false;
    for (int i = 0; i < n; ++i) {
        if (roadIndex[i] < 0) {
            continue;
        }
        if (best < 0) {
            best = i;
            continue;
        }
        if (roadIndex[i] != roadIndex[best]) {
            differs = true;
        }
        if (roadIndex[i] < roadIndex[best]) {
            best = i;
        }
    }
    return differs ? best : -1;
}

}  // namespace RoadUpgrade
}  // namespace Ext
