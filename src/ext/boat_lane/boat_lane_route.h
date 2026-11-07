// ============================================================================
// src/ext/boat_lane/boat_lane_route.h -- ext.boat_lane: the trade boats sail the lane (class B)
// ============================================================================
// NOT RETAIL. docs/extensions/handins/BOATLANE.md. The seam is BoatComponent::SetDestination
// (boatcomponent_route.cpp, 0x1400456e0), where retail appends its two A* legs' cells: with the row on,
// the lane's points (boat_lane.h Legs, the shape ext.winter_ice keeps open) take their place.
// ============================================================================
#pragma once

#include <cstdint>
#include <vector>

class Map;

namespace Ext {
namespace BoatLane {

// The lane's two legs from `entry` to `dock` and on to `exit` (cells, row * size + col), as WORLD x, y
// pairs (cell coordinates minus the map's half edge), legs in order. False when the lane cannot be
// built; the caller then keeps retail's cells.
bool RoutePoints(const Map& map, int entry, int dock, int exit, std::vector<float>& worldXY);

}  // namespace BoatLane
}  // namespace Ext
