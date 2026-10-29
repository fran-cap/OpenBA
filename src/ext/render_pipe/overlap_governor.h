// ============================================================================
// src/ext/render_pipe/overlap_governor.h -- P13_OVERLAP: take the snapshot only
// when the fence wait it removes would shorten the frame
// ============================================================================
// NOT RETAIL. ext.render_pipe (docs/android/P13_OVERLAP.md section 10).
//
// The snapshot buys the main thread's wait at THE FENCE with main-thread time (the
// copy). Measured (P13_OVERLAP section 9, NEXT_SESSION 2.15): it pays in the S10's
// stress town (wait 4.2 ms, copy 2.0 ms, the frame 19 ms against a 16.7 ms cap: 52 ->
// 60 fps); it costs where removing the wait cannot shorten the frame -- normal play
// at 1x on the S10 (the frame idles at the cap anyway: main thread 35 -> 56 % of a
// core, the soak hotter) and the desktop at speed 50 (wait 0.17 ms, copy 0.18 ms plus
// the park at the gate: -8 % fps).
//
// Fed once a Frame tail (main thread, pipe_driver.h) with
//   removable  the fence wait the snapshot takes off the main thread: without one,
//              the wait THE FENCE had; with one, the emit's run beside the steps (from
//              the claim to its gate, the whole steps when it had not reached it);
//              in both, less the part the emit spent before its BeginCommands returned
//              (a free swap-chain image, a frame in flight: the display's or the GPU's
//              pace, which a snapshot only moves); 0 for a Frame that did not step;
//   idle       the main thread's other waits that Frame: the frame cap's sleep, the
//              hand-off's wait for the render thread (it is the slower one, or its
//              Present waits for the display), and the fence's waits for an emit short
//              of BeginCommands (normal play at 1x: ~7 of the S10's 11 ms fence wait);
// and with the copy's time of every snapshot built.
//
// Off at install: today's fence, always correct. Switched on when the frame does not
// idle (a wait removed from a frame that idles only moves the idle time) and the
// removable wait is above kOnMinUs and above the copy. Switched off when the frame
// would idle more than kIdleOffUs without the snapshot (its idle now, plus the copy
// given back, less the wait it removes), or the removable wait falls under kOffMinUs
// or half the copy. Between the two the state holds, and any switch holds for
// kHoldFrames, so a noisy frame cannot flap it. Engine-free (the unit test drives it).
// ============================================================================
#pragma once

#include <cstdint>

namespace Ext {
namespace RenderPipe {

class OverlapGovernor {
public:
    // A removable wait under 1 ms a frame is below every copy measured on a phone (2.0-2.1 ms
    // on the S10); 0.5 ms off leaves a band so the estimate's noise never flips it.
    static constexpr double  kOnMinUs    = 1000.0;
    static constexpr double  kOffMinUs   = 500.0;
    // On only while the frame idles under 0.5 ms (it is not held back by the cap, the display
    // or the render thread); off once it would idle 4 ms without the copies -- a quarter of a
    // 60 Hz frame, so a frame near the cap keeps the copies: the S10's stress town idles 0-2.5
    // ms with them from window to window, which the estimate puts at -1.2..+0.8 ms without.
    static constexpr double  kIdleOnUs   = 500.0;
    static constexpr double  kIdleOffUs  = 4000.0;
    // Two seconds at 60 fps: a load hitch or one busy second does not switch it.
    static constexpr int32_t kHoldFrames = 120;
    // The smoothing weight of one Frame (about the last 16 Frames).
    static constexpr double  kWeight     = 1.0 / 16.0;

    // One Frame's measures (us). True: the state switched.
    bool Frame(int64_t removableUs, int64_t idleUs) {
        _removableUs += (static_cast<double>(removableUs) - _removableUs) * kWeight;
        _idleUs      += (static_cast<double>(idleUs) - _idleUs) * kWeight;
        if (_held < kHoldFrames) {
            ++_held;
            return false;
        }
        bool want;
        if (!_on) {
            want = _idleUs < kIdleOnUs && _removableUs > kOnMinUs && _removableUs > _copyUs;
        } else {
            const double idleWithout = IdleWithoutUs();
            want = !(idleWithout > kIdleOffUs || _removableUs < kOffMinUs || _removableUs < _copyUs * 0.5);
        }
        if (want == _on) return false;
        _on   = want;
        _held = 0;
        return true;
    }

    // The main-thread time of one snapshot built.
    void Copy(int64_t us) {
        _copyUs = _copies == 0 ? static_cast<double>(us)
                               : _copyUs + (static_cast<double>(us) - _copyUs) * kWeight;
        ++_copies;
    }

    bool    On() const { return _on; }
    int64_t RemovableUs() const { return static_cast<int64_t>(_removableUs); }
    int64_t IdleUs() const { return static_cast<int64_t>(_idleUs); }
    int64_t CopyUs() const { return static_cast<int64_t>(_copyUs); }
    // On: what the frame would idle without the snapshot. Off: what it idles.
    int64_t IdleWithout() const { return static_cast<int64_t>(_on ? IdleWithoutUs() : _idleUs); }

private:
    double IdleWithoutUs() const { return _idleUs + _copyUs - _removableUs; }

    bool     _on          = false;
    int32_t  _held        = 0;     // Frames since install or the last switch
    double   _removableUs = 0.0;   // smoothed removable fence wait per Frame
    double   _idleUs      = 0.0;   // smoothed other waits per Frame
    double   _copyUs      = 0.0;   // smoothed copy time per snapshot (0: none built yet)
    uint64_t _copies      = 0;
};

}  // namespace RenderPipe
}  // namespace Ext
