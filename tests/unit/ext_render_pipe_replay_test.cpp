// ============================================================================
// tests/unit/ext_render_pipe_replay_test.cpp -- SMOOTHREPLAY: ext.render_pipe's
// replayed frames (the row render_pipe_replay)
// ============================================================================
// The pure parts on their own:
//   * display_clock.h: the alpha a frame is drawn at, from the wall clock;
//   * ui/SpriteRecord.h: the chunk bookkeeping SpriteContext::Flush drives;
//   * handoff.h's ReplayFits: only at a claimed gate, and only while one more
//     frame fits in the last claimed steps' span;
//   * resource_pin.h's FreeWaiting: a step's free waiting for the reads;
//   * pipe_driver.h: the copies are asked for after a long step while the replay
//     is on, whatever the governor says.
// docs/extensions/handins/SMOOTHREPLAY.md
// ============================================================================
#include "unit.h"
#include "ext/render_pipe/display_clock.h"
#include "ext/render_pipe/gpu_plan.h"
#include "ext/render_pipe/handoff.h"
#include "ext/render_pipe/pipe_driver.h"
#include "ext/render_pipe/resource_pin.h"
#include "ui/SpriteRecord.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <thread>

namespace {

using Ext::RenderPipe::DisplayAlpha;
using Ext::RenderPipe::Handoff;

}  // namespace

// The clock in a unit of 1000 ticks a step, a delay of 480 ticks (8 ms of 16.67).
UNIT_TEST(ext_render_pipe_replay_clock_is_the_packet_alpha_a_delay_late) {
    const int64_t start = 100000;
    const int64_t step  = 1000;
    const int64_t delay = 480;
    // Drawn exactly `delay` after the Frame's start: the packet's own alpha.
    CHECK_EQ(DisplayAlpha(0.25f, start, step, delay, start + delay), 0.25f);
    // Half a step later: half an alpha more.
    CHECK_EQ(DisplayAlpha(0.25f, start, step, delay, start + delay + 500), 0.75f);
    // Drawn at once (before the delay ran out): behind the packet's alpha.
    CHECK(DisplayAlpha(0.75f, start, step, delay, start) < 0.75f);
    CHECK(DisplayAlpha(0.75f, start, step, delay, start) > 0.26f);
}

UNIT_TEST(ext_render_pipe_replay_clock_clamps_and_never_runs_backwards) {
    const int64_t start = 5000;
    const int64_t step  = 1000;
    const int64_t delay = 480;
    CHECK_EQ(DisplayAlpha(0.1f, start, step, delay, start), 0.0f);            // a stall, not a negative alpha
    CHECK_EQ(DisplayAlpha(0.9f, start, step, delay, start + 5000), 1.0f);     // the pair's end, not past it
    float last = -1.0f;
    for (int64_t now = start; now < start + 3000; now += 37) {
        const float a = DisplayAlpha(0.3f, start, step, delay, now);
        CHECK(a >= last);
        CHECK(a >= 0.0f && a <= 1.0f);
        last = a;
    }
    // No clock (a step of zero): the packet's alpha.
    CHECK_EQ(DisplayAlpha(0.4f, start, 0, delay, start + 999), 0.4f);
}

// SpriteContext::Flush's protocol: BeginChunk hands out room, the vertices are written through
// the cursor, EndChunk keeps what was written and places the batch.
UNIT_TEST(ext_render_pipe_replay_sprite_record_keeps_each_chunk_and_its_state) {
    UI::SpriteRecord rec;
    CHECK(rec.Empty());

    const float view[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    const float proj[16] = {2, 0, 0, 0, 0, 2, 0, 0, 0, 0, 2, 0, 0, 0, 0, 2};

    // A chunk recorded before any camera has none.
    UI::SpriteRecordBatch state;
    state.stride    = 32;
    state.primitive = 2;
    uint8_t* cursor = rec.BeginChunk(4 * 32);
    std::memset(cursor, 0x11, 2 * 32);
    cursor += 2 * 32;                       // two of the four vertices written
    rec.EndChunk(cursor, state);
    CHECK_EQ(static_cast<int>(rec.batches.size()), 1);
    CHECK_EQ(rec.batches[0].vertices, 2);
    CHECK_EQ(rec.batches[0].camera, -1);
    CHECK_EQ(static_cast<int>(rec.batches[0].firstByte), 0);
    CHECK_EQ(static_cast<int>(rec.bytes.size()), 64);   // the unused room is given back

    rec.AddCamera(view, proj);
    state.primitive  = 0;
    state.scissor[2] = 640;
    cursor = rec.BeginChunk(6 * 32);
    std::memset(cursor, 0x22, 6 * 32);
    cursor += 6 * 32;
    rec.EndChunk(cursor, state);
    CHECK_EQ(static_cast<int>(rec.batches.size()), 2);
    CHECK_EQ(rec.batches[1].vertices, 6);
    CHECK_EQ(rec.batches[1].camera, 0);
    CHECK_EQ(static_cast<int>(rec.batches[1].firstByte), 64);
    CHECK_EQ(rec.batches[1].scissor[2], 640);
    CHECK_EQ(rec.bytes[63], 0x11);
    CHECK_EQ(rec.bytes[64], 0x22);
    CHECK_EQ(rec.cameras[0].proj[0], 2.0f);

    // A chunk nothing was written to leaves no batch.
    cursor = rec.BeginChunk(6 * 32);
    rec.EndChunk(cursor, state);
    CHECK_EQ(static_cast<int>(rec.batches.size()), 2);
    CHECK_EQ(static_cast<int>(rec.bytes.size()), 64 + 192);

    rec.Clear();
    CHECK(rec.Empty());
    CHECK(rec.bytes.empty());
    CHECK(rec.cameras.empty());
}

// A replayed frame is asked for only at a claimed gate; once a claim's span is known, only while
// the claim's age plus the frame's estimate stays under 95% of it.
UNIT_TEST(ext_render_pipe_replay_fits_only_inside_claimed_steps) {
    Handoff h;
    std::atomic<int>  phase{0};
    std::atomic<bool> fitsOpen{true};
    std::atomic<bool> fitsFirstClaim{false};
    std::atomic<bool> fitsShort{false};
    std::atomic<bool> fitsLong{true};
    std::thread t([&] {
        int job = 0;
        while (h.WaitJob()) {
            ++job;
            h.NoteBegun();
            if (job == 1) {
                fitsOpen.store(h.ReplayFits(100));              // nobody claimed the gate
                phase.store(1);
                while (phase.load() != 2) std::this_thread::yield();
                fitsFirstClaim.store(h.ReplayFits(1000000));    // claimed, no span known yet: it fits
                phase.store(3);
            } else {
                while (phase.load() != 5) std::this_thread::yield();
                fitsShort.store(h.ReplayFits(100));             // 0.1 ms inside a 40 ms span
                fitsLong.store(h.ReplayFits(10000000));         // 10 s does not
                phase.store(6);
            }
            if (!h.PassGate()) h.WaitGate();
            h.Done();
        }
    });
    h.Publish();
    while (phase.load() != 1) std::this_thread::yield();
    CHECK(h.ClaimSteps());
    phase.store(2);
    while (phase.load() != 3) std::this_thread::yield();
    std::this_thread::sleep_for(std::chrono::milliseconds(40));   // the claimed steps: the span
    h.OpenGate();
    h.WaitIdle();

    h.Publish();
    CHECK(h.ClaimSteps());
    phase.store(5);
    while (phase.load() != 6) std::this_thread::yield();
    h.OpenGate();
    h.WaitIdle();
    h.Stop();
    t.join();

    CHECK(!fitsOpen.load());
    CHECK(fitsFirstClaim.load());
    CHECK(fitsShort.load());
    CHECK(!fitsLong.load());
}

// The pin: while a step's free waits for the packet's reads, FreeWaiting answers true (the
// replayed frames stop on it); ReadsOver lets the free go and the answer is false again.
UNIT_TEST(ext_render_pipe_replay_pin_reports_a_waiting_free) {
    Ext::RenderPipe::PinGate gate;
    Ext::RenderPipe::PinSet  set;
    int mesh = 0;
    set.Add(&mesh);
    gate.Publish(&set);
    CHECK(!gate.FreeWaiting());

    std::atomic<bool> freed{false};
    std::thread step([&] {
        gate.BeforeFree(&mesh, 5000000);
        freed.store(true);
    });
    while (!gate.FreeWaiting()) std::this_thread::yield();
    CHECK(!freed.load());
    gate.ReadsOver();
    step.join();
    CHECK(freed.load());
    CHECK(!gate.FreeWaiting());
}

namespace {

// A sequential-free host: the driver's replay rule needs only Tail and BeforeSteps.
struct ReplayHost {
    bool sawSnapshot = false;
    bool sawReplay   = false;
    int32_t ContextFrameIndex() { return 0; }
    void MainHalf(Ext::RenderPipe::Packet& next, const Ext::RenderPipe::TailPlan&) {
        sawSnapshot = next.snapshot;
        sawReplay   = next.replay;
    }
    Ext::RenderPipe::EmitResult Emit(const Ext::RenderPipe::Packet&, Handoff* h) {
        if (h != nullptr) {
            h->NoteBegun();
            if (!h->PassGate()) h->WaitGate();
            h->Release();
        }
        return Ext::RenderPipe::EmitResult{};
    }
    void Recover(const Ext::RenderPipe::EmitResult&) {}
    void AfterJoin(const Ext::RenderPipe::Packet&) {}
    void BeforePublish(const Ext::RenderPipe::Packet&) {}
    void OverlapSwitch(const Ext::RenderPipe::OverlapGovernor&) {}
    void FaithfulTail() {}
};

}  // namespace

// The copies are taken for the replayed frames only while the replay is asked for, a render thread
// is up and the last steps were long enough for one; the packet says whether the replay is on.
UNIT_TEST(ext_render_pipe_replay_asks_for_copies_after_a_long_step) {
    Handoff h;
    ReplayHost host;
    Ext::RenderPipe::Driver<ReplayHost> driver;
    driver.Attach(&h);
    std::thread render([&] { driver.WorkerLoop(host); });

    Ext::RenderPipe::TailInputs in;
    in.replay = true;

    // No step yet: the governor (off) decides.
    driver.Tail(host, in);
    CHECK(!driver.ReplaySnapshot());
    CHECK(!host.sawSnapshot);
    CHECK(host.sawReplay);

    // A short step: still no copies.
    driver.BeforeSteps(host);
    driver.Tail(host, in);
    CHECK(!driver.ReplaySnapshot());

    // A step longer than kReplayStepUs: the next main half takes them, and keeps taking them on
    // the Frames that do not step.
    driver.BeforeSteps(host);
    std::this_thread::sleep_for(std::chrono::microseconds(Ext::RenderPipe::kReplayStepUs * 3));
    driver.Tail(host, in);
    CHECK(driver.ReplaySnapshot());
    CHECK(host.sawSnapshot);
    driver.Tail(host, in);
    CHECK(driver.ReplaySnapshot());

    // The replay switched off: back to the governor.
    in.replay = false;
    driver.Tail(host, in);
    CHECK(!driver.ReplaySnapshot());
    CHECK(!host.sawSnapshot);
    CHECK(!host.sawReplay);

    driver.BeforeTransition(host);
    h.Stop();
    render.join();
}

// The GPU plan: a frame presented now is complete after everything presented before it and its own
// time; an idle GPU starts at once; the estimate never runs more than kMaxQueued frames ahead.
UNIT_TEST(ext_render_pipe_replay_gpu_plan_queues_frames_behind_each_other) {
    Ext::RenderPipe::GpuPlan plan;
    CHECK_EQ(plan.CompleteIn(1000000, 12000), 12000);   // nothing presented: its own time

    // The S25's stress town: a real frame presented at the end of a 13 ms step, 12 ms of GPU time.
    // 3 ms into the next step a replayed frame would be complete 21 ms from now: not inside the step.
    plan.Presented(1000000, 12000);
    CHECK_EQ(plan.CompleteIn(1003000, 12000), 21000);
    // A desktop: 2 ms of GPU time; the same question 3 ms later answers 2 ms.
    Ext::RenderPipe::GpuPlan fast;
    fast.Presented(1000000, 2000);
    CHECK_EQ(fast.CompleteIn(1003000, 2000), 2000);

    // Two more presented back to back queue behind the first.
    plan.Presented(1003000, 12000);
    plan.Presented(1004000, 12000);
    CHECK_EQ(plan.CompleteIn(1004000, 12000), 44000);
    // The GPU idle since: only the frame's own time again.
    CHECK_EQ(plan.CompleteIn(2000000, 12000), 12000);

    // Presents faster than the GPU's time: capped at kMaxQueued frames ahead.
    for (int32_t i = 0; i < 20; ++i) plan.Presented(3000000 + i * 1000, 12000);
    CHECK_EQ(plan.CompleteIn(3019000, 12000), 12000 * Ext::RenderPipe::GpuPlan::kMaxQueued + 12000);
}

// Sequential (no render thread): the replay is never on.
UNIT_TEST(ext_render_pipe_replay_is_off_without_a_render_thread) {
    ReplayHost host;
    Ext::RenderPipe::Driver<ReplayHost> driver;
    Ext::RenderPipe::TailInputs in;
    in.replay = true;
    driver.BeforeSteps(host);
    std::this_thread::sleep_for(std::chrono::microseconds(Ext::RenderPipe::kReplayStepUs * 3));
    driver.Tail(host, in);
    CHECK(!driver.ReplaySnapshot());
    CHECK(!host.sawSnapshot);
    CHECK(!host.sawReplay);
}
