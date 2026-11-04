#pragma once
// ============================================================================
// src/ext/fps_counter/fps_stats.h -- ext.fps_counter's sums (NOT RETAIL)
// ============================================================================
// docs/extensions/handins/FPSCOUNTER.md. Standard C++ (tests/unit/ext_fps_counter_test.cpp). One call per
// drawn game frame with the clock's tick count; frames are counted in windows of one second, and each window
// that closes gives one frame rate. The current rate is the last window's, the minimum and maximum are over the
// windows, the average is every counted frame over every counted second (so it is not an average of rates).
//
// A gap between two frames longer than kMaxGapSeconds (a load, the window moved off, a breakpoint) drops the
// window that was open and starts a new one at the second frame: the gap is not a slow frame of the game.
// ============================================================================

#include <cstdint>

namespace Ext {
namespace FpsCounter {

constexpr int64_t kMaxGapSeconds = 2;

struct Stats {
    int64_t lastTick = 0;       // the previous frame's tick
    bool havePrev = false;      // false until the first frame primes lastTick
    int64_t windowStart = 0;    // the open window's first tick
    int32_t windowFrames = 0;   // frames ended inside the open window
    int64_t countedTicks = 0;   // the closed windows' length, summed
    int64_t countedFrames = 0;  // the closed windows' frames, summed
    int32_t windows = 0;        // closed windows: 0 = nothing measured yet
    double current = 0.0;       // the last closed window's rate
    double minimum = 0.0;
    double maximum = 0.0;

    void Reset() {
        *this = Stats();
    }

    // One drawn frame at `now`, a clock that runs at `frequency` ticks a second.
    void Frame(int64_t now, int64_t frequency) {
        if (frequency <= 0) return;
        if (!havePrev) {
            havePrev = true;
            lastTick = now;
            windowStart = now;
            windowFrames = 0;
            return;
        }
        const int64_t gap = now - lastTick;
        lastTick = now;
        if (gap < 0 || gap > frequency * kMaxGapSeconds) {
            windowStart = now;
            windowFrames = 0;
            return;
        }
        ++windowFrames;
        const int64_t elapsed = now - windowStart;
        if (elapsed < frequency) return;
        const double rate = static_cast<double>(windowFrames) * static_cast<double>(frequency) /
                            static_cast<double>(elapsed);
        current = rate;
        if (windows == 0 || rate < minimum) minimum = rate;
        if (windows == 0 || rate > maximum) maximum = rate;
        ++windows;
        countedTicks += elapsed;
        countedFrames += windowFrames;
        windowStart = now;
        windowFrames = 0;
    }

    double Average(int64_t frequency) const {
        if (countedTicks <= 0) return 0.0;
        return static_cast<double>(countedFrames) * static_cast<double>(frequency) /
               static_cast<double>(countedTicks);
    }

    // Whole seconds the closed windows cover.
    int64_t Seconds(int64_t frequency) const {
        return frequency > 0 ? countedTicks / frequency : 0;
    }
};

}  // namespace FpsCounter
}  // namespace Ext
