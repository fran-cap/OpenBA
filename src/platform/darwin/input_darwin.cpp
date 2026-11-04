// ============================================================================
// src/platform/darwin/input_darwin.cpp -- the Darwin twin of src/platform/posix/input_posix.cpp
// ============================================================================
// Platform::Input::ShiftHeld exactly as the POSIX twin (SDL's tracked modifier state, after
// a pump), plus the Mac input layer behind the hooks of platform_input_hooks.h. The rules
// themselves are the pure ones of src/platform/common/mac_keymap.h (unit-tested in
// tests/unit/mac_keymap_test.cpp):
//   * ApplyKeyLayer: Command acts as Control, Right Option is a Windows-keyboard layer,
//     F13-F15 are Print Screen / Scroll Lock / Pause;
//   * PreprocessPumpEvent / PolledMouseButtons: Option+left click is the middle button,
//     decided once per press by the event pump, never by the modifier at poll time;
//   * ConsumePumpEvent: Control+Command+F toggles native full screen.
// ============================================================================

#include "platform_input.h"
#include "platform_input_hooks.h"
#include "mac_keymap.h"
#include "../sdl/window_sdl_internal.h"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_video.h>

namespace Platform {
namespace Input {

namespace {

// The modifiers the Option-click decision reads: taken from the ordered key events, because
// SDL_GetModState may already include an Option-up queued after the click.
uint16_t g_mouseModifiers = 0;

// macOS window chord: Control+Command+F toggles native full screen (a Space), the system's
// own shortcut. Session only: the persistent setting is Options > fullscreen, which
// Video::Device::AdjustDisplay turns into a borderless window pinned to the monitor. The
// resize the toggle causes reaches the engine as the usual SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED
// -> WM_SIZE -> Video::Device::SizeWindow. Returns true when the event was the chord (the
// game sees no key for it).
bool HandleWindowChord(const SDL_KeyboardEvent& k) {
    if (!MacKeys::IsFullscreenChord(k.mod, static_cast<int>(k.scancode), k.repeat)) {
        return false;
    }
    SDL_Window* w = Window::Sdl::GameWindow();
    if (w != nullptr) {
        const bool on = (SDL_GetWindowFlags(w) & SDL_WINDOW_FULLSCREEN) != 0;
        SDL_SetWindowFullscreen(w, !on);
    }
    return true;
}

}  // namespace

bool ShiftHeld() {
    SDL_PumpEvents();
    return (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;
}

const bool* ApplyKeyLayer(const bool* raw, int* count, bool* scratch) {
    if (*count > MacKeys::kStateCount) {
        *count = MacKeys::kStateCount;
    }
    MacKeys::ApplyMacKeyLayer(raw, *count, scratch);
    return scratch;
}

uint32_t PolledMouseButtons(uint32_t physical) {
    // The event pump owns the Option-click decision, including its release pairing.
    return MacKeys::MouseButtons().PolledButtons(physical);
}

void PreprocessPumpEvent(SDL_Event* ev) {
    // Use ordered key events: SDL_GetModState may already include an Option-up
    // queued after this click. The button identity stays latched through release.
    if (ev->type == SDL_EVENT_KEY_DOWN || ev->type == SDL_EVENT_KEY_UP) {
        g_mouseModifiers = static_cast<uint16_t>(ev->key.mod);
    }
    if (ev->type == SDL_EVENT_WINDOW_FOCUS_GAINED) {
        g_mouseModifiers = static_cast<uint16_t>(SDL_GetModState());
    }
    if (ev->type == SDL_EVENT_WINDOW_FOCUS_LOST) {
        MacKeys::MouseButtons().Reset();
        g_mouseModifiers = 0;
    }
    if (ev->type == SDL_EVENT_MOUSE_BUTTON_DOWN || ev->type == SDL_EVENT_MOUSE_BUTTON_UP) {
        const bool down = (ev->type == SDL_EVENT_MOUSE_BUTTON_DOWN);
        ev->button.button =
            MacKeys::MouseButtons().ButtonEvent(ev->button.button, down, g_mouseModifiers);
    }
}

bool ConsumePumpEvent(const SDL_Event& ev) {
    return ev.type == SDL_EVENT_KEY_DOWN && HandleWindowChord(ev.key);
}

bool RotateDragReturnsCursor() {
    return false;
}

void TextEntryChanged(bool /*active*/) {
}

void AddScreenKeys(uint8_t* /*dik*/) {
}

}  // namespace Input
}  // namespace Platform
