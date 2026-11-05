// ============================================================================
// src/engine/system/parallel_for.cpp -- the fork-join pool behind ParallelFor
// ============================================================================
// NOT RETAIL (block WGPAR). parallel_for.h has the contract.
//
// One call:
//   caller   takes the call lock; publishes the job (fn, ctx, range, grain, MXCSR) and
//            bumps the epoch under the pool mutex; wakes every worker; claims chunks
//            from the atomic `next` like any worker; then waits (condition variable)
//            until every worker has checked in for this epoch.
//   worker   wakes on the new epoch, copies the caller's MXCSR, claims chunks until
//            none is left, checks in, parks again.
// Every worker checks in for every epoch, so no worker can still be inside a job when
// the next one is published, and the mutex hand-offs order the job's writes before
// the workers' reads and the workers' writes before the caller returns.
// ============================================================================
#include "system/parallel_for.h"

#include "platform/common/cpu/topology.h"   // Cpu::Get().physical, Cpu::ParallelWorkers (roster.h)
#include "diag/diag_threads.h"  // DiagThreads::Register (role 'J')
#include "platform/common/platform_thread.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>

#if defined(__SSE__) || defined(__x86_64__)
#include <xmmintrin.h>
#define PARALLEL_HAVE_MXCSR 1
#endif

namespace System {

namespace {

constexpr uint32_t kWorkerStack = 0x40000;

thread_local bool tl_inParallel = false;   // inside a job (worker) or inside a call (caller)

inline uint32_t ReadFpState() {
#ifdef PARALLEL_HAVE_MXCSR
    return _mm_getcsr();
#elif defined(__aarch64__)
    uint64_t fpcr;
    __asm__ volatile("mrs %0, fpcr" : "=r"(fpcr));
    return static_cast<uint32_t>(fpcr);
#else
    return 0;
#endif
}

inline void WriteFpState(uint32_t csr) {
#ifdef PARALLEL_HAVE_MXCSR
    _mm_setcsr(csr);
#elif defined(__aarch64__)
    const uint64_t fpcr = csr;
    __asm__ volatile("msr fpcr, %0" : : "r"(fpcr));
#else
    (void)csr;
#endif
}

struct Pool;

struct Worker {
    Pool*                  pool   = nullptr;
    Platform::ThreadHandle thread = nullptr;
    uint64_t               startEpoch = 0;   // the pool's epoch when Start built this thread
};

struct Pool {
    std::mutex callLock;   // one fork-join at a time; also guards building / tearing down

    std::mutex              m;
    std::condition_variable wake;
    std::condition_variable done;
    uint64_t epoch     = 0;   // under m
    int      checkedIn = 0;   // under m
    bool     quit      = false;

    // The job (written by the caller before the epoch bump).
    Parallel::RangeFn fn = nullptr;
    void*    ctx    = nullptr;
    int      begin  = 0;
    int      end    = 0;
    int      grain  = 1;
    int      chunks = 0;
    uint32_t csr    = 0;
    std::atomic<int> next{0};

    int    workers = -1;   // -1 = not built
    Worker w[Parallel::kMaxWorkers];

    ~Pool() { Stop(); }

    void RunChunks() {
        for (;;) {
            const int c = next.fetch_add(1, std::memory_order_relaxed);
            if (c >= chunks) return;
            const int b = begin + c * grain;
            const int e = (end - b > grain) ? b + grain : end;
            fn(ctx, b, e);
        }
    }

    static unsigned int ThreadMain(void* arg) {
        Worker* self = static_cast<Worker*>(arg);   // the thread entry's void* context  // CAST-OK: user_data
        DiagThreads::Register(DiagThreads::kParallel);
        tl_inParallel = true;
        self->pool->Loop(self);
        return 0;
    }

    void Loop(Worker* self) {
        // [EROSIONPAR] Not 0: a pool rebuilt by SetWorkers / after Shutdown starts with the old
        // epoch still set, and a new thread starting from 0 woke at once for that finished job,
        // then checked in -- into the NEXT call's count when it raced that call's publication
        // (a call returned early; another then waited forever). Start records the epoch while
        // callLock holds off every call, so the thread waits for the first job published after it.
        uint64_t seen = self->startEpoch;
        for (;;) {
            {
                std::unique_lock<std::mutex> lk(m);
                wake.wait(lk, [&] { return quit || epoch != seen; });
                if (quit) return;
                seen = epoch;
            }
            DiagThreads::SetBusy(true);   // the sampler's busy bit (review S-034)
            WriteFpState(csr);
            RunChunks();
            DiagThreads::SetBusy(false);
            {
                std::lock_guard<std::mutex> lk(m);
                ++checkedIn;
            }
            done.notify_one();
        }
    }

    // callLock held.
    void Start(int n) {
        if (n < 0) n = 0;
        if (n > Parallel::kMaxWorkers) n = Parallel::kMaxWorkers;
        quit = false;
        workers = 0;
        uint64_t now;
        {
            std::lock_guard<std::mutex> lk(m);
            now = epoch;
        }
        for (int i = 0; i < n; ++i) {
            Worker& wk = w[i];
            wk.pool = this;
            wk.startEpoch = now;
            wk.thread = Platform::ThreadCreateSuspended(&Pool::ThreadMain, &wk, kWorkerStack);
            if (wk.thread == nullptr) break;   // run with the ones we have
            Platform::ThreadSetNormalPriority(wk.thread);
            Platform::ThreadResume(wk.thread);
            ++workers;
        }
    }

    // callLock held (or static destruction).
    void Stop() {
        if (workers <= 0) { workers = -1; return; }
        {
            std::lock_guard<std::mutex> lk(m);
            quit = true;
        }
        wake.notify_all();
        for (int i = 0; i < workers; ++i) {
            Platform::ThreadJoin(w[i].thread);
            Platform::ThreadClose(w[i].thread);
            w[i].thread = nullptr;
        }
        workers = -1;
    }

    // callLock held.
    void Ensure() {
        if (workers < 0) Start(Cpu::ParallelWorkers(Cpu::Get().physical));
    }
};

Pool& ThePool() {
    static Pool pool;   // destroyed at exit: ~Pool joins the workers
    return pool;
}

}  // namespace

namespace Parallel {

int Workers() {
    Pool& p = ThePool();
    std::lock_guard<std::mutex> lk(p.callLock);
    p.Ensure();
    return p.workers;
}

void SetWorkers(int workers) {
    Pool& p = ThePool();
    std::lock_guard<std::mutex> lk(p.callLock);
    p.Stop();
    p.Start(workers);
}

void Shutdown() {
    Pool& p = ThePool();
    std::lock_guard<std::mutex> lk(p.callLock);
    p.Stop();
}

}  // namespace Parallel

void ParallelForRaw(int begin, int end, int grain, Parallel::RangeFn fn, void* ctx) {
    if (end <= begin) return;
    if (tl_inParallel) { fn(ctx, begin, end); return; }   // nested: inline

    Pool& p = ThePool();
    std::lock_guard<std::mutex> call(p.callLock);
    p.Ensure();

    const int n = end - begin;
    const int threads = p.workers + 1;
    if (grain <= 0) {
        const int target = threads * 4;
        grain = (n + target - 1) / target;
        if (grain < 1) grain = 1;
    }
    const int chunks = (n + grain - 1) / grain;
    if (p.workers == 0 || chunks <= 1) {
        tl_inParallel = true;
        fn(ctx, begin, end);
        tl_inParallel = false;
        return;
    }

    p.fn     = fn;
    p.ctx    = ctx;
    p.begin  = begin;
    p.end    = end;
    p.grain  = grain;
    p.chunks = chunks;
    p.csr    = ReadFpState();
    p.next.store(0, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lk(p.m);   // publishes the job to every worker
        p.checkedIn = 0;
        ++p.epoch;
    }
    p.wake.notify_all();

    tl_inParallel = true;
    p.RunChunks();
    tl_inParallel = false;

    std::unique_lock<std::mutex> lk(p.m);
    p.done.wait(lk, [&] { return p.checkedIn == p.workers; });
}

}  // namespace System
