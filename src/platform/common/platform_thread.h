#pragma once
// ============================================================================
// src/platform/common/platform_thread.h -- OS threads (Phase D.4, THREADS seam)
// ============================================================================
// D4_RULES C1: namespace `Platform`, free functions, NO OS header. Windows bodies:
// src/platform/win/thread_win.cpp, one call each.
//
// `ThreadHandle` is a pointer to an INCOMPLETE type, 8 bytes (System::Thread+0x10).
// On Windows the value IS the handle _beginthreadex returned.
//
// STACK SIZE. On Windows the `stackSize` argument of _beginthreadex is the initial
// COMMIT; the reserve is the PE default (2 MB with GNU ld), so every engine worker
// really has 2 MB whatever it asks for. A POSIX impl must give max(stackSize, 2 MB),
// never the literal 0x10000 (plans/THREADS.md F.3).
// ============================================================================

#include <cstdint>

namespace Platform {

struct ThreadObject;                    // never defined on Windows: the pointer is the HANDLE
using ThreadHandle = ThreadObject*;
static_assert(sizeof(ThreadHandle) == 8, "a ThreadHandle fills a retail 8-byte HANDLE slot");

// The entry signature _beginthreadex takes (`__stdcall` is a no-op on x64).
using ThreadEntry = unsigned int (*)(void*);

// _beginthreadex(NULL, stackSize, entry, arg, CREATE_SUSPENDED, &id): the thread
// does not run until ThreadResume.
ThreadHandle ThreadCreateSuspended(ThreadEntry entry, void* arg, uint32_t stackSize);
bool ThreadSetNormalPriority(ThreadHandle t);   // SetThreadPriority(t, THREAD_PRIORITY_NORMAL)
// NOT RETAIL (block CPULIMIT): ext.path_workers' pool threads only; retail's threads keep
// NORMAL. Windows: SetThreadPriority(t, THREAD_PRIORITY_BELOW_NORMAL). POSIX: a no-op that
// returns false -- SCHED_OTHER has no per-thread priority through pthread_setschedparam
// (its static priority must be 0), and a per-thread nice needs the kernel tid, which a
// ThreadHandle does not carry.
bool ThreadSetBelowNormalPriority(ThreadHandle t);
// NOT RETAIL (the derived cache's rewrite, src/ext/derived_cache/derived_cache.cpp): the CALLING
// thread's background processing mode on (`on`) or off. Windows: SetThreadPriority(GetCurrentThread(),
// THREAD_MODE_BACKGROUND_BEGIN / _END), which lowers its I/O and memory priority so a long disk job
// stays out of the foreground's way. POSIX: a no-op that returns false (no per-thread I/O priority
// through a portable call).
bool ThreadBackgroundMode(bool on);
uint32_t ThreadResume(ThreadHandle t);          // ResumeThread: the previous suspend count
uint32_t ThreadJoin(ThreadHandle t);            // WaitForSingleObject(t, INFINITE)
bool ThreadClose(ThreadHandle t);               // CloseHandle

bool YieldThread();                             // SwitchToThread
void SleepMs(uint32_t ms);                      // Sleep
// GetCurrentThreadId. Equality is all the engine relies on (Video::Device+0x88,
// where 0xFFFFFFFF means "no owner"); an impl never returns 0xFFFFFFFF.
uint32_t CurrentThreadId();

// NOT RETAIL (ext.render_pipe, review F-224): run the window procedures of the messages other
// threads SENT to the calling thread's windows, taking no posted message, and return true. A
// thread parked in a plain wait never runs them, so their sender blocks until it wakes.
// Windows: PeekMessageW(&m, nullptr, 0, 0, PM_NOREMOVE | PM_QS_SENDMESSAGE). POSIX: there are
// no cross-thread sent messages: a no-op that returns false (the caller waits without polling).
bool DispatchSentMessages();

}  // namespace Platform
