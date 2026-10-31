// ============================================================================
// src/platform/win/app_win.cpp -- Platform::App lifecycle hooks (Windows)
// ============================================================================
// The Windows half of src/platform/common/platform_app.h. WinMain's own SDL set-up
// (SDL_RegisterApp, the DPI-awareness and timer-resolution hints, SDL_UnregisterApp)
// stays inline in src/app/main.cpp, in its order; PreSdlInitHints sets the one hint
// only the Windows video backend reads. Shutdown stops the boot-state watchdog before
// SDL_Quit, so a clean exit leaves STATE=EXITED instead of the last RUNNING / QUITTING tick
// (as the Darwin and Android twins do).
// ============================================================================

#include "platform_app.h"

#include "boot_state_watchdog.h"

#include <SDL3/SDL_hints.h>

namespace Platform {
namespace App {

// SDL_BORDERLESS_WINDOWED_STYLE = 0: a borderless window is WS_POPUP (| WS_MINIMIZEBOX),
// as retail's AdjustDisplay makes it for fullscreen ((style & 0xFF30FFFF) | WS_POPUP,
// Platform::Window::SetFramed). SDL's default style for a borderless window keeps
// WS_CAPTION | WS_SYSMENU and hides the frame in WM_NCCALCSIZE, but DXGI does not see
// that: IDXGISwapChain::ResizeTarget sizes the window with AdjustWindowRect of the real
// style, so every fullscreen reset grew the window by a 6 x 29 caption frame, the client
// shrank by it on the next SetWindowPos, and the WM_SIZE re-armed the reset: while
// SetFullscreenState(TRUE) is refused (another window holds the foreground) the window
// shrank every frame (known_port_issues row 1334).
void EnterRunDirectory() {
}

void PreSdlInitHints() {
    SDL_SetHintWithPriority("SDL_BORDERLESS_WINDOWED_STYLE", "0", SDL_HINT_OVERRIDE);
}

void PostSdlInit() {
}

void Shutdown() {
    BootState::Stop();
}

const char* MissingGameDataHelp() {
    return "OpenBA needs your own copy of Banished (data0.pkg, data1.pkg, patch0.pkg, patch1.pkg). "
           "Put OpenBA.exe in your Banished folder, beside its WinData folder (for example "
           "C:\\GOG Games\\Banished or Steam\\steamapps\\common\\Banished), and start it from there. "
           "Nothing in that folder is changed.";
}

bool TakeSaveRequest() {
    return false;
}

}  // namespace App
}  // namespace Platform
