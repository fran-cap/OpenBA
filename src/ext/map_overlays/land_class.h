#pragma once
// ============================================================================
// src/ext/map_overlays/land_class.h -- ext.map_overlays: the Land view's rule for one tile
// ============================================================================
// NOT RETAIL. docs/extensions/handins/MAPOVERLAYS.md §2. Standard C++ and Game/PathType.h only: unit tested
// (tests/unit/ext_map_overlays_math_test.cpp).
//
// The rule is the placement tool's own test of one footprint cell, Map::TestPlacement (0x14009f5d0), for an
// ordinary building: the cell's PathType is in the building's place bits, nothing blocks it
// (MapCell::blocker) and no building's yard lies on it (MapCell::footprintCount). The place bits are a
// building's own data; nearly every building's are Normal | Obstacle (houses, storage, workshops, fields,
// pastures), so that is the set the view shows. The buildings that differ are the ones that need the other
// ground: docks and bridges reach onto water, mines and quarries sit on the hillside.
//
// What a building cannot stand on is split in two: ground that never takes one (water, steep ground,
// mountain: Unbuildable) and ground something already stands on (None: the map draws the building, road or
// field itself, so the view leaves it clear).
// ============================================================================
#include <cstdint>

#include "Game/PathType.h"
#include "ext/map_overlays/overlay_core.h"

namespace Ext {
namespace MapOverlays {

inline LandClass ClassifyLand(PathType type, bool blocked, uint8_t footprintCount) {
    if (blocked || footprintCount != 0) {
        return LandClass::None;
    }
    switch (type) {
        case PathType::Normal:
            return LandClass::Open;
        case PathType::Obstacle:
            return LandClass::NeedsClearing;
        case PathType::Unusable:
        case PathType::Water:
        case PathType::DeepWater:
        case PathType::Walkable:
            return LandClass::Unbuildable;
        default:
            return LandClass::None;   // roads, field / yard ground, a pit, a bridge deck, a tunnel floor
    }
}

}  // namespace MapOverlays
}  // namespace Ext
