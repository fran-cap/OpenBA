// ============================================================================
// tests/unit/ext_river_channel_test.cpp -- ext.river_channel's carve
// ============================================================================
// src/ext/river_channel/river_channel_math.h without the game.
// ============================================================================
#include "unit.h"
#include "ext/river_channel/river_channel_math.h"
#include "ext/registry.h"

#include <vector>

using namespace Ext::RiverChannel;

namespace {

constexpr int32_t kEdge = 41;
constexpr int32_t kHalf = 20;

size_t At(int32_t x, int32_t y) {
    return static_cast<size_t>(y + kHalf) * kEdge + static_cast<size_t>(x + kHalf);
}

std::vector<float> Flat(float height) {
    return std::vector<float>(static_cast<size_t>(kEdge) * kEdge, height);
}

}  // namespace

UNIT_TEST(ext_river_channel_registry_row_is_class_b_and_off) {
    const Ext::Entry* e = Ext::FindEntry("river_channel");
    CHECK(e != nullptr);
    if (e == nullptr) return;
    CHECK(e->cls == Ext::Class::B);
    CHECK(!e->defaultOn);
}

UNIT_TEST(ext_river_channel_a_dry_run_gets_a_channel_two_tiles_wide) {
    std::vector<float> h = Flat(0.5f);
    const std::vector<float> before(h);
    const Point line[2] = {{-10.0f, 0.0f}, {10.0f, 0.0f}};
    const int lowered = CarvePath(h.data(), before.data(), kEdge, kHalf, line, 2);
    CHECK(lowered > 0);
    for (int32_t x = -8; x <= 8; ++x) {
        CHECK(h[At(x, 0)] < kWaterline - kDepth + 1e-3f);   // the centreline is the deepest
        CHECK(h[At(x, 1)] < h[At(x, 2)]);            // the bed rises toward the banks
        CHECK(h[At(x, 2)] < kWaterline);             // two vertices to each side are under water: four cells
        CHECK(h[At(x, -2)] < kWaterline);
        CHECK(h[At(x, 3)] == 0.5f);                  // the bank is untouched
        CHECK(h[At(x, -3)] == 0.5f);
    }
}

UNIT_TEST(ext_river_channel_a_diagonal_run_has_no_cornerwise_only_vertex) {
    std::vector<float> h = Flat(0.5f);
    const std::vector<float> before(h);
    const Point line[2] = {{-10.0f, -10.0f}, {10.0f, 10.0f}};
    CarvePath(h.data(), before.data(), kEdge, kHalf, line, 2);
    for (int32_t k = -8; k <= 8; ++k) {
        CHECK(h[At(k, k)] < kWaterline);
        CHECK(h[At(k + 1, k)] < kWaterline);   // wet beside it, not only at its corners
        CHECK(h[At(k, k + 1)] < kWaterline);
    }
}

UNIT_TEST(ext_river_channel_a_wide_river_is_left_exactly_as_it_is) {
    // Shallow water five vertices (two tiles) wide: every vertex within the half width is wet already.
    std::vector<float> h = Flat(0.5f);
    for (int32_t x = -kHalf; x <= kHalf; ++x) {
        for (int32_t y = -2; y <= 2; ++y) {
            h[At(x, y)] = kWaterline - 0.05f;
        }
    }
    const std::vector<float> before(h);
    const Point line[2] = {{-10.0f, 0.0f}, {10.0f, 0.0f}};
    const int lowered = CarvePath(h.data(), before.data(), kEdge, kHalf, line, 2);
    CHECK(lowered == 0);
    CHECK(h == before);
}

UNIT_TEST(ext_river_channel_only_lowers) {
    // A deep one-wide trench: it is widened, and its deep centre is kept.
    std::vector<float> h = Flat(0.5f);
    for (int32_t x = -kHalf; x <= kHalf; ++x) {
        h[At(x, 0)] = -3.0f;
    }
    const std::vector<float> before(h);
    const Point line[2] = {{-10.0f, 0.0f}, {10.0f, 0.0f}};
    CarvePath(h.data(), before.data(), kEdge, kHalf, line, 2);
    for (size_t i = 0; i < h.size(); ++i) {
        CHECK(h[i] <= before[i]);
    }
    CHECK(h[At(0, 0)] == -3.0f);
    CHECK(h[At(0, 1)] < kWaterline);
}

UNIT_TEST(ext_river_channel_a_path_at_the_map_edge_stays_inside_the_grid) {
    std::vector<float> h = Flat(0.5f);
    const std::vector<float> before(h);
    const Point line[3] = {{-25.0f, -20.0f}, {0.0f, -20.0f}, {25.0f, 20.0f}};
    const int lowered = CarvePath(h.data(), before.data(), kEdge, kHalf, line, 3);
    CHECK(lowered > 0);
    CHECK(h[At(-20, -20)] < kWaterline);
}
