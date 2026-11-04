// ============================================================================
// tests/unit/vulkan_async_create_exit_test.cpp -- src/render/vulkan/async_gpu_create/async_create.cpp:
// a normal exit with the ext.async_gpu_create workers parked (review findings F-069, F-065)
// ============================================================================
// BUILT ONLY WITH THE VULKAN BACKEND (BANISHED_RENDER_BACKEND=vulkan), as every tests/unit/vulkan_*
// test. The workers are detached and park on the queue's condition variable for the rest of the run.
// While the queue state had static storage duration, exit() destroyed that condition variable with
// the workers still blocked in it: undefined behaviour ([thread.condition.condvar]), and glibc's
// pthread_cond_destroy waits for the waiters forever, so the process hung in exit().
//
// The POSIX case runs the enqueue in a forked child (the parent never starts the workers, so the
// unit process's own exit is not the thing under test) and fails if the child has not exited within
// the timeout. Windows has no fork, and winpthreads' destroy does not wait, so the hang is not
// observable there: the Windows case checks the in-process enqueue / drain only.
// ============================================================================
#include "unit.h"

#include "render/vulkan/async_gpu_create/async_create.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>

#ifndef _WIN32
#include <csignal>
#include <ctime>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

// The unit build links no Vulkan device (device.cpp): async_create.cpp's Begin and Supported read it.
namespace VideoVulkan {
bool DeviceLive() {
    return false;
}
}  // namespace VideoVulkan

namespace {

// One job through the workers and back. True when the job ran and Drain returned after it.
bool RunOneJob() {
    static int obj = 0;
    std::atomic<int> ran{0};
    VideoVulkan::AsyncCreate::Enqueue(&obj, 16, [&ran] { ran.store(1); });
    VideoVulkan::AsyncCreate::Drain();
    return ran.load() == 1 && VideoVulkan::AsyncCreate::g_pending.load() == 0;
}

}  // namespace

UNIT_TEST(vulkan_async_create_exit_with_parked_workers) {
#ifdef _WIN32
    CHECK(RunOneJob());
#else
    std::fflush(stdout);
    std::fflush(stderr);
    const pid_t pid = fork();
    CHECK(pid >= 0);
    if (pid < 0) return;
    if (pid == 0) {
        // The child: start the workers, leave them parked, exit normally (static destructors run).
        std::exit(RunOneJob() ? 0 : 2);
    }
    int status = 0;
    pid_t done = 0;
    for (int i = 0; i < 1000 && done == 0; ++i) {   // 10 s
        done = waitpid(pid, &status, WNOHANG);
        if (done == 0) {
            const timespec tick = {0, 10 * 1000 * 1000};
            nanosleep(&tick, nullptr);
        }
    }
    if (done == 0) {
        std::printf("  child still running 10 s after the job: hung in exit()\n");
        kill(pid, SIGKILL);
        waitpid(pid, &status, 0);
    }
    CHECK(done == pid);
    CHECK(done == pid && WIFEXITED(status) && WEXITSTATUS(status) == 0);
#endif
}
