// ============================================================================
// src/diag/android/crash_handler_android.cpp -- the game's crash handler (Android)
// ============================================================================
// The Android twin of src/diag/posix/crash_handler_posix.cpp (src/platform/android/README.md:
// a src/diag/android/<name>_android.cpp replaces src/diag/posix/<name>_posix.cpp whole-file).
// Same entry points (src/diag/crash_handler.h), same artifact and the same dump shape
// (=== STANDALONE CRASH ===, the registers, Module: <name> + 0x<off>, Fault:, the
// MemoryAllocator cell, Stack trace:, === END CRASH ===), so triage reads it like the Linux
// and Windows logs. What differs:
//   * The registers are arm64's (x0-x30, sp, pc), from the signal frame's mcontext; Fault:
//     READ / WRITE comes from the ESR record the kernel appends to it (WnR, bit 6), EXEC is
//     pc == fault address. The x86_64 build (the emulator, docs/android/EMULATOR.md) prints the
//     Linux twin's register set and reads READ / WRITE / EXEC from the page-fault error code.
//   * The stack: bionic has no backtrace() before API 33 (minSdk is 29), so the crash stack
//     is walked from the signal frame's pc, lr and frame-pointer chain (arm64 Android code
//     keeps x29 as the frame pointer), each frame checked readable first; std::terminate and
//     the pure-virtual handlers, which run outside a signal, unwind with _Unwind_Backtrace.
//   * Android's own crash reporter (debuggerd) also gets the signal: after the dump the
//     previous handler is put back and the signal re-delivered, so the system writes its
//     tombstone (`adb bugreport`, or logcat's "DEBUG" lines) and the process dies of the
//     signal rather than _exit(3). docs/android/README.md has the ndk-stack recipe.
//   * Every line of the dump is also written to logcat (tag OpenBA, priority ERROR), because
//     the logcat copy of standalone_error.log (app_android.cpp) does not outlive the process.
//   * The redzone heap dump is the POSIX twin's (src/diag/posix/redzone_alloc_posix.cpp).
// Exit codes 4 (pure virtual) and 5 (std::terminate) are kept: those paths end with _exit.
// ============================================================================

#include "crash_handler.h"
#include "redzone_alloc.h"            // RedzoneAlloc_SetDumpSink / RedzoneAlloc_HeapDump
#include "signal_context_android.h"   // SignalContext: the pc / sp / lr / frame pointer by ABI
#include "system/MemoryAllocator.h"   // System::MemoryAllocator::_singleton (the crash dump's cell)

#include <android/log.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>
#include <ucontext.h>
#include <unistd.h>
#include <unwind.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <initializer_list>

namespace {

int g_crashLog = -1;
const char kLogTag[] = "OpenBA";

void OpenCrashLog() {
    if (g_crashLog >= 0) return;  // idempotent
    g_crashLog = open("standalone_crash.log", O_WRONLY | O_CREAT | O_TRUNC | O_SYNC | O_CLOEXEC,
                      0644);
    if (g_crashLog >= 0) return;
    char alt[64];
    std::snprintf(alt, sizeof(alt), "standalone_crash_%d.log", static_cast<int>(getpid()));
    g_crashLog = open(alt, O_WRONLY | O_CREAT | O_TRUNC | O_SYNC | O_CLOEXEC, 0644);
    if (g_crashLog >= 0) {
        std::fprintf(stderr, "[CrashLog] standalone_crash.log could not be opened -- this "
                             "run's dump goes to %s\n", alt);
        std::fflush(stderr);
    }
}

void CrashLogRaw(const char* msg) {
    if (msg == nullptr) return;
    __android_log_write(ANDROID_LOG_ERROR, kLogTag, msg);
    if (g_crashLog < 0) return;
    ssize_t r = write(g_crashLog, msg, std::strlen(msg));
    (void)r;
}

// printf-style, through snprintf into a stack buffer and one write(2). A variadic template
// rather than a va_list function (tools/abi_audit.py counts va_list as an ABI hazard).
template <typename... Args>
void CrashLogf(const char* fmt, Args... args) {
    char buf[1024];
    int n;
    if constexpr (sizeof...(Args) == 0) {
        n = std::snprintf(buf, sizeof(buf), "%s", fmt);
    } else {
        n = std::snprintf(buf, sizeof(buf), fmt, args...);
    }
    if (n <= 0) return;
    CrashLogRaw(buf);
}

void CrashLogSink(const char* s) { CrashLogRaw(s); }

void FlushLogsToDisk() {
    std::fflush(stderr);
    std::fflush(stdout);
    fsync(fileno(stderr));
    fsync(fileno(stdout));
}

// <module basename> + offset for an address, as the Windows log prints it.
void LogModuleLine(const char* label, const void* addr) {
    Dl_info info;
    if (addr != nullptr && dladdr(addr, &info) != 0 && info.dli_fname != nullptr) {
        const char* nm = std::strrchr(info.dli_fname, '/');
        const uintptr_t off = reinterpret_cast<uintptr_t>(addr) -  // CAST-OK: crash_walk
                              reinterpret_cast<uintptr_t>(info.dli_fbase);  // CAST-OK: crash_walk
        if (info.dli_sname != nullptr) {
            const uintptr_t soff = reinterpret_cast<uintptr_t>(addr) -  // CAST-OK: crash_walk
                                   reinterpret_cast<uintptr_t>(info.dli_saddr);  // CAST-OK: crash_walk
            CrashLogf("%s%s + 0x%llX  (base=%p)  %s+0x%llX\n", label,
                      nm ? nm + 1 : info.dli_fname, static_cast<unsigned long long>(off),
                      info.dli_fbase, info.dli_sname, static_cast<unsigned long long>(soff));
        } else {
            CrashLogf("%s%s + 0x%llX  (base=%p)\n", label, nm ? nm + 1 : info.dli_fname,
                      static_cast<unsigned long long>(off), info.dli_fbase);
        }
    } else {
        CrashLogf("%sUNKNOWN (no module covers %p)\n", label, addr);
    }
}

// One frame line: #N <module> + 0x<off>  (<symbol>+0x<off>), the POSIX twin's spelling.
// ndk-stack and llvm-addr2line take the module offset as printed.
void LogFrame(int i, const void* pc) {
    Dl_info info;
    if (dladdr(pc, &info) != 0 && info.dli_fname != nullptr) {
        const char* nm = std::strrchr(info.dli_fname, '/');
        const uintptr_t off = reinterpret_cast<uintptr_t>(pc) -  // CAST-OK: crash_walk
                              reinterpret_cast<uintptr_t>(info.dli_fbase);  // CAST-OK: crash_walk
        if (info.dli_sname != nullptr) {
            const uintptr_t soff = reinterpret_cast<uintptr_t>(pc) -  // CAST-OK: crash_walk
                                   reinterpret_cast<uintptr_t>(info.dli_saddr);  // CAST-OK: crash_walk
            CrashLogf("  #%-2d %-22s + 0x%-8llX  (%s+0x%llX)\n", i, nm ? nm + 1 : info.dli_fname,
                      static_cast<unsigned long long>(off), info.dli_sname,
                      static_cast<unsigned long long>(soff));
        } else {
            CrashLogf("  #%-2d %-22s + 0x%-8llX\n", i, nm ? nm + 1 : info.dli_fname,
                      static_cast<unsigned long long>(off));
        }
    } else {
        CrashLogf("  #%-2d ???                    @ %p\n", i, pc);
    }
}

// True when [p, p+n) is mapped readable: the IsBadReadPtr of the Windows dump. write(2)
// into a pipe reports EFAULT instead of faulting, so a probe is safe inside the handler.
bool Readable(const void* p, size_t n) {
    int fds[2];
    if (pipe(fds) != 0) return false;
    const bool ok = write(fds[1], p, n) == static_cast<ssize_t>(n);
    close(fds[0]);
    close(fds[1]);
    return ok;
}

// The stack outside a signal (std::terminate, pure virtual): the NDK unwinder.
struct UnwindState {
    void** frames;
    int count;
    int cap;
};

_Unwind_Reason_Code UnwindStep(struct _Unwind_Context* ctx, void* arg) {
    UnwindState* st = static_cast<UnwindState*>(arg);  // CAST-OK: posix (the unwinder's void* is ours)
    if (st->count >= st->cap) return _URC_END_OF_STACK;
    const uintptr_t pc = _Unwind_GetIP(ctx);
    if (pc == 0) return _URC_END_OF_STACK;
    st->frames[st->count++] = reinterpret_cast<void*>(pc);  // CAST-OK: crash_walk
    return _URC_NO_REASON;
}

void PrintStackTrace() {
    CrashLogf("Stack trace:\n");
    void* frames[48];
    UnwindState st{frames, 0, 48};
    _Unwind_Backtrace(&UnwindStep, &st);
    for (int i = 0; i < st.count; ++i) LogFrame(i, frames[i]);
}

// The crash stack from the signal frame: pc, then lr (arm64), then the frame-pointer chain
// ({fp, return address} pairs).
void PrintSignalStack(const mcontext_t& mc) {
    CrashLogf("Stack trace:\n");
    int i = 0;
    LogFrame(i++, reinterpret_cast<void*>(SignalContext::Pc(mc)));  // CAST-OK: crash_walk
    if (SignalContext::Lr(mc) != 0) {
        LogFrame(i++, reinterpret_cast<void*>(SignalContext::Lr(mc)));  // CAST-OK: crash_walk
    }
    uintptr_t fp = SignalContext::Fp(mc);
    while (i < 48 && fp != 0 && (fp & 0xF) == 0) {
        const uintptr_t* rec = reinterpret_cast<const uintptr_t*>(fp);  // CAST-OK: crash_walk
        if (!Readable(rec, 2 * sizeof(uintptr_t))) break;
        const uintptr_t next = rec[0];
        const uintptr_t ret = rec[1];
        if (ret == 0) break;
        LogFrame(i++, reinterpret_cast<void*>(ret));  // CAST-OK: crash_walk
        if (next <= fp) break;   // the chain grows towards higher addresses; anything else is garbage
        fp = next;
    }
}

const char* SignalName(int sig) {
    switch (sig) {
    case SIGSEGV: return "SIGSEGV (ACCESS_VIOLATION)";
    case SIGBUS:  return "SIGBUS";
    case SIGFPE:  return "SIGFPE (INT_DIVIDE_BY_ZERO / FP)";
    case SIGILL:  return "SIGILL (ILLEGAL_INSTRUCTION)";
    case SIGABRT: return "SIGABRT";
    default:      return "UNKNOWN";
    }
}

#if defined(__aarch64__)
// The ESR the kernel appends to an arm64 signal frame (its esr_context record in
// mcontext's reserved area); 0 when absent.
uint64_t FaultEsr(const mcontext_t& mc) {
    const uint8_t* p = reinterpret_cast<const uint8_t*>(mc.__reserved);  // CAST-OK: posix (the kernel's record area)
    const uint8_t* const end = p + sizeof(mc.__reserved);
    while (p + 8 <= end) {
        uint32_t magic = 0;
        uint32_t size = 0;
        std::memcpy(&magic, p, 4);
        std::memcpy(&size, p + 4, 4);
        if (magic == 0 || size < 8 || p + size > end) break;
        if (magic == 0x45535201u && size >= 16) {   // ESR_MAGIC: { head, u64 esr }
            uint64_t esr = 0;
            std::memcpy(&esr, p + 8, 8);
            return esr;
        }
        p += size;
    }
    return 0;
}
#endif

// The fault's kind for the Fault: line. arm64: WnR (bit 6) of the ESR, EXEC when pc is the fault
// address. x86_64: the page-fault error code (bit 1 = write, bit 4 = instruction fetch).
const char* FaultOp(const mcontext_t& mc, const siginfo_t* si) {
    const bool exec = SignalContext::Pc(mc) == reinterpret_cast<uintptr_t>(si->si_addr);  // CAST-OK: crash_walk
#if defined(__aarch64__)
    const uint64_t esr = FaultEsr(mc);
    if (exec) return "EXEC";
    if (esr == 0) return "ACCESS";
    return (esr & (1u << 6)) ? "WRITE" : "READ";
#else
    const unsigned long long err = static_cast<unsigned long long>(mc.gregs[REG_ERR]);
    if (exec || (err & 0x10)) return "EXEC";
    return (err & 0x2) ? "WRITE" : "READ";
#endif
}

// The signal frame's registers, in the dump shape of the ABI's desktop twin.
void LogRegisters(const mcontext_t& mc) {
#if defined(__aarch64__)
    for (int r = 0; r < 28; r += 4) {
        CrashLogf("X%-2d=%016llX X%-2d=%016llX X%-2d=%016llX X%-2d=%016llX\n", r,
                  (unsigned long long)mc.regs[r], r + 1, (unsigned long long)mc.regs[r + 1], r + 2,
                  (unsigned long long)mc.regs[r + 2], r + 3, (unsigned long long)mc.regs[r + 3]);
    }
    CrashLogf("X28=%016llX FP =%016llX LR =%016llX\n", (unsigned long long)mc.regs[28],
              (unsigned long long)mc.regs[29], (unsigned long long)mc.regs[30]);
    CrashLogf("PC =%016llX SP =%016llX\n", (unsigned long long)mc.pc, (unsigned long long)mc.sp);
#else
    const greg_t* g = mc.gregs;
    CrashLogf("RAX=%016llX RCX=%016llX RDX=%016llX RBX=%016llX\n",
              (unsigned long long)g[REG_RAX], (unsigned long long)g[REG_RCX],
              (unsigned long long)g[REG_RDX], (unsigned long long)g[REG_RBX]);
    CrashLogf("RBP=%016llX RSI=%016llX RDI=%016llX R8 =%016llX\n",
              (unsigned long long)g[REG_RBP], (unsigned long long)g[REG_RSI],
              (unsigned long long)g[REG_RDI], (unsigned long long)g[REG_R8]);
    CrashLogf("R9 =%016llX R10=%016llX R11=%016llX R12=%016llX\n",
              (unsigned long long)g[REG_R9], (unsigned long long)g[REG_R10],
              (unsigned long long)g[REG_R11], (unsigned long long)g[REG_R12]);
    CrashLogf("R13=%016llX R14=%016llX R15=%016llX\n",
              (unsigned long long)g[REG_R13], (unsigned long long)g[REG_R14],
              (unsigned long long)g[REG_R15]);
    CrashLogf("RIP=%016llX RSP=%016llX\n", (unsigned long long)g[REG_RIP], (unsigned long long)g[REG_RSP]);
#endif
}

// A call through a null pointer leaves pc = 0: the caller is the return address, in lr on arm64,
// at [rsp] on x86_64 (the CALL pushed it); 0 when unreadable.
uintptr_t NullCallCaller(const mcontext_t& mc) {
    if (SignalContext::Lr(mc) != 0) return SignalContext::Lr(mc);
    const uintptr_t* sp = reinterpret_cast<const uintptr_t*>(SignalContext::Sp(mc));  // CAST-OK: crash_walk
    if (!Readable(sp, sizeof(uintptr_t))) return 0;
    return *sp;
}

struct sigaction g_previous[32];

// The thread writing the crash dump, 0 while none is. Lock-free, so the handler may use it.
std::atomic<pid_t> g_dumpOwner{0};
static_assert(std::atomic<pid_t>::is_always_lock_free, "the crash handler's owner must be lock-free");

// A thread that faults while another writes the dump parks here: the dump ends in the owner's
// re-raise to debuggerd, which ends the process. The wait is bounded (10 s), in case the dump
// itself waits on this thread (a lock it holds); then the process ends as before.
void ParkWhileAnotherThreadDumps() {
    for (int i = 0; i < 100; ++i) {
        timespec ts{0, 100 * 1000 * 1000};
        nanosleep(&ts, nullptr);
    }
    _exit(3);
}

void CrashHandler(int sig, siginfo_t* si, void* ucv) {
    // Re-entry on the dumping thread (a fault inside the dump) ends the process at once. A fault
    // on another thread meanwhile waits for the dump instead: a process-wide flag sent it to
    // _exit(3) too, which cut the dump and debuggerd's tombstone short (review F-306).
    const pid_t self = gettid();
    pid_t owner = 0;
    if (!g_dumpOwner.compare_exchange_strong(owner, self)) {
        if (owner == self) _exit(3);
        ParkWhileAnotherThreadDumps();
    }

    const ucontext_t* uc = static_cast<const ucontext_t*>(ucv);  // CAST-OK: posix (sigaction's void* is the ucontext_t)
    const mcontext_t& mc = uc->uc_mcontext;

    CrashLogRaw("\n=== STANDALONE CRASH ===\n");
    CrashLogf("Exception: signal %d (%s) si_code=%d\n", sig, SignalName(sig), si->si_code);
    CrashLogf("Address:   %p\n", reinterpret_cast<void*>(SignalContext::Pc(mc)));  // CAST-OK: crash_walk
    LogRegisters(mc);

    LogModuleLine("Module:    ", reinterpret_cast<void*>(SignalContext::Pc(mc)));  // CAST-OK: crash_walk
    // A call through a null pointer leaves pc = 0 with the return address in lr (arm64) or at [rsp].
    if (SignalContext::Pc(mc) == 0) {
        LogModuleLine("Caller:    ", reinterpret_cast<void*>(NullCallCaller(mc)));  // CAST-OK: crash_walk
    }

    if (sig == SIGSEGV || sig == SIGBUS) {
        CrashLogf("Fault: %s of %p\n", FaultOp(mc, si), si->si_addr);
    }

    {
        System::MemoryAllocator** const cell = &System::MemoryAllocator::_singleton;
        CrashLogf("MemoryAllocator::_singleton cell=%p value=%p\n",
                  static_cast<void*>(cell), static_cast<void*>(*cell));
    }

    CrashLogf("(heap-dump follows; also mirrored to standalone_debug.log)\n");
    RedzoneAlloc_SetDumpSink(&CrashLogSink);
    RedzoneAlloc_HeapDump("CrashHandler");
    RedzoneAlloc_SetDumpSink(nullptr);

    PrintSignalStack(mc);

    CrashLogf("=== END CRASH (the system tombstone follows: adb logcat -s DEBUG) ===\n");
    FlushLogsToDisk();

    // Hand the signal to debuggerd's handler (the banner). A fault re-executes the faulting
    // instruction on return and lands there; an abort is raised again, pending until return.
    if (sig > 0 && sig < 32) sigaction(sig, &g_previous[sig], nullptr);
    if (sig == SIGABRT) raise(sig);
}

void LogPureVirtualCall(const char* tag, void* retAddr) {
    if (g_crashLog < 0) OpenCrashLog();
    CrashLogRaw("\n=== STANDALONE PURE-VIRTUAL CALL ===\n");
    CrashLogf("Tag:            %s\n", tag);
    CrashLogf("Return address: %p\n", retAddr);
    if (retAddr != nullptr) LogModuleLine("Module:         ", retAddr);
    PrintStackTrace();
    CrashLogf("=== END PURE-VIRTUAL ===\n");
    FlushLogsToDisk();
    _exit(4);
}

void TerminateHandler() {
    if (g_crashLog < 0) OpenCrashLog();
    CrashLogRaw("\n=== STANDALONE std::terminate() ===\n");
    PrintStackTrace();
    CrashLogf("=== END terminate ===\n");
    FlushLogsToDisk();
    _exit(5);
}

}  // namespace

extern "C" void __cxa_pure_virtual() {
    LogPureVirtualCall("__cxa_pure_virtual", __builtin_return_address(0));
}

extern "C" void _purecall() {
    LogPureVirtualCall("_purecall", __builtin_return_address(0));
}

// ============================================================
// The entry points (src/diag/crash_handler.h)
// ============================================================
void CrashHandler_Install() {
    // The unwinder's first call may allocate; make it now, not inside a handler.
    void* warm[1];
    UnwindState st{warm, 0, 1};
    _Unwind_Backtrace(&UnwindStep, &st);

    // An alternate stack, so a stack overflow's SIGSEGV still has room to log.
    static char altStack[64 * 1024];
    stack_t ss{};
    ss.ss_sp = altStack;
    ss.ss_size = sizeof(altStack);
    sigaltstack(&ss, nullptr);

    struct sigaction sa{};
    sa.sa_sigaction = &CrashHandler;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&sa.sa_mask);
    for (int sig : {SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT})
        sigaction(sig, &sa, &g_previous[sig]);
}

void CrashHandler_OpenLog() { OpenCrashLog(); }

void CrashHandler_InstallTerminate() { std::set_terminate(TerminateHandler); }
