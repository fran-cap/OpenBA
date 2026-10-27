#pragma once
// ============================================================================
// src/platform/common/platform_window_hooks.h -- per-platform window hooks
// ============================================================================
// NOT RETAIL. The steps of the SDL window backend (src/platform/sdl/window_sdl.cpp,
// display_sdl.cpp) that a platform may need to do its own way. Every hook has an identity
// (or "nothing to do") body on Windows and Linux, so those hosts keep retail's behaviour:
//   win/window_win.cpp, posix/window_posix.cpp   the identity bodies;
//   darwin/window_darwin.cpp                      replaces window_posix.cpp on Apple
//                                                 (src/platform/darwin/README.md).
// D4_RULES C1: namespace `Platform`, free functions, NO OS / SDL header here; an
// SDL_Window is only forward-declared. Display_geometry.h supplies FrameBorders.
// ============================================================================

#include <cstdint>

#include "display_geometry.h"

struct SDL_Window;

namespace Platform {
namespace Window {

// Called by Create before SDL_CreateWindow. May change the client size it will ask for
// (width / height are in/out, SDL points) and sets the application-specific SDL window
// flags OR-ed onto SDL_WINDOW_HIDDEN: the graphics-API flag (SDL_WINDOW_VULKAN on Windows and
// Linux, SDL_WINDOW_METAL on Apple) and, on Apple, the high-pixel-density
// request. Also the place for hints that must be set before the window exists.
uint64_t PrepareCreate(int32_t* width, int32_t* height);

// Harness mode on Apple (Opt().background): keep the window out of the way (no activation,
// no Dock tile, behind other windows). `window` may be nullptr, which only configures the
// application. Nothing to do elsewhere.
void ConfigureBackgroundWindow(SDL_Window* window);

// SetWindowPos hook: fit a framed window's client rect to the usable screen (menu bar,
// Dock): clamp the size, then slide it so the title bar and the bottom edge are on screen.
// All values are SDL points. Identity elsewhere (retail never clamps).
void FitToUsableScreen(SDL_Window* window, const FrameBorders& frame, int32_t* clientX,
                       int32_t* clientY, int32_t* clientW, int32_t* clientH);

// The windowed OPENING geometry, called by the renderer's CreateDevice (saved values are
// Configuration::windowX/Y/Width/Height in engine PIXELS, -1 when none was saved; videoW/H
// the configured video size). Sizes and places the (still hidden) window in points.
// The only caller is the Metal backend (src/render/metal, the renderer PR).
// Nothing to do on a host whose windows open at the size they were created with.
void ApplyInitialWindowGeometry(SDL_Window* window, int savedX, int savedY, int savedPxW,
                                int savedPxH, int videoW, int videoH);

// The primary display as the Options resolution list needs it, in engine PIXELS: the usable
// area less a title bar (maxW / maxH) and the display's own size (nativeW / nativeH).
// The only caller is the Metal backend (src/render/metal, the renderer PR).
// All zero when unknown (BuildResolutionList then does not filter).
void QueryScreenForModes(int* maxW, int* maxH, int* nativeW, int* nativeH);

// The pixel density of the game window (display_sdl.cpp's PixelDensity of the one window);
// 1.0 before it exists. For code that has no SDL_Window (display_density.cpp).
float GamePixelDensity();

// P7, the render scale: engine pixels per window pixel, in (0, 1]. The SDL backend multiplies it into
// PixelDensity and into every window pixel size it reports (Sdl::WindowSizeInEnginePixels), so the
// engine's back buffer, the client rect, the cursor and the size events all agree on the smaller
// size, and the renderer scales the frame up to the window when it presents. 1.0 on Windows, Linux
// and macOS; Android reads `VideoRenderScale` (a percent, 25..100) from the user's settings.cfg once
// (the phone's first-run file seeds 50: half resolution, docs/vulkan/PLAN.md §1). P8: any thread
// (the Vulkan backend reads it where it rebuilds the swap chain); SetRenderScale changes it.
float RenderScale();

// P8: whether the render scale can be changed in a run (the Options dialog's Render scale row,
// src/ext/enhanced_tab/render_scale_entry.cpp): true on Android, false on Windows, Linux and macOS,
// whose RenderScale stays 1.0 (a desktop's present path does not scale the frame; the row is shown
// disabled there).
bool RenderScaleAdjustable();

// P8: the render scale from now on, a percent (25..100; out of range is ignored). Main thread. The
// caller raises Video::Device::settingsChanged, so the next frame's settings change rebuilds the back
// buffer, the targets and the swap chain at the new size (the Vulkan backend's
// ApplyEngineSettingsChange reads the window's size through RenderScale), and records the key
// (`VideoRenderScale`) so the next start opens at it. A no-op where not RenderScaleAdjustable.
void SetRenderScale(int percent);

// P8: true where the game's surface is the whole screen at its own size and refresh, whatever the
// engine's Video configuration says: Android (an activity's window; the Vulkan backend republishes the
// window's size as the back buffer and never sets a display mode). There retail's Options rows
// Resolution, Refresh rate and Fullscreen change nothing; ext.phone_menu disables them and keeps VSync
// on (src/ext/phone_menu/video_rows.cpp). False on Windows, Linux and macOS.
bool FixedDisplaySurface();

// P12: the frame rate the game is paced to, told to the display. The frame limiter calls it with
// each ceiling it paces to (src/app/frame_limiter.cpp SetCeiling; 0 = not paced). Android passes it
// to ANativeWindow_setFrameRate on the window's current native window (API 30, resolved at run
// time, so a minSdk 29 device ignores it): SurfaceFlinger then switches a variable-rate panel to
// that rate (a 120 Hz panel runs at 60 under the phone's default cap), which halves the display's
// power and makes FIFO itself pace the game, instead of the limiter's sleep landing on every other
// vsync. Nothing to do on Windows, Linux and macOS (the display keeps its mode). Any thread.
void SetPreferredFrameRate(float fps);

// P12: the last SetPreferredFrameRate again, on the window's current native window: a trip to the
// background and back gives the window a new native window that knows nothing of the rate. The
// Vulkan backend calls it where it makes a swap chain. Nothing to do off Android.
void ReapplyPreferredFrameRate();

}  // namespace Window
}  // namespace Platform
