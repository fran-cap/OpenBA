// ============================================================================
// src/platform/win/sync_win.cpp -- Platform:: locks and events over Win32
// ============================================================================
// The Windows half of src/platform/common/platform_sync.h. Each primitive is the
// Win32 call the engine made at its call site before Phase D.4, with the same
// arguments: the spelling moved, the call did not.
// ============================================================================

#include "platform_sync.h"

#include <windows.h>

static_assert(sizeof(CRITICAL_SECTION) == sizeof(Platform::LockStorage),
              "a CRITICAL_SECTION fills the 0x28-byte LockStorage exactly (x64)");
static_assert(alignof(CRITICAL_SECTION) <= alignof(Platform::LockStorage),
              "LockStorage is aligned for a CRITICAL_SECTION");
static_assert(sizeof(HANDLE) == sizeof(Platform::EventHandle), "an event handle is a HANDLE");
static_assert(sizeof(INIT_ONCE) == sizeof(Platform::OnceFlag), "a OnceFlag is an INIT_ONCE");

namespace Platform {

namespace {
CRITICAL_SECTION* Cs(LockStorage* lock) {
    return reinterpret_cast<CRITICAL_SECTION*>(lock->bytes);  // CAST-OK: win32
}
// InitOnceExecuteOnce's callback: the parameter carries the caller's function.
BOOL CALLBACK OnceTrampoline(PINIT_ONCE, PVOID fn, PVOID*) {
    reinterpret_cast<void (*)()>(fn)();  // CAST-OK: win32
    return TRUE;
}
}  // namespace

void OnceRun(OnceFlag* flag, void (*fn)()) {
    InitOnceExecuteOnce(reinterpret_cast<PINIT_ONCE>(flag), &OnceTrampoline,  // CAST-OK: win32
                        reinterpret_cast<PVOID>(fn), nullptr);  // CAST-OK: win32
}

void MutexInit(LockStorage* lock, uint32_t spinCount) {
    InitializeCriticalSection(Cs(lock));
    if (spinCount != 0) SetCriticalSectionSpinCount(Cs(lock), spinCount);
}
// The primitives the analysis takes on trust (platform_sync.h declares what they acquire / release).
void MutexLock(LockStorage* lock) NO_THREAD_SAFETY_ANALYSIS { EnterCriticalSection(Cs(lock)); }
void MutexUnlock(LockStorage* lock) NO_THREAD_SAFETY_ANALYSIS { LeaveCriticalSection(Cs(lock)); }
void MutexDestroy(LockStorage* lock) { DeleteCriticalSection(Cs(lock)); }

EventHandle EventCreate(bool manualReset, bool initiallySignaled) {
    return static_cast<EventHandle>(  // CAST-OK: win32
        CreateEventW(nullptr, manualReset ? TRUE : FALSE, initiallySignaled ? TRUE : FALSE, nullptr));
}
bool EventSet(EventHandle e)   { return SetEvent(e) != FALSE; }
bool EventReset(EventHandle e) { return ResetEvent(e) != FALSE; }
uint32_t EventWait(EventHandle e, uint32_t timeoutMs) { return WaitForSingleObject(e, timeoutMs); }
uint32_t EventWaitAny(uint32_t count, const EventHandle* events, uint32_t timeoutMs) {
    return WaitForMultipleObjects(count, reinterpret_cast<const HANDLE*>(events),  // CAST-OK: win32
                                  FALSE, timeoutMs);
}
bool EventClose(EventHandle e) { return CloseHandle(e) != FALSE; }

}  // namespace Platform
