// ============================================================================
// tests/unit/ext_cancel_refund_test.cpp -- fix.cancel_refund's pile size and ring walk
// ============================================================================
// src/ext/cancel_refund/cancel_refund_rule.h without the world: the overflow piles are
// ScatterContents' size (100 / weight, at most what is left), and the search starts are
// the on-map cells of the ring just outside the footprint, each once, clockwise.
// ============================================================================
#include "unit.h"
#include "ext/cancel_refund/cancel_refund_rule.h"

#include <set>
#include <vector>

using namespace Ext::CancelRefund;

UNIT_TEST(ext_cancel_refund_pile_size_is_scatter_size) {
    CHECK_EQ(PileSize(1, 250), 100);
    CHECK_EQ(PileSize(1, 40), 40);
    CHECK_EQ(PileSize(3, 250), 33);
    CHECK_EQ(PileSize(3, 10), 10);
    CHECK_EQ(PileSize(150, 5), 1);   // 100 / 150 = 0: still a pile of one, never a stall
    CHECK_EQ(PileSize(0, 7), 7);     // no weight: the rest in one pile
    CHECK_EQ(PileSize(2, 0), 0);
    CHECK_EQ(PileSize(2, -3), 0);
}

UNIT_TEST(ext_cancel_refund_piles_sum_to_the_overflow) {
    for (int weight : {1, 2, 3, 7, 40, 101}) {
        for (int total : {1, 33, 100, 101, 999}) {
            int left = total;
            int piles = 0;
            while (left > 0) {
                const int n = PileSize(weight, left);
                CHECK(n > 0);
                CHECK(n <= left);
                left -= n;
                ++piles;
            }
            CHECK_EQ(left, 0);
            CHECK(piles <= total);
        }
    }
}

UNIT_TEST(ext_cancel_refund_rim_is_the_outside_ring) {
    const int size = 32;
    std::vector<int> seen;
    const int r = ForEachRimCell(10, 12, 3, 2, size, [&](int cell) {
        seen.push_back(cell);
        return false;
    });
    CHECK_EQ(r, -1);
    CHECK_EQ(static_cast<int>(seen.size()), 2 * (3 + 2) + 4);
    CHECK_EQ(seen.front(), 11 * size + 9);   // above the top-left corner
    std::set<int> unique(seen.begin(), seen.end());
    CHECK_EQ(unique.size(), seen.size());
    for (int cell : seen) {
        const int col = cell % size;
        const int row = cell / size;
        const bool inside = col >= 10 && col < 13 && row >= 12 && row < 14;
        CHECK(!inside);
        const int dx = col < 10 ? 10 - col : (col >= 13 ? col - 12 : 0);
        const int dy = row < 12 ? 12 - row : (row >= 14 ? row - 13 : 0);
        CHECK_EQ(dx > dy ? dx : dy, 1);
    }
}

UNIT_TEST(ext_cancel_refund_rim_skips_off_map_and_stops) {
    const int size = 16;
    std::vector<int> seen;
    ForEachRimCell(0, 0, 2, 2, size, [&](int cell) {
        seen.push_back(cell);
        return false;
    });
    CHECK_EQ(static_cast<int>(seen.size()), 5);   // only the right and bottom sides are on the map
    for (int cell : seen) {
        CHECK(cell >= 0);
        CHECK(cell < size * size);
    }
    int calls = 0;
    const int hit = ForEachRimCell(4, 4, 2, 2, size, [&](int cell) {
        ++calls;
        return cell == 3 * size + 6;   // the top side's last cell (x0 + w, y0 - 1)
    });
    CHECK_EQ(hit, 3 * size + 6);
    CHECK_EQ(calls, 4);
}
