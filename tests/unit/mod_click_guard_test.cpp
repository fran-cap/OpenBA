// ============================================================================
// tests/unit/mod_click_guard_test.cpp -- the ModDialog row bound of the click drivers
// ============================================================================
// src/diag/mod_click_guard.h is what AUTOMENU_CLICKS and PAUSEMENU_CLICKS check
// before they hand ModDialog a fabricated per-row id (review F-294, F-295). Move up
// on row 0 and move down on the last row name a neighbour that does not exist:
// Modification::Swap would index _mods[-1] / _mods[count].
// ============================================================================
#include "unit.h"
#include "mod_click_guard.h"

UNIT_TEST(mod_click_guard_not_a_row_id) {
    CHECK(ModDialogClickRowInRange(5, 0));
    CHECK(ModDialogClickRowInRange(999, 0));
    CHECK(ModDialogClickRowInRange(6000, 0));
    CHECK(ModDialogClickRowInRange(-1, 0));
}

UNIT_TEST(mod_click_guard_row_must_exist) {
    CHECK(!ModDialogClickRowInRange(1000, 0));
    CHECK(ModDialogClickRowInRange(1000, 1));
    CHECK(ModDialogClickRowInRange(1002, 3));
    CHECK(!ModDialogClickRowInRange(1003, 3));
    CHECK(ModDialogClickRowInRange(4002, 3));
    CHECK(!ModDialogClickRowInRange(4003, 3));
    CHECK(ModDialogClickRowInRange(5000, 3));
    CHECK(!ModDialogClickRowInRange(5999, 3));
}

UNIT_TEST(mod_click_guard_move_up_needs_a_row_above) {
    CHECK(!ModDialogClickRowInRange(2000, 3));   // Swap(0, -1)
    CHECK(ModDialogClickRowInRange(2001, 3));
    CHECK(ModDialogClickRowInRange(2002, 3));
    CHECK(!ModDialogClickRowInRange(2003, 3));
}

UNIT_TEST(mod_click_guard_move_down_needs_a_row_below) {
    CHECK(ModDialogClickRowInRange(3000, 3));
    CHECK(ModDialogClickRowInRange(3001, 3));
    CHECK(!ModDialogClickRowInRange(3002, 3));   // Swap(2, 3) with 3 rows
    CHECK(!ModDialogClickRowInRange(3000, 1));   // Swap(0, 1) with 1 row
}
