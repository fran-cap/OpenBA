// ============================================================================
// tests/unit/ext_fps_counter_test.cpp -- ext.fps_counter's sums (src/ext/fps_counter/fps_stats.h)
// ============================================================================
// The figures the FPS page shows: one-second windows give the current, minimum and maximum rate, the average
// is every counted frame over every counted second, and a long gap between frames (a load, a breakpoint) is
// dropped rather than counted as a slow frame.
// ============================================================================
#include "unit.h"
#include "ext/fps_counter/fps_stats.h"

#include <cmath>
#include <cstdint>

namespace {

using Ext::FpsCounter::Stats;

constexpr int64_t kHz = 600000;   // a clock every rate below divides evenly

bool Near(double a, double b) {
    return std::fabs(a - b) < 1e-6;
}

// `frames` frames at `rate` a second from `start`; returns the tick after the last.
int64_t Run(Stats& s, int64_t start, int frames, int64_t rate) {
    int64_t t = start;
    for (int i = 0; i < frames; ++i) {
        t += kHz / rate;
        s.Frame(t, kHz);
    }
    return t;
}

}  // namespace

UNIT_TEST(ext_fps_counter_nothing_measured_before_a_second) {
    Stats s;
    s.Frame(0, kHz);   // primes
    Run(s, 0, 59, 60);
    CHECK_EQ(s.windows, 0);
    CHECK(Near(s.Average(kHz), 0.0));
    CHECK_EQ(s.Seconds(kHz), 0);
}

UNIT_TEST(ext_fps_counter_a_steady_rate_is_every_figure) {
    Stats s;
    s.Frame(0, kHz);
    Run(s, 0, 600, 60);   // ten seconds at 60
    CHECK_EQ(s.windows, 10);
    CHECK(Near(s.current, 60.0));
    CHECK(Near(s.minimum, 60.0));
    CHECK(Near(s.maximum, 60.0));
    CHECK(Near(s.Average(kHz), 60.0));
    CHECK_EQ(s.Seconds(kHz), 10);
}

UNIT_TEST(ext_fps_counter_min_max_over_windows_average_over_frames) {
    Stats s;
    s.Frame(0, kHz);
    int64_t t = Run(s, 0, 100, 100);   // one second at 100
    t = Run(s, t, 50, 50);             // one at 50
    t = Run(s, t, 200, 200);           // one at 200
    (void)t;
    CHECK_EQ(s.windows, 3);
    CHECK(Near(s.minimum, 50.0));
    CHECK(Near(s.maximum, 200.0));
    CHECK(Near(s.current, 200.0));
    // 350 frames over three seconds, not the mean of the three rates (116.7)
    CHECK(Near(s.Average(kHz), 350.0 / 3.0));
}

UNIT_TEST(ext_fps_counter_a_long_gap_is_dropped_not_counted) {
    Stats s;
    s.Frame(0, kHz);
    int64_t t = Run(s, 0, 60, 60);   // one second at 60
    t = Run(s, t, 30, 60);           // half a window open
    t += 5 * kHz;                    // a load: five seconds without a frame
    s.Frame(t, kHz);                 // drops the open window, starts a new one here
    CHECK_EQ(s.windows, 1);
    t = Run(s, t, 60, 60);
    CHECK_EQ(s.windows, 2);
    CHECK(Near(s.minimum, 60.0));   // the gap did not make a 1-fps window
    CHECK(Near(s.Average(kHz), 60.0));
    CHECK_EQ(s.Seconds(kHz), 2);
}

UNIT_TEST(ext_fps_counter_reset_starts_over) {
    Stats s;
    s.Frame(0, kHz);
    int64_t t = Run(s, 0, 300, 30);
    CHECK_EQ(s.windows, 10);
    s.Reset();
    CHECK_EQ(s.windows, 0);
    CHECK(!s.havePrev);
    s.Frame(t, kHz);   // primes again
    Run(s, t, 120, 120);
    CHECK_EQ(s.windows, 1);
    CHECK(Near(s.minimum, 120.0));
    CHECK(Near(s.Average(kHz), 120.0));
}

UNIT_TEST(ext_fps_counter_a_clock_going_back_is_a_gap) {
    Stats s;
    s.Frame(1000, kHz);
    s.Frame(500, kHz);   // never counted
    CHECK_EQ(s.windowFrames, 0);
    s.Frame(0, 0);       // no clock: ignored
    CHECK_EQ(s.lastTick, 500);
}
