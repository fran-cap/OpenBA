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
// Windows: PeekMessageW(&m, nullptr, 0, 0, PM_NOREMOVE | PM_QS_SENDMESSAGE), and only when
// GetQueueStatus(QS_SENDMESSAGE) says one is waiting. POSIX: there are no cross-thread sent
// messages: a no-op that returns false (the caller waits without polling).
//
// THE HUNG WINDOW (the "Banished (Not Responding)" ghost). Handoff's main-thread waits call
// this every millisecond, and a render-bound frame spends most of its time there. A filtered
// PeekMessage (a PM_QS_* mask or a message range) that finds nothing, called that often, makes
// IsHungAppWindow TRUE about 5 s later although the pump's unfiltered PM_REMOVE drain still runs
// every step: measured in the game (Gate.sav 1x, ext.render_pipe on: 12 of 15 two-second samples
// hung) and in a bare Win32 loop (a PM_REMOVE drain every 16 ms, filtered peeks every 1 ms
// between: hung; plain Sleep, an unfiltered PM_NOREMOVE peek, or the GetQueueStatus test first:
// never; a filtered peek that does run a sent message: never). GetQueueStatus reads the queue
// without that effect, so the peek runs only when there is a sent message for it to run. Nothing
// posted or input is retrieved either way: the pump inside the step is still the only reader.
bool DispatchSentMessages();

}  // namespace Platform
