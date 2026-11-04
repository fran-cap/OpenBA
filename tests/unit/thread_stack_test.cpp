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
//   * the hang dump ends while another thread holds the loader lock for good (review F-297),
//     and ModuleAt names an image the way the loader does, without it.
// ============================================================================
#if defined(_WIN32)

#include "unit.h"

#include "win/thread_stack.h"

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

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

UNIT_TEST(thread_stack_capture_unwinds_a_parked_thread) {
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

// ----------------------------------------------------------------------------
// The hang dump against a loader lock held for good (review F-297): a main thread spinning
// inside DllMain owns LdrpLoaderLock and never lets go. Here a "main" thread takes it with
// ntdll's LdrLockLoaderLock and spins; another thread runs the hang dump's body
// (DumpOtherThreads) into a buffer. Naming the frames with GetModuleHandleExA /
// GetModuleFileNameA blocks on that lock, so the dump never ends; a dump not finished
// after 5 s counts as a deadlock, and the test then lets the spinner unlock to break it.
// ----------------------------------------------------------------------------
namespace {

typedef LONG(NTAPI* LdrLockLoaderLockFn)(ULONG flags, ULONG* state, ULONG_PTR* cookie);
typedef LONG(NTAPI* LdrUnlockLoaderLockFn)(ULONG flags, ULONG_PTR cookie);

struct LoaderHold {
    LdrLockLoaderLockFn lock = nullptr;
    LdrUnlockLoaderLockFn unlock = nullptr;
    HANDLE held = nullptr;      // set once the spinner owns the loader lock
    HANDLE go = nullptr;        // starts the dump
    HANDLE dumped = nullptr;    // set when DumpOtherThreads returned
    volatile LONG release = 0;  // the spinner unlocks and returns
    DWORD spinnerTid = 0;
    char out[64 * 1024];
    size_t outLen = 0;
};

__attribute__((noinline)) void SpinHoldingLoaderLock(LoaderHold* h) {
    while (!h->release) {
        YieldProcessor();
    }
    __asm__ volatile("" ::: "memory");
}

DWORD WINAPI LoaderLockSpinner(void* p) {
    LoaderHold* h = static_cast<LoaderHold*>(p);
    ULONG_PTR cookie = 0;
    if (h->lock(0, nullptr, &cookie) < 0) {
        SetEvent(h->held);
        return 1;
    }
    SetEvent(h->held);
    SpinHoldingLoaderLock(h);
    h->unlock(0, cookie);
    return 0;
}

void AppendLine(void* user, const char* line) {
    LoaderHold* h = static_cast<LoaderHold*>(user);
    const size_t n = std::strlen(line);
    if (h->outLen + n + 1 > sizeof(h->out)) return;
    std::memcpy(h->out + h->outLen, line, n);
    h->outLen += n;
    h->out[h->outLen] = '\0';
}

DWORD WINAPI HangDumper(void* p) {
    LoaderHold* h = static_cast<LoaderHold*>(p);
    WaitForSingleObject(h->go, INFINITE);
    ThreadStack::DumpOtherThreads(h->spinnerTid, &AppendLine, h);
    SetEvent(h->dumped);
    return 0;
}

}  // namespace

UNIT_TEST(thread_stack_hang_dump_completes_while_the_loader_lock_is_held) {
    static LoaderHold h;   // 64 KB: not on the test's stack
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    h.lock = reinterpret_cast<LdrLockLoaderLockFn>(  // CAST-OK: win32
        reinterpret_cast<void*>(GetProcAddress(ntdll, "LdrLockLoaderLock")));  // CAST-OK: win32
    h.unlock = reinterpret_cast<LdrUnlockLoaderLockFn>(  // CAST-OK: win32
        reinterpret_cast<void*>(GetProcAddress(ntdll, "LdrUnlockLoaderLock")));  // CAST-OK: win32
    CHECK(h.lock != nullptr && h.unlock != nullptr);
    if (!h.lock || !h.unlock) return;
    h.held = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    h.go = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    h.dumped = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    h.release = 0;
    h.outLen = 0;
    h.out[0] = '\0';

    // Both threads exist before the lock is taken: thread start and exit wait on it.
    HANDLE dumper = CreateThread(nullptr, 0, &HangDumper, &h, 0, nullptr);
    HANDLE spinner = CreateThread(nullptr, 0, &LoaderLockSpinner, &h, CREATE_SUSPENDED, &h.spinnerTid);
    CHECK(dumper != nullptr && spinner != nullptr);
    if (!dumper || !spinner) return;
    Sleep(100);   // the dumper has finished starting
    ResumeThread(spinner);
    WaitForSingleObject(h.held, INFINITE);
    Sleep(50);    // the spinner is in its loop

    SetEvent(h.go);
    const bool deadlock = WaitForSingleObject(h.dumped, 5000) == WAIT_TIMEOUT;
    InterlockedExchange(&h.release, 1);   // unlocks: a blocked dump then finishes
    WaitForSingleObject(h.dumped, 10000);
    WaitForSingleObject(spinner, 10000);
    WaitForSingleObject(dumper, 10000);
    CloseHandle(spinner);
    CloseHandle(dumper);
    CloseHandle(h.held);
    CloseHandle(h.go);
    CloseHandle(h.dumped);

    std::printf("  hang dump with the loader lock held: deadlock=%d, %zu bytes\n", (int)deadlock, h.outLen);
    CHECK(!deadlock);
    // The spinner is reported as the main thread, with frames named by module.
    const char* mainAt = std::strstr(h.out, "[MAIN THREAD]");
    CHECK(mainAt != nullptr);
    if (!mainAt) return;
    const char* next = std::strstr(mainAt, "\n--- thread ");
    const size_t sectionLen = next ? (size_t)(next - mainAt) : std::strlen(mainAt);
    const std::string section(mainAt, sectionLen);
    CHECK(section.find("RIP=") != std::string::npos);
    CHECK(section.find(".exe") != std::string::npos);   // SpinHoldingLoaderLock: the unit exe
    CHECK(section.find(" + 0x") != std::string::npos);
}

// ModuleAt names the image an address is in without the loader: the same base and file
// name the loader reports.
UNIT_TEST(thread_stack_module_at_names_the_image) {
    uint64_t base = 0;
    char name[MAX_PATH];
    const uint64_t here = reinterpret_cast<uintptr_t>(&SpinHoldingLoaderLock);  // CAST-OK: crash_walk
    CHECK(ThreadStack::ModuleAt(here, &base, name, sizeof(name)));
    CHECK_EQ(base, (uint64_t)reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)));  // CAST-OK: crash_walk
    char path[MAX_PATH] = { 0 };
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    const char* slash = std::strrchr(path, '\\');
    const char* expected = slash ? slash + 1 : path;
    std::printf("  ModuleAt: %s (loader: %s)\n", name, expected);
    CHECK(_stricmp(name, expected) == 0);
    // Heap memory is in no image.
    uint64_t heapBase = 0;
    char heapName[8];
    void* heap = std::malloc(64);
    CHECK(!ThreadStack::ModuleAt(reinterpret_cast<uintptr_t>(heap), &heapBase, heapName,  // CAST-OK: crash_walk
                                 sizeof(heapName)));
    std::free(heap);
}

#endif  // _WIN32
