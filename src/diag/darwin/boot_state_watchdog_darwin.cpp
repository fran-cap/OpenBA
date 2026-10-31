// src/diag/darwin/boot_state_watchdog_darwin.cpp
// The Darwin twin of src/diag/posix/boot_state_watchdog_posix.cpp. It publishes the same
// standalone_boot_state.txt (STATE / pid / uptimeSec / frame / stale / detail), with the
// same states, the same frame heartbeat, the same two hang budgets (20 s RUNNING, 45 s
// QUITTING) and the same one-second interval, so tools/boot_state.sh and every route that
// polls it read a macOS run as they read a Linux one. stale= is the real seconds since the
// frame counter last moved, and a run that stops advancing reaches HUNG and, when
// unattended, ends with _exit(4).
//
// WHAT DIFFERS FROM THE POSIX FILE
//  * The hang dump has no /proc/self/task and no tgkill. It walks the process's Mach
//    threads instead: task_threads(), then for each other thread thread_suspend,
//    thread_get_state, a frame-pointer walk of its stack (arm64 always keeps x29 as the
//    frame pointer; every pointer is read through mach_vm_read_overwrite, so a torn stack
//    cannot fault the watchdog) and thread_resume. Frames print in the same spelling as
//    the POSIX dump. On a CPU other than arm64 the thread list is written without stacks.
//  * Threads are identified by their Mach port; the main thread is marked by the port
//    recorded in Start().

#include "boot_state_watchdog.h"

#include "options.h"
#include "platform/common/cpu/topology.h"   // CPULIMIT: the [Cpu] facts on the RUNNING line

#include <dlfcn.h>
#include <fcntl.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace BootState {

namespace {

std::atomic<int> g_startupComplete{ 0 };
std::atomic<int> g_quitting{ 0 };
std::atomic<uint32_t> g_frames{ 0 };
std::atomic<int> g_stop{ 0 };
pthread_t g_thread;
bool g_threadStarted = false;
uint64_t g_startMs = 0;
mach_port_t g_mainThread = MACH_PORT_NULL;

// The same two budgets as src/diag/win/boot_state_watchdog.cpp (tools/boot_state.sh
// reads them from that file).
const unsigned kHungRunningSec = 20;   // BOOTING/RUNNING: 20s with no new frame
const unsigned kHungQuittingSec = 45;   // QUITTING: teardown renders no frames at all

uint64_t NowMs() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1000u + static_cast<uint64_t>(ts.tv_nsec) / 1000000u;
}

mach_port_t CurrentThread() {
    const mach_port_t self = mach_thread_self();
    mach_port_deallocate(mach_task_self(), self);   // the port number stays valid for a live thread
    return self;
}

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
int g_hangLog = -1;

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

// One stack frame, in the POSIX dump's spelling.
void DumpFrame(int index, void* pc) {
    char line[256];
    Dl_info info;
    if (dladdr(pc, &info) != 0 && info.dli_fname != nullptr) {
        const char* nm = std::strrchr(info.dli_fname, '/');
        const uintptr_t off = reinterpret_cast<uintptr_t>(pc) -  // CAST-OK: crash_walk
                              reinterpret_cast<uintptr_t>(info.dli_fbase);  // CAST-OK: crash_walk
        if (info.dli_sname != nullptr) {
            const uintptr_t soff = reinterpret_cast<uintptr_t>(pc) -  // CAST-OK: crash_walk
                                   reinterpret_cast<uintptr_t>(info.dli_saddr);  // CAST-OK: crash_walk
            std::snprintf(line, sizeof(line), "  #%-2d %-22s + 0x%-8llX  (%s+0x%llX)\n", index,
                          nm ? nm + 1 : info.dli_fname, static_cast<unsigned long long>(off),
                          info.dli_sname, static_cast<unsigned long long>(soff));
        } else {
            std::snprintf(line, sizeof(line), "  #%-2d %-22s + 0x%-8llX\n", index,
                          nm ? nm + 1 : info.dli_fname, static_cast<unsigned long long>(off));
        }
    } else {
        std::snprintf(line, sizeof(line), "  #%-2d ???                    @ %p\n", index, pc);
    }
    HangLogRaw(line);
}

// Reads one pointer-sized word of the stopped thread's stack without being able to fault.
bool ReadWord(uint64_t address, uint64_t* out) {
    mach_vm_size_t got = 0;
    const kern_return_t rc = mach_vm_read_overwrite(mach_task_self(), address, sizeof(*out),
                                                    reinterpret_cast<mach_vm_address_t>(out), &got);  // CAST-OK: crash_walk
    return rc == KERN_SUCCESS && got == sizeof(*out);
}

// What a stopped thread yields: raw return addresses only. Nothing here may allocate, take
// a lock or call dladdr (dyld's lock) while the thread is suspended: if the suspended
// thread holds malloc's or dyld's lock, the watchdog would wait on it forever and never
// reach _exit(4). Symbolizing (DumpFrame) happens after every thread is resumed.
constexpr int kMaxFrames = 40;
constexpr mach_msg_type_number_t kMaxThreads = 128;

struct Capture {
    thread_act_t thread;
    bool suspended;
    bool stateOk;
    int count;
    uint64_t pcs[kMaxFrames];
};

#if defined(__aarch64__)
// The stopped thread's PC and LR, then the saved (fp, lr) chain, as raw words.
void CaptureThreadStack(Capture& cap) {
    arm_thread_state64_t state;
    mach_msg_type_number_t count = ARM_THREAD_STATE64_COUNT;
    if (thread_get_state(cap.thread, ARM_THREAD_STATE64, reinterpret_cast<thread_state_t>(&state),
                         &count) != KERN_SUCCESS) {
        return;
    }
    cap.stateOk = true;
    cap.pcs[cap.count++] = reinterpret_cast<uint64_t>(arm_thread_state64_get_pc(state));  // CAST-OK: crash_walk
    cap.pcs[cap.count++] = reinterpret_cast<uint64_t>(arm_thread_state64_get_lr(state));  // CAST-OK: crash_walk
    uint64_t fp = arm_thread_state64_get_fp(state);
    while (cap.count < kMaxFrames && fp != 0 && (fp & 7u) == 0) {
        uint64_t nextFp = 0;
        uint64_t ret = 0;
        if (!ReadWord(fp, &nextFp) || !ReadWord(fp + 8, &ret) || ret == 0)
            break;
        cap.pcs[cap.count++] = ret;
        if (nextFp <= fp)
            break;
        fp = nextFp;
    }
}
#else
void CaptureThreadStack(Capture&) {}
#endif

void PrintCapture(const Capture& cap) {
    if (!cap.suspended) {
        HangLogRaw("  (thread_suspend failed)\n");
        return;
    }
#if defined(__aarch64__)
    if (!cap.stateOk) {
        HangLogRaw("  (thread_get_state failed)\n");
        return;
    }
    for (int i = 0; i < cap.count; ++i)
        DumpFrame(i, reinterpret_cast<void*>(cap.pcs[i]));  // CAST-OK: crash_walk
#else
    HangLogRaw("  (stack walk is arm64-only)\n");
#endif
}

void WriteHangDump(const char* phase, unsigned uptimeSec, unsigned frame,
                   unsigned staleSec, unsigned budgetSec) {
    g_hangLog = open("standalone_hang.log", O_WRONLY | O_CREAT | O_TRUNC | O_SYNC | O_CLOEXEC, 0644);
    if (g_hangLog < 0) return;

    HangLogRaw("=== STANDALONE HANG ===\n");
    HangLogf("Phase:       %s\n", phase);
    HangLogf("Pid:         %u   (main thread port %u)\n",
             static_cast<uint32_t>(getpid()), static_cast<uint32_t>(g_mainThread));
    HangLogf("Uptime:      %us\n", uptimeSec);
    HangLogf("Frame:       %u  (main-loop iterations completed)\n", frame);
    HangLogf("Stale:       %us  (budget for this phase: %us)\n", staleSec, budgetSec);
    HangLogRaw(
        "The frame counter stopped advancing. Every thread's stack follows;\n"
        "the MAIN thread is the one that matters. A UI::ShowDialog pump on it\n"
        "means an unanswered in-game modal, not a deadlock (see\n"
        "src/diag/win/boot_state_watchdog.cpp).\n\n");

    const mach_port_t selfThread = CurrentThread();
    thread_act_array_t threads = nullptr;
    mach_msg_type_number_t threadCount = 0;
    if (task_threads(mach_task_self(), &threads, &threadCount) != KERN_SUCCESS) {
        HangLogRaw("(task_threads failed -- no thread list)\n");
    } else {
        // Phase 1: raw capture, one thread at a time, each resumed before the next. No
        // formatting, allocation or dladdr in this loop.
        static Capture captures[kMaxThreads];
        const mach_msg_type_number_t used = threadCount < kMaxThreads ? threadCount : kMaxThreads;
        for (mach_msg_type_number_t i = 0; i < used; ++i) {
            Capture& cap = captures[i];
            cap.thread = threads[i];
            cap.suspended = false;
            cap.stateOk = false;
            cap.count = 0;
            if (cap.thread == selfThread)
                continue;
            if (thread_suspend(cap.thread) != KERN_SUCCESS)
                continue;
            cap.suspended = true;
            CaptureThreadStack(cap);
            thread_resume(cap.thread);
        }
        // Phase 2: every thread runs again; now it is safe to symbolize and format.
        for (mach_msg_type_number_t i = 0; i < used; ++i) {
            const thread_act_t thread = captures[i].thread;
            const bool isMain = thread == g_mainThread;
            const bool isSelf = thread == selfThread;
            HangLogf("\n--- thread %u%s%s ---\n", static_cast<uint32_t>(thread),
                     isMain ? "  [MAIN THREAD]" : "",
                     isSelf ? "  [watchdog -- this thread]" : "");
            if (isSelf)
                HangLogRaw("  (the watchdog itself; not interesting)\n");
            else
                PrintCapture(captures[i]);
        }
        for (mach_msg_type_number_t i = 0; i < threadCount; ++i)
            mach_port_deallocate(mach_task_self(), threads[i]);
        vm_deallocate(mach_task_self(), reinterpret_cast<vm_address_t>(threads),  // CAST-OK: crash_walk
                      threadCount * sizeof(thread_act_t));
    }

    HangLogRaw("\n=== END HANG ===\n");
    fsync(g_hangLog);
    close(g_hangLog);
    g_hangLog = -1;
}

void* WatchdogThread(void*) {
    pthread_setname_np("BootState");   // Darwin names the calling thread
    const char* lastState = "";
    uint32_t lastFrame = 0xFFFFFFFFu;
    uint64_t lastMoveMs = NowMs();
    bool hungReported = false;

    while (g_stop.load() == 0) {
        const uint64_t now = NowMs();
        const unsigned uptimeSec = static_cast<unsigned>((now - g_startMs) / 1000);

        const uint32_t frame = g_frames.load();
        const bool quitting = g_quitting.load() != 0;

        if (frame != lastFrame) {
            lastFrame = frame;
            lastMoveMs = now;
            hungReported = false;
        }
        if (quitting && std::strcmp(lastState, "QUITTING") != 0 &&
            std::strcmp(lastState, "HUNG") != 0) {
            lastMoveMs = now;
            hungReported = false;
        }
        const unsigned staleSec = static_cast<unsigned>((now - lastMoveMs) / 1000);

        const char* phase = quitting ? "QUITTING"
                                     : (g_startupComplete.load() ? "RUNNING" : "BOOTING");
        const unsigned budget = quitting ? kHungQuittingSec : kHungRunningSec;
        const bool hung = staleSec >= budget;
        const char* state = hung ? "HUNG" : phase;

        WriteStateFile(state, uptimeSec, frame, staleSec, hung ? phase : "-");

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
            std::fprintf(stderr,
                         "[BootState] QUITTING at %us after %d frames "
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
    g_mainThread = CurrentThread();   // Start() is called from main
    WriteStateFile("BOOTING", 0, 0, 0, "-");

    const int rc = pthread_create(&g_thread, nullptr, &WatchdogThread, nullptr);
    if (rc != 0) {
        std::fprintf(stderr,
                     "[BootState] WARNING: watchdog thread failed to start "
                     "(pthread_create=%d) -- boot state will not be published\n",
                     rc);
        std::fflush(stderr);
        return;
    }
    g_threadStarted = true;
}

void MarkStartupComplete() {
    g_startupComplete.store(1);
}

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
