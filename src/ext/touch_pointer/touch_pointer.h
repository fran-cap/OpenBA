#pragma once
// ============================================================================
// src/ext/touch_pointer/touch_pointer.h -- ext.touch_pointer (class A, live, default OFF)
// ============================================================================
// NOT RETAIL (docs/android/P11_TOUCH_POINTER.md; rulings docs/android/TOUCH_PLACEMENT_IDEAS.md §5,
// 2026-10-26). The fingers drive the game's own tools through the input device, which keeps seeing a
// mouse:
//   * Platform::Touch::SetSink takes the finger events from the pump (SDL's first-finger mouse off);
//   * Input::Device::_extAfterUpdate writes the pointer the fingers make, one latched state per poll,
//     and one-step edges on the inputs the KeyMap binds to rotate / flip / cancel;
//   * one finger aims while a placement or drag tool is active (the cursor above the finger by the
//     lift offset, the button up: retail's hover shows the ghost), a line / rectangle tool runs retail's
//     single-click mode, a tap with no tool picks the nearest pickable (snap and tap-to-cycle);
//   * two fingers pan, pinch-zoom and spin through ext.touch_mode's camera hold (Source::Fingers);
//   * an action bar (Dialog/TouchPointer.rsc) on the right above the toolbar: Place / Confirm, Rotate
//     left / right, Change model, Cancel; a marker at the aim point.
// A real mouse event hands the pointer back at once (last device wins); the bar hides after 3 s of it.
// Default OFF on a desktop; the Android first run switches it on (src/platform/android/app_android.cpp).
// Under the faithful profile no seam is installed.

#include <cstdint>

namespace UI {
class DialogInstance;
}

namespace Ext {
namespace TouchPointer {

// Boot only (registry_boot.cpp): installs Input::Device::_extAfterUpdate (null under the faithful profile).
void Sync();

// ext.touch_mode's place function, first: true when this row placed the dialog (the bar, the marker).
bool Place(int32_t point, const UI::DialogInstance* dialog, int32_t width, int32_t height, int32_t* rect);

// PlayerToolManager::UpdateCamera, every game frame (EXT SEAM): opens / closes / shapes the bar and the
// marker for the current tool. Called while the row is on or Shown().
void GameFrame();
bool Shown();

}  // namespace TouchPointer
}  // namespace Ext
