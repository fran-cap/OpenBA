// ============================================================================
// src/platform/posix/input_posix.cpp -- the POSIX twin of src/platform/win/input_win.cpp
// ============================================================================
// Phase D.4 SDL-5 (SDL Block 5 D1c). Platform::Input::ShiftHeld, the launcher's Shift test
// (Engine::PlatformConfigure 0x1800a1670: GetAsyncKeyState(VK_LSHIFT / VK_RSHIFT) & 0x8000
// on Windows).
//
// There is no portable "is this key physically down right now" query: Wayland has none at
// all, and SDL3 exposes only its own tracked state, which is filled from key events
// delivered to a focused SDL window (on X11 SDL reconciles it with XQueryKeymap when the
// window GAINS focus). PlatformConfigure runs inside Engine::Startup, after the window is
// created but before it is shown, so on a POSIX host this answers false at launch: holding
// Shift does not take Half B. The other ways in stay: `-launcher` on the command line
// forces Half B, and the settings.cfg entries LauncherDisableMods / LauncherResetSettings
// replace the dialog's two check boxes on every boot (SETTINGS E.5).
// known_port_issues row 1278.
//
// The pump first, as Platform::Input::Poll does (plan §E rule 1): SDL_GetModState is only
// as fresh as the last SDL_PumpEvents. The pump queues events; it dispatches nothing.
// ============================================================================

#include "platform_input.h"
#include "platform_input_hooks.h"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_keyboard.h>

namespace Platform {
namespace Input {

bool ShiftHeld() {
    SDL_PumpEvents();
    return (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;
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
