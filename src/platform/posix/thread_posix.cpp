// ============================================================================
// src/platform/posix/thread_posix.cpp -- Platform:: threads over pthreads
// ============================================================================
// The POSIX half of src/platform/common/platform_thread.h (Phase D.4 THREADS E.9).
//
// A ThreadHandle is a heap ThreadObject (plain `new`, never MemoryAllocator memory).
// _beginthreadex's CREATE_SUSPENDED is a start latch: the pthread is created at once
// and blocks in its trampoline until ThreadResume opens the latch, so System::Thread
// can create its three events after the thread exists, as retail does.
//
// STACK: max(stackSize, 2 MB). Windows treats _beginthreadex's argument as the
// initial COMMIT and reserves the PE default (2 MB with GNU ld), so every engine
// worker really runs on 2 MB; the literal 0x10000 would overflow the pathfinder
// (plans/THREADS.md F.3).
//
// LIFETIME: the object is shared by the handle and the running thread (a count of
// two). ThreadClose drops the handle's share and detaches an unjoined thread; the
// trampoline drops the thread's share when the entry returns; the last one frees it.
// A thread closed while still suspended never runs its entry (a suspended Win32
// thread whose handle is closed never runs either).
// ============================================================================

#include "platform_thread.h"

#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <time.h>
#include <cerrno>
#include <cstdio>
#include <cstdlib>

namespace Platform {

struct ThreadObject {
    pthread_t thread;
    pthread_mutex_t latchLock;
    pthread_cond_t latchCond;
    bool resumed;
    bool abandoned;      // closed before it was resumed: the entry never runs
    bool joined;
    int refs;            // the handle's share + the running thread's share
    ThreadEntry entry;
    void* arg;
};

namespace {

constexpr uint32_t kMinStack = 2u * 1024u * 1024u;   // the PE default reserve (GNU ld)

void Release(ThreadObject* t) {
    if (__atomic_sub_fetch(&t->refs, 1, __ATOMIC_ACQ_REL) == 0) {
        pthread_cond_destroy(&t->latchCond);
        pthread_mutex_destroy(&t->latchLock);
        delete t;
    }
}

// An alternate signal stack for this thread, as the crash handler installs for the main
// thread (src/diag/posix/crash_handler_posix.cpp): a new pthread starts without one, and
// with none a stack overflow's SIGSEGV cannot be delivered to the SA_ONSTACK handler, so the
// process dies with no standalone_crash.log (review F-277). A thread that already has one
// (bionic gives every thread its own) keeps it. Returns the stack to free, or null.
constexpr size_t kAltStackSize = 64u * 1024u;

void* InstallAltStack() {
    stack_t cur{};
    if (sigaltstack(nullptr, &cur) != 0 || (cur.ss_flags & SS_DISABLE) == 0) return nullptr;
    void* mem = std::malloc(kAltStackSize);
    if (mem == nullptr) return nullptr;
    stack_t ss{};
    ss.ss_sp = mem;
    ss.ss_size = kAltStackSize;
    if (sigaltstack(&ss, nullptr) != 0) {
        std::free(mem);
        return nullptr;
    }
    return mem;
}

void RemoveAltStack(void* mem) {
    if (mem == nullptr) return;
    stack_t off{};
    off.ss_flags = SS_DISABLE;
    sigaltstack(&off, nullptr);
    std::free(mem);
}

void* Trampoline(void* p) {
    ThreadObject* t = static_cast<ThreadObject*>(p);  // CAST-OK: user_data
    pthread_mutex_lock(&t->latchLock);
    while (!t->resumed) pthread_cond_wait(&t->latchCond, &t->latchLock);
    const bool run = !t->abandoned;
    pthread_mutex_unlock(&t->latchLock);
    if (run) {
        void* altStack = InstallAltStack();
        t->entry(t->arg);
        RemoveAltStack(altStack);
    }
    Release(t);
    return nullptr;
}

// The process-wide sequential thread id: 1, 2, 3 ... (never 0, never 0xFFFFFFFF,
// which Video::Device+0x88 reserves for "no owner").
uint32_t g_nextThreadId = 1;
thread_local uint32_t t_threadId = 0;

}  // namespace

ThreadHandle ThreadCreateSuspended(ThreadEntry entry, void* arg, uint32_t stackSize) {
    ThreadObject* t = new ThreadObject();
    pthread_mutex_init(&t->latchLock, nullptr);
    pthread_cond_init(&t->latchCond, nullptr);
    t->resumed = false;
    t->abandoned = false;
    t->joined = false;
    t->refs = 2;
    t->entry = entry;
    t->arg = arg;

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, stackSize > kMinStack ? stackSize : kMinStack);
    const int rc = pthread_create(&t->thread, &attr, &Trampoline, t);
    pthread_attr_destroy(&attr);
    if (rc != 0) {
        std::fprintf(stderr, "[Platform] pthread_create failed (%d)\n", rc);
        pthread_cond_destroy(&t->latchCond);
        pthread_mutex_destroy(&t->latchLock);
        delete t;
        return nullptr;   // _beginthreadex's failure value
    }
    return t;
}

// SetThreadPriority(NORMAL): a new pthread already runs at the default policy and
// priority, so there is nothing to change.
bool ThreadSetNormalPriority(ThreadHandle) { return true; }
// NOT RETAIL (CPULIMIT): a no-op (platform_thread.h says why); the path workers run at the
// default priority on Linux.
bool ThreadSetBelowNormalPriority(ThreadHandle) { return false; }
bool ThreadBackgroundMode(bool) { return false; }   // NOT RETAIL: platform_thread.h

// NOT RETAIL (review F-132): a null handle (a failed pthread_create) fails these three
// calls the way Win32 fails them on a null HANDLE, instead of dereferencing it.
// System::Thread keeps retail's unchecked sequence (thread.cpp InitFields /
// ReleaseFields), which on Windows survives a failed _beginthreadex: ResumeThread
// returns (DWORD)-1, WaitForSingleObject WAIT_FAILED and CloseHandle FALSE.
uint32_t ThreadResume(ThreadHandle t) {
    if (t == nullptr) return 0xFFFFFFFFu;   // ResumeThread's failure value
    pthread_mutex_lock(&t->latchLock);
    const uint32_t previous = t->resumed ? 0u : 1u;   // ResumeThread: the previous suspend count
    t->resumed = true;
    pthread_cond_broadcast(&t->latchCond);
    pthread_mutex_unlock(&t->latchLock);
    return previous;
}

uint32_t ThreadJoin(ThreadHandle t) {
    if (t == nullptr) return 0xFFFFFFFFu;   // WAIT_FAILED
    if (!t->joined) {
        pthread_join(t->thread, nullptr);
        t->joined = true;
    }
    return 0;   // WAIT_OBJECT_0
}

bool ThreadClose(ThreadHandle t) {
    if (t == nullptr) return false;         // CloseHandle(NULL) fails
    pthread_mutex_lock(&t->latchLock);
    if (!t->resumed) {
        t->abandoned = true;
        t->resumed = true;
        pthread_cond_broadcast(&t->latchCond);
    }
    pthread_mutex_unlock(&t->latchLock);
    if (!t->joined) pthread_detach(t->thread);
    Release(t);
    return true;
}

bool YieldThread() { return sched_yield() == 0; }

void SleepMs(uint32_t ms) {
    struct timespec req;
    req.tv_sec = static_cast<time_t>(ms / 1000u);
    req.tv_nsec = static_cast<decltype(req.tv_nsec)>((ms % 1000u) * 1000000u);
    while (nanosleep(&req, &req) != 0 && errno == EINTR) {
    }
}

uint32_t CurrentThreadId() {
    if (t_threadId == 0) t_threadId = __atomic_fetch_add(&g_nextThreadId, 1u, __ATOMIC_RELAXED);
    return t_threadId;
}

bool DispatchSentMessages() { return false; }   // NOT RETAIL: platform_thread.h (review F-224)

}  // namespace Platform
