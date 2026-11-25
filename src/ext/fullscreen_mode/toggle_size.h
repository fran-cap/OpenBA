// src/ext/fullscreen_mode/toggle_size.h -- NOT RETAIL. ext.fullscreen_mode: the window Alt+Enter opens
//
// Retail goes from fullscreen to a window only through the Options dialog, which opens it at the saved
// window size (Video::Configuration::windowWidth / Height; -1 until a window was ever sized) or, with
// none, at the display mode's size. Alt+Enter is the way out of a fullscreen that went wrong, so the
// window it opens has to be one the player can reach: the saved size when it fits the display, else the
// mode's size when that fits, else three quarters of the display. Sizes are client sizes in engine
// pixels; the saved size fits when it is no larger than the display, the mode's only when it leaves
// room for the frame (strictly smaller on both axes). An unknown display (0 x 0) filters nothing.
#pragma once

#include <cstdint>

namespace Ext {
namespace FullscreenMode {

inline bool FitsDisplay(int32_t w, int32_t h, int32_t displayW, int32_t displayH, bool roomForFrame) {
    if (w <= 0 || h <= 0) return false;
    if (displayW <= 0 || displayH <= 0) return true;
    if (roomForFrame) return w < displayW && h < displayH;
    return w <= displayW && h <= displayH;
}

inline void WindowSizeForToggle(int32_t savedW, int32_t savedH, int32_t modeW, int32_t modeH, int32_t displayW,
                                int32_t displayH, int32_t* outW, int32_t* outH) {
    // The saved size is one the player's own window had (a maximised window is as wide as the display).
    if (FitsDisplay(savedW, savedH, displayW, displayH, false)) {
        *outW = savedW;
        *outH = savedH;
        return;
    }
    if (FitsDisplay(modeW, modeH, displayW, displayH, true)) {
        *outW = modeW;
        *outH = modeH;
        return;
    }
    if (displayW > 0 && displayH > 0) {
        *outW = displayW * 3 / 4;
        *outH = displayH * 3 / 4;
        return;
    }
    *outW = 1280;
    *outH = 720;
}

}  // namespace FullscreenMode
}  // namespace Ext
