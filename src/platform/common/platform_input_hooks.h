#pragma once
// ============================================================================
// src/platform/common/platform_input_hooks.h -- per-platform input hooks
// ============================================================================
// NOT RETAIL. The steps of the SDL input backend (src/platform/sdl/input_sdl.cpp,
// pump_sdl.cpp) that a platform may need to do its own way. Every hook has an identity
// (or "nothing to do") body on Windows and Linux, so those hosts keep retail's behaviour:
//   win/input_win.cpp, posix/input_posix.cpp   the identity bodies;
//   darwin/input_darwin.cpp                    replaces input_posix.cpp on Apple
//                                              (src/platform/darwin/README.md): the Mac key
//                                              layer, the Option-click remap and the window
//                                              chord of src/platform/common/mac_keymap.h.
//   android/input_android.cpp                  replaces input_posix.cpp on Android: the
//                                              rotate drag's cursor return (P8) and the
//                                              screen keyboard (issue #100).
// D4_RULES C1: namespace `Platform`, free functions, NO OS / SDL header here; the SDL types
// are only forward-declared.
// ============================================================================

#include <cstdint>

union SDL_Event;

namespace Platform {
namespace Input {

// The capacity of the scratch array ApplyKeyLayer fills: one flag per SDL scan code.
inline constexpr int kKeyLayerCapacity = 512;

// SDL's per-scan-code key state, as the DIK table is to be read from it. `raw` and `*count`
// are SDL_GetKeyboardState's result. `scratch` holds kKeyLayerCapacity flags and receives the
// result when the platform changes the state; `*count` is lowered to what the result holds.
// Identity elsewhere: returns `raw` untouched.
const bool* ApplyKeyLayer(const bool* raw, int* count, bool* scratch);

// The LOGICAL mouse buttons (SDL_MouseButtonFlags) the pump's decisions leave down, given
// the physical ones SDL_GetGlobalMouseState reports. Identity elsewhere.
uint32_t PolledMouseButtons(uint32_t physical);

// The pump calls this for every event before anything else sees it. The platform may
// observe it and rewrite it in place (the Option-click button remap). Nothing elsewhere.
void PreprocessPumpEvent(SDL_Event* event);

// The pump calls this after the debug overlay and before the event is translated; true
// when the platform consumed the event (the game sees nothing). Always false elsewhere.
bool ConsumePumpEvent(const SDL_Event& event);

// P8: true when the system puts the pointer back where the camera-rotate drag began once the
// drag's relative mode ends, so SDL's position must be put there too (input_sdl.cpp
// SetRotateDrag). Android: relative mode is pointer capture, and ending a capture shows the
// pointer where the capture started, while SDL's own position has moved with the drag; the first
// motion after the release then snapped the cursor across the screen. False elsewhere: SDL puts
// the system cursor where its own position moved to, as before.
bool RotateDragReturnsCursor();

// Issue #100: a text box gained (true) or lost (false) the keyboard (SetTextEntryActive,
// platform_input.h). Android shows the screen keyboard while it is true and hides it after.
// Nothing elsewhere: a desktop types on its own keyboard, and SDL's text input stays off there.
void TextEntryChanged(bool active);

// Issue #100: the last step of ReadKeyboard, on its success path. `dik` holds the 256 DIK bytes
// read from SDL's key state; the platform may add keys SDL's state never showed. Android: the
// screen keyboard's keys, one per poll with a released poll between two (SDL sends each one's
// press and release together, so the state never holds it at a poll). Nothing elsewhere.
void AddScreenKeys(uint8_t dik[256]);

}  // namespace Input
}  // namespace Platform
