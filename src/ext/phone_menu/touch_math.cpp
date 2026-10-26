// ============================================================================
// src/ext/phone_menu/touch_math.cpp -- ext.phone_menu's dialog fit for a finger (P9)
// ============================================================================
// The rules are in touch_math.h. Standard C++ only.
// ============================================================================

#include "touch_math.h"

namespace Ext {
namespace PhoneMenu {

int32_t ScrollBarThickness(int32_t spriteThickness, int32_t retailThickness) {
    if (spriteThickness <= 0) return retailThickness;
    const int32_t wide = spriteThickness * kScrollBarFactor;
    return wide > retailThickness ? wide : retailThickness;
}

int32_t CloseIconSide(int32_t spriteSide) {
    if (spriteSide <= 0) return 0;
    return spriteSide * kCloseButtonFactor;
}

bool FitsSafeArea(int32_t layoutW, int32_t layoutH, int32_t dialogW, int32_t dialogH) {
    const Rect safe = InsetLayout(layoutW, layoutH);
    return dialogW <= safe.x2 - safe.x1 && dialogH <= safe.y2 - safe.y1;
}

float FitScale(float userScale, int32_t backBufferW, int32_t backBufferH, int32_t dialogW, int32_t dialogH) {
    if (backBufferW <= 0 || backBufferH <= 0 || dialogW <= 0 || dialogH <= 0) return userScale;
    // In whole percent, as the slider and UserInterface::Update would compute the layout size.
    const int32_t from = static_cast<int32_t>(userScale * 100.0f + 0.5f);
    const int32_t to = static_cast<int32_t>(kFitScaleMax * 100.0f + 0.5f);
    for (int32_t percent = from; percent <= to; ++percent) {
        const float scale = percent == from ? userScale : static_cast<float>(percent) / 100.0f;
        const int32_t w = static_cast<int32_t>(static_cast<float>(backBufferW) * scale);
        const int32_t h = static_cast<int32_t>(static_cast<float>(backBufferH) * scale);
        if (FitsSafeArea(w, h, dialogW, dialogH)) return scale;
    }
    return userScale > kFitScaleMax ? userScale : kFitScaleMax;
}

int32_t ToLayoutUnits(int32_t own, float k) {
    if (k == 1.0f) return own;
    const float v = static_cast<float>(own) * k;
    return static_cast<int32_t>(v < 0.0f ? v - 0.5f : v + 0.5f);
}

Rect LowerBackdrop(const Rect& top, int32_t width, int32_t height) {
    const int32_t pictureH = top.y2 - top.y1;
    if (width <= 0 || height <= 0 || pictureH <= 0 || top.y2 <= height) return Rect{0, 0, 0, 0};
    return Rect{top.x1, height - pictureH, top.x2, height};
}

Rect LowVersionLayout(int32_t width, int32_t height) {
    Rect r = InsetLayout(width, height);
    r.y2 = height;
    return r;
}

int32_t LowVersionTop(int32_t height, int32_t versionHeight) {
    if (versionHeight <= 0) return -1;
    return height - versionHeight;
}

}  // namespace PhoneMenu
}  // namespace Ext
