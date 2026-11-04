#pragma once
// ============================================================================
// src/ext/fps_counter/fps_counter.h -- ext.fps_counter (class A, live, default OFF)
// ============================================================================
// NOT RETAIL (docs/extensions/handins/FPSCOUNTER.md). A frame-rate counter: while the row is on, every drawn
// game frame is counted (FrameLimiter::g_extGameFrame, called at the top of FrameLimiter::TickInGame, the one
// chokepoint both GameUpdateLoop::Draw and ext.render_pipe pass once a frame) and the FPS page of the extension
// window (the toolbar's extension button in the Reports group) shows the current, average, minimum and maximum
// frame rate and how long it has measured, with a Reset button. Class A: nothing is saved, no sim effect, live.
// OFF by default: it adds the extension button to the toolbar, which is the player's call. Under the faithful
// profile the row is off and the seam stays null.

#include "ext/fps_counter/fps_stats.h"

#include <cstdint>

namespace Ext {
namespace FpsCounter {

// The seam follows the row: set while it is on, null while it is off (the stats are reset when it comes on).
// Boot (registry_boot.cpp) and every toggle (SetAndRecord). Main thread.
void Sync();

// The sums so far, and the clock's frequency they are in. Main thread (the frames are counted there too).
const Stats& Current();
int64_t Frequency();

// Starts the sums over (the page's Reset button).
void Reset();

}  // namespace FpsCounter
}  // namespace Ext
