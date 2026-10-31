// ============================================================================
// tests/unit/ext_render_pipe_test.cpp -- RENDERPIPE-0: ext.render_pipe (class A)
// ============================================================================
// The pure parts of src/ext/render_pipe on their own:
//   * packet.h's Emit: the second half's steps in retail's order (MainLoop::Frame
//     tail -> MainLoop::Draw -> GameUpdateLoop::Draw's context half, pass 2, the
//     post-scene draws -> the post-draw callbacks, EndCommands, the frame index,
//     Finish), and the BeginCommands failure path stopping where MainLoop::Draw's
//     does;
//   * pipe_state.h's PipeState: the ping-pong of the two Renderer slots, the
//     first frame holding nothing, the fence before a step, every "drain, then
//     run the frame unpipelined" transition (toggle off + uninstall, resize, the
//     minimap paint, the eye camera, a screenshot, a quit, a loop change, a
//     diagnostic), the device-lost discard, and renderIdle (fixes 4 and 5);
//   * a scripted run of the state machine over many frames: never two packets
//     held, pass 1 never fills the held packet's slot, every held packet emitted
//     before any step;
//   * the registry row;
//   * P13_OVERLAP: the gate (open unless the steps claim it before the job
//     reaches it; claimed, the job parks after pass 2 until the main thread is
//     past the steps; a wait for idle opens it), the scripted run with packets
//     that carry a snapshot (their emit beside the steps up to the gate, the UI
//     half never beside a step), and the join before a loop transition.
// docs/extensions/handins/RENDERPIPE0.md, docs/android/P13_OVERLAP.md
// ============================================================================
#include "unit.h"
#include "ext/render_pipe/packet.h"
#include "ext/render_pipe/pipe_state.h"
#include "ext/render_pipe/pipe_driver.h"
#include "ext/render_pipe/handoff.h"
#include "ext/registry.h"

#include <atomic>
#include <random>
#include <thread>
#include <string>
#include <vector>

namespace {

using Ext::RenderPipe::Packet;
using Ext::RenderPipe::PipeState;
using Ext::RenderPipe::TailInputs;
using Ext::RenderPipe::TailPlan;

struct RecordOps {
    std::vector<std::string> log;
    bool begin = true;
    void FrameTailTime(const Packet&)  { log.push_back("FrameTailTime"); }
    bool BeginCommands()               { log.push_back("BeginCommands"); return begin; }
    void BeginFailed()                 { log.push_back("BeginFailed"); }
    void ClearResourcesIfDirty()       { log.push_back("ClearResourcesIfDirty"); }
    void ViewportReconcile()           { log.push_back("ViewportReconcile"); }
    void PacketBegin()                 { log.push_back("PacketBegin"); }
    void RenderDocFrame()              { log.push_back("RenderDocFrame"); }
    void SetTime(const Packet&)        { log.push_back("SetTime"); }
    void WeatherContext(const Packet&) { log.push_back("WeatherContext"); }
    void ForceSnow(const Packet&)      { log.push_back("ForceSnow"); }
    void Pass2(const Packet&)          { log.push_back("Pass2"); }
    void Gate(const Packet&)           { log.push_back("Gate"); }
    void SaveLoadDraw(const Packet&)   { log.push_back("SaveLoadDraw"); }
    void SetCamera(const Packet&)      { log.push_back("SetCamera"); }
    void ToolDraw(const Packet&)       { log.push_back("ToolDraw"); }
    void UIDraw(const Packet&)         { log.push_back("UIDraw"); }
    void PacketEnd()                   { log.push_back("PacketEnd"); }
    void PostDraw()                    { log.push_back("PostDraw"); }
    void Released(const Packet&)       { log.push_back("Released"); }
    void EndCommands()                 { log.push_back("EndCommands"); }
    void FrameUpdateTime()             { log.push_back("FrameUpdateTime"); }
    void IncFrameIndex()               { log.push_back("IncFrameIndex"); }
    void Finish()                      { log.push_back("Finish"); }
};

TailInputs Clear() { return TailInputs{}; }

}  // namespace

UNIT_TEST(ext_render_pipe_emit_replays_in_retail_order) {
    RecordOps ops;
    Packet p;
    Ext::RenderPipe::Emit(ops, p);
    // Retail's frame from the Frame tail on, minus the main half's steps (3, 4,
    // the sun half of 5, 6 and pass 1): mainloop_frame.cpp tail, loops.cpp
    // MainLoop::Draw, gameupdateloop.cpp Draw steps 2, 5, 7 (pass 2), 8, 10-12.
    const std::vector<std::string> want = {
        "FrameTailTime", "BeginCommands", "ClearResourcesIfDirty", "ViewportReconcile",
        "PacketBegin", "RenderDocFrame", "SetTime", "WeatherContext", "ForceSnow", "Pass2",
        "Gate", "SaveLoadDraw", "SetCamera", "ToolDraw", "UIDraw", "PacketEnd",
        "PostDraw", "Released", "EndCommands", "FrameUpdateTime", "IncFrameIndex", "Finish",
    };
    CHECK(ops.log == want);
}

UNIT_TEST(ext_render_pipe_emit_stops_after_a_failed_begin) {
    RecordOps ops;
    ops.begin = false;
    Packet p;
    Ext::RenderPipe::Emit(ops, p);
    const std::vector<std::string> want = { "FrameTailTime", "BeginCommands", "BeginFailed" };
    CHECK(ops.log == want);
}

UNIT_TEST(ext_render_pipe_first_frame_holds_nothing_then_ping_pongs) {
    PipeState s;
    CHECK(!s.Pending());
    TailPlan plan = s.PlanTail(Clear());
    CHECK(plan.pipelined);
    CHECK(!plan.drain);
    CHECK(plan.renderIdle);              // nothing held: SunLight::Set / UpdateCamera run
    const int32_t first = plan.pass1Slot;
    CHECK(first == 0 || first == 1);
    s.Hold(first);
    CHECK(s.Pending());
    CHECK(s.PendingSlot() == first);

    for (int frame = 0; frame < 8; ++frame) {
        plan = s.PlanTail(Clear());
        CHECK(plan.pipelined);
        CHECK(!plan.renderIdle);         // a packet is held: fixes 4 and 5 skip
        CHECK(plan.pass1Slot == 1 - s.PendingSlot());
        const int32_t slot = plan.pass1Slot;
        s.Drained();                     // the held packet emitted after pass 1
        s.Hold(slot);
        CHECK(s.PendingSlot() == slot);
    }
}

UNIT_TEST(ext_render_pipe_fence_before_a_step) {
    PipeState s;
    CHECK(!s.DrainBeforeSteps(true));    // nothing held
    s.Hold(s.FreeSlot());
    CHECK(!s.DrainBeforeSteps(false));   // no step this Frame: keep it
    CHECK(s.DrainBeforeSteps(true));
    s.Drained();
    CHECK(!s.Pending());
    // The first frame after the step: nothing held, so the sun / camera writes run.
    const TailPlan plan = s.PlanTail(Clear());
    CHECK(plan.pipelined);
    CHECK(plan.renderIdle);
}

UNIT_TEST(ext_render_pipe_every_transition_drains_and_runs_unpipelined) {
    using Field = bool TailInputs::*;
    const Field fields[] = { &TailInputs::quitting, &TailInputs::settingsChanged,
                             &TailInputs::paintTerrain, &TailInputs::eyeCamera,
                             &TailInputs::screenshot, &TailInputs::diagnostic };
    for (Field f : fields) {
        PipeState s;
        s.Hold(1);
        TailInputs in = Clear();
        in.*f = true;
        const TailPlan plan = s.PlanTail(in);
        CHECK(plan.drain);
        CHECK(!plan.pipelined);
        CHECK(!plan.uninstall);
        // With nothing held there is nothing to drain, and the frame is still sequential.
        PipeState empty;
        const TailPlan plan2 = empty.PlanTail(in);
        CHECK(!plan2.drain);
        CHECK(!plan2.pipelined);
    }
    {   // the loop is not the game's (menu loop, a load, the world not built)
        PipeState s;
        s.Hold(0);
        TailInputs in = Clear();
        in.gameLoop = false;
        const TailPlan plan = s.PlanTail(in);
        CHECK(plan.drain);
        CHECK(!plan.pipelined);
    }
    {   // toggle off: drain, the faithful tail, then the seam is removed
        PipeState s;
        s.Hold(0);
        TailInputs in = Clear();
        in.want = false;
        const TailPlan plan = s.PlanTail(in);
        CHECK(plan.drain);
        CHECK(!plan.pipelined);
        CHECK(plan.uninstall);
    }
}

UNIT_TEST(ext_render_pipe_faithful_frame_and_discard_pick_the_other_slot_next) {
    PipeState s;
    s.FaithfulFrame();                    // the loop's own Renderer (slot 0) drew
    CHECK(s.FreeSlot() == 1);
    s.Hold(1);
    s.Drained();
    s.Discard(0);                         // device lost: the new packet in slot 0 is dropped
    CHECK(!s.Pending());
    CHECK(s.FreeSlot() == 1);
    const TailPlan plan = s.PlanTail(Clear());
    CHECK(plan.renderIdle);
}

UNIT_TEST(ext_render_pipe_scripted_frames_keep_the_ownership_rules) {
    std::mt19937 rng(20261014u);
    PipeState s;
    int presented = 0, gathered = 0, discarded = 0;
    for (int frame = 0; frame < 20000; ++frame) {
        // A Frame: maybe steps (fence), then the tail.
        const bool steps = (rng() % 7) == 0;
        if (s.DrainBeforeSteps(steps)) { s.Drained(); ++presented; }
        if (steps) CHECK(!s.Pending());   // the sim never runs with a packet held

        TailInputs in = Clear();
        const uint32_t r = rng() % 100;
        if (r == 0) in.settingsChanged = true;
        else if (r == 1) in.eyeCamera = true;
        else if (r == 2) in.screenshot = true;
        else if (r == 3) in.gameLoop = false;
        const TailPlan plan = s.PlanTail(in);
        if (plan.drain) { s.Drained(); ++presented; }
        if (!plan.pipelined) {
            CHECK(!s.Pending());
            s.FaithfulFrame();
            ++presented;
            continue;
        }
        CHECK(plan.renderIdle == !s.Pending());
        CHECK(!s.Pending() || plan.pass1Slot != s.PendingSlot());   // never the held packet's slot
        ++gathered;
        bool keep = true;
        if (s.Pending()) {
            s.Drained();
            ++presented;
            keep = (rng() % 997) != 0;      // a device loss in the emit
        }
        if (keep) s.Hold(plan.pass1Slot);
        else { s.Discard(plan.pass1Slot); ++discarded; }
    }
    CHECK(gathered > 0);
    CHECK(presented > 0);
    (void)discarded;
}

UNIT_TEST(ext_render_pipe_registry_row) {
    const Ext::Entry* e = Ext::FindEntry("render_pipe");
    CHECK(e != nullptr);
    if (e == nullptr) return;
    CHECK(e->tier == Ext::Tier::Extension);
    CHECK(e->cls == Ext::Class::A);
    CHECK(e->defaultOn);   // class A ships ON (DESIGN.md section 2, ruled 2026-10-18)
    CHECK(e->formatVersion == 0);
    CHECK(std::string(e->displayName) == "Render thread");
}

// ============================================================================
// RENDERPIPE-1: pipe_driver.h's Driver over handoff.h, with a fake engine. The
// threaded runs use a std::thread as the render thread (the test only; the game
// uses a System::Thread). Failures on the render thread are counted in an
// atomic and checked on the main thread.
// ============================================================================
namespace {

using Ext::RenderPipe::Counters;
using Ext::RenderPipe::Driver;
using Ext::RenderPipe::EmitResult;
using Ext::RenderPipe::Handoff;

struct FakeHost {
    Driver<FakeHost>* driver = nullptr;
    std::thread::id   mainId;
    bool              threaded = false;
    std::mt19937      rngMain{7u};

    std::atomic<int>  ctxIndex{0};      // Context::_frameIndex (the emit's tail increments it)
    std::atomic<int>  inFlight{0};      // packets being emitted right now (tail included)
    std::atomic<bool> emitting{false};  // the emit up to its release (pass 2 .. PostDraw)
    std::atomic<bool> drawingUi{false}; // P13_OVERLAP: the emit past its gate, up to its release
    std::atomic<bool> passTwo{false};   // P13_OVERLAP: the emit before its gate
    int               overlapEvery = 0; // >0: every Nth gathered packet carries a snapshot (threaded)
    int               stepsBesideEmit = 0;   // steps that ran while a pass 2 was in flight
    std::mt19937      rngOverlap{11u};
    std::atomic<bool> tailing{false};   // RENDERPIPE_FENCE: the tail (EndCommands .. Present)
    int               stepsInTail = 0;  // steps that ran while a tail was in flight
    std::atomic<int>  errors{0};        // failed checks on the render thread
    std::atomic<int>  emits{0};
    int               loseEvery = 0;    // >0: the Present of every Nth emit fails
    int               mainHalves = 0;
    int               recoveries = 0;
    int               lastModel = -1;   // the render thread's: modelIndex of the last emit kept

    int32_t ContextFrameIndex() {
        if (emitting.load() || tailing.load()) ++errors;  // read only with nothing in flight
        return ctxIndex.load();
    }
    void MainHalf(Ext::RenderPipe::Packet& next, const Ext::RenderPipe::TailPlan& plan) {
        if (std::this_thread::get_id() != mainId) ++errors;
        if (plan.renderIdle && emitting.load()) ++errors;   // fixes 4 / 5 write only when idle
        if (driver->State().Pending() && next.slot == driver->State().PendingSlot()) ++errors;
        ++mainHalves;
        next.overlap = threaded && overlapEvery > 0 && rngOverlap() % static_cast<unsigned>(overlapEvery) == 0;
        // Pass 1 takes a while; the emit of the pending packet runs meanwhile.
        for (volatile int i = 0, n = static_cast<int>(rngMain() % 64); i < n; i = i + 1) {}
    }
    EmitResult Emit(const Ext::RenderPipe::Packet& p, Handoff* release) {
        EmitResult r;
        const bool onThread = release != nullptr;
        if (onThread != threaded) ++errors;
        if (threaded && std::this_thread::get_id() == mainId) ++errors;
        if (!threaded && std::this_thread::get_id() != mainId) ++errors;
        if (inFlight.fetch_add(1) != 0) ++errors;           // never two packets alive
        emitting.store(true);
        if (p.frameIndex != ctxIndex.load()) ++errors;      // fix 2: the sequential order's index
        if (p.modelIndex != p.frameIndex) ++errors;
        const int n = emits.fetch_add(1) + 1;
        passTwo.store(true);
        for (volatile int i = 0, k = (n * 37) % 97; i < k; i = i + 1) {}
        passTwo.store(false);
        // packet.h's Gate: a packet with a snapshot waits here until the steps are over.
        if (p.overlap) {
            if (release == nullptr) ++errors;   // the sequential split never carries one
            else if (!release->PassGate()) release->WaitGate();
        }
        drawingUi.store(true);
        for (volatile int i = 0, k = (n * 29) % 61; i < k; i = i + 1) {}
        drawingUi.store(false);
        // packet.h's Released: the fence may let the steps run from here.
        tailing.store(true);
        emitting.store(false);
        if (release != nullptr) release->Release();
        // The tail: EndCommands, ++_frameIndex, Present.
        for (volatile int i = 0, k = (n * 53) % 211; i < k; i = i + 1) {}
        if (loseEvery > 0 && n % loseEvery == 0) {
            r.lost = true;
            r.recover = threaded;   // threaded: the main thread recovers at the join
            r.hr = 0x887a0007;
        } else {
            ctxIndex.fetch_add(1);  // ++Context::_frameIndex, Present
        }
        tailing.store(false);
        inFlight.fetch_sub(1);
        return r;
    }
    void Recover(const EmitResult&) {
        if (std::this_thread::get_id() != mainId) ++errors;
        if (emitting.load() || tailing.load() || inFlight.load() != 0) ++errors;   // render thread idle
        ++recoveries;
        // The recreated context counts from where the lost one stopped (the lost
        // emit did not increment it): nothing to do here.
    }
    void AfterJoin(const Ext::RenderPipe::Packet&) {
        if (emitting.load() || inFlight.load() != 0) ++errors;   // the camera shadow writes the real camera: render idle
    }
    void FaithfulTail() {
        if (emitting.load() || inFlight.load() != 0) ++errors;
        ctxIndex.fetch_add(1);           // MainLoop::Draw's own ++_frameIndex
    }
    void BeforePublish(const Ext::RenderPipe::Packet&) {
        if (std::this_thread::get_id() != mainId) ++errors;
        if (!threaded || inFlight.load() != 0) ++errors;   // the pose seam is written with the render thread idle
        ++publishes;
    }
    void OverlapSwitch(const Ext::RenderPipe::OverlapGovernor&) { ++switches; }
    int publishes = 0;
    int switches  = 0;
};

// One scripted run of `frames` Frames. Returns the window counters summed.
Counters RunScripted(bool threaded, int frames, int loseEvery, uint32_t seed, FakeHost& host,
                     int overlapEvery = 0) {
    Driver<FakeHost> driver;
    Handoff handoff;
    host.driver = &driver;
    host.mainId = std::this_thread::get_id();
    host.threaded = threaded;
    host.loseEvery = loseEvery;
    host.overlapEvery = overlapEvery;
    std::thread render;
    if (threaded) {
        driver.Attach(&handoff);
        render = std::thread([&] { driver.WorkerLoop(host); });
    }
    std::mt19937 rng(seed);
    Counters sum;
    for (int frame = 0; frame < frames; ++frame) {
        const bool steps = (rng() % 5) == 0;
        const uint32_t r = rng() % 200;
        if (steps) {
            driver.BeforeSteps(host);   // THE FENCE
            if (driver.Overlapping()) {
                // P13_OVERLAP: the packet's pass 2 may run on; its UI half never beside a step.
                CHECK(threaded);
                CHECK(driver.State().Pending());
                for (int i = 0; i < 64; ++i) {
                    if (host.drawingUi.load()) ++host.errors;
                    if (host.passTwo.load()) ++host.stepsBesideEmit;
                }
            } else {
                // The sim runs: nothing pending, no emit before its release. Threaded,
                // the tail of the released emit may still run (RENDERPIPE_FENCE).
                CHECK(!driver.State().Pending());
                CHECK(!host.emitting.load());
                if (!threaded || !driver.TailInFlight()) CHECK(!handoff.Busy());
                if (host.tailing.load()) ++host.stepsInTail;
            }
            if (r == 3) {
                // A step ends the loop: MainLoop::Update joins before Shutdown / delete.
                driver.BeforeTransition(host);
                CHECK(!driver.State().Pending());
                if (threaded) CHECK(!handoff.Busy());
                CHECK(!driver.Overlapping());
            }
        }
        TailInputs in = Clear();
        if (r == 0) in.settingsChanged = true;
        else if (r == 1) in.eyeCamera = true;
        else if (r == 2) in.screenshot = true;
        else if (r == 3) in.gameLoop = false;
        else if (r == 4) in.quitting = true;
        const bool uninstall = driver.Tail(host, in);
        CHECK(!uninstall);
        const bool sequentialFrame = r <= 4;
        if (sequentialFrame) {   // every drain joins: the render thread is idle
            CHECK(!driver.State().Pending());
            CHECK(!handoff.Busy());
        }
        // After every tail nothing but the packet just published is in flight.
        CHECK(!driver.TailInFlight());
        CHECK(!driver.Overlapping());      // P13_OVERLAP: the tail waited for the fence the steps skipped
        const Counters c = driver.TakeWindow();
        CHECK(c.discarded <= 1);
        CHECK(c.discarded <= c.recoveries + (threaded ? 0 : c.emits));   // a discard only follows a loss
        if (!threaded) CHECK(c.waits == 0);
        sum.pipelined += c.pipelined; sum.sequential += c.sequential; sum.emits += c.emits;
        sum.drainStep += c.drainStep; sum.drainOther += c.drainOther; sum.discarded += c.discarded;
        sum.waits += c.waits; sum.recoveries += c.recoveries; sum.latencyN += c.latencyN;
        sum.idleWrites += c.idleWrites; sum.idleSkips += c.idleSkips;
        sum.overlapped += c.overlapped;
    }
    // Toggle off: drain, the faithful tail, uninstall; then the thread stops.
    TailInputs off = Clear();
    off.want = false;
    CHECK(driver.Tail(host, off));
    CHECK(!driver.State().Pending());
    CHECK(!handoff.Busy());
    if (threaded) {
        handoff.Stop();
        render.join();
    }
    const Counters c = driver.TakeWindow();
    sum.emits += c.emits; sum.recoveries += c.recoveries; sum.discarded += c.discarded; sum.latencyN += c.latencyN;
    return sum;
}

}  // namespace

UNIT_TEST(ext_render_pipe_handoff_parks_and_stops) {
    Handoff h;
    std::atomic<int> ran{0};
    std::thread t([&] { while (h.WaitJob()) { ran.fetch_add(1); h.Done(); } });
    CHECK(!h.WaitIdle());              // nothing published: no wait
    for (int i = 0; i < 1000; ++i) {
        h.Publish();
        h.WaitIdle();
        CHECK(!h.Busy());
        CHECK(ran.load() == i + 1);    // the job's writes are visible after the join
    }
    h.Stop();
    t.join();
    CHECK(ran.load() == 1000);
}

// RENDERPIPE_FENCE: WaitReleased returns once the job released itself, before it
// is done; the job's writes made before the release are visible; Busy stays
// true until Done; a job that never releases is released by Done, busy first.
UNIT_TEST(ext_render_pipe_handoff_release_comes_before_idle) {
    Handoff h;
    std::atomic<bool> stepped{false};
    std::atomic<bool> release{true};
    int before = 0;                    // written by the job before Release, read by main after
    std::thread t([&] {
        while (h.WaitJob()) {
            ++before;
            if (release.load()) {
                h.Release();
                while (!stepped.load()) std::this_thread::yield();   // the tail outlives the step
            }
            h.Done();
        }
    });
    CHECK(!h.WaitReleased());          // nothing published: released
    for (int i = 0; i < 200; ++i) {
        stepped.store(false);
        h.Publish();
        h.WaitReleased();
        CHECK(before == i + 1);
        CHECK(h.Busy());               // the tail waits for this "step"
        stepped.store(true);
        h.WaitIdle();
        CHECK(!h.Busy());
    }
    release.store(false);              // a job that never releases (a failed BeginCommands)
    for (int i = 0; i < 200; ++i) {
        h.Publish();
        h.WaitReleased();
        CHECK(!h.Busy());              // released only by Done, and busy was cleared first
    }
    h.Stop();
    t.join();
}

UNIT_TEST(ext_render_pipe_threaded_100k_frames_keep_the_ownership_rules) {
    FakeHost host;
    const Counters c = RunScripted(true, 100000, 0, 20261014u, host);
    CHECK(host.errors.load() == 0);    // the render thread's checks
    CHECK(c.pipelined > 0);
    CHECK(c.emits > 0);
    CHECK(c.drainStep > 0);
    CHECK(c.drainOther > 0);
    CHECK(c.discarded == 0);
    CHECK(c.recoveries == 0);
    CHECK(c.latencyN == c.emits);      // every joined packet was published, and its latency counted
    CHECK(c.idleWrites > 0 && c.idleSkips > 0);
    CHECK(host.mainHalves == static_cast<int>(c.pipelined));
    // Every gathered packet was emitted (none left, none lost): emits == pipelined.
    CHECK(c.emits == c.pipelined);
    (void)host.stepsInTail;            // timing-dependent: the overlap itself is proven by the handoff test
}

// P13_OVERLAP: the gate. Unclaimed, the job goes straight through it. Claimed before the job
// reaches it, the job parks there until the main thread opens it (or waits for idle, which
// opens it); the job's writes before the gate and the main thread's writes before OpenGate
// are each visible to the other side. A job already past its gate cannot be claimed.
UNIT_TEST(ext_render_pipe_handoff_gate_holds_the_job_until_opened) {
    Handoff h;
    std::atomic<int>  atGate{0};
    std::atomic<int>  go{0};
    std::atomic<bool> parked{false};
    int beforeGate = 0;                // the job's, before the gate
    int stepWrite  = 0;                // main's, before OpenGate; read by the job after the gate
    int seen       = 0;
    std::thread t([&] {
        int job = 0;
        while (h.WaitJob()) {
            ++job;
            ++beforeGate;
            atGate.fetch_add(1);
            while (go.load() != job) std::this_thread::yield();
            if (!h.PassGate()) {
                parked.store(true);
                h.WaitGate();
                parked.store(false);
            }
            seen = stepWrite;
            h.Done();
        }
    });
    for (int i = 0; i < 300; ++i) {
        h.Publish();
        while (atGate.load() != i + 1) std::this_thread::yield();
        const int mode = i % 3;
        if (mode == 2) {
            // Unclaimed: straight through.
            go.store(i + 1);
            h.WaitIdle();
            CHECK(!h.ClaimSteps());    // done: nothing to claim
        } else {
            CHECK(h.ClaimSteps());     // not past the gate yet
            go.store(i + 1);
            while (!parked.load()) std::this_thread::yield();
            CHECK(h.Busy());           // parked: not done until the gate opens
            stepWrite = i + 1;
            if (mode == 0) {
                h.OpenGate();
                h.OpenGate();          // idempotent
            }
            h.WaitIdle();              // mode 1: the wait opens it
            CHECK(seen == i + 1);
        }
        CHECK(!h.Busy());
        CHECK(beforeGate == i + 1);
    }
    h.Stop();
    t.join();
}

UNIT_TEST(ext_render_pipe_threaded_overlap_keeps_the_ui_half_off_the_steps) {
    FakeHost host;
    const Counters c = RunScripted(true, 100000, 0, 20261028u, host, 2);
    CHECK(host.errors.load() == 0);    // the render thread's checks, and no UI half beside a step
    CHECK(host.publishes == static_cast<int>(c.pipelined - c.discarded));   // BeforePublish before each
    CHECK(c.overlapped > 0);           // fences skipped for packets with a snapshot
    CHECK(c.drainStep > 0);            // the others kept their fence
    CHECK(c.idleWrites > 0);           // after overlapped steps the tail's fence leaves nothing pending
    CHECK(c.discarded == 0);
    CHECK(c.latencyN == c.emits);
    CHECK(c.emits == c.pipelined);
    CHECK(host.mainHalves == static_cast<int>(c.pipelined));
    (void)host.stepsBesideEmit;        // timing-dependent
}

UNIT_TEST(ext_render_pipe_threaded_overlap_device_lost_recovers_on_main) {
    FakeHost host;
    const Counters c = RunScripted(true, 100000, 997, 99u, host, 1);
    CHECK(host.errors.load() == 0);
    CHECK(c.recoveries > 0);
    CHECK(host.recoveries == static_cast<int>(c.recoveries));
    CHECK(c.discarded <= c.recoveries);
    CHECK(c.emits + c.discarded == c.pipelined);
}

UNIT_TEST(ext_render_pipe_threaded_device_lost_recovers_on_main_and_discards_one) {
    FakeHost host;
    const Counters c = RunScripted(true, 100000, 997, 77u, host);
    CHECK(host.errors.load() == 0);
    CHECK(c.recoveries > 0);
    CHECK(host.recoveries == static_cast<int>(c.recoveries));
    // A loss joined at the hand-off, or at the settle of a tail a fence
    // released, discards exactly the one packet gathered meanwhile; one joined
    // at a drain (or a fence whose tail was already done) discards nothing.
    CHECK(c.discarded > 0);
    CHECK(c.discarded <= c.recoveries);
    // Every gathered packet was emitted or discarded.
    CHECK(c.emits + c.discarded == c.pipelined);
}

UNIT_TEST(ext_render_pipe_sequential_driver_matches_the_split) {
    FakeHost host;
    const Counters c = RunScripted(false, 20000, 997, 5u, host);
    CHECK(host.errors.load() == 0);
    CHECK(c.waits == 0);
    CHECK(c.recoveries == 0);          // the faithful Finish recovered inline
    CHECK(host.recoveries == 0);
    CHECK(c.emits + c.discarded == c.pipelined);
    CHECK(c.discarded > 0);
}

// ---- the overlap governor (overlap_governor.h) -----------------------------
namespace {
using Ext::RenderPipe::OverlapGovernor;

// Feeds `n` Frames; returns the switches seen.
int Feed(OverlapGovernor& g, int n, int64_t removableUs, int64_t idleUs) {
    int switches = 0;
    for (int i = 0; i < n; ++i) switches += g.Frame(removableUs, idleUs) ? 1 : 0;
    return switches;
}
}  // namespace

UNIT_TEST(ext_render_pipe_governor_off_without_a_wait_worth_a_copy) {
    OverlapGovernor g;
    CHECK(!g.On());
    CHECK(Feed(g, 1000, 0, 0) == 0);       // no fence wait
    CHECK(Feed(g, 1000, 170, 0) == 0);     // the desktop's 0.17 ms
    CHECK(Feed(g, 1000, 900, 0) == 0);     // under the 1 ms floor
    CHECK(!g.On());
}

UNIT_TEST(ext_render_pipe_governor_off_when_the_frame_idles_anyway) {
    OverlapGovernor g;
    // Normal play at the cap: a 5 ms fence wait, then the cap's sleep: removing the wait
    // only lengthens the sleep.
    CHECK(Feed(g, 2000, 5000, 6000) == 0);
    CHECK(!g.On());
    CHECK(Feed(g, 2000, 5000, 600) == 0);  // still idles over the 0.5 ms floor
    CHECK(!g.On());
}

UNIT_TEST(ext_render_pipe_governor_on_when_the_wait_pays_and_holds) {
    OverlapGovernor g;
    // The first switch waits for the hold (a load hitch at install does not switch it).
    CHECK(Feed(g, OverlapGovernor::kHoldFrames - 1, 4200, 0) == 0);
    CHECK(!g.On());
    CHECK(Feed(g, 30, 4200, 0) == 1);      // the S10 stress town: 4.2 ms, no idle
    CHECK(g.On());
    g.Copy(2000);                          // a 2 ms copy
    // On, the frame reaches the cap: it idles 1.5 ms, and would idle 1.5 + 2 - 3.7 < 0 ms
    // without the copies: kept.
    CHECK(Feed(g, 1000, 3700, 1500) == 0);
    CHECK(g.On());
    // The load drops: it idles 6 ms, 4.3 ms without the copies (over 4): off, then it stays off.
    CHECK(Feed(g, 1000, 3700, 6000) == 1);
    CHECK(!g.On());
    CHECK(Feed(g, 1000, 3700, 6000) == 0);
    CHECK(!g.On());
}

UNIT_TEST(ext_render_pipe_governor_never_buys_a_wait_with_a_dearer_copy) {
    OverlapGovernor g;
    g.Copy(3000);                          // the copy costs 3 ms
    CHECK(Feed(g, 2000, 2500, 0) == 0);    // a 2.5 ms wait is not worth it
    CHECK(!g.On());
    CHECK(Feed(g, 2000, 3500, 0) == 1);
    CHECK(g.On());
    for (int i = 0; i < 200; ++i) g.Copy(9000);   // the copy grew past twice the wait
    CHECK(Feed(g, 200, 3500, 0) == 1);
    CHECK(!g.On());
}

UNIT_TEST(ext_render_pipe_governor_does_not_flap_on_a_noisy_frame) {
    OverlapGovernor g;
    g.Copy(2000);
    std::mt19937 rng(5u);
    int switches = 0;
    // A wait jumping between 0 and 8 ms Frame to Frame (mean 4 ms), the idle between 0 and
    // 2 ms on: one switch on, then it holds.
    for (int i = 0; i < 20000; ++i) {
        const bool on = g.On();
        const int64_t w = (rng() & 1u) != 0 ? 8000 : 0;
        const int64_t idle = on ? ((rng() & 1u) != 0 ? 2000 : 0) : 0;
        switches += g.Frame(w, idle) ? 1 : 0;
    }
    CHECK(switches == 1);
    // A load drifting across both thresholds flips once per crossing, never faster than the hold.
    OverlapGovernor h;
    int last = -OverlapGovernor::kHoldFrames;
    for (int i = 0; i < 20000; ++i) {
        const int64_t idle = (i / 3000) % 2 == 0 ? 0 : 9000;
        if (h.Frame(4000, idle)) {
            CHECK(i - last >= OverlapGovernor::kHoldFrames);
            last = i;
        }
    }
}
