// ============================================================================
// tests/unit/ext_crop_planting_exit_test.cpp -- fix.crop_planting_exit's rules
// ============================================================================
// src/ext/crop_planting_exit/crop_planting_exit_rule.h without the world: when
// PlantArea ends planting over slots no worker can reach.
// ============================================================================
#include "unit.h"

#include "ext/crop_planting_exit/crop_planting_exit_rule.h"

using Ext::CropPlantingExit::EndPlantingOnUnreachable;

UNIT_TEST(ext_crop_planting_exit_unreachable_ends_planting_only_with_the_fix) {
    CHECK(!EndPlantingOnUnreachable(false, 0, 9));
    CHECK(EndPlantingOnUnreachable(true, 0, 9));
}

UNIT_TEST(ext_crop_planting_exit_unreachable_waits_for_plants_in_flight) {
    CHECK(!EndPlantingOnUnreachable(true, 1, 9));
}

UNIT_TEST(ext_crop_planting_exit_unreachable_needs_a_planted_slot) {
    // nothing planted yet: the citizen may be the one who cannot reach the field
    CHECK(!EndPlantingOnUnreachable(true, 0, 0));
}
