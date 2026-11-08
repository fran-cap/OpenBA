// src/app/boot_window.cpp — see boot_window.h for the rationale + measurements.

#include "boot_window.h"
#include "stall_debug.h"
#include "options.h"
#include "video/Device.h"   // B4: Video::Configuration::hideWindow
#include "platform_window.h"   // Platform::Window::ShowDefault (Phase D.4 RENDER-3)
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>

namespace BootWindow {

namespace {

// Video::Configuration+0x42 — the engine's own non-persisted "don't show the
// window" flag, read by VideoDX11 CreateDevice step 6. Mirrors Cfg::kSkipShow
// in src/render/dx11/devicedata.cpp; kept as a local constant so this file does not
// depend on the renderer's internal header.
constexpr size_t kCfgSkipShow = 0x42;

void*  g_hwnd      = nullptr;
bool   g_armed     = false;
bool   g_shown     = false;
double g_armedAtMs = 0.0;

bool DeferralEnabled() {
    return Opt().deferWindowShow;
}

} // namespace

bool ArmDeferredShow(::Video::Configuration* videoConfig, void* hwnd) {
    if (!videoConfig || !hwnd) return false;
    if (!DeferralEnabled()) {
        printf("[BootWindow] deferral disabled (BANISHED_DEFER_WINDOW_SHOW=0) — "
               "shipped ShowWindow will run inline\n");
        std::fflush(stdout);
        return false;
    }
    if (g_armed || g_shown) return g_armed;   // only the first device creation arms

    // B5 DEV-A (B5_RULES D7): src/render/dx11/devicedata.cpp hands the typed configuration.
    videoConfig->hideWindow = 1;   // +0x42 (kCfgSkipShow)

    g_hwnd      = hwnd;
    g_armed     = true;
    g_shown     = false;
    g_armedAtMs = StallDebug::NowMs();
    printf("[BootWindow] armed: Configuration+0x42 set, hwnd=%p stays hidden "
           "until the main loop presents a frame\n", hwnd);
    std::fflush(stdout);
    return true;
}

void ShowNow(const char* reason) {
    if (!g_armed || g_shown || !g_hwnd) return;
    g_shown = true;
    Platform::Window::ShowDefault(g_hwnd);
    printf("[BootWindow] revealing hwnd=%p (%s)\n", g_hwnd,
           reason ? reason : "?");
    std::fflush(stdout);
    StallDebug::Mark("  BootWindow reveal  <== WINDOW NOW VISIBLE");
}

bool IsPending() {
    return g_armed && !g_shown;
}

void MarkFrame() {
    if (!IsPending()) return;
    printf("[BootWindow] first frame presented %.0f ms after the arm\n",
           StallDebug::NowMs() - g_armedAtMs);
    ShowNow("main loop presented its first frame");
}

} // namespace BootWindow

// Engine::MainLoop::Run calls this after every Frame (src/engine/engine/mainloop_run.cpp),
// declared there the way BootState_MarkFrame is, so the engine includes no app header.
extern "C" void BootWindow_MarkFrame() {
    BootWindow::MarkFrame();
}
