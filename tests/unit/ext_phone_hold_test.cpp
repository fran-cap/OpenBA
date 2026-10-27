// ============================================================================
// tests/unit/ext_phone_hold_test.cpp -- ext.phone_menu, where a finger's tooltip goes (P11c)
// ============================================================================
// src/ext/phone_menu/hold_math.h without the game: the lift's source and units, the tooltip above the
// finger, the flip below it, the safe-area clamps (docs/android/P11C_TOOLTIP.md). The seam call needs
// the running game.
// ============================================================================
#include "unit.h"
#include "ext/phone_menu/hold_math.h"

namespace PM = Ext::PhoneMenu;

UNIT_TEST(ext_phone_hold_lift_source) {
    CHECK(PM::HoldTipLiftDevicePx(false, 240) == 160);   // ext.touch_pointer off: the ruled default
    CHECK(PM::HoldTipLiftDevicePx(true, 240) == 240);    // on: its lift
    CHECK(PM::HoldTipLiftDevicePx(true, 120) == 120);
    CHECK(PM::HoldTipLiftDevicePx(true, 0) == 0);        // a lift of 0 is the player's
    CHECK(PM::HoldTipLiftDevicePx(true, -5) == 0);
}

UNIT_TEST(ext_phone_hold_lift_in_layout_units) {
    // The S25 at render Half (0.5 back-buffer pixel per screen pixel), UI 40 % (_uiScale 0.8): 64 units.
    CHECK(PM::DeviceToLayout(160, 0.5f, 0.8f) == 64);
    // A desktop at render Full, UI 100 %: device pixels are layout units.
    CHECK(PM::DeviceToLayout(160, 1.0f, 1.0f) == 160);
    // Full render scale, UI 40 %.
    CHECK(PM::DeviceToLayout(160, 1.0f, 0.4f) == 64);
    CHECK(PM::DeviceToLayout(0, 0.5f, 0.8f) == 0);
    CHECK(PM::DeviceToLayout(-10, 0.5f, 0.8f) == 0);
}

UNIT_TEST(ext_phone_hold_tip_above_the_finger) {
    // The S25's layout screen at render Half, UI 40 %: 936 x 432, a 19-unit margin.
    const PM::Rect safe{19, 19, 917, 413};
    const PM::HoldTip t = PM::PlaceHoldTip(400, 380, 120, 50, 64, safe, 936, 432);
    CHECK(!t.below);
    CHECK(t.y == 380 - 64 - 50);   // its bottom the lift above the finger
    CHECK(t.y + 50 <= 380 - 64);
    CHECK(t.x == 400);             // retail's x: its left at the finger
}

UNIT_TEST(ext_phone_hold_tip_flips_below_without_room) {
    const PM::Rect safe{19, 19, 917, 413};
    // A finger near the top (an Options check box at the top of the window): no room above.
    const PM::HoldTip t = PM::PlaceHoldTip(300, 90, 120, 50, 64, safe, 936, 432);
    CHECK(t.below);
    CHECK(t.y == 90 + 64);
    // Exactly enough room above: stays above.
    const PM::HoldTip u = PM::PlaceHoldTip(300, 19 + 50 + 64, 120, 50, 64, safe, 936, 432);
    CHECK(!u.below);
    CHECK(u.y == 19);
    // No room on either side: at the safe top.
    const PM::HoldTip v = PM::PlaceHoldTip(300, 200, 120, 300, 64, safe, 936, 432);
    CHECK(!v.below);
    CHECK(v.y == 19);
}

UNIT_TEST(ext_phone_hold_tip_kept_inside_the_safe_area) {
    const PM::Rect safe{19, 19, 917, 413};
    // A toolbar button at the right edge: pulled in to the safe right.
    const PM::HoldTip r = PM::PlaceHoldTip(900, 400, 200, 60, 64, safe, 936, 432);
    CHECK(r.x == 917 - 200);
    CHECK(r.y == 400 - 64 - 60);
    // A finger inside the left margin: at the safe left.
    const PM::HoldTip l = PM::PlaceHoldTip(5, 400, 200, 60, 64, safe, 936, 432);
    CHECK(l.x == 19);
    // Wider than the safe area: the whole screen's width.
    const PM::HoldTip w = PM::PlaceHoldTip(900, 400, 920, 60, 64, safe, 936, 432);
    CHECK(w.x == 936 - 920);
    // No margin (a screen too small for it): the screen's edges.
    const PM::Rect none{0, 0, 300, 200};
    const PM::HoldTip n = PM::PlaceHoldTip(10, 150, 100, 40, 64, none, 300, 200);
    CHECK(n.y == 150 - 64 - 40);
    CHECK(n.x == 10);
}
