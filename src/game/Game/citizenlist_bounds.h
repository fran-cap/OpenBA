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
// test; AddCellEntry then reads, writes and frees through record `side * (y >> 3) + (x >> 3)`.
// The test is exactly that record's bound, so every cell retail files inside the grid is still
// filed the same way. An edge that is not a multiple of 8 is valid data (the terrain size table
// is moddable): a cell in its last partial column (x >> 3 == side) aliases into the first
// record of the next bucket row, which is inside the grid, and is filed there as retail does.
// Only the last partial row (y >> 3 == side) and the partial column of the last full row fall
// past the grid. An edge of 0 divides by zero; a negative one indexes nothing.
constexpr bool IsResidenceCell(int32_t cell, int32_t mapEdge) {
    if (mapEdge <= 0 || cell < 0) {
        return false;
    }
    if (static_cast<int64_t>(cell) >= static_cast<int64_t>(mapEdge) * mapEdge) {
        return false;
    }
    const int64_t side = mapEdge >> 3;
    const int64_t x = cell % mapEdge;
    const int64_t y = cell / mapEdge;
    const int64_t record = side * (y >> 3) + (x >> 3);
    return record < side * side;
}

}  // namespace CitizenListBounds
