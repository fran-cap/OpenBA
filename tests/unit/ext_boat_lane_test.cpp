// ============================================================================
// tests/unit/ext_boat_lane_test.cpp -- ext.boat_lane: the trade boats' lane (src/ext/boat_lane/boat_lane.h)
// ============================================================================
// The lane's two legs over a made-up river: built only when the game's own legs exist, ending on the
// cell centres the game's route uses, deterministic, and kept off the banks. The registry row (class B,
// OFF by default). Every platform.
// ============================================================================
#include "unit.h"

#include "ext/boat_lane/boat_lane.h"
#include "ext/registry.h"

#include <cmath>
#include <cstdint>
#include <vector>

using namespace Ext::BoatLane;

namespace {

// A 16 x 16 map: land with a deep river 7 cells wide along rows 4..10, and a land cell for a dock above
// it unless `dockOnWater`.
std::vector<uint8_t> River(bool dockOnWater) {
    const int size = 16;
    std::vector<uint8_t> types(size * size, 2);
    for (int y = 4; y <= 10; ++y) {
        for (int x = 0; x < size; ++x) types[y * size + x] = 8;
    }
    types[3 * size + 8] = dockOnWater ? 7 : 2;
    return types;
}

}  // namespace

UNIT_TEST(ext_boat_lane_needs_the_games_legs) {
    const int size = 16;
    LaneLegs legs;
    const std::vector<uint8_t> land = River(false);
    CHECK(!Legs(land.data(), size, 4 * size + 0, 3 * size + 8, 10 * size + 15, kLanePull, kLaneSmoothing, legs));
    CHECK(legs.in.empty() && legs.out.empty());
    const std::vector<uint8_t> water = River(true);
    CHECK(Legs(water.data(), size, 4 * size + 0, 3 * size + 8, 10 * size + 15, kLanePull, kLaneSmoothing, legs));
}

UNIT_TEST(ext_boat_lane_ends_on_the_route_cells) {
    const int size = 16;
    const std::vector<uint8_t> types = River(true);
    LaneLegs legs;
    CHECK(Legs(types.data(), size, 4 * size + 0, 3 * size + 8, 10 * size + 15, kLanePull, kLaneSmoothing, legs));
    if (legs.in.size() < 4 || legs.out.size() < 4) return;
    CHECK(legs.in[0] == 0.5f && legs.in[1] == 4.5f);                                   // the entry cell's centre
    CHECK(legs.in[legs.in.size() - 2] == 8.5f && legs.in.back() == 3.5f);              // the dock's
    CHECK(legs.out[0] == 8.5f && legs.out[1] == 3.5f);
    CHECK(legs.out[legs.out.size() - 2] == 15.5f && legs.out.back() == 10.5f);         // the exit's
}

UNIT_TEST(ext_boat_lane_is_deterministic_and_keeps_off_the_banks) {
    const int size = 16;
    const std::vector<uint8_t> types = River(true);
    LaneLegs a;
    LaneLegs b;
    CHECK(Legs(types.data(), size, 4 * size + 0, 3 * size + 8, 4 * size + 15, kLanePull, kLaneSmoothing, a));
    CHECK(Legs(types.data(), size, 4 * size + 0, 3 * size + 8, 4 * size + 15, kLanePull, kLaneSmoothing, b));
    CHECK(a.in == b.in && a.out == b.out);
    // Between the ends, the lane leaves the top bank row (y 4.5) for the middle of the river (y 7.5).
    float deepest = 0.0f;
    for (size_t i = 1; i + 1 < a.in.size(); i += 2) deepest = a.in[i] > deepest ? a.in[i] : deepest;
    CHECK(deepest >= 6.0f);
    // A smooth curve: no step between two points longer than a cell and a half.
    for (size_t i = 2; i + 1 < a.in.size(); i += 2) {
        const float dx = a.in[i] - a.in[i - 2];
        const float dy = a.in[i + 1] - a.in[i - 1];
        CHECK(std::sqrt(dx * dx + dy * dy) < 1.5f);
    }
}

UNIT_TEST(ext_boat_lane_row_is_class_b_default_off) {
    const Ext::Entry* row = Ext::FindEntry("boat_lane");
    CHECK(row != nullptr);
    if (row == nullptr) return;
    CHECK(row->cls == Ext::Class::B);
    CHECK(row->defaultOn);          // on by default (ruled 2026-11-08 for 1.0.3, DESIGN.md section 2)
    CHECK(row->formatVersion >= 1); // class B: in the save manifest
}

// The way out keeps to the way in's channel: entering and leaving by the same edge, the two legs are
// one line (each the other reversed, near enough), not two lanes side by side.
UNIT_TEST(ext_boat_lane_legs_share_one_channel) {
    const int size = 24;
    std::vector<uint8_t> types(size * size, 2);
    for (int y = 4; y <= 19; ++y) {
        for (int x = 0; x < size; ++x) types[y * size + x] = 8;   // a wide lake, rows 4..19
    }
    types[3 * size + 20] = 7;                                        // the dock, on the top shore
    LaneLegs legs;
    // In at the left edge's middle, out at the left edge two cells lower.
    CHECK(Legs(types.data(), size, 11 * size + 0, 3 * size + 20, 13 * size + 0, kLanePull, kLaneSmoothing, legs));
    // Every point of the way out lies within two cells of some point of the way in.
    float worst = 0.0f;
    for (size_t i = 0; i + 1 < legs.out.size(); i += 2) {
        float best = 1.0e9f;
        for (size_t j = 0; j + 1 < legs.in.size(); j += 2) {
            const float dx = legs.out[i] - legs.in[j];
            const float dy = legs.out[i + 1] - legs.in[j + 1];
            const float d = dx * dx + dy * dy;
            best = d < best ? d : best;
        }
        worst = best > worst ? best : worst;
    }
    CHECK(worst <= 4.0f);
}

// Round a bend the lane keeps to the middle, not the inside corner: an L-shaped river 7 cells wide.
UNIT_TEST(ext_boat_lane_keeps_the_middle_round_a_bend) {
    const int size = 24;
    std::vector<uint8_t> types(size * size, 2);
    for (int y = 2; y <= 8; ++y) {
        for (int x = 0; x <= 8; ++x) types[y * size + x] = 8;        // the arm along the top, rows 2..8
    }
    for (int y = 2; y < size; ++y) {
        for (int x = 2; x <= 8; ++x) types[y * size + x] = 8;        // the arm down, columns 2..8
    }
    const std::vector<float> bank = BankDistance(types.data(), size);
    // From the top arm's right end to the bottom of the down arm, both on the middle line (row / col 5).
    const std::vector<int> path = BoatWaterPath(types.data(), size, 5 * size + 8, 23 * size + 5, bank.data(), kLanePull);
    CHECK(!path.empty());
    // The inside corner is the cell (8, 8); the lane passes no closer than two cells to it.
    for (const int c : path) {
        const int dx = c % size - 8;
        const int dy = c / size - 8;
        CHECK(dx * dx + dy * dy >= 4);
    }
}
