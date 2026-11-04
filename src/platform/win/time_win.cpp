// ============================================================================
// src/platform/win/time_win.cpp -- Platform:: clocks and timed waits over Win32
// ============================================================================
// The Windows half of src/platform/common/platform_time.h, one call each (the
// waitable timer's creation keeps the frame limiter's high-resolution-then-plain
// fallback, moved here from src/app/frame_limiter.cpp unchanged).
// ============================================================================

#include "platform_time.h"

#include <windows.h>

// CREATE_WAITABLE_TIMER_HIGH_RESOLUTION is only in newer SDK headers (Win10 1803+).
#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

static_assert(sizeof(LARGE_INTEGER) == sizeof(int64_t), "QPC writes one int64");

namespace Platform {

bool PerfCounter(int64_t* ticks) {
    return QueryPerformanceCounter(reinterpret_cast<LARGE_INTEGER*>(ticks)) != FALSE;  // CAST-OK: win32
}
bool PerfFrequency(int64_t* hz) {
    return QueryPerformanceFrequency(reinterpret_cast<LARGE_INTEGER*>(hz)) != FALSE;  // CAST-OK: win32
}
uint64_t TickCount64() { return GetTickCount64(); }
uint64_t NowFileTime() {
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    return (static_cast<uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
}

void LocalTimeNow(LocalTime* out) {
    SYSTEMTIME st;
    GetLocalTime(&st);
    out->year         = st.wYear;
    out->month        = st.wMonth;
    out->day          = st.wDay;
    out->hour         = st.wHour;
    out->minute       = st.wMinute;
    out->second       = st.wSecond;
    out->milliseconds = st.wMilliseconds;
}

WaitTimerHandle WaitTimerCreate() {
    HANDLE t = CreateWaitableTimerExW(nullptr, nullptr,
                                      CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    if (!t) {
        // Older OS without the high-resolution flag: a plain auto-reset timer.
        t = CreateWaitableTimerW(nullptr, FALSE, nullptr);
    }
    return static_cast<WaitTimerHandle>(t);  // CAST-OK: win32
}
void WaitTimerDestroy(WaitTimerHandle t) {
    if (t != nullptr) CloseHandle(static_cast<HANDLE>(t));   // CAST-OK: win32
}
bool WaitTimerSleep(WaitTimerHandle t, int64_t relative100ns) {
    LARGE_INTEGER due;
    due.QuadPart = -relative100ns;   // negative = relative
    if (!SetWaitableTimer(t, &due, 0, nullptr, nullptr, FALSE)) return false;
    WaitForSingleObject(t, INFINITE);
    return true;
}

}  // namespace Platform
