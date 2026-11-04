// ============================================================================
// tests/unit/diag_posix_crash_handler_test.cpp -- the Linux crash handler on worker threads
// ============================================================================
// Linux x86-64 only (empty elsewhere): src/diag/posix/crash_handler_posix.cpp reads the
// x86-64 gregs; macOS and Android have their own twins. The handler file is compiled
// into this test (the unit link does not carry it); its two pure-virtual entry points are
// renamed so they do not replace the unit's own (posix_crt_stubs.cpp, libstdc++), and the
// redzone heap dump it calls is stubbed. Each case forks: the child installs the handler
// and crashes, the parent reads the exit status and standalone_crash.log.
//
//   * Review F-290: two threads that fault together. The second must wait for the first
//     dump instead of ending the process in the middle of it, so the log is whole: one
//     "=== STANDALONE CRASH ===", a "Stack trace:" and "=== END CRASH ===", exit 3.
//   * Review F-277: a stack overflow on a Platform:: thread. The alternate signal stack
//     must exist on that thread too, or the kernel kills the process with SIGSEGV and
//     no log is written.
// ============================================================================

#if defined(__linux__) && !defined(__ANDROID__) && defined(__x86_64__)

#define _purecall unit_crash_handler_purecall
#define __cxa_pure_virtual unit_crash_handler_cxa_pure_virtual
#include "../../src/diag/posix/crash_handler_posix.cpp"
#undef _purecall
#undef __cxa_pure_virtual

#include "unit.h"

#include "platform_thread.h"

#include <pthread.h>
#include <sys/wait.h>

#include <atomic>
#include <cstdio>
#include <string>

// The unit link has no redzone allocator (src/diag/posix/redzone_alloc_posix.cpp).
extern "C" void RedzoneAlloc_SetDumpSink(void (*)(const char*)) {}
extern "C" void RedzoneAlloc_HeapDump(const char*) {}

namespace {

std::atomic<int> g_chArrived{0};

void FaultNow() {
    volatile int* volatile p = nullptr;
    *p = 1;
}

void* FaultTogether(void*) {
    g_chArrived.fetch_add(1);
    while (g_chArrived.load() < 2) {
    }
    FaultNow();
    return nullptr;
}

int Recurse(int depth) {
    volatile char pad[512];
    pad[0] = static_cast<char>(depth);
    return Recurse(depth + 1) + pad[0];
}

unsigned int OverflowEntry(void*) {
    return static_cast<unsigned int>(Recurse(0));
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

// Runs `crash` in a forked child with the handler installed, in a fresh directory.
// Returns the wait status; `log` gets standalone_crash.log.
int RunChild(void (*crash)(), std::string* log) {
    char dir[] = "/tmp/openba_crash_handler_XXXXXX";
    if (mkdtemp(dir) == nullptr) return -1;
    std::fflush(stdout);
    std::fflush(stderr);
    const pid_t child = fork();
    if (child == 0) {
        if (chdir(dir) != 0) _exit(90);
        CrashHandler_OpenLog();
        CrashHandler_Install();
        crash();
        _exit(91);   // the crash did not happen
    }
    int status = -1;
    if (child > 0) waitpid(child, &status, 0);
    const std::string path = std::string(dir) + "/standalone_crash.log";
    *log = ReadAll(path.c_str());
    std::remove(path.c_str());
    rmdir(dir);
    return status;
}

void TwoThreadsFault() {
    g_chArrived.store(0);
    pthread_t a;
    pthread_t b;
    pthread_create(&a, nullptr, &FaultTogether, nullptr);
    pthread_create(&b, nullptr, &FaultTogether, nullptr);
    pthread_join(a, nullptr);
    pthread_join(b, nullptr);
}

void WorkerOverflows() {
    Platform::ThreadHandle t = Platform::ThreadCreateSuspended(&OverflowEntry, nullptr, 0x10000);
    Platform::ThreadResume(t);
    Platform::ThreadJoin(t);
}

}  // namespace

UNIT_TEST(diag_posix_crash_handler_second_faulting_thread_waits_for_the_dump) {
    for (int run = 0; run < 8; ++run) {
        std::string log;
        const int status = RunChild(&TwoThreadsFault, &log);
        const bool exited3 = WIFEXITED(status) && WEXITSTATUS(status) == 3;
        const bool whole = Count(log, "=== STANDALONE CRASH ===") == 1 && Count(log, "Stack trace:") == 1 &&
                           Count(log, "=== END CRASH ===") == 1;
        CHECK(exited3);
        CHECK(whole);
        if (!exited3 || !whole) {
            std::printf("  run %d: status=0x%x\n%s\n", run, static_cast<unsigned>(status), log.c_str());
            return;
        }
    }
}

UNIT_TEST(diag_posix_crash_handler_logs_a_worker_thread_stack_overflow) {
    std::string log;
    const int status = RunChild(&WorkerOverflows, &log);
    const bool exited3 = WIFEXITED(status) && WEXITSTATUS(status) == 3;
    CHECK(exited3);
    CHECK(Count(log, "=== STANDALONE CRASH ===") == 1);
    CHECK(Count(log, "=== END CRASH ===") == 1);
    if (!exited3)
        std::printf("  status=0x%x (signalled=%d sig=%d)\n", static_cast<unsigned>(status),
                    WIFSIGNALED(status) ? 1 : 0, WIFSIGNALED(status) ? WTERMSIG(status) : 0);
}

#endif  // __linux__ && !__ANDROID__ && __x86_64__
