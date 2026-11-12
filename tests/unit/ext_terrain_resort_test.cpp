// ============================================================================
// tests/unit/ext_terrain_resort_test.cpp -- fix.terrain_resort's heal rule
// ============================================================================
// src/ext/terrain_resort/terrain_resort_rule.h without the world: only a free
// cell stored "too steep" (10) that the classifier now calls buildable (2) is
// healed; a claimed cell, a road, another type or another class is left alone,
// and a buildable cell is never demoted.
// ============================================================================
#include "unit.h"
#include "ext/terrain_resort/terrain_resort_rule.h"

#include <cstdint>
#include <initializer_list>

namespace {

using namespace Ext::TerrainResort;

CellView FreeSteep() {
    CellView cell;
    cell.terrainType    = kTypeSteep;
    cell.footprintCount = 0;
    cell.road           = kNoRoad;
    cell.blocked        = false;
    cell.occupied       = false;
    return cell;
}

}  // namespace

UNIT_TEST(ext_terrain_resort_heals_free_steep_cell_now_flat) {
    CHECK(ShouldHeal(FreeSteep(), kTypeBuildable));
}

UNIT_TEST(ext_terrain_resort_keeps_cells_still_steep_or_other_class) {
    for (int classified : {6, 7, 8, 10}) {
        CHECK(!ShouldHeal(FreeSteep(), classified));
    }
}

UNIT_TEST(ext_terrain_resort_never_demotes_or_touches_other_types) {
    for (uint8_t type : {0, 1, 2, 3, 4, 5, 6, 7, 8}) {
        CellView cell = FreeSteep();
        cell.terrainType = type;
        CHECK(!ShouldHeal(cell, kTypeBuildable));
        CHECK(!ShouldHeal(cell, kTypeSteep));
    }
}

UNIT_TEST(ext_terrain_resort_leaves_claimed_cells) {
    CellView blocked = FreeSteep();
    blocked.blocked = true;
    CHECK(!ShouldHeal(blocked, kTypeBuildable));

    CellView occupied = FreeSteep();
    occupied.occupied = true;
    CHECK(!ShouldHeal(occupied, kTypeBuildable));

    CellView counted = FreeSteep();
    counted.footprintCount = 1;
    CHECK(!ShouldHeal(counted, kTypeBuildable));

    CellView road = FreeSteep();
    road.road = 3;
    CHECK(!ShouldHeal(road, kTypeBuildable));
}

UNIT_TEST(ext_terrain_resort_cleared_cell_on_steep_ground_stays_buildable) {
    CHECK_EQ(ClearedCellType(kTypeResource, kTypeSteep, false), kTypeBuildable);
}

UNIT_TEST(ext_terrain_resort_cleared_cell_keeps_other_classes) {
    for (int classified : {2, 6, 7, 8}) {
        CHECK_EQ(ClearedCellType(kTypeResource, classified, false), classified);
    }
}

UNIT_TEST(ext_terrain_resort_cleared_cell_only_for_a_lone_occupant) {
    // A cell another entity's zone claims (a pasture's fence, a cemetery's stone), or one that
    // was not type 4 (a building's own cells).
    CHECK_EQ(ClearedCellType(kTypeResource, kTypeSteep, true), kTypeSteep);
    CHECK_EQ(ClearedCellType(5, kTypeSteep, false), kTypeSteep);
}

UNIT_TEST(ext_terrain_resort_removal_marker_claims_no_ground) {
    // Stone cleared with the Remove tool: the tool's marker blocks the cell, the stone is still a
    // lone occupant and the cell stays buildable. Another building's blocker keeps the exclusion.
    CHECK(!ZonedByOther(false, false));
    CHECK(ZonedByOther(true, false));
    CHECK(!ZonedByOther(true, true));
    CHECK_EQ(ClearedCellType(kTypeResource, kTypeSteep, ZonedByOther(true, true)), kTypeBuildable);
    CHECK_EQ(ClearedCellType(kTypeResource, kTypeSteep, ZonedByOther(true, false)), kTypeSteep);
}
