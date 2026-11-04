// src/diag/win/hw_watch.cpp — hardware data watchpoints on the MAIN (sim) thread.
//
// Diagnostic only. HwWatch_Arm(addr, len) programs one of the four x64 debug registers
// (DR0..DR3, rotating) as a WRITE watchpoint on the main thread; the vectored handler in
// main.cpp forwards EXCEPTION_SINGLE_STEP here, and HwWatch_OnSingleStep prints the
// writer's RIP + stack to stderr and the crash log, then lets execution continue.
//
// Why: a heap walker can only say a tag WAS clobbered; a data breakpoint on the exact
// out-of-bounds slot names the instruction that did it. Used 2026-09-04 for the 800x10
// "int array full of 2s spills +1 into the next block's tag" corruption.
//
// A thread cannot set its own debug registers, so Arm() runs the register update on a
// helper thread that suspends the main thread, edits its CONTEXT, and resumes it.
#include "hw_watch.h"
#include <windows.h>
#include <cstdio>
#include <cstdint>
#include <cstring>

namespace {
DWORD  g_mainTid = 0;
HANDLE g_mainThread = nullptr;
struct Slot { uintptr_t addr; int len; int hits; };
Slot g_slots[4] = {};
int  g_next = 0;
int  g_armed = 0;
CRITICAL_SECTION g_cs;
bool g_csInit = false;

struct ArmReq { int slot; uintptr_t addr; int len; };

// The thread owns the request (HwWatch_Arm may have stopped waiting for it) and frees it.
DWORD WINAPI ArmThread(LPVOID p) {
    ArmReq* r = reinterpret_cast<ArmReq*>(p);  // CAST-OK: win32
    if (SuspendThread(g_mainThread) == (DWORD)-1) {
        delete r;
        return 1;
    }
    CONTEXT ctx; memset(&ctx, 0, sizeof(ctx));
    ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
    if (GetThreadContext(g_mainThread, &ctx)) {
        DWORD64* dr[4] = { &ctx.Dr0, &ctx.Dr1, &ctx.Dr2, &ctx.Dr3 };
        *dr[r->slot] = r->addr;
        // DR7: L<n> enable bit (bit 2n), RW<n> = 01 (write) at bits 16+4n, LEN<n> at bits 18+4n
        // (00=1 byte, 01=2, 11=4, 10=8).
        const int lenBits = r->len == 1 ? 0 : r->len == 2 ? 1 : r->len == 8 ? 2 : 3;
        DWORD64 dr7 = ctx.Dr7;
        dr7 &= ~(0xFull << (16 + 4 * r->slot));
        dr7 |= (DWORD64)(0x1 | (lenBits << 2)) << (16 + 4 * r->slot);
        dr7 |= (DWORD64)1 << (2 * r->slot);
        ctx.Dr7 = dr7;
        ctx.Dr6 = 0;
        SetThreadContext(g_mainThread, &ctx);
    }
    ResumeThread(g_mainThread);
    delete r;
    return 0;
}
}  // namespace

void* HwWatch_MainThreadHandle() { return g_mainThread; }

extern "C" void HwWatch_Init() {
    if (g_mainTid) return;
    g_mainTid = GetCurrentThreadId();
    g_mainThread = OpenThread(THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_SUSPEND_RESUME |
                              THREAD_QUERY_INFORMATION, FALSE, g_mainTid);
    InitializeCriticalSection(&g_cs); g_csInit = true;
}

extern "C" int HwWatch_Arm(void* addr, int len, const char* why) {
    if (!g_mainThread || GetCurrentThreadId() != g_mainTid) return -1;
    EnterCriticalSection(&g_cs);
    const int slot = g_next; g_next = (g_next + 1) & 3;
    g_slots[slot].addr = reinterpret_cast<uintptr_t>(addr);  // CAST-OK: crash_walk
    g_slots[slot].len = len; g_slots[slot].hits = 0;
    ++g_armed;
    LeaveCriticalSection(&g_cs);
    // On the heap, owned by the helper: the wait below gives up after 2 s (an INFINITE wait
    // could deadlock on a teardown path holding the loader lock, which a starting thread
    // needs), and a helper that starts later still reads the request.
    ArmReq* req = new ArmReq{ slot, reinterpret_cast<uintptr_t>(addr), len };  // CAST-OK: crash_walk
    HANDLE th = CreateThread(nullptr, 0, &ArmThread, req, 0, nullptr);
    if (!th) {
        delete req;
        return -1;
    }
    WaitForSingleObject(th, 2000);
    CloseHandle(th);
    std::fprintf(stderr, "[HWWATCH] armed DR%d on %p len=%d (%s)\n", slot, addr, len, why ? why : "");
    std::fflush(stderr);
    return slot;
}

extern "C" int HwWatch_Armed() { return g_armed; }

// Called from the vectored handler for EXCEPTION_SINGLE_STEP. Returns 1 when the
// exception was one of our watchpoints (caller should continue execution).
extern "C" int HwWatch_OnSingleStep(void* epV, void (*logStack)(void* ctx)) {
    EXCEPTION_POINTERS* ep = reinterpret_cast<EXCEPTION_POINTERS*>(epV);  // CAST-OK: crash_walk
    CONTEXT* ctx = ep->ContextRecord;
    const DWORD64 dr6 = ctx->Dr6;
    int slot = -1;
    for (int i = 0; i < 4; ++i) if (dr6 & (1ull << i)) { slot = i; break; }
    if (slot < 0) return 0;
    ctx->Dr6 = 0;
    Slot& s = g_slots[slot];
    ++s.hits;
    HMODULE mod = nullptr;
    const void* rip = reinterpret_cast<const void*>(ctx->Rip);  // CAST-OK: crash_walk
    char where[300];
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)rip, &mod) && mod) {  // CAST-OK: crash_walk
        char path[MAX_PATH] = {0};
        GetModuleFileNameA(mod, path, MAX_PATH);
        const char* nm = strrchr(path, '\\');
        std::snprintf(where, sizeof(where), "%s+0x%llX", nm ? nm + 1 : path,
                      (unsigned long long)((uintptr_t)rip - (uintptr_t)mod));  // CAST-OK: crash_walk
    } else std::snprintf(where, sizeof(where), "%p", rip);
    uint64_t now = 0;
    if (s.len == 4) now = *reinterpret_cast<uint32_t*>(s.addr);  // CAST-OK: crash_walk
    else if (s.len == 8) now = *reinterpret_cast<uint64_t*>(s.addr);  // CAST-OK: crash_walk
    else now = *reinterpret_cast<uint8_t*>(s.addr);  // CAST-OK: crash_walk
    std::fprintf(stderr, "[HWWATCH] HIT DR%d addr=%p (hit #%d) value now=0x%llx  writer RIP=%s  "
                         "RAX=%llx RCX=%llx RDX=%llx R8=%llx R9=%llx\n",
                 slot, (void*)s.addr, s.hits, (unsigned long long)now, where,
                 (unsigned long long)ctx->Rax, (unsigned long long)ctx->Rcx,
                 (unsigned long long)ctx->Rdx, (unsigned long long)ctx->R8,
                 (unsigned long long)ctx->R9);
    std::fflush(stderr);
    if (logStack && s.hits <= 3) logStack(ctx);
    return 1;
}
