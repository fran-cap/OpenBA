// ============================================================================
// tests/unit/ext_phone_touch_test.cpp -- ext.phone_menu, the P9 dialog fit
// ============================================================================
// src/ext/phone_menu/touch_math.h without the game: the scroll bar and close button sizes, the UI
// scale a dialog larger than the safe area is shown at, the menus' extra backdrop draw and the
// version line's place in its bottom border. The numbers are the S25's (a 2340x1080 back buffer) at
// the owner's UI 40, the Options dialog's measured 437 x 427 units.
// The walk over the dialogs and the seams (UserInterface::Update / Draw) need the running game.
// ============================================================================
#include "unit.h"
#include "ext/phone_menu/touch_math.h"

namespace PM = Ext::PhoneMenu;

UNIT_TEST(ext_phone_touch_scroll_bar_four_times_thicker) {
    CHECK(PM::kScrollBarFactor == 4);
    CHECK(PM::ScrollBarThickness(12, 12) == 48);   // SharedElements.rsc's 12-unit bar
    CHECK(PM::ScrollBarThickness(12, 60) == 60);   // never thinner than retail's own minimum
    CHECK(PM::ScrollBarThickness(0, 12) == 12);    // no sprite: retail's
    CHECK(PM::ScrollBarThickness(-3, 7) == 7);
}

UNIT_TEST(ext_phone_touch_close_icon_twice_the_sprite) {
    CHECK(PM::kCloseButtonFactor == 2);
    CHECK(PM::CloseIconSide(16) == 32);            // StandardDialog.rsc's 16-unit cross
    CHECK(PM::CloseIconSide(0) == 0);
    CHECK(PM::CloseIconSide(-1) == 0);
}

UNIT_TEST(ext_phone_touch_fits_safe_area) {
    // 936 x 432 (the S25 at UI 40): margin 19, a 394-unit safe height.
    CHECK(!PM::FitsSafeArea(936, 432, 437, 427));   // the Options dialog: its top was cut off
    CHECK(PM::FitsSafeArea(936, 432, 437, 394));
    CHECK(!PM::FitsSafeArea(936, 432, 437, 395));
    CHECK(!PM::FitsSafeArea(936, 432, 899, 100));   // too wide: 898 across
    CHECK(PM::FitsSafeArea(936, 432, 898, 100));
}

UNIT_TEST(ext_phone_touch_fit_scale) {
    // The Options dialog at the owner's UI 40 is shown at 44 % (475 units high, margin 21, 433 fit).
    const float s = PM::FitScale(0.40f, 2340, 1080, 437, 427);
    CHECK(s > 0.435f && s < 0.445f);
    // A dialog that fits keeps the player's scale exactly.
    CHECK(PM::FitScale(0.40f, 2340, 1080, 300, 300) == 0.40f);
    CHECK(PM::FitScale(0.37f, 2340, 1080, 300, 300) == 0.37f);
    // Larger than the screen even at 100 %: 100 % (retail's own layout from there).
    CHECK(PM::FitScale(0.40f, 2340, 1080, 437, 2000) == PM::kFitScaleMax);
    // A player above 100 % is left there.
    CHECK(PM::FitScale(1.25f, 2340, 1080, 437, 5000) == 1.25f);
    // No device or no dialog size: the player's scale.
    CHECK(PM::FitScale(0.40f, 0, 0, 437, 427) == 0.40f);
    CHECK(PM::FitScale(0.40f, 2340, 1080, 0, 427) == 0.40f);
    // Whatever it answers fits.
    const int32_t w = static_cast<int32_t>(2340.0f * s);
    const int32_t h = static_cast<int32_t>(1080.0f * s);
    CHECK(PM::FitsSafeArea(w, h, 437, 427));
}

UNIT_TEST(ext_phone_touch_lower_backdrop) {
    // 936 x 432 against the 16:9 overlay moved to the top (LogoTop): 526 high, 94 below the screen.
    const PM::Rect lower = PM::LowerBackdrop(PM::Rect{0, 0, 936, 526}, 936, 432);
    CHECK(lower.x1 == 0);
    CHECK(lower.y1 == -94);   // the extra copy's bottom on the screen's bottom
    CHECK(lower.x2 == 936);
    CHECK(lower.y2 == 432);
    // A picture that does not overhang the bottom (16:9, or 16:10's height fit): no extra draw.
    const PM::Rect none = PM::LowerBackdrop(PM::Rect{0, 0, 1920, 1080}, 1920, 1080);
    CHECK(none.x2 <= none.x1);
    const PM::Rect side = PM::LowerBackdrop(PM::Rect{-107, 0, 2027, 1200}, 1920, 1200);
    CHECK(side.x2 <= side.x1);
    const PM::Rect empty = PM::LowerBackdrop(PM::Rect{0, 0, 0, 0}, 936, 432);
    CHECK(empty.x2 <= empty.x1);
}

UNIT_TEST(ext_phone_touch_low_version_line) {
    const PM::Rect r = PM::LowVersionLayout(936, 432);
    CHECK(r.x1 == 19);
    CHECK(r.y1 == 19);
    CHECK(r.x2 == 936 - 19);
    CHECK(r.y2 == 432);   // the screen's bottom edge, not the safe area's
    CHECK(PM::LowVersionTop(432, 56) == 376);
    CHECK(PM::LowVersionTop(432, 0) == -1);
    // Lower than the safe-area placement by the margin.
    CHECK(PM::LowVersionTop(432, 56) - PM::VersionTop(936, 432, 56) == 19);
}

UNIT_TEST(ext_phone_touch_fit_is_the_dialogs_own_scale) {
    // P11: the fit is the tall dialog's own scale k = player / fitted (UserInterface::_extDialogScale),
    // the interface keeps the player's 0.40. The engine lays the dialog out in the placed rectangle
    // divided by k: the safe area of the 936 x 432 layout, in the dialog's own units, holds it whole.
    const float user = 0.40f;
    const float s = PM::FitScale(user, 2340, 1080, 437, 427);
    const float k = user / s;
    CHECK(k < 1.0f);
    const PM::Rect safe = PM::InsetLayout(936, 432);
    const int32_t ownW = static_cast<int32_t>(static_cast<float>(safe.x2 - safe.x1) / k);
    const int32_t ownH = static_cast<int32_t>(static_cast<float>(safe.y2 - safe.y1) / k);
    CHECK(ownW >= 437 && ownH >= 427);
    // Own units back to layout units (phone_menu.cpp's safe-area arm): rounded, sign kept, k 1 exact.
    CHECK(PM::ToLayoutUnits(437, 1.0f) == 437);
    CHECK(PM::ToLayoutUnits(427, k) == static_cast<int32_t>(427.0f * k + 0.5f));
    CHECK(PM::ToLayoutUnits(-10, 0.5f) == -5);
    CHECK(PM::ToLayoutUnits(0, k) == 0);
    CHECK(PM::ToLayoutUnits(PM::ToLayoutUnits(200, 2.0f), 0.5f) == 200);
}
