#pragma once
// ============================================================================
// src/platform/android/thread_start_android.h -- the affinity a new thread starts with (Android)
// ============================================================================
// NOT RETAIL. A Linux thread inherits its creator's affinity. The frame's two critical threads are
// pinned to the fastest cores (perf_hint_android.cpp), so a thread either of them creates would be
// pinned there too. thread_start_android.cpp wraps pthread_create (the link's
// -Wl,--wrap=pthread_create, CMakeLists.txt, Android only): while a start mask is set, every thread
// created through this library (Platform::ThreadCreateSuspended, std::thread) starts on it. SDL's
// own threads are created inside libSDL3.so, outside the wrap; they start before the pin.
// ============================================================================

#include <cstdint>

namespace Platform {
namespace Android {

// The mask (bit i = CPU i) every new thread starts on; 0 = inherit, as without the wrap.
void SetThreadStartMask(uint64_t mask);

}  // namespace Android
}  // namespace Platform
