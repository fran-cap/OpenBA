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

inline constexpr PathType kFrozenType = PathType::Bridge;   // retail's bridge-cell type 9: walkers cross, boats pass

extern const uint8_t* g_frozen;   // g_count bytes, 1 = a walkable frozen cell; null = none (faithful)
extern const MapCell* g_base;     // the cell array g_frozen indexes
extern int g_count;

// The ice's top, world units: the water surface, which the ice pass redraws with the water's own vertex
// program (the waterline -0.75 of GetUnderWaterAmount plus the 12 / 256 every water surface sits within,
// ext/better_water/shore_strip.h kWaterTop).
inline constexpr float kIceTopZ = -0.75f + 12.0f / 256.0f;

// THE SURFACE (WINTERICE.md 5.3 steps 4 and 5): where a walker is drawn standing on the ice. Taken with the
// walk set at the freeze, one float per cell: the cell's depth in the ice model's units (ice_model.h; -1 = not
// a walk cell). It outlives the walk set: pathing closes on the first thaw, while the drawn ice melts from
// the banks over the next weeks, so a walker still out on the lake stays on the ice wherever the thaw front
// has not reached its cell's depth, and wades (retail's height) where it has. Dropped once no ice is left.
// Drawing only: the height is the model's, no sim reads it. Null = none (faithful: one null test per walker).
extern const float* g_surface;       // g_surfaceCount depths, indexed like the cell array g_surfaceBase
extern const MapCell* g_surfaceBase;
extern int g_surfaceCount;
extern float g_surfaceThaw;          // the thaw front (0 while the lake is walkable)

// A walker's height (`z`: what retail's move tick gives it, CharacterComponent_MoveTick_1094c0: the terrain,
// floored at the description's +0x44, the lake bed under the ice), raised to the ice where the surface says
// there is ice under (x, y) (world position; the cell Map::GetTerrainPos 0x14009f3e0 picks, clamped).
inline float OnIce(const Map& map, float x, float y, float z) {
    const float* surface = g_surface;
    if (surface == nullptr || map.cells.GetData() != g_surfaceBase) return z;
    const int dim = map.size;
    int ix = static_cast<int>(x + static_cast<float>(dim >> 1));
    int iy = static_cast<int>(y + static_cast<float>(dim >> 1));
    ix = ix < 0 ? 0 : (ix >= dim ? dim - 1 : ix);
    iy = iy < 0 ? 0 : (iy >= dim ? dim - 1 : iy);
    const int64_t i = static_cast<int64_t>(dim) * iy + ix;
    if (i < 0 || i >= g_surfaceCount || !(surface[i] > g_surfaceThaw)) return z;
    return z < kIceTopZ ? kIceTopZ : z;
}

// The terrain type `cell` (an element of the map's cell array) has for the walk readers.
inline PathType TypeOf(const MapCell& cell) {
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
