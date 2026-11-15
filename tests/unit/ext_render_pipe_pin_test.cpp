// ============================================================================
// tests/unit/ext_render_pipe_pin_test.cpp -- ext.render_pipe's resource pin
// (src/ext/render_pipe/resource_pin.h, P13_OVERLAP.md section 2.8)
// ============================================================================
// The snapshot shares a HighlightComponent's outline EdgeMesh raw, and a step may delete it
// while the overlapped emit's pass 2 still draws it. The pin makes that free wait until the
// emit says its reads are over, and lets every other free through at once. A std::thread
// plays the render thread. The pointers are identities only (elements of a local array);
// nothing depends on a heap address.
// ============================================================================
#include "unit.h"
#include "ext/render_pipe/resource_pin.h"

#include <atomic>
#include <chrono>
#include <thread>

namespace {

using Ext::RenderPipe::PinGate;
using Ext::RenderPipe::PinSet;
using Ext::RenderPipe::PinStats;

constexpr int64_t kLongBoundUs = 10000000;   // 10 s: never reached by a test that ends its reads

}  // namespace

UNIT_TEST(ext_render_pipe_pin_set_holds_each_resource_once) {
    int meshes[3] = {};
    PinSet set;
    set.Add(&meshes[0]);
    set.Add(&meshes[0]);
    set.Add(nullptr);
    set.Add(&meshes[1]);
    CHECK(set.Size() == 2);
    CHECK(set.Holds(&meshes[0]));
    CHECK(set.Holds(&meshes[1]));
    CHECK(!set.Holds(&meshes[2]));
    set.Clear();
    CHECK(set.Size() == 0);
    CHECK(!set.Holds(&meshes[0]));
}

// Nothing in flight, a packet without copies, a resource the packet does not share, a packet
// whose reads are over: the free goes at once and nothing waits.
UNIT_TEST(ext_render_pipe_pin_lets_unpinned_frees_through) {
    int meshes[2] = {};
    PinSet set;
    set.Add(&meshes[0]);
    PinGate gate;

    gate.BeforeFree(&meshes[0], kLongBoundUs);   // nothing published yet
    gate.Publish(nullptr);                       // a packet without copies
    gate.BeforeFree(&meshes[0], kLongBoundUs);
    gate.Publish(&set);
    CHECK(gate.Pinned(&meshes[0]));
    CHECK(!gate.Pinned(&meshes[1]));
    gate.BeforeFree(&meshes[1], kLongBoundUs);   // not this packet's
    gate.ReadsOver();
    CHECK(!gate.Pinned(&meshes[0]));
    gate.BeforeFree(&meshes[0], kLongBoundUs);   // its reads are over
    gate.ReadsOver();                            // idempotent

    const PinStats s = gate.TakeStats();
    CHECK(s.checks == 4);
    CHECK(s.waits == 0);
    CHECK(s.timeouts == 0);
    const PinStats again = gate.TakeStats();
    CHECK(again.checks == 0);
}

// The race the pin closes: the render thread still reads the mesh when the step frees it. The
// free returns only after the reads are over, and everything the render thread did before
// ReadsOver is visible to it.
UNIT_TEST(ext_render_pipe_pin_free_waits_for_the_reads) {
    int meshes[1] = {};
    PinSet set;
    set.Add(&meshes[0]);
    PinGate gate;
    gate.Publish(&set);

    std::atomic<bool> started{false};
    int drawn = 0;   // written by the "render thread" before ReadsOver, read by the freer after
    std::thread render([&]() {
        started.store(true, std::memory_order_release);
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        drawn = 1;
        gate.ReadsOver();
    });
    while (!started.load(std::memory_order_acquire)) std::this_thread::yield();
    gate.BeforeFree(&meshes[0], kLongBoundUs);
    const int seen = drawn;
    render.join();

    CHECK(seen == 1);
    const PinStats s = gate.TakeStats();
    CHECK(s.checks == 1);
    CHECK(s.waits == 1);
    CHECK(s.timeouts == 0);
    CHECK(s.waitUs >= 1000);   // it really waited (the reads took 30 ms)
    CHECK(s.maxUs == s.waitUs);
}

// The next packet: a publish re-arms the gate with its own set, and the previous packet's
// resources are no longer pinned.
UNIT_TEST(ext_render_pipe_pin_publish_rearms_with_the_next_set) {
    int meshes[2] = {};
    PinSet first;
    PinSet second;
    first.Add(&meshes[0]);
    second.Add(&meshes[1]);
    PinGate gate;
    gate.Publish(&first);
    gate.ReadsOver();
    gate.Publish(&second);
    CHECK(!gate.Pinned(&meshes[0]));
    CHECK(gate.Pinned(&meshes[1]));
    gate.BeforeFree(&meshes[0], kLongBoundUs);
    gate.ReadsOver();
    gate.BeforeFree(&meshes[1], kLongBoundUs);
    CHECK(gate.TakeStats().waits == 0);
}

// The bound: reads that never end let the free go after it, counted as a timeout.
UNIT_TEST(ext_render_pipe_pin_gives_up_at_the_bound) {
    int meshes[1] = {};
    PinSet set;
    set.Add(&meshes[0]);
    PinGate gate;
    gate.Publish(&set);
    gate.BeforeFree(&meshes[0], 20000);   // 20 ms, no ReadsOver
    const PinStats s = gate.TakeStats();
    CHECK(s.waits == 1);
    CHECK(s.timeouts == 1);
    CHECK(s.waitUs >= 20000);
    CHECK(gate.Pinned(&meshes[0]));   // the packet's reads are still not over
}
