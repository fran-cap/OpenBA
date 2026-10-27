// ============================================================================
// src/ext/touch_pointer/touch_pointer_math.h -- ext.touch_pointer's pure decisions (unit-tested)
// ============================================================================
// NOT RETAIL (docs/android/P11_TOUCH_POINTER.md). Plain arithmetic, no engine type: the latch queue
// of pointer states, the tap test, the two-finger gesture (pan, pinch, spin), the aim point under a
// lift offset, the edge auto-pan, the pick snap's ring samples and candidate order with the
// tap-to-cycle, and the action bar's layout. Pointer coordinates are client engine pixels (the
// cursor's unit); bar rectangles are the game UI's layout units.
// ============================================================================
#pragma once

#include <cstdint>
#include <vector>

namespace Ext {
namespace TouchPointer {

// ---- the latch queue -----------------------------------------------------------------------

// One step's pointer: where the cursor is and which buttons are down.
struct PointerState {
    float x = 0.0f;
    float y = 0.0f;
    bool left = false;
    bool right = false;
    bool operator==(const PointerState& o) const {
        return x == o.x && y == o.y && left == o.left && right == o.right;
    }
};

// The states still to be shown, one per input poll. A state with the same buttons as the last queued
// one replaces it (a move coalesces); a button change always queues, so a press and a release that came
// in one pump are shown on two polls (no tap is shorter than one step). Pop: the next state, or the
// current one again when nothing is queued.
class LatchQueue {
public:
    void Push(const PointerState& s);
    // Queue `s` as a state of its own (never merged into the last one), unless it equals it and `repeat` is
    // false (a repeated state holds the pointer still for one more poll).
    void Append(const PointerState& s, bool repeat = false);
    PointerState Pop();
    const PointerState& Current() const { return _current; }
    bool Empty() const { return _queue.empty(); }
    void Reset(const PointerState& s);
    // The last state that will be shown (the queued tail, else the current one).
    const PointerState& Last() const { return _queue.empty() ? _current : _queue.back(); }

private:
    PointerState _current;
    std::vector<PointerState> _queue;
};

// ---- taps -------------------------------------------------------------------------------------

constexpr float kTapSeconds = 0.35f;      // a tap is shorter than this
constexpr float kTapSlopFraction = 0.03f; // and travels less than this fraction of the short edge

bool IsTap(float seconds, float travel, float shortEdge);

// P11b: a finger held on a dialog keeps the cursor at its press point until it has moved this fraction of the
// short edge away (a finger trembles; retail's tooltip delay counts only while the cursor stands still). Once out
// of the slop the cursor follows the finger for the rest of the press.
constexpr float kHoldSlopFraction = 0.012f;

bool LeftHoldSlop(float dx, float dy, float shortEdge);

// ---- the aim point ---------------------------------------------------------------------------

// The cursor for a finger at (fx, fy) with the lift offset `lift` (engine px): `lift` above the finger,
// never above the screen's top row.
void AimPoint(float fx, float fy, float lift, float& ax, float& ay);

// ---- edge auto-pan ---------------------------------------------------------------------------

constexpr float kEdgeBandFraction = 0.06f;   // of the short edge, from each screen edge

// The pan direction (-1, 0, +1 per axis, + right / down) for a finger at (x, y) on a w x h screen: an axis
// is set while the finger is within the band of that edge.
void EdgePan(float x, float y, int32_t w, int32_t h, int32_t& dx, int32_t& dy);

// ---- two fingers -----------------------------------------------------------------------------

// The thresholds past which each component of a two-finger gesture engages (then it follows the fingers
// exactly): the centroid's travel (a fraction of the short edge), the spread's log ratio, the angle.
constexpr float kPanEngageFraction = 0.02f;
constexpr float kPinchEngageLog = 0.06f;
constexpr float kSpinEngageDegrees = 8.0f;
// One retail zoom step (camera_input.cpp kZoomStep) per this log ratio of the spread.
constexpr float kPinchLogPerStep = 0.12f;

struct GestureStep {
    float dragX = 0.0f;       // the centroid's motion this step, fraction of the short edge, + right
    float dragY = 0.0f;       // + down
    float zoomSteps = 0.0f;   // + in (the fingers spread)
    float yawDegrees = 0.0f;  // the pair's turn this step, + clockwise on the screen
};

class TwoFingers {
public:
    void Begin(float x1, float y1, float x2, float y2, float shortEdge);
    GestureStep Step(float x1, float y1, float x2, float y2);
    bool panEngaged() const { return _pan; }
    bool pinchEngaged() const { return _pinch; }
    bool spinEngaged() const { return _spin; }

private:
    float _shortEdge = 1.0f;
    float _startCx = 0.0f, _startCy = 0.0f, _startDist = 1.0f, _startAngle = 0.0f;
    float _cx = 0.0f, _cy = 0.0f, _dist = 1.0f, _angle = 0.0f;
    bool _pan = false, _pinch = false, _spin = false;
};

// The difference a - b of two angles in degrees, in (-180, 180].
float AngleDelta(float a, float b);

// ---- the pick snap ---------------------------------------------------------------------------

struct Sample {
    float x = 0.0f;
    float y = 0.0f;
    float distance = 0.0f;   // from the tap
};

// The tap itself, then rings at r/3, 2r/3 and r with 8, 12 and 16 samples, nearest first. r <= 0: the tap.
std::vector<Sample> RingSamples(float x, float y, float radius);

// One ray's answer: the entity it picked (0 = nothing) at a sample.
struct Hit {
    uintptr_t entity = 0;
    Sample sample;
};

// The distinct entities hit, each at its nearest sample, nearest first (a stable order on ties: the
// sample order). Hits with entity 0 are dropped.
std::vector<Hit> Candidates(const std::vector<Hit>& hits);

// Tap-to-cycle: the candidate to choose this tap. `previous` is the entity the last tap chose when this
// tap continues it (within the radius and the time), else 0: the one after it in this tap's order
// (wrapping), or the first when it is not among them. -1 with no candidate.
int32_t ChooseCandidate(const std::vector<Hit>& candidates, uintptr_t previous);

constexpr float kCycleSeconds = 1.5f;

// ---- the action bar --------------------------------------------------------------------------

enum Button : int32_t {
    kPlace = 0,         // Place / Confirm: a click at the parked cursor
    kRotateLeft = 1,    // action 0x13
    kRotateRight = 2,   // action 0x14
    kFlip = 3,          // action 0x15
    kCancel = 4,        // action 6
    kButtonCount = 5
};

struct Rect {
    int32_t x1 = 0, y1 = 0, x2 = 0, y2 = 0;
    bool Contains(int32_t x, int32_t y) const { return x >= x1 && x < x2 && y >= y1 && y < y2; }
    bool Empty() const { return x2 <= x1 || y2 <= y1; }
};

struct Bar {
    Rect button[kButtonCount];   // empty when not shown
    Rect box;                    // around the shown buttons (the dialog's root)
};

// The bar on a width x height layout screen: one row of `cell`-sized buttons `gap` apart, in Button order
// (the hidden ones left out), inside a frame of `frame`, its right edge `margin` in from the right, its
// bottom `gap` above `aboveY` (the top of the toolbar or the sub-toolbar, whichever is higher; 0 = none:
// then `margin` above the bottom). `shown` is a bit per Button.
Bar LayoutBar(int32_t width, int32_t height, int32_t cell, int32_t gap, int32_t frame, int32_t margin,
              int32_t aboveY, uint32_t shown);

// The button under (x, y), or -1.
int32_t HitButton(const Bar& bar, int32_t x, int32_t y);

// ---- the Enhanced tab's "Touch pointer" presets (P11b) ---------------------------------------

// The three keys set together (ext.touch_pointer_lift / _pick_radius / _tap_twice; each a value the
// registry's key accepts). The combo (EnhancedOptions.rsc:touchPointerItems): item 0 = the row off,
// item 1 + i = kTouchPresets[i], the last item = Custom (settings.cfg values no preset matches).
struct TouchPreset {
    int32_t lift;         // device px
    int32_t pickRadius;   // device px
    int32_t tapTwice;     // 0 / 1
};

constexpr TouchPreset kTouchPresets[] = {
    {160, 50, 0},   // Default: the keys' defaults (lift 160 and radius 50 RULED kept, 2026-10-26)
    {120, 25, 0},   // Close aim: the aim nearer the finger, a tighter pick snap
    {240, 75, 0},   // Far aim: the aim well clear of a large thumb, a wider pick snap
    {160, 50, 1},   // Tap twice: Default with a second tap near the first placing the building
};
constexpr int32_t kTouchPresetCount = static_cast<int32_t>(sizeof(kTouchPresets) / sizeof(kTouchPresets[0]));
constexpr int32_t kTouchItemOff = 0;
constexpr int32_t kTouchItemCustom = kTouchPresetCount + 1;

// The combo item for the row's state and the three keys: kTouchItemOff with the row off, else the
// matching preset's item, else kTouchItemCustom.
int32_t TouchPresetItem(bool rowOn, int32_t lift, int32_t pickRadius, int32_t tapTwice);

}  // namespace TouchPointer
}  // namespace Ext
