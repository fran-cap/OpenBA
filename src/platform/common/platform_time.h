#pragma once
// ============================================================================
// src/platform/common/platform_time.h -- clocks and timed waits (Phase D.4, THREADS seam)
// ============================================================================
// D4_RULES C1: namespace `Platform`, free functions, NO OS header. Windows bodies:
// src/platform/win/time_win.cpp, one call each.
//
// THE PERFORMANCE COUNTER keeps QueryPerformanceCounter's shape (an out-parameter and
// a success flag) so System::Timer's int64 statics stay in counter units. A POSIX
// impl reports CLOCK_MONOTONIC at 10 MHz, never 1 GHz: Timer::CurrentTimeTicks
// computes (now - initial) * 1e6 / freq in int64, which overflows after 2.6 hours at
// 1 GHz and after 10.7 days at 10 MHz (plans/THREADS.md F.4).
//
// FILETIME units (100 ns since 1601-01-01 UTC) are the engine's wall-clock unit
// (System::DateTime holds one); `FileTimeFromUnix` is the portable conversion.
// ============================================================================

#include <cstdint>

namespace Platform {

bool PerfCounter(int64_t* ticks);       // QueryPerformanceCounter
bool PerfFrequency(int64_t* hz);        // QueryPerformanceFrequency
uint64_t TickCount64();                 // GetTickCount64: milliseconds since boot
uint64_t NowFileTime();                 // GetSystemTimeAsFileTime, as one 64-bit value

// Seconds between 1601-01-01 and 1970-01-01, and the conversion a POSIX clock needs.
constexpr int64_t kFileTimeUnixEpochSeconds = 11644473600LL;
constexpr uint64_t FileTimeFromUnix(int64_t seconds, uint32_t nanoseconds) {
    return static_cast<uint64_t>(seconds + kFileTimeUnixEpochSeconds) * 10000000ull
         + nanoseconds / 100u;
}

// GetLocalTime, field for field (SYSTEMTIME without the day of the week).
struct LocalTime {
    uint16_t year, month, day, hour, minute, second, milliseconds;
};
void LocalTimeNow(LocalTime* out);

// A one-shot timer for sub-millisecond sleeps (the frame limiter's). Windows: a
// high-resolution waitable timer, falling back to a plain one on an OS without
// CREATE_WAITABLE_TIMER_HIGH_RESOLUTION; null if neither can be created.
struct WaitTimerObject;
using WaitTimerHandle = WaitTimerObject*;
WaitTimerHandle WaitTimerCreate();
// Releases a WaitTimerCreate handle (CloseHandle / delete); null is a no-op. The frame limiter's
// timer lives for the process; a short-lived timer (a test) gives its handle back here.
void WaitTimerDestroy(WaitTimerHandle t);
// SetWaitableTimer(t, -relative100ns) then WaitForSingleObject(t, INFINITE). Returns
// false without waiting when the timer could not be armed.
bool WaitTimerSleep(WaitTimerHandle t, int64_t relative100ns);

// YieldProcessor: the spin-wait hint (PAUSE on x86). Inline, it is one instruction.
inline void CpuRelax() {
#if defined(__x86_64__) || defined(__i386__)
    __builtin_ia32_pause();
#elif defined(__aarch64__)
    __asm__ __volatile__("yield");
#endif
}

}  // namespace Platform
