// ============================================================================
// src/platform/sdl/window_sdl.cpp -- Platform::Window: the game window over SDL3
// ============================================================================
// Phase D.4 SDL step W3 (plan §A.1, §C; D4_RULES C5).
//
// RETAIL (Application::PlatformStartup, 0x18009fc10):
//     RegisterClassW(&{0, WindowProc, 0, 0, hInstance, NULL, NULL, (HBRUSH)6, NULL,
//                      L"Banished"});
//     hwnd = CreateWindowExW(0, L"Banished", L"Banished", 0, CW_USEDEFAULT,
//                            CW_USEDEFAULT, 10, 10, NULL, NULL, hInstance, NULL);
//     UpdateWindow(hwnd); SetCursor(NULL);
//
// WHAT SDL DOES DIFFERENTLY, AND WHY IT IS ACCEPTED
//  * The class is SDL's: src/app/main.cpp registers it through SDL_RegisterApp with
//    retail's NAME ("Banished"), STYLE (0) and module handle, before SDL_Init(VIDEO);
//    its procedure is SDL's WIN_WindowProc, which queues SDL events that
//    src/platform/sdl/pump_sdl.cpp turns back into retail's message tuples. The class has
//    no background brush (retail: COLOR_WINDOW + 1, never painted because WindowProc
//    swallows WM_ERASEBKGND); SDL paints the client black on its first erase
//    (SDL_HINT_WINDOWS_ERASE_BACKGROUND_MODE's default), the colour the diag hook's
//    brush swap used to force.
//  * 10 x 10 is the CLIENT size (retail: the outer size of a WS_OVERLAPPED window) and the
//    position is SDL_WINDOWPOS_UNDEFINED (SDL centres it; retail CW_USEDEFAULT cascades).
//    Neither survives: Video::Device::PlatformStartup -> AdjustDisplay gives the window
//    its real style, size and place before it is shown.
//  * The window is created HIDDEN (retail: dwStyle 0, no WS_VISIBLE: also hidden) and
//    with the graphics-API flag Platform::Window::PrepareCreate returns (the render
//    backend's on Windows and Apple: none for DX11, SDL_WINDOW_VULKAN for Vulkan,
//    SDL_WINDOW_METAL for Metal; SDL_WINDOW_VULKAN on Linux, D4_RULES C5). The Vulkan
//    flag makes SDL load the Vulkan loader when the window is created, and creation
//    FAILS without one; the DX11 build asks for no flag (review F-208), so a machine with
//    D3D11 but no Vulkan runtime opens its window as retail's did.
//  * UpdateWindow (a WM_PAINT for an empty update region of a hidden window) has no SDL
//    spelling and is not made. SetCursor(NULL) is (SDL step W5): Platform::Cursor::Set
//    with a null handle selects SDL's "no cursor" (src/platform/sdl/cursor_sdl.cpp), so
//    the client area shows no OS arrow until slot 8 builds the game cursor, as retail.
// ============================================================================

#include "platform_window.h"
#include "platform_window_hooks.h"
#include "window_sdl_internal.h"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_messagebox.h>
#include <SDL3/SDL_properties.h>
#include <SDL3/SDL_video.h>

#include <cstdio>
#include <string>

namespace Platform {
namespace Window {

namespace {
SDL_Window* g_window = nullptr;
}  // namespace

namespace Sdl {
SDL_Window* GameWindow() { return g_window; }
}  // namespace Sdl

float GamePixelDensity() {
    return PixelDensity(g_window);
}

bool VideoAvailable() {
    return SDL_WasInit(SDL_INIT_VIDEO) != 0;
}

void* Create(const char* title, int32_t width, int32_t height) {
    const uint64_t platformFlags = PrepareCreate(&width, &height);
    SDL_Window* w = SDL_CreateWindow(title, width, height, SDL_WINDOW_HIDDEN | platformFlags);
    if (w == nullptr) {
        std::fprintf(stderr, "[SDL] SDL_CreateWindow failed: %s\n", SDL_GetError());
        return nullptr;
    }
    g_window = w;
    Sdl::InstallPumpHooks();
    Cursor::Set(nullptr);   // retail 0x18009fc10: SetCursor(NULL) after UpdateWindow
    return NativeHandle();
}

void Destroy(void* /*window*/) {
    // Retail's DestroyWindow on the handle it is given; there is one window, so the
    // handle and g_window are the same object.
    if (g_window == nullptr) return;
    Sdl::RemovePumpHooks();
    SDL_DestroyWindow(g_window);
    g_window = nullptr;
}

void* NativeHandle() {
    if (g_window == nullptr) return nullptr;
#if defined(SDL_PLATFORM_WINDOWS)
    // Main thread only (SDL's rule): Create and the renderer's startup run there.
    return SDL_GetPointerProperty(SDL_GetWindowProperties(g_window),
                                  SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
#else
    // Off Windows the handle is the SDL_Window* itself (plan §C, D4_RULES C5); the Vulkan
    // backend makes its surface on it through SDL.
    return g_window;
#endif
}

// ============================================================================
// IsVisible / ShowDefault (Phase D.4 SDL-5, Block 5 D1b): SDL3 on every host.
// ============================================================================
// Were IsWindowVisible / ShowWindow(SW_SHOWDEFAULT) on the SDL-created HWND
// (src/platform/win/window_win.cpp) and the SDL pair in src/platform/posix/window_posix.cpp;
// both files are gone. RENDER-3 routed the retail call sites here (VideoDX11-x64.dll, now
// src/render/dx11/devicedata.cpp; plus our BootWindow reveal):
//
//   0x180004340  the swap-chain resize helper (DeviceData_Resize):
//                    Device+0x9c = 1;
//                    if (!IsWindowVisible(Device+0x90)) return 0;   // the reset stays pending
//                and TraceGeom's diagnostic `vis=` column.
//   0x180004e10  slot 2, CreateDevice step 6:
//                    if (!Configuration+0x42) ShowWindow(Device+0x90, SW_SHOWDEFAULT /*10*/);
//                and BootWindow::ShowNow (src/app/boot_window.cpp), the deferred reveal of
//                the same window, same call.
//
// Both take the native handle the caller holds (the HWND on Windows, D4_RULES C5) and,
// like the rest of this backend (display_sdl.cpp), act on the one game window: there is
// one window, so the handle and GameWindow() are the same object.
//
// WHAT SDL DOES DIFFERENTLY
//  * IsVisible reads SDL's SDL_WINDOW_HIDDEN flag, not the window's WS_VISIBLE bit (the
//    ShowDefault point below says how SDL keeps it), and a minimised window stays "shown"
//    for SDL as it stays visible for IsWindowVisible (the resize helper's own IsIconic
//    test after this one covers the minimised case). With no window (before Create /
//    after Destroy) it answers false, as IsWindowVisible(NULL) does.
//  * ShowDefault is SDL_ShowWindow, whose Windows body is ShowWindow(hwnd, SW_SHOW)
//    (SDL_windowswindow.c WIN_ShowWindow, activating, SDL_HINT_WINDOW_ACTIVATE_WHEN_SHOWN
//    defaults to true). SW_SHOWDEFAULT took the show state from the STARTUPINFO the
//    process was started with (STARTF_USESHOWWINDOW: a shortcut set to "Minimized" or
//    "Maximized", `Start-Process -WindowStyle Minimized`), so retail's window opened
//    minimised or maximised that way (src/app/main.cpp's DumpStartupInfo logs the state;
//    a minimised start dead-ends in CreateColorBuffer). SW_SHOW does not ask for the
//    startup state. ShowWindow's documentation says nCmdShow is ignored on a process's
//    FIRST ShowWindow call when the launcher supplied a STARTUPINFO show state, which
//    would make the two spellings equal on this, the process's first show; that is NOT
//    verified by a run. A normal launch (a desktop double-click, Steam / GOG, the gate)
//    carries SW_SHOWNORMAL or no flag, for which SW_SHOW and SW_SHOWDEFAULT both show
//    the window normally and activated. known_port_issues row 1277.
//    SDL_ShowWindow on a window SDL already considers shown returns at once, where
//    ShowWindow re-applied the show command; both callers show a hidden window, once.
//    SDL's flag follows SDL_ShowWindow / SDL_HideWindow and WM_SHOWWINDOW
//    (SDL_windowsevents.c); nothing in the tree shows this window any other way.
//  * Off Windows there is no startup show state at all: SDL_ShowWindow is the whole of
//    it (the old window_posix.cpp body).
// ============================================================================
bool IsVisible(void* /*window*/) {
    SDL_Window* w = Sdl::GameWindow();
    return w != nullptr && (SDL_GetWindowFlags(w) & SDL_WINDOW_HIDDEN) == 0;
}

void ShowDefault(void* /*window*/) {
    // ShowWindow on a handle that is not a window fails and does nothing; so does this.
    if (SDL_Window* w = Sdl::GameWindow()) {
        SDL_ShowWindow(w);
        ConfigureBackgroundWindow(w);
    }
}

int64_t DefaultWindowProc(void* /*window*/, uint32_t /*message*/, uint64_t /*wParam*/,
                          int64_t /*lParam*/) {
    // SDL's WIN_WindowProc already ran DefWindowProcW (or its own handling) on the REAL
    // message before the event reached the pump; the synthesised tuple has nothing left
    // to default. WM_CLOSE's default (DestroyWindow -> WM_DESTROY) is reproduced by the
    // translator's second tuple (window_messages.h).
    return 0;
}

// ============================================================================
// SDL step W6: System::Error::Display's box.
// ============================================================================
// RETAIL (Error::Display, 0x1800c63c3): MessageBoxW(nullptr, wtext, wcap, 0x10) --
// MB_OK | MB_ICONERROR, no owner, modal on the calling thread.
//
// SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, caption, text, nullptr): one OK button,
// the error icon, no parent. It takes UTF-8, so the UTF-16 buffers retail built (with
// its clamp) are converted here, unpaired surrogates as U+FFFD (MessageBoxW would draw
// them as boxes). On Windows SDL tries TaskDialogIndirect first, which needs comctl32 v6;
// this exe declares no common-controls dependency in its manifest, so LoadLibrary finds
// comctl32 v5 without that export and SDL builds the box from a dialog template with no
// custom class: a "#32770" window titled with the caption, which is what
// src/diag/win/boot_state_watchdog.cpp looks for. SDL's box also centres on the primary
// display and shows the cursor; MessageBoxW did both too. Callable before SDL_Init.
// ============================================================================
namespace {

void AppendUtf8(std::string& out, uint32_t cp) {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

std::string Utf16ToUtf8(const uint16_t* s) {
    std::string out;
    for (size_t i = 0; s[i] != 0; ++i) {
        uint32_t cp = s[i];
        if (cp >= 0xD800 && cp <= 0xDBFF && s[i + 1] >= 0xDC00 && s[i + 1] <= 0xDFFF) {
            cp = 0x10000 + ((cp - 0xD800) << 10) + (s[i + 1] - 0xDC00);
            ++i;
        } else if (cp >= 0xD800 && cp <= 0xDFFF) {
            cp = 0xFFFD;
        }
        AppendUtf8(out, cp);
    }
    return out;
}

}  // namespace

void ShowErrorMessage(const uint16_t* textUtf16, const uint16_t* captionUtf16) {
    const std::string text    = Utf16ToUtf8(textUtf16);
    const std::string caption = Utf16ToUtf8(captionUtf16);
    if (!SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, caption.c_str(), text.c_str(),
                                  nullptr)) {
        std::fprintf(stderr, "[SDL] SDL_ShowSimpleMessageBox failed: %s\n", SDL_GetError());
    }
}

}  // namespace Window
}  // namespace Platform
