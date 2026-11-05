// ============================================================================
// tests/unit/hw_watch_arm_test.cpp -- HwWatch_Arm's helper-thread request (review F-280, F-286)
// ============================================================================
// Windows only (src/diag/win/hw_watch.cpp; the POSIX twin arms nothing): EMPTY elsewhere.
// HwWatch_Arm hands the slot / address / length to a helper thread (a thread cannot set its
// own debug registers) and waits for it at most 2 s. A helper that has not started by then
// still reads the request later, so the request must outlive HwWatch_Arm's frame.
// Here the loader lock is held for 3 s, which holds the new helper thread in its
// DLL_THREAD_ATTACH, so the 2 s wait times out. The caller then overwrites the stack where
// HwWatch_Arm's frame was and spins without calls; once the lock is released the helper
// arms the watchpoint, and a poller thread reads the debug registers back: DR0 must hold
// the watched address (on origin/main it held what the scribble left there: 0).
// ============================================================================
#if defined(_WIN32)

#include "unit.h"

#include "hw_watch.h"

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

typedef LONG(NTAPI* LdrLockFn)(ULONG flags, ULONG* state, ULONG_PTR* cookie);
typedef LONG(NTAPI* LdrUnlockFn)(ULONG flags, ULONG_PTR cookie);

volatile uint32_t g_watched = 0;     // never written: the watchpoint must not fire
volatile LONG g_locked = 0;
volatile LONG g_done = 0;
DWORD64 g_seenDr0 = 0;
DWORD64 g_seenDr7 = 0;
HANDLE g_testThread = nullptr;

DWORD WINAPI LockerThread(void*) {
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    auto lock = reinterpret_cast<LdrLockFn>(GetProcAddress(ntdll, "LdrLockLoaderLock"));  // CAST-OK: win32
    auto unlock = reinterpret_cast<LdrUnlockFn>(GetProcAddress(ntdll, "LdrUnlockLoaderLock"));  // CAST-OK: win32
    if (!lock || !unlock) {
        InterlockedExchange(&g_locked, -1);
        return 1;
    }
    ULONG_PTR cookie = 0;
    lock(0, nullptr, &cookie);
    InterlockedExchange(&g_locked, 1);
    Sleep(3000);
    unlock(0, cookie);
    return 0;
}

// Reads the test thread's debug registers until DR7 enables slot 0 (or 8 s pass), records
// them, disarms the thread again and releases the spinning test thread.
DWORD WINAPI PollerThread(void*) {
    const DWORD start = GetTickCount();
    CONTEXT ctx;
    for (;;) {
        std::memset(&ctx, 0, sizeof(ctx));
        ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
        SuspendThread(g_testThread);
        const BOOL ok = GetThreadContext(g_testThread, &ctx);
        if (ok && (ctx.Dr7 & 1u) != 0) {
            g_seenDr0 = ctx.Dr0;
            g_seenDr7 = ctx.Dr7;
            ctx.Dr0 = 0;
            ctx.Dr7 = 0;
            ctx.Dr6 = 0;
            SetThreadContext(g_testThread, &ctx);
            ResumeThread(g_testThread);
            break;
        }
        ResumeThread(g_testThread);
        if (GetTickCount() - start > 8000) break;
        Sleep(10);
    }
    InterlockedExchange(&g_done, 1);
    return 0;
}

// Overwrites the stack below the caller's frame, where HwWatch_Arm's frame (and its request,
// on origin/main) lived.
__attribute__((noinline)) void ScribbleStack() {
    volatile uint8_t junk[8192];
    for (size_t i = 0; i < sizeof(junk); ++i) junk[i] = 0;
}

}  // namespace

UNIT_TEST(hw_watch_arm_request_outlives_a_timed_out_wait) {
    HwWatch_Init();
    g_testThread = static_cast<HANDLE>(HwWatch_MainThreadHandle());
    CHECK(g_testThread != nullptr);
    if (!g_testThread) return;
    // Both helpers exist before the loader lock is taken (a thread starting under it waits).
    HANDLE poller = CreateThread(nullptr, 0, &PollerThread, nullptr, CREATE_SUSPENDED, nullptr);
    HANDLE locker = CreateThread(nullptr, 0, &LockerThread, nullptr, 0, nullptr);
    CHECK(poller != nullptr && locker != nullptr);
    if (!poller || !locker) return;
    while (g_locked == 0) Sleep(1);
    CHECK(g_locked == 1);
    if (g_locked != 1) return;
    ResumeThread(poller);
    const DWORD t0 = GetTickCount();
    const int slot = HwWatch_Arm(const_cast<uint32_t*>(&g_watched), 4, "hw_watch_arm_test");
    const DWORD waited = GetTickCount() - t0;
    ScribbleStack();
    while (g_done == 0) {
    }
    WaitForSingleObject(poller, 10000);
    WaitForSingleObject(locker, 10000);
    CloseHandle(poller);
    CloseHandle(locker);
    std::printf("  HwWatch_Arm returned slot %d after %lu ms; DR0=%#llx DR7=%#llx (watched %p)\n", slot,
                (unsigned long)waited, (unsigned long long)g_seenDr0, (unsigned long long)g_seenDr7,
                static_cast<const void*>(const_cast<uint32_t*>(&g_watched)));
    CHECK_EQ(slot, 0);
    CHECK(waited >= 1900);   // the wait did time out: the helper ran after HwWatch_Arm returned
    CHECK_EQ(g_seenDr0, static_cast<DWORD64>(reinterpret_cast<uintptr_t>(&g_watched)));  // CAST-OK: crash_walk
    // DR7: L0 enabled, RW0 = 01 (write), LEN0 = 11 (4 bytes).
    CHECK_EQ(g_seenDr7 & 0xF0001u, static_cast<DWORD64>(0xD0001u));
}

#endif  // _WIN32
