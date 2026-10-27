// ============================================================================
// tests/unit/ext_touch_pointer_test.cpp -- ext.touch_pointer's pure decisions (P11)
// ============================================================================
// docs/android/P11_TOUCH_POINTER.md: the latch queue (a tap inside one pump still shows a press and a release),
// the tap test, the lift offset, the edge auto-pan, the two-finger gesture classifier (pan, pinch, spin, each
// engaging past its threshold), the pick snap's samples and candidate order with the tap-to-cycle, the action
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
    s = p.Step(390, 500, 610, 500);               // ratio 1.1: log 0.095 > 0.06
    CHECK(p.pinchEngaged() && !p.panEngaged() && !p.spinEngaged());
    CHECK(Near(s.zoomSteps, std::log(1.1f) / kPinchLogPerStep, 1e-4f) && s.zoomSteps > 0.0f);
    s = p.Step(400, 500, 600, 500);               // back together: zoom out by the same amount
    CHECK(Near(s.zoomSteps, -std::log(1.1f) / kPinchLogPerStep, 1e-4f));

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
    const uint32_t all = 0x1Fu;
    const Bar b = LayoutBar(2340, 1080, 119, 15, 32, 49, 900, all);
    CHECK(b.box.x2 == 2340 - 49);
    CHECK(b.box.y2 == 900 - 15);
    CHECK(b.box.x2 - b.box.x1 == 2 * 32 + 5 * 119 + 4 * 15);
    CHECK(b.box.y2 - b.box.y1 == 2 * 32 + 119);
    for (int32_t i = 0; i < kButtonCount; ++i) {
        CHECK(b.button[i].x2 - b.button[i].x1 == 119);
        if (i > 0) CHECK(b.button[i].x1 == b.button[i - 1].x2 + 15);
    }
    CHECK(HitButton(b, b.button[kCancel].x1 + 5, b.button[kCancel].y1 + 5) == kCancel);
    CHECK(HitButton(b, b.box.x1 + 1, b.box.y1 + 1) == -1);   // the frame is not a button
    // A drag tool: Place and Cancel only, packed to the right edge, the others empty.
    const Bar d = LayoutBar(2340, 1080, 119, 15, 32, 49, 900, (1u << kPlace) | (1u << kCancel));
    CHECK(d.button[kRotateLeft].Empty() && d.button[kFlip].Empty());
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
