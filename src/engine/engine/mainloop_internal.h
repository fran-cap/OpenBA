#pragma once
// ============================================================================
// src/engine/engine/mainloop_internal.h — Wave 25 lane BODY
//
// ONE owner for the clock expression `MainLoop` inlines in three places
// (`Run` 0x1800dd3da, `Frame` 0x1800dd119, `Draw` 0x1800dcd40).
// ============================================================================
//
// WHY THIS FILE EXISTS AT ALL.  `mainloop_run.cpp` carried this helper in an
// anonymous namespace, which was right while it had exactly one caller.  Lane
// BODY adds `MainLoop::Frame`, which inlines the SAME four instructions against
// the SAME three globals.  Two copies of a clock that must agree on an EPOCH is
// how the two-epoch bug that mainloop_run.cpp §2 describes gets reintroduced by
// a later edit to one copy — so the helper gets ONE definition (in
// mainloop_frame.cpp) and both files call it through this declaration.
// Nothing about its behaviour changes; this is a move, not a rewrite.
//
// `System::Timer::CurrentTimeTicks()` (src/engine/system/timer.cpp) computes the
// same expression from the same Timer class statics, so there is one epoch.
//
// The header deliberately declares NO types.
// ============================================================================

#include <atomic>
#include <cstdint>

namespace rtmainloop {

// Retail's inlined System::Timer::CurrentTimeTicks:
//
//     CALL [0x18014b1a8]              ; QueryPerformanceCounter
//     SUB  RAX,[0x1801fe738]          ; _systemInitialTick
//     IMUL RAX,[0x1801fe720]          ; _ticksPerSecond (1000000)
//     CQO / IDIV [0x1801fe730]        ; _systemTicksPerSecond (QPF)
//
// Defined in mainloop_frame.cpp.
int64_t CurrentTimeTicksViaGlobals();

}  // namespace rtmainloop
