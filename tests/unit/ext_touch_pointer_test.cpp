// ============================================================================
// tests/unit/ext_touch_pointer_test.cpp -- ext.touch_pointer's pure decisions (P11)
// ============================================================================
// docs/android/P11_TOUCH_POINTER.md: the latch queue (a tap inside one pump still shows a press and a release),
// the tap test, the lift offset, the edge auto-pan, the two-finger gesture classifier (pan, pinch, spin, each
// engaging past its threshold) and its mode lock, the pick snap's samples and candidate order with the tap-to-cycle, the action
// bar's layout, the touch_mode hold's drag mapping, and the registry row with its three value keys.
// ============================================================================
#include "unit.h"
#include "ext/touch_pointer/touch_pointer_math.h"
#include "ext/touch_mode/touch_math.h"
#include "ext/registry.h"
#include "io/settings_file.h"

#include <cmath>
#include <string>
#include <vector>

namespace {

using namespace Ext::TouchPointer;
using IO::SettingsFile::Entries;

bool EntriesLookup(void* ctx, const char* key, std::string& value) {
    const Entries* entries = static_cast<const Entries*>(ctx);
    for (const IO::SettingsFile::Entry& e : *entries) {
        if (e.key == key) {
            value = e.value;
            return true;
        }
    }
    return false;
}

Ext::State ResolveText(const char* text) {
    Entries entries = IO::SettingsFile::Parse(std::string(text));
    return Ext::Resolve(&EntriesLookup, &entries, nullptr, nullptr);
}

PointerState P(float x, float y, bool left) {
    PointerState s;
    s.x = x;
    s.y = y;
    s.left = left;
    return s;
}

bool Near(float a, float b, float eps) {
    return std::fabs(a - b) <= eps;
}

// A two-finger pose: the centroid (cx, cy), the spread d, the pair's angle in degrees (y down).
struct Pose {
    float cx;
    float cy;
    float d;
    float deg;
};

void BeginPose(TwoFingers& g, const Pose& p, float shortEdge = 1000.0f) {
    const float a = p.deg * 3.14159265f / 180.0f;
    const float hx = 0.5f * p.d * std::cos(a);
    const float hy = 0.5f * p.d * std::sin(a);
    g.Begin(p.cx - hx, p.cy - hy, p.cx + hx, p.cy + hy, shortEdge);
}

GestureStep StepPose(TwoFingers& g, const Pose& p) {
    const float a = p.deg * 3.14159265f / 180.0f;
    const float hx = 0.5f * p.d * std::cos(a);
    const float hy = 0.5f * p.d * std::sin(a);
    return g.Step(p.cx - hx, p.cy - hy, p.cx + hx, p.cy + hy);
}

void Accumulate(GestureStep& total, const GestureStep& s) {
    total.dragX += s.dragX;
    total.dragY += s.dragY;
    total.zoomSteps += s.zoomSteps;
    total.yawDegrees += s.yawDegrees;
}

}  // namespace

UNIT_TEST(ext_touch_pointer_latch_queue) {
    LatchQueue q;
    q.Reset(P(10, 10, false));
    // A tap inside one pump: down and up at once. Two polls show the press and then the release.
    q.Push(P(50, 60, true));
    q.Push(P(50, 60, false));
    PointerState s = q.Pop();
    CHECK(s.left && s.x == 50 && s.y == 60);
    s = q.Pop();
    CHECK(!s.left && s.x == 50);
    // Nothing queued: the current state again.
    s = q.Pop();
    CHECK(!s.left && s.x == 50);
    // Moves coalesce: only the last position of a run with the same buttons is shown.
    q.Push(P(60, 60, false));
    q.Push(P(70, 60, false));
    q.Push(P(80, 60, false));
    s = q.Pop();
    CHECK(s.x == 80);
    CHECK(q.Empty());
    // A drag: press, moves while down, release: the moves while down merge, the edges stay.
    q.Push(P(80, 60, true));
    q.Push(P(90, 60, true));
    q.Push(P(95, 60, false));
    CHECK(q.Pop().left);
    s = q.Pop();
    CHECK(!s.left && s.x == 95);
    // Append keeps a state of its own (the release on a button, then the cursor back on the world).
    q.Push(P(100, 100, true));
    q.Push(P(100, 100, false));
    q.Append(P(5, 5, false));
    CHECK(q.Pop().left);
    s = q.Pop();
    CHECK(!s.left && s.x == 100);
    s = q.Pop();
    CHECK(s.x == 5);
    // A push equal to the current state with nothing queued queues nothing.
    q.Push(P(5, 5, false));
    CHECK(q.Empty());
    // A repeated Append holds the same state for one more poll each (the release held over a dialog).
    q.Append(P(5, 5, false), true);
    q.Append(P(5, 5, false), true);
    q.Append(P(9, 9, false));
    CHECK(q.Pop().x == 5);
    CHECK(q.Pop().x == 5);
    CHECK(q.Pop().x == 9);
}

UNIT_TEST(ext_touch_pointer_tap_and_aim) {
    CHECK(IsTap(0.1f, 5.0f, 1080.0f));
    CHECK(!IsTap(0.5f, 5.0f, 1080.0f));        // held too long
    CHECK(!IsTap(0.1f, 40.0f, 1080.0f));       // moved past 3 % of the short edge
    float ax = 0.0f;
    float ay = 0.0f;
    AimPoint(500.0f, 600.0f, 160.0f, ax, ay);
    CHECK(ax == 500.0f && ay == 440.0f);
    AimPoint(500.0f, 100.0f, 160.0f, ax, ay);  // never above the top row
    CHECK(ay == 0.0f);
    AimPoint(500.0f, 100.0f, 0.0f, ax, ay);    // lift off: at the finger
    CHECK(ay == 100.0f);
}

UNIT_TEST(ext_touch_pointer_edge_pan) {
    int32_t dx = 0;
    int32_t dy = 0;
    // 1170 x 540 (the S25 at render scale Half): the band is 6 % of 540 = 32.4 px.
    EdgePan(585.0f, 270.0f, 1170, 540, dx, dy);
    CHECK(dx == 0 && dy == 0);
    EdgePan(10.0f, 270.0f, 1170, 540, dx, dy);
    CHECK(dx == -1 && dy == 0);
    EdgePan(1160.0f, 530.0f, 1170, 540, dx, dy);
    CHECK(dx == 1 && dy == 1);
    EdgePan(600.0f, 20.0f, 1170, 540, dx, dy);
    CHECK(dx == 0 && dy == -1);
}

UNIT_TEST(ext_touch_pointer_two_finger_classifier) {
    CHECK(Near(AngleDelta(170.0f, -170.0f), -20.0f, 1e-3f));
    CHECK(Near(AngleDelta(-170.0f, 170.0f), 20.0f, 1e-3f));

    // Pan: both fingers move right together. Below the threshold nothing, then it follows exactly.
    TwoFingers g;
    g.Begin(400, 500, 600, 500, 1000.0f);
    GestureStep s = g.Step(410, 500, 610, 500);   // 10 px < 20 px (2 %)
    CHECK(!g.panEngaged() && s.dragX == 0.0f);
    s = g.Step(450, 500, 650, 500);               // 50 px total: engaged, this step's 40 px
    CHECK(g.panEngaged());
    CHECK(Near(s.dragX, 0.04f, 1e-5f) && s.dragY == 0.0f);
    CHECK(!g.pinchEngaged() && !g.spinEngaged() && s.zoomSteps == 0.0f && s.yawDegrees == 0.0f);

    // Pinch: the fingers spread symmetrically, the centroid stays: zoom in, no pan, no spin.
    TwoFingers p;
    p.Begin(400, 500, 600, 500, 1000.0f);
    s = p.Step(385, 500, 615, 500);               // ratio 1.15: log 0.140 > 0.06, 30 px > 25 px (2.5 %)
    CHECK(p.pinchEngaged() && !p.panEngaged() && !p.spinEngaged());
    CHECK(Near(s.zoomSteps, std::log(1.15f) / kPinchLogPerStep, 1e-4f) && s.zoomSteps > 0.0f);
    s = p.Step(400, 500, 600, 500);               // back together: zoom out by the same amount
    CHECK(Near(s.zoomSteps, -std::log(1.15f) / kPinchLogPerStep, 1e-4f));

    // Spin: the pair turns clockwise on the screen (y down) about its centre by 10 degrees.
    TwoFingers r;
    r.Begin(400, 500, 600, 500, 1000.0f);
    const float a = 10.0f * 3.14159265f / 180.0f;
    s = r.Step(500 - 100 * std::cos(a), 500 - 100 * std::sin(a), 500 + 100 * std::cos(a), 500 + 100 * std::sin(a));
    CHECK(r.spinEngaged() && !r.pinchEngaged() && !r.panEngaged());
    CHECK(Near(s.yawDegrees, 10.0f, 1e-3f));
    // Small wobbles under every threshold engage nothing.
    TwoFingers w;
    w.Begin(400, 500, 600, 500, 1000.0f);
    s = w.Step(402, 501, 601, 499);
    CHECK(!w.panEngaged() && !w.pinchEngaged() && !w.spinEngaged());
    CHECK(s.dragX == 0.0f && s.zoomSteps == 0.0f && s.yawDegrees == 0.0f);
}

// The mode lock: the first component past its threshold owns the gesture until the next Begin.
UNIT_TEST(ext_touch_pointer_gesture_lock_pan_only) {
    // A two-finger drag whose spread grows 15 % and whose pair tilts 6 degrees on the way: before the lock the
    // spread alone (log 0.14 > 0.06) engaged a zoom. Locked to pan: no zoom, no yaw, the whole drag.
    TwoFingers g;
    BeginPose(g, Pose{500, 500, 200, 0});
    GestureStep total;
    for (int32_t i = 1; i <= 30; ++i) {
        const float k = static_cast<float>(i);
        Accumulate(total, StepPose(g, Pose{500 + 10 * k, 500, 200 + k, 0.2f * k}));
    }
    CHECK(g.panEngaged() && !g.pinchEngaged() && !g.spinEngaged());
    CHECK(total.zoomSteps == 0.0f && total.yawDegrees == 0.0f);
    // Pan engages on the step its total passes 20 px (step 3); from there it follows: 280 px.
    CHECK(Near(total.dragX, 0.28f, 1e-4f) && Near(total.dragY, 0.0f, 1e-5f));
}

UNIT_TEST(ext_touch_pointer_gesture_lock_pinch_drift) {
    // A symmetric pinch out (200 -> 600 px) whose centroid drifts 30 px down (1.5x the pan threshold): no drag.
    TwoFingers p;
    BeginPose(p, Pose{500, 500, 200, 0});
    GestureStep total;
    for (int32_t i = 1; i <= 20; ++i) {
        const float k = static_cast<float>(i);
        Accumulate(total, StepPose(p, Pose{500, 500 + 1.5f * k, 200 + 20 * k, 0}));
    }
    CHECK(p.pinchEngaged() && !p.panEngaged() && !p.spinEngaged());
    CHECK(total.dragX == 0.0f && total.dragY == 0.0f && total.yawDegrees == 0.0f);
    // Engaged at step 2 (40 px of spread; step 1's 20 px is under the 25 px floor).
    CHECK(Near(total.zoomSteps, std::log(600.0f / 220.0f) / kPinchLogPerStep, 1e-3f));

    // A one-thumb pinch: one finger still, the other out 200 px, so the centroid travels 100 px (5x the pan
    // threshold). The spread passes its threshold first: zoom only.
    TwoFingers q;
    q.Begin(400, 500, 600, 500, 1000.0f);
    total = GestureStep();
    for (int32_t i = 1; i <= 20; ++i) {
        Accumulate(total, q.Step(400, 500, 600 + 10 * static_cast<float>(i), 500));
    }
    CHECK(q.pinchEngaged() && !q.panEngaged() && !q.spinEngaged());
    CHECK(total.dragX == 0.0f && total.dragY == 0.0f);
    CHECK(Near(total.zoomSteps, std::log(400.0f / 220.0f) / kPinchLogPerStep, 1e-3f));   // from step 3 (30 px)
}

UNIT_TEST(ext_touch_pointer_gesture_lock_rotate_first) {
    // A 40-degree turn whose centroid drifts 30 px and whose spread wobbles 5 %: yaw only.
    TwoFingers r;
    BeginPose(r, Pose{500, 500, 300, 0});
    GestureStep total;
    for (int32_t i = 1; i <= 20; ++i) {
        const float k = static_cast<float>(i);
        const float wobble = (i % 2 == 0) ? 15.0f : 0.0f;
        Accumulate(total, StepPose(r, Pose{500 + 1.5f * k, 500, 300 + wobble, 2.0f * k}));
    }
    CHECK(r.spinEngaged() && !r.panEngaged() && !r.pinchEngaged());
    CHECK(total.dragX == 0.0f && total.dragY == 0.0f && total.zoomSteps == 0.0f);
    CHECK(Near(total.yawDegrees, 32.0f, 1e-2f));   // engaged at 10 degrees (step 5), then follows
}

UNIT_TEST(ext_touch_pointer_gesture_lock_wide_one_thumb_pinch) {
    // Fingers 70 % of the short edge apart, one still, the other closing 15 % of the spread (700 -> 595 px) in
    // 5 px steps. At 45 px the centroid has travelled 22.5 px (pan ratio 1.125) and the spread log 0.066 (pinch
    // ratio 1.107): pan is furthest past its threshold, but one finger has not moved at all (under 0.4x the
    // centroid's travel), so it is a pinch. Zoom out from that step, no drag.
    TwoFingers g;
    g.Begin(150, 500, 850, 500, 1000.0f);
    GestureStep total;
    for (int32_t i = 1; i <= 21; ++i) {
        Accumulate(total, g.Step(150, 500, 850 - 5 * static_cast<float>(i), 500));
    }
    CHECK(g.pinchEngaged() && !g.panEngaged() && !g.spinEngaged());
    CHECK(total.dragX == 0.0f && total.dragY == 0.0f && total.yawDegrees == 0.0f);
    CHECK(Near(total.zoomSteps, std::log(595.0f / 660.0f) / kPinchLogPerStep, 1e-3f));   // from step 9
}

UNIT_TEST(ext_touch_pointer_gesture_lock_pan_with_spread_jitter) {
    // A two-finger drag whose spread jitters 3 % (200 px, +-6 px every other step): still a pan, no zoom.
    TwoFingers g;
    BeginPose(g, Pose{500, 500, 200, 0});
    GestureStep total;
    for (int32_t i = 1; i <= 20; ++i) {
        const float k = static_cast<float>(i);
        const float jitter = (i % 2 == 0) ? -6.0f : 6.0f;
        Accumulate(total, StepPose(g, Pose{500 + 10 * k, 500, 200 + jitter, 0}));
    }
    CHECK(g.panEngaged() && !g.pinchEngaged() && !g.spinEngaged());
    CHECK(total.zoomSteps == 0.0f && total.yawDegrees == 0.0f);
    CHECK(Near(total.dragX, 0.18f, 1e-4f));   // engaged at 30 px (step 3), then follows to 200 px

    // The S25 case: thumbs 400 px apart whose spread has jittered 22 px by the time the centroid has travelled
    // 22 px (pan ratio 1.1, pinch ratio 0.86). Both fingers moved (11 and 33 px): pan.
    TwoFingers w;
    w.Begin(300, 500, 700, 500, 1000.0f);
    const GestureStep s = w.Step(311, 500, 733, 500);
    CHECK(w.panEngaged() && !w.pinchEngaged() && !w.spinEngaged());
    CHECK(Near(s.dragX, 0.022f, 1e-5f) && s.zoomSteps == 0.0f);
}

UNIT_TEST(ext_touch_pointer_gesture_lock_pan_asymmetric_speeds) {
    // A drag right with one finger at 70 % of the other's speed (10 and 7 px a step): the spread closes 60 px
    // over the drag, but both fingers move: pan from step 3 (centroid 25.5 px), no zoom.
    TwoFingers g;
    g.Begin(400, 500, 600, 500, 1000.0f);
    GestureStep total;
    for (int32_t i = 1; i <= 20; ++i) {
        const float k = static_cast<float>(i);
        Accumulate(total, g.Step(400 + 10 * k, 500, 600 + 7 * k, 500));
    }
    CHECK(g.panEngaged() && !g.pinchEngaged() && !g.spinEngaged());
    CHECK(total.zoomSteps == 0.0f && total.yawDegrees == 0.0f);
    CHECK(Near(total.dragX, 0.153f, 1e-4f));   // 170 px less the 17 px before the lock
}

UNIT_TEST(ext_touch_pointer_gesture_lock_pan_drifting_apart) {
    // Both fingers drag down together while the pair drifts apart slowly (200 -> 220 px, +10 % over the drag):
    // pan, no zoom.
    TwoFingers g;
    BeginPose(g, Pose{500, 500, 200, 0});
    GestureStep total;
    for (int32_t i = 1; i <= 20; ++i) {
        const float k = static_cast<float>(i);
        Accumulate(total, StepPose(g, Pose{500, 500 + 10 * k, 200 + k, 0}));
    }
    CHECK(g.panEngaged() && !g.pinchEngaged() && !g.spinEngaged());
    CHECK(total.zoomSteps == 0.0f && total.yawDegrees == 0.0f);
    CHECK(Near(total.dragX, 0.0f, 1e-5f) && Near(total.dragY, 0.18f, 1e-4f));
}

// Fingers close together (index + middle, 120 px apart on the S25's 1080 short edge): the px floor (27 px).
UNIT_TEST(ext_touch_pointer_gesture_lock_close_fingers) {
    // A 150 px pan whose spread jitters +-8 px and whose angle jitters +-5 degrees every step. The relative
    // thresholds alone (7 px of spread, 8 degrees) would lock to pinch on the first step; the floor keeps both
    // under 27 px, so pan engages at step 3 (30 px > 21.6 px) and follows.
    TwoFingers g;
    BeginPose(g, Pose{500, 500, 120, 0}, 1080.0f);
    GestureStep total;
    for (int32_t i = 1; i <= 15; ++i) {
        const float k = static_cast<float>(i);
        const float sign = (i % 2 == 0) ? -1.0f : 1.0f;
        Accumulate(total, StepPose(g, Pose{500 + 10 * k, 500, 120 + 8 * sign, 5 * sign}));
    }
    CHECK(g.panEngaged() && !g.pinchEngaged() && !g.spinEngaged());
    CHECK(total.zoomSteps == 0.0f && total.yawDegrees == 0.0f);
    CHECK(Near(total.dragX, 130.0f / 1080.0f, 1e-4f));   // 150 px less the 20 px before the lock

    // A deliberate pinch of close fingers, 120 -> 60 px in 5 px steps: engaged at 30 px of spread (step 6).
    TwoFingers p;
    BeginPose(p, Pose{500, 500, 120, 0}, 1080.0f);
    total = GestureStep();
    for (int32_t i = 1; i <= 12; ++i) {
        Accumulate(total, StepPose(p, Pose{500, 500, 120 - 5 * static_cast<float>(i), 0}));
    }
    CHECK(p.pinchEngaged() && !p.panEngaged() && !p.spinEngaged());
    CHECK(total.dragX == 0.0f && total.dragY == 0.0f && total.yawDegrees == 0.0f);
    CHECK(Near(total.zoomSteps, std::log(60.0f / 95.0f) / kPinchLogPerStep, 1e-3f));

    // A deliberate 40-degree twist of close fingers in 2-degree steps: the arc passes 27 px at 14 degrees (step 7).
    TwoFingers r;
    BeginPose(r, Pose{500, 500, 120, 0}, 1080.0f);
    total = GestureStep();
    for (int32_t i = 1; i <= 20; ++i) {
        Accumulate(total, StepPose(r, Pose{500, 500, 120, 2 * static_cast<float>(i)}));
    }
    CHECK(r.spinEngaged() && !r.panEngaged() && !r.pinchEngaged());
    CHECK(total.dragX == 0.0f && total.dragY == 0.0f && total.zoomSteps == 0.0f);
    CHECK(Near(total.yawDegrees, 28.0f, 1e-2f));
}

UNIT_TEST(ext_touch_pointer_gesture_lock_join_and_reset) {
    // Pinch locked: a 15-degree twist (under 3x the spin threshold) is dropped; past 24 degrees spin joins and
    // follows from that step. Pan never joins, however far the centroid then travels.
    TwoFingers g;
    BeginPose(g, Pose{500, 500, 200, 0});
    GestureStep s = StepPose(g, Pose{500, 500, 240, 0});
    CHECK(g.pinchEngaged() && s.zoomSteps > 0.0f);
    s = StepPose(g, Pose{500, 500, 240, 15});
    CHECK(!g.spinEngaged() && s.yawDegrees == 0.0f);
    s = StepPose(g, Pose{500, 500, 240, 30});
    CHECK(g.spinEngaged() && Near(s.yawDegrees, 15.0f, 1e-3f));
    s = StepPose(g, Pose{700, 500, 240, 30});
    CHECK(!g.panEngaged() && s.dragX == 0.0f);

    // Lift and touch again: undecided, and a plain drag now pans with no zoom or yaw.
    BeginPose(g, Pose{500, 500, 200, 0});
    CHECK(!g.panEngaged() && !g.pinchEngaged() && !g.spinEngaged());
    s = StepPose(g, Pose{550, 500, 200, 0});
    CHECK(g.panEngaged() && !g.pinchEngaged() && !g.spinEngaged());
    CHECK(Near(s.dragX, 0.05f, 1e-5f) && s.zoomSteps == 0.0f && s.yawDegrees == 0.0f);
    // And the reverse: a pan-locked gesture, lifted, then a pinch zooms.
    BeginPose(g, Pose{500, 500, 200, 0});
    s = StepPose(g, Pose{500, 500, 260, 0});
    CHECK(g.pinchEngaged() && !g.panEngaged() && s.zoomSteps > 0.0f && s.dragX == 0.0f);
}

UNIT_TEST(ext_touch_pointer_hold_drag_to_world) {
    // touch_mode's mapping: the camera moves against the fingers along retail's ground basis.
    float dx = 0.0f;
    float dy = 0.0f;
    Ext::TouchMode::FingerDragToWorld(0.1f, 0.0f, 0.0f, 50.0f, dx, dy);   // yaw 0: right = (1, 0)
    CHECK(Near(dx, -0.1f * 50.0f * Ext::TouchMode::kFingerDragWorldPerEdge, 1e-4f) && Near(dy, 0.0f, 1e-4f));
    Ext::TouchMode::FingerDragToWorld(0.0f, 0.1f, 0.0f, 50.0f, dx, dy);   // down: the Back axis is (0, -1)
    CHECK(Near(dx, 0.0f, 1e-4f) && Near(dy, 0.1f * 50.0f * Ext::TouchMode::kFingerDragWorldPerEdge, 1e-4f));
    Ext::TouchMode::FingerDragToWorld(0.1f, 0.0f, 90.0f, 50.0f, dx, dy);  // yaw 90: right = (0, 1)
    CHECK(Near(dx, 0.0f, 1e-3f) && dy < 0.0f);
}

UNIT_TEST(ext_touch_pointer_snap_samples_and_cycle) {
    const std::vector<Sample> s = RingSamples(100.0f, 200.0f, 30.0f);
    CHECK(s.size() == 1u + 8u + 12u + 16u);
    CHECK(s[0].x == 100.0f && s[0].y == 200.0f && s[0].distance == 0.0f);
    for (size_t i = 1; i < s.size(); ++i) {
        CHECK(s[i].distance >= s[i - 1].distance);
        const float d = std::sqrt((s[i].x - 100.0f) * (s[i].x - 100.0f) + (s[i].y - 200.0f) * (s[i].y - 200.0f));
        CHECK(Near(d, s[i].distance, 1e-3f));
        CHECK(s[i].distance <= 30.0f + 1e-3f);
    }
    CHECK(RingSamples(0.0f, 0.0f, 0.0f).size() == 1u);

    // Hits: entity 7 at 20 and 10, entity 3 at 30, entity 5 at 10 (after 7's 10 in sample order), misses.
    std::vector<Hit> hits;
    auto add = [&](uintptr_t e, float d) {
        Hit h;
        h.entity = e;
        h.sample.distance = d;
        h.sample.x = d;
        hits.push_back(h);
    };
    add(0, 0.0f);
    add(7, 20.0f);
    add(7, 10.0f);
    add(5, 10.0f);
    add(3, 30.0f);
    add(0, 10.0f);
    const std::vector<Hit> c = Candidates(hits);
    CHECK(c.size() == 3u);
    CHECK(c[0].entity == 7 && c[0].sample.distance == 10.0f);   // its nearest sample
    CHECK(c[1].entity == 5);
    CHECK(c[2].entity == 3);
    // Tap-to-cycle: a fresh tap takes the nearest; a continuing one the next after the last choice, wrapping.
    CHECK(ChooseCandidate(c, 0) == 0);
    CHECK(ChooseCandidate(c, 7) == 1);
    CHECK(ChooseCandidate(c, 5) == 2);
    CHECK(ChooseCandidate(c, 3) == 0);
    CHECK(ChooseCandidate(c, 99) == 0);   // the last choice walked away: the nearest
    CHECK(ChooseCandidate(std::vector<Hit>(), 7) == -1);
}

UNIT_TEST(ext_touch_pointer_bar_layout) {
    // The S25 at UI 100 (layout = 2340 x 1080 px): cell 119, gap 15, frame 32, margin 49, above the toolbar at 900.
    const uint32_t all = 0x3Fu;
    const Bar b = LayoutBar(2340, 1080, 119, 15, 32, 49, 900, all);
    CHECK(b.box.x2 == 2340 - 49);
    CHECK(b.box.y2 == 900 - 15);
    CHECK(b.box.x2 - b.box.x1 == 2 * 32 + 6 * 119 + 5 * 15);
    CHECK(b.box.y2 - b.box.y1 == 2 * 32 + 119);
    for (int32_t i = 0; i < kButtonCount; ++i) {
        CHECK(b.button[i].x2 - b.button[i].x1 == 119);
        if (i > 0) CHECK(b.button[i].x1 == b.button[i - 1].x2 + 15);
    }
    CHECK(HitButton(b, b.button[kCancel].x1 + 5, b.button[kCancel].y1 + 5) == kCancel);
    CHECK(HitButton(b, b.box.x1 + 1, b.box.y1 + 1) == -1);   // the frame is not a button
    // A drag tool: Place and Cancel only, packed to the right edge, the others empty.
    const Bar d = LayoutBar(2340, 1080, 119, 15, 32, 49, 900, (1u << kPlace) | (1u << kCancel));
    CHECK(d.button[kRotateLeft].Empty() && d.button[kFlip].Empty() && d.button[kShift].Empty());
    // The road tool: Place, Shift and Cancel, Shift between the two.
    const Bar r = LayoutBar(2340, 1080, 119, 15, 32, 49, 900, (1u << kPlace) | (1u << kShift) | (1u << kCancel));
    CHECK(r.button[kShift].x1 == r.button[kPlace].x2 + 15 && r.button[kCancel].x1 == r.button[kShift].x2 + 15);
    CHECK(HitButton(r, r.button[kShift].x1 + 3, r.button[kShift].y1 + 3) == kShift);
    CHECK(d.button[kCancel].x2 == 2340 - 49 - 32);
    CHECK(d.button[kCancel].x1 == d.button[kPlace].x2 + 15);
    // No toolbar: the margin above the bottom. Nothing shown: no bar.
    CHECK(LayoutBar(2340, 1080, 119, 15, 32, 49, 0, all).box.y2 == 1080 - 49);
    CHECK(LayoutBar(2340, 1080, 119, 15, 32, 49, 900, 0).box.Empty());
}

UNIT_TEST(ext_touch_pointer_registry_row_and_keys) {
    const Ext::Entry* e = Ext::FindEntry("touch_pointer");
    CHECK(e != nullptr);
    if (e == nullptr) return;
    CHECK(e->tier == Ext::Tier::Extension);
    CHECK(e->cls == Ext::Class::A);
    CHECK(!e->defaultOn);
    CHECK(Ext::SettingsKey(*e) == "ext.touch_pointer");
    CHECK(!ResolveText("").ext.touch_pointer);
    CHECK(ResolveText("ext.touch_pointer=1\n").ext.touch_pointer);
    CHECK(!ResolveText("ext.touch_pointer=1\nprofile=faithful\n").ext.touch_pointer);

    const Ext::ValueKey* lift = Ext::FindValueKey("touch_pointer_lift");
    const Ext::ValueKey* radius = Ext::FindValueKey("touch_pointer_pick_radius");
    const Ext::ValueKey* twice = Ext::FindValueKey("touch_pointer_tap_twice");
    CHECK(lift != nullptr && radius != nullptr && twice != nullptr);
    if (lift == nullptr || radius == nullptr || twice == nullptr) return;
    CHECK(std::string(lift->owner) == "touch_pointer");
    CHECK(lift->defaultValue == 160 && Ext::Values().touch_pointer_lift == 160);
    CHECK(radius->defaultValue == 50 && Ext::Values().touch_pointer_pick_radius == 50);
    CHECK(twice->defaultValue == 0 && Ext::Values().touch_pointer_tap_twice == 0);
    CHECK(ResolveText("ext.touch_pointer_lift=240\n").val.touch_pointer_lift == 240);
    CHECK(Ext::ClampValue(*lift, 0) == 0);
    CHECK(Ext::ClampValue(*lift, 150) == 160);   // not one of the steps: the default
    CHECK(Ext::ClampValue(*radius, 75) == 75);
    CHECK(Ext::ClampValue(*twice, 1) == 1);
    CHECK(Ext::ClampValue(*twice, 5) == 1);      // a range key clamps
}

// P11b (docs/android/P11B_POINTER.md §3): the Enhanced tab's "Touch pointer" presets. Every preset value is one the
// registry key accepts as it is (a preset that clamped would read back as Custom), the presets are distinct, and a
// stored set no preset matches is Custom.
UNIT_TEST(ext_touch_pointer_presets) {
    const Ext::ValueKey* lift = Ext::FindValueKey("touch_pointer_lift");
    const Ext::ValueKey* radius = Ext::FindValueKey("touch_pointer_pick_radius");
    const Ext::ValueKey* twice = Ext::FindValueKey("touch_pointer_tap_twice");
    CHECK(lift != nullptr && radius != nullptr && twice != nullptr);
    if (lift == nullptr || radius == nullptr || twice == nullptr) return;
    for (int32_t i = 0; i < kTouchPresetCount; ++i) {
        const TouchPreset& p = kTouchPresets[i];
        CHECK(Ext::ClampValue(*lift, p.lift) == p.lift);
        CHECK(Ext::ClampValue(*radius, p.pickRadius) == p.pickRadius);
        CHECK(Ext::ClampValue(*twice, p.tapTwice) == p.tapTwice);
        CHECK(TouchPresetItem(true, p.lift, p.pickRadius, p.tapTwice) == i + 1);
    }
    // The first preset is the keys' defaults.
    CHECK(kTouchPresets[0].lift == lift->defaultValue);
    CHECK(kTouchPresets[0].pickRadius == radius->defaultValue);
    CHECK(kTouchPresets[0].tapTwice == twice->defaultValue);
    CHECK(TouchPresetItem(false, 160, 50, 0) == kTouchItemOff);
    CHECK(TouchPresetItem(true, 200, 50, 0) == kTouchItemCustom);
    CHECK(TouchPresetItem(true, 160, 100, 1) == kTouchItemCustom);
    CHECK(kTouchItemCustom == kTouchPresetCount + 1);
}

// P11b: a finger held on a dialog keeps the cursor still inside the hold slop (the press-and-hold tooltip).
UNIT_TEST(ext_touch_pointer_hold_slop) {
    const float shortEdge = 1080.0f;   // the S25 in landscape
    CHECK(!LeftHoldSlop(0.0f, 0.0f, shortEdge));
    CHECK(!LeftHoldSlop(6.0f, -6.0f, shortEdge));    // a trembling finger
    CHECK(LeftHoldSlop(14.0f, 0.0f, shortEdge));     // past 0.012 * 1080 = 13 px
    CHECK(LeftHoldSlop(0.0f, -40.0f, shortEdge));
    CHECK(kHoldSlopFraction < kTapSlopFraction);      // a slider drag starts well inside a tap's slop
}

// Review F-258: Shift and the UI hold belong to one tool selection. A new tool at the freed tool's
// address (another group) is another selection; the same tool in the same group is the same one.
UNIT_TEST(ext_touch_pointer_tool_selection_is_not_the_address_alone) {
    static int block;
    static int woodHouse;
    static int stoneHouse;
    ToolSelection shift;
    shift.tool = &block;
    shift.group = &woodHouse;
    ToolSelection now = shift;
    CHECK(now == shift);
    now.group = &stoneHouse;   // StoneHouse's PlacementTool at the WoodHouse tool's address
    CHECK(!(now == shift));
    now.group = nullptr;       // the EditTool there (SelectTool(nullptr))
    CHECK(!(now == shift));
    CHECK(ToolSelection() == ToolSelection());
}
