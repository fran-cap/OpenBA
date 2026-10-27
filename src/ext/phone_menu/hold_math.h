// ============================================================================
// src/ext/phone_menu/hold_math.h -- ext.phone_menu: where a finger's tooltip goes (P11c)
// ============================================================================
// NOT RETAIL. Standard C++ only (tests/unit/ext_phone_hold_test.cpp links hold_math.cpp on its own).
// The rule and the seam are in touch_hold.h; the session log is docs/android/P11C_TOOLTIP.md.
//
// UNITS. UI layout units (the back buffer * UserInterface::_uiScale), the global ones: a tooltip at
// its own scale k (touch_fit.h (c)) is converted by the caller.
// ============================================================================
#pragma once

#include <cstdint>

#include "phone_math.h"

namespace Ext {
namespace PhoneMenu {

// The tooltip's height above the finger when ext.touch_pointer is off (device pixels): that row's
// default lift (registry.h `touch_pointer_lift`), the distance the owner ruled (2026-10-26).
constexpr int32_t kHoldTipLiftDevicePx = 160;

// The lift in device pixels: ext.touch_pointer_lift with that row on, else kHoldTipLiftDevicePx.
int32_t HoldTipLiftDevicePx(bool touchPointerOn, int32_t touchPointerLift);

// Device pixels as layout units: `enginePerDevice` back-buffer pixels per screen pixel (the render
// scale, Platform::Touch::EnginePixelsPerDevicePixel) times `uiScale` layout units per back-buffer
// pixel (UserInterface::_uiScale; ui_render_scale.h). Rounded; never negative.
int32_t DeviceToLayout(int32_t devicePx, float enginePerDevice, float uiScale);

struct HoldTip {
    int32_t x = 0;        // the tooltip's padded upper-left
    int32_t y = 0;
    bool below = false;   // flipped under the finger: no room above it
};

// The upper-left of a `w` x `h` tooltip (padded) for a finger at (fx, fy), inside `safe`
// (ext.phone_menu's safe area, phone_math.h InsetLayout) on a `width` x `height` layout screen:
//   * y: `lift` above the finger (its bottom at fy - lift); when that crosses the safe top, `lift`
//     below it (its top at fy + lift); when neither fits, at the safe top (above the finger, as far
//     from it as the screen allows);
//   * x: retail's, its left at the finger, pulled inside the safe area's left / right edges;
//   * an axis the tooltip does not fit in between the margins uses the whole screen (SafeArea's rule).
HoldTip PlaceHoldTip(int32_t fx, int32_t fy, int32_t w, int32_t h, int32_t lift, const Rect& safe, int32_t width,
                     int32_t height);

}  // namespace PhoneMenu
}  // namespace Ext
