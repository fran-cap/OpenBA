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
// /proc/self/task, only the thread being asked writing or answering (g_dumpTarget, tested
// again just before each line is written), _exit(4) on an unattended HUNG) is the POSIX
// twin's; its banner holds (review F-302, F-304, F-307, F-316).

#include "boot_state_watchdog.h"
#include "boot_state_background.h"
#include "signal_context_android.h"   // SignalContext: the pc / lr / frame pointer by ABI

#include "options.h"
#include "app/clamped_atoi.h"   // review F-087: checked parse of /proc text
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
std::atomic<int> g_hangLog{-1};   // read by the handlers: lock-free, so signal-safe
sem_t g_dumpDone;
const int kDumpSignal = SIGRTMIN + 3;   // not one SDL, bionic (its SIGRTMIN reserve) or the crash handler use
// The thread asked for its stack, 0 when none is: the POSIX twin's protocol. A handler tests
// it immediately before each write() (after dladdr(), which takes the linker's lock and can
// block past the wait), so a line is written only while its thread is the target; what
// remains is the gap between that load and the write, which holds no blocking call. The one
// answer belongs to whoever clears the target first: the handler when it is done, or the
// watchdog when the second runs out. A late handler therefore neither writes under the next
// thread's header nor leaves a post that satisfies the next wait.
std::atomic<pid_t> g_dumpTarget{0};
// Handlers between entry and return. The watchdog closes the log only when none is, so no
// late write can reach a reused descriptor.
std::atomic<int> g_dumpHandlers{0};
static_assert(std::atomic<int>::is_always_lock_free, "the handler's loads must be lock-free");
static_assert(std::atomic<pid_t>::is_always_lock_free, "the handler's loads must be lock-free");

void HangLogRaw(const char* s) {
    const int fd = g_hangLog.load();
    if (fd < 0 || !s) return;
    ssize_t r = write(fd, s, std::strlen(s));
    (void)r;
}

// printf-style through snprintf (a variadic template, not va_list: tools/abi_audit.py).
template <typename... Args>
void HangLogf(const char* fmt, Args... args) {
    char buf[512];
    const int n0 = std::snprintf(buf, sizeof(buf), fmt, args...);
    const int fd = g_hangLog.load();
    if (n0 <= 0 || fd < 0) return;
    const int n = n0 > static_cast<int>(sizeof(buf)) ? static_cast<int>(sizeof(buf)) : n0;
    ssize_t r = write(fd, buf, static_cast<size_t>(n));
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

// One frame's line, while the dump still waits for this thread; false once it does not.
bool DumpFrame(pid_t self, int i, uintptr_t pcValue) {
    if (g_dumpTarget.load() != self) return false;   // the watchdog stopped waiting
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
    if (g_dumpTarget.load() != self) return false;   // it stopped waiting while dladdr() ran
    HangLogRaw(line);
    return true;
}

// The interrupted thread's stack, one line per frame: pc, lr (arm64), then the frame-pointer
// chain; it stops at the first frame the dump no longer waits for.
void DumpInterruptedStack(pid_t self, const mcontext_t& mc) {
    int i = 0;
    if (!DumpFrame(self, i++, SignalContext::Pc(mc))) return;
    if (SignalContext::Lr(mc) != 0 && !DumpFrame(self, i++, SignalContext::Lr(mc))) return;
    uintptr_t fp = SignalContext::Fp(mc);
    while (i < 40 && fp != 0 && (fp & 0xF) == 0) {
        const uintptr_t* rec = reinterpret_cast<const uintptr_t*>(fp);  // CAST-OK: crash_walk
        if (!Readable(rec, 2 * sizeof(uintptr_t))) break;
        const uintptr_t next = rec[0];
        const uintptr_t ret = rec[1];
        if (ret == 0) break;
        if (!DumpFrame(self, i++, ret)) return;
        if (next <= fp) break;
        fp = next;
    }
}

void DumpSignalHandler(int, siginfo_t*, void* ucv) {
    const int saved = errno;
    g_dumpHandlers.fetch_add(1);
    const pid_t self = CurrentTid();
    if (g_dumpTarget.load() == self) {
        const ucontext_t* uc = static_cast<const ucontext_t*>(ucv);  // CAST-OK: posix (sigaction's void* is the ucontext_t)
        DumpInterruptedStack(self, uc->uc_mcontext);
        pid_t expected = self;
        if (g_dumpTarget.compare_exchange_strong(expected, 0)) sem_post(&g_dumpDone);
    }
    g_dumpHandlers.fetch_sub(1);
    errno = saved;
}

void WriteHangDump(const char* phase, unsigned uptimeSec, unsigned frame,
                   unsigned staleSec, unsigned budgetSec) {
    const int hangLog = open("standalone_hang.log", O_WRONLY | O_CREAT | O_TRUNC | O_SYNC | O_CLOEXEC, 0644);
    if (hangLog < 0) return;
    g_hangLog.store(hangLog);

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
            const pid_t tid = static_cast<pid_t>(ClampedAtoi(e->d_name));
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
            g_dumpTarget.store(tid);
            if (syscall(SYS_tgkill, pid, tid, kDumpSignal) != 0) {
                const int err = errno;
                g_dumpTarget.store(0);
                HangLogf("  (tgkill failed, errno=%d)\n", err);
                continue;
            }
            timespec deadline;
            clock_gettime(CLOCK_REALTIME, &deadline);
            deadline.tv_sec += 1;
            int waited;
            do {
                waited = sem_timedwait(&g_dumpDone, &deadline);
            } while (waited != 0 && errno == EINTR);
            if (waited != 0) {
                pid_t expected = tid;
                if (g_dumpTarget.compare_exchange_strong(expected, 0)) {
                    HangLogRaw("  (no answer within 1s: blocked with the signal masked?)\n");
                } else {
                    // The handler finished as the second ran out: take its post now, so it
                    // is not left for the next thread.
                    while (sem_wait(&g_dumpDone) != 0 && errno == EINTR) {
                    }
                }
            }
        }
        closedir(tasks);
    }

    HangLogRaw("\n=== END HANG ===\n");
    fsync(hangLog);
    g_hangLog.store(-1);
    // A handler still running may have read the descriptor before the store above: leave
    // it open (one descriptor per dump) rather than let that handler's next write land in
    // whatever reuses the number, such as the next standalone_boot_state.txt.
    if (g_dumpHandlers.load() == 0) close(hangLog);
}

void* WatchdogThread(void*) {
    const char* lastState = BootStateName(BootStateId::None);
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
        if (quitting && std::strcmp(lastState, BootStateName(BootStateId::Quitting)) != 0 &&
            std::strcmp(lastState, BootStateName(BootStateId::Hung)) != 0) {
            lastMoveMs   = now;
            hungReported = false;
        }
        const unsigned staleSec = static_cast<unsigned>((now - lastMoveMs) / 1000);

        const char* phase = quitting
            ? BootStateName(BootStateId::Quitting)
            : (g_startupComplete.load() ? BootStateName(BootStateId::Running)
                                        : BootStateName(BootStateId::Booting));
        const unsigned budget = quitting ? kHungQuittingSec : kHungRunningSec;
        const bool hung = staleSec >= budget;
        const char* state = hung ? BootStateName(BootStateId::Hung) : phase;

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
        if (changed && std::strcmp(state, BootStateName(BootStateId::Running)) == 0) {
            // CPULIMIT: the process mask and core counts ride on the RUNNING line, so
            // every run (and every stress A/B leg) proves its sys.cpu_limit.
            char cpu[192];
            Cpu::Describe(Cpu::Get(), cpu, sizeof(cpu));
            std::fprintf(stderr, "[BootState] RUNNING (Engine::Startup returned at %us) cpu %s\n",
                         uptimeSec, cpu);
            std::fflush(stderr);
        }
        if (changed && std::strcmp(state, BootStateName(BootStateId::Quitting)) == 0) {
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
char g_exitState[32]   = "EXITED";   // BootStateName(BootStateId::Exited)
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
    WriteStateFile(BootStateName(BootStateId::Booting), 0, 0, 0, "-");

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
