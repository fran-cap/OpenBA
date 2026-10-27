// ============================================================================
// src/ext/phone_menu/touch_math.h -- ext.phone_menu's dialog fit for a finger (P9)
// ============================================================================
// NOT RETAIL. Standard C++ only (tests/unit/ext_phone_touch_test.cpp links touch_math.cpp and
// phone_math.cpp on their own). docs/extensions/handins/PHONEMENU.md §2 has the why; the session
// log is docs/android/P9_DIALOGS.md.
//
// UNITS. Element extents are UI layout units (the back buffer * _uiScale); the scale is
// UserInterface::_uiScale (layout units per device pixel: 0.40 at the Options slider's 40 %).
// ============================================================================
#pragma once

#include <cstdint>

#include "phone_math.h"

namespace Ext {
namespace PhoneMenu {

// ---- scroll bars and close buttons ------------------------------------------

// How much thicker a scroll bar is drawn with the row on (owner's ruling, P9: "scroll bars about 4x
// wider on the phone"): the game's bars (SharedElements.rsc:verticalScrollbar) are a 12-unit sprite
// wide, 48 units with the row on: 120 device pixels on the S25 at UI 40 (2.5 pixels per unit), about
// 7 mm, a thumb a finger can hold.
constexpr int32_t kScrollBarFactor = 4;

// How much larger a dialog's close button's icon is drawn (owner's P9 note: "the close [x] buttons
// larger on the phone"): the game's 16-unit cross (StandardDialog.rsc:buttonClose) at twice its size,
// a whole multiple so every sprite pixel stays the same size.
constexpr int32_t kCloseButtonFactor = 2;

// The thickness (across the bar) a scroll bar is laid out at with the row on: kScrollBarFactor times
// its track sprite's own (`spriteThickness`), never thinner than retail's (`retailThickness`, the
// bar's padded sprite or its description's minimum, whichever is larger). <= 0 sprite: retail's.
int32_t ScrollBarThickness(int32_t spriteThickness, int32_t retailThickness);

// The close button's icon side with the row on: kCloseButtonFactor times the sprite's; <= 0: 0.
int32_t CloseIconSide(int32_t spriteSide);

// ---- a dialog larger than the safe area -------------------------------------------

// True when a dialog `dialogW` x `dialogH` (padded, layout units) fits inside the safe area of a
// `layoutW` x `layoutH` screen (InsetLayout).
bool FitsSafeArea(int32_t layoutW, int32_t layoutH, int32_t dialogW, int32_t dialogH);

// The UI scale (layout units per device pixel) a `backBufferW` x `backBufferH` screen is laid out at
// so that a dialog `dialogW` x `dialogH` fits in its safe area: `userScale` when it already fits;
// else the lowest scale above it, in steps of 0.01 (the Options slider's 1 %), at which it fits; at
// most kFitScaleMax (retail's own 100 %: a dialog too large there is retail's). Layout sizes are
// UserInterface::Update's `(int)(back buffer * scale)`. The dialog's size in layout units does not
// change with the scale, so the answer is the whole fit.
constexpr float kFitScaleMax = 1.0f;
float FitScale(float userScale, int32_t backBufferW, int32_t backBufferH, int32_t dialogW, int32_t dialogH);

// A length or point in a fitted dialog's own units (own = layout / k, touch_fit.h (c)) in the
// interface's layout units: `own * k`, rounded to the nearest unit. k == 1: `own` unchanged.
int32_t ToLayoutUnits(int32_t own, float k);

// ---- the menus' backdrop, its bottom border -----------------------------------------

// The extra draw of the menus' 16:9 backdrop on a screen wider than it (P9, the owner: "the black
// fringe image is missing"). LogoTop puts the picture's top on the screen's top, so its bottom
// border (the dark torn edge, the picture's last tenth or so) falls below the screen. The extra draw
// is the same picture with its BOTTOM on the screen's bottom, drawn BEFORE the moved one: the moved
// picture's opaque top border then covers what the extra copy has above it (the lower edge of a
// second logo), and its transparent middle lets the extra copy's bottom border through.
// `top` is the moved picture's rectangle (LogoTop's) on a `width` x `height` screen; the answer is
// empty (x2 <= x1) when the picture does not overhang the screen's bottom (it is whole there).
Rect LowerBackdrop(const Rect& top, int32_t width, int32_t height);

// ---- the version line under the menus -------------------------------------------------

// With the backdrop's bottom border on screen (LowerBackdrop) the version line sits on the screen's
// bottom edge instead of the safe area's, inside the dark border (the owner, P9: "the version text
// could be farther down to be all in the black area"); a landscape phone's rounded corners and
// camera cut-out are not under the bottom edge's middle, where the centred line is.
// The version line's layout rectangle: the safe area with its bottom on the screen's bottom.
Rect LowVersionLayout(int32_t width, int32_t height);

// Its top for a line `versionHeight` tall (padded); -1 for a height <= 0.
int32_t LowVersionTop(int32_t height, int32_t versionHeight);

}  // namespace PhoneMenu
}  // namespace Ext
