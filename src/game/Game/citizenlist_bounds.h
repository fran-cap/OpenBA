#pragma once
// src/game/Game/citizenlist_bounds.h
// The test CitizenBehavior::PostLoad (0x14008f580) applies to a saved residence cell before
// AddCellEntry (0x140088a80) files it in the 8x8-cell bucket grid (field_0x140, side * side
// records, side = map edge >> 3). Pure, so the unit tests reach it
// (tests/unit/review_citizenlist_bounds_test.cpp).

#include <cstdint>

namespace CitizenListBounds {

// NOT RETAIL (review S-008): the cell comes from the save (CitizenBehavior::Serialize, the
// +0x130 pairs' low int) and retail hands `cell % edge`, `cell / edge` to AddCellEntry with no
// test; its record index `(edge >> 3) * (y >> 3) + (x >> 3)` then reads, writes and frees
// through a record outside the grid. A cell is filed only when that record is in the grid.
constexpr bool IsResidenceCell(int32_t cell, int32_t mapEdge) {
    if (mapEdge <= 0 || cell < 0) {
        return false;
    }
    if (static_cast<int64_t>(cell) >= static_cast<int64_t>(mapEdge) * mapEdge) {
        return false;
    }
    const int32_t side = mapEdge >> 3;
    const int32_t x = cell % mapEdge;
    const int32_t y = cell / mapEdge;
    return (x >> 3) < side && (y >> 3) < side;
}

}  // namespace CitizenListBounds
