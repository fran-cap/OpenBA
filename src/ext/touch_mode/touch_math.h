// ============================================================================
// src/ext/touch_mode/touch_math.h -- ext.touch_mode's pure decisions (unit-tested)
// ============================================================================
// NOT RETAIL (docs/android/TOUCH_MODE.md). Everything here is plain arithmetic on the
// layout-space screen (UserInterface::_uiScale units, the space every dialog but a scaled one is
// laid out in): the control cluster's rectangles, the toolbar's own scale k, which control a
// point is on, the D-pad's pan vector, the zoom buttons' rate and the unit-space mappings the
// place hook needs. Sizes are fractions of the screen's short edge, so the cluster keeps its
// physical size at any UI scale and render scale.
// ============================================================================
#pragma once

#include <cstdint>

namespace Ext {
namespace TouchMode {

// The cluster's cell (a D-pad third, a button's side), the gap between its buttons and the frame
// around them as fractions of the short edge, the D-pad's dead-zone radius as a fraction of its side, the
// margin used without ext.phone_menu. The gap and the frame match the toolbar's look on the S25 at
// UI 40 (Toolbar.rsc: 4-unit cells, a 9-unit content pad, at k ~1.5): one frame around the group
// (the owner, 2026-10-26).
constexpr float kCellFraction = 0.11f;
constexpr float kGapFraction = 0.014f;
constexpr float kFrameFraction = 0.03f;
constexpr float kDeadZoneFraction = 0.10f;   // P11c: 0.15 for the 8-way pad, smaller for the analogue one
constexpr float kDesktopMarginFraction = 0.02f;

// The zoom buttons (P11c, the owner's ruling of 2026-10-26: smooth and continuous, zero delay, no steps): a held
// button zooms at a constant kZoomStepsPerSecond of retail's zoom steps (camera_input.cpp kZoomStep, 5 distance
// units) from the first frame to the release. 8 steps/s = 40 units/s: retail's 20 .. 130 range in 2.75 s.
constexpr float kZoomStepsPerSecond = 8.0f;

// Retail's keyboard rotate rate (camera_input.cpp kRotSpeed, 0x140172204), degrees per second.
constexpr float kRotateDegPerSecond = 90.0f;

struct Rect {
    int32_t x1, y1, x2, y2;
    int32_t Width() const { return x2 - x1; }
    int32_t Height() const { return y2 - y1; }
    bool Contains(int32_t x, int32_t y) const { return x >= x1 && x < x2 && y >= y1 && y < y2; }
};

// The controls, in the order the cluster dialog lists its buttons.
enum Control : int32_t {
    kNone = -1,
    kPad = 0,
    kRotateLeft = 1,
    kRotateRight = 2,
    kZoomIn = 3,
    kZoomOut = 4,
    kControlCount = 5
};

struct Cluster {
    int32_t cell = 0;
    int32_t gap = 0;
    int32_t frame = 0;    // the frame's inset around the controls
    int32_t margin = 0;
    Rect control[kControlCount] = {};
    Rect box = {};        // the frame around all the controls (the dialog's root)
    Rect barRegion = {};  // where the toolbar may sit: right of the cluster, to the safe right edge
};

// The margin kept from every screen edge: ext.phone_menu's safe margin when that row is on
// (`phoneMargin`), else kDesktopMarginFraction of the short edge.
int32_t Margin(int32_t width, int32_t height, int32_t phoneMargin);

// The cluster on a width x height layout screen, `margin` in from the edges (TOUCH_MODE.md §3):
// one frame (`frame` inset) around the D-pad bottom-left (3 cells square), the two rotate buttons
// above it and the two zoom buttons right of it; the bar region right of the frame.
Cluster LayoutCluster(int32_t width, int32_t height, int32_t margin);

// The toolbar's own scale: `requested` (1.0 .. 2.0), shrunk so a bar `ownWidth` wide in its own
// units fits `regionWidth` layout units, never below 1. A width <= 0 (not laid out yet): requested.
float ToolbarScale(float requested, int32_t regionWidth, int32_t ownWidth);

// The control under (x, y), or kNone.
Control HitControl(const Cluster& c, int32_t x, int32_t y);

// The D-pad (P11c, the owner's ruling of 2026-10-26: analogue, a circle of directions, the rectangular graphic
// unchanged): the pan at (x, y) from the pad's centre. (x, y) of the result is the direction (a unit vector,
// right / down positive) times the speed 0..1, (0, 0) inside the dead zone (kDeadZoneFraction of the side, as a
// radius). kPadFull: full speed outside the dead zone. kPadProportional: the speed grows from 0 at the dead
// zone's edge to 1 at the inscribed circle (half the side), full beyond. A point outside the pad still gives
// its direction (a finger that started on the pad and slid off keeps panning that way: the capture rule).
enum PadSpeed : int32_t { kPadFull = 0, kPadProportional = 1 };
struct PadPan {
    float x = 0.0f;
    float y = 0.0f;
};
PadPan PadVector(const Rect& pad, int32_t x, int32_t y, PadSpeed mode);

// The capture rule (owner, 2026-10-26): a press that STARTS on a control captures the pointer until it
// is released; while down, that control and only that control acts, wherever the pointer moves (no
// hand-off by sliding onto a neighbour, the toolbar or the world). A press that starts anywhere else
// captures nothing, and sliding onto the cluster then does nothing. Step(cluster, down, x, y) once per
// frame: the control acting this frame, or kNone.
struct Capture {
    Control control = kNone;
    bool wasDown = false;
    Control Step(const Cluster& c, bool down, int32_t x, int32_t y);
    // The owner's ruling (2026-10-26): the captured control shows the toolbar buttons' pressed art for as long as
    // the press is held, even after the finger slid off it; released on lift.
    bool ShowsPressed(Control c) const { return c != kNone && wasDown && control == c; }
};

// A held zoom button's zoom this frame in retail's steps (zoomDir's sign: + in), `dir` +1 in / -1 out / 0 up:
// dir * kZoomStepsPerSecond * dt, 0 for a negative dt.
float HeldZoomSteps(int32_t dir, float dt);

// The held zoom applied as retail's own zoom moves (PlayerToolManager::UpdateCamera): the zoom target goes
// `steps` * `stepDistance` closer (steps > 0) or farther, clamped to [zMin, zMax] (retail's range or
// ext.better_camera's, as the caller's frame uses), and the smoothed distance moves by the same applied amount so
// retail's 0.9 / 0.1 smoothing adds no lag at the press or the release (the zoom starts and stops with the finger).
void ApplyHeldZoom(float steps, float stepDistance, float zMin, float zMax, float& target, float& distance);

// P11b (docs/android/P11B_POINTER.md §4; the owner's ruling of 2026-10-26: the D-pad pan SMOOTH like the
// two-finger pan): the pad's pan ramps its speed (the rotate buttons did too until P11c: the owner's ruling of
// 2026-10-26 took the rotate ramp out, full speed on the press). Step(held, dt) returns the gain
// 0..1 this frame: up from 0 over kRampInSeconds while held, down to 0 over kRampOutSeconds after the
// release (the camera coasts to a stop in the last direction), eased with a smoothstep. Zoom has no
// ramp (HeldZoomSteps).
// P11c (the owner, 2026-10-26: 0.30 / 0.20 s too laggy, keep the ramp at about 100 ms): 0.10 s in, 0.09 s out.
constexpr float kRampInSeconds = 0.10f;
constexpr float kRampOutSeconds = 0.09f;

struct SpeedRamp {
    float level = 0.0f;   // linear 0..1
    float Step(bool held, float dt);
};

// Unit-space mappings (TOUCH_MODE.md §2). A dialog with its own scale k is laid out in units of
// (layout units / k).
int32_t ToOwn(int32_t layout, float k);
int32_t ToLayout(int32_t own, float k);

// The right edge (layout units) a sub-toolbar is laid out against so that retail's own xPos
// (ToolbarDialog::OnElementEvent: layoutWidth * ... - buttonCentre - subWidth / 2, computed with the
// GLOBAL screen width) centres the child on its button inside the scaled toolbar: in own units the
// edge must be the global width, so in layout units it is width * k.
int32_t SubToolbarRight(int32_t layoutWidth, float k);

// A tooltip opened from a dialog with scale k: retail placed it at (ulX, ulY) = (x, y - h) with the
// mouse in the dialog's own units. The layout offset (dx, dy) that ComputeLayout adds to the
// upper-left so it lands at the global point (x * k, y * k - h).
void TooltipOffset(int32_t ulX, int32_t ulY, int32_t h, float k, int32_t& dx, int32_t& dy);

// P11 (ext.touch_pointer): a two-finger drag of (dragX, dragY) short edges (+ right / down) as the camera
// target's world motion, so the ground under the fingers follows them: the camera moves the other way,
// along retail's ground basis at `yawDeg` (camera_input.cpp: right = (cos, sin), the Back key's axis
// (sin, -cos)), kFingerDragWorldPerEdge world units per short edge per unit of camera distance.
constexpr float kFingerDragWorldPerEdge = 1.1f;
void FingerDragToWorld(float dragX, float dragY, float yawDeg, float distance, float& dx, float& dy);

// The toolbar scale value key (percent) as k.
float ScaleOfPercent(int32_t percent);

}  // namespace TouchMode
}  // namespace Ext
