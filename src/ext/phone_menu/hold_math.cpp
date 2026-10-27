// ============================================================================
// src/ext/phone_menu/hold_math.cpp -- ext.phone_menu: where a finger's tooltip goes (P11c)
// ============================================================================
// The rules are in hold_math.h. Standard C++ only.
// ============================================================================

#include "hold_math.h"

namespace Ext {
namespace PhoneMenu {

namespace {

// The edges a `size` is kept between on one axis: the safe ones, or the whole 0 .. extent when the
// size does not fit between them.
void Axis(int32_t size, int32_t safeLo, int32_t safeHi, int32_t extent, int32_t& lo, int32_t& hi) {
    lo = safeLo;
    hi = safeHi;
    if (size > safeHi - safeLo) {
        lo = 0;
        hi = extent;
    }
}

}  // namespace

int32_t HoldTipLiftDevicePx(bool touchPointerOn, int32_t touchPointerLift) {
    if (touchPointerOn) return touchPointerLift > 0 ? touchPointerLift : 0;
    return kHoldTipLiftDevicePx;
}

int32_t DeviceToLayout(int32_t devicePx, float enginePerDevice, float uiScale) {
    const float v = static_cast<float>(devicePx) * enginePerDevice * uiScale;
    if (!(v > 0.0f)) return 0;
    return static_cast<int32_t>(v + 0.5f);
}

HoldTip PlaceHoldTip(int32_t fx, int32_t fy, int32_t w, int32_t h, int32_t lift, const Rect& safe, int32_t width,
                     int32_t height) {
    int32_t left = 0;
    int32_t right = 0;
    int32_t top = 0;
    int32_t bottom = 0;
    Axis(w, safe.x1, safe.x2, width, left, right);
    Axis(h, safe.y1, safe.y2, height, top, bottom);

    HoldTip tip;
    const int32_t above = fy - lift - h;
    const int32_t below = fy + lift;
    if (above >= top) {
        tip.y = above;
    } else if (below + h <= bottom) {
        tip.y = below;
        tip.below = true;
    } else {
        tip.y = top;
    }

    tip.x = fx;
    if (tip.x + w > right) tip.x = right - w;
    if (tip.x < left) tip.x = left;
    return tip;
}

}  // namespace PhoneMenu
}  // namespace Ext
