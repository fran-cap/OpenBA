// ============================================================================
// src/platform/sdl/display_sdl.cpp -- Platform::Window geometry and displays over SDL3
// ============================================================================
// Phase D.4 SDL step W4 (plan §A.3). The callers are retail transcriptions written
// against Win32 meanings (Video::Device::AdjustDisplay / SizeWindow / MoveWindow /
// GetDefaultResolution, Input::Device's clip rect, game_main's shutdown log), so each
// body here keeps the Win32 meaning its declaration in platform_window.h states. What SDL
// changes is said at each function.
//
// THE ONE ARITHMETIC THAT MATTERS: SDL positions and sizes the CLIENT area; Win32's
// SetWindowPos / GetWindowRect / AdjustWindowRect are OUTER rects. AdjustDisplay places
// the OUTER rect at the persisted Configuration::windowX / Y, which MoveWindow saved as a
// CLIENT origin (ClientToScreen of 0,0): after a user move retail's next run opens the
// window one frame-width down and right of where it was left. That drift is retail's and
// is kept by converting outer <-> client with the window's frame
// (src/platform/common/display_geometry.h), never by passing the outer values to SDL.
//
// The frame comes from SDL_GetWindowBordersSize, which on Windows measures the live
// window (GetWindowRect vs GetClientRect). It is meaningless while the window is
// minimised (an iconic window's rects) and may read 0 before the window system has
// applied a style, so the last good measurement per style (framed / not) is kept and used
// in those cases. The process is DPI-unaware (src/app/main.cpp), so a frame does not
// change size between displays.
// ============================================================================

#include "platform_window.h"
#include "display_geometry.h"
#include "platform_window_hooks.h"
#include "window_sdl_internal.h"

#include <SDL3/SDL_properties.h>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_video.h>

#include <cstring>

namespace Platform {
namespace Window {

namespace {

constexpr uint32_t kShowNormal    = 1;   // SW_SHOWNORMAL
constexpr uint32_t kShowMinimized = 2;   // SW_SHOWMINIMIZED
constexpr uint32_t kShowMaximized = 3;   // SW_SHOWMAXIMIZED

void Zero(Rect* r) { std::memset(r, 0, sizeof(*r)); }

// COORDINATE UNITS. SDL positions and sizes windows and displays in POINTS; the engine
// works in PIXELS (the back buffer is GetWindowSizeInPixels). The two differ by the
// window's pixel density (PixelDensity below), which is 1.0 for every window created
// without SDL_WINDOW_HIGH_PIXEL_DENSITY, so everything here is then the identity
// (display_geometry.h: PointsToPixels...). Every coordinate that crosses this seam is
// converted: SDL values * density going out to the engine, engine values / density going
// into SDL.

Rect FromSdl(const SDL_Rect& s, float density) {
    Rect r;
    r.left   = PointsToPixels(static_cast<float>(s.x), density);
    r.top    = PointsToPixels(static_cast<float>(s.y), density);
    r.right  = PointsToPixels(static_cast<float>(s.x + s.w), density);
    r.bottom = PointsToPixels(static_cast<float>(s.y + s.h), density);
    return r;
}

// The last good frame measurement, per style: [0] not framed, [1] framed.
FrameBorders g_frame[2]  = {};
bool         g_haveFrame[2] = {false, false};

FrameBorders CurrentFrame(SDL_Window* w) {
    const SDL_WindowFlags flags = SDL_GetWindowFlags(w);
    const int framed = (flags & SDL_WINDOW_BORDERLESS) ? 0 : 1;
    if ((flags & SDL_WINDOW_MINIMIZED) == 0) {
        int top = 0, left = 0, bottom = 0, right = 0;
        // A restore from a minimise can still be in flight here: SDL has cleared MINIMIZED
        // but Windows still reports the iconic window rect (160 x 28 at -32000), so the
        // frame comes out negative and OuterToClient nearly doubled the window (4960 x 2852
        // for a 2560 x 1440 fullscreen restore). Such a measurement keeps the last good one.
        if (SDL_GetWindowBordersSize(w, &top, &left, &bottom, &right) &&
            UsableFrameMeasurement(FrameBorders{top, left, bottom, right}, framed != 0)) {
            g_frame[framed]     = FrameBorders{top, left, bottom, right};
            g_haveFrame[framed] = true;
        }
    }
    if (g_haveFrame[framed]) return g_frame[framed];
    return FrameBorders{0, 0, 0, 0};
}

// The frame in engine pixels (SDL measures it in points).
FrameBorders CurrentFramePixels(SDL_Window* w) {
    const FrameBorders f = CurrentFrame(w);
    const float d = PixelDensity(w);
    return FrameBorders{PointsToPixels(static_cast<float>(f.top), d),
                        PointsToPixels(static_cast<float>(f.left), d),
                        PointsToPixels(static_cast<float>(f.bottom), d),
                        PointsToPixels(static_cast<float>(f.right), d)};
}

}  // namespace

// GetClientRect: {0, 0, w, h}. Win32 reports an iconic window's client area as empty, and
// SizeWindow / AdjustDisplay read it back after a minimise, so a minimised window answers
// zeros here too (SDL keeps reporting the restored size).
void GetClientRect(void* /*window*/, Rect* out) {
    Zero(out);
    SDL_Window* w = Sdl::GameWindow();
    if (w == nullptr) return;
    if (SDL_GetWindowFlags(w) & SDL_WINDOW_MINIMIZED) return;
    int cw = 0, ch = 0;
    Sdl::WindowSizeInEnginePixels(w, &cw, &ch);
    out->right  = cw;
    out->bottom = ch;
}

// GetWindowRect: the client rect in screen coordinates grown by the frame.
void GetWindowRect(void* /*window*/, Rect* out) {
    Zero(out);
    SDL_Window* w = Sdl::GameWindow();
    if (w == nullptr) return;
    int x = 0, y = 0, cw = 0, ch = 0;
    SDL_GetWindowPosition(w, &x, &y);
    Sdl::WindowSizeInEnginePixels(w, &cw, &ch);
    const float d = PixelDensity(w);
    out->left   = PointsToPixels(static_cast<float>(x), d);
    out->top    = PointsToPixels(static_cast<float>(y), d);
    out->right  = out->left + cw;
    out->bottom = out->top + ch;
    ClientRectToOuter(out, CurrentFramePixels(w));
}

// ClientToScreen: SDL's window position IS the client area's screen origin.
void ClientToScreen(void* /*window*/, Point* inOut) {
    SDL_Window* w = Sdl::GameWindow();
    if (w == nullptr) return;
    int x = 0, y = 0;
    SDL_GetWindowPosition(w, &x, &y);
    // The client point arrives in pixels, the window origin is in points: the result is in
    // pixels (the engine's screen unit; see the banner above).
    const float d = PixelDensity(w);
    inOut->x += PointsToPixels(static_cast<float>(x), d);
    inOut->y += PointsToPixels(static_cast<float>(y), d);
}

uint32_t GetShowCmd(void* /*window*/) {
    SDL_Window* w = Sdl::GameWindow();
    if (w == nullptr) return 0;
    const SDL_WindowFlags flags = SDL_GetWindowFlags(w);
    if (flags & SDL_WINDOW_MINIMIZED) return kShowMinimized;
    if (flags & SDL_WINDOW_MAXIMIZED) return kShowMaximized;
    return kShowNormal;
}

bool IsIconic(void* /*window*/) {
    SDL_Window* w = Sdl::GameWindow();
    return w != nullptr && (SDL_GetWindowFlags(w) & SDL_WINDOW_MINIMIZED) != 0;
}

// Retail: style = (style & 0xFF30FFFF) | WS_POPUP for fullscreen, style | 0x00CF0000
// (WS_OVERLAPPEDWINDOW) for windowed, through Get/SetWindowLongW(GWL_STYLE). SDL owns the
// style bits: bordered + resizable is SDL's WS_OVERLAPPEDWINDOW (caption, system menu,
// sizing frame, min / max boxes), borderless + not resizable its WS_POPUP. One retail
// quirk is not kept: retail ORs the windowed bits onto whatever was there, so a window
// that had been fullscreen keeps WS_POPUP beside WS_OVERLAPPEDWINDOW; SDL's framed style
// has no WS_POPUP. Neither the frame nor the client area differs.
void SetFramed(void* /*window*/, bool framed) {
    SDL_Window* w = Sdl::GameWindow();
    if (w == nullptr) return;
    SDL_SetWindowBordered(w, framed);
    SDL_SetWindowResizable(w, framed);
}

// Retail: SetWindowPos(hwnd, insertAfter, x, y, w, h, SWP_FRAMECHANGED | SWP_NOACTIVATE)
// of the OUTER rect. SDL places the client: the outer rect is converted with the frame.
// * insertAfter NoTopmost (HWND_NOTOPMOST, the windowed branch) clears always-on-top,
//   which nothing sets; Top (HWND_TOP, the fullscreen branch) raised the window in the
//   z-order WITHOUT activating it, which SDL cannot spell (SDL_RaiseWindow activates), so
//   no z-order change is made. The fullscreen branch runs while the game owns the
//   foreground in practice.
// * SDL ignores position and size requests for a MAXIMISED window (it applies them on
//   restore); retail's SetWindowPos moved the maximised window's outer origin to the
//   work-area origin and left it flagged maximised. The client size, which AdjustDisplay
//   reads back, is the same either way.
// * A size that leaves no client area (a minimised window's 0 x 0 back buffer) is refused
//   by SDL; Win32 accepted it.
void SetWindowPos(void* /*window*/, ZOrder insertAfter, int32_t x, int32_t y,
                  int32_t width, int32_t height, uint32_t /*flags*/) {
    SDL_Window* w = Sdl::GameWindow();
    if (w == nullptr) return;
    if (insertAfter == ZOrder::NoTopmost) SDL_SetWindowAlwaysOnTop(w, false);
    // The engine's outer rect is in pixels; SDL takes points, and the frame it reports is
    // already in points.
    const float d = PixelDensity(w);
    int32_t cx = 0, cy = 0, cw = 0, ch = 0;
    OuterToClient(PixelsToPointsRound(x, d), PixelsToPointsRound(y, d),
                  PixelsToPointsRound(width, d), PixelsToPointsRound(height, d),
                  CurrentFrame(w), &cx, &cy, &cw, &ch);
    // Darwin only (window_darwin.cpp): fit a framed window to the usable screen. Identity
    // elsewhere, so Windows places the window exactly as retail's SetWindowPos does.
    FitToUsableScreen(w, CurrentFrame(w), &cx, &cy, &cw, &ch);
    SDL_SetWindowPosition(w, cx, cy);
    SDL_SetWindowSize(w, cw, ch);
    SDL_SyncWindow(w);
}

// Retail: AdjustWindowRect(&rect, style, FALSE) with the style it had just set. Here the
// window's own current frame, which SetFramed has just set.
void AdjustWindowRect(void* /*window*/, Rect* inOut) {
    SDL_Window* w = Sdl::GameWindow();
    if (w == nullptr) return;
    SDL_SyncWindow(w);
    ClientRectToOuter(inOut, CurrentFramePixels(w));
}

// Retail: GetMonitorInfoW(Device::monitor).rcMonitor, the HMONITOR the renderer took from
// the DXGI output (devicedata.cpp). The monitor stays the DXGI output's, not the window's
// display: on Windows the SDL display carrying that HMONITOR is found by its property.
void GetMonitorRect(void* monitor, Rect* out) {
    Zero(out);
    int count = 0;
    SDL_DisplayID* ids = SDL_GetDisplays(&count);
    if (ids == nullptr) return;
    const float density = PixelDensity(Sdl::GameWindow());
    for (int i = 0; i < count; ++i) {
#if defined(SDL_PLATFORM_WINDOWS)
        void* hmon = SDL_GetPointerProperty(SDL_GetDisplayProperties(ids[i]),
                                            SDL_PROP_DISPLAY_WINDOWS_HMONITOR_POINTER,
                                            nullptr);
        if (hmon == nullptr || hmon != monitor) continue;
#else
        // No HMONITOR off Windows: the output is the window's display.
        (void)monitor;
        SDL_Window* w = Sdl::GameWindow();
        if (w == nullptr || SDL_GetDisplayForWindow(w) != ids[i]) continue;
#endif
        SDL_Rect b;
        if (SDL_GetDisplayBounds(ids[i], &b)) *out = FromSdl(b, density);
        break;
    }
    SDL_free(ids);
}

// Retail: MonitorFromRect(&probe, MONITOR_DEFAULTTONULL) == NULL means "off every
// monitor". SDL_GetDisplayForRect never says that (it returns the closest display), so
// every display's bounds are tested for an intersection.
bool RectOnAnyDisplay(const Rect* rect) {
    int count = 0;
    SDL_DisplayID* ids = SDL_GetDisplays(&count);
    if (ids == nullptr) return false;
    bool on = false;
    const float density = PixelDensity(Sdl::GameWindow());
    for (int i = 0; i < count && !on; ++i) {
        SDL_Rect b;
        if (!SDL_GetDisplayBounds(ids[i], &b)) continue;
        const Rect d = FromSdl(b, density);
        on = RectTouchesAnyDisplay(*rect, &d, 1);
    }
    SDL_free(ids);
    return on;
}

// Retail: GetWindowRect(GetDesktopWindow()), the primary monitor's rect.
void GetPrimaryDisplayRect(Rect* out) {
    Zero(out);
    SDL_Rect b;
    if (SDL_GetDisplayBounds(SDL_GetPrimaryDisplay(), &b))
        *out = FromSdl(b, PixelDensity(Sdl::GameWindow()));
}

// Retail: EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS) unchecked, so a failure
// left the zeroed DEVMODEW's 0 x 0. SDL reports the mode in physical pixels from the same
// DEVMODE (WIN_GetDisplayMode).
void GetCurrentDisplaySize(int32_t* width, int32_t* height) {
    *width  = 0;
    *height = 0;
    const SDL_DisplayMode* mode = SDL_GetCurrentDisplayMode(SDL_GetPrimaryDisplay());
    if (mode == nullptr) return;
    // SDL_DisplayMode w / h are points; the engine's back buffer is pixels.
    const float d = PixelDensity(Sdl::GameWindow());
    *width  = PointsToPixels(static_cast<float>(mode->w), d);
    *height = PointsToPixels(static_cast<float>(mode->h), d);
}

// Was EnumDisplaySettingsW's dmDisplayFrequency (> 1, else unknown). SDL reports the rate
// as a float from the display configuration (59.94, 143.98); it is rounded to whole Hz,
// the unit DEVMODE carried.
int32_t GetCurrentDisplayRefreshHz() {
    const SDL_DisplayMode* mode = SDL_GetCurrentDisplayMode(SDL_GetPrimaryDisplay());
    if (mode == nullptr || mode->refresh_rate <= 1.0f) return 0;
    return static_cast<int32_t>(mode->refresh_rate + 0.5f);
}

int32_t GetDisplayRefreshRates(int32_t (*rates)[2], int32_t cap) {
    int modeCount = 0;
    SDL_DisplayMode** modes = SDL_GetFullscreenDisplayModes(SDL_GetPrimaryDisplay(), &modeCount);
    if (modes == nullptr) return 0;
    int32_t count = 0;
    for (int i = 0; i < modeCount; ++i) {
        const SDL_DisplayMode* m = modes[i];
        int32_t num = m->refresh_rate_numerator;
        int32_t den = m->refresh_rate_denominator;
        if (num <= 0 || den <= 0) {
            // No exact rate: the float, in whole Hz.
            if (m->refresh_rate <= 1.0f) continue;
            num = static_cast<int32_t>(m->refresh_rate + 0.5f);
            den = 1;
        }
        // One entry per whole Hz the Options combo shows (num / den): a display lists 59.94 and 60
        // under several sizes; the first exact pair seen for that Hz is kept.
        const int32_t hz = num / den;
        int32_t at = 0;
        while (at < count && rates[at][0] / rates[at][1] < hz) ++at;
        if (at < count && rates[at][0] / rates[at][1] == hz) continue;
        if (count == cap) continue;
        for (int32_t k = count; k > at; --k) {
            rates[k][0] = rates[k - 1][0];
            rates[k][1] = rates[k - 1][1];
        }
        rates[at][0] = num;
        rates[at][1] = den;
        ++count;
    }
    SDL_free(modes);
    return count;
}


// window_sdl_internal.h. The one body of the window's pixel density: back-buffer pixels per
// SDL point. SDL answers 1.0 for a window created without SDL_WINDOW_HIGH_PIXEL_DENSITY
// (every Windows and Linux window), so this is the identity there. P7: times the render scale
// (platform_window_hooks.h RenderScale, 1.0 but on Android), so a smaller back buffer and every
// coordinate the engine sees agree.
float PixelDensity(SDL_Window* window) {
    float d = 1.0f;
    if (window != nullptr) {
        const float sdl = SDL_GetWindowPixelDensity(window);
        if (sdl > 0.0f) d = sdl;
    }
    return d * RenderScale();
}

namespace Sdl {

// window_sdl_internal.h (P7): SDL's pixel size of the window, times the render scale.
void WindowSizeInEnginePixels(SDL_Window* window, int* w, int* h) {
    int pw = 0;
    int ph = 0;
    if (window != nullptr) SDL_GetWindowSizeInPixels(window, &pw, &ph);
    const float scale = RenderScale();
    *w = scale == 1.0f ? pw : static_cast<int>(static_cast<float>(pw) * scale + 0.5f);
    *h = scale == 1.0f ? ph : static_cast<int>(static_cast<float>(ph) * scale + 0.5f);
}

}  // namespace Sdl

}  // namespace Window
}  // namespace Platform
