// ============================================================================
// src/platform/win/input_win.cpp -- the Platform::Input primitive SDL cannot answer
// ============================================================================
// Phase D.4 SDL-5 (SDL Block 5 D1c). Every other Platform::Input primitive is SDL's on
// every host (src/platform/sdl/input_sdl.cpp, SDL-C). This one is not: platform_input.h
// says why (the Shift test runs before the game window has ever had the keyboard focus,
// and SDL's keyboard state only comes from key events to a focused window). It is the
// launch-time query of the PHYSICAL keys, which Win32 answers and SDL does not.
//
// Moved from src/platform/win/engine_platform_win.cpp (RtEnginePlatformShiftHeld, Wave 23
// lane ENGINE), the call and its mask unchanged:
//
//   ?PlatformConfigure@Engine@1@QEAA_N_NPEAX@Z   retail 0x1800a1670
//   0x1800a16f7  CALL qword ptr [0x18014b548]   ; user32!GetAsyncKeyState(VK_LSHIFT)
//   0x1800a170e  CALL qword ptr [0x18014b548]   ; user32!GetAsyncKeyState(VK_RSHIFT)
//
// The IAT slot was read out of Runtime-x64.dll's import directory, not guessed from a
// Ghidra label. 0xA2 / 0xA3 are VK_LSHIFT / VK_RSHIFT and the mask is 0x8000, the "down
// right now" bit -- NOT 0x0001, the "pressed since last call" bit. That distinction
// decides whether holding Shift at launch takes Half B of PlatformConfigure
// (src/engine/engine/engine_platform.cpp), which is the documented retail behaviour.
// ============================================================================

#include <windows.h>

#include "platform_input.h"
#include "platform_input_hooks.h"

namespace Platform {
namespace Input {

bool ShiftHeld() {
    return (GetAsyncKeyState(VK_LSHIFT) & 0x8000) != 0
        || (GetAsyncKeyState(VK_RSHIFT) & 0x8000) != 0;
}

// The hooks of platform_input_hooks.h: nothing to change on this host.
const bool* ApplyKeyLayer(const bool* raw, int* /*count*/, bool* /*scratch*/) {
    return raw;
}

uint32_t PolledMouseButtons(uint32_t physical) {
    return physical;
}

void PreprocessPumpEvent(SDL_Event* /*event*/) {
}

bool ConsumePumpEvent(const SDL_Event& /*event*/) {
    return false;
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
