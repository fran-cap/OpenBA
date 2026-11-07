// ============================================================================
// src/ext/winter_ice/walk_type.h -- ext.walk_on_ice's seam: a frozen lake cell reads as type 9 to the walk
// readers
// ============================================================================
// NOT RETAIL. docs/extensions/handins/WINTERICE.md section 5.2.
//
// While a lake is walkable its cells read as terrain type 9, retail's bridge-cell type (in the walk mask
// 0xe1f and in the boats' 0x380), to the readers that decide where a walker may go, and to those only:
//   Map::FillZones / Map::FillZone (the zone flood), RegionGraph's Walkable (the region flood),
//   Map::RequestPath and Map::ValidateCell (the walk test), the route query's 0xe07 test
//   (action_pf_roots_route.cpp); the pathfinder worker gets 9 by SetCellTerrain at the freeze.
// MapCell::terrainType itself keeps the water type: fishing, placement, roads, the speed read, the boats'
// own scans, the save and both ice passes see water.
//
// The faithful path (the row off, or no walkable lake) pays one load and a null test per read: g_frozen is
// null. The set is keyed on the cell array it was built for, so a stale set never reads another map.
// Main thread only (the readers run on the main thread; the worker has its own copy of the types).
// ============================================================================
#pragma once

#include <cstddef>
#include <cstdint>

#include "Game/Map.h"   // MapCell

namespace Ext {
namespace WalkOnIce {

inline constexpr uint8_t kFrozenType = 9;   // retail's bridge-cell type: walkers cross, boats pass

extern const uint8_t* g_frozen;   // g_count bytes, 1 = a walkable frozen cell; null = none (faithful)
extern const MapCell* g_base;     // the cell array g_frozen indexes
extern int g_count;

// The terrain type `cell` (an element of the map's cell array) has for the walk readers.
inline uint8_t TypeOf(const MapCell& cell) {
    const uint8_t* frozen = g_frozen;
    if (frozen == nullptr) return cell.terrainType;
    // Compared as addresses: a cell of another array is no pointer difference the language defines.
    const uintptr_t at = reinterpret_cast<uintptr_t>(&cell);
    const uintptr_t base = reinterpret_cast<uintptr_t>(g_base);
    if (at < base) return cell.terrainType;
    const uintptr_t i = (at - base) / sizeof(MapCell);
    if (i < static_cast<uintptr_t>(g_count) && frozen[i] != 0) return kFrozenType;
    return cell.terrainType;
}

}  // namespace WalkOnIce
}  // namespace Ext
