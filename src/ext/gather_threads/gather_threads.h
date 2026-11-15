// ============================================================================
// src/ext/gather_threads/gather_threads.h -- ext.gather_threads (class A): the helper pool
// ============================================================================
// NOT RETAIL. docs/extensions/handins/CULLPAR.md. Standard C++ plus the platform
// thread / time seams (src/platform/common); the unit tests link gather_threads.cpp.
// The row shares its walk with ext.draw_gather: the parallel walk that drives this pool is in
// src/ext/draw_gather/gather_walk.h (ParWalk / ParGather), its driver ParallelGather and the pool's
// lifetime (SyncThreads) in draw_gather.cpp, beside the sequential walk they mirror.
//
// The frame helpers of the parallel cull (THREADRESEARCH §2.3-§2.5). NOT the path
// workers' pool (§2.4: one FIFO of millisecond jobs and a Barrier that waits for all
// of them): persistent threads of their own, created once when the row turns on.
//
//   parked    the helpers wait on a condition variable (no CPU).
//   Wake()    ONCE per frame, before the first gather pass of the frame (the
//             gather calls it when it finds the pool parked).
//   awake     the helpers SPIN on the task ticket with a pause instruction, never a
//             syscall, through every gather pass of the frame (pass 1 of
//             Renderer::Draw): a hand-off costs ~1 us instead of an event's 5-30 us.
//             P7: a spin that finds no task for kSpinMicros (50 us) sleeps on the
//             condition variable until the next Publish (or Wake / quit); Publish
//             notifies only when a helper sleeps. On the S25 the unbounded spin was
//             10% of all the process's CPU samples (docs/android/PERF_P7.md §3.3).
//   Park()    at the end of pass 1 (Scene::_extDrawPass1End, set by draw_gather.cpp):
//             the spin is bounded by the frame.
//
// A pass: the caller Publish()es `count` tasks, does work of its own, then Finish()
// claims whatever is left and waits for the tasks a helper took. Tasks are claimed
// from one atomic ticket (count | next index), so the caller never waits while a task
// is unclaimed, and a helper that is late (preempted, still parked) only loses work.
// Main-thread only: Publish / Finish / Wake / Park and the ctor / dtor.
// ============================================================================
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>

#include "platform/common/cpu/roster.h"
#include "platform/common/platform_thread.h"

namespace Ext {
namespace GatherThreads {

class HelperPool {
public:
    // worker 0 = the calling (main) thread, 1..Helpers() = the helpers.
    using TaskFn = void (*)(void* ctx, int32_t task, int32_t worker);

    static constexpr int kMaxTasks = 0xffff;

    // Starts min(max(helpers, 0), Cpu::kMaxCullHelpers) threads at NORMAL priority,
    // parked. 0 = no thread (Finish runs every task on the caller).
    explicit HelperPool(int helpers);
    ~HelperPool();                       // parks, stops and joins the helpers
    HelperPool(const HelperPool&) = delete;
    HelperPool& operator=(const HelperPool&) = delete;

    int  Helpers() const { return _helpers; }
    bool Awake() const { return _awake.load(std::memory_order_relaxed); }
    void Wake();                         // no-op when awake
    void Park();                         // no-op when parked

    // Tasks [0, count) become claimable; `fn` / `ctx` stay valid until Finish returns.
    void Publish(TaskFn fn, void* ctx, int32_t count);

    struct PassResult {
        int32_t tasks       = 0;   // published
        int32_t callerTasks = 0;   // run by worker 0
        int64_t waitTicks   = 0;   // QPC ticks the caller spent waiting for helper tasks
    };
    // Runs the unclaimed tasks on the caller, then waits for every task.
    PassResult Finish();

    uint64_t Wakes() const { return _wakes; }   // Wake()s that woke the pool

private:
    struct Helper {
        HelperPool*           pool = nullptr;
        int32_t               worker = 0;
        Platform::ThreadHandle thread = nullptr;
    };
    static unsigned int ThreadMain(void* arg);
    void HelperLoop(int32_t worker);
    bool Claim(int32_t* task);

    // The ticket: count in bits 16..31, the next unclaimed index in bits 0..15, a pass
    // generation above (only for readability in a debugger: a claim is a CAS on the
    // whole word, so it can never take an index of a pass it did not load).
    alignas(64) std::atomic<uint64_t> _ticket{0};
    alignas(64) std::atomic<int32_t>  _done{0};      // tasks finished by helpers this pass
    alignas(64) std::atomic<bool>     _awake{false};
    alignas(64) std::atomic<int32_t>  _sleepers{0};  // P7: helpers asleep inside an awake frame
    TaskFn   _fn  = nullptr;
    void*    _ctx = nullptr;
    int32_t  _count = 0;
    uint32_t _gen   = 0;

    std::mutex              _m;
    std::condition_variable _cv;
    uint64_t                _epoch = 0;     // under _m: bumped by every Wake
    bool                    _quit  = false; // under _m

    int      _helpers = 0;
    uint64_t _wakes   = 0;
    Helper   _h[Cpu::kMaxCullHelpers];
};

}  // namespace GatherThreads
}  // namespace Ext
