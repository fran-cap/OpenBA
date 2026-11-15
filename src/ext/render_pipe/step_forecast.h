// ============================================================================
// src/ext/render_pipe/step_forecast.h -- HITCH_TAIL: will the next Frame step?
// ============================================================================
// NOT RETAIL. ext.render_pipe (overlap_governor.h, pipe_driver.h).
//
// A packet built at a Frame tail is emitted during the next Frame. Only when that
// Frame steps does THE FENCE wait for it, so only then does its snapshot (P13_OVERLAP)
// remove a wait; on a Frame that does not step the copy is spent for nothing. At
// speed 50 on the desktop one Frame in three steps (60 Hz steps, ~180 fps), so a
// snapshot on every packet paid three copies for one fence.
//
// MainLoop::Frame steps when its accumulator reaches one step: the accumulator now
// (after this Frame's steps) plus the ticks from this Frame's sample to the next
// Frame's sample (MainLoop::_startTicks, re-sampled at the top of every Frame). The
// part already elapsed is known at the tail; the rest -- the tail itself, the yield,
// the next Frame's sample -- is the smoothed gap from a tail's entry to the next
// Frame's sample, measured here. The forecast leans to "steps" (half the gap again
// plus a margin): a wrong "steps" costs one copy, a wrong "does not step" the fence
// wait the snapshot exists to remove.
//
// Only WHEN the copies are taken changes: the snapshot packet and the plain one emit
// the same commands (the RenderPipe shadow, BANISHED_STRESS_COMPPROF=4), and the sim
// never reads either. Engine-free: tests/unit/ext_render_pipe_test.cpp drives it.
// ============================================================================
#pragma once

#include <cstdint>

namespace Ext {
namespace RenderPipe {

class StepForecast {
public:
    // The smoothing weight of one gap (about the last 8 Frames).
    static constexpr double kWeight = 1.0 / 8.0;
    // The gap estimate is stretched by this much: the tail's own time varies Frame to Frame.
    static constexpr double kGapStretch = 1.5;

    // One Frame tail. nowTicks = the clock at the tail's entry; startTicks / accumulator /
    // stepTicks = MainLoop::_startTicks / _accumulator / _deltaTime._ticks; marginTicks =
    // a fixed lean towards "steps". True: the next Frame is expected to step.
    bool AtTail(int64_t nowTicks, int64_t startTicks, int64_t accumulator, int64_t stepTicks,
                int64_t marginTicks) {
        if (_lastTail != 0 && startTicks > _lastTail) {
            const double gap = static_cast<double>(startTicks - _lastTail);
            _gap = _have ? _gap + (gap - _gap) * kWeight : gap;
            _have = true;
        }
        _lastTail = nowTicks;
        if (!_have) return true;   // no gap measured yet: lean to "steps"
        const double elapsed = static_cast<double>(nowTicks - startTicks);
        const double ahead = static_cast<double>(accumulator) + elapsed + _gap * kGapStretch +
                             static_cast<double>(marginTicks);
        return ahead >= static_cast<double>(stepTicks);
    }

    double GapTicks() const { return _gap; }

private:
    int64_t _lastTail = 0;     // the previous tail's entry
    double  _gap      = 0.0;   // smoothed: a tail's entry -> the next Frame's sample
    bool    _have     = false;
};

}  // namespace RenderPipe
}  // namespace Ext
