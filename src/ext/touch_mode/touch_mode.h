#pragma once
// ============================================================================
// src/ext/touch_mode/touch_mode.h -- ext.touch_mode (class A, live, default OFF)
// ============================================================================
// NOT RETAIL (docs/android/TOUCH_MODE.md; rulings 2026-10-26). A phone layout for fingers:
//   (a) a control cluster at the bottom left of the game view (Dialog/TouchControls.rsc): a D-pad
//       (camera pan, any direction: P11c), Rotate left / right above it, Zoom in / out right of it, sized as
//       fractions of the screen's short edge (touch_math.h LayoutCluster). A held control drives
//       the same camera locals retail's keys drive in PlayerToolManager::UpdateCamera;
//   (b) the in-game toolbar (Dialog/Toolbar.rsc) and its sub-toolbars in their own unit space, k
//       times the UI scale (ext.touch_mode_toolbar_scale, shrunk to fit beside the cluster): laid
//       out, hit-tested and drawn at k through UI::UserInterface::_extDialogScale (installed by
//       ext.phone_menu, which asks Scale first); the rest of the
//       UI keeps the UI scale. The toolbar's tooltips, sub-toolbars and event notices are put back
//       where they belong in the global units (Place);
//   (c) the right-hand strip above the bar stays free for the later touch_pointer action bar;
//       the camera hold (Hold) is callable from it (last device wins).
// Default OFF on a desktop; the Android first run switches it on (src/platform/android/
// app_android.cpp). Live: every hook reads the row each time. Under the faithful profile the
// engine seam stays null.

#include <cstdint>

namespace UI {
class DialogInstance;
}

namespace Ext {
namespace TouchMode {

// The toolbar's own scale k for `dialog` (the toolbar, a sub-toolbar), 1.0 for any other dialog or
// with the row off. ext.phone_menu's UI::UserInterface::_extDialogScale function asks this first
// (phone_menu.h DialogScale; the seam is null under the faithful profile).
float Scale(const UI::DialogInstance* dialog);

// ext.phone_menu's place function (UI::UserInterface::_extDialogPlace), first: true when this row
// placed the dialog (the toolbar, a sub-toolbar, a toolbar tooltip, the event notices, the
// cluster), so phone_menu's own rule is skipped for it. False with the row off.
bool Place(int32_t point, const UI::DialogInstance* dialog, int32_t width, int32_t height, int32_t* rect);

// The cluster's cell (layout units of the game's UI), 0 before the first layout: ext.touch_pointer's
// action bar uses the same button size.
int32_t Cell();

// True while the cluster dialog is open (the camera seam calls CameraInput while the row is on
// OR this, so a row switched off closes it).
bool Shown();

// PlayerToolManager::UpdateCamera, after the keys set the locals and before ext.eye_camera's zoom:
// the held control (or the touch_pointer hold) assigns the pan axes as the keys do, turns the yaw
// at retail's key rate and adds zoom steps. Opens / closes the cluster with the row and the toolbar.
// P11b: the pad's pan ramps in and out (P11c: the rotate buttons do not) (touch_math.h SpeedRamp); `panGain` (the caller's 1.0)
// is set to the pan's ramp while the pad drives the pan axes, and multiplies retail's pan speed.
void CameraInput(float dt, float& yaw, float& panX, float& panY, int& zoomDir, float& panGain);

// The camera hold of a source: the cluster's pad (Pad) or the later touch_pointer extension's
// fingers (Fingers). `rotate` -1 left / +1 right, `zoom` +1 in / -1 out (one step per call while it
// changes), `zoomSteps` continuous steps (a pinch). The source that changed last wins; keys and
// the real mouse are never overridden (they keep working through retail's lines).
enum class Source : int32_t { Pad = 0, Fingers = 1 };
struct CameraHold {
    // The pan: a direction times a speed (the Pad: touch_math.h PadVector, length 0..1; the Fingers: an edge
    // pan's -1 / 0 / +1 axes).
    float panX = 0.0f;
    float panY = 0.0f;
    int8_t rotate = 0;
    int8_t zoom = 0;
    float zoomSteps = 0.0f;
    // P11 (ext.touch_pointer, docs/android/P11_TOUCH_POINTER.md), the Fingers source only: a two-finger
    // grab-the-map drag this frame (the fingers' motion in fractions of the screen's short edge, + right /
    // down; the ground under them follows) and a direct turn of the yaw in degrees.
    float dragX = 0.0f;
    float dragY = 0.0f;
    float yawDegrees = 0.0f;
};
void Hold(Source source, const CameraHold& hold);

// The toolbar's and the open sub-toolbar's rectangles in layout units ({0,0,0,0} when not shown),
// for the touch_pointer action bar (TOUCH_MODE.md section 4 "Reserved strip").
void BarRect(int32_t* rect);
void SubToolbarRect(int32_t* rect);

}  // namespace TouchMode
}  // namespace Ext
