// src/app/frame_limiter.h
// Per-frame CPU frame limiter for the standalone exe.
//
// The engine main loop (Runtime MainLoop::Frame, which standalone runs via
// MainLoop::Run @ Runtime 0x1800dd3c0) is uncapped — its only per-frame yield
// is Application::YieldToThreads (a thunk to SwitchToThread, which does NOT
// sleep). With nothing else runnable on the core, the menu loop spins at
// thousands of FPS and pegs one CPU core to 100%. The only natural throttle
// is Present blocking on vsync, which the standalone never enables.
//
// FrameLimiter::Tick() is called once per rendered frame of the menu (the menu loop's Draw vtable
// slot), TickInGame() once per rendered frame of the game (GameUpdateLoop::Draw): the per-frame
// chokepoints we own. Both sleep at the start of the frame's Draw; MainLoop::Frame's fixed-step
// accumulator (mainloop_frame.cpp) still runs every simulation step the elapsed time owes, so a
// slower frame rate never changes the sim tick rate, only how many frames draw it.
//
// Which ceiling a loop gets is frame_limiter_ceiling.h (ResolveCeiling):
//   * the menu: an UPPER ceiling of (display refresh + headroom) FPS that sits ABOVE the refresh
//     rate on purpose: it exists only to kill the multi-thousand-FPS spin, never to be the real
//     limiter. Any external pacing the user has (G-Sync/FreeSync, vsync, RTSS, a driver cap) runs
//     at or below refresh and engages first; the limiter then measures elapsed >= interval and
//     sleeps zero, so external frame limiting is never fought;
//   * the game: nothing, unless a cap is asked for (uncapped, as retail);
//   * both: the settings cap (ext.fps_cap, the Options dialog's "Frame rate cap"; the phone's first
//     run turns it on at 60), set through SetSettingsCap;
//   * both, over everything: the harness flag.
//
// Tuning (env vars, read once at first Tick, through Opt()):
//   BANISHED_FPS_CAP=<n>        Absolute ceiling in FPS for the menu AND the game. 0 / "off" =
//                              disabled (fully uncapped). Overrides the auto ceiling and the
//                              settings cap.
//   BANISHED_FPS_HEADROOM=<p>   Percent above display refresh for the menu's auto ceiling.
//                              Default 50 (=> refresh * 1.5).

#pragma once

#include <cstdint>

namespace FrameLimiter {

// Pace the calling thread so successive Tick() calls are at least one frame
// interval apart. Cheap no-op when the frame already took longer than the
// ceiling interval (i.e. when something else is already limiting the rate).
// Call exactly once per rendered frame, at frame start (before draw work) for
// lowest added latency. First call only primes the timestamp.
void Tick();

// The game loop's twin of Tick (GameUpdateLoop::Draw): paces only to the settings cap or the
// harness flag; a no-op (no clock read beyond one test) when neither asks for a cap.
void TickInGame();

// The settings cap in FPS, 0 = none (src/ext/fps_cap/: at boot and when the Options dialog
// applies). Main thread. Takes effect at the next Tick / TickInGame.
void SetSettingsCap(double fps);

// Most recent measured frame rate (frames/sec), updated ~once per second.
// 0 until enough frames have elapsed. Intended for HUD/log readout.
double GetFps();

// The ceiling the last Tick / TickInGame paced to, in FPS (0 if that loop is not paced).
double GetCapFps();

// How long the last Tick / TickInGame waited for its deadline, us (0: not paced, or late).
// ext.render_pipe's overlap governor counts it as the frame's idle time.
int64_t LastWaitUs();

} // namespace FrameLimiter
