#pragma once
// ============================================================================
// src/platform/common/mac_keymap.h -- the Mac keyboard layer over SDL's key state
// ============================================================================
// The game was written for a full Windows keyboard. A Mac keyboard lacks Insert, Print
// Screen, Scroll Lock and Pause, hides Home / End / Page Up / Page Down / Delete behind fn,
// and its primary shortcut modifier is Command, not Control. This header is the pure
// (no SDL header, no OS header) rule set that turns SDL's per-scan-code key state into the
// state the DIK table is read from (src/platform/darwin/input_darwin.cpp applies it on macOS
// as the ApplyKeyLayer hook, which src/platform/sdl/input_sdl.cpp's ReadKeyboard and the legacy
// AsyncKeyState both read). Documented for players in
// docs/reference/macos_input_window.md; tested in tests/unit/mac_keymap_test.cpp.
//
// THE RULES (state in -> state out; SDL scan code numbers, SDL_scancode.h)
//  1. Command is Control. Either Command key down sets Left Control in the output and is
//     itself cleared (the Windows key slot, DIK_LWIN, is a hotkey nobody wants on a Mac).
//     Physical Control still works.
//  2. Right Option is the "Windows keyboard" layer. While it is down:
//        Up -> Page Up      Down -> Page Down     Left -> Home       Right -> End
//        Backspace (the Mac "delete" key) -> Delete                  Return -> Insert
//        F10 -> Scroll Lock   F11 -> Print Screen   F12 -> Pause
//     The pressed key is replaced by its target, and Right Option is cleared (the layer
//     key is consumed whenever a mapping fires; held alone it stays Right Alt).
//     The fn combinations a Mac keyboard already sends (fn+Up = Page Up, fn+Down, fn+Left =
//     Home, fn+Right = End, fn+Delete = forward Delete) need no rule: SDL delivers them
//     as the Windows keys, and they pass through untouched.
//  3. Apple extended keyboards' F13 / F14 / F15 sit where Print Screen / Scroll Lock /
//     Pause do on a PC: F13 -> Print Screen, F14 -> Scroll Lock, F15 -> Pause.
//
//  4. A trackpad has no middle button, and the camera rotates on middle-drag. Option held
//     with the left button turns the left button into the middle button (MouseButtonRemapper;
//     either Option key). Two-finger click is the system's right button already.
//
// Also here: the window chord, Control+Command+F (macOS's own full-screen shortcut), which
// the SDL pump consumes to toggle native full screen.
// ============================================================================

#include <cstdint>

namespace Platform {
namespace Input {
namespace MacKeys {

// SDL_Scancode values (SDL_scancode.h), spelled as numbers so no SDL header is needed;
// tests/unit/mac_keymap_test.cpp checks them against SDL's real enum.
enum : int {
    kBackspace = 42,
    kReturn = 40,
    kF9 = 66,
    kF10 = 67,
    kF11 = 68,
    kF12 = 69,
    kPrintScreen = 70,
    kScrollLock = 71,
    kPause = 72,
    kInsert = 73,
    kHome = 74,
    kPageUp = 75,
    kDelete = 76,
    kEnd = 77,
    kPageDown = 78,
    kRight = 79,
    kLeft = 80,
    kDown = 81,
    kUp = 82,
    kF13 = 104,
    kF14 = 105,
    kF15 = 106,
    kLCtrl = 224,
    kLAlt = 226,
    kLGui = 227,
    kRCtrl = 228,
    kRAlt = 230,
    kRGui = 231,
    kStateCount = 512,
};

struct LayerEntry {
    int from;
    int to;
};

inline constexpr LayerEntry kOptionLayer[] = {
    { kUp, kPageUp },
    { kDown, kPageDown },
    { kLeft, kHome },
    { kRight, kEnd },
    { kBackspace, kDelete },
    { kReturn, kInsert },
    { kF10, kScrollLock },
    { kF11, kPrintScreen },
    { kF12, kPause },
};

inline constexpr LayerEntry kExtendedFKeys[] = {
    { kF13, kPrintScreen },
    { kF14, kScrollLock },
    { kF15, kPause },
};

// `in` and `out` hold `count` flags (one per SDL scan code, count <= kStateCount; codes
// beyond `count` are untouched). `out` may not alias `in`.
inline void ApplyMacKeyLayer(const bool* in, int count, bool* out) {
    if (count > kStateCount) count = kStateCount;
    for (int i = 0; i < count; ++i)
        out[i] = in[i];
    auto has = [&](int sc) { return sc < count; };

    if (has(kRGui)) {
        if (in[kLGui] || in[kRGui]) {
            out[kLCtrl] = true;
            out[kLGui] = false;
            out[kRGui] = false;
        }
    }

    if (has(kRAlt) && in[kRAlt]) {
        bool fired = false;
        for (const LayerEntry& e : kOptionLayer) {
            if (has(e.from) && has(e.to) && in[e.from]) {
                out[e.from] = false;
                out[e.to] = true;
                fired = true;
            }
        }
        if (fired) out[kRAlt] = false;
    }

    for (const LayerEntry& e : kExtendedFKeys) {
        if (has(e.from) && has(e.to) && in[e.from]) {
            out[e.from] = false;
            out[e.to] = true;
        }
    }
}

// SDL_MouseButtonFlags bits: button 1 (left) = bit 0, button 2 (middle) = bit 1.
inline constexpr uint32_t kButtonLeft = 0x1;
inline constexpr uint32_t kButtonMiddle = 0x2;
inline constexpr uint16_t kModOption = 0x0100 | 0x0200;   // SDL_KMOD_LALT | SDL_KMOD_RALT

// Rule 4 is decided once, by the event pump, never by the current modifier at poll
// time. Releasing Option during a drag must not turn that drag back into a left click.
class MouseButtonRemapper {
public:
    uint8_t ButtonEvent(uint8_t button, bool down, uint16_t modifiers) {
        if (button != 1) return button;
        if (down && !leftDown_) {
            middle_ = (modifiers & kModOption) != 0;
        }
        const uint8_t logical = middle_ ? 2 : 1;
        leftDown_ = down;
        if (!down) middle_ = false;
        return logical;
    }

    uint32_t PolledButtons(uint32_t physical) const {
        // A physical press can arrive before its queued event is dispatched (retail
        // polls before pumping). Hide it until the pump has chosen its logical button.
        const uint32_t other = physical & ~kButtonLeft;
        if (!leftDown_ || (physical & kButtonLeft) == 0) return other;
        return other | (middle_ ? kButtonMiddle : kButtonLeft);
    }

    void Reset() {
        leftDown_ = false;
        middle_ = false;
    }

private:
    bool leftDown_ = false;
    bool middle_ = false;
};

// One main-thread state shared by the SDL event pump and both polling APIs. The
// platform-specific callers use this only on Darwin; the rule itself remains pure.
inline MouseButtonRemapper& MouseButtons() {
    static MouseButtonRemapper state;
    return state;
}

// SDL_Keymod bits (SDL_keycode.h).
inline constexpr uint16_t kModCtrl = 0x0040 | 0x0080;
inline constexpr uint16_t kModGui = 0x0400 | 0x0800;

// Control+Command+F: macOS's full-screen toggle. `repeat` is a held key's repeats, which
// must not toggle again.
inline bool IsFullscreenChord(uint16_t sdlMod, int scancode, bool repeat) {
    const int kF = 9;   // SDL_SCANCODE_F
    return !repeat && scancode == kF && (sdlMod & kModCtrl) != 0 && (sdlMod & kModGui) != 0;
}

}  // namespace MacKeys
}  // namespace Input
}  // namespace Platform
