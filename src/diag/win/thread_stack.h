// src/diag/win/thread_stack.h -- the stack of ANOTHER thread of this process (Windows).
//
// The hang dump (boot_state_watchdog.cpp), the redzone watchdog's thread dump
// (redzone_alloc.cpp) and the sampling profiler (sampler.cpp) suspend a thread and report
// its call stack. Nothing may run between SuspendThread and ResumeThread that waits on a
// lock the suspended thread can hold: GetModuleHandleExA, GetModuleFileNameA and
// RtlLookupFunctionEntry wait on the loader's module-table / function-table locks, which a
// thread inside LoadLibrary / FreeLibrary or thread start / exit holds, and printf takes the
// CRT's stdout lock (review F-275, F-288, F-291, F-297). Unwinding after ResumeThread is no
// way out: the thread may unmap the very module whose unwind data is being read.
//
// So Capture unwinds while the thread is stopped, with calls that take no lock: VirtualQuery
// names the image a RIP is in, its own exception directory gives the RUNTIME_FUNCTION
// (binary search, the lookup RtlLookupFunctionEntry does under its lock), and
// RtlVirtualUnwind steps the frame. It reports raw frames; the caller symbolizes them
// (FormatFrame: module lookups) after Capture has resumed the thread. Dynamic function
// tables (RtlAddFunctionTable code) are not searched: a frame there ends the walk.
// The crash handler walks its own faulting thread and keeps StandaloneWalkStack
// (crash_handler_win.cpp).
#pragma once

#include <windows.h>

#include <cstddef>
#include <cstdint>

namespace ThreadStack {

struct Frame {
    uint64_t rip;   // frame 0: the stopped instruction; then each return address
    uint64_t rsp;   // the frame's RSP
};

enum class Status { kOk, kSuspendFailed, kContextFailed };

struct Result {
    Status status;
    DWORD error;   // GetLastError of the failed call
    int frames;    // frames written (kOk only)
};

// Suspends `th`, reads its registers into `ctx` (the caller sets ctx.ContextFlags; CONTROL
// and INTEGER at least), walks at most `maxFrames` frames, calls `whileSuspended(user)` when
// given (it must not take a lock), and resumes it.
Result Capture(HANDLE th, CONTEXT& ctx, Frame* frames, int maxFrames,
               void (*whileSuspended)(void* user) = nullptr, void* user = nullptr);

// "  #N  <module> + 0xOFF  (RIP=... RSP=...)\n", the crash log's and the hang log's frame line.
// Looks the module up: never while a thread is suspended.
void FormatFrame(char* line, size_t lineLen, int frame, uint64_t rip, uint64_t rsp);

}  // namespace ThreadStack
