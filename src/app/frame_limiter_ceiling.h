// src/app/frame_limiter_ceiling.h -- which frame rate a loop is paced to (NOT RETAIL; frame_limiter.h)
//
// Pure logic, standard C++ only (tests/unit/frame_limiter_ceiling_test.cpp). Three sources, in order:
//   1. BANISHED_FPS_CAP (the harness flag, Opt().fpsCap): "off" / "false" / 0 = no pacing at all, in
//      the menu and in game; a number = that ceiling in both loops. It wins over everything below.
//   2. The settings cap (ext.fps_cap with ext.fps_cap_rate, src/ext/fps_cap/): a ceiling in both
//      loops. In the menu the automatic ceiling below still applies when it is lower.
//   3. The automatic ceiling (display refresh + BANISHED_FPS_HEADROOM, default refresh * 1.5): the
//      MENU only. It exists to stop the menu spinning a core at thousands of fps. The game loop has
//      none: with no flag and no settings cap the game is uncapped, as it always was.
#pragma once

namespace FrameLimiter {

enum class HarnessCap {
    None,    // BANISHED_FPS_CAP absent
    Off,     // "off", "false", or a number <= 0
    Fixed,   // a number > 0 (harnessFps)
};

// The lowest ceiling that is paced (review F-134). Below it, freq / fps and the 100 ns sleep arithmetic
// leave int64_t on a 1 GHz counter.
constexpr double kMinCeilingFps = 0.01;

// The ceiling one loop is paced to, in fps; 0 = the loop is not paced. `autoFps` is the menu's
// automatic ceiling (> 0), `settingsFps` the settings cap (0 = none), `menu` true for the menu loop.
inline double ResolveCeiling(HarnessCap harness, double harnessFps, double autoFps, double settingsFps,
                             bool menu) {
    if (harness == HarnessCap::Off) return 0.0;
    // Below kMinCeilingFps, or NaN, BANISHED_FPS_CAP paces nothing (review F-134, undefined behaviour:
    // the int64_t conversions in SetCeiling and the product in WaitUntil leave their range).
    if (harness == HarnessCap::Fixed) return harnessFps >= kMinCeilingFps ? harnessFps : 0.0;
    if (settingsFps > 0.0) {
        if (menu && autoFps > 0.0 && autoFps < settingsFps) return autoFps;
        return settingsFps;
    }
    return menu ? autoFps : 0.0;
}

}  // namespace FrameLimiter
