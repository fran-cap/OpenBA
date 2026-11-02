// ============================================================================
// tests/unit/ext_firefighter_wells_test.cpp -- fix.firefighter_wells's threshold rule
// ============================================================================
// src/ext/firefighter_wells/firefighter_wells_rule.h without the world: only the Firefighter
// goal (stat 0x19, constant 40, Template\Well.rsc) compares against 20, and only with the row on.
// ============================================================================
#include "unit.h"

#include "ext/firefighter_wells/firefighter_wells_rule.h"

using Ext::FirefighterWells::Threshold;

UNIT_TEST(ext_firefighter_wells_retail_keeps_40) {
    CHECK(Threshold(false, 0x19, 40, "Template\\Well.rsc") == 40);
}

UNIT_TEST(ext_firefighter_wells_fix_uses_20) {
    CHECK(Threshold(true, 0x19, 40, "Template\\Well.rsc") == 20);
}

UNIT_TEST(ext_firefighter_wells_other_goals_untouched) {
    // Builder's "one well" goal: stat 0x19, constant 1
    CHECK(Threshold(true, 0x19, 1, "Template\\Well.rsc") == 1);
    // another template at 40
    CHECK(Threshold(true, 0x19, 40, "Template\\StoneHouse.rsc") == 40);
    // another stat at 40
    CHECK(Threshold(true, 0x18, 40, "Template\\Well.rsc") == 40);
    // a mod's own constant
    CHECK(Threshold(true, 0x19, 30, "Template\\Well.rsc") == 30);
    CHECK(Threshold(true, 0x19, 40, nullptr) == 40);
}
