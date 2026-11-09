// ============================================================================
// tests/unit/ext_fancy_terrain_horizon_test.cpp -- ext.fancy_terrain's horizon map
// (src/ext/fancy_terrain/horizon.h)
// ============================================================================
// Flat ground sees a level horizon; a wall gives atan(rise / run) toward it and a level one away from it; an
// incline's horizon is signed (up the slope above level, down it below), and the sky term taken against the
// slope sees the whole sky on a plain incline and less in a hollow; a rebuild of an edited rectangle grown
// by the radius matches a full build byte for byte. The pass that reads the map needs the running game.
// ============================================================================
#include "unit.h"
#include "ext/fancy_terrain/horizon.h"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <vector>

namespace {

using namespace Ext::FancyTerrain::Horizon;

constexpr int kSize = 48;
constexpr float kScale = 1.0f / 256.0f;

Map Build(const std::vector<int16_t>& heights, int radius) {
    Map m;
    m.Resize(kSize, kSize);
    Compute(heights.data(), kSize, kScale, SearchSteps(radius), Rect{ 0, 0, kSize, kSize }, &m);
    return m;
}

}  // namespace

UNIT_TEST(ext_fancy_terrain_horizon_flat_ground_is_open) {
    const std::vector<int16_t> flat(kSize * kSize, static_cast<int16_t>(3 * 256));
    const Map m = Build(flat, 32);
    bool allLevel = true;
    for (int y = 0; y < kSize; ++y)
        for (int x = 0; x < kSize; ++x)
            for (int k = 0; k < kDirections; ++k)
                if (std::fabs(DecodeAngle(m.At(x, y, k))) > 0.01f) allLevel = false;
    CHECK(allLevel);
    CHECK(std::fabs(SkyVisibility(m, flat.data(), kSize, kScale, 20, 20) - 1.0f) < 1e-4f);
}

UNIT_TEST(ext_fancy_terrain_horizon_wall_east) {
    // A wall 8 units high on column 30: from (20, 24) the east horizon is atan(8 / 10).
    std::vector<int16_t> h(kSize * kSize, 0);
    for (int y = 0; y < kSize; ++y) h[y * kSize + 30] = static_cast<int16_t>(8 * 256);
    const Map m = Build(h, 32);
    const float east = DecodeAngle(m.At(20, 24, 0));
    CHECK(std::fabs(east - std::atan(8.0f / 10.0f)) < 0.01f);
    CHECK(std::fabs(DecodeAngle(m.At(20, 24, 4))) < 0.01f);   // west: level
    CHECK(std::fabs(DecodeAngle(m.At(20, 24, 2))) < 0.01f);   // north: along the wall, level
    CHECK(m.At(31, 24, 4) > m.At(40, 24, 4));                 // west of the far side: nearer is steeper
    // The wall top looks down to the east: the highest point is the farthest searched (16 out: the next step, 18, is off the map).
    CHECK(std::fabs(DecodeAngle(m.At(30, 24, 0)) - std::atan(-8.0f / 16.0f)) < 0.01f);
    CHECK(SkyVisibility(m, h.data(), kSize, kScale, 27, 24) < 0.9f);   // near the foot of the wall: less sky
}

UNIT_TEST(ext_fancy_terrain_horizon_incline_is_open_sky) {
    // A plain incline rising 0.5 per vertex to the east: up the slope the horizon is atan(0.5), down it
    // -atan(0.5); against its own slope the surface sees the whole sky (the old flat-ground measure darkened
    // every slope just for being one).
    std::vector<int16_t> h(kSize * kSize);
    for (int y = 0; y < kSize; ++y)
        for (int x = 0; x < kSize; ++x) h[y * kSize + x] = static_cast<int16_t>(x * 128);
    const Map m = Build(h, 16);
    CHECK(std::fabs(DecodeAngle(m.At(24, 24, 0)) - std::atan(0.5f)) < 0.01f);
    CHECK(std::fabs(DecodeAngle(m.At(24, 24, 4)) + std::atan(0.5f)) < 0.01f);
    CHECK(SkyVisibility(m, h.data(), kSize, kScale, 24, 24) > 0.999f);
}

UNIT_TEST(ext_fancy_terrain_horizon_hollow_sees_less_sky) {
    // A bowl: heights rise with the distance from (24, 24); its floor sees less sky than a plain incline.
    std::vector<int16_t> h(kSize * kSize);
    for (int y = 0; y < kSize; ++y)
        for (int x = 0; x < kSize; ++x) {
            const float dx = static_cast<float>(x - 24);
            const float dy = static_cast<float>(y - 24);
            h[y * kSize + x] = static_cast<int16_t>((dx * dx + dy * dy) * 0.05f * 256.0f);
        }
    const Map m = Build(h, 16);
    const float floor = SkyVisibility(m, h.data(), kSize, kScale, 24, 24);
    CHECK(floor < 0.95f);
    CHECK(floor > 0.0f);
}

UNIT_TEST(ext_fancy_terrain_horizon_search_steps_reach_radius) {
    const std::vector<int> s = SearchSteps(64);
    CHECK(s.front() == 1);
    CHECK(s.back() <= 64);
    CHECK(s.back() > 48);
    for (size_t i = 1; i < s.size(); ++i) CHECK(s[i] > s[i - 1]);
    CHECK(SearchSteps(0).empty());
}

UNIT_TEST(ext_fancy_terrain_horizon_partial_rebuild_matches_full) {
    // A random-ish terrain, then a raised block (a quarry or the terrain tool): rebuilding the edited
    // rectangle grown by the radius gives exactly the full build.
    std::vector<int16_t> h(kSize * kSize);
    unsigned seed = 12345u;
    for (auto& v : h) {
        seed = seed * 1103515245u + 12345u;
        v = static_cast<int16_t>((seed >> 16) % (6 * 256));
    }
    const int radius = 12;
    Map m = Build(h, radius);
    const Rect edit{ 20, 18, 26, 22 };
    for (int y = edit.y0; y < edit.y1; ++y)
        for (int x = edit.x0; x < edit.x1; ++x) h[y * kSize + x] = static_cast<int16_t>(20 * 256);
    Compute(h.data(), kSize, kScale, SearchSteps(radius), Grow(edit, radius, kSize, kSize), &m);
    const Map full = Build(h, radius);
    CHECK(m.plane[0] == full.plane[0]);
    CHECK(m.plane[1] == full.plane[1]);
}

UNIT_TEST(ext_fancy_terrain_horizon_grow_clips) {
    const Rect r = Grow(Rect{ 2, 3, 10, 12 }, 5, 16, 14);
    CHECK(r.x0 == 0);
    CHECK(r.y0 == 0);
    CHECK(r.x1 == 15);
    CHECK(r.y1 == 14);
}
