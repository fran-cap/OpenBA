// ============================================================================
// tests/unit/diag_posix_hang_dump_test.cpp -- the POSIX hang dump's per-thread answers
// ============================================================================
// Linux only (empty elsewhere): src/diag/posix/boot_state_watchdog_posix.cpp walks
// /proc/self/task and tgkills each thread; macOS has its own twin and Android its own.
// The watchdog file is compiled into this test (it is not part of the unit link, and its
// hang dump lives in an anonymous namespace), so WriteHangDump runs on the test thread
// with the handler Start() installs, minus the watchdog thread itself. Each case restores
// the signal's previous action and destroys the semaphore before it returns.
//
// Review F-276 / F-292 / F-298: a thread that answers the dump signal after its 1 s wait
// timed out must not be counted for the next thread, must write no line under another
// thread's header, and must leave no stale semaphore post behind for a later dump.
//   * "late" blocks the dump signal, sees it pending, waits 1.5 s, then unblocks: its
//     answer arrives while the dump is waiting on the next thread.
//   * "masked" blocks the signal for the whole dump and answers only after it.
//   * "stalled" takes the signal at once, but its handler's first dladdr() blocks for
//     1.5 s (glibc's dladdr takes the loader lock, which a dlopen running a slow
//     constructor holds: the phase-2 proof hangdump-late-handler). It passed the
//     target test before the stall, so only a test after dladdr() keeps its line out
//     of the next thread's section. The stalled call names its module STALLED_FRAME;
//     that name must appear nowhere in the log.
// ============================================================================

#if defined(__linux__) && !defined(__ANDROID__)

#include <dlfcn.h>

// The watchdog's dladdr() calls go through StallingDladdr (below), which stalls one
// chosen thread's first call. Declared before the include, defined after it.
int StallingDladdr(const void* addr, Dl_info* info);
#define dladdr StallingDladdr
#include "../../src/diag/posix/boot_state_watchdog_posix.cpp"
#undef dladdr

#include "unit.h"

#include <sys/stat.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <string>

// The unit link has no src/app/options.cpp; the watchdog reads Opt() only in its thread,
// which this test never starts. If options.cpp ever joins the unit link, this definition
// becomes a duplicate symbol (an ODR clash): delete it then.
const Options& Opt() {
    static Options o;
    return o;
}

namespace {

std::atomic<int> g_hdReady{0};
std::atomic<int> g_hdLateAnswered{0};
std::atomic<int> g_hdReleaseMasked{0};
std::atomic<int> g_hdReleaseStalled{0};
std::atomic<pid_t> g_hdLateTid{0};
std::atomic<pid_t> g_hdMaskedTid{0};
std::atomic<pid_t> g_hdStalledTid{0};
// 0 armed, 1 inside the stalled dladdr(), 2 that call has returned.
std::atomic<int> g_hdStallState{0};

}  // namespace

int StallingDladdr(const void* addr, Dl_info* info) {
    const pid_t self = BootState::CurrentTid();
    int expected = 0;
    if (self != g_hdStalledTid.load() || !g_hdStallState.compare_exchange_strong(expected, 1))
        return dladdr(addr, info);
    timespec left{1, 500 * 1000 * 1000};   // past the 1 s wait, inside the next thread's
    while (nanosleep(&left, &left) != 0 && errno == EINTR) {
    }
    const int r = dladdr(addr, info);
    if (r == 0) info->dli_fbase = nullptr;
    info->dli_fname = "/STALLED_FRAME";
    info->dli_sname = nullptr;
    g_hdStallState.store(2);
    return 1;
}

namespace {

void BlockDumpSignal(int how) {
    sigset_t s;
    sigemptyset(&s);
    sigaddset(&s, BootState::kDumpSignal);
    pthread_sigmask(how, &s, nullptr);
}

bool DumpSignalPending() {
    sigset_t s;
    sigemptyset(&s);
    sigpending(&s);
    return sigismember(&s, BootState::kDumpSignal) == 1;
}

void* LateAnswerer(void*) {
    BlockDumpSignal(SIG_BLOCK);
    g_hdLateTid.store(BootState::CurrentTid());
    g_hdReady.fetch_add(1);
    while (!DumpSignalPending())
        usleep(1000);
    usleep(1500 * 1000);   // past this thread's 1 s wait, inside the next thread's
    BlockDumpSignal(SIG_UNBLOCK);   // the pending handler runs here
    g_hdLateAnswered.store(1);
    return nullptr;
}

void* MaskedThread(void*) {
    BlockDumpSignal(SIG_BLOCK);
    g_hdMaskedTid.store(BootState::CurrentTid());
    g_hdReady.fetch_add(1);
    while (g_hdReleaseMasked.load() == 0)
        usleep(1000);
    BlockDumpSignal(SIG_UNBLOCK);   // answers after the dump has ended
    return nullptr;
}

void* StalledThread(void*) {
    g_hdStalledTid.store(BootState::CurrentTid());
    g_hdReady.fetch_add(1);
    while (g_hdReleaseStalled.load() == 0)
        usleep(1000);   // the signal is taken here; its handler stalls in dladdr()
    return nullptr;
}

std::string ReadAll(const char* path) {
    std::string out;
    if (FILE* f = std::fopen(path, "rb")) {
        char buf[4096];
        size_t n;
        while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0)
            out.append(buf, n);
        std::fclose(f);
    }
    return out;
}

int Count(const std::string& s, const char* what) {
    int n = 0;
    for (size_t p = s.find(what); p != std::string::npos; p = s.find(what, p + 1))
        ++n;
    return n;
}

// The lines between "--- thread <tid> " and the next section.
std::string Section(const std::string& log, pid_t tid) {
    char head[48];
    std::snprintf(head, sizeof(head), "--- thread %u ", static_cast<unsigned>(tid));
    const size_t a = log.find(head);
    if (a == std::string::npos) return std::string();
    size_t b = log.find("\n--- thread ", a + 1);
    const size_t end = log.find("\n=== END HANG", a + 1);
    if (b == std::string::npos || (end != std::string::npos && end < b)) b = end;
    return log.substr(a, b == std::string::npos ? std::string::npos : b - a);
}

// A temporary working directory, and Start()'s handler setup without the watchdog thread
// (the test thread plays it). Leave() undoes both.
struct DumpScene {
    char cwd[4096];
    char dir[64];
    struct sigaction previous{};
    bool ok = false;

    bool Enter() {
        if (getcwd(cwd, sizeof(cwd)) == nullptr) return false;
        std::snprintf(dir, sizeof(dir), "/tmp/openba_hang_dump_XXXXXX");
        if (mkdtemp(dir) == nullptr) return false;
        if (chdir(dir) != 0) return false;

        BootState::g_mainTid = BootState::CurrentTid();
        void* warm[1];
        backtrace(warm, 1);
        sem_init(&BootState::g_dumpDone, 0, 0);
        struct sigaction sa{};
        sa.sa_handler = &BootState::DumpSignalHandler;
        sa.sa_flags = SA_RESTART;
        sigemptyset(&sa.sa_mask);
        sigaction(BootState::kDumpSignal, &sa, &previous);
        ok = true;
        return true;
    }

    // Every handler has returned and every thread that took the signal is joined.
    void Leave() {
        if (ok) {
            sigaction(BootState::kDumpSignal, &previous, nullptr);
            sem_destroy(&BootState::g_dumpDone);
        }
        std::remove("standalone_hang.log");
        if (chdir(cwd) != 0) std::printf("chdir back to %s failed\n", cwd);
        rmdir(dir);
    }
};

}  // namespace

UNIT_TEST(diag_posix_hang_dump_late_answer_is_not_counted_for_the_next_thread) {
    DumpScene scene;
    if (!scene.Enter()) {
        CHECK(false);
        return;
    }

    g_hdReady.store(0);
    g_hdLateAnswered.store(0);
    g_hdReleaseMasked.store(0);
    pthread_t late;
    pthread_t masked;
    pthread_create(&late, nullptr, &LateAnswerer, nullptr);
    pthread_create(&masked, nullptr, &MaskedThread, nullptr);
    while (g_hdReady.load() < 2)
        usleep(1000);

    BootState::WriteHangDump(BootState::BootStateName(BootState::BootStateId::Running), 30, 7, 21, 20);
    while (g_hdLateAnswered.load() == 0)
        usleep(1000);
    g_hdReleaseMasked.store(1);
    pthread_join(late, nullptr);
    pthread_join(masked, nullptr);
    usleep(100 * 1000);   // a late handler's post, if any, has landed

    int stalePosts = -1;
    sem_getvalue(&BootState::g_dumpDone, &stalePosts);
    const std::string log = ReadAll("standalone_hang.log");
    const std::string maskedSection = Section(log, g_hdMaskedTid.load());
    const std::string lateSection = Section(log, g_hdLateTid.load());

    CHECK(Count(log, "=== END HANG ===") == 1);
    CHECK(Count(lateSection, "(no answer within 1s") == 1);
    CHECK(Count(maskedSection, "(no answer within 1s") == 1);
    CHECK(Count(maskedSection, "\n  #") == 0);   // no other thread's frames under its header
    CHECK_EQ(stalePosts, 0);                     // nothing left over for the next dump
    if (Count(maskedSection, "\n  #") != 0 || stalePosts != 0)
        std::printf("%s\n", log.c_str());

    scene.Leave();
}

UNIT_TEST(diag_posix_hang_dump_handler_stalled_in_dladdr_writes_no_late_line) {
    DumpScene scene;
    if (!scene.Enter()) {
        CHECK(false);
        return;
    }

    g_hdReady.store(0);
    g_hdReleaseMasked.store(0);
    g_hdReleaseStalled.store(0);
    g_hdStallState.store(0);
    // Created first, so /proc/self/task lists it before the masked thread, whose 1 s wait
    // is still running (the log still open) when the stalled dladdr() returns.
    pthread_t stalled;
    pthread_t masked;
    pthread_create(&stalled, nullptr, &StalledThread, nullptr);
    while (g_hdReady.load() < 1)
        usleep(1000);
    pthread_create(&masked, nullptr, &MaskedThread, nullptr);
    while (g_hdReady.load() < 2)
        usleep(1000);

    BootState::WriteHangDump(BootState::BootStateName(BootState::BootStateId::Running), 30, 7, 21, 20);
    while (g_hdStallState.load() != 2)
        usleep(1000);
    while (BootState::g_dumpHandlers.load() != 0)
        usleep(1000);
    g_hdReleaseMasked.store(1);
    g_hdReleaseStalled.store(1);
    pthread_join(stalled, nullptr);
    pthread_join(masked, nullptr);
    usleep(100 * 1000);   // a late handler's post, if any, has landed

    int stalePosts = -1;
    sem_getvalue(&BootState::g_dumpDone, &stalePosts);
    const std::string log = ReadAll("standalone_hang.log");
    const std::string stalledSection = Section(log, g_hdStalledTid.load());

    CHECK(Count(log, "=== END HANG ===") == 1);
    CHECK(Count(stalledSection, "(no answer within 1s") == 1);
    CHECK(Count(log, "STALLED_FRAME") == 0);   // not under the next header, nor after END HANG
    CHECK_EQ(stalePosts, 0);
    if (Count(log, "STALLED_FRAME") != 0 || stalePosts != 0)
        std::printf("%s\n", log.c_str());

    scene.Leave();
}

#endif  // __linux__ && !__ANDROID__
