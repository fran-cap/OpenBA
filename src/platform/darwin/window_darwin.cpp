// ============================================================================
// src/platform/darwin/window_darwin.cpp -- Platform::Window hooks (macOS)
// ============================================================================
// The Darwin twin of src/platform/posix/window_posix.cpp (src/platform/darwin/README.md):
// it replaces that file whole on Apple and defines every hook of
// src/platform/common/platform_window_hooks.h. NOT RETAIL: macOS windows are Metal
// windows, always high-pixel-density (Retina), and need a usable-screen fit, an
// opening geometry and a harness "background" mode.
//
// Cocoa is reached through the Objective-C runtime (objc_msgSend) so this stays a .cpp
// like every other platform file; the only Cocoa calls are the background-mode ones.
// ============================================================================

#include "platform_window_hooks.h"

#include "app/options.h"
#include "render_backend.h"   // Render::GraphicsWindowFlags
#include "../sdl/window_sdl_internal.h"

#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_properties.h>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_video.h>

#include <objc/message.h>
#include <objc/runtime.h>

#include <cstdio>

namespace Platform {
namespace Window {

namespace {

constexpr int32_t kBackgroundWidth = 640;
constexpr int32_t kBackgroundHeight = 360;
constexpr int32_t kBackgroundX = 8;
constexpr int32_t kBackgroundY = 32;
constexpr long kActivationPolicyProhibited = 2;   // NSApplicationActivationPolicyProhibited
constexpr long kBelowNormalWindowLevel = -1;  // NSNormalWindowLevel (0) - 1

}  // namespace

uint64_t PrepareCreate(int32_t* width, int32_t* height) {
    if (Opt().background) {
        SDL_SetHint(SDL_HINT_FORCE_RAISEWINDOW, "0");
        SDL_SetHint(SDL_HINT_WINDOW_ACTIVATE_WHEN_SHOWN, "0");
        SDL_SetHint(SDL_HINT_WINDOW_ACTIVATE_WHEN_RAISED, "0");
        ConfigureBackgroundWindow(nullptr);
        *width = kBackgroundWidth;
        *height = kBackgroundHeight;
    }
    // Native Retina is unconditional on Apple: the window is high-pixel-density. The
    // graphics-API flag is the render backend's fact (SDL_WINDOW_METAL for Metal).
    return Render::GraphicsWindowFlags() | SDL_WINDOW_HIGH_PIXEL_DENSITY;
}

void ConfigureBackgroundWindow(SDL_Window* window) {
    if (!Opt().background) return;
    id app = reinterpret_cast<id (*)(Class, SEL)>(objc_msgSend)(
        objc_getClass("NSApplication"), sel_registerName("sharedApplication"));
    reinterpret_cast<BOOL (*)(id, SEL, long)>(objc_msgSend)(
        app, sel_registerName("setActivationPolicy:"), kActivationPolicyProhibited);
    if (window == nullptr) return;
    SDL_SetWindowSize(window, kBackgroundWidth, kBackgroundHeight);
    SDL_SetWindowPosition(window, kBackgroundX, kBackgroundY);
    id native = static_cast<id>(SDL_GetPointerProperty(
        SDL_GetWindowProperties(window), SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr));
    if (native == nullptr) return;
    reinterpret_cast<void (*)(id, SEL, BOOL)>(objc_msgSend)(
        native, sel_registerName("setHidesOnDeactivate:"), NO);
    reinterpret_cast<void (*)(id, SEL, long)>(objc_msgSend)(
        native, sel_registerName("setLevel:"), kBelowNormalWindowLevel);
    reinterpret_cast<void (*)(id, SEL, id)>(objc_msgSend)(
        native, sel_registerName("orderBack:"), nullptr);
}

// A framed window must fit the usable screen (menu bar, Dock): the Options resolution list
// is filtered to it, but a saved or hand-edited size may not be. Clamp the size, then slide
// the window so title bar and bottom edge are on screen (never stuck).
void FitToUsableScreen(SDL_Window* window, const FrameBorders& frame, int32_t* clientX,
                       int32_t* clientY, int32_t* clientW, int32_t* clientH) {
    if (SDL_GetWindowFlags(window) & SDL_WINDOW_BORDERLESS) return;
    SDL_Rect u{ 0, 0, 0, 0 };
    if (!SDL_GetDisplayUsableBounds(SDL_GetDisplayForWindow(window), &u)) return;
    if (u.w <= 0 || u.h <= 0) return;
    const int32_t maxW = u.w - frame.left - frame.right;
    const int32_t maxH = u.h - frame.top - frame.bottom;
    if (maxW > 0 && *clientW > maxW) *clientW = maxW;
    if (maxH > 0 && *clientH > maxH) *clientH = maxH;
    if (*clientY - frame.top < u.y) *clientY = u.y + frame.top;
    if (*clientY + *clientH + frame.bottom > u.y + u.h) {
        *clientY = u.y + u.h - *clientH - frame.bottom;
    }
    if (*clientX - frame.left < u.x) *clientX = u.x + frame.left;
    if (*clientX + *clientW + frame.right > u.x + u.w) {
        *clientX = u.x + u.w - *clientW - frame.right;
    }
}

// ============================================================================
// The windowed OPENING geometry (the Metal backend's CreateDevice; docs/reference/macos_input_window.md
// "Opening size"). The engine's windowed back buffer starts as Configuration::windowWidth /
// Height, which is -1 until a window geometry has been saved: used as a size that collapsed
// the 10 x 10 point creation window to 1 x 1 (pixel_size_changed 20x20, then resized 1x1).
// Here the size comes from display_geometry.h's InitialWindowPoints (saved PIXELS / density,
// else the configured video size as POINTS, never below 640 x 360 points, never above the
// usable screen) and the window is placed at the saved origin when that is still on a
// display, else centred on the primary display's usable area. Everything is in points.
// ============================================================================
void ApplyInitialWindowGeometry(SDL_Window* window, int savedX, int savedY, int savedPxW,
                                int savedPxH, int videoW, int videoH) {
    if (window == nullptr) return;
    const float d = PixelDensity(window);
    SDL_Rect usable{ 0, 0, 0, 0 };
    const bool haveUsable = SDL_GetDisplayUsableBounds(SDL_GetPrimaryDisplay(), &usable);
    int top = 0;
    int left = 0;
    int bottom = 0;
    int right = 0;
    SDL_GetWindowBordersSize(window, &top, &left, &bottom, &right);
    if (top <= 0) top = 28;   // a hidden window may not know its frame yet: the macOS title bar
    int32_t cw = 0;
    int32_t ch = 0;
    InitialWindowPoints(
        savedPxW, savedPxH, videoW, videoH, d,
        haveUsable ? usable.w - left - right : 0,
        haveUsable ? usable.h - top - bottom : 0, &cw, &ch);
    std::fprintf(stderr,
                 "[Window] initial geometry: saved %dx%d video %dx%d density %.1f usable %dx%d "
                 "-> %dx%d pt\n",
                 savedPxW, savedPxH, videoW, videoH, d, usable.w, usable.h, cw, ch);
    SDL_SetWindowSize(window, cw, ch);

    int px = 0;
    int py = 0;
    bool placed = false;
    if (savedPxW > 0 && savedPxH > 0 && !(savedX == -1 && savedY == -1)) {
        px = PixelsToPointsRound(savedX, d);
        py = PixelsToPointsRound(savedY, d);
        int count = 0;
        SDL_DisplayID* ids = SDL_GetDisplays(&count);
        if (ids != nullptr) {
            for (int i = 0; i < count && !placed; ++i) {
                SDL_Rect b;
                if (!SDL_GetDisplayUsableBounds(ids[i], &b)) continue;
                // The title bar (top points above the client origin) must also be on screen.
                const Platform::Rect win{ px, py - top, px + cw, py + ch };
                const Platform::Rect disp{ b.x, b.y, b.x + b.w, b.y + b.h };
                placed = RectTouchesAnyDisplay(win, &disp, 1) && py - top >= b.y;
            }
            SDL_free(ids);
        }
    }
    if (!placed) {
        if (haveUsable) {
            px = usable.x + (usable.w - cw) / 2;
            py = usable.y + top + (usable.h - top - ch) / 2;
        } else {
            px = 100;
            py = 100;
        }
    }
    SDL_SetWindowPosition(window, px, py);
    SDL_SyncWindow(window);
}

void QueryScreenForModes(int* maxW, int* maxH, int* nativeW, int* nativeH) {
    *maxW = 0;
    *maxH = 0;
    *nativeW = 0;
    *nativeH = 0;
    const SDL_DisplayID id = SDL_GetPrimaryDisplay();
    const SDL_DisplayMode* mode = SDL_GetDesktopDisplayMode(id);
    const float d = GamePixelDensity();   // the game window's density: the Retina density on Apple
    SDL_Rect u{ 0, 0, 0, 0 };
    if (SDL_GetDisplayUsableBounds(id, &u) && u.w > 0 && u.h > 0) {
        *maxW = PointsToPixels(static_cast<float>(u.w), d);
        *maxH = PointsToPixels(static_cast<float>(u.h - 32), d);
    }
    if (mode != nullptr) {
        *nativeW = PointsToPixels(static_cast<float>(mode->w), d);
        *nativeH = PointsToPixels(static_cast<float>(mode->h), d);
    }
}

float RenderScale() {
    return 1.0f;
}

bool RenderScaleAdjustable() {
    return false;
}

void SetRenderScale(int percent) {
    (void)percent;
}

bool FixedDisplaySurface() {
    return false;
}

void SetPreferredFrameRate(float fps) {
    (void)fps;
}

void ReapplyPreferredFrameRate() {
}

}  // namespace Window
}  // namespace Platform
