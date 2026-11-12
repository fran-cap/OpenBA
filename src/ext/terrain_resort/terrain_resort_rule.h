// ============================================================================
// src/ext/terrain_resort/terrain_resort_rule.h -- fix.terrain_resort: the pure rule
// ============================================================================
// NOT RETAIL. docs/extensions/handins/TERRAINRESORT.md. Standard C++ only, so
// tests/unit/ext_terrain_resort_test.cpp links it without the world; the map walk
// is terrain_resort.cpp.
//
// A cell is healed when it is stored as type 10 (too steep), the classifier
// (Map::ClassifyTerrain 0x14009e2c0) now calls it 2 (buildable) from the
// current heights, and nothing claims it: no blocker, no occupant, a zero
// footprint count and no road or pending road (+0x2A == 0xFF). The reverse
// (stored 2, now steep) is left alone: a cell beside a flattened road stays
// buildable, as retail leaves it.
// ============================================================================
#pragma once

#include <cstdint>

#include "Game/PathType.h"   // PathType (standalone: <cstdint> and EnumFlags.h only)

namespace Ext {
namespace TerrainResort {

// MapCell +0x29 as the integer the classifier returns (the rule compares ints).
constexpr uint8_t kTypeBuildable = static_cast<uint8_t>(PathType::Normal);
constexpr uint8_t kTypeSteep     = static_cast<uint8_t>(PathType::Walkable);   // "mountain, below 2.0"
// MapCell +0x2A with no road: MapCell::kNoRoadBatch (Map.h is not standalone; terrain_resort.cpp asserts it).
constexpr uint8_t kNoRoad        = 0xFF;

struct CellView {
    uint8_t terrainType;      // +0x29
    uint8_t footprintCount;   // +0x28
    uint8_t road;             // +0x2A
    bool    blocked;          // +0x00 blocker set
    bool    occupied;         // +0x08 occupant set
};

// A lone occupant stands here: a tree, ore, a goods pile.
constexpr uint8_t kTypeResource = static_cast<uint8_t>(PathType::Obstacle);

// The removal re-sort (0x14009dee0 / StartTown_UnmarkResourceCellGrid): an entity leaving a
// type-4 cell no other entity's zone claims. World generation scatters trees and ore on ground up
// to height 1.0 and normal z 0.75 (their templates' windows), the classifier calls buildable only
// height <= 0.75 and normal z >= 0.9, so retail turns such a cell red when the tree goes: a road
// placeable over the tree is not placeable once it is cut. A goods pile dropped on a stored-2
// cell the classifier calls steep does the same when it is picked up. The fix keeps the cell
// buildable. Only 10 is replaced; water (7, 8) and high mountain (6) stay what the classifier
// says.
//
// `zoned` is ZonedByOther: another entity blocks the cell (a pasture's fence and a cemetery's stone
// are type 4 inside their owner's zone). The Remove tool's marker is no such owner: it blocks the one
// cell only while its resource is cleared, so stone, iron or a tree removed with the tool is a lone
// occupant too. Without that exception every resource cleared with the tool on such ground left a
// red cell, where the same resource taken by a forester or a gatherer left none.
inline bool ZonedByOther(bool blockedByOther, bool blockerIsRemovalMarker) {
    return blockedByOther && !blockerIsRemovalMarker;
}

inline int ClearedCellType(uint8_t typeBefore, int classified, bool zoned) {
    if (typeBefore != kTypeResource || zoned) return classified;
    if (classified != kTypeSteep) return classified;
    return kTypeBuildable;
}

inline bool ShouldHeal(const CellView& cell, int classified) {
    if (cell.terrainType != kTypeSteep) return false;
    if (classified != kTypeBuildable) return false;
    if (cell.blocked || cell.occupied) return false;
    if (cell.footprintCount != 0) return false;
    return cell.road == kNoRoad;
}

}  // namespace TerrainResort
}  // namespace Ext
