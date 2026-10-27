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
class Tool;   // src/game/Player/Tool.h

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

// PlayerToolManager::Update (EXT SEAM, P11b): true while a finger pressed on a dialog with `current` (a placement or
// drag tool) aiming, until the cursor is back on the world; the manager then skips the tool's hotkeys (no click of
// the dialog's press reaches the tool) and updates it as if no dialog held the mouse, its ray on the aim
// (ToolCursor), so its ghost stays where it was aimed (retail hides it while the cursor is over a dialog). False
// otherwise, always with the row off, and once another tool is current.
bool HoldToolAim(const Tool* current);

// The draw's side of the same hold (GameUpdateLoop::Draw, render_pipe's main pass; EXT SEAM): true while the hold
// above is on for `current`, so the tool's Draw gets retail's "no dialog under the cursor" flag and keeps drawing
// its ghost (Tool::Draw hides it while the UI holds the mouse). Reads only.
bool ToolAimHeld(const Tool* current);

// The tools' cursor ray (PlayerRay_GetMouseRay_1005a0, EXT SEAM): while that hold is on, (x, y) are replaced by the
// world cursor the tool last aimed at, so GetPlacement / the drag's end cell keep the aim while the real cursor is on
// the dialog for the click. Any thread (the draw); a no-op otherwise.
void ToolCursor(int16_t& x, int16_t& y);

}  // namespace TouchPointer
}  // namespace Ext
