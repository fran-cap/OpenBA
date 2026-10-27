// ============================================================================
// tests/unit/ext_phone_loading_test.cpp -- ext.phone_menu: the loading screen's backdrop (P9)
// ============================================================================
// src/ext/phone_menu/loading_math.h: the picture top-aligned (LogoTop), its lower copy at the same
// scale (LowerCopy) and the strip that copy is clipped to (BorderStripTop); P11: the menus' overlay
// drawn bottom-aligned instead (OverlayLower, OverlayStripTop) and the screen's real bottom edge in
// layout units (ScreenBottomUnits).
// docs/android/P9_LOADING_SCALE.md §1. The recognition and the seams need the running game.
// ============================================================================
#include "unit.h"
#include "ext/phone_menu/loading_math.h"

namespace {

using Ext::PhoneMenu::BorderStripTop;
using Ext::PhoneMenu::LowerCopy;
using Ext::PhoneMenu::OverlayLower;
using Ext::PhoneMenu::OverlayStripTop;
using Ext::PhoneMenu::Rect;
using Ext::PhoneMenu::ScreenBottomUnits;

// Retail's cover fit on a screen wider than a 16:9 picture (UserInterface::Draw, 0x180121dbe).
Rect CoverWide(int32_t width, int32_t height) {
    const int32_t half = static_cast<int32_t>(static_cast<float>(width) / (1600.0f / 900.0f) + 0.5f) >> 1;
    const int32_t mid = height >> 1;
    return Rect{0, mid - half, width, mid + half};
}

}  // namespace

UNIT_TEST(ext_phone_loading_both_borders_at_the_menus_scale) {
    // The S25 at UI 40: a 936 x 432 layout; the width-fitted picture is 526 tall.
    const Rect top = Ext::PhoneMenu::LogoTop(CoverWide(936, 432));
    CHECK(top.y1 == 0);                                                          // the top border whole
    CHECK(top.y2 - top.y1 == Ext::PhoneMenu::BackdropHeight(936, 432, 1600, 900));   // the menus' scale
    const Rect lower = LowerCopy(top, 432);
    CHECK(lower.y2 == 432);                                  // the bottom border on the screen's bottom
    CHECK(lower.y2 - lower.y1 == top.y2 - top.y1);           // at the same scale
    CHECK(lower.x1 == top.x1 && lower.x2 == top.x2);
    // The strip the lower copy is clipped to: kLoadingBorderFraction of the picture, at the bottom.
    const int32_t strip = BorderStripTop(432, lower.y2 - lower.y1);
    CHECK(strip == 432 - 66);   // 526 * 0.125 = 65.75
    CHECK(strip > top.y1 && strip < 432);
}

UNIT_TEST(ext_phone_loading_no_lower_copy_when_the_picture_fits) {
    // 16:9 (the picture fills the screen) and a picture that ends above the bottom: nothing to add.
    const Rect full{0, 0, 1600, 900};
    const Rect a = LowerCopy(full, 900);
    CHECK(a.x2 <= a.x1);
    const Rect b = LowerCopy(Rect{0, 0, 1400, 800}, 1000);
    CHECK(b.x2 <= b.x1);
}

UNIT_TEST(ext_phone_loading_screen_bottom_rounds_up) {
    // The S25 at UI 42: the layout height is (int)(1080 * 0.42) = 453 of 453.6 units; a draw that ends
    // at 453 leaves the last device pixels uncovered (P11's sliver). The real bottom rounds up.
    CHECK(ScreenBottomUnits(453, 1080, 0.42f) == 454);
    // An exact layout keeps its height (1080 * 0.5 = 540).
    CHECK(ScreenBottomUnits(540, 1080, 0.5f) == 540);
    // Never above the layout height; no device or scale: the layout height.
    CHECK(ScreenBottomUnits(453, 0, 0.42f) == 453);
    CHECK(ScreenBottomUnits(453, 1080, 0.0f) == 453);
}

UNIT_TEST(ext_phone_loading_overlay_bottom_border) {
    // The S25 at UI 42 (982 x 453 layout, real bottom 454): the picture top-aligned, the menus' 1600x900
    // overlay bottom-aligned at its width, ending on the real bottom.
    const Rect top = Ext::PhoneMenu::LogoTop(CoverWide(982, 453));
    const Rect over = OverlayLower(top, 454, 1600, 900);
    CHECK(over.x1 == top.x1 && over.x2 == top.x2);
    CHECK(over.y2 == 454);
    CHECK(over.y2 - over.y1 == static_cast<int32_t>(982.0f * 900.0f / 1600.0f + 0.5f));
    // The clip line is in the overlay's transparent middle: below its logo (top quarter), above its
    // torn bottom border (lowest ~14 %).
    const int32_t strip = OverlayStripTop(454, over.y2 - over.y1);
    const int32_t h = over.y2 - over.y1;
    CHECK(strip > over.y1 + h / 4);
    CHECK(strip < over.y2 - (h * 14) / 100);
    // A picture that does not overhang the bottom, or an overlay with no size: nothing drawn.
    const Rect none = OverlayLower(Rect{0, 0, 1600, 900}, 900, 1600, 900);
    CHECK(none.x2 <= none.x1);
    const Rect empty = OverlayLower(top, 454, 0, 900);
    CHECK(empty.x2 <= empty.x1);
}
