// tests/unit/review_citizenlist_bounds_test.cpp
// review S-008: the saved residence cells CitizenBehavior::PostLoad files in the well bucket
// grid (src/game/Game/citizenlist_bounds.h). PostLoad / AddCellEntry need a live world; they
// call this. All platforms.

#include "unit.h"
#include "Game/citizenlist_bounds.h"
#include <cstdint>

using CitizenListBounds::IsResidenceCell;

UNIT_TEST(review_citizenlist_residence_cell) {
    const int32_t edge = 256;   // side 32, 1024 records
    CHECK(IsResidenceCell(0, edge));
    CHECK(IsResidenceCell(255, edge));
    CHECK(IsResidenceCell(edge * edge - 1, edge));
    CHECK(IsResidenceCell(130 * edge + 77, edge));

    // The finding's worked example: -9 -> x = -9, record -2 (before the grid).
    CHECK(!IsResidenceCell(-9, edge));
    CHECK(!IsResidenceCell(-1, edge));
    CHECK(!IsResidenceCell(INT32_MIN, edge));
    // edge * edge -> y = edge, record side * side (one past the grid).
    CHECK(!IsResidenceCell(edge * edge, edge));
    CHECK(!IsResidenceCell(INT32_MAX, edge));

    // An edge of 0 divides by zero; a negative one indexes nothing.
    CHECK(!IsResidenceCell(0, 0));
    CHECK(!IsResidenceCell(0, -8));

    // An edge that is not a multiple of 8: its last column / row fall past side = edge >> 3.
    const int32_t odd = 260;   // side 32
    CHECK(IsResidenceCell(255, odd));
    CHECK(!IsResidenceCell(256, odd));
    CHECK(!IsResidenceCell(256 * odd, odd));
}
