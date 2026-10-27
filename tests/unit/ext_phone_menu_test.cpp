// ============================================================================
// tests/unit/ext_phone_menu_test.cpp -- ext.phone_menu
// ============================================================================
// src/ext/phone_menu/phone_math.h without the game: the registry row (class A, OFF by default, the
// profile keys), the safe-area margin and inset, the backdrop's top anchor, every other dialog's safe
// area, and the slider minimum.
// The seams themselves (UserInterface::Update / Draw, the two dialogs) need the running game.
// ============================================================================
#include "unit.h"
#include "ext/phone_menu/phone_math.h"
#include "ext/registry.h"
#include "io/settings_file.h"

#include <string>

namespace {

using IO::SettingsFile::Entries;

bool EntriesLookup(void* ctx, const char* key, std::string& value) {
    const Entries* entries = static_cast<const Entries*>(ctx);
    for (const IO::SettingsFile::Entry& e : *entries) {
        if (e.key == key) {
            value = e.value;
            return true;
        }
    }
    return false;
}

Ext::State ResolveText(const char* text) {
    Entries entries = IO::SettingsFile::Parse(std::string(text));
    return Ext::Resolve(&EntriesLookup, &entries, nullptr, nullptr);
}

// Retail's cover fit (UserInterface::Draw, 0x180121dbe), on a screen wider than the sprite.
Ext::PhoneMenu::Rect RetailCoverWide(int32_t width, int32_t height, float spriteAspect) {
    const int32_t half = static_cast<int32_t>(static_cast<float>(width) / spriteAspect + 0.5f) >> 1;
    const int32_t mid = height >> 1;
    return Ext::PhoneMenu::Rect{0, mid - half, width, mid + half};
}

}  // namespace

UNIT_TEST(ext_phone_menu_registry_row) {
    const Ext::Entry* e = Ext::FindEntry("phone_menu");
    CHECK(e != nullptr);
    if (e == nullptr) return;
    CHECK(e->tier == Ext::Tier::Extension);
    CHECK(e->cls == Ext::Class::A);
    CHECK(!e->defaultOn);
    CHECK(e->formatVersion == 0);
    CHECK(Ext::SettingsKey(*e) == "ext.phone_menu");
    CHECK(!ResolveText("").ext.phone_menu);                         // absent key = the default (off)
    CHECK(ResolveText("ext.phone_menu=1\n").ext.phone_menu);        // the Android first run's line
    CHECK(!ResolveText("ext.phone_menu=1\nprofile=faithful\n").ext.phone_menu);
}

UNIT_TEST(ext_phone_menu_safe_margin_is_3_4_percent_of_the_height) {
    // P11c: three quarters of P7's 4.5 % (the owner's ruling, NEXT_SESSION.md §2.11).
    CHECK(Ext::PhoneMenu::SafeMargin(1080) == 36);    // the S25 at 100 %: 36.45 (P7: 49)
    CHECK(Ext::PhoneMenu::SafeMargin(453) == 15);     // the S25 at UI 42: 15.3 (P7: 20)
    CHECK(Ext::PhoneMenu::SafeMargin(270) == 9);      // the S25 at 25 %: 9.11 (P7: 12)
    CHECK(Ext::PhoneMenu::SafeMargin(0) == 0);
    CHECK(Ext::PhoneMenu::SafeMargin(-5) == 0);
}

UNIT_TEST(ext_phone_menu_inset_layout_takes_edges) {
    const Ext::PhoneMenu::Rect r = Ext::PhoneMenu::InsetLayout(2340, 1080);
    CHECK(r.x1 == 36);
    CHECK(r.y1 == 36);
    CHECK(r.x2 == 2340 - 36);   // the right EDGE, as DialogInstance::ComputeLayout takes it
    CHECK(r.y2 == 1080 - 36);
    // A screen too thin for the margin keeps retail's whole-screen rectangle.
    const Ext::PhoneMenu::Rect thin = Ext::PhoneMenu::InsetLayout(1, 1000);
    CHECK(thin.x1 == 0);
    CHECK(thin.y1 == 0);
    CHECK(thin.x2 == 1);
    CHECK(thin.y2 == 1000);
}

UNIT_TEST(ext_phone_menu_backdrop_logo_top) {
    // 2340x1080 against the menu's 16:9 overlay: retail cuts 118 off the top and the bottom.
    CHECK(Ext::PhoneMenu::WiderThanBackdrop(2340, 1080, 1600, 900));
    const Ext::PhoneMenu::Rect retail = RetailCoverWide(2340, 1080, 1600.0f / 900.0f);
    CHECK(retail.y1 == -118);
    CHECK(retail.y2 == 1198);
    CHECK(Ext::PhoneMenu::BackdropHeight(2340, 1080, 1600, 900) == 1316);
    // The top on the screen's top: the logo whole and centred, the cut moved to the bottom.
    const Ext::PhoneMenu::Rect r = Ext::PhoneMenu::LogoTop(retail);
    CHECK(r.x1 == 0);
    CHECK(r.x2 == 2340);
    CHECK(r.y1 == 0);
    CHECK(r.y2 == 1316);
    CHECK(Ext::PhoneMenu::LogoBottom(1316) == 290);   // 0.22 of the drawn picture
    // A 16:9 screen fits exactly (retail's compare sends it to the width fit, no overhang).
    CHECK(Ext::PhoneMenu::WiderThanBackdrop(1920, 1080, 1600, 900));
    CHECK(Ext::PhoneMenu::BackdropHeight(1920, 1080, 1600, 900) == 1080);
    // A 16:10 screen is narrower: retail fits the height, the picture is the screen's height.
    CHECK(!Ext::PhoneMenu::WiderThanBackdrop(1920, 1200, 1600, 900));
    CHECK(Ext::PhoneMenu::BackdropHeight(1920, 1200, 1600, 900) == 1200);
    // A rectangle that does not overhang the top: unchanged.
    const Ext::PhoneMenu::Rect sides{-200, 0, 1800, 1200};
    const Ext::PhoneMenu::Rect same = Ext::PhoneMenu::LogoTop(sides);
    CHECK(same.x1 == -200);
    CHECK(same.y1 == 0);
    CHECK(same.y2 == 1200);
    // No picture: never wider.
    CHECK(!Ext::PhoneMenu::WiderThanBackdrop(2340, 1080, 0, 0));
    CHECK(Ext::PhoneMenu::LogoBottom(0) == -1);
}

UNIT_TEST(ext_phone_menu_button_row) {
    // The labels need the largest label ("Achievements", 125 with the retail padding) plus the
    // frame's inset on both sides.
    const int32_t widths[] = {65, 67, 92, 125, 85, 71, 104, 60};
    const int32_t heights[] = {30, 30, 30, 30, 30, 30, 30, 30};
    CHECK(Ext::PhoneMenu::SquareSide(widths, heights, 8) == 125 + 2 * Ext::PhoneMenu::kSquareInset);
    const int32_t tallW[] = {20};
    const int32_t tallH[] = {40};
    CHECK(Ext::PhoneMenu::SquareSide(tallW, tallH, 1) == 40 + 2 * Ext::PhoneMenu::kSquareInset);
    CHECK(Ext::PhoneMenu::SquareSide(widths, heights, 0) == 0);
    // The S25 at UI 100, render scale 50: 1170x540 units, margin 18; the logo ends at 145 (0.22
    // of a 658-unit picture), the version line (56 padded) starts at 466.
    CHECK(Ext::PhoneMenu::BackdropHeight(1170, 540, 1600, 900) == 658);
    CHECK(Ext::PhoneMenu::LogoBottom(658) == 145);
    CHECK(Ext::PhoneMenu::VersionTop(1170, 540, 56) == 466);
    CHECK(Ext::PhoneMenu::VersionTop(1170, 540, 0) == -1);
    // The band between the logo and the version line (less its gap), inside the safe area.
    const Ext::PhoneMenu::Rect band = Ext::PhoneMenu::RowBand(1170, 540, 145, 466);
    CHECK(band.x1 == 18);
    CHECK(band.x2 == 1170 - 18);
    CHECK(band.y1 == 145);
    CHECK(band.y2 == 462);
    CHECK(Ext::PhoneMenu::BlockHeight(1170, 540, 145, 466) == 317);
    CHECK(Ext::PhoneMenu::BlockHeight(1170, 540, -1, -1) == 504);    // nothing known: the safe area
    CHECK(Ext::PhoneMenu::BlockHeight(1170, 540, 600, -1) == 0);     // a logo past the safe area
}

namespace {

// The menus as measured on the S25: the ribbon's 8-unit padding around the row, 2-unit button
// padding, 2 units between squares. "Achievements" (89 wide in the labels' own font, 68 in the
// small font) with the frame's inset on both sides.
constexpr int32_t kOwn = 89 + 2 * Ext::PhoneMenu::kSquareInset;
constexpr int32_t kSmall = 68 + 2 * Ext::PhoneMenu::kSquareInset;

Ext::PhoneMenu::RowMetrics Menu(int32_t count, int32_t contentSide) {
    Ext::PhoneMenu::RowMetrics m;
    m.count = count;
    m.contentSide = contentSide;
    m.outerWidth = 16;
    m.outerHeight = 16;
    m.buttonPadWidth = 4;
    m.buttonPadHeight = 4;
    m.spacing = 2;
    return m;
}

// The owner's S25 settings, UI 35 at render scale 100: an 819x378 layout (safe area 13 .. 806 x
// 13 .. 365), the logo to 101, the version line from 305.
Ext::PhoneMenu::RowPlan PlanUi35(int32_t count, int32_t contentSide) {
    return Ext::PhoneMenu::PlanRows(819, 378, Menu(count, contentSide), 101, 305);
}

// A 2528x1345 window at UI 32: an 808x430 layout (safe area 15 .. 793 x 15 .. 415), the logo to
// 100, the version line from 355. At 1.88:1 the logo leaves more height than on the phone.
Ext::PhoneMenu::RowPlan PlanWindow32(int32_t count, int32_t contentSide) {
    return Ext::PhoneMenu::PlanRows(808, 430, Menu(count, contentSide), 100, 355);
}

}  // namespace

UNIT_TEST(ext_phone_menu_rows_sizes) {
    const Ext::PhoneMenu::RowMetrics m = Menu(8, kOwn);
    CHECK(Ext::PhoneMenu::RowsWidth(m, 8, kOwn) == 16 + 8 * (kOwn + 4) + 7 * 2);
    CHECK(Ext::PhoneMenu::RowsHeight(m, 1, kOwn) == 16 + kOwn + 4);
    CHECK(Ext::PhoneMenu::RowsHeight(m, 2, kOwn) == 16 + 2 * (kOwn + 4) + 2);
    CHECK(Ext::PhoneMenu::BlockHeight(819, 378, 101, 305) == 200);
    CHECK(Ext::PhoneMenu::BlockHeight(808, 430, 100, 355) == 251);
}

UNIT_TEST(ext_phone_menu_rows_at_the_owners_ui_35) {
    // n = 8 to 11 in the labels' own font (99): neither one row (at most 90 across, for 8) nor
    // two rows (87 down in the 200-unit band) fit; the caller then plans in the small font (78).
    for (int32_t n = 8; n <= 11; ++n) CHECK(PlanUi35(n, kOwn).rows == 0);
    // n = 8 (the main menu): one row (90 across), the labels' side (over the 0.22 cap, 75).
    Ext::PhoneMenu::RowPlan p = PlanUi35(8, kSmall);
    CHECK(p.rows == 1);
    CHECK(p.perRow == 8);
    CHECK(p.side == kSmall);
    // n = 9 (a button added to the main menu): still one row (79 across).
    p = PlanUi35(9, kSmall);
    CHECK(p.rows == 1);
    CHECK(p.perRow == 9);
    CHECK(p.side == kSmall);
    // n = 10 (the pause menu): one row is 71 across, under the labels' 78: two rows of five.
    p = PlanUi35(10, kSmall);
    CHECK(p.rows == 2);
    CHECK(p.perRow == 5);
    CHECK(p.side == kSmall);
    // n = 11 (a button added to the pause menu): two rows, six on the first.
    p = PlanUi35(11, kSmall);
    CHECK(p.rows == 2);
    CHECK(p.perRow == 6);
    CHECK(p.side == kSmall);
}

UNIT_TEST(ext_phone_menu_rows_wrap_on_a_wide_window) {
    // n = 8, 9, 10, 11 in the labels' own font: one row does not fit across, two rows do.
    for (int32_t n = 8; n <= 11; ++n) {
        const Ext::PhoneMenu::RowPlan p = PlanWindow32(n, kOwn);
        CHECK(p.rows == 2);
        CHECK(p.perRow == (n + 1) / 2);
        CHECK(p.side == kOwn);   // the 0.22 cap (86) is under the labels (99): they get their side
        CHECK(Ext::PhoneMenu::RowsWidth(Menu(n, kOwn), p.perRow, p.side) <= 793 - 15);
        CHECK(Ext::PhoneMenu::RowsHeight(Menu(n, kOwn), 2, p.side) <= Ext::PhoneMenu::BlockHeight(808, 430, 100, 355));
    }
    // An even count splits in halves, an odd one puts the extra square on the first row.
    CHECK(PlanWindow32(10, kOwn).perRow == 5);
    CHECK(PlanWindow32(11, kOwn).perRow == 6);
    // At UI 100 (2528x1345, safe height 1255) one row of eight fits, at the 0.22 cap.
    const Ext::PhoneMenu::RowPlan big = Ext::PhoneMenu::PlanRows(2528, 1345, Menu(8, kOwn), 313, 1228);
    CHECK(big.rows == 1);
    CHECK(big.side == 276);
}

UNIT_TEST(ext_phone_menu_rows_give_way_to_the_column) {
    // The owner's S25 at UI 25 (585x270, safe area 9 .. 576 x 9 .. 261, the logo to 72, the
    // version line from 202): even two rows of small-font squares (2 x 82 + 18) do not fit the
    // 126-unit band between the logo and the version line: the column.
    CHECK(Ext::PhoneMenu::BlockHeight(585, 270, 72, 202) == 126);
    for (int32_t n = 8; n <= 11; ++n) {
        CHECK(Ext::PhoneMenu::PlanRows(585, 270, Menu(n, kSmall), 72, 202).rows == 0);
    }
    // Nothing to place.
    CHECK(Ext::PhoneMenu::PlanRows(819, 378, Menu(0, kOwn), 101, 305).rows == 0);
    CHECK(Ext::PhoneMenu::PlanRows(819, 378, Menu(8, 0), 101, 305).rows == 0);
    // One button never wraps.
    const Ext::PhoneMenu::RowPlan one = Ext::PhoneMenu::PlanRows(819, 378, Menu(1, kOwn), 101, 305);
    CHECK(one.rows == 1);
    CHECK(one.side == kOwn);
}

UNIT_TEST(ext_phone_menu_menu_band) {
    // Nothing known (the first frame): the safe area.
    Ext::PhoneMenu::Rect r = Ext::PhoneMenu::MenuBand(2340, 1080, -1, -1, 0);
    CHECK(r.y1 == 36);
    CHECK(r.y2 == 1044);
    // The S25 at 50 %: 1170x540 units, logo ends at 0.22 of a 658-unit picture (145), version
    // line from 497, a 300-unit column: centred between the two.
    r = Ext::PhoneMenu::MenuBand(1170, 540, 145, 497, 300);
    CHECK(r.x1 == 18);
    CHECK(r.x2 == 1170 - 18);
    CHECK(r.y1 == 145);
    CHECK(r.y2 == 497 - Ext::PhoneMenu::kVersionGap);
    // Too tall for the band under the logo but not for the space above the version line.
    r = Ext::PhoneMenu::MenuBand(1170, 540, 145, 497, 400);
    CHECK(r.y1 == 18);
    CHECK(r.y2 == 497 - Ext::PhoneMenu::kVersionGap);
    // Taller than the safe band but not than the space above the version line: the band
    // (ComputeLayout lets its top rise into the margin, its bottom stays above the line).
    r = Ext::PhoneMenu::MenuBand(1170, 540, 145, 497, 480);
    CHECK(r.y1 == 18);
    CHECK(r.y2 == 497 - Ext::PhoneMenu::kVersionGap);
    // Taller than the screen allows: the safe area.
    r = Ext::PhoneMenu::MenuBand(585, 270, 72, 230, 300);
    CHECK(r.y1 == 9);
    CHECK(r.y2 == 270 - 9);
    // No logo (the pause menu has no backdrop): above the version line.
    r = Ext::PhoneMenu::MenuBand(1170, 540, -1, 497, 300);
    CHECK(r.y1 == 18);
    CHECK(r.y2 == 497 - Ext::PhoneMenu::kVersionGap);
}

UNIT_TEST(ext_phone_menu_version_aside) {
    // 25 % on a 2.17:1 screen 1421 pixels tall: 770x355 units, a 100x290 column, the version
    // line 250 wide from 287: the column cannot sit above it, the line fits to its left.
    CHECK(Ext::PhoneMenu::VersionAside(770, 355, 287, 250, 100, 290));
    const Ext::PhoneMenu::Rect r = Ext::PhoneMenu::VersionAsideLayout(770, 355, 250);
    CHECK(r.x1 == 12);
    CHECK(r.x2 == 12 + 250);
    CHECK(r.y1 == 12);
    CHECK(r.y2 == 355 - 12);
    // The column fits above the line: the line stays centred.
    CHECK(!Ext::PhoneMenu::VersionAside(770, 355, 287, 250, 100, 200));
    // No room beside the column (a 16:9 screen at 25 %): stays.
    CHECK(!Ext::PhoneMenu::VersionAside(450, 253, 190, 250, 100, 290));
    // Not laid out yet.
    CHECK(!Ext::PhoneMenu::VersionAside(770, 355, -1, 250, 100, 290));
    CHECK(!Ext::PhoneMenu::VersionAside(770, 355, 287, 250, 0, 0));
}

UNIT_TEST(ext_phone_menu_safe_area_anchors_move_in) {
    // The S25 at 100 %: margin 36. A dialog at the default upper-left (0, 0), laid out or not.
    const Ext::PhoneMenu::Rect r = Ext::PhoneMenu::SafeArea(2340, 1080, 0, 0, 400, 120);
    CHECK(r.x1 == 36);
    CHECK(r.y1 == 36);
    CHECK(r.x2 == 2340 - 36);   // a right / bottom anchor (the toolbar) moves in by the margin
    CHECK(r.y2 == 1080 - 36);
    const Ext::PhoneMenu::Rect first = Ext::PhoneMenu::SafeArea(2340, 1080, 0, 0, 0, 0);
    CHECK(first.x1 == 36);
    CHECK(first.y2 == 1080 - 36);
}

UNIT_TEST(ext_phone_menu_safe_area_is_a_floor_for_placed_dialogs) {
    // ComputeLayout puts a left / top aligned dialog at upperLeft + x1: the margin is a floor.
    const Ext::PhoneMenu::Rect inside = Ext::PhoneMenu::SafeArea(2340, 1080, 20, 30, 300, 100);
    CHECK(20 + inside.x1 == 36);   // a point inside the margin is pulled to its edge
    CHECK(30 + inside.y1 == 36);
    const Ext::PhoneMenu::Rect placed = Ext::PhoneMenu::SafeArea(2340, 1080, 700, 400, 300, 100);
    CHECK(placed.x1 == 0);         // a tooltip / drop-down / dragged panel stays where retail puts it
    CHECK(placed.y1 == 0);
    CHECK(placed.x2 == 2340 - 36); // and is still kept off the far edges
    CHECK(placed.y2 == 1080 - 36);
    const Ext::PhoneMenu::Rect above = Ext::PhoneMenu::SafeArea(2340, 1080, 100, -40, 300, 100);
    CHECK(above.y1 == 76);         // a tooltip above the screen's top: retail's clamp would leave it
                                   // off screen; the floor brings it to the margin
}

UNIT_TEST(ext_phone_menu_safe_bounds_match_the_layout) {
    // The S25 at UI 42 (982 x 453 layout units, margin 15): an entity panel 404 x 220.
    const Ext::PhoneMenu::Rect b = Ext::PhoneMenu::SafeBounds(982, 453, 404, 220);
    CHECK(b.x1 == 15);
    CHECK(b.y1 == 15);
    CHECK(b.x2 == 982 - 15);
    CHECK(b.y2 == 453 - 15);
    // An upper-left clamped into the bounds is one SafeArea leaves where it is (no snap).
    const Ext::PhoneMenu::Rect at = Ext::PhoneMenu::SafeArea(982, 453, b.x1, b.y1, 404, 220);
    CHECK(at.x1 == 0);
    CHECK(at.y1 == 0);
    CHECK(at.y2 == b.y2);   // and the far edge is the same
    // Taller than the safe band: that axis is retail's whole extent, as SafeArea's.
    const Ext::PhoneMenu::Rect tall = Ext::PhoneMenu::SafeBounds(982, 453, 404, 440);
    CHECK(tall.y1 == 0);
    CHECK(tall.y2 == 453);
    CHECK(tall.x1 == 15);
}

UNIT_TEST(ext_phone_menu_safe_area_keeps_retail_for_big_dialogs) {
    // Taller than the safe band (1080 - 72): the vertical axis is retail's, the other still inset.
    const Ext::PhoneMenu::Rect tall = Ext::PhoneMenu::SafeArea(2340, 1080, 0, 0, 900, 1040);
    CHECK(tall.y1 == 0);
    CHECK(tall.y2 == 1080);
    CHECK(tall.x1 == 36);
    CHECK(tall.x2 == 2340 - 36);
    const Ext::PhoneMenu::Rect exact = Ext::PhoneMenu::SafeArea(2340, 1080, 0, 0, 2340 - 72, 1080 - 72);
    CHECK(exact.x1 == 36);         // exactly the safe size still fits
    CHECK(exact.y2 == 1080 - 36);
    const Ext::PhoneMenu::Rect over = Ext::PhoneMenu::SafeArea(2340, 1080, 0, 0, 2340 - 72, 1080 - 71);
    CHECK(over.y1 == 0);           // one unit more does not
    // A screen too small for the margin: retail's whole screen.
    const Ext::PhoneMenu::Rect thin = Ext::PhoneMenu::SafeArea(1, 1000, 0, 0, 0, 0);
    CHECK(thin.x1 == 0);
    CHECK(thin.y1 == 0);
    CHECK(thin.x2 == 1);
    CHECK(thin.y2 == 1000);
}

UNIT_TEST(ext_phone_menu_slider_minimum) {
    // A screen not wider than the menus' backdrop (no floor): 25, or the platform's lower answer.
    CHECK(Ext::PhoneMenu::SliderMinimum(75.0f, 0) == 25.0f);   // retail's 75 at density 1
    CHECK(Ext::PhoneMenu::SliderMinimum(38.0f, 0) == 25.0f);   // a Retina window's 38
    CHECK(Ext::PhoneMenu::SliderMinimum(19.0f, 0) == 19.0f);   // a platform answer already lower is kept
    // A floor: the slider stops there.
    CHECK(Ext::PhoneMenu::SliderMinimum(75.0f, 35) == 35.0f);
    CHECK(Ext::PhoneMenu::SliderMinimum(19.0f, 35) == 35.0f);
}

UNIT_TEST(ext_phone_menu_ui_scale_floor) {
    const Ext::PhoneMenu::RowMetrics pause = Ext::PhoneMenu::PauseMenuMetrics();
    CHECK(pause.count == 10);
    CHECK(pause.contentSide == 68 + 2 * Ext::PhoneMenu::kSquareInset);
    // The S25 at render scale 100 (2340x1080): the pause menu's two small-font rows fit from 33 %
    // (a 772x356 layout), not at 32 %: the floor rounds up to 35.
    CHECK(Ext::PhoneMenu::UiScaleFloor(2340, 1080, pause, 16, 9, Ext::PhoneMenu::kVersionHeight) == 35);
    CHECK(Ext::PhoneMenu::PlanRows(772, 356, pause, 95, 284).rows == 2);
    CHECK(Ext::PhoneMenu::PlanRows(748, 345, pause, 92, 273).rows == 0);
    // At render scale 50 (1170x540) the same layout needs about twice the percent: 65 (P7's wider
    // margin: 70).
    CHECK(Ext::PhoneMenu::UiScaleFloor(1170, 540, pause, 16, 9, Ext::PhoneMenu::kVersionHeight) == 65);
    // A 16:10 desktop screen is not wider than the backdrop: no floor (the rows never apply).
    CHECK(Ext::PhoneMenu::UiScaleFloor(1920, 1200, pause, 16, 9, Ext::PhoneMenu::kVersionHeight) == 0);
    // The floor is a multiple of 5.
    CHECK(Ext::PhoneMenu::UiScaleFloor(2528, 1345, pause, 16, 9, Ext::PhoneMenu::kVersionHeight) % 5 == 0);
}

UNIT_TEST(ext_phone_menu_icon_side) {
    // A 78-unit square (the S25 at UI 35) holding a label 32 tall with its 16-unit icon: 0.45 of
    // the side is 35, which leaves the text and the 5-unit inset inside the square; rounded down to
    // twice the sprite, 32.
    CHECK(Ext::PhoneMenu::IconSide(78, 32, 16) == 32);
    // Little room: the text and the inset come first, and the icon never shrinks below its sprite.
    CHECK(Ext::PhoneMenu::IconSide(40, 32, 16) == 16);   // room for 14 only
    CHECK(Ext::PhoneMenu::IconSide(30, 32, 16) == 16);
    CHECK(Ext::PhoneMenu::IconSide(0, 32, 16) == 16);
    // A large square (108, the S25 at UI 100 / render scale 50): 49, three times the sprite: 48.
    CHECK(Ext::PhoneMenu::IconSide(108, 32, 16) == 48);
    // A sprite of another size scales by whole multiples of its own size.
    CHECK(Ext::PhoneMenu::IconSide(78, 30, 12) == 24);   // 35 rounded down to twice 12
}
