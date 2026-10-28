// ============================================================================
// src/ext/render_pipe/pipe_driver.h -- RENDERPIPE-1: the frame orchestration
// (fence, drains, hand-off), sequential or on a render thread
// ============================================================================
// NOT RETAIL. ext.render_pipe (docs/extensions/handins/RENDERPIPE1.md).
//
// Engine-free: the engine's Host lives in render_pipe.cpp, the unit test's in
// tests/unit/ext_render_pipe_test.cpp (with a std::thread as the render thread).
//
// Two modes, one state machine (pipe_state.h, depth 1):
//   SEQUENTIAL (no Handoff; RENDERPIPE-0, and every machine with fewer than
//     three physical cores): "join" = emit the held packet on the main thread,
//     in the pipelined order (pass 1 of N+1, then the packet of N).
//   THREADED (a Handoff and a render thread running WorkerLoop): the packet is
//     PUBLISHED at the Frame tail that held it and the render thread emits it
//     while the main thread goes on (the next Frame's prelude, and the main half
//     of N+1 when that Frame does not step). "join" = wait until the render
//     thread is idle, then read its result.
//
// Every join site (THE FENCE before a step, the drain of every transition,
// the hand-off at a pipelined tail) is followed by main-thread code that may
// write what the emit reads; the join is the acquire that orders them
// (handoff.h). Device lost in the threaded mode: the render thread only
// PRESENTS and reports the failure (EmitResult::recover); the recovery runs
// here, on the main thread, at the join (Host::Recover), where retail runs it
// (inside MainLoop::Draw, on the main thread, with nothing else in flight).
//
// RENDERPIPE_FENCE (docs/extensions/handins/RENDERPIPE_FENCE.md): THE FENCE,
// threaded, waits only until the emit RELEASED itself (packet.h: after the
// post-draw callbacks); the emit's tail (EndCommands, _frameUpdateTime, the frame
// index, Present) may still run while the steps do. That tail is SETTLED (waited
// idle, its result read, a lost device recovered) at the next Frame tail before
// anything that needs the render thread idle: a drain / faithful frame, or, in a
// pipelined frame, after the main half and before the publish (the main half
// overlaps the tail too; it never reads the context's frame index while a tail
// may run -- fix 2 takes it from the held packet). A tail that lost the device
// discards the packet gathered meanwhile, as the hand-off does.
//
// P13_OVERLAP (docs/android/P13_OVERLAP.md): a packet whose items point at frozen
// copies (Packet::overlap, snapshot.h) is not waited for at THE FENCE when its
// emit has not reached its Gate (packet.h): the fence claims the gate instead
// (Handoff::ClaimSteps), the emit runs on beside the steps and parks there. THE
// FENCE then moves to the Frame tail: the main thread opens the gate and waits
// for the release there, before the main half -- so the main half runs with
// nothing pending exactly as after a fence before the steps (fixes 4 and 5: the
// sun and the camera are written; the emit's tail may still run). Handoff::WaitIdle
// opens the gate too. An emit already past its gate is fenced as before. Before MainLoop::Update ends the loop
// (kExtBeforeTransition) the render thread is joined, so no step frees what the
// emit still reads.
// ============================================================================
#pragma once

#include <chrono>
#include <cstdint>

#include "ext/render_pipe/handoff.h"
#include "ext/render_pipe/packet.h"
#include "ext/render_pipe/pipe_state.h"
#include "ext/render_pipe/render_pipe.h"   // Counters

namespace Ext {
namespace RenderPipe {

// What one emit reports to the join.
struct EmitResult {
    bool    failed    = false;   // BeginCommands failed (retail's DrawFailed path ran in the emit)
    bool    lost      = false;   // the device was lost in Finish
    bool    recover   = false;   // threaded: the recovery half is still to run, on the main thread
    int32_t hr        = 0;       // the Present / removed-reason result the recovery reports
    int64_t presentUs = 0;       // NowUs() after Finish (the packet latency)
};

inline int64_t NowUs() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::steady_clock::now().time_since_epoch()).count();
}

// Host (main thread unless stated):
//   int32_t    ContextFrameIndex();               Context::_frameIndex (only read when nothing is in flight)
//   void       MainHalf(Packet& next, const TailPlan& plan);   steps 1-6 + pass 1 into next.slot
//   EmitResult Emit(const Packet& p, Handoff* h);              the EMITTING thread: render (h non-null:
//                                                              threaded, the emit calls h->Release()) or main
//   void       Recover(const EmitResult& r);                   the device-lost recovery half (threaded only)
//   void       AfterJoin(const Packet& next);                  render idle, a skipping frame kept: the camera shadow
//   void       FaithfulTail();                                 MainLoop::Frame's own two tail statements
template <class Host>
class Driver {
public:
    // Threaded when `h` is non-null (its render thread runs WorkerLoop).
    void Attach(Handoff* h) { _handoff = h; }
    bool Threaded() const { return _handoff != nullptr; }
    const PipeState& State() const { return _state; }

    void Reset() {
        _state     = PipeState{};
        _held      = Packet{};
        _result    = EmitResult{};
        _publishUs = 0;
        _tail      = false;
        _overlapping = false;
    }

    Counters TakeWindow() {
        const Counters c = _counters;
        _counters = Counters{};
        return c;
    }

    // kExtBeforeSteps: THE FENCE. The sim never runs with a packet in flight
    // up to its post-draw callbacks (threaded: its tail may still run) --
    // P13_OVERLAP: except one whose emit reads frozen copies, parked at its gate.
    void BeforeSteps(Host& host) {
        if (_state.DrainBeforeSteps(true)) {
            // P13_OVERLAP: a packet with a snapshot runs on during the steps, up to its gate.
            if (_handoff != nullptr && _held.overlap && _handoff->ClaimSteps()) {
                _overlapping = true;
                ++_counters.overlapped;
                return;
            }
            ++_counters.drainStep;
            if (_handoff != nullptr)
                FenceReleased(host);
            else
                Join(host, false);
        }
    }

    // True between a fence that let the steps overlap an emit's tail and the
    // settle of that tail (the unit test reads it).
    bool TailInFlight() const { return _tail; }

    // True while the steps run beside an emit that has not reached its gate (the unit test reads it).
    bool Overlapping() const { return _overlapping; }

    // kExtBeforeTransition (P13_OVERLAP): MainLoop::Update is about to shut down and
    // delete the current loop. Nothing of the emit may be in flight then: the render
    // thread is joined here (its gate opened by the wait). The Frame's tail then sees
    // the new loop and runs unpipelined.
    void BeforeTransition(Host& host) {
        if (_handoff == nullptr) return;
        _overlapping = false;
        SettleTail(host, false);
        if (_state.Pending()) {
            ++_counters.drainOther;
            Join(host, false);
        }
    }

    // kExtFrameTail. True: the extension was switched off -- uninstall now (the
    // render thread is idle and joined).
    bool Tail(Host& host, const TailInputs& in) {
        // P13_OVERLAP: the steps are over; the emit parked at its gate goes on, and THE FENCE
        // the steps skipped is waited for here.
        if (_overlapping) {
            _overlapping = false;
            _handoff->OpenGate();
            FenceReleased(host);
        }
        const TailPlan plan = _state.PlanTail(in);
        if (!plan.pipelined) SettleTail(host, false);   // the faithful tail needs the render thread idle
        if (plan.drain) {
            ++_counters.drainOther;
            Join(host, false);
        }
        if (!plan.pipelined) {
            ++_counters.sequential;
            host.FaithfulTail();
            _state.FaithfulFrame();
            return plan.uninstall;
        }
        Pipelined(host, plan);
        return false;
    }

    // The render thread's body: park, emit, report, until Stop.
    void WorkerLoop(Host& host) {
        while (_handoff->WaitJob()) {
            _result = host.Emit(_held, _handoff);
            _result.presentUs = NowUs();
            _handoff->Done();
        }
    }

private:
    // Join the pending packet: emit it here (sequential) or wait for the
    // render thread (threaded). False: the frame gathered since must not be
    // emitted (the device was lost, or BeginCommands failed).
    bool Join(Host& host, bool handoff) {
        EmitResult res;
        if (_handoff != nullptr) {
            WaitIdle(handoff);   // ACQUIRE: the emit's writes are visible
            res = Collect();
        } else {
            res = host.Emit(_held, nullptr);
        }
        _state.Drained();
        ++_counters.emits;
        Recover(host, res);
        return !res.failed && !res.lost;
    }

    // Threaded: block until the render thread is idle, counted.
    void WaitIdle(bool handoff) {
        const int64_t t0 = NowUs();
        const bool waited = _handoff->WaitIdle();
        if (waited) {
            ++_counters.waits;
            if (!handoff) ++_counters.waitsFence;
            _counters.waitUs += static_cast<uint64_t>(NowUs() - t0);
        }
    }

    // Threaded, the render thread idle: the joined emit's result and its latency.
    EmitResult Collect() {
        const EmitResult res = _result;
        if (res.presentUs >= _publishUs) {
            _counters.latencyUs += static_cast<uint64_t>(res.presentUs - _publishUs);
            ++_counters.latencyN;
        }
        return res;
    }

    void Recover(Host& host, const EmitResult& res) {
        if (res.recover) {
            ++_counters.recoveries;
            host.Recover(res);   // the MAIN thread, the render thread idle
        }
    }

    // THE FENCE, threaded: wait until the pending emit released itself. When
    // its tail is done already (or it never released: a failed BeginCommands,
    // released by Done with busy cleared first) this is the full join; else
    // the tail is left in flight and settled at the next Frame tail.
    void FenceReleased(Host& host) {
        const int64_t t0 = NowUs();
        const bool waited = _handoff->WaitReleased();   // ACQUIRE: the emit's writes up to PostDraw
        if (waited) {
            ++_counters.waits;
            ++_counters.waitsFence;
            _counters.waitUs += static_cast<uint64_t>(NowUs() - t0);
        }
        _state.Drained();
        ++_counters.emits;
        if (_handoff->Busy()) {
            _tail = true;
            return;
        }
        Recover(host, Collect());
    }

    // The tail a fence left in flight: wait idle, read its result, recover a
    // lost device. False: it lost the device (or failed) -- what was gathered
    // since must not be emitted.
    bool SettleTail(Host& host, bool handoff) {
        if (!_tail) return true;
        _tail = false;
        WaitIdle(handoff);
        const EmitResult res = Collect();
        Recover(host, res);
        return !res.failed && !res.lost;
    }

    void Pipelined(Host& host, const TailPlan& plan) {
        const bool pending = _state.Pending();
        Packet next;
        next.slot = plan.pass1Slot;
        // Fix 2: the index the context holds when this packet's pass 2 runs. With
        // a packet pending, or the tail of one a fence released still in flight,
        // that emit increments it first; the context is read only when nothing is
        // in flight (the render thread may be writing it). RENDERPIPE_FENCE: both
        // indices come from the held copy whenever a tail may run.
        const bool inFlight = pending || _tail;
        next.frameIndex = inFlight ? _held.frameIndex + 1 : host.ContextFrameIndex();
        next.modelIndex = inFlight ? _held.modelIndex + 1 : host.ContextFrameIndex();

        host.MainHalf(next, plan);   // threaded: concurrent with the emit of _held (or its tail)
        ++_counters.pipelined;
        if (plan.renderIdle) ++_counters.idleWrites; else ++_counters.idleSkips;

        bool keep = SettleTail(host, true);     // a fenced tail: wait for it before N+1 goes
        if (pending) keep = Join(host, true);   // THE HAND-OFF: wait for N, then N+1 goes
        if (!keep) {
            _state.Discard(next.slot);
            ++_counters.discarded;
            return;
        }
        if (!plan.renderIdle) host.AfterJoin(next);
        _held = next;
        _state.Hold(next.slot);
        if (_handoff != nullptr) {
            _publishUs = NowUs();
            _handoff->Publish();   // RELEASE: _held is written before
        }
    }

    PipeState  _state;
    Packet     _held;              // the pending packet (the render thread reads it while busy)
    bool       _tail      = false; // RENDERPIPE_FENCE: a fence released the emit, its tail may run
    bool       _overlapping = false; // P13_OVERLAP: the steps run beside an emit short of its gate
    EmitResult _result;            // written by the emitting thread before Done
    Handoff*   _handoff   = nullptr;
    int64_t    _publishUs = 0;
    Counters   _counters;          // main thread only
};

}  // namespace RenderPipe
}  // namespace Ext
