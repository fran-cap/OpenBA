// src/app/frame_limiter.cpp
// See frame_limiter.h for rationale.


#include "frame_limiter.h"
#include "frame_limiter_ceiling.h"
#include "options.h"

#include "platform_window.h"   // Platform::Window::GetCurrentDisplayRefreshHz (SDL W4)
#include "platform_window_hooks.h"   // Platform::Window::SetPreferredFrameRate (Android P12)
#include "platform_perf_hint.h"   // Platform::PerfHint: the main thread's performance hint session (Android P12)
#include "platform_thread.h"   // Platform::SleepMs
#include "platform_time.h"     // Platform::PerfCounter / PerfFrequency / WaitTimer* / CpuRelax
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include "rt_crt.h"   // rtcrt::StrICmp, retail's _stricmp (D.4 Q9)

namespace FrameLimiter {

namespace {

bool      g_initialized   = false;
int64_t   g_qpcFreq       = 0;        // QueryPerformanceFrequency

// The three sources (frame_limiter_ceiling.h), resolved at the first Tick except the settings cap.
HarnessCap g_harness      = HarnessCap::None;   // BANISHED_FPS_CAP
double    g_harnessFps    = 0.0;
double    g_autoFps       = 0.0;      // the menu's refresh + headroom ceiling
double    g_settingsFps   = 0.0;      // SetSettingsCap (ext.fps_cap), 0 = none

int64_t   g_intervalTicks = 0;        // QPC ticks per frame at the ceiling
double    g_capFps        = 0.0;      // the ceiling the last tick paced to (0 = not paced)
bool      g_lastMenu      = true;     // which loop the last tick was

int64_t   g_lastFrameTick = 0;        // QPC stamp the prior frame was released at
bool      g_havePrev      = false;    // false until the first paced tick primes it

// High-resolution waitable timer (Win10 1803+). Lets us sleep with sub-ms
// precision without timeBeginPeriod's process-wide clock bump. Null => fall
// back to a plain Sleep(1)+spin loop. Platform::WaitTimerCreate keeps the
// high-resolution-then-plain fallback (src/platform/win/time_win.cpp).
Platform::WaitTimerHandle g_timer = nullptr;

// FPS accounting (updated once per ~second).
int64_t   g_fpsWindowStart = 0;
int       g_fpsFrameCount  = 0;
double    g_measuredFps    = 0.0;

inline int64_t QpcNow() {
    int64_t li;
    Platform::PerfCounter(&li);   // QueryPerformanceCounter
    return li;
}

// Query the primary display's current refresh rate in Hz. Returns 0 if unknown.
// Phase D.4 SDL W4: was EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS)'s
// dmDisplayFrequency (0 or 1 = unknown); now SDL's current mode of the primary display,
// rounded to whole Hz (display_sdl.cpp).
int QueryDisplayRefreshHz() {
    return Platform::Window::GetCurrentDisplayRefreshHz();
}

void Init() {
    g_initialized = true;

    int64_t f;
    Platform::PerfFrequency(&f);  // QueryPerformanceFrequency
    g_qpcFreq = f;

    // P12: this (the main) thread's performance hint session; a no-op off Android (platform_perf_hint.h).
    Platform::PerfHint::AttachThread(Platform::PerfHint::Role::Main);

    // --- The harness flag ---
    if (const char* capEnv = Opt().fpsCap) {
        if (rtcrt::StrICmp(capEnv, "off") == 0 || rtcrt::StrICmp(capEnv, "false") == 0) {
            g_harness = HarnessCap::Off;
        } else {
            double v = atof(capEnv);
            if (v <= 0.0) {
                g_harness = HarnessCap::Off;   // "0" => disabled
            } else {
                g_harness    = HarnessCap::Fixed;
                g_harnessFps = v;
            }
        }
    }

    // --- The menu's automatic ceiling ---
    int refresh = QueryDisplayRefreshHz();
    if (refresh <= 0) refresh = 60;     // safe default if the query fails
    double headroomPct = 50.0;          // default: ceiling = refresh * 1.5
    if (const char* hrEnv = Opt().fpsHeadroom) {
        double h = atof(hrEnv);
        if (h >= 0.0) headroomPct = h;
    }
    g_autoFps = (double)refresh * (1.0 + headroomPct / 100.0);

    if (g_harness == HarnessCap::Off) {
        // Log to stderr: in standalone, stdout is freopen'd to standalone_debug.log
        // which a stale instance can hold open (redirect silently fails, output
        // lost); stderr -> standalone_error.log is reliable and survives close.
        fprintf(stderr, "[FrameLimiter] disabled (uncapped) via BANISHED_FPS_CAP\n");
        fflush(stderr);
    }
}

// A new ceiling for the loop about to be paced (0 = none): the interval, the timer, one log line,
// and the cadence primed again.
void SetCeiling(double fps, bool menu) {
    if (fps == g_capFps && menu == g_lastMenu) return;
    const bool logIt = (fps != g_capFps);
    g_capFps   = fps;
    g_lastMenu = menu;
    g_havePrev = false;
    // P12: the display is told the rate too (a no-op off Android, platform_window_hooks.h).
    if (logIt) Platform::Window::SetPreferredFrameRate(fps > 0.0 ? static_cast<float>(fps) : 0.0f);
    // ... and the performance hint sessions their frame budget (0 = none).
    Platform::PerfHint::SetTarget(fps > 0.0 ? static_cast<int64_t>(1.0e9 / fps) : 0);
    if (fps <= 0.0) {
        if (logIt) {
            fprintf(stderr, "[FrameLimiter] %s: not paced\n", menu ? "menu" : "game");
            fflush(stderr);
        }
        return;
    }
    g_intervalTicks = (int64_t)((double)g_qpcFreq / fps);
    if (g_intervalTicks < 1) g_intervalTicks = 1;
    // CreateWaitableTimerExW(.., HIGH_RESOLUTION, TIMER_ALL_ACCESS), and on an
    // older OS without the high-res flag CreateWaitableTimerW(NULL, FALSE, NULL).
    if (g_timer == nullptr) g_timer = Platform::WaitTimerCreate();
    if (logIt) {
        const char* source = g_harness == HarnessCap::Fixed ? "BANISHED_FPS_CAP"
                             : fps == g_settingsFps          ? "settings cap"
                                                             : "refresh+headroom";
        fprintf(stderr,
               "[FrameLimiter] %s: ceiling = %.1f FPS (%.3f ms/frame) [%s], "
               "hi-res timer=%s. External limiters (vsync/G-Sync/RTSS) at or "
               "below the ceiling are never affected.\n",
               menu ? "menu" : "game", fps, 1000.0 / fps, source,
               g_timer ? "yes" : "no (Sleep fallback)");
        fflush(stderr);
    }
}

// Block until `deadlineTick`. Coarse-sleeps the bulk via the waitable timer,
// then busy-spins the final fraction for an accurate release time.
void WaitUntil(int64_t deadlineTick) {
    // Tiny spin tail (~0.25 ms): the high-res timer is accurate enough that we
    // don't want to burn a core fraction spinning — this limiter exists to SAVE
    // CPU, so favor the sleep.
    const int64_t spinMarginTicks = g_qpcFreq / 4000;

    for (;;) {
        int64_t now = QpcNow();
        int64_t remaining = deadlineTick - now;
        if (remaining <= 0) break;

        if (remaining > spinMarginTicks && g_timer) {
            // Sleep all but the spin tail. SetWaitableTimer due time is in
            // 100 ns units, negative = relative.
            // WaitTimerSleep is SetWaitableTimer(-due) + WaitForSingleObject(INFINITE);
            // it returns false, without waiting, when the timer cannot be armed.
            int64_t sleepTicks = remaining - spinMarginTicks;
            const int64_t due100ns = (sleepTicks * 10000000) / g_qpcFreq;
            if (!(due100ns > 0 && Platform::WaitTimerSleep(g_timer, due100ns))) {
                Platform::SleepMs(0);   // Sleep(0)
            }
        } else {
            // Final sub-ms tail: spin politely (hint the CPU, don't burn power).
            Platform::CpuRelax();       // YieldProcessor
        }
    }
}

// One paced frame of `menu`'s loop (Tick / TickInGame).
void Pace(bool menu) {
    if (!g_initialized) Init();
    SetCeiling(ResolveCeiling(g_harness, g_harnessFps, g_autoFps, g_settingsFps, menu), menu);
    if (g_capFps <= 0.0) return;

    int64_t now = QpcNow();

    if (!g_havePrev) {
        g_havePrev       = true;
        g_lastFrameTick  = now;
        g_fpsWindowStart = now;
        g_fpsFrameCount  = 0;
        Platform::PerfHint::WorkBegin(Platform::PerfHint::Role::Main);
        return;
    }

    // The frame's work on this thread ends here; the wait below is not work.
    Platform::PerfHint::WorkEnd(Platform::PerfHint::Role::Main);

    int64_t deadline = g_lastFrameTick + g_intervalTicks;

    if (now < deadline) {
        WaitUntil(deadline);
        now = QpcNow();
    }
    Platform::PerfHint::WorkBegin(Platform::PerfHint::Role::Main);

    // Advance the cadence. If we fell behind by more than one full interval
    // (breakpoint, alt-tab, a long hitch), resync to `now` instead of trying
    // to "catch up" with a burst of un-throttled frames.
    if (now > deadline + g_intervalTicks) {
        g_lastFrameTick = now;
    } else {
        g_lastFrameTick = deadline;
    }

    // --- FPS measurement (once per ~second; the readout every second in the menu, every ten in
    // game, so a long session's log stays short) ---
    g_fpsFrameCount++;
    int64_t elapsed = now - g_fpsWindowStart;
    if (elapsed >= g_qpcFreq) {
        g_measuredFps    = (double)g_fpsFrameCount * (double)g_qpcFreq / (double)elapsed;
        g_fpsWindowStart = now;
        g_fpsFrameCount  = 0;
        static int s_gameWindows = 0;
        if (menu || (++s_gameWindows % 10) == 0) {
            // Periodic readout to stderr (reliable; see Init()).
            fprintf(stderr, "[FrameLimiter] fps=%.1f (cap=%.1f)%s\n",
                    g_measuredFps, g_capFps, menu ? "" : " in game");
            fflush(stderr);
        }
    }
}

} // namespace

void Tick() {
    Pace(true);
}

void TickInGame() {
    // The common case off the phone (no flag, no settings cap): one test, no clock read.
    if (g_initialized && g_harness != HarnessCap::Fixed && g_settingsFps <= 0.0) {
        if (g_capFps != 0.0 || g_lastMenu) SetCeiling(0.0, false);
        return;
    }
    Pace(false);
}

void SetSettingsCap(double fps) {
    if (fps < 0.0) fps = 0.0;
    if (fps == g_settingsFps) return;
    g_settingsFps = fps;
    fprintf(stderr, "[FrameLimiter] settings cap = %.0f FPS%s\n", fps, fps > 0.0 ? "" : " (none)");
    fflush(stderr);
}

double GetFps()    { return g_measuredFps; }
double GetCapFps() { return g_capFps; }

} // namespace FrameLimiter

