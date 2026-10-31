// ============================================================================
// tests/unit/ext_foundation_piles_test.cpp -- fix.foundation_piles's drop-cell choice
// ============================================================================
// src/ext/foundation_piles/foundation_piles_rule.h without the world: the cell a pile
// is carried to is the nearest one outside the site that the caller's test accepts;
// a cell inside the site or off the map is never offered; ties go to row-major order;
// nothing within the ring limit gives -1.
// ============================================================================
#include "unit.h"
#include "ext/foundation_piles/foundation_piles_rule.h"

#include <vector>

namespace {

using namespace Ext::FoundationPiles;

constexpr int kMap = 64;

Rect Site(int x0, int y0, int w, int h) {
    Rect r;
    r.x0 = x0;
    r.y0 = y0;
    r.w = w;
    r.h = h;
    return r;
}

int Cell(int col, int row) { return row * kMap + col; }

}  // namespace

UNIT_TEST(ext_foundation_piles_edge_pile_goes_to_the_adjacent_outside_cell) {
    const Rect site = Site(10, 10, 4, 4);
    auto any = [](int, int) { return true; };
    // On the left edge, middle row: the cell straight left of it.
    CHECK_EQ(ChooseDropCell(site, 10, 11, kMap, RingsFor(site, 6), any), Cell(9, 11));
    // On the bottom edge: straight below.
    CHECK_EQ(ChooseDropCell(site, 12, 13, kMap, RingsFor(site, 6), any), Cell(12, 14));
}

UNIT_TEST(ext_foundation_piles_never_offers_a_cell_inside_the_site) {
    const Rect site = Site(20, 20, 6, 5);
    std::vector<int> offered;
    auto record = [&](int col, int row) {
        offered.push_back(Cell(col, row));
        CHECK(!site.Contains(col, row));
        return false;
    };
    CHECK_EQ(ChooseDropCell(site, 22, 22, kMap, RingsFor(site, 6), record), -1);
    CHECK(!offered.empty());
}

UNIT_TEST(ext_foundation_piles_inner_pile_takes_the_nearest_side) {
    // A 6-wide, 4-high site; a pile one cell from the top edge is nearest the top.
    const Rect site = Site(30, 30, 6, 4);
    auto any = [](int, int) { return true; };
    CHECK_EQ(ChooseDropCell(site, 33, 31, kMap, RingsFor(site, 6), any), Cell(33, 29));
}

UNIT_TEST(ext_foundation_piles_skips_refused_cells_and_falls_back_outward) {
    const Rect site = Site(10, 10, 3, 3);
    // Refuse every cell of the first column outside the site's left edge.
    auto notColumn9 = [](int col, int) { return col != 9; };
    const int got = ChooseDropCell(site, 10, 11, kMap, RingsFor(site, 6), notColumn9);
    CHECK(got >= 0);
    CHECK(got % kMap != 9);
    CHECK(!site.Contains(got % kMap, got / kMap));
    // Ring 2 around (10, 11): (10, 9), (8, 11) and (10, 13) are all two cells away;
    // row-major order takes (10, 9).
    CHECK_EQ(got, Cell(10, 9));
}

UNIT_TEST(ext_foundation_piles_ties_go_row_major) {
    // A 1x1 site: all four edge neighbours are at distance 1; the first in row-major
    // order (above) wins.
    const Rect site = Site(5, 5, 1, 1);
    auto any = [](int, int) { return true; };
    CHECK_EQ(ChooseDropCell(site, 5, 5, kMap, 3, any), Cell(5, 4));
}

UNIT_TEST(ext_foundation_piles_stays_on_the_map) {
    // A site in the map's corner: the left and top sides are off the map.
    const Rect site = Site(0, 0, 3, 3);
    auto onMap = [](int col, int row) {
        CHECK(col >= 0 && row >= 0 && col < kMap && row < kMap);
        return true;
    };
    // Rings 1 and 2 fall inside the site or off the map; ring 3's first row-major cell at
    // the smallest distance is (3, 0).
    CHECK_EQ(ChooseDropCell(site, 0, 0, kMap, RingsFor(site, 6), onMap), Cell(3, 0));
}

UNIT_TEST(ext_foundation_piles_none_within_the_rings_is_minus_one) {
    const Rect site = Site(10, 10, 2, 2);
    auto none = [](int, int) { return false; };
    CHECK_EQ(ChooseDropCell(site, 10, 10, kMap, RingsFor(site, 6), none), -1);
    CHECK_EQ(RingsFor(site, 6), 8);
}
