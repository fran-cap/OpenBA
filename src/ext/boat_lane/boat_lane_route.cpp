// ============================================================================
// src/ext/boat_lane/boat_lane_route.cpp -- ext.boat_lane: the lane as world route points (class B)
// ============================================================================
// NOT RETAIL. docs/extensions/handins/BOATLANE.md.
// ============================================================================
#include "ext/boat_lane/boat_lane_route.h"
#include "ext/boat_lane/boat_lane.h"

#include "Game/Map.h"

namespace Ext {
namespace BoatLane {

bool RoutePoints(const Map& map, int entry, int dock, int exit, std::vector<float>& worldXY) {
    worldXY.clear();
    const int32_t size = map.size;
    if (size <= 0 || map.cells.GetSize() < size * size) return false;
    std::vector<uint8_t> types(static_cast<size_t>(size) * static_cast<size_t>(size));
    const MapCell* cells = map.cells.GetData();
    for (size_t i = 0; i < types.size(); ++i) types[i] = ToUnderlying(cells[i].terrainType);
    LaneLegs legs;
    if (!Legs(types.data(), size, entry, dock, exit, kLanePull, kLaneSmoothing, legs)) return false;
    const float half = static_cast<float>(size >> 1);
    worldXY.reserve(legs.in.size() + legs.out.size());
    for (const std::vector<float>* leg : {&legs.in, &legs.out}) {
        for (size_t i = 0; i + 1 < leg->size(); i += 2) {
            worldXY.push_back((*leg)[i] - half);
            worldXY.push_back((*leg)[i + 1] - half);
        }
    }
    return true;
}

}  // namespace BoatLane
}  // namespace Ext
