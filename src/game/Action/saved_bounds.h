#pragma once
// src/game/Action/saved_bounds.h
// The tests an Action applies to the values its Persist read back from a save before it
// uses them as an index or a count. Retail applies none of them (review F-105, F-106,
// F-107, F-160); each one only changes a value a valid save never holds.

#include <cstdint>

namespace ActionBounds {

// The capacity of MoveTo::path (+0x68, int32_t[0x100]).
constexpr int32_t kMovePathCapacity = 0x100;

// NOT RETAIL (review F-105, F-106): a saved cell index (CompleteRoad +0x20,
// CreateRawMaterial +0x44) names a cell of the map. Retail indexes Map::cells with it
// unchecked.
constexpr bool IsMapCell(int32_t cellIndex, int32_t cellCount) {
    return cellIndex >= 0 && cellIndex < cellCount;
}

// NOT RETAIL (review F-107): MoveTo::Persist clamps the saved path count (+0x64) to
// [0, 0x100] and returns how many saved ints are left over past the array. Retail
// 0x1400c8ec0 reads the whole count into path[0x100] (a heap overflow past 0x100).
constexpr int32_t ClampSavedPathCount(int32_t& pathCount) {
    if (pathCount < 0) {
        pathCount = 0;
        return 0;
    }
    if (pathCount > kMovePathCapacity) {
        int32_t excess = pathCount - kMovePathCapacity;
        pathCount = kMovePathCapacity;
        return excess;
    }
    return 0;
}

// NOT RETAIL (review F-107): the saved path index (+0x60) is -1 (no path) or a node
// number; Execute reads path[pathIndex] for anything below -1 (before the array).
constexpr int32_t SavedPathIndex(int32_t pathIndex) {
    if (pathIndex < -1) {
        return -1;
    }
    return pathIndex;
}

// One axis of MoveTo's world-to-cell conversion: CVTTSS2SI, then a clamp to [0, mapSize).
// NOT RETAIL (review F-160): a float outside int's range (or NaN, e.g. a saved
// MoveTo::destination) is UB to convert in C++. CVTTSS2SI answers 0x80000000 for it, which
// the clamp turns into 0; that answer is produced here without the conversion. Every
// in-range value converts exactly as before.
constexpr int WorldToCellAxis(float world, int mapSize) {
    int v = INT32_MIN;
    if (world > -2147483904.0f && world < 2147483648.0f) {
        v = static_cast<int>(world);
    }
    if (v < 0) {
        v = 0;
    }
    if (v >= mapSize) {
        v = mapSize - 1;
    }
    return v;
}

}  // namespace ActionBounds
