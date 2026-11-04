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

    // An edge of 0 divides by zero; a negative one indexes nothing; one below 8 has side 0.
    CHECK(!IsResidenceCell(0, 0));
    CHECK(!IsResidenceCell(0, -8));
    CHECK(!IsResidenceCell(0, 7));
}

// The exact boundary on an edge that is not a multiple of 8 (260: side 32, 1024 records).
// The last partial column (x = 256..259, x >> 3 == side) aliases into the next bucket row's
// first record, which retail fills in bounds; only records >= side * side are refused.
UNIT_TEST(review_citizenlist_residence_cell_odd_edge) {
    const int32_t odd = 260;
    CHECK(IsResidenceCell(255, odd));               // x 255, y 0 -> record 31
    CHECK(IsResidenceCell(256, odd));               // x 256, y 0 -> record 32 (row 1, bucket 0)
    CHECK(IsResidenceCell(259, odd));               // x 259, y 0 -> record 32
    CHECK(IsResidenceCell(247 * odd + 259, odd));   // y >> 3 = 30, x >> 3 = 32 -> record 992
    CHECK(IsResidenceCell(255 * odd + 255, odd));   // y >> 3 = 31, x >> 3 = 31 -> record 1023

    // The partial column of the last full row: y >> 3 = 31, x >> 3 = 32 -> record 1024.
    CHECK(!IsResidenceCell(248 * odd + 256, odd));
    CHECK(!IsResidenceCell(255 * odd + 259, odd));
    // The last partial row: y = 256 (y >> 3 = 32) -> record 1024 at x = 0.
    CHECK(!IsResidenceCell(256 * odd, odd));
    CHECK(!IsResidenceCell(odd * odd - 1, odd));
}
