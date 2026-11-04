// ============================================================================
// src/engine/application/application.cpp — Wave 8 lane E — Application::Application
// ============================================================================
// The members of this class that do NOT touch Win32 directly: IsPaused / Quit,
// and (Phase D.4 THREADS E.4, moved from src/platform/win/application_win.cpp)
// SleepThread / YieldToThreads, whose QueryPerformanceCounter / SwitchToThread
// are the Platform:: primitives PerfCounter / YieldThread. PlatformUpdate (the
// message pump) is application_platform.cpp's (SDL W3) — <windows.h> must never appear here.
//
//   ?IsPaused@Application@1@QEAA_NXZ    retail 0x18009f4e0   ordinal 3150
//   ?Quit@Application@1@QEAAXXZ         retail 0x18009f4d0   ordinal 3437
//   ?SleepThread@Application@1@QEAAXH@Z       retail 0x18009f850   ord 4069
//   ?YieldToThreads@Application@1@QEAAXXZ     retail 0x18009f920   ord 4568
//
// See src/engine/application/Application.h for the layout and the
// vtable verdict (not polymorphic, proven from the ctor's first store).
//
// `Application::Application` (the class) is NOT in this lane's function list
// and is not ported: the wave brief names only the six functions in
// docs/history/runtime_port/wave8_lanes.txt, and the ctor is not one of them. This
// file only reads/writes the two bytes IsPaused and Quit touch.
// ============================================================================

#include <cstdint>
#include <cstring>
#include "application/Application.h"
#include "system/timer_internal.h"   // RtSystem::Timer* (SleepThread's tick conversion)
#include "system/Timer.h"            // System::CounterToTicks (review F-151)
#include "platform_thread.h"         // Platform::YieldThread
#include "platform_time.h"           // Platform::PerfCounter

// ENDGAME B.3.7: `Application::Application` is the real class; a global
// `using RtApplication::Application;` is ill-formed next to the namespace.
using App = Application::Application;

// ============================================================================
// Application::Application::IsPaused() const   retail 0x18009f4e0   ord 3150
// ?IsPaused@Application@1@QEAA_NXZ
// ----------------------------------------------------------------------------
// `return this[0x11] == 0`. A plain byte read, no state mutated.
// Oracle: FULL DIFF.
// ============================================================================
bool Application::Application::IsPaused() {
    const bool ours = (_running == 0);
    return ours;
}

// ============================================================================
// Application::Application::Quit()              retail 0x18009f4d0   ord 3437
// ?Quit@Application@1@QEAAXXZ
// ----------------------------------------------------------------------------
// `this[0x28] = 1`. One byte store.
//
// ORACLE: SCRATCH-COPY DIFF over sizeof(Application).
//
// CORRECTED BY WAVE 12 LANE B (PLAN §6 step 8). This comment used to say the
// copy was "restricted to the first 0x29 bytes ... this lane does not port the
// constructor and so does not know the class's true extent — only that it is AT
// LEAST 0x29 bytes". That caveat is now stale in both halves: the extent IS
// known — 0x30, from retail's own allocation at Engine::Startup 0x1800dbbb3
// (`MemAlloc(0x30, 4)`), asserted in the header — and the constructor IS ported
// (src/engine/engine/singleton_ctors.cpp:576). Nothing about the code below
// changes: `sizeof(Application)` was already 0x30 to the compiler, because the
// struct's alignment is 8, so the copy was always a full-object copy and always
// in bounds. Only the reasoning was wrong.
// ============================================================================
void Application::Application::Quit() {
    _quitRequested = 1;
}

namespace {

// System::Timer's four data exports.
//
// These were four RAW-DOOR data reads (`the raw DLL door(double*, "?_secondsPerTick@…")`
// and its three siblings) — WAVE9_LANE_V.md §4's DATA READ row for
// `?_secondsPerTick@Timer@System@@0NA` names this file at :45. Wave 9 lane B3
// ported System::Timer and the port OWNS the storage behind all five statics
// (src/engine/system/timer_internal.h), so the reads come from us now and the row is
// closed by a port rather than by lane C2 minting a value.
//
// Two behavioural notes, both improvements and both stated rather than assumed:
//   * `_ticksPerSecond` and `_secondsPerTick` are compile-time constants
//     (1000000 and 1e-6) — see timer.cpp's header for the disassembly.
//   * the DLL's `_systemInitialTick`/`_systemTicksPerSecond` are ZERO until
//     Engine::Startup runs, and QpcToEngineTicks below divided by the latter.
//     Our accessors run PlatformStartup on first use, so that divide is now
//     safe at every point in boot.
//
// The epoch consequence: SleepThread's engine-tick readings are now measured
// from OUR PlatformStartup sample, not the DLL's. It only ever computes a
// DIFFERENCE of two readings taken inside the same call (`elapsed`), so a
// shifted origin cancels exactly.
int64_t QpcToEngineTicks(int64_t qpc) {
    // review F-151: System::CounterToTicks (Timer.h), the overflow-free spelling.
    return System::CounterToTicks(qpc - RtSystem::TimerSystemInitialTick(), RtSystem::TimerTicksPerSecond(),
                                  RtSystem::TimerSystemTicksPerSecond());
}

}  // namespace

// ============================================================================
// Application::Application::SleepThread(int ms)   retail 0x18009f850   ord 4069
// ?SleepThread@Application@1@QEAAXH@Z
// ----------------------------------------------------------------------------
// Busy-waits (SwitchToThread, not Sleep) until at least `ms` milliseconds of
// engine time, measured through the same QPC-derived tick conversion
// System::Timer uses elsewhere, have elapsed. DAT_18016172c (retail's
// millisecond divisor) reads as 1000.0f (bytes 00 00 7a 44), confirmed by
// direct memory read against the shipped DLL rather than assumed.
//
// `this` is never read — retail's SleepThread ignores the object entirely,
// which is why the parameter below is unused and untyped-through only for
// signature fidelity.
// ============================================================================
void Application::Application::SleepThread(int32_t ms) {

    int64_t t0;
    Platform::PerfCounter(&t0);                 // QueryPerformanceCounter
    const int64_t startTicks = QpcToEngineTicks(t0);

    const double targetTicks =
        (static_cast<double>(static_cast<float>(ms) / 1000.0f)) / RtSystem::TimerSecondsPerTick();

    int64_t t1;
    Platform::PerfCounter(&t1);
    int64_t elapsed = QpcToEngineTicks(t1) - startTicks;
    while (elapsed < static_cast<int64_t>(targetTicks)) {
        Platform::YieldThread();                // SwitchToThread
        Platform::PerfCounter(&t1);
        elapsed = QpcToEngineTicks(t1) - startTicks;
    }
}

// ============================================================================
// Application::Application::YieldToThreads()   retail 0x18009f920   ord 4568
// ?YieldToThreads@Application@1@QEAAXXZ
// ----------------------------------------------------------------------------
// `SwitchToThread(); return;` — five bytes past the prologue. Ghidra's
// decompile prints a spurious "could not recover jumptable / treating
// indirect jump as call" warning above it; the disassembly shows a plain
// direct CALL to the imported SwitchToThread thunk, nothing indirect. `this`
// is not read.
// ============================================================================
void Application::Application::YieldToThreads() {
    Platform::YieldThread();                    // SwitchToThread
}

