// ============================================================================
// tests/unit/ext_phone_size_test.cpp -- ext.phone_menu, the P11 touch size of every control
// ============================================================================
// src/ext/phone_menu/size_math.h without the game: the touch side (the P9 close button's), the icon
// doubling rule and the padding split. The walk over the dialogs and the slider seam need the running
// game (docs/android/P11_TOUCH_SIZE.md).
// ============================================================================
#include "unit.h"
#include "ext/phone_menu/size_math.h"

namespace PM = Ext::PhoneMenu;

UNIT_TEST(ext_phone_size_touch_side_is_the_close_button) {
    // StandardDialog.rsc's 16-unit cross at twice its size, padded 2 on each side.
    CHECK(PM::kTouchSide == PM::CloseIconSide(16) + 2 + 2);
    CHECK(PM::kTouchSide == 36);
    CHECK(PM::kSliderSpriteFactor == 2);
}

UNIT_TEST(ext_phone_size_icon_doubled_when_it_fits) {
    CHECK(PM::TouchIconSide(16) == 32);   // the title bar's pin / centre, the citizen row's `>`
    CHECK(PM::TouchIconSide(8) == 16);
    CHECK(PM::TouchIconSide(17) == 17);   // doubled it would not fit the touch side with its padding
    CHECK(PM::TouchIconSide(32) == 32);   // the toolbar's icons: already finger-sized
    CHECK(PM::TouchIconSide(0) == 0);
    CHECK(PM::TouchIconSide(-1) == 0);
}

UNIT_TEST(ext_phone_size_pad_split) {
    const PM::PadSplit a = PM::PadToTarget(24, 36);   // an Ok button's label: 6 above, 6 below
    CHECK(a.before == 6 && a.after == 6);
    const PM::PadSplit b = PM::PadToTarget(23, 36);   // odd: the extra unit below
    CHECK(b.before == 6 && b.after == 7);
    const PM::PadSplit c = PM::PadToTarget(36, 36);   // already the touch side
    CHECK(c.before == 0 && c.after == 0);
    const PM::PadSplit d = PM::PadToTarget(40, 36);   // larger: never shrunk
    CHECK(d.before == 0 && d.after == 0);
    const PM::PadSplit e = PM::PadToTarget(10, PM::kTouchSide / 2);   // a spinner arrow: half the side
    CHECK(e.before == 4 && e.after == 4);
}

// Issue #73: the title bar's name and buttons side by side, not over each other.
UNIT_TEST(ext_phone_size_title_bar_min_width) {
    CHECK(PM::TitleBarMinWidth(0, 120 + 300, 2) == 424);    // a work place: the name, its count and arrows
    CHECK(PM::TitleBarMinWidth(500, 120 + 300, 2) == 500);  // retail's minimum already larger: kept
    CHECK(PM::TitleBarMinWidth(0, 120, 1) == 120);          // one part: no gap
    CHECK(PM::TitleBarMinWidth(40, 0, 0) == 40);            // nothing to place: retail's
}

UNIT_TEST(ext_phone_size_spread_spacing) {
    CHECK(PM::SpreadSpacing(4, 400, 4 * 80, 3) == 26);    // four 80-unit buttons over 400: 26 between
    CHECK(PM::SpreadSpacing(4, 401, 4 * 80, 3) == 27);
    CHECK(PM::SpreadSpacing(4, 300, 4 * 80, 3) == 4);     // no room to spare: the description's gap
    CHECK(PM::SpreadSpacing(4, 400, 80, 0) == 4);         // one button: nothing to spread
}

// P11b (touch_scroll.h (b)): a tall window's lists capped so it fits the safe area.
UNIT_TEST(ext_phone_size_list_cap_shrink) {
    CHECK(PM::kListFloor == 108);
    CHECK(PM::ShrinkCap(0, 318, 60, PM::kListFloor) == 258);    // the Options pages: 60 units over
    CHECK(PM::ShrinkCap(258, 258, 10, PM::kListFloor) == 248);  // a further step (two stacked lists)
    CHECK(PM::ShrinkCap(0, 318, 300, PM::kListFloor) == 108);   // never under the floor
    CHECK(PM::ShrinkCap(108, 108, 20, PM::kListFloor) == 108);  // at the floor: the scale-down takes the rest
    CHECK(PM::ShrinkCap(0, 90, 20, PM::kListFloor) == 0);       // lists already short: nothing to cap
    CHECK(PM::ShrinkCap(200, 318, 0, PM::kListFloor) == 200);   // fits: unchanged
}

UNIT_TEST(ext_phone_size_list_cap_grow) {
    CHECK(PM::GrowCap(258, 20, 318) == 278);   // a larger UI room: the lists grow back
    CHECK(PM::GrowCap(258, 60, 318) == 0);     // back to their own height: no cap
    CHECK(PM::GrowCap(258, 80, 318) == 0);
    CHECK(PM::GrowCap(0, 80, 318) == 0);       // no cap in force
    CHECK(PM::GrowCap(258, 0, 318) == 258);
}

// P11d (touch_scroll.cpp FitFrames): a frame with its own minimum around a capped list follows the cap.
UNIT_TEST(ext_phone_size_list_frame_min) {
    CHECK(PM::FrameMinHeight(360, 0) == 360);     // SaveLoad.rsc mainGroup, no cap: retail's
    CHECK(PM::FrameMinHeight(360, 35) == 325);    // its 344-unit list capped at 309 (the S25 at UI 42)
    CHECK(PM::FrameMinHeight(360, 400) == 0);     // never negative
    CHECK(PM::FrameMinHeight(360, -5) == 360);    // a list grown past its description: retail's
}
