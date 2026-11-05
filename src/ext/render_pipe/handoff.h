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
// park on the plain condition variable where it does not.
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
        }
        _idle.notify_one();
    }

    // The job and its result are complete. busy first: see the ordering banner.
    void Done() {
        {
            Sys::MutexLock lk(_m);
            _busy.store(false, std::memory_order_release);
            _released.store(true, std::memory_order_release);
        }
        _idle.notify_one();
    }

private:
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
    std::atomic<bool>       _busy{false};
    std::atomic<bool>       _released{true};   // RENDERPIPE_FENCE: false from Publish to Release / Done
    std::atomic<int64_t>    _begunUs{0};        // P13_OVERLAP: when BeginCommands returned (NoteBegun)
};

}  // namespace RenderPipe
}  // namespace Ext
