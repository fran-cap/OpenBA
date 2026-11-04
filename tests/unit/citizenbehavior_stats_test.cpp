// ============================================================================
// tests/unit/citizenbehavior_stats_test.cpp -- CitizenBehavior's path pair order and the
// educated-percentage sample
// ============================================================================
// BuildPathGraph (0x1400d70c0) appends each (entity cell, building cell) pair as
// {max, min}: 0x1400d73d3..0x1400d73e7 store both, then swap when entityCell < buildingCell
// (review F-182). ProcessPaths passes the pair to Map::PathDistance in that order.
//
// CollectStats (0x14008a730) divides educated by adults with no zero test (0x14008a98b
// DIVSS), scales by 100 and truncates with CVTTSS2SI (0x14008a9c5): a town with no adult
// stores 0/0 = NaN -> 0x80000000 in the history series (review F-184).
// ============================================================================
#include "unit.h"

#include <cstdint>

#include "Entity/CitizenBehavior.h"

UNIT_TEST(citizenbehavior_path_pair_is_max_min) {
    const PathCellPair low = MakePathCellPair(10, 250);
    CHECK_EQ(low.cellA, 250);
    CHECK_EQ(low.cellB, 10);

    const PathCellPair high = MakePathCellPair(900, 12);
    CHECK_EQ(high.cellA, 900);
    CHECK_EQ(high.cellB, 12);

    const PathCellPair same = MakePathCellPair(7, 7);
    CHECK_EQ(same.cellA, 7);
    CHECK_EQ(same.cellB, 7);
}

UNIT_TEST(citizenbehavior_educated_percent) {
    CHECK_EQ(EducatedPercent(3, 4), 75);
    CHECK_EQ(EducatedPercent(1, 3), 33);
    CHECK_EQ(EducatedPercent(0, 5), 0);
    CHECK_EQ(EducatedPercent(5, 5), 100);
    // No adult: retail's 0/0 is NaN and CVTTSS2SI stores the integer indefinite.
    CHECK_EQ(static_cast<uint32_t>(EducatedPercent(0, 0)), 0x80000000u);
}
