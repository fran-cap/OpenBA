// ============================================================================
// src/platform/darwin/time_darwin.cpp -- Platform:: clocks and timed waits over POSIX
// ============================================================================
// The POSIX half of src/platform/common/platform_time.h (Phase D.4 THREADS E.9).
//
// THE PERFORMANCE COUNTER runs at 10 MHz: CLOCK_MONOTONIC nanoseconds / 100. Never
// 1 GHz: Timer::CurrentTimeTicks computes (now - initial) * 1e6 / freq in int64,
// which overflows after 2.6 hours at 1 GHz and after 10.7 days at 10 MHz (the
// QueryPerformanceFrequency most Windows machines report; plans/THREADS.md F.4).
//
// WALL CLOCK: CLOCK_REALTIME converted into FILETIME units (FileTimeFromUnix).
// LOCAL TIME: localtime_r, the zone the C library resolves (no getenv of ours).
// ============================================================================

#include "platform_time.h"

#include <time.h>
#include <cerrno>

namespace Platform {

namespace {

constexpr int64_t kPerfHz = 10000000;   // 10 MHz: 100 ns per tick

int64_t MonotonicNs() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<int64_t>(ts.tv_sec) * 1000000000 + static_cast<int64_t>(ts.tv_nsec);
}

}  // namespace

bool PerfCounter(int64_t* ticks) {
    *ticks = MonotonicNs() / 100;
    return true;
}

bool PerfFrequency(int64_t* hz) {
    *hz = kPerfHz;
    return true;
}

// GetTickCount64: milliseconds since boot. CLOCK_BOOTTIME counts suspend like
// Windows does; elsewhere CLOCK_MONOTONIC.
uint64_t TickCount64() {
    struct timespec ts;
#if defined(CLOCK_BOOTTIME)
    clock_gettime(CLOCK_BOOTTIME, &ts);
#else
    clock_gettime(CLOCK_MONOTONIC, &ts);
#endif
    return static_cast<uint64_t>(ts.tv_sec) * 1000u + static_cast<uint64_t>(ts.tv_nsec) / 1000000u;
}

uint64_t NowFileTime() {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return FileTimeFromUnix(static_cast<int64_t>(ts.tv_sec), static_cast<uint32_t>(ts.tv_nsec));
}

void LocalTimeNow(LocalTime* out) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    struct tm tmv;
    const time_t secs = ts.tv_sec;
    localtime_r(&secs, &tmv);
    out->year = static_cast<uint16_t>(tmv.tm_year + 1900);
    out->month = static_cast<uint16_t>(tmv.tm_mon + 1);
    out->day = static_cast<uint16_t>(tmv.tm_mday);
    out->hour = static_cast<uint16_t>(tmv.tm_hour);
    out->minute = static_cast<uint16_t>(tmv.tm_min);
    out->second = static_cast<uint16_t>(tmv.tm_sec);
    out->milliseconds = static_cast<uint16_t>(ts.tv_nsec / 1000000);
}

// The waitable timer's POSIX form needs no kernel object: clock_nanosleep on
// CLOCK_MONOTONIC is already sub-millisecond. The handle only has to be non-null.
struct WaitTimerObject {
    int unused;
};

WaitTimerHandle WaitTimerCreate() { return new WaitTimerObject(); }

void WaitTimerDestroy(WaitTimerHandle t) { delete t; }

bool WaitTimerSleep(WaitTimerHandle t, int64_t relative100ns) {
    if (t == nullptr) return false;
    if (relative100ns <= 0) return true;
    struct timespec req{ static_cast<time_t>(relative100ns / 10000000),
                         static_cast<long>((relative100ns % 10000000) * 100) };
    while (nanosleep(&req, &req) != 0 && errno == EINTR) {}
    return true;
}

}  // namespace Platform
