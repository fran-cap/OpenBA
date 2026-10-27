// ============================================================================
// src/platform/win/window_win.cpp -- Platform::Window hooks (Windows)
// ============================================================================
// The Windows half of src/platform/common/platform_window_hooks.h. Retail has no step of
// its own at these spots, so every body is the identity (or does nothing): the window is
// created with the Vulkan flag and placed exactly as retail's SetWindowPos placed it.
// ============================================================================

#include "platform_window_hooks.h"

#include <SDL3/SDL_video.h>

namespace Platform {
namespace Window {

uint64_t PrepareCreate(int32_t* width, int32_t* height) {
    (void)width;
    (void)height;
    return SDL_WINDOW_VULKAN;
}

void ConfigureBackgroundWindow(SDL_Window* window) {
    (void)window;
}

void FitToUsableScreen(SDL_Window* window, const FrameBorders& frame, int32_t* clientX,
                       int32_t* clientY, int32_t* clientW, int32_t* clientH) {
    (void)window;
    (void)frame;
    (void)clientX;
    (void)clientY;
    (void)clientW;
    (void)clientH;
}

void ApplyInitialWindowGeometry(SDL_Window* window, int savedX, int savedY, int savedPxW,
                                int savedPxH, int videoW, int videoH) {
    (void)window;
    (void)savedX;
    (void)savedY;
    (void)savedPxW;
    (void)savedPxH;
    (void)videoW;
    (void)videoH;
}

void QueryScreenForModes(int* maxW, int* maxH, int* nativeW, int* nativeH) {
    *maxW = 0;
    *maxH = 0;
    *nativeW = 0;
    *nativeH = 0;
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
