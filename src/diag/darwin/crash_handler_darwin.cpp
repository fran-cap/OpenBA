// ============================================================================
// src/diag/darwin/crash_handler_darwin.cpp -- the exe's crash handler (Darwin)
// ============================================================================
// The Darwin twin of src/diag/posix/crash_handler_posix.cpp (arm64 register dump from the
// Mach ucontext; copy of the POSIX file otherwise). The POSIX twin of src/diag/win/crash_handler_win.cpp (Phase D.4 LINUX-1; D4_RULES
// C10: the crash / hang handlers get a POSIX twin once a Linux exe exists). Same
// entry points (src/diag/crash_handler.h), same artifact, same exit codes:
//
//   standalone_crash.log   opened (truncated) at boot beside the other logs, written
//                          with write(2) on an O_SYNC descriptor -- the twin of the
//                          FILE_FLAG_WRITE_THROUGH handle: no CRT FILE*, whose lock a
//                          crashing thread may hold.
//   exit 3                 a fatal signal (SIGSEGV / SIGBUS / SIGFPE / SIGILL /
//                          SIGABRT) -- the Windows second-chance filter's ExitProcess(3)
//   exit 4                 a pure-virtual call (__cxa_pure_virtual / _purecall)
//   exit 5                 std::terminate
//
// The dump keeps the Windows log's shape (=== STANDALONE CRASH ===, the registers,
// Module: <name> + 0x<off>, Fault:, the VCALL-of-0 block, the MemoryAllocator cell,
// Stack trace:, === END CRASH ===) so triage reads both alike. What differs:
//   * "Exception:" carries the signal number and name (there is no NTSTATUS), and
//     "Fault:" READ / WRITE comes from the page-fault error code (REG_ERR bit 1);
//     EXEC is RIP == fault address.
//   * The stack is glibc's backtrace() from inside the handler (libgcc unwinds
//     through the kernel's signal frame), each frame resolved with dladdr to
//     module + offset and, when the dynamic symbol table has it, the symbol name.
//     The exe links with -rdynamic, so its own functions are named.
//   * The redzone heap dump is RedzoneAlloc_HeapDump's POSIX twin, which says it is
//     unavailable (src/diag/posix/redzone_alloc_posix.cpp).
//   * There is no first-chance fail-fast VEH and no hardware-watchpoint single step:
//     a stack-protector failure on glibc is __stack_chk_fail -> abort -> SIGABRT,
//     which lands here; hw_watch has no POSIX body.
//
// _purecall: the MSVC CRT's pure-virtual handler, named by CompanionData.h. This file
// defines it for the POSIX build.
// ============================================================================

#include "crash_handler.h"
#include "redzone_alloc.h"            // RedzoneAlloc_SetDumpSink / RedzoneAlloc_HeapDump
#include "system/MemoryAllocator.h"   // System::MemoryAllocator::_singleton (the crash dump's cell)

#include <dlfcn.h>
#include <execinfo.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <sys/ucontext.h>
#include <time.h>
#include <unistd.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <initializer_list>

namespace {

int g_crashLog = -1;

void OpenCrashLog() {
    if (g_crashLog >= 0) return;  // idempotent
    g_crashLog = open("standalone_crash.log", O_WRONLY | O_CREAT | O_TRUNC | O_SYNC | O_CLOEXEC,
                      0644);
    if (g_crashLog >= 0) return;
    // The Windows twin falls back to a pid-suffixed name when a zombie holds the file;
    // the same fallback covers an unwritable file here.
    char alt[64];
    std::snprintf(alt, sizeof(alt), "standalone_crash_%d.log", static_cast<int>(getpid()));
    g_crashLog = open(alt, O_WRONLY | O_CREAT | O_TRUNC | O_SYNC | O_CLOEXEC, 0644);
    if (g_crashLog >= 0) {
        std::fprintf(stderr,
                     "[CrashLog] standalone_crash.log could not be opened -- this "
                     "run's dump goes to %s\n",
                     alt);
        std::fflush(stderr);
    }
}

void CrashLogRaw(const char* msg) {
    if (g_crashLog < 0 || msg == nullptr) return;
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
    if (n <= 0 || g_crashLog < 0) return;
    if (n > static_cast<int>(sizeof(buf))) n = sizeof(buf);
    ssize_t r = write(g_crashLog, buf, static_cast<size_t>(n));
    (void)r;
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

// One line per frame: #N <module> + 0x<off>  (<symbol>+0x<off>), the module part
// matching StandaloneWalkStack's Windows lines.
void PrintStackTrace() {
    CrashLogf("Stack trace:\n");
    void* frames[48];
    const int n = backtrace(frames, 48);
    for (int i = 0; i < n; ++i) {
        Dl_info info;
        if (dladdr(frames[i], &info) != 0 && info.dli_fname != nullptr) {
            const char* nm = std::strrchr(info.dli_fname, '/');
            const uintptr_t off = reinterpret_cast<uintptr_t>(frames[i]) -  // CAST-OK: crash_walk
                                  reinterpret_cast<uintptr_t>(info.dli_fbase);  // CAST-OK: crash_walk
            if (info.dli_sname != nullptr) {
                const uintptr_t soff = reinterpret_cast<uintptr_t>(frames[i]) -  // CAST-OK: crash_walk
                                       reinterpret_cast<uintptr_t>(info.dli_saddr);  // CAST-OK: crash_walk
                CrashLogf("  #%-2d %-22s + 0x%-8llX  (%s+0x%llX)\n", i, nm ? nm + 1 : info.dli_fname,
                          static_cast<unsigned long long>(off), info.dli_sname,
                          static_cast<unsigned long long>(soff));
            } else {
                CrashLogf("  #%-2d %-22s + 0x%-8llX\n", i, nm ? nm + 1 : info.dli_fname,
                          static_cast<unsigned long long>(off));
            }
        } else {
            CrashLogf("  #%-2d ???                    @ %p\n", i, frames[i]);
        }
    }
}

const char* SignalName(int sig) {
    switch (sig) {
        case SIGSEGV:
            return "SIGSEGV (ACCESS_VIOLATION)";
        case SIGBUS:
            return "SIGBUS";
        case SIGFPE:
            return "SIGFPE (INT_DIVIDE_BY_ZERO / FP)";
        case SIGILL:
            return "SIGILL (ILLEGAL_INSTRUCTION)";
        case SIGABRT:
            return "SIGABRT";
        default:
            return "UNKNOWN";
    }
}

// The thread writing the crash dump (pthread_threadid_np), 0 while none is. Lock-free, so the
// handler may use it.
std::atomic<uint64_t> g_dumpOwner{0};
static_assert(std::atomic<uint64_t>::is_always_lock_free, "the crash handler's owner must be lock-free");

// A thread that faults while another writes the dump parks here: the dump ends in the owner's
// _exit(3). The wait is bounded (10 s), in case the dump itself waits on this thread (a lock it
// holds); then the process ends as before.
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
    // _exit(3) too, which cut the dump short (review F-306).
    uint64_t self = 0;
    pthread_threadid_np(nullptr, &self);
    uint64_t owner = 0;
    if (!g_dumpOwner.compare_exchange_strong(owner, self)) {
        if (owner == self) _exit(3);
        ParkWhileAnotherThreadDumps();
    }

    const ucontext_t* uc = static_cast<const ucontext_t*>(ucv);  // CAST-OK: posix (sigaction's void* is the ucontext_t)
    const auto& regs = uc->uc_mcontext->__ss;
    CrashLogRaw("\n=== STANDALONE CRASH (ARM64) ===\n");
    CrashLogf("Exception: signal %d (%s) address=%p\n", sig, SignalName(sig), si->si_addr);
    CrashLogf("PC=%016llX SP=%016llX LR=%016llX FP=%016llX\n", regs.__pc, regs.__sp, regs.__lr,
              regs.__fp);
    LogModuleLine("Module: ", reinterpret_cast<void*>(regs.__pc));
    for (int i = 0; i < 29; ++i)
        CrashLogf("X%d=%016llX\n", i, regs.__x[i]);

    {
        System::MemoryAllocator** const cell = &System::MemoryAllocator::_singleton;
        CrashLogf("MemoryAllocator::_singleton cell=%p value=%p\n",
                  static_cast<void*>(cell), static_cast<void*>(*cell));
    }

    CrashLogf("(heap-dump follows; also mirrored to standalone_debug.log)\n");
    RedzoneAlloc_SetDumpSink(&CrashLogSink);
    RedzoneAlloc_HeapDump("CrashHandler");
    RedzoneAlloc_SetDumpSink(nullptr);

    PrintStackTrace();

    CrashLogf("=== END CRASH ===\n");
    FlushLogsToDisk();
    _exit(3);
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
    // backtrace() loads libgcc's unwinder on its first call; make that call now, not
    // inside a signal handler.
    void* warm[1];
    backtrace(warm, 1);

    // An alternate stack, so a stack overflow's SIGSEGV still has room to log. This one is
    // the main thread's; each Platform:: thread installs its own (src/platform/posix/
    // thread_posix.cpp), since a new pthread starts without one.
    static char altStack[64 * 1024];
    stack_t ss{};
    ss.ss_sp = altStack;
    ss.ss_size = sizeof(altStack);
    sigaltstack(&ss, nullptr);

    struct sigaction sa{};
    sa.sa_sigaction = &CrashHandler;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&sa.sa_mask);
    for (int sig : { SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT })
        sigaction(sig, &sa, nullptr);
}

void CrashHandler_OpenLog() { OpenCrashLog(); }

void CrashHandler_InstallTerminate() { std::set_terminate(TerminateHandler); }
