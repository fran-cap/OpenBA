// ============================================================================
// src/ext/render_pipe/display_clock.h -- SMOOTHREPLAY: the alpha a frame is drawn
// at, read from the wall clock when it is drawn
// ============================================================================
// NOT RETAIL. ext.render_pipe, its row render_pipe_replay
// (docs/extensions/handins/SMOOTHREPLAY.md section 2).
//
// MainLoop::Frame samples the accumulator at the Frame's start, so a packet's alpha
// puts the displayed pose at `frameStart - step` of wall time, whenever the packet is
// drawn. A replayed frame draws the same packet again later: with that alpha it would
// show the same picture. The display clock recomputes the alpha when the frame is
// drawn, a constant `delay` behind the sim:
//
//     alpha(now) = packetAlpha + (now - frameStart - delay) / step, clamped to [0, 1]
//
// The displayed pose is then the wall clock `now - step - delay` for the packet's own
// pass 2 and for each replay alike, so a replay during the next steps shows a pose the
// packet's pair already holds (alpha below 1 while now < frameStart + delay +
// (1 - packetAlpha) * step). One value drives everything time-driven in pass 2:
// Renderer::_alphaT and SetTime's shader time.
//
// Engine-free: ticks are the caller's (System::Timer's in the engine).
// ============================================================================
#pragma once

#include <cstdint>

namespace Ext {
namespace RenderPipe {

// How far the displayed pose runs behind retail's, microseconds. 8 ms: at the stress town's
// 50x, 99% of replayed frames show new motion and no packet frame stalls at alpha 0 (12 ms
// stalled 22% of them); the mean picture is 2.7 ms older.
constexpr int64_t kDisplayDelayUs = 8000;

// The alpha at `nowTicks` for a packet gathered with `packetAlpha` at `frameStartTicks`.
// `stepTicks` is one fixed step, `delayTicks` the delay; all in the same tick unit. A step of 0
// or less (no clock) answers the packet's alpha.
inline float DisplayAlpha(float packetAlpha, int64_t frameStartTicks, int64_t stepTicks, int64_t delayTicks,
                          int64_t nowTicks) {
    if (stepTicks <= 0) return packetAlpha;
    const double a = static_cast<double>(packetAlpha) +
                     static_cast<double>(nowTicks - frameStartTicks - delayTicks) / static_cast<double>(stepTicks);
    if (a < 0.0) return 0.0f;
    if (a > 1.0) return 1.0f;
    return static_cast<float>(a);
}

}  // namespace RenderPipe
}  // namespace Ext
