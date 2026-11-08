// ============================================================================
// src/render/vulkan/async_gpu_create/async_create.cpp -- ext.async_gpu_create, the Vulkan side
// (docs/extensions/handins/ASYNCGPUCREATE.md). The contract is in async_create.h.
// ============================================================================
// NOT RETAIL (P4). The DX11 body's machinery (src/render/dx11/async_gpu_create/async_create.cpp): one
// FIFO of jobs, a small pool of worker threads made at the first deferred create and never destroyed,
// the set of objects with a job pending, back-pressure on the bytes held. Every lock is this file's one
// mutex; no Vulkan call is made while it is held.
// ============================================================================
#include "async_gpu_create/async_create.h"

#include "vulkan_entry.h"

#include "async_gpu_create/async_gpu_create_backend.h"
#include "system/annotated_mutex.h"   // Sys::Mutex / Sys::MutexLock (clang -Wthread-safety)

#include <algorithm>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <mutex>
#include <set>
#include <thread>
#include <unordered_set>
#include <utility>

namespace VideoVulkan {
namespace AsyncCreate {

std::atomic<int64_t> g_pending{0};

namespace {

constexpr uint64_t kMaxBytesInFlight = 256ull << 20;

struct Job {
    uint64_t seq = 0;
    const void* obj = nullptr;
    uint64_t bytes = 0;
    std::function<void()> run;
};

// NOT RETAIL (UB fix, review F-069): IMMORTAL, made at static initialisation and never destroyed. The
// workers are detached and stay parked in g_workCv.wait for the rest of the run; as namespace-scope
// objects, exit() destroyed this state under them, and destroying a condition variable that has
// blocked waiters is undefined ([thread.condition.condvar]): glibc's pthread_cond_destroy waits for
// them forever, so the process hung in exit(). Same rule as src/platform/common/event_std.cpp.
// GUARDED_BY: checked by clang's thread-safety analysis (system/thread_annotations.h); g_pending and
// g_requested are the lock-free fast paths.
Sys::Mutex& g_mutex = *new Sys::Mutex;
std::condition_variable& g_workCv = *new std::condition_variable;  // a job was queued
// a job finished (Await / Drain / back-pressure)
std::condition_variable& g_doneCv = *new std::condition_variable;
std::deque<Job>& g_queue GUARDED_BY(g_mutex) = *new std::deque<Job>;
std::unordered_set<const void*>& g_pendingObjs GUARDED_BY(g_mutex) = *new std::unordered_set<const void*>;
std::set<uint64_t>& g_pendingSeqs GUARDED_BY(g_mutex) = *new std::set<uint64_t>;
uint64_t g_nextSeq GUARDED_BY(g_mutex) = 0;
uint64_t g_bytesHeld GUARDED_BY(g_mutex) = 0;
int g_waiters GUARDED_BY(g_mutex) = 0;
int g_workers GUARDED_BY(g_mutex) = 0;
std::atomic<bool> g_requested{false};
// The run's totals (under g_mutex), and what the last load-end line printed.
int64_t g_jobsTotal GUARDED_BY(g_mutex) = 0;
uint64_t g_bytesTotal GUARDED_BY(g_mutex) = 0;
int64_t g_jobsPrinted GUARDED_BY(g_mutex) = 0;

thread_local bool t_deferring = false;

void WorkerMain() {
    for (;;) {
        Job j;
        {
            Sys::MutexLock lock(g_mutex);
            g_workCv.wait(lock.Native(), []() REQUIRES(g_mutex) { return !g_queue.empty(); });
            j = std::move(g_queue.front());
            g_queue.pop_front();
        }
        j.run();
        j.run = nullptr;
        {
            Sys::MutexLock lock(g_mutex);
            g_pendingObjs.erase(j.obj);
            g_pendingSeqs.erase(j.seq);
            g_bytesHeld -= j.bytes;
            g_pending.fetch_sub(1, std::memory_order_release);
            if (g_waiters != 0) g_doneCv.notify_all();
        }
    }
}

// The DX11 count (handins/ASYNCGPUCREATE.md §3): two at most, one on a CPU with fewer than eight threads.
int WorkerCount() {
    const int hw = static_cast<int>(std::thread::hardware_concurrency());
    return std::max(1, std::min(2, hw / 4));
}

}  // namespace

void Begin() {
    t_deferring = g_requested.load(std::memory_order_acquire) && DeviceLive();
}

void End() {
    if (t_deferring) Drain();
    t_deferring = false;
    Sys::MutexLock lock(g_mutex);
    if (g_jobsTotal == g_jobsPrinted) return;
    g_jobsPrinted = g_jobsTotal;
    std::fprintf(stderr, "[AsyncGpuCreate] load end (run total): %lld creates on the workers (%.1f MB of payload)\n",
                 static_cast<long long>(g_jobsTotal), static_cast<double>(g_bytesTotal) / (1024.0 * 1024.0));
}

bool Deferring() {
    return t_deferring;
}

void Drain() {
    if (g_pending.load(std::memory_order_acquire) == 0) return;
    Sys::MutexLock lock(g_mutex);
    const uint64_t ticket = g_nextSeq;
    ++g_waiters;
    g_doneCv.wait(lock.Native(),
                  [&]() REQUIRES(g_mutex) { return g_pendingSeqs.empty() || *g_pendingSeqs.begin() >= ticket; });
    --g_waiters;
}

void AwaitSlow(const void* obj) {
    Sys::MutexLock lock(g_mutex);
    if (g_pendingObjs.find(obj) == g_pendingObjs.end()) return;
    ++g_waiters;
    g_doneCv.wait(lock.Native(), [&]() REQUIRES(g_mutex) { return g_pendingObjs.find(obj) == g_pendingObjs.end(); });
    --g_waiters;
}

void Enqueue(const void* obj, uint64_t bytes, std::function<void()> run) {
    Sys::MutexLock lock(g_mutex);
    if (g_workers == 0) {
        g_workers = WorkerCount();
        for (int i = 0; i < g_workers; ++i)
            std::thread(WorkerMain).detach();
        std::fprintf(stderr, "[AsyncGpuCreate] Vulkan: %d worker thread(s)\n", g_workers);
    }
    // Back-pressure: wait for room unless nothing is held (one oversized job always goes through).
    if (g_bytesHeld != 0 && g_bytesHeld + bytes > kMaxBytesInFlight) {
        ++g_waiters;
        g_doneCv.wait(lock.Native(),
                      [&]() REQUIRES(g_mutex) { return g_bytesHeld == 0 || g_bytesHeld + bytes <= kMaxBytesInFlight; });
        --g_waiters;
    }
    ++g_jobsTotal;
    g_bytesTotal += bytes;
    Job j;
    j.seq = g_nextSeq++;
    j.obj = obj;
    j.bytes = bytes;
    j.run = std::move(run);
    g_bytesHeld += bytes;
    g_pendingObjs.insert(obj);
    g_pendingSeqs.insert(j.seq);
    g_pending.fetch_add(1, std::memory_order_release);
    g_queue.push_back(std::move(j));
    g_workCv.notify_one();
}

}  // namespace AsyncCreate
}  // namespace VideoVulkan

// ---- the backend-neutral interface (async_gpu_create_backend.h) -----------------------------------
namespace AsyncGpuCreateBackend {
BANISHED_RENDER_IMPL_BEGIN

void Request(bool on) {
    VideoVulkan::AsyncCreate::g_requested.store(on, std::memory_order_release);
}

// Vulkan creates are free-threaded on every device (async_create.h): known once a device exists.
bool Supported() {
    return VideoVulkan::DeviceLive();
}

BANISHED_RENDER_IMPL_END
}  // namespace AsyncGpuCreateBackend
