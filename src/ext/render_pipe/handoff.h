// ============================================================================
// src/ext/render_pipe/handoff.h -- RENDERPIPE-1: the depth-1 hand-off between
// the main thread and the render thread
// ============================================================================
// NOT RETAIL. ext.render_pipe (docs/extensions/handins/RENDERPIPE1.md section 2).
//
// Standard C++ only (std::mutex / std::condition_variable / std::atomic, as
// src/ext/path_workers/pool.h): the unit test drives it with a std::thread.
//
// SENT MESSAGES (review F-224). The main thread owns the window; the render thread's
// Present (DXGI in exclusive fullscreen) or a BeginCommands that resizes may SEND it a
// message, which completes only once the main thread runs it. So the main thread's waits
// (ClaimSteps, WaitIdle, WaitReleased) wake every millisecond to run the sent messages
// (Platform::DispatchSentMessages, never a posted one) where the platform has them, and
// park on the plain condition variable where it does not. On Windows that call peeks only
// when a sent message is waiting: an empty filtered peek every millisecond made Windows
// flag the window as hung (platform_thread.h, THE HUNG WINDOW).
//
// ONE job slot. The main thread writes the job (the packet, held by the caller)
// and then Publish()es it; the render thread blocks in WaitJob() until a job or
// Stop() arrives, runs it, writes its result (held by the caller) and calls
// Done(). The main thread's WaitIdle() returns once the render thread is idle.
// Nobody spins: both sides park on a condition variable.
//
// RENDERPIPE_FENCE (docs/extensions/handins/RENDERPIPE_FENCE.md): a job may also
// Release() itself before it is Done -- the emit does so after the post-draw
// callbacks, before EndCommands / the frame index / Present. WaitReleased()
// returns at that point (or at Done, for a job that never released: a failed
// BeginCommands). Only the fence waits on it; every other join waits for idle.
//
// MEMORY ORDERING (the pairs; each mutex unlock is a release, each lock an
// acquire, and the atomic is the fast path):
//   publish  main:   [writes the packet] -> lock; ready = true; busy = true
//                    (relaxed: the unlock releases it); unlock (RELEASE)
//            render: lock (ACQUIRE) in WaitJob, sees ready -> every packet
//                    write made before Publish happens-before the emit
//   idle     render: [the emit, Finish, the result] -> lock;
//                    busy.store(false, RELEASE); unlock; notify
//            main:   busy.load(ACQUIRE) == false (the fast path), or lock
//                    (ACQUIRE) + wait until !busy -> every write of the emit
//                    (the context, _frameUpdateTime, SaveLoad's capture flags,
//                    the road tool's path, the result) happens-before what
//                    main runs next (steps, the next main half's writes)
//   released render: [the emit up to PostDraw] -> lock;
//                    released.store(true, RELEASE); unlock; notify
//            main:   released.load(ACQUIRE) == true (the fast path), or lock
//                    (ACQUIRE) + wait until released -> every write the emit
//                    made up to its post-draw callbacks (pass 2, the post-scene
//                    draws, SaveLoad's capture flags, the road tool's path)
//                    happens-before the steps. The tail's own writes (the
//                    resolve, _frameUpdateTime, ++Context::_frameIndex, Present,
//                    the result) are NOT covered: they wait for idle.
//   Done stores busy = false BEFORE released = true, so a main thread that sees
//   released also sees busy == false when the job is complete (a failed
//   BeginCommands: released only by Done).
//   stop     main (render idle): lock; stop = true; unlock; notify ->
//            render: WaitJob returns false, Run returns, ~Thread joins
//   gate     P13_OVERLAP (docs/android/P13_OVERLAP.md). Open by default: a job
//            whose main thread does not step goes straight through.
//            main, before steps beside the job: ClaimSteps locks and waits until
//            the job's BeginCommands returned (NoteBegun), it passed its gate or it
//            is done; if it is not past its gate, gate = false (true returned) --
//            else false, and the caller keeps THE FENCE. Review F-158:
//            BeginCommands reads live device state a step writes (the message
//            pump's SizeWindow, the options apply: Device::settingsChanged and the
//            back-buffer size), and a set flag makes it resize the swap chain and
//            the window from the render thread; no step runs before it returned. main, the steps over: OpenGate locks;
//            gate = true; notify. render: WaitGate locks, waits for gate, marks
//            itself past it -> every step write happens-before the rest of the
//            job, and no step runs beside it. WaitIdle opens it first: a main
//            thread that waits for the job has stopped stepping.
//   governor P13_OVERLAP section 10 (overlap_governor.h): two timestamps, steady
//            clock us. PassGate records when the job parked (read by OpenGate
//            under the lock); NoteBegun, when its BeginCommands returned (stored
//            under the lock, read after WaitReleased / OpenGate, whose locks order
//            it). ClaimSteps waits for NoteBegun (above); the rest only measure.
//   timeline OVERLAP (the [Stress::overlap] line, stress_harness.cpp): measures only, nothing
//            waits on them. Publish stamps the job under the lock; WaitJob (render thread)
//            sums its wake latency from that stamp; Release / Done stamp when they ran and Done
//            sums the job's span, its wait until BeginCommands returned and its tail. The sums
//            are relaxed atomics read and cleared by TakeTimeline (main thread, any time: a
//            window may split one job's numbers, which a window of thousands does not see).
// ============================================================================
#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <condition_variable>
#include <mutex>

#include "system/annotated_mutex.h"   // Sys::Mutex / Sys::MutexLock (clang -Wthread-safety)
#include "platform_thread.h"   // Platform::DispatchSentMessages (review F-224)

namespace Ext {
namespace RenderPipe {

// OVERLAP: the render thread's side of the frame timeline (sums over the jobs since the last
// TakeTimeline, us).
struct HandoffTimeline {
    uint64_t jobs     = 0;   // jobs done
    uint64_t wakeUs   = 0;   // Publish -> WaitJob returned (the render thread's wake latency)
    uint64_t jobUs    = 0;   // WaitJob returned -> Done (the emit's whole span)
    uint64_t beginUs  = 0;   // WaitJob returned -> BeginCommands returned (the GPU's / display's pace)
    uint64_t tailUs   = 0;   // Release -> Done (EndCommands, the frame index, Present)
    uint64_t idleUs   = 0;   // Done -> the next WaitJob returned (parked or between jobs)
};

class Handoff {
public:
    // ---- the main thread -------------------------------------------------
    // The job (written by the caller before this call) is handed over. The
    // render thread must be idle (the caller joined it first: depth 1).
    void Publish() {
        {
            Sys::MutexLock lk(_m);
            _ready  = true;
            _gate   = true;
            _passed = false;
            _parkedUs = 0;
            _begunUs.store(0, std::memory_order_relaxed);
            _busy.store(true, std::memory_order_relaxed);
            _released.store(false, std::memory_order_relaxed);
            _publishedUs = SteadyUs();   // OVERLAP: the render thread's wake latency starts here
        }
        _work.notify_one();
    }

    // P13_OVERLAP: steps are about to run beside the job. Waits until its BeginCommands returned
    // (review F-158: that op reads device state the steps write). True: it had not reached its
    // gate and now waits there until OpenGate. False: it is past its gate (or done) -- keep the
    // fence.
    bool ClaimSteps() {
        Sys::MutexLock lk(_m);
        MainWait(lk, [this]() REQUIRES(_m) {
            return _begunUs.load(std::memory_order_relaxed) != 0 || _passed ||
                   !_busy.load(std::memory_order_acquire);
        });
        if (_passed || !_busy.load(std::memory_order_acquire)) return false;
        _gate = false;
        _claimUs = SteadyUs();
        return true;
    }

    // The steps the job may have overlapped are over (P13_OVERLAP): let it past its gate.
    // Returns when the job reached the gate and parked (steady clock, us), 0 when it had not.
    int64_t OpenGate() {
        int64_t parked = 0;
        {
            Sys::MutexLock lk(_m);
            if (_gate) return 0;
            _gate  = true;
            parked = _parkedUs;
            _lastStepUs = SteadyUs() - _claimUs;   // SMOOTHREPLAY: the next claim's replays plan with it
        }
        _work.notify_one();
        return parked;
    }

    // Block until the render thread is idle. True when it had to wait.
    bool WaitIdle() {
        if (!_busy.load(std::memory_order_acquire)) return false;
        OpenGate();
        Sys::MutexLock lk(_m);
        MainWait(lk, [this]() REQUIRES(_m) { return !_busy.load(std::memory_order_acquire); });
        return true;
    }

    // Block until the job released itself (Release) or is done. True when it had to wait.
    bool WaitReleased() {
        if (_released.load(std::memory_order_acquire)) return false;
        Sys::MutexLock lk(_m);
        MainWait(lk, [this]() REQUIRES(_m) { return _released.load(std::memory_order_acquire); });
        return true;
    }

    bool Busy() const { return _busy.load(std::memory_order_acquire); }

    // P13_OVERLAP's governor: when the job's BeginCommands returned (steady clock, us; 0: not
    // yet). Read after WaitReleased or OpenGate, whose lock orders the store before.
    int64_t BegunUs() const { return _begunUs.load(std::memory_order_relaxed); }

    // The render thread is idle (WaitIdle returned): make its WaitJob return false.
    void Stop() {
        {
            Sys::MutexLock lk(_m);
            _stop = true;
        }
        _work.notify_one();
    }

    // ---- the render thread -----------------------------------------------
    // Park until a job (true) or Stop (false).
    bool WaitJob() {
        Sys::MutexLock lk(_m);
        _work.wait(lk.Native(), [this]() REQUIRES(_m) { return _ready || _stop; });
        if (_ready) {
            _ready = false;
            // OVERLAP: the wake latency and the idle span before this job (render thread only).
            _jobStartUs = SteadyUs();
            Add(_tlWakeUs, _jobStartUs - _publishedUs);
            if (_jobDoneUs != 0) Add(_tlIdleUs, _jobStartUs - _jobDoneUs);
            return true;
        }
        return false;
    }

    // P13_OVERLAP: past the gate at once when no steps claimed it (true); false: they did, and
    // the caller parks in WaitGate (after dropping what a step may need).
    bool PassGate() {
        Sys::MutexLock lk(_m);
        if (!_gate) {
            // The governor's measure (overlap_governor.h): how far the job ran beside the steps.
            _parkedUs = std::chrono::duration_cast<std::chrono::microseconds>(
                            std::chrono::steady_clock::now().time_since_epoch()).count();
            return false;
        }
        _passed = true;
        return true;
    }

    // SMOOTHREPLAY (docs/extensions/handins/SMOOTHREPLAY.md): the job, at a claimed gate, asks
    // whether one more replayed frame of about `estimateUs` fits before the steps end: the gate
    // still closed, and the claim's age plus the estimate under 95% of the last claimed steps'
    // span (no span known yet: it fits). It orders nothing: WaitGate still does.
    bool ReplayFits(int64_t estimateUs) {
        Sys::MutexLock lk(_m);
        if (_gate || _stop) return false;
        if (_lastStepUs <= 0) return true;
        return (SteadyUs() - _claimUs) + estimateUs < (_lastStepUs * 95) / 100;
    }

    // P13_OVERLAP: park while the main thread's steps run (ClaimSteps .. OpenGate); from here on
    // no step runs beside the job.
    void WaitGate() {
        Sys::MutexLock lk(_m);
        _work.wait(lk.Native(), [this]() REQUIRES(_m) { return _gate || _stop; });
        _passed = true;
    }

    // The job's BeginCommands returned: what it waited for until here (a free swap-chain image,
    // a frame in flight) was the GPU or the display, not its own work. Under the lock and
    // notified: a ClaimSteps waits for it (review F-158).
    void NoteBegun() {
        const int64_t now = std::chrono::duration_cast<std::chrono::microseconds>(
                                std::chrono::steady_clock::now().time_since_epoch()).count();
        {
            Sys::MutexLock lk(_m);
            _begunUs.store(now != 0 ? now : 1, std::memory_order_relaxed);
        }
        _idle.notify_one();
    }

    // The part of the job the fence waits for is complete; the rest may still run.
    void Release() {
        {
            Sys::MutexLock lk(_m);
            _released.store(true, std::memory_order_release);
            _releasedUs.store(SteadyUs(), std::memory_order_relaxed);   // OVERLAP: the fence's wake latency
        }
        _idle.notify_one();
    }

    // The job and its result are complete. busy first: see the ordering banner.
    void Done() {
        // OVERLAP: the job's span, its wait at BeginCommands and its tail (render thread only).
        const int64_t now = SteadyUs();
        const int64_t begun = _begunUs.load(std::memory_order_relaxed);
        const int64_t releasedUs = _releasedUs.load(std::memory_order_relaxed);
        Add(_tlJobUs, now - _jobStartUs);
        if (begun >= _jobStartUs) Add(_tlBeginUs, begun - _jobStartUs);
        if (releasedUs >= _jobStartUs) Add(_tlTailUs, now - releasedUs);
        _tlJobs.fetch_add(1, std::memory_order_relaxed);
        _jobDoneUs = now;
        {
            Sys::MutexLock lk(_m);
            _busy.store(false, std::memory_order_release);
            _released.store(true, std::memory_order_release);
            _doneUs.store(now, std::memory_order_relaxed);
        }
        _idle.notify_one();
    }

    // ---- OVERLAP: the timeline's measures -----------------------------------
    // When the last job's Release / Done ran (steady clock, us; 0: never). Read by the main thread
    // after the wait that their lock ordered.
    int64_t ReleasedUs() const { return _releasedUs.load(std::memory_order_relaxed); }
    int64_t DoneUs() const { return _doneUs.load(std::memory_order_relaxed); }

    // The render thread's sums since the last call, cleared (main thread).
    HandoffTimeline TakeTimeline() {
        HandoffTimeline t;
        t.jobs    = _tlJobs.exchange(0, std::memory_order_relaxed);
        t.wakeUs  = _tlWakeUs.exchange(0, std::memory_order_relaxed);
        t.jobUs   = _tlJobUs.exchange(0, std::memory_order_relaxed);
        t.beginUs = _tlBeginUs.exchange(0, std::memory_order_relaxed);
        t.tailUs  = _tlTailUs.exchange(0, std::memory_order_relaxed);
        t.idleUs  = _tlIdleUs.exchange(0, std::memory_order_relaxed);
        return t;
    }

private:
    static int64_t SteadyUs() {
        return std::chrono::duration_cast<std::chrono::microseconds>(
                   std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    // OVERLAP: one span into a timeline sum (a negative span, a clock oddity, is dropped).
    static void Add(std::atomic<uint64_t>& sum, int64_t us) {
        if (us > 0) sum.fetch_add(static_cast<uint64_t>(us), std::memory_order_relaxed);
    }

    // The main thread's wait on _idle until `done` (lk held): see SENT MESSAGES in the banner.
    // The lock is dropped while the window procedures run (review F-224), so the analysis is off here.
    template <class Done>
    void MainWait(Sys::MutexLock& lk, Done done) NO_THREAD_SAFETY_ANALYSIS {
        if (done()) return;
        lk.Unlock();
        const bool sent = Platform::DispatchSentMessages();
        lk.Lock();
        if (!sent) {
            _idle.wait(lk.Native(), done);
            return;
        }
        while (!_idle.wait_for(lk.Native(), std::chrono::milliseconds(1), done)) {
            lk.Unlock();
            Platform::DispatchSentMessages();
            lk.Lock();
        }
    }

    // GUARDED_BY: checked by clang's thread-safety analysis (system/thread_annotations.h). The three
    // atomics are the lock-free fast paths of the ordering banner, not guarded.
    Sys::Mutex              _m;
    std::condition_variable _work;
    std::condition_variable _idle;
    bool                    _ready GUARDED_BY(_m) = false;
    bool                    _stop GUARDED_BY(_m)  = false;
    bool                    _gate GUARDED_BY(_m)  = true;     // P13_OVERLAP: false from ClaimSteps to OpenGate
    bool                    _passed GUARDED_BY(_m) = false;   // P13_OVERLAP: the job is past its gate
    int64_t                 _parkedUs GUARDED_BY(_m) = 0;     // P13_OVERLAP: when it parked at the gate (0: not)
    int64_t                 _claimUs GUARDED_BY(_m) = 0;      // SMOOTHREPLAY: when the steps claimed the gate
    int64_t                 _lastStepUs GUARDED_BY(_m) = 0;   // SMOOTHREPLAY: the last claim .. OpenGate span
    std::atomic<bool>       _busy{false};
    std::atomic<bool>       _released{true};   // RENDERPIPE_FENCE: false from Publish to Release / Done
    std::atomic<int64_t>    _begunUs{0};        // P13_OVERLAP: when BeginCommands returned (NoteBegun)
    // OVERLAP: the timeline (banner). Stamps: Publish's under the lock, the job's start and end on
    // the render thread alone, Release / Done for the main thread's wake latency.
    int64_t                 _publishedUs GUARDED_BY(_m) = 0;
    int64_t                 _jobStartUs = 0;    // render thread only
    int64_t                 _jobDoneUs  = 0;    // render thread only (0: no job yet)
    std::atomic<int64_t>    _releasedUs{0};
    std::atomic<int64_t>    _doneUs{0};
    std::atomic<uint64_t>   _tlJobs{0};
    std::atomic<uint64_t>   _tlWakeUs{0};
    std::atomic<uint64_t>   _tlJobUs{0};
    std::atomic<uint64_t>   _tlBeginUs{0};
    std::atomic<uint64_t>   _tlTailUs{0};
    std::atomic<uint64_t>   _tlIdleUs{0};
};

}  // namespace RenderPipe
}  // namespace Ext
