#pragma once
// ============================================================================
// src/ext/phone_menu/loading_math.h -- ext.phone_menu's loading-screen fit (pure)
// ============================================================================
// NOT RETAIL (P9, docs/android/P9_LOADING_SCALE.md §1). Standard C++ only
// (tests/unit/ext_phone_loading_test.cpp). Units: the UI layout space, as phone_math.h.
//
// The loading screen (Dialog/Loading.rsc, the LoaderDialog of every GameLoadDialogLoopT) is an OPAQUE
// 16:9 picture with torn black borders at its top and bottom, and one bottom-centred group (`main`:
// the spinner, "Loading...", the tip). On a screen wider than the picture retail's centred cover fit
// cuts both borders. The owner's ruling (P9): look like the pause menu, both torn borders whole at the
// menus' width-fitted picture scale. The picture is drawn top-aligned (phone_math.h LogoTop: its top
// border whole) and then once more, bottom-aligned at the same scale (LowerCopy), OVER it but clipped
// to the bottom strip that holds its torn bottom border (BorderStripTop): the strip shows the border
// whole, the group sits in it.
// P11 (docs/android/P11_PHONE_BUGS.md §2): a straight clip through the picture's own torn edge leaves
// the lower copy's picture showing between the edge's peaks (a band that does not match the picture
// above the line: "a sliver of the picture at the bottom"). The torn bottom border drawn now is the
// menus' see-through overlay's (Dialog/MainMenu.rsc's backdrop, kept loaded past the menu loop),
// bottom-aligned at the picture's width: above its torn edge it is transparent, so the clip line can
// sit well above the edge and the picture shows through continuously (OverlayLower). The lower copy
// of the picture (LowerCopy) stays the fallback for a loading screen drawn before any menu opened.
// ============================================================================

#include "ext/phone_menu/phone_math.h"

#include <cstdint>

namespace Ext {
namespace PhoneMenu {

// The part of the loading picture's height the clipped strip takes, from the picture's bottom:
// measured on the installed pictures (the solid black from about 0.897 of the drawn height down, the
// torn edge reaching up to between 0.857 and 0.885 depending on the picture). 0.125 sits inside the
// torn edge: a higher line shows a band of the lower copy's picture above the edge (a visible seam),
// a lower one cuts the highest peaks of the edge flat. A number about the user's own data.
constexpr float kLoadingBorderFraction = 0.125f;

// The screen's bottom edge in layout units, rounded UP. UserInterface's layout height is the back
// buffer times the UI scale TRUNCATED (453 for 1080 x 0.42 = 453.6), so a draw anchored to it ends a
// fraction of a unit short of the screen: one or two device pixels of whatever is under it stay
// visible along the bottom edge (the loading picture's sliver the owner saw under its border, P11).
// The bottom-aligned draws below anchor to this instead. A scale or height <= 0: `layoutHeight`.
inline int32_t ScreenBottomUnits(int32_t layoutHeight, int32_t backBufferH, float scale) {
    if (backBufferH <= 0 || scale <= 0.0f) return layoutHeight;
    const float exact = static_cast<float>(backBufferH) * scale;
    int32_t bottom = static_cast<int32_t>(exact);
    if (static_cast<float>(bottom) < exact) ++bottom;
    return bottom > layoutHeight ? bottom : layoutHeight;
}

// The share of the overlay's height the clipped overlay strip takes, from its bottom: inside the
// overlay's transparent middle (its torn bottom border reaches up to about 0.14 of its height, its
// logo down to about 0.25 from its top), so the line itself never shows.
constexpr float kOverlayStripFraction = 0.30f;

// The menus' overlay (a `spriteW` x `spriteH` sprite) drawn bottom-aligned at the width the loading
// picture has in `top` (its top-aligned rectangle) on a screen `height` units high. Empty
// ({0, 0, 0, 0}) when `top` does not overhang the screen's bottom (its own border is whole there) or
// the sprite has no size.
inline Rect OverlayLower(const Rect& top, int32_t height, int32_t spriteW, int32_t spriteH) {
    const int32_t w = top.x2 - top.x1;
    if (w <= 0 || height <= 0 || spriteW <= 0 || spriteH <= 0 || top.y2 <= height) return Rect{0, 0, 0, 0};
    const int32_t h = static_cast<int32_t>(static_cast<float>(w) * static_cast<float>(spriteH) /
                                           static_cast<float>(spriteW) + 0.5f);
    return Rect{top.x1, height - h, top.x2, height};
}

// The top of the strip the overlay is clipped to: the screen's bottom less kOverlayStripFraction of
// the overlay's drawn height.
inline int32_t OverlayStripTop(int32_t height, int32_t overlayH) {
    return height - static_cast<int32_t>(static_cast<float>(overlayH) * kOverlayStripFraction + 0.5f);
}

// The picture drawn bottom-aligned at the size it has in `top` (its top-aligned rectangle): its bottom
// on the screen's bottom. Empty ({0, 0, 0, 0}) when `top` does not overhang the screen's bottom (the
// border is already whole).
inline Rect LowerCopy(const Rect& top, int32_t height) {
    const int32_t pictureH = top.y2 - top.y1;
    if (pictureH <= 0 || height <= 0 || top.y2 <= height) return Rect{0, 0, 0, 0};
    return Rect{top.x1, height - pictureH, top.x2, height};
}

// The top of the strip the lower copy is clipped to (layout units from the screen's top): the
// screen's bottom less kLoadingBorderFraction of the picture's height.
inline int32_t BorderStripTop(int32_t height, int32_t pictureH) {
    return height - static_cast<int32_t>(static_cast<float>(pictureH) * kLoadingBorderFraction + 0.5f);
}

}  // namespace PhoneMenu
}  // namespace Ext
