// ============================================================================
// src/ext/phone_menu/phone_math.h -- ext.phone_menu's pure decisions
// ============================================================================
// NOT RETAIL. Standard C++ only (tests/unit/ext_phone_menu_test.cpp links
// phone_math.cpp on its own). docs/extensions/handins/PHONEMENU.md has the why.
//
// UNITS. Every extent here is in the units the caller hands in: the UI layout
// space (back buffer * _uiScale), which is also the space UserInterface::Draw
// computes a dialog's backdrop rectangle in. The answers depend only on ratios
// of their inputs, so they hold at any UI scale.
//
// TWO LAYOUTS. The main menu is a 3D scene (Game/MenuScene) under the menu
// dialog, whose backdrop is a 16:9 overlay: the Banished logo near its top and
// the dark torn borders, transparent between them.
//   * On a screen WIDER than the overlay (a 19.5:9 phone) retail's centred cover
//     fit cuts the logo's top off. There the overlay is drawn with its top on
//     the screen's top (LogoTop: the logo whole, centred at the top) and, when
//     it fits, the menu's buttons become a HORIZONTAL ROW OF SQUARE BUTTONS
//     across the middle of the safe area, wrapped to two rows when one does not
//     fit (PlanRows), the version line centred at the bottom.
//   * Otherwise (a desktop's 16:9 or 16:10, or a row too wide for the screen)
//     the overlay keeps retail's fit on a desktop and the column stays centred,
//     kept inside the safe area and clear of the logo and the version line
//     (MenuBand, VersionAside).
// Every decision is made from THIS frame's sizes (the caller sizes the tree
// before it asks), so the first frame a menu is drawn is already its final one.
// ============================================================================
#pragma once

#include <cstdint>

namespace Ext {
namespace PhoneMenu {

// The safe-area margin as a fraction of the screen height. A phone held in
// landscape loses its corners to the rounded display (the S25's corner radius
// is about 3.5 % of its 1080-pixel height) and the middle of one short edge to
// the camera cut-out (about 3 % of the height deep). 4.5 % clears both with a
// little air and is still small enough that nothing centred moves: on a desktop
// screen the inset only shows on an element anchored to an edge.
constexpr float kSafeMarginFraction = 0.045f;

// The Options > UI scale slider's lowest value with the row on (percent).
// _uiScale = percent / 100 is LAYOUT UNITS PER DEVICE PIXEL, so 25 draws the UI
// four times larger than 100: what a 6-inch 1080-pixel screen needs to come near
// a desktop's physical size. Retail's lowest is 75.
constexpr float kUiScaleMinPercent = 25.0f;

// Where the Banished logo ends inside the main menu's backdrop, as a fraction of the drawn
// picture's height from its top: measured on the installed pictures (the logo's lowest
// pixel, the descender of its long s, at 0.210 of the drawn height), plus about one percent
// of air. A number about the user's own data, not a copy of it; a mod that repaints the
// backdrop with the logo elsewhere only moves the band the buttons are kept out of.
constexpr float kLogoBottomFraction = 0.22f;

// The gap kept between the buttons and the version line (layout units).
constexpr int32_t kVersionGap = 4;

struct Rect {
    int32_t x1, y1, x2, y2;
};

// round(height * kSafeMarginFraction); 0 for a height <= 0.
int32_t SafeMargin(int32_t height);

// The rectangle a dialog is laid out in on a width x height screen: the screen
// inset by SafeMargin(height) on every side (DialogInstance::ComputeLayout takes
// the right and bottom EDGES, not sizes). A margin that would leave nothing is
// dropped (the whole screen).
Rect InsetLayout(int32_t width, int32_t height);

// ---- the main menu's backdrop ----------------------------------------------

// True when the screen is wider than the backdrop picture (spriteW x spriteH), by retail's own
// compare in UserInterface::Draw (0x180121dbe): `spriteAspect > screenAspect` fits the height
// (false here), anything else fits the width and overhangs top and bottom (true). A picture or
// screen with no size: false.
bool WiderThanBackdrop(int32_t width, int32_t height, int32_t spriteW, int32_t spriteH);

// The height retail draws the backdrop at: on a wider screen the width-fitted picture
// (`2 * ((int)(width / aspect + 0.5) >> 1)`, Draw's arithmetic), otherwise the screen's height.
int32_t BackdropHeight(int32_t width, int32_t height, int32_t spriteW, int32_t spriteH);

// Retail's backdrop rectangle (`retail`) moved so its top is on the screen's top when it
// overhangs it (a wider screen): the logo is whole and stays centred; the cut moves to the
// bottom border. A rectangle that does not overhang the top is returned unchanged.
Rect LogoTop(const Rect& retail);

// Where the logo ends (layout units from the screen's top) in a backdrop drawn
// `backdropHeight` tall with its top on the screen's top.
int32_t LogoBottom(int32_t backdropHeight);

// ---- the button row (a screen wider than the backdrop) ----------------------

// The room kept between a label and its square's edge (layout units): the button frame's own
// inset (MainMenu.rsc's `button*BorderPad` frames are padded 3 in from the button), so the
// longest label ("Achievements") keeps 2 units of air inside the frame instead of touching it.
constexpr int32_t kSquareInset = 5;

// The side the labels need: the largest of their padded widths and heights (`count` of each)
// plus kSquareInset on both sides, so every label fits inside its square's frame. 0 when there
// is none.
int32_t SquareSide(const int32_t* labelWidths, const int32_t* labelHeights, int32_t count);

// The largest square side as a fraction of the safe area's height: the squares fill the row
// across the safe area up to this, so on a large screen they stay buttons, not tiles.
constexpr float kRowSideFraction = 0.22f;

// The band the rows are centred in (their dialog's root is MidCenter, handed this rectangle):
// the safe area below the logo (`logoBottom`) and kVersionGap above the version line
// (`versionTop`); either < 0: unknown, the safe area's edge.
Rect RowBand(int32_t width, int32_t height, int32_t logoBottom, int32_t versionTop);

// RowBand's height, the tallest the rows' dialog may be; 0 when there is no room.
int32_t BlockHeight(int32_t width, int32_t height, int32_t logoBottom, int32_t versionTop);

// What the row is made of, measured on the menu with its buttons `contentSide` square in ONE
// row (all in layout units).
struct RowMetrics {
    int32_t count = 0;            // visible buttons
    int32_t contentSide = 0;      // the side the labels need (SquareSide)
    int32_t outerWidth = 0;       // the dialog's padded width less the buttons and the gaps between them
    int32_t outerHeight = 0;      // the dialog's padded height less one row of buttons
    int32_t buttonPadWidth = 0;   // a button's left + right padding
    int32_t buttonPadHeight = 0;  // a button's top + bottom padding
    int32_t spacing = 0;          // the gap between two squares, across a row and between the rows
};

// The dialog's padded size with `perRow` squares of `side` across and `rows` rows.
int32_t RowsWidth(const RowMetrics& m, int32_t perRow, int32_t side);
int32_t RowsHeight(const RowMetrics& m, int32_t rows, int32_t side);

struct RowPlan {
    int32_t rows = 0;     // 1 or 2; 0: no row fits (the caller tries the small font, then the column)
    int32_t perRow = 0;   // squares on the first row: ceil(count / rows); the second has the rest
    int32_t side = 0;     // the square side
};

// The row for a width x height screen. One row if it fits, else TWO rows (the first ceil(n / 2)
// squares, the rest centred under them: a button added to the menu wraps, it never breaks the
// layout). The side fills the safe width for the longer row, is capped by kRowSideFraction of the
// safe height and by the band the rows must fit in between the logo and the version line
// (BlockHeight), and is never smaller than the labels need. Nothing fits: rows 0.
RowPlan PlanRows(int32_t width, int32_t height, const RowMetrics& m, int32_t logoBottom, int32_t versionTop);

// ---- the centred column (any other screen, and the pause menu) ---------------

// The band the menu's button column is centred in, on a width x height screen:
//   * by default the safe area (InsetLayout);
//   * below the logo (`logoBottom`, < 0 when unknown) and above the version line
//     (`versionTop` less kVersionGap, < 0 when unknown), when the column (`columnHeight`, its
//     laid-out height, <= 0 when unknown) fits there: at a large UI scale the centred
//     column would otherwise cover the logo and run under the version line;
//   * else above the version line alone, when it fits between the screen's top edge and
//     the line (its top may then rise into the margin: the buttons stay whole and clear
//     of the version line, which matters more than the margin at a 25 % UI);
//   * else the safe area (the column is taller than the screen allows; retail's own
//     ComputeLayout then keeps its bottom on screen).
Rect MenuBand(int32_t width, int32_t height, int32_t logoBottom, int32_t versionTop,
              int32_t columnHeight);

// True when the version line should leave the bottom centre for the bottom-left corner of
// the safe area: the button column (centred, `columnWidth` x `columnHeight`) cannot sit
// above the line (`versionTop`) and the line (`versionWidth` wide) fits beside the column
// with kVersionGap to spare. Any size <= 0 (not laid out yet): false.
bool VersionAside(int32_t width, int32_t height, int32_t versionTop, int32_t versionWidth,
                  int32_t columnWidth, int32_t columnHeight);

// The version line's layout rectangle when it is aside: the safe area's left part, as wide
// as the line, so its bottom-centre alignment puts it in the bottom-left corner.
Rect VersionAsideLayout(int32_t width, int32_t height, int32_t versionWidth);

// The top of the version line (`versionHeight` tall, padded) laid out bottom-centred in the
// safe area; -1 for a height <= 0.
int32_t VersionTop(int32_t width, int32_t height, int32_t versionHeight);

// ---- every other dialog (the in-game HUD, its panels, the front end's other dialogs) ----

// The rectangle DialogInstance::ComputeLayout (retail 0x180113d90) is handed for a dialog that is
// not one of the menus above, so that what retail anchors to an edge or a corner (the toolbar and
// the event notices bottom-right, the autosave notice top-left, a panel opened at its default
// upper-left (0, 0)) sits inside the safe area. ComputeLayout places a dialog at
// `_upperLeftX + x1` (left / top alignment) or against `x2` less the dialog's width (right /
// bottom), centres it between the two, and pulls one that would cross `x2` back to `x2`. So:
//   * the far edges are the safe area's (x2 = width - m, y2 = height - m): right / bottom anchors
//     move in, centred dialogs do not move, and nothing is pushed past the safe edge;
//   * the near edges are a FLOOR, not an offset: x1 = max(0, m - upperLeftX), so the dialog's
//     left is max(upperLeftX, m). A dialog put at a screen point (a tooltip at the cursor, a combo
//     box's drop-down under its box, a panel dragged by the player) stays exactly where retail
//     puts it unless that point is inside the margin; a panel at its default (0, 0) opens at the
//     safe corner. (Dragging a panel out of the corner then moves it once the cursor has gone
//     past the margin: the dead zone retail itself has at the right and bottom edges.)
//   * an axis on which the dialog (`lastWidth` / `lastHeight`, as laid out; <= 0 when not laid
//     out yet) is wider than the safe area keeps retail's whole extent (0 .. width): the
//     margin is not worth cutting a dialog off for;
//   * a screen too small for the margin keeps retail's rectangle (InsetLayout's rule).
Rect SafeArea(int32_t width, int32_t height, int32_t upperLeftX, int32_t upperLeftY, int32_t lastWidth,
              int32_t lastHeight);

// The rectangle DialogInstance::SetUpperLeft clamps a dialog's upper-left into (P11b, the seam's
// kExtPlaceUpperLeft; retail's is {0, 0, width, height}), for a dialog of `dialogWidth` x
// `dialogHeight`: the safe area on each axis the dialog fits in with its margins, retail's whole
// extent on an axis it does not (SafeArea's rule). An upper-left clamped into it is one SafeArea
// keeps: SetUpperLeft then puts the dialog where its next layout leaves it. Without it a dialog a
// game tool places after the layout (an entity's panel above its building, every frame) is drawn
// at retail's clamp for one frame and pulled to the margin by the next layout (the snap).
Rect SafeBounds(int32_t width, int32_t height, int32_t dialogWidth, int32_t dialogHeight);

// The upper-left a laid-out dialog's root is at (`root`: its laid-out {left, top, right, bottom},
// `leftPad` / `topPad` its pads), on the axes its `alignment` (ElementDescription::Alignment: 0..8,
// x = alignment % 3, y = alignment / 3) anchors at the upper-left; `upperLeftX` / `upperLeftY` are
// left as they are on a centred or right / bottom anchored axis and for a root not laid out yet
// (empty). DialogInstance::SetUpperLeft moves the laid-out tree by (new - _upperLeft), so when the
// layout put the root elsewhere (ComputeLayout pulls a dialog that crosses the far edge back to it;
// the safe area raises one inside the margin) the move lands it off by that difference for one
// frame, until the next layout (P11b: an entity's panel placed at its unbuilt size, 88 units high,
// then laid out at 220 against the bottom margin, was drawn 3 units too high, then snapped down).
void LaidOutUpperLeft(int32_t alignment, const Rect& root, int32_t leftPad, int32_t topPad, int32_t& upperLeftX,
                      int32_t& upperLeftY);

// ---- the icons in the squares ----------------------------------------------------

// The icon's share of the square's side (owner's ruling, P8 round 4: "the icons could be a bit
// larger in mobile mode"); the label's text stays under it.
constexpr float kIconFraction = 0.45f;

// The icon height for a square of `side` holding a label `labelHeight` tall whose icon is
// `iconHeight` (both with the icon at its sprite size, icon above text): kIconFraction of the
// side, but no taller than leaves the text and kSquareInset on both sides inside the square,
// rounded down to a whole multiple of the sprite's size (the icons are 16-pixel sprites; a whole
// multiple scales every sprite pixel alike), and never smaller than the sprite's own size. <= 0
// inputs: `iconHeight`.
int32_t IconSide(int32_t side, int32_t labelHeight, int32_t iconHeight);

// ---- the Options slider and the UI scale floor ----------------------------------------

// The pause menu (the menu with the most buttons) as measured on the S25 in the small font: ten
// buttons, "Achievements" 68 wide plus kSquareInset on both sides, the ribbon's 8-unit padding,
// 2-unit button padding, 2 units between squares. Numbers about the user's own data.
RowMetrics PauseMenuMetrics();

// The version line's padded height (Dialog/BuildId.rsc: a 48-unit logo plus 8 under it).
constexpr int32_t kVersionHeight = 56;

// The lowest UI scale (percent) at which the pause menu still fits as rows of squares (PlanRows,
// in the small font) on a back buffer `backBufferWidth` x `backBufferHeight` (the render scale's
// size) under a backdrop of aspect spriteW : spriteH, rounded UP to a multiple of 5 (owner's
// ruling, P8 round 4: the slider stops there instead of letting the menus fall back to a column
// that runs off the screen). 0 when the screen is not wider than the backdrop (the rows never
// apply there; the slider keeps kUiScaleMinPercent). 100 when nothing below fits.
int32_t UiScaleFloor(int32_t backBufferWidth, int32_t backBufferHeight, const RowMetrics& menu,
                     int32_t spriteW, int32_t spriteH, int32_t versionHeight);

// The slider minimum with the row on: `floorPercent` when there is one (> 0), else the lower of
// the platform's answer (Platform::Display::UiScaleSliderRange) and kUiScaleMinPercent.
float SliderMinimum(float platformMinPercent, int32_t floorPercent);

}  // namespace PhoneMenu
}  // namespace Ext
