// ============================================================================
// tests/unit/stagger_hold_test.cpp -- ext.staggered_passes' hold decision
// (src/engine/graphics/stagger_hold.h)
// ============================================================================
// The pure decision on its own: every <= 1 is retail; a pass holds at most
// every - 1 frames after a real render and renders on its scheduled frame (the
// shadow on frame % every == 0, the reflection a frame later); a key change, an
// epoch bump (Invalidate), a projection change, a turn or a move past the
// tolerance render at once; the frame index going backwards renders.
// ============================================================================
#include "unit.h"
#include "graphics/stagger_hold.h"

#include <cmath>
#include <cstdint>

namespace {

using Graphics::StaggerHold::Key;
using Graphics::StaggerHold::State;
using Graphics::StaggerHold::View;

namespace SH = Graphics::StaggerHold;

View StillView() {
    View v;
    // axisX, axisY, axisZ, translation
    const float xf[12] = { 1, 0, 0, 0, 0.6f, -0.8f, 0, 0.8f, 0.6f, 100, 200, 50 };
    for (int i = 0; i < 12; ++i) v.xf[i] = xf[i];
    for (int i = 0; i < 16; ++i) v.proj[i] = (i % 5 == 0) ? 1.5f : 0.0f;
    return v;
}

Key SomeKey() {
    static int scene = 0;
    static int target = 0;
    Key k;
    k.a = &scene;
    k.b = &target;
    k.c = 2;
    k.d = 2048;
    k.epoch = SH::Epoch();
    return k;
}

}  // namespace

UNIT_TEST(stagger_hold_every_one_is_retail) {
    State s;
    const View v = StillView();
    const Key k = SomeKey();
    SH::Latch(s, 10, k, v);
    for (int32_t f = 11; f < 20; ++f) {
        CHECK(!SH::ShouldHold(s, 1, SH::kShadowPhase, f, k, v, 1.0f));
        CHECK(!SH::ShouldHold(s, 0, SH::kShadowPhase, f, k, v, 1.0f));
    }
}

UNIT_TEST(stagger_hold_nothing_rendered_yet_renders) {
    State s;
    CHECK(!SH::ShouldHold(s, 2, SH::kShadowPhase, 1, SomeKey(), StillView(), 1.0f));
}

UNIT_TEST(stagger_hold_still_view_alternates_two_passes) {
    // Every frame the caller renders when told to and latches; count renders per pass.
    State shadow;
    State refl;
    const View v = StillView();
    const Key k = SomeKey();
    int shadowRenders = 0;
    int reflRenders = 0;
    for (int32_t f = 100; f < 140; ++f) {
        const bool holdS = SH::ShouldHold(shadow, 2, SH::kShadowPhase, f, k, v, 1.0f);
        const bool holdR = SH::ShouldHold(refl, 2, SH::kReflectionPhase, f, k, v, 1.0f);
        if (!holdS) {
            SH::Latch(shadow, f, k, v);
            ++shadowRenders;
        }
        if (!holdR) {
            SH::Latch(refl, f, k, v);
            ++reflRenders;
        }
        if (f > 101) {
            // Past the first frames, exactly one of the two renders each frame, and never stale
            // by more than one frame.
            CHECK(holdS != holdR);
            CHECK(f - shadow.frame <= 1);
            CHECK(f - refl.frame <= 1);
        }
    }
    CHECK(shadowRenders >= 20 && shadowRenders <= 21);
    CHECK(reflRenders >= 20 && reflRenders <= 21);
}

UNIT_TEST(stagger_hold_never_past_every_minus_one) {
    for (int32_t every = 2; every <= 8; ++every) {
        State s;
        const View v = StillView();
        const Key k = SomeKey();
        int32_t longest = 0;
        for (int32_t f = 1; f < 200; ++f) {
            if (!SH::ShouldHold(s, every, SH::kReflectionPhase, f, k, v, 1.0f)) {
                SH::Latch(s, f, k, v);
            }
            if (f - s.frame > longest) longest = f - s.frame;
        }
        CHECK_EQ(longest, every - 1);
    }
}

UNIT_TEST(stagger_hold_forced_refreshes) {
    State s;
    const View v = StillView();
    const Key k = SomeKey();
    // Frame 11 with every = 2 and the shadow phase is a hold frame (11 % 2 != 0).
    SH::Latch(s, 10, k, v);
    CHECK(SH::ShouldHold(s, 2, SH::kShadowPhase, 11, k, v, 1.0f));

    // The same frame or one before it: render.
    CHECK(!SH::ShouldHold(s, 2, SH::kShadowPhase, 10, k, v, 1.0f));
    CHECK(!SH::ShouldHold(s, 2, SH::kShadowPhase, 9, k, v, 1.0f));

    // Each part of the key.
    Key k2 = k;
    k2.c = 1;
    CHECK(!SH::ShouldHold(s, 2, SH::kShadowPhase, 11, k2, v, 1.0f));
    k2 = k;
    k2.d = 4096;
    CHECK(!SH::ShouldHold(s, 2, SH::kShadowPhase, 11, k2, v, 1.0f));
    k2 = k;
    k2.a = &k2;
    CHECK(!SH::ShouldHold(s, 2, SH::kShadowPhase, 11, k2, v, 1.0f));

    // Invalidate moves the epoch a fresh key carries.
    SH::Invalidate();
    CHECK(!SH::ShouldHold(s, 2, SH::kShadowPhase, 11, SomeKey(), v, 1.0f));

    // The projection (a zoom or an aspect change), by its bits.
    View z = v;
    z.proj[0] = std::nextafter(z.proj[0], 2.0f);
    CHECK(!SH::ShouldHold(s, 2, SH::kShadowPhase, 11, k, z, 1.0f));
}

UNIT_TEST(stagger_hold_motion_tolerance) {
    State s;
    const View v = StillView();
    const Key k = SomeKey();
    SH::Latch(s, 10, k, v);

    // A move inside the tolerance holds; past it renders; a NaN renders.
    View m = v;
    m.xf[9] += 0.5f;
    CHECK(SH::ShouldHold(s, 2, SH::kShadowPhase, 11, k, m, 1.0f));
    m.xf[9] = v.xf[9] + 1.5f;
    CHECK(!SH::ShouldHold(s, 2, SH::kShadowPhase, 11, k, m, 1.0f));
    m.xf[9] = NAN;
    CHECK(!SH::ShouldHold(s, 2, SH::kShadowPhase, 11, k, m, 1.0f));
    // A zero tolerance holds only the exact view.
    CHECK(SH::ShouldHold(s, 2, SH::kShadowPhase, 11, k, v, 0.0f));
    m = v;
    m.xf[10] = std::nextafter(v.xf[10], 1000.0f);
    CHECK(!SH::ShouldHold(s, 2, SH::kShadowPhase, 11, k, m, 0.0f));

    // A turn of about 0.1 degree holds, 1 degree renders (about the vertical axis).
    auto turned = [&](float degrees) {
        View t = v;
        const float a = degrees * 3.14159265f / 180.0f;
        const float c = std::cos(a);
        const float sn = std::sin(a);
        for (int axis = 0; axis < 3; ++axis) {
            const float x = v.xf[axis * 3 + 0];
            const float y = v.xf[axis * 3 + 1];
            t.xf[axis * 3 + 0] = c * x - sn * y;
            t.xf[axis * 3 + 1] = sn * x + c * y;
        }
        return t;
    };
    CHECK(SH::ShouldHold(s, 2, SH::kShadowPhase, 11, k, turned(0.1f), 1.0f));
    CHECK(!SH::ShouldHold(s, 2, SH::kShadowPhase, 11, k, turned(1.0f), 1.0f));
    CHECK(!SH::ShouldHold(s, 2, SH::kShadowPhase, 11, k, turned(180.0f), 1.0f));

    // The tolerance is measured from the rendered view, not the last frame's: two small
    // moves in a row add up.
    m = v;
    m.xf[9] += 0.6f;
    CHECK(SH::ShouldHold(s, 4, SH::kReflectionPhase, 12, k, m, 1.0f));
    m.xf[9] += 0.6f;
    CHECK(!SH::ShouldHold(s, 4, SH::kReflectionPhase, 13, k, m, 1.0f));
}
