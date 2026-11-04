// ============================================================================
// tests/unit/thread_stack_test.cpp -- ThreadStack::Capture, the suspended-thread stack of the
// hang dump, the redzone thread dump and the sampler (review F-275, F-288, F-291, F-297)
// ============================================================================
// Windows only (src/diag/win/thread_stack.cpp): EMPTY elsewhere.
//   * no deadlock and no fault: a target thread loops LoadLibrary / FreeLibrary while
//     another thread captures it again and again. Walking with RtlLookupFunctionEntry while
//     the target is suspended (the shape of origin/main's three callers) blocks on the
//     loader lock the target holds within a second; a capture that has not returned after
//     2 s counts as a deadlock, and the test then resumes the target to break it. (Walking
//     after the resume instead faults: the target unmaps dbghelp.dll under the unwinder.)
//   * the walk is right: a target parked two known calls deep reports both return
//     addresses, in order, with RSPs on its stack.
// ============================================================================
#if defined(_WIN32)

#include "unit.h"

#include "win/thread_stack.h"

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

constexpr int kMaxFrames = 32;

struct Churn {
    HANDLE target = nullptr;
    volatile LONG stop = 0;
    volatile LONG inCapture = 0;
    volatile LONG64 captures = 0;
};

DWORD WINAPI LoaderChurnThread(void* p) {
    Churn* s = static_cast<Churn*>(p);
    while (!s->stop) {
        HMODULE m = LoadLibraryW(L"dbghelp.dll");
        if (m) FreeLibrary(m);
    }
    return 0;
}

DWORD WINAPI CaptureLoopThread(void* p) {
    Churn* s = static_cast<Churn*>(p);
    ThreadStack::Frame frames[kMaxFrames];
    while (!s->stop) {
        CONTEXT ctx;
        std::memset(&ctx, 0, sizeof(ctx));
        ctx.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
        InterlockedExchange(&s->inCapture, 1);
        const ThreadStack::Result r = ThreadStack::Capture(s->target, ctx, frames, kMaxFrames);
        InterlockedExchange(&s->inCapture, 0);
        if (r.status == ThreadStack::Status::kSuspendFailed) break;
        InterlockedIncrement64(&s->captures);
    }
    return 0;
}

// Parked target: Outer -> Inner -> WaitForSingleObject. Each records its return address.
struct Parked {
    HANDLE ready = nullptr;
    HANDLE release = nullptr;
    void* innerReturn = nullptr;   // an address in Outer
    void* outerReturn = nullptr;   // an address in ParkedThread
};

__attribute__((noinline)) void Inner(Parked* p) {
    p->innerReturn = __builtin_return_address(0);
    SetEvent(p->ready);
    WaitForSingleObject(p->release, INFINITE);
    __asm__ volatile("" ::: "memory");
}

__attribute__((noinline)) void Outer(Parked* p) {
    p->outerReturn = __builtin_return_address(0);
    volatile char pad[512];
    pad[0] = 1;
    Inner(p);
    __asm__ volatile("" ::: "memory");
    (void)pad[0];
}

DWORD WINAPI ParkedThread(void* p) {
    Outer(static_cast<Parked*>(p));
    return 0;
}

}  // namespace

UNIT_TEST(thread_stack_capture_does_not_deadlock_on_the_loader) {
    Churn s;
    s.target = CreateThread(nullptr, 0, &LoaderChurnThread, &s, 0, nullptr);
    HANDLE sampler = CreateThread(nullptr, 0, &CaptureLoopThread, &s, 0, nullptr);
    CHECK(s.target != nullptr && sampler != nullptr);
    if (!s.target || !sampler) return;
    bool deadlock = false;
    const DWORD start = GetTickCount();
    LONG64 last = -1;
    DWORD lastMove = GetTickCount();
    while (GetTickCount() - start < 6000) {
        Sleep(20);
        const LONG64 n = s.captures;
        if (n != last) {
            last = n;
            lastMove = GetTickCount();
        } else if (s.inCapture && GetTickCount() - lastMove > 2000) {
            deadlock = true;
            break;
        }
    }
    InterlockedExchange(&s.stop, 1);
    if (deadlock) ResumeThread(s.target);   // release the lock the capture waits on
    WaitForSingleObject(sampler, 5000);
    while (ResumeThread(s.target) > 1) {
    }
    WaitForSingleObject(s.target, 5000);
    CloseHandle(sampler);
    CloseHandle(s.target);
    std::printf("  %lld captures of a thread in LoadLibrary / FreeLibrary, deadlock=%d\n", (long long)s.captures,
                (int)deadlock);
    CHECK(!deadlock);
    CHECK(s.captures > 100);
}

UNIT_TEST(thread_stack_capture_unwinds_the_copied_stack) {
    Parked p;
    p.ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    p.release = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE th = CreateThread(nullptr, 0, &ParkedThread, &p, 0, nullptr);
    CHECK(th != nullptr);
    if (!th) return;
    WaitForSingleObject(p.ready, INFINITE);
    Sleep(50);   // let it reach the wait

    ThreadStack::Frame frames[kMaxFrames];
    CONTEXT ctx;
    std::memset(&ctx, 0, sizeof(ctx));
    ctx.ContextFlags = CONTEXT_FULL;
    const ThreadStack::Result r = ThreadStack::Capture(th, ctx, frames, kMaxFrames);
    CHECK(r.status == ThreadStack::Status::kOk);
    CHECK(r.frames >= 3);
    int innerAt = -1;
    int outerAt = -1;
    for (int i = 0; i < r.frames; ++i) {
        if (frames[i].rip == reinterpret_cast<uintptr_t>(p.innerReturn)) innerAt = i;  // CAST-OK: crash_walk
        if (frames[i].rip == reinterpret_cast<uintptr_t>(p.outerReturn)) outerAt = i;  // CAST-OK: crash_walk
    }
    std::printf("  %d frames; return into Outer at #%d, into ParkedThread at #%d\n", r.frames, innerAt, outerAt);
    CHECK(innerAt > 0);
    CHECK(outerAt == innerAt + 1);
    // Every RSP is an address of the target's stack, above the stopped RSP.
    for (int i = 0; i < r.frames; ++i) {
        CHECK(frames[i].rsp >= ctx.Rsp);
        CHECK(frames[i].rsp < ctx.Rsp + 1024 * 1024);
    }
    CHECK_EQ(frames[0].rip, (uint64_t)ctx.Rip);

    SetEvent(p.release);
    WaitForSingleObject(th, 5000);
    CloseHandle(th);
    CloseHandle(p.ready);
    CloseHandle(p.release);
}

#endif  // _WIN32
