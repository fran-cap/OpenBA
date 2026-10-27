// ============================================================================
// src/ext/phone_menu/size_math.h -- ext.phone_menu's touch size for every control (P11)
// ============================================================================
// NOT RETAIL. Standard C++ only (tests/unit/ext_phone_size_test.cpp links size_math.cpp on its own).
// The rules are in touch_size.h; the session log is docs/android/P11_TOUCH_SIZE.md.
//
// UNITS. UI layout units, as touch_math.h (the back buffer * UserInterface::_uiScale).
// ============================================================================
#pragma once

#include <cstdint>

#include "touch_math.h"

namespace Ext {
namespace PhoneMenu {

// The side a control a finger presses is brought to with the row on (the owner, P11: "everything
// clickable gets about the height of the enlarged [x]"): the P9 close button, a 16-unit cross drawn
// at kCloseButtonFactor times its size inside StandardDialog.rsc's 2-unit icon padding on each side.
// 90 device pixels on the S25 at UI 40 (2.5 pixels per unit), about 5 mm.
constexpr int32_t kTouchCloseSprite = 16;
constexpr int32_t kTouchSide = kTouchCloseSprite * kCloseButtonFactor + 2 * 2;

// A slider's sprites (track, thumb) are drawn this whole multiple of their size with the row on, so
// every sprite pixel stays the same size (UI::SliderElement::_extSpriteScale).
constexpr int32_t kSliderSpriteFactor = 2;

// An icon's side (a button's image, a check box's tick) with the row on: twice the sprite's side when
// the doubled icon still fits inside kTouchSide with the close button's 2-unit padding (the same
// whole multiple as the close button's cross), else the sprite's own (a toolbar's 32-unit icons are
// already finger-sized). <= 0: 0 (no sprite: left alone).
int32_t TouchIconSide(int32_t spriteSide);

// Extra padding that brings a `padded` extent (a control's content with retail's padding) up to
// `target`: split in two, the smaller half first (before = top / left), so the content stays centred.
// Zero when it is already that large.
struct PadSplit {
    int32_t before = 0;
    int32_t after = 0;
};
PadSplit PadToTarget(int32_t padded, int32_t target);

// ---- a tall window's scrolling lists (touch_scroll.h (b), P11b) ------------------------------------

// The lowest height a list is capped at: three rows of finger-sized controls. A window still taller than
// the safe area with its lists there is shown smaller (touch_fit.h (c)).
constexpr int32_t kListFloor = 3 * kTouchSide;

// The cap (one height for every vertical list of a window) that takes `over` units off a window whose
// tallest list is `tallest` high now: tallest - over, at least `floor`. `cap` (the cap in force, 0 =
// none) is kept when the tallest list is already at or under the floor.
int32_t ShrinkCap(int32_t cap, int32_t tallest, int32_t over, int32_t floor);

// The cap in force (`cap` > 0) raised by the `slack` units the window has to spare; 0 (no cap: the
// description's heights) once it reaches `natural`, the tallest list's uncapped height.
int32_t GrowCap(int32_t cap, int32_t slack, int32_t natural);

}  // namespace PhoneMenu
}  // namespace Ext
