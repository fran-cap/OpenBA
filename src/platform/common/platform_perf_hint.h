#pragma once
// ============================================================================
// src/platform/common/platform_perf_hint.h -- performance hint sessions and the thermal log
// ============================================================================
// NOT RETAIL. Tells the OS the frame budget the game paces to and how long each frame's work took on
// the threads that carry a frame, so its governor can pick the core and the clock that just meet the
// budget (no game-visible behaviour: the frame cap, the simulation and the draws are untouched).
//   Android (src/platform/android/perf_hint_android.cpp): ADPF. APerformanceHint_* (API 31) and
//            AThermal_* (API 30) are asked of libandroid.so at run time (dlsym; minSdk is 29), so a
//            phone without them runs as before. Every call on a session is made by its own thread.
//   Everywhere else (src/platform/posix/perf_hint_posix.cpp, also macOS's; src/platform/win/
//            perf_hint_win.cpp): identity bodies.
// D4_RULES C1: namespace `Platform`, free functions, NO OS header here.
// Callers: src/app/frame_limiter.cpp (the target, the main thread's session),
// src/ext/render_pipe/render_pipe.cpp (the render thread's session).
// ============================================================================

#include <cstdint>

namespace Platform {
namespace PerfHint {

// The thread a session times.
enum class Role {
    Main,     // the thread that runs the loop (frame limiter, simulation, draw gather)
    Render,   // ext.render_pipe's recording thread
};

// The frame budget of every session, nanoseconds: the frame limiter's ceiling (0 = the loop is not
// paced; the reports are then dropped, as there is no budget to hold). Any thread; each session picks
// a new target up at its thread's next WorkEnd.
void SetTarget(int64_t targetNs);

// The CALLING thread becomes `role`'s thread. Its session is made at its first WorkEnd with a target
// (one `[ADPF]` line: the tid, the target, the power-efficiency preference); a role re-attached from
// another thread (the render thread is re-created when ext.render_pipe is toggled) closes the old
// session and makes a new one for the new tid. The first attach of the process also starts the
// thermal log where the OS has one: `[Thermal] status N` at start and at every change (a log for
// measurements only: nothing reads it). Android also pins the calling thread to the CPUs of the
// largest capacity when a heterogeneous SoC has at least two (the frame's critical path,
// perf_hint_android.cpp); elsewhere, and on a uniform CPU, nothing is pinned.
void AttachThread(Role role);

// `role`'s thread is about to exit: its session is closed (called by that thread).
void DetachThread(Role role);

// One frame's work on `role`'s thread (called by that thread): the thread's CPU time between the two
// calls is reported at WorkEnd, so every wait inside the bracket (the swap chain, the hand-off, a
// fence) is excluded and the governor sees the work, not the frame. A WorkEnd with no WorkBegin since
// the last one reports nothing.
void WorkBegin(Role role);
void WorkEnd(Role role);

}  // namespace PerfHint
}  // namespace Platform
