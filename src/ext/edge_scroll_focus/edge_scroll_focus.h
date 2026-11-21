// ============================================================================
// src/ext/edge_scroll_focus/edge_scroll_focus.h -- fix.edge_scroll_focus (fix tier, default ON)
// ============================================================================
// NOT RETAIL. docs/retail_bugs.md §37.
//
// The mouse poller (Input::Device::PlatformGetMouseState 0x1800e1e10) reports every mouse
// input as zero while the game window is not the foreground window: the DirectInput mouse is
// DISCL_FOREGROUND, its read fails, and the zeroed scratch record is published as the state.
// The cursor position is then (0, 0), and the camera's edge scroll
// (PlayerToolManager::UpdateCamera 0x1400feec0) reads x < 4 and y < 4 as "the pointer is at
// the left and top edges": the camera pans up and left for as long as the game runs
// unfocused. The application's pause (WM_KILLFOCUS) skips the camera update, so the pan lasts
// only for the frames where the window is not foreground and the application is not paused
// (around a focus change), or for as long as the two disagree.
//
// With the row on, the edge-scroll arm is skipped on a frame where the window is not the
// foreground window. The keys, the wheel and the rest of UpdateCamera are untouched (they read
// zero on such a frame anyway).
// ============================================================================
#pragma once

namespace Ext {
namespace EdgeScrollFocus {

// The pure rule: does the edge-scroll arm read the pointer on this frame? `settingOn` is the
// player's Edge Scroll option (retail's own test), `fixOn` the row, `foreground` whether the
// game window is the foreground window (the mouse poller's own test).
inline bool ReadsPointer(bool settingOn, bool fixOn, bool foreground) {
    if (!settingOn) {
        return false;
    }
    if (fixOn && !foreground) {
        return false;
    }
    return true;
}

// Is the game window the foreground window (Platform::Input::IsForeground on the
// application's window)? True when there is no application.
bool WindowIsForeground();

}  // namespace EdgeScrollFocus
}  // namespace Ext
