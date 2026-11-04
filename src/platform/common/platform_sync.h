#pragma once
// ============================================================================
// src/platform/common/platform_sync.h -- locks and events (Phase D.4, THREADS seam)
// ============================================================================
// D4_RULES C1: namespace `Platform`, free functions, NO OS header. Implementations:
//   src/platform/win/sync_win.cpp      one Win32 call per primitive (the calls the
//                                      engine made before, with the same arguments)
//   src/platform/common/event_std.cpp  the std::mutex + condition_variable events the
//                                      POSIX build uses (host-tested on Windows too)
//
// LOCKS. Every lock the engine embeds is a Win32 CRITICAL_SECTION held as 0x28 opaque
// bytes at a retail offset (System::CriticalSection, MemoryAllocator+0x640,
// Video::Device+0x60, Audio::Stream+0x50). `LockStorage` is those same 40 bytes,
// constructed IN PLACE by `MutexInit`: no heap object, so the allocator's own lock does
// not allocate from the allocator it guards. Darwin stores a pointer to a libc-heap
// mutex in that slot because its native mutex is larger. Every lock is RECURSIVE (CRITICAL_SECTION
// is; Video::Device::Lock relies on it). Each impl asserts its native object fits.
//
// EVENTS. `EventHandle` is a pointer to an INCOMPLETE type, 8 bytes, so the Thread /
// Stream fields keep their offsets. On Windows the value IS the Win32 event HANDLE.
// Wait results use Win32's numbering: the signalled index (0 = WAIT_OBJECT_0),
// `kWaitTimeout` (WAIT_TIMEOUT), `kWaitFailed` (WAIT_FAILED).
// ============================================================================

#include <cstdint>

#include "system/thread_annotations.h"   // CAPABILITY / ACQUIRE / RELEASE (empty off clang)

namespace Platform {

// ---- locks ------------------------------------------------------------------
// A capability to clang's thread-safety analysis (thread_annotations.h); the attribute changes no
// layout. The analysis does not model recursion: a recursive re-take is stated where it happens.
struct CAPABILITY("mutex") alignas(8) LockStorage {
    uint8_t bytes[0x28];
};
static_assert(sizeof(LockStorage) == 0x28, "LockStorage is the retail 0x28-byte CRITICAL_SECTION slot");
static_assert(alignof(LockStorage) == 8, "LockStorage keeps the slot's 8-byte alignment");

// Windows: InitializeCriticalSection(lock), then SetCriticalSectionSpinCount(lock,
// spinCount) when spinCount != 0 (every retail lock passes 5000; the port-only
// res_load_lock passes 0, i.e. it never set a spin count).
void MutexInit(LockStorage* lock, uint32_t spinCount);
void MutexLock(LockStorage* lock) ACQUIRE(*lock);     // EnterCriticalSection: recursive
void MutexUnlock(LockStorage* lock) RELEASE(*lock);   // LeaveCriticalSection
void MutexDestroy(LockStorage* lock);   // DeleteCriticalSection

// ---- one-time initialisation ---------------------------------------------------
// INIT_ONCE: one pointer, zero = not run (INIT_ONCE_STATIC_INIT). OnceRun runs `fn`
// exactly once across threads, the losers blocking until it has returned. Windows:
// InitOnceExecuteOnce, which needs no CRT once machinery (res_load_lock.cpp says why).
struct OnceFlag {
    void* state;
};
static_assert(sizeof(OnceFlag) == 8, "OnceFlag is an INIT_ONCE");
void OnceRun(OnceFlag* flag, void (*fn)());

// ---- events -----------------------------------------------------------------
struct EventObject;                     // never defined on Windows: the pointer is the HANDLE
using EventHandle = EventObject*;
static_assert(sizeof(EventHandle) == 8, "an EventHandle fills a retail 8-byte HANDLE slot");

constexpr uint32_t kInfinite    = 0xFFFFFFFFu;   // INFINITE
constexpr uint32_t kWaitTimeout = 0x00000102u;   // WAIT_TIMEOUT
constexpr uint32_t kWaitFailed  = 0xFFFFFFFFu;   // WAIT_FAILED

// CreateEventW(NULL, manualReset, initiallySignaled, NULL)
EventHandle EventCreate(bool manualReset, bool initiallySignaled);
bool EventSet(EventHandle e);           // SetEvent
bool EventReset(EventHandle e);         // ResetEvent
// WaitForSingleObject(e, timeoutMs): 0 when signalled (an AUTO-reset event is
// consumed, a MANUAL one is not), kWaitTimeout, or kWaitFailed.
uint32_t EventWait(EventHandle e, uint32_t timeoutMs);
// WaitForMultipleObjects(count, events, FALSE, timeoutMs): wait for ANY; returns the
// LOWEST signalled index and consumes only that event if it is auto-reset.
uint32_t EventWaitAny(uint32_t count, const EventHandle* events, uint32_t timeoutMs);
bool EventClose(EventHandle e);         // CloseHandle

}  // namespace Platform
