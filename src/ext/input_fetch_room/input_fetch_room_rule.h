// ============================================================================
// src/ext/input_fetch_room/input_fetch_room_rule.h -- fix.input_fetch_room: the pure rule
// ============================================================================
// NOT RETAIL. docs/retail_bugs.md §25. Standard C++ only, so
// tests/unit/ext_input_fetch_room_test.cpp links it without the world.
//
// ConsumeProduce::Execute's second pass (retail 0x1400e7840, the fetch built at 0x1400e87b9,
// src/game/Work/consumeproduce.cpp) sends an empty-handed worker for a missing input with count
// -1 (0x1400e8785 OR R12D,-1): PickupStoredRawMaterial (0x1400ca520) then loads as much as the
// worker can carry, and DropOffRawMaterial (0x1400c7620) delivers ALL of it or nothing, because
// StorageComponent::CanAdd (0x14012cd80) admits a whole stack only. Nothing compares the load with
// the workplace's free room. A workplace with less room than one load refuses it, the drop-off
// carries it to the nearest other store, and the next pass sends the worker for it again.
// With the row on, the fetch asks for what the workplace can take (and the worker can carry);
// when that is nothing, the input is not fetched.
// ============================================================================
#pragma once

namespace Ext {
namespace InputFetchRoom {

// Retail's count: "as much as the worker can carry" (the Pickup ctor's count < 1 arm).
constexpr int kRetailCount = -1;

// The count the second pass hands the fetch for an input of unit weight `weight`, with the
// workplace storage at `destStored` of `destCapacity` and the worker's at `carryStored` of
// `carryCapacity` (QueryCount and capacity +0x38, all in weight units). Off: retail's -1.
// On: the units both can take; 0 means "do not fetch this input".
inline int FetchCount(bool fixOn, int destCapacity, int destStored, int carryCapacity, int carryStored,
                      int weight) {
    if (!fixOn) return kRetailCount;
    if (weight <= 0) return kRetailCount;
    int destRoom = (destCapacity - destStored) / weight;
    int carryRoom = (carryCapacity - carryStored) / weight;
    int count = destRoom < carryRoom ? destRoom : carryRoom;
    return count > 0 ? count : 0;
}

}  // namespace InputFetchRoom
}  // namespace Ext
