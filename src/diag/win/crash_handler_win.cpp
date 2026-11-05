// ============================================================================
// src/diag/win/crash_handler_win.cpp -- the exe's crash handler (Windows)
// ============================================================================
// Moved out of src/app/main.cpp in Phase D.4 THREADS E.8 (D4_RULES C10): the
// second-chance filter (standalone_crash.log + exit 3), the pure-virtual and
// std::terminate handlers (exits 4 and 5), the first-chance fail-fast / hardware
// watchpoint VEH, the crash log itself, and StandaloneWalkStack -- the exe's one
// stack walker, which the hang dump in boot_state_watchdog.cpp also calls. The
// bodies are unchanged; main.cpp's Init() / WinMain call the three entry points
// in src/diag/crash_handler.h at the points where the installs stood.
// ============================================================================

#include "crash_handler.h"
#include "boot_state_watchdog.h"      // StackLineSink, StandaloneWalkStack's declaration
#include "hw_watch.h"                 // HwWatch_Armed / HwWatch_OnSingleStep
#include "redzone_alloc.h"            // RedzoneAlloc_SetDumpSink / RedzoneAlloc_HeapDump
#include "thread_stack.h"             // ThreadStack::FormatFrame: the frame line the hang dump shares
#include "system/MemoryAllocator.h"   // System::MemoryAllocator::_singleton (the crash dump's cell)

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <io.h>      // _get_osfhandle, _fileno
#include <windows.h>

// ============================================================
// Crash handler (simplified VEH)
// ============================================================
static const char* ExceptionCodeToString(DWORD code) {
    switch (code) {
    case EXCEPTION_ACCESS_VIOLATION:    return "ACCESS_VIOLATION";
    case EXCEPTION_ILLEGAL_INSTRUCTION: return "ILLEGAL_INSTRUCTION";
    case EXCEPTION_STACK_OVERFLOW:      return "STACK_OVERFLOW";
    case EXCEPTION_INT_DIVIDE_BY_ZERO:  return "INT_DIVIDE_BY_ZERO";
    default:                            return "UNKNOWN";
    }
}

// Forward decl — declared above the crash log helpers but defined below.
static void CrashLogf(const char* fmt, ...);

// Walk the saved CONTEXT via the x64 unwind tables and emit one line per
// frame: <module>!<offset>. No symbol names — Banished ships stripped — but
// module+offset is what we'd paste into Ghidra anyway.
//
// The crash handler's walker: it unwinds the faulting thread's own CONTEXT in place, on a
// stack nobody else is changing. Another thread's stack -- the hang dump in
// boot_state_watchdog.cpp, the redzone watchdog's dump, the sampler -- goes through
// ThreadStack::Capture (thread_stack.h) instead: the module lookups and
// RtlLookupFunctionEntry wait on loader locks a SUSPENDED thread can hold, so that thread is
// walked without them and symbolized once resumed. Both print ThreadStack::FormatFrame's
// frame line. The walker takes a line sink instead of writing to the crash log directly, and the
// CONTEXT is copied (RtlVirtualUnwind mutates it).
extern "C" void StandaloneWalkStack(CONTEXT* origCtx, StackLineSink sink, void* user) {
    CONTEXT ctx = *origCtx;
    char line[256];

    for (int frame = 0; frame < 32; frame++) {
        ThreadStack::FormatFrame(line, sizeof(line), frame, ctx.Rip, ctx.Rsp);
        sink(user, line);

        DWORD64 imageBase = 0;
        PRUNTIME_FUNCTION fn = RtlLookupFunctionEntry(ctx.Rip, &imageBase, nullptr);
        if (!fn) {
            // Leaf or no unwind data — try one manual pop of the return address.
            // Many leaves have no .pdata entry; the return value lives at [RSP].
            uintptr_t* sp = reinterpret_cast<uintptr_t*>(ctx.Rsp);  // CAST-OK: crash_walk
            if (IsBadReadPtr(sp, sizeof(uintptr_t))) break;
            ctx.Rip = *sp;
            ctx.Rsp += 8;
            if (!ctx.Rip) break;
            continue;
        }

        PVOID handlerData = nullptr;
        ULONG64 establisherFrame = 0;
        RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, ctx.Rip, fn,
                         &ctx, &handlerData, &establisherFrame, nullptr);

        if (!ctx.Rip) break;
    }
}

// The crash handler's view of the walker: every line straight to the
// write-through crash log.
static void CrashLogStackSink(void*, const char* s) { CrashLogf("%s", s); }

static void PrintStackTrace(CONTEXT* origCtx) {
    CrashLogf("Stack trace:\n");
    StandaloneWalkStack(origCtx, &CrashLogStackSink, nullptr);
}

// Dedicated crash log handle, opened with FILE_FLAG_WRITE_THROUGH so every
// WriteFile call commits synchronously to disk. We bypass the CRT entirely
// because fprintf-to-stderr proved racy under crashing-thread teardown:
// the kernel write was buffered and got cut mid-line by TerminateProcess.
static HANDLE g_crashLog = INVALID_HANDLE_VALUE;

static void OpenCrashLog() {
    if (g_crashLog != INVALID_HANDLE_VALUE) return;  // idempotent
    g_crashLog = CreateFileA(
        "standalone_crash.log",
        GENERIC_WRITE, FILE_SHARE_READ, nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH,
        nullptr);
    if (g_crashLog != INVALID_HANDLE_VALUE) return;

    // A previous crashed run can linger as a zombie that never releases its
    // handles (taskkill reports "no running instance" while tasklist still
    // lists it). It holds standalone_crash.log with FILE_SHARE_READ only, so
    // this GENERIC_WRITE open is denied -- and then EVERY CrashLogf silently
    // drops, which loses the fault address and stack for the run that matters.
    // Fall back to a pid-suffixed name so a dump is always written somewhere.
    char alt[64];
    wsprintfA(alt, "standalone_crash_%lu.log", GetCurrentProcessId());
    g_crashLog = CreateFileA(
        alt,
        GENERIC_WRITE, FILE_SHARE_READ, nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH,
        nullptr);
    if (g_crashLog != INVALID_HANDLE_VALUE) {
        fprintf(stderr, "[CrashLog] standalone_crash.log is locked (stale zombie "
                        "process?) -- this run's dump goes to %s\n", alt);
        fflush(stderr);
    }
}

// Raw WriteFile of a literal string — CRT-free, used as a progress marker so
// that even if formatted CrashLogf calls die later, we know how far we got.
static void CrashLogRaw(const char* msg) {
    if (g_crashLog == INVALID_HANDLE_VALUE) return;
    DWORD len = 0;
    while (msg[len]) ++len;
    DWORD wrote = 0;
    WriteFile(g_crashLog, msg, len, &wrote, nullptr);
}

// Synchronous write to the dedicated crash log. Each call commits to disk
// before returning thanks to FILE_FLAG_WRITE_THROUGH. No stderr mirroring —
// stderr writes go through the CRT's per-FILE lock which the main thread
// may be holding, deadlocking us inside the kernel callback.
static void CrashLogf(const char* fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n <= 0) return;
    if (n > (int)sizeof(buf)) n = sizeof(buf);
    if (g_crashLog != INVALID_HANDLE_VALUE) {
        DWORD wrote = 0;
        WriteFile(g_crashLog, buf, (DWORD)n, &wrote, nullptr);
    }
}

// Sink for RedzoneAlloc_HeapDump — writes each already-formatted line straight
// to the write-through crash log so the dump survives a re-entrant fault.
static void CrashLogSink(const char* s) {
    if (g_crashLog == INVALID_HANDLE_VALUE || !s) return;
    DWORD len = 0; while (s[len]) ++len;
    DWORD wrote = 0;
    WriteFile(g_crashLog, s, len, &wrote, nullptr);
}

// Force the OS-level file buffers behind stderr/stdout to disk. Used at
// the END of the crash handler to flush the convenience-mirror to stderr;
// the crash log itself is FILE_FLAG_WRITE_THROUGH so each CrashLogf is
// already on disk.
static void FlushLogsToDisk() {
    fflush(stderr);
    fflush(stdout);
    HANDLE he = (HANDLE)_get_osfhandle(_fileno(stderr));  // CAST-OK: win32
    if (he != INVALID_HANDLE_VALUE) FlushFileBuffers(he);
    HANDLE ho = (HANDLE)_get_osfhandle(_fileno(stdout));  // CAST-OK: win32
    if (ho != INVALID_HANDLE_VALUE) FlushFileBuffers(ho);
}

// Installed via SetUnhandledExceptionFilter — runs ONLY for second-chance
// exceptions that no SEH __try/__except in the call chain caught. This
// avoids logging benign first-chance exceptions raised and absorbed by
// Banished's normal control flow (lazy-commit reads, internal SEH probes).
static LONG WINAPI CrashHandler(EXCEPTION_POINTERS* ep) {
    DWORD code = ep->ExceptionRecord->ExceptionCode;
    // Filter out benign codes raised + absorbed by Banished's SEH chains
    // (e.g., FP-control-word probes). Everything else gets logged so we
    // catch C++ exceptions, breakpoints, and unusual fault codes during
    // teardown, not just classic AV/IllegalInst/StackOverflow.
    if (code == 0x40010005 ||           // DBG_CONTROL_C
        code == DBG_CONTROL_C ||
        code == DBG_PRINTEXCEPTION_C ||
        code == 0x4001000A ||           // DBG_PRINTEXCEPTION_WIDE_C
        code == 0x406D1388) {           // MS_VC_THREADNAME (debugger thread name)
        return EXCEPTION_CONTINUE_SEARCH;
    }

    // No CS — during late teardown the CRT may have torn down thread-safe
    // statics already, and we'd rather risk interleaved output than hang here.
    CrashLogRaw("\n=== STANDALONE CRASH ===\n");
    CrashLogf("Exception: 0x%08lX (%s)\n", code, ExceptionCodeToString(code));
    CrashLogf("Address:   %p\n", ep->ExceptionRecord->ExceptionAddress);

    CONTEXT* ctx = ep->ContextRecord;
    CrashLogf("RAX=%016llX RCX=%016llX RDX=%016llX RBX=%016llX\n",
              (unsigned long long)ctx->Rax, (unsigned long long)ctx->Rcx,
              (unsigned long long)ctx->Rdx, (unsigned long long)ctx->Rbx);
    CrashLogf("RBP=%016llX RSI=%016llX RDI=%016llX R8 =%016llX\n",
              (unsigned long long)ctx->Rbp, (unsigned long long)ctx->Rsi,
              (unsigned long long)ctx->Rdi, (unsigned long long)ctx->R8);
    CrashLogf("R9 =%016llX R10=%016llX R11=%016llX R12=%016llX\n",
              (unsigned long long)ctx->R9,  (unsigned long long)ctx->R10,
              (unsigned long long)ctx->R11, (unsigned long long)ctx->R12);
    CrashLogf("RIP=%016llX RSP=%016llX\n",
              (unsigned long long)ctx->Rip, (unsigned long long)ctx->Rsp);

    // Identify the module containing RIP — saves us doing arithmetic by hand.
    {
        HMODULE mod = nullptr;
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               (LPCSTR)(uintptr_t)ctx->Rip, &mod) && mod) {  // CAST-OK: crash_walk
            char path[MAX_PATH] = {0};
            GetModuleFileNameA(mod, path, MAX_PATH);
            uintptr_t off = (uintptr_t)ctx->Rip - (uintptr_t)mod;  // CAST-OK: crash_walk
            const char* nm = strrchr(path, '\\');
            CrashLogf("Module:    %s + 0x%llX  (base=%p)\n",
                      nm ? nm + 1 : path, (unsigned long long)off, (void*)mod);
        } else {
            CrashLogf("Module:    UNKNOWN (no module covers RIP)\n");
        }
    }

    if (code == EXCEPTION_ACCESS_VIOLATION && ep->ExceptionRecord->NumberParameters >= 2) {
        const char* op = ep->ExceptionRecord->ExceptionInformation[0] == 0 ? "READ" :
                         ep->ExceptionRecord->ExceptionInformation[0] == 1 ? "WRITE" : "EXEC";
        CrashLogf("Fault: %s of %p\n", op,
                  (void*)ep->ExceptionRecord->ExceptionInformation[1]);
    }

    // Specialized handler: EXEC-of-0 through a virtual call (RIP==0). The faulting
    // instruction was `CALL [vtable + N]` where that slot is NULL. RCX = the object
    // (this). Dump its vtable (module + offset identifies the type) + the first 13
    // slots so we can see WHICH slot is null. Used to diagnose the minimap
    // UpdateEvents crash (target->vtable[+0x40] == 0).
    if (code == EXCEPTION_ACCESS_VIOLATION &&
        ep->ExceptionRecord->ExceptionInformation[0] == 8 /*EXEC*/ &&
        ep->ExceptionRecord->ExceptionInformation[1] == 0 &&
        ctx->Rcx && !IsBadReadPtr((void*)ctx->Rcx, 8)) {  // CAST-OK: crash_walk
        void* target = (void*)ctx->Rcx;  // CAST-OK: crash_walk
        uintptr_t vt = *(uintptr_t*)target;  // CAST-OK: crash_walk
        CrashLogf("VCALL-of-0: this=%p vtable=%p\n", target, (void*)vt);
        HMODULE vmod = nullptr;
        if (vt && GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                     GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                     (LPCSTR)vt, &vmod) && vmod) {  // CAST-OK: crash_walk
            char path[MAX_PATH] = {0};
            GetModuleFileNameA(vmod, path, MAX_PATH);
            const char* nm = strrchr(path, '\\');
            CrashLogf("  vtable in %s + 0x%llX  (base=%p)\n",
                      nm ? nm + 1 : path, (unsigned long long)(vt - (uintptr_t)vmod),  // CAST-OK: crash_walk
                      (void*)vmod);
        } else {
            CrashLogf("  vtable in UNKNOWN module (heap? bad ptr?)\n");
        }
        // A raw dump of the table's first 13 words, in COMPILER slot order (a
        // class with `virtual ~T()` has two destructor words where retail has
        // one, so compiler slot N >= 1 is retail slot N - 1 there).
        if (vt && !IsBadReadPtr((void*)vt, 13 * 8)) {  // CAST-OK: crash_walk
            for (int i = 0; i < 13; i++)
                CrashLogf("  vtable compiler slot %2d [+0x%02X] = %p\n", i, i * 8,
                          *((void**)vt + i));  // CAST-OK: crash_walk
        }
        if (!IsBadReadPtr(target, 0x40)) {
            for (int i = 0; i < 8; i++)
                CrashLogf("  this[+0x%02X] = 0x%016llX\n", i * 8,
                          (unsigned long long)*((uint64_t*)target + i));  // CAST-OK: crash_walk
        }
    }

    // B34B_RULES 6.3 (open defect): the allocator "LockEnter on a NULL lock" crash is
    // Get() returning null, i.e. EnterCriticalSection(0x640). Two windows fit that stack
    // and only this value separates them: a NULL cell with a sane heap is W2 (Destruct
    // nulled it while another thread still allocates); a NULL cell with a corrupted free
    // list is W2' (a wild write landed on the .bss cell). Diagnostic only.
    {
        System::MemoryAllocator** const cell = &System::MemoryAllocator::_singleton;  // 0x1801fc718
        CrashLogf("MemoryAllocator::_singleton cell=%p value=%p\n",
                  static_cast<void*>(cell), static_cast<void*>(*cell));
    }

    // Free-list poisoning diagnosis: walk the engine allocator's boundary tags
    // and dump the corrupted block + its physical predecessor's vtable (= the
    // class whose buffer overflowed). Output is mirrored to the WRITE-THROUGH
    // crash log via the sink (stdout/debug.log output was lost — the dump faults
    // mid-walk in crash context before its unbuffered lines commit).
    // Works on a fully-native run (no redzone shim needed).
    CrashLogf("(heap-dump follows; also mirrored to standalone_debug.log)\n");
    RedzoneAlloc_SetDumpSink(&CrashLogSink);
    RedzoneAlloc_HeapDump("CrashHandler");
    RedzoneAlloc_SetDumpSink(nullptr);

    PrintStackTrace(ctx);

    CrashLogf("=== END CRASH ===\n");
    FlushLogsToDisk();

    // Avoid the OS unhandled-exception-filter race: terminate the process
    // ourselves on our own terms. ExitProcess skips CRT shutdown but still
    // runs DLL_PROCESS_DETACH; TerminateProcess is more aggressive but
    // can leave kernel state cluttered. _exit was problematic during late
    // teardown — go with ExitProcess.
    ExitProcess(3);
    return EXCEPTION_EXECUTE_HANDLER;
}

// ============================================================
// Pure-virtual call handler. libstdc++ exports __cxa_pure_virtual as a
// weak symbol that aborts via std::terminate; MSVC's CRT exports _purecall
// with similar behavior. Both bypass our SetUnhandledExceptionFilter so a
// pure-virtual call shows nothing in standalone_crash.log unless we
// override them here. Both variants converge on the same body.
// ============================================================
static void LogPureVirtualCall(const char* tag, void* retAddr) {
    if (g_crashLog == INVALID_HANDLE_VALUE) OpenCrashLog();
    CrashLogRaw("\n=== STANDALONE PURE-VIRTUAL CALL ===\n");
    CrashLogf("Tag:            %s\n", tag);
    CrashLogf("Return address: %p\n", retAddr);

    HMODULE mod = nullptr;
    if (retAddr && GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                       GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                       (LPCSTR)retAddr, &mod) && mod) {  // CAST-OK: crash_walk
        char path[MAX_PATH] = {0};
        GetModuleFileNameA(mod, path, MAX_PATH);
        const char* nm = strrchr(path, '\\');
        uintptr_t off = (uintptr_t)retAddr - (uintptr_t)mod;  // CAST-OK: crash_walk
        CrashLogf("Module:         %s + 0x%llX  (base=%p)\n",
                  nm ? nm + 1 : path, (unsigned long long)off, (void*)mod);
    }

    CONTEXT ctx;
    RtlCaptureContext(&ctx);
    PrintStackTrace(&ctx);

    CrashLogf("=== END PURE-VIRTUAL ===\n");
    FlushLogsToDisk();
    ExitProcess(4);
}

extern "C" void __cxa_pure_virtual() {
    LogPureVirtualCall("__cxa_pure_virtual", __builtin_return_address(0));
}

extern "C" void _purecall() {
    LogPureVirtualCall("_purecall", __builtin_return_address(0));
}

static void TerminateHandler() {
    if (g_crashLog == INVALID_HANDLE_VALUE) OpenCrashLog();
    CrashLogRaw("\n=== STANDALONE std::terminate() ===\n");
    CONTEXT ctx;
    RtlCaptureContext(&ctx);
    PrintStackTrace(&ctx);
    CrashLogf("=== END terminate ===\n");
    FlushLogsToDisk();
    ExitProcess(5);
}

// First-chance vectored handler that logs ONLY the fatal fail-fast /stack-cookie
// code (0xC0000409, STATUS_STACK_BUFFER_OVERRUN) — a __security_check_cookie /
// __fastfail failure bypasses SetUnhandledExceptionFilter entirely, so nothing
// reaches standalone_crash.log. Everything else (the game's normal first-chance
// SEH-absorbed AVs) passes straight through untouched.
static void HwWatch_LogStack(void* ctx) {
    if (g_crashLog == INVALID_HANDLE_VALUE) OpenCrashLog();
    CrashLogRaw("\n=== HWWATCH HIT (data breakpoint) ===\n");
    PrintStackTrace(reinterpret_cast<CONTEXT*>(ctx));  // CAST-OK: crash_walk
    CrashLogf("=== END HWWATCH ===\n");
    FlushLogsToDisk();
    // Mirror the frames to stderr too (the run harness keeps stderr per run).
    CONTEXT c = *reinterpret_cast<CONTEXT*>(ctx);  // CAST-OK: crash_walk
    for (int frame = 0; frame < 24; frame++) {
        HMODULE mod = nullptr;
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               (LPCSTR)(uintptr_t)c.Rip, &mod) && mod) {  // CAST-OK: crash_walk
            char path[MAX_PATH] = {0};
            GetModuleFileNameA(mod, path, MAX_PATH);
            const char* nm = strrchr(path, '\\');
            fprintf(stderr, "[HWWATCH]   #%-2d %-18s + 0x%llX\n", frame, nm ? nm + 1 : path,
                    (unsigned long long)((uintptr_t)c.Rip - (uintptr_t)mod));  // CAST-OK: crash_walk
        } else fprintf(stderr, "[HWWATCH]   #%-2d ??? @ %p\n", frame, (void*)c.Rip);
        DWORD64 imageBase = 0;
        PRUNTIME_FUNCTION fn = RtlLookupFunctionEntry(c.Rip, &imageBase, nullptr);
        if (!fn) {
            uintptr_t* sp = reinterpret_cast<uintptr_t*>(c.Rsp);  // CAST-OK: crash_walk
            if (IsBadReadPtr(sp, sizeof(uintptr_t))) break;
            c.Rip = *sp; c.Rsp += 8;
            if (!c.Rip) break;
            continue;
        }
        PVOID hd = nullptr; ULONG64 est = 0;
        RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, c.Rip, fn, &c, &hd, &est, nullptr);
        if (!c.Rip) break;
    }
    fflush(stderr);
}

static LONG WINAPI FirstChanceFailFast(EXCEPTION_POINTERS* ep) {
    DWORD code = ep->ExceptionRecord->ExceptionCode;
    if (code == EXCEPTION_SINGLE_STEP && HwWatch_Armed()) {
        if (HwWatch_OnSingleStep(ep, &HwWatch_LogStack)) return EXCEPTION_CONTINUE_EXECUTION;
    }
    if (code == 0xC0000409 /*STATUS_STACK_BUFFER_OVERRUN*/ ||
        code == 0xC0000602 /*STATUS_FAIL_FAST_EXCEPTION*/) {
        if (g_crashLog == INVALID_HANDLE_VALUE) OpenCrashLog();
        CrashLogRaw("\n=== STANDALONE FAIL-FAST (first-chance) ===\n");
        CrashLogf("Code: 0x%08lX  Address: %p\n", code,
                  ep->ExceptionRecord->ExceptionAddress);
        HMODULE mod = nullptr;
        void* addr = ep->ExceptionRecord->ExceptionAddress;
        if (addr && GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                       GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                       (LPCSTR)addr, &mod) && mod) {  // CAST-OK: crash_walk
            char path[MAX_PATH] = {0};
            GetModuleFileNameA(mod, path, MAX_PATH);
            const char* nm = strrchr(path, '\\');
            CrashLogf("Module: %s + 0x%llX  (base=%p)\n", nm ? nm + 1 : path,
                      (unsigned long long)((uintptr_t)addr - (uintptr_t)mod), (void*)mod);  // CAST-OK: crash_walk
        }
        PrintStackTrace(ep->ContextRecord);
        CrashLogf("=== END FAIL-FAST ===\n");
        FlushLogsToDisk();
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

// ============================================================
// The entry points (src/diag/crash_handler.h)
// ============================================================
void CrashHandler_Install() {
    // Only second-chance exceptions reach CrashHandler, so first-chance AVs
    // absorbed by Banished's SEH chains stay quiet; FirstChanceFailFast sees
    // the fail-fast codes and hw_watch's single steps first.
    SetUnhandledExceptionFilter(CrashHandler);
    AddVectoredExceptionHandler(1, FirstChanceFailFast);
}

void CrashHandler_OpenLog() { OpenCrashLog(); }

void CrashHandler_InstallTerminate() { std::set_terminate(TerminateHandler); }
