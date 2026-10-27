// ============================================================================
// src/platform/android/window_android.cpp -- Platform::Window hooks (Android)
// ============================================================================
// The Android twin of src/platform/posix/window_posix.cpp (src/platform/android/README.md):
// it replaces that file whole on Android. One body differs: PrepareCreate adds
// SDL_WINDOW_FULLSCREEN to the Vulkan flag. An Android window always covers the activity, but
// only a fullscreen SDL window puts the activity in immersive mode (status and navigation bars
// hidden; a swipe brings them back for a moment), which is how a game owns the screen there. The
// engine's own "fullscreen" (a borderless window placed over the display, SetFramed) has no
// meaning on Android and stays as the shared SDL code does it. P7: RenderScale reads the phone's
// render scale (below); P8: SetRenderScale changes it in the run. P12: SetPreferredFrameRate tells
// the display the rate the game is paced to (below). Every other hook is the identity.
// ============================================================================

#include "platform_window_hooks.h"
#include "android_paths.h"
#include "platform/sdl/window_sdl_internal.h"   // Sdl::GameWindow

#include <SDL3/SDL_video.h>
#include <SDL3/SDL_properties.h>
#include <dlfcn.h>
#include <mutex>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace Platform {
namespace Window {

// P7: `VideoRenderScale=<percent>` in the user's settings.cfg (Documents/Banished under the data
// folder), read once at the first call: the window's creation, after EnterRunDirectory has seeded a
// first run's file (app_android.cpp: 50). Absent or out of 25..100: 100, the window's own size. The
// engine keeps the key with the rest (IO::Persistence writes back every key it read). P8: the
// Options dialog's Render scale row changes it in the run (SetRenderScale) and records the key. The
// percent is an atomic: the Vulkan backend reads the scale where it rebuilds the swap chain, which is
// the render thread under ext.render_pipe.
namespace {

std::atomic<int> g_percent{0};   // 0 = not read yet

int ReadPercentFromSettings() {
    int result = 100;
    const std::string path = Android::DataDir() + "/Documents/Banished/settings.cfg";
    FILE* f = std::fopen(path.c_str(), "r");
    if (f == nullptr) return result;
    char line[256];
    const char key[] = "VideoRenderScale=";
    while (std::fgets(line, sizeof(line), f) != nullptr) {
        if (std::strncmp(line, key, sizeof(key) - 1) != 0) continue;
        const int percent = std::atoi(line + sizeof(key) - 1);
        if (percent >= 25 && percent <= 100) result = percent;
    }
    std::fclose(f);
    std::fprintf(stderr, "[Window] render scale %.2f (VideoRenderScale in %s)\n",
                 static_cast<float>(result) / 100.0f, path.c_str());
    return result;
}

}  // namespace

float RenderScale() {
    int percent = g_percent.load(std::memory_order_acquire);
    if (percent == 0) {
        percent = ReadPercentFromSettings();
        int expected = 0;
        if (!g_percent.compare_exchange_strong(expected, percent, std::memory_order_acq_rel))
            percent = expected;
    }
    return static_cast<float>(percent) / 100.0f;
}

bool RenderScaleAdjustable() {
    return true;
}

bool FixedDisplaySurface() {
    return true;
}

void SetRenderScale(int percent) {
    if (percent < 25 || percent > 100) return;
    RenderScale();   // the boot value is read first, so a late first read cannot overwrite this one
    const int before = g_percent.exchange(percent, std::memory_order_acq_rel);
    if (before != percent) {
        std::fprintf(stderr, "[Window] render scale %.2f -> %.2f (Options)\n",
                     static_cast<float>(before) / 100.0f, static_cast<float>(percent) / 100.0f);
    }
}

// P12, THE PREFERRED FRAME RATE (platform_window_hooks.h). ANativeWindow_setFrameRate is API 30
// and minSdk is 29, so libandroid.so is asked for it once at the first call (dlsym; absent = the
// hooks do nothing). The native window is SDL's SDL_PROP_WINDOW_ANDROID_WINDOW_POINTER, the
// window's CURRENT ANativeWindow (a new one after each trip to the background); a rate is set on
// it with FIXED_SOURCE compatibility (the game's content is produced at that rate), 0 for no
// preference. The last rate is kept so the new native window can be told it again.
namespace {

using SetFrameRateFn = int32_t (*)(void* window, float frameRate, int8_t compatibility);
constexpr int8_t kFrameRateCompatibilityFixedSource = 1;   // ANATIVEWINDOW_FRAME_RATE_COMPATIBILITY_FIXED_SOURCE

std::mutex g_frameRateLock;
SetFrameRateFn g_setFrameRate = nullptr;
bool g_setFrameRateLooked = false;
float g_preferredFps = 0.0f;
void* g_frameRateWindow = nullptr;   // the native window the rate was last applied to
float g_frameRateApplied = -1.0f;    // the rate applied to it (-1: none yet)

// Under g_frameRateLock.
void ApplyFrameRateLocked() {
    if (!g_setFrameRateLooked) {
        g_setFrameRateLooked = true;
        void* lib = dlopen("libandroid.so", RTLD_NOW | RTLD_NOLOAD);
        if (lib == nullptr) lib = dlopen("libandroid.so", RTLD_NOW);
        if (lib != nullptr) {
            g_setFrameRate = reinterpret_cast<SetFrameRateFn>(dlsym(lib, "ANativeWindow_setFrameRate"));  // CAST-OK: dlsym
        }
        std::fprintf(stderr, "[Window] ANativeWindow_setFrameRate %s\n",
                     g_setFrameRate != nullptr ? "resolved" : "absent (API 29): the display keeps its own rate");
    }
    if (g_setFrameRate == nullptr) return;
    SDL_Window* window = Sdl::GameWindow();
    if (window == nullptr) return;
    void* native = SDL_GetPointerProperty(SDL_GetWindowProperties(window),
                                          SDL_PROP_WINDOW_ANDROID_WINDOW_POINTER, nullptr);
    if (native == nullptr) return;
    if (native == g_frameRateWindow && g_frameRateApplied == g_preferredFps) return;
    const int32_t rc = g_setFrameRate(native, g_preferredFps, kFrameRateCompatibilityFixedSource);
    g_frameRateWindow = native;
    g_frameRateApplied = g_preferredFps;
    std::fprintf(stderr, "[Window] preferred frame rate %.0f -> ANativeWindow_setFrameRate = %d\n",
                 static_cast<double>(g_preferredFps), static_cast<int>(rc));
}

}  // namespace

void SetPreferredFrameRate(float fps) {
    std::lock_guard<std::mutex> guard(g_frameRateLock);
    g_preferredFps = fps > 0.0f ? fps : 0.0f;
    ApplyFrameRateLocked();
}

void ReapplyPreferredFrameRate() {
    std::lock_guard<std::mutex> guard(g_frameRateLock);
    ApplyFrameRateLocked();
}

uint64_t PrepareCreate(int32_t* width, int32_t* height) {
    (void)width;
    (void)height;
    return SDL_WINDOW_VULKAN | SDL_WINDOW_FULLSCREEN;
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

}  // namespace Window
}  // namespace Platform
