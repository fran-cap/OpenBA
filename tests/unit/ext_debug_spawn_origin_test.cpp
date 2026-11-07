// ============================================================================
// tests/unit/ext_debug_spawn_origin_test.cpp -- fix.debug_spawn_origin's rules
// ============================================================================
// src/ext/debug_spawn_origin/debug_spawn_origin_rule.h without the world: when the spawn press
// ends at the origin, which cell holds the origin, and the nearest standable cell to it.
// ============================================================================
#include "unit.h"

#include "ext/debug_spawn_origin/debug_spawn_origin_rule.h"

#include <vector>

using Ext::DebugSpawnOrigin::IsZonedLand;
using Ext::DebugSpawnOrigin::NearestCell;
using Ext::DebugSpawnOrigin::OriginCell;
using Ext::DebugSpawnOrigin::PlaceAtOrigin;

UNIT_TEST(ext_debug_spawn_origin_only_an_empty_pass_with_the_fix) {
    CHECK(!PlaceAtOrigin(false, 0));   // retail: the pass repeats
    CHECK(PlaceAtOrigin(true, 0));
    CHECK(!PlaceAtOrigin(true, 1));    // a use node took a citizen: retail's path goes on
    CHECK(!PlaceAtOrigin(true, 4));
}

UNIT_TEST(ext_debug_spawn_origin_origin_is_the_map_centre) {
    // world (x, y) lies on cell (x + size / 2, y + size / 2): world 0 is cell size / 2
    CHECK(OriginCell(256) == 128 * 256 + 128);
    CHECK(OriginCell(4) == 2 * 4 + 2);
}

UNIT_TEST(ext_debug_spawn_origin_land_is_a_zoned_land_type) {
    CHECK(IsZonedLand(PathType::Normal, 7));
    CHECK(IsZonedLand(PathType::Walkable, 1));
    CHECK(!IsZonedLand(PathType::Normal, 0));    // no zone yet
    CHECK(!IsZonedLand(PathType::Immovable, 7));    // a footprint / mountain / water type (5-8) has no zone
    CHECK(!IsZonedLand(PathType::DeepWater, 7));
}

UNIT_TEST(ext_debug_spawn_origin_nearest_takes_the_origin_when_it_stands) {
    CHECK(NearestCell(9, [](int) { return true; }) == OriginCell(9));
}

UNIT_TEST(ext_debug_spawn_origin_nearest_goes_round_water) {
    // a 9 x 9 map, a lake of radius 2 around the centre (4, 4); land at (7, 4) and (1, 1)
    const int size = 9;
    std::vector<bool> land(size * size, false);
    land[4 * size + 7] = true;   // ring 3, distance 9
    land[1 * size + 1] = true;   // ring 3, distance 18
    land[0 * size + 4] = true;   // ring 4
    const int cell = NearestCell(size, [&](int c) { return static_cast<bool>(land[c]); });
    CHECK(cell == 4 * size + 7);
}

UNIT_TEST(ext_debug_spawn_origin_nearest_reaches_the_map_edge) {
    const int size = 8;
    const int corner = (size - 1) * size + (size - 1);
    CHECK(NearestCell(size, [&](int c) { return c == 0; }) == 0);
    CHECK(NearestCell(size, [&](int c) { return c == corner; }) == corner);
}

UNIT_TEST(ext_debug_spawn_origin_nearest_none_on_the_map) {
    CHECK(NearestCell(8, [](int) { return false; }) == -1);
    CHECK(NearestCell(0, [](int) { return true; }) == -1);
}
