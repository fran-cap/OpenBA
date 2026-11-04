// ============================================================================
// tests/unit/diag_posix_hang_dump_test.cpp -- the POSIX hang dump's per-thread answers
// ============================================================================
// Linux only (empty elsewhere): src/diag/posix/boot_state_watchdog_posix.cpp walks
// /proc/self/task and tgkills each thread; macOS has its own twin and Android its own.
// The watchdog file is compiled into this test (it is not part of the unit link, and its
// hang dump lives in an anonymous namespace), so WriteHangDump runs on the test thread
// with the handler Start() installs, minus the watchdog thread itself.
//
// Review F-276 / F-292 / F-298: a thread that answers the dump signal after its 1 s wait
// timed out must not be counted for the next thread, and must leave no stale semaphore
// post behind for a later dump.
//   * "late" blocks the dump signal, sees it pending, waits 1.5 s, then unblocks: its
//     answer arrives while the dump is waiting on the next thread.
//   * "masked" blocks the signal for the whole dump and answers only after it.
// Both must read "(no answer within 1s" in the log, the masked thread's section must
// carry no frame lines, and the semaphore must hold no post once both have answered.
// ============================================================================

#if defined(__linux__) && !defined(__ANDROID__)

#include "../../src/diag/posix/boot_state_watchdog_posix.cpp"

#include "unit.h"

#include <sys/stat.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <string>

// The unit link has no src/app/options.cpp; the watchdog reads Opt() only in its thread,
// which this test never starts.
const Options& Opt() {
    static Options o;
    return o;
}

namespace {

std::atomic<int> g_hdReady{0};
std::atomic<int> g_hdLateAnswered{0};
std::atomic<int> g_hdReleaseMasked{0};
std::atomic<pid_t> g_hdLateTid{0};
std::atomic<pid_t> g_hdMaskedTid{0};

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

}  // namespace

UNIT_TEST(diag_posix_hang_dump_late_answer_is_not_counted_for_the_next_thread) {
    char cwd[4096];
    if (getcwd(cwd, sizeof(cwd)) == nullptr) {
        CHECK(false);
        return;
    }
    char dir[] = "/tmp/openba_hang_dump_XXXXXX";
    if (mkdtemp(dir) == nullptr) {
        CHECK(false);
        return;
    }
    if (chdir(dir) != 0) {
        CHECK(false);
        return;
    }

    // Start()'s handler setup, without the watchdog thread (this thread plays it).
    BootState::g_mainTid = BootState::CurrentTid();
    void* warm[1];
    backtrace(warm, 1);
    sem_init(&BootState::g_dumpDone, 0, 0);
    struct sigaction sa{};
    sa.sa_handler = &BootState::DumpSignalHandler;
    sa.sa_flags = SA_RESTART;
    sigemptyset(&sa.sa_mask);
    sigaction(BootState::kDumpSignal, &sa, nullptr);

    g_hdReady.store(0);
    g_hdLateAnswered.store(0);
    g_hdReleaseMasked.store(0);
    pthread_t late;
    pthread_t masked;
    pthread_create(&late, nullptr, &LateAnswerer, nullptr);
    pthread_create(&masked, nullptr, &MaskedThread, nullptr);
    while (g_hdReady.load() < 2)
        usleep(1000);

    BootState::WriteHangDump("RUNNING", 30, 7, 21, 20);
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

    std::remove("standalone_hang.log");
    if (chdir(cwd) != 0)
        CHECK(false);
    rmdir(dir);
}

#endif  // __linux__ && !__ANDROID__
