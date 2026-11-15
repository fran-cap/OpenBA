// ============================================================================
// src/platform/darwin/sync_darwin.cpp -- Platform:: locks and events over pthreads + std
// ============================================================================
// The POSIX half of src/platform/common/platform_sync.h (Phase D.4 THREADS E.9;
// D4_RULES C7: C++17 std + pthreads, never SDL).
//
// LOCKS: a RECURSIVE pthread_mutex_t. macOS's pthread_mutex_t is 64 bytes and does not
// fit the 0x28 bytes of LockStorage, so the slot holds a pointer to a mutex MutexInit
// allocates with new and MutexDestroy deletes. The spin count has no pthread equivalent
// and is ignored (a CRITICAL_SECTION's spin is a contention tuning, not a semantic).
//
// ONCE: a three-state word in the OnceFlag's pointer slot (null = not run), the
// losers yielding until the winner has returned: InitOnceExecuteOnce's contract.
//
// EVENTS forward to src/platform/common/event_std.cpp (host-tested on Windows too).
// ============================================================================

#include "platform_sync.h"
#include "event_std.h"

#include <pthread.h>
#include <sched.h>

#include <new>


namespace Platform {

namespace {

pthread_mutex_t* Mx(LockStorage* lock) {
    return *std::launder(reinterpret_cast<pthread_mutex_t**>(lock->bytes));
}

// The OnceFlag states besides null: the addresses of these two bytes.
char g_onceRunning;
char g_onceDone;

}  // namespace

void MutexInit(LockStorage* lock, uint32_t /*spinCount*/) {
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);   // CRITICAL_SECTION is recursive
    // Darwin's mutex is 64 bytes; libc new avoids recursion into the game allocator.
    new (lock->bytes) pthread_mutex_t*(new pthread_mutex_t);
    pthread_mutex_init(Mx(lock), &attr);
    pthread_mutexattr_destroy(&attr);
}
// The primitives the analysis takes on trust (platform_sync.h declares what they acquire / release).
void MutexLock(LockStorage* lock) NO_THREAD_SAFETY_ANALYSIS { pthread_mutex_lock(Mx(lock)); }
void MutexUnlock(LockStorage* lock) NO_THREAD_SAFETY_ANALYSIS { pthread_mutex_unlock(Mx(lock)); }
void MutexDestroy(LockStorage* lock) {
    pthread_mutex_destroy(Mx(lock));
    delete Mx(lock);
}

void OnceRun(OnceFlag* flag, void (*fn)()) {
    void* expected = nullptr;
    if (__atomic_compare_exchange_n(&flag->state, &expected, static_cast<void*>(&g_onceRunning),
                                    false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
        fn();
        __atomic_store_n(&flag->state, static_cast<void*>(&g_onceDone), __ATOMIC_RELEASE);
        return;
    }
    while (__atomic_load_n(&flag->state, __ATOMIC_ACQUIRE) != static_cast<void*>(&g_onceDone))
        sched_yield();
}

EventHandle EventCreate(bool manualReset, bool initiallySignaled) {
    return StdEvent::Create(manualReset, initiallySignaled);
}
bool EventSet(EventHandle e) { return StdEvent::Set(e); }
bool EventReset(EventHandle e) { return StdEvent::Reset(e); }
uint32_t EventWait(EventHandle e, uint32_t timeoutMs) { return StdEvent::Wait(e, timeoutMs); }
uint32_t EventWaitAny(uint32_t count, const EventHandle* events, uint32_t timeoutMs) {
    return StdEvent::WaitAny(count, events, timeoutMs);
}
bool EventClose(EventHandle e) { return StdEvent::Close(e); }

}  // namespace Platform
