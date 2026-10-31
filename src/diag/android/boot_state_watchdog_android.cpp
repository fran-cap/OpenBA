// src/diag/android/boot_state_watchdog_android.cpp
// The Android twin of src/diag/posix/boot_state_watchdog_posix.cpp (src/platform/android/README.md:
// it replaces that file whole on Android). The same boot state, written the same way in the
// working directory (the app's data folder: Platform::App::EnterRunDirectory), so
// tools/android_boot_state.sh pulls it and tools/boot_state.sh reads it as a Linux run's:
//
//   standalone_boot_state.txt, rewritten once a second:
//   STATE=<state> pid=<n> uptimeSec=<n> frame=<n> stale=<n> detail=<text>
//
// with the same states, heartbeat, budgets and publish interval (the constants below must stay
// equal to the Windows file's, as in the POSIX twin). What differs from the POSIX twin:
//  * Bionic has no backtrace() before API 33 (minSdk is 29). Each thread's hang stack is walked
//    inside the dump signal's handler from the interrupted context it receives (SA_SIGINFO):
//    pc, lr, then the x29 frame-pointer chain, each record checked readable first (the crash
//    handler's walk, src/diag/android/crash_handler_android.cpp).
//  * The [BootState] lines reach logcat through the logcat copy of standalone_error.log
//    (src/platform/android/app_android.cpp).
//  * P7: the background does not count (boot_state_background.h). Between
//    WILL_ENTER_BACKGROUND and DID_ENTER_FOREGROUND the stale clock is held at zero and the
//    state file says detail=background.
// Everything else (BLOCKED_DIALOG never published, the per-thread signal walk over
// /proc/self/task, _exit(4) on an unattended HUNG) is the POSIX twin's; its banner holds.

#include "boot_state_watchdog.h"
#include "boot_state_background.h"

#include "options.h"
#include "platform/common/cpu/topology.h"   // CPULIMIT: the [Cpu] facts on the RUNNING line

#include <dirent.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <sys/syscall.h>
#include <time.h>
#include <ucontext.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace BootState {

namespace {

std::atomic<int>      g_startupComplete{0};
std::atomic<int>      g_quitting{0};
std::atomic<uint32_t> g_frames{0};
std::atomic<int>      g_stop{0};
std::atomic<int>      g_background{0};   // P7: between WILL_ENTER_BACKGROUND and DID_ENTER_FOREGROUND
pthread_t             g_thread;
bool                  g_threadStarted = false;
uint64_t              g_startMs       = 0;
pid_t                 g_mainTid       = 0;

// The same two budgets as src/diag/win/boot_state_watchdog.cpp (tools/boot_state.sh
// reads them from that file).
const unsigned kHungRunningSec  = 20;   // BOOTING/RUNNING: 20s with no new frame
const unsigned kHungQuittingSec = 45;   // QUITTING: teardown renders no frames at all

uint64_t NowMs() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1000u + static_cast<uint64_t>(ts.tv_nsec) / 1000000u;
}

pid_t CurrentTid() { return static_cast<pid_t>(syscall(SYS_gettid)); }

void WriteStateFile(const char* state, unsigned uptimeSec, unsigned frame,
                    unsigned staleSec, const char* detail) {
    FILE* f = std::fopen("standalone_boot_state.txt", "w");
    if (!f) return;
    std::fprintf(f, "STATE=%s pid=%u uptimeSec=%u frame=%u stale=%u detail=%s\n",
                 state, static_cast<uint32_t>(getpid()), uptimeSec, frame, staleSec,
                 (detail && detail[0]) ? detail : "-");
    std::fclose(f);
}

// ---------------------------------------------------------------------------
// The hang dump
// ---------------------------------------------------------------------------
int   g_hangLog = -1;
sem_t g_dumpDone;
const int kDumpSignal = SIGRTMIN + 3;   // not one SDL, bionic (its SIGRTMIN reserve) or the crash handler use

void HangLogRaw(const char* s) {
    if (g_hangLog < 0 || !s) return;
    ssize_t r = write(g_hangLog, s, std::strlen(s));
    (void)r;
}

// printf-style through snprintf (a variadic template, not va_list: tools/abi_audit.py).
template <typename... Args>
void HangLogf(const char* fmt, Args... args) {
    char buf[512];
    const int n0 = std::snprintf(buf, sizeof(buf), fmt, args...);
    if (n0 <= 0 || g_hangLog < 0) return;
    const int n = n0 > static_cast<int>(sizeof(buf)) ? static_cast<int>(sizeof(buf)) : n0;
    ssize_t r = write(g_hangLog, buf, static_cast<size_t>(n));
    (void)r;
}

// True when [p, p+n) is mapped readable. write(2) into a pipe reports EFAULT instead of
// faulting, so the probe is safe inside the dump signal's handler.
bool Readable(const void* p, size_t n) {
    int fds[2];
    if (pipe(fds) != 0) return false;
    const bool ok = write(fds[1], p, n) == static_cast<ssize_t>(n);
    close(fds[0]);
    close(fds[1]);
    return ok;
}

void DumpFrame(int i, uintptr_t pcValue) {
    const void* pc = reinterpret_cast<const void*>(pcValue);  // CAST-OK: crash_walk
    char line[256];
    Dl_info info;
    if (dladdr(pc, &info) != 0 && info.dli_fname != nullptr) {
        const char* nm = std::strrchr(info.dli_fname, '/');
        const uintptr_t off = pcValue - reinterpret_cast<uintptr_t>(info.dli_fbase);  // CAST-OK: crash_walk
        if (info.dli_sname != nullptr) {
            const uintptr_t soff = pcValue - reinterpret_cast<uintptr_t>(info.dli_saddr);  // CAST-OK: crash_walk
            std::snprintf(line, sizeof(line), "  #%-2d %-22s + 0x%-8llX  (%s+0x%llX)\n", i,
                          nm ? nm + 1 : info.dli_fname, static_cast<unsigned long long>(off),
                          info.dli_sname, static_cast<unsigned long long>(soff));
        } else {
            std::snprintf(line, sizeof(line), "  #%-2d %-22s + 0x%-8llX\n", i,
                          nm ? nm + 1 : info.dli_fname, static_cast<unsigned long long>(off));
        }
    } else {
        std::snprintf(line, sizeof(line), "  #%-2d ???                    @ %p\n", i, pc);
    }
    HangLogRaw(line);
}

// The interrupted thread's stack, one line per frame: pc, lr, then the x29 chain.
void DumpInterruptedStack(const mcontext_t& mc) {
    int i = 0;
    DumpFrame(i++, static_cast<uintptr_t>(mc.pc));
    DumpFrame(i++, static_cast<uintptr_t>(mc.regs[30]));
    uintptr_t fp = static_cast<uintptr_t>(mc.regs[29]);
    while (i < 40 && fp != 0 && (fp & 0xF) == 0) {
        const uintptr_t* rec = reinterpret_cast<const uintptr_t*>(fp);  // CAST-OK: crash_walk
        if (!Readable(rec, 2 * sizeof(uintptr_t))) break;
        const uintptr_t next = rec[0];
        const uintptr_t ret = rec[1];
        if (ret == 0) break;
        DumpFrame(i++, ret);
        if (next <= fp) break;
        fp = next;
    }
}

void DumpSignalHandler(int, siginfo_t*, void* ucv) {
    const int saved = errno;
    const ucontext_t* uc = static_cast<const ucontext_t*>(ucv);  // CAST-OK: posix (sigaction's void* is the ucontext_t)
    DumpInterruptedStack(uc->uc_mcontext);
    sem_post(&g_dumpDone);
    errno = saved;
}

void WriteHangDump(const char* phase, unsigned uptimeSec, unsigned frame,
                   unsigned staleSec, unsigned budgetSec) {
    g_hangLog = open("standalone_hang.log", O_WRONLY | O_CREAT | O_TRUNC | O_SYNC | O_CLOEXEC, 0644);
    if (g_hangLog < 0) return;

    HangLogRaw("=== STANDALONE HANG ===\n");
    HangLogf("Phase:       %s\n", phase);
    HangLogf("Pid:         %u   (main thread id %u)\n",
             static_cast<uint32_t>(getpid()), static_cast<uint32_t>(g_mainTid));
    HangLogf("Uptime:      %us\n", uptimeSec);
    HangLogf("Frame:       %u  (main-loop iterations completed)\n", frame);
    HangLogf("Stale:       %us  (budget for this phase: %us)\n", staleSec, budgetSec);
    HangLogRaw("The frame counter stopped advancing. Every thread's stack follows;\n"
               "the MAIN thread is the one that matters. A UI::ShowDialog pump on it\n"
               "means an unanswered in-game modal, not a deadlock (see\n"
               "src/diag/win/boot_state_watchdog.cpp).\n\n");

    const pid_t selfTid = CurrentTid();
    const pid_t pid = getpid();
    DIR* tasks = opendir("/proc/self/task");
    if (tasks == nullptr) {
        HangLogRaw("(/proc/self/task unreadable -- no thread list)\n");
    } else {
        while (dirent* e = readdir(tasks)) {
            if (e->d_name[0] < '0' || e->d_name[0] > '9') continue;
            const pid_t tid = static_cast<pid_t>(std::atoi(e->d_name));
            const bool isMain = tid == g_mainTid;
            const bool isSelf = tid == selfTid;
            char comm[64] = {0};
            char path[64];
            std::snprintf(path, sizeof(path), "/proc/self/task/%d/comm", static_cast<int>(tid));
            if (FILE* cf = std::fopen(path, "r")) {
                if (std::fgets(comm, sizeof(comm), cf) == nullptr) comm[0] = '\0';
                std::fclose(cf);
                if (char* nl = std::strchr(comm, '\n')) *nl = '\0';
            }
            HangLogf("\n--- thread %u (%s)%s%s ---\n", static_cast<uint32_t>(tid), comm,
                     isMain ? "  [MAIN THREAD]" : "",
                     isSelf ? "  [watchdog -- this thread]" : "");
            if (isSelf) {
                HangLogRaw("  (the watchdog itself; not interesting)\n");
                continue;
            }
            if (syscall(SYS_tgkill, pid, tid, kDumpSignal) != 0) {
                HangLogf("  (tgkill failed, errno=%d)\n", errno);
                continue;
            }
            timespec deadline;
            clock_gettime(CLOCK_REALTIME, &deadline);
            deadline.tv_sec += 1;
            if (sem_timedwait(&g_dumpDone, &deadline) != 0)
                HangLogRaw("  (no answer within 1s: blocked with the signal masked?)\n");
        }
        closedir(tasks);
    }

    HangLogRaw("\n=== END HANG ===\n");
    fsync(g_hangLog);
    close(g_hangLog);
    g_hangLog = -1;
}

void* WatchdogThread(void*) {
    const char* lastState = "";
    uint32_t    lastFrame    = 0xFFFFFFFFu;
    uint64_t    lastMoveMs   = NowMs();
    bool        hungReported = false;

    while (g_stop.load() == 0) {
        const uint64_t now       = NowMs();
        const unsigned uptimeSec = static_cast<unsigned>((now - g_startMs) / 1000);

        const uint32_t frame    = g_frames.load();
        const bool     quitting = g_quitting.load() != 0;

        const bool background = g_background.load() != 0;
        if (frame != lastFrame || background) {
            lastFrame    = frame;
            lastMoveMs   = now;
            hungReported = false;
        }
        if (quitting && std::strcmp(lastState, "QUITTING") != 0 &&
            std::strcmp(lastState, "HUNG") != 0) {
            lastMoveMs   = now;
            hungReported = false;
        }
        const unsigned staleSec = static_cast<unsigned>((now - lastMoveMs) / 1000);

        const char* phase = quitting ? "QUITTING"
                                     : (g_startupComplete.load() ? "RUNNING" : "BOOTING");
        const unsigned budget = quitting ? kHungQuittingSec : kHungRunningSec;
        const bool hung = staleSec >= budget;
        const char* state = hung ? "HUNG" : phase;

        WriteStateFile(state, uptimeSec, frame, staleSec, hung ? phase : (background ? "background" : "-"));

        if (hung && !hungReported) {
            hungReported = true;
            std::fprintf(stderr,
                "[BootState] HUNG in %s after %us -- the main loop has not completed a "
                "frame for %us (budget %us, frame=%d). Writing standalone_hang.log "
                "(all-thread stacks).%s\n",
                phase, uptimeSec, staleSec, budget, static_cast<int32_t>(frame),
                Opt().unattendedBoot ? " Unattended run: terminating with exit code 4."
                                     : " Attended run: NOT terminating.");
            std::fflush(stderr);

            WriteHangDump(phase, uptimeSec, frame, staleSec, budget);

            if (Opt().unattendedBoot) {
                std::fflush(stdout);
                std::fflush(stderr);
                _exit(4);
            }
        }

        const bool changed = (std::strcmp(state, lastState) != 0);
        if (changed && std::strcmp(state, "RUNNING") == 0) {
            // CPULIMIT: the process mask and core counts ride on the RUNNING line, so
            // every run (and every stress A/B leg) proves its sys.cpu_limit.
            char cpu[192];
            Cpu::Describe(Cpu::Get(), cpu, sizeof(cpu));
            std::fprintf(stderr, "[BootState] RUNNING (Engine::Startup returned at %us) cpu %s\n",
                         uptimeSec, cpu);
            std::fflush(stderr);
        }
        if (changed && std::strcmp(state, "QUITTING") == 0) {
            std::fprintf(stderr, "[BootState] QUITTING at %us after %d frames "
                                 "(teardown budget %us)\n",
                         uptimeSec, static_cast<int32_t>(frame), kHungQuittingSec);
            std::fflush(stderr);
        }
        lastState = state;

        usleep(1000 * 1000);   // the Windows watchdog's Sleep(1000)
    }
    return nullptr;
}

// The line Stop() writes: EXITED / clean-shutdown unless SetExitState named another
// (boot_state_watchdog.h).
char g_exitState[32]   = "EXITED";
char g_exitDetail[512] = "clean-shutdown";

}  // namespace

void SetExitState(const char* state, const char* detail) {
    std::snprintf(g_exitState, sizeof(g_exitState), "%s", state);
    std::snprintf(g_exitDetail, sizeof(g_exitDetail), "%s", detail);
    // Published at once as well: a box shown before the process exits must not leave a previous
    // run's line (STALE) or none at all behind while it waits.
    WriteStateFile(g_exitState, 0, 0, 0, g_exitDetail);
}

void Start() {
    if (g_threadStarted) return;
    g_startMs = NowMs();
    g_mainTid = CurrentTid();   // Start() is called from main
    WriteStateFile("BOOTING", 0, 0, 0, "-");

    // The hang dump's per-thread handler (it walks the context it interrupts).
    sem_init(&g_dumpDone, 0, 0);
    struct sigaction sa{};
    sa.sa_sigaction = &DumpSignalHandler;
    sa.sa_flags = SA_RESTART | SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    sigaction(kDumpSignal, &sa, nullptr);

    const int rc = pthread_create(&g_thread, nullptr, &WatchdogThread, nullptr);
    if (rc != 0) {
        std::fprintf(stderr, "[BootState] WARNING: watchdog thread failed to start "
                             "(pthread_create=%d) -- boot state will not be published\n", rc);
        std::fflush(stderr);
        return;
    }
    pthread_setname_np(g_thread, "BootState");
    g_threadStarted = true;
}

void MarkStartupComplete() {
    g_startupComplete.store(1);
}

namespace Android {

void EnterBackground() {
    g_background.store(1);
    std::fprintf(stderr, "[BootState] background: the hang budget is paused\n");
}

void EnterForeground() {
    // The watchdog's next tick sees the flag clear with lastMoveMs = that tick: the budget restarts.
    g_background.store(0);
    std::fprintf(stderr, "[BootState] foreground: the hang budget runs again\n");
}

}  // namespace Android

void Stop() {
    g_stop.store(1);
    if (g_threadStarted) {
        pthread_join(g_thread, nullptr);
        g_threadStarted = false;
    }
    // Before Start() (a boot stopped by the game-data check) there is no uptime to report.
    const unsigned uptimeSec = g_startMs != 0 ? static_cast<unsigned>((NowMs() - g_startMs) / 1000) : 0;
    WriteStateFile(g_exitState, uptimeSec, g_frames.load(), 0, g_exitDetail);
}

}  // namespace BootState

extern "C" void BootState_MarkFrame() {
    BootState::g_frames.fetch_add(1, std::memory_order_relaxed);
}

extern "C" void BootState_MarkQuitting() {
    BootState::g_quitting.store(1);
}
