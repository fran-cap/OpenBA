// src/diag/ach_clock.cpp
//
// THE ACHIEVEMENT CLOCK — non-retail test knobs for the time-gated achievements.
//
// Twelve of the 36 achievements are gated on game time rather than on anything the
// town does: Miner and Mason want 3 years, Educated / Blacksmith / Stylish 4, Jack
// of all Trades 5, Smiles all Around and Healthy 10, Mountain Men 20, Established
// 100 and Tenure 200. Measured at 50x speed a year costs about 4,500 frames, so
// Tenure alone is on the order of 900,000 frames — many hours per attempt, and the
// stress town starves long before that. Waiting them out is not a test, it is a
// vigil.
//
// There are exactly two mechanisms behind all twelve, and this file drives each
// directly:
//
//   TIME GOALS (TimeGoalInstance::Slot5, exe 0x1400dac30) accumulate the Time value
//   handed down from AchievementManager::Update -- `self->elapsedTime += *time` --
//   and reset it to 0 whenever the achievement's OTHER goals stop being met. So one
//   multiplier on that single Time value advances every TimeGoal in the game, and
//   because the reset rule still applies, an achievement whose conditions lapse
//   still loses its progress. That is the property worth preserving: the goal is
//   accelerated, not bypassed.
//
//   THE YEAR COUNTER, *(uint32_t*)(*(void**)(gameWorld + 0x128)), is what stat 26
//   reads (dispatcher case 0x1a, retail 0x1400da659). Established and Tenure have no
//   TimeGoal at all -- they are a population StatGoal plus `stat26 >= 100 / 200` --
//   so for those two the counter itself has to move.
//
// Neither knob touches the simulation: citizens do not age faster and no extra ticks
// run. That is the point (a 200-year real run is a different test, about whether a
// town SURVIVES 200 years), and it is also the limitation -- these knobs prove the
// achievement's goal wiring fires, not that the town could have got there on its own.
//
// Both default to off; unset behaviour is byte-unchanged.
//   BANISHED_ACH_TIME_SCALE=<f>   multiply the achievement-manager Time by f
//   BANISHED_ACH_YEAR_BOOST=<n>   add n to the year counter every frame
//   BANISHED_ACH_YEAR_MAX=<n>     stop boosting at n years (default 250)

#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "Game/Calendar.h"         // B4: GameUpdateLoop::m_dayCounter
#include "options.h"
#include "clamped_atoi.h"     // review F-087: checked parse of BANISHED_* text
#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace {

float s_timeScale = -1.0f;      // -1 = env unread
int   s_yearBoost = -1;
int   s_yearMax   = 250;
int   s_lastLoggedYear = -1;

}  // namespace

// Multiplier applied to the Time handed to AchievementManager::Update.
extern "C" float AchClock_TimeScale() {
    if (s_timeScale < 0.0f) {
        s_timeScale = 1.0f;
        if (const char* e = Opt().achTimeScale) {
            const float v = ClampedAtof(e);
            if (v > 0.0f) s_timeScale = v;
        }
        if (s_timeScale != 1.0f) {
            std::fprintf(stderr, "[AchClock] achievement Time scaled x%.1f — every "
                                 "TimeGoal advances that much faster (the "
                                 "AreGoalsMetExcept reset still applies)\n", s_timeScale);
            std::fflush(stderr);
        }
    }
    return s_timeScale;
}

extern "C" void AchClock_Tick(int frameNum) {
    if (s_yearBoost < 0) {
        s_yearBoost = 0;
        if (const char* e = Opt().achYearBoost) {
            const int v = HarnessAtoi(e);
            if (v > 0) s_yearBoost = v;
        }
        if (const char* e = Opt().achYearMax) {
            const int v = HarnessAtoi(e);
            if (v > 0) s_yearMax = v;
        }
        if (s_yearBoost > 0) {
            std::fprintf(stderr, "[AchClock] year counter +%d/frame up to %d "
                                 "(gameWorld+0x128 -> +0x00, the field stat 26 reads)\n",
                         s_yearBoost, s_yearMax);
            std::fflush(stderr);
        }
    }
    if (s_yearBoost <= 0) return;
    if (GameUpdateLoop::_singleton == nullptr) return;

    Calendar* dateObj = GameUpdateLoop::_singleton->m_dayCounter;   // +0x128
    if (!dateObj) return;

    int32_t* year = &dateObj->year;                                   // +0x00
    if ((int)*year >= s_yearMax) return;
    *year = (int32_t)((uint32_t)*year + (uint32_t)s_yearBoost);

    // One line per decade: enough to see the counter move in the log without
    // drowning the achievement census it exists to serve.
    const int decade = (int)*year / 10;
    if (decade != s_lastLoggedYear) {
        s_lastLoggedYear = decade;
        std::fprintf(stderr, "[AchClock] f=%d year=%u\n", frameNum, *year);
        std::fflush(stderr);
    }
}
