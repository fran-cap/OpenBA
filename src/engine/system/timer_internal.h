#pragma once
// ============================================================================
// src/engine/system/timer_internal.h — System::Timer's five data exports, owned by us
// ============================================================================
//
// `timer.cpp` ports the whole class; these accessors read its five statics for
// the readers outside the class (src/engine/application/application.cpp,
// src/engine/audio/audio.cpp, src/engine/save/saveload_internal.h). Two of the
// five are the compile-time constants 1000000 and 1e-6 (see timer.cpp's header
// for the disassembly that establishes that, and for the correction it makes to
// docs/history/runtime_port/WAVE9_LANE_V.md §4); the other three are QPC-derived
// and are written by `System::Timer::PlatformStartup()`. `_secondsPerTick` is
// initialised at namespace scope and is never zero.
// ============================================================================

#include <cstdint>

namespace RtSystem {

// ?_ticksPerSecond@Timer@System@@0_JA        retail 0x1801fe720  (== 1000000)
// ?_secondsPerTick@Timer@System@@0NA         retail 0x1801fe728  (== 1e-6)
// ?_systemTicksPerSecond@Timer@System@@0_JA  retail 0x1801fe730  (QPF)
// ?_systemInitialTick@Timer@System@@0_JA     retail 0x1801fe738  (QPC at startup)
// ?_systemSecondsPerTick@Timer@System@@0NA   retail 0x1801fe740  (1/QPF)
//
// The three QPC-derived ones are zero until PlatformStartup has run;
// TimerEnsurePlatformStarted() makes that true and is idempotent.
int64_t TimerTicksPerSecond();
double  TimerSecondsPerTick();
int64_t TimerSystemTicksPerSecond();
int64_t TimerSystemInitialTick();
double  TimerSystemSecondsPerTick();

void TimerEnsurePlatformStarted();

}  // namespace RtSystem
