// ============================================================================
// src/ext/phone_menu/size_math.cpp -- ext.phone_menu's touch size for every control (P11)
// ============================================================================
// The rules are in size_math.h. Standard C++ only.
// ============================================================================

#include "size_math.h"

namespace Ext {
namespace PhoneMenu {

int32_t TouchIconSide(int32_t spriteSide) {
    if (spriteSide <= 0) return 0;
    const int32_t doubled = spriteSide * kCloseButtonFactor;
    if (doubled + 2 * 2 <= kTouchSide) return doubled;
    return spriteSide;
}

PadSplit PadToTarget(int32_t padded, int32_t target) {
    PadSplit split;
    if (padded >= target) return split;
    const int32_t extra = target - padded;
    split.before = extra / 2;
    split.after = extra - split.before;
    return split;
}

int32_t ShrinkCap(int32_t cap, int32_t tallest, int32_t over, int32_t floor) {
    if (over <= 0 || tallest <= floor) return cap;
    const int32_t want = tallest - over;
    return want > floor ? want : floor;
}

int32_t GrowCap(int32_t cap, int32_t slack, int32_t natural) {
    if (cap <= 0 || slack <= 0) return cap;
    const int32_t want = cap + slack;
    return want >= natural ? 0 : want;
}

int32_t FrameMinHeight(int32_t retailMin, int32_t taken) {
    if (taken <= 0) return retailMin;
    const int32_t want = retailMin - taken;
    return want > 0 ? want : 0;
}

}  // namespace PhoneMenu
}  // namespace Ext
