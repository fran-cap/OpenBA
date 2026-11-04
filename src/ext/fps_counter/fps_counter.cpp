// ============================================================================
// src/ext/fps_counter/fps_counter.cpp -- NOT RETAIL. ext.fps_counter (fps_counter.h has the contract)
// ============================================================================

#include "ext/fps_counter/fps_counter.h"

#include "ext/registry.h"
#include "frame_limiter.h"   // src/app: FrameLimiter::g_extGameFrame
#include "platform_time.h"   // Platform::PerfCounter / PerfFrequency

namespace Ext {
namespace FpsCounter {

namespace {

Stats g_stats;
int64_t g_frequency = 0;

// FrameLimiter::TickInGame, once per drawn game frame (main thread).
void OnGameFrame() {
    int64_t now = 0;
    if (!Platform::PerfCounter(&now)) return;
    g_stats.Frame(now, g_frequency);
}

}  // namespace

void Sync() {
    const bool on = Ext().fps_counter;
    if (on && FrameLimiter::g_extGameFrame != &OnGameFrame) {
        if (g_frequency <= 0) Platform::PerfFrequency(&g_frequency);
        g_stats.Reset();
    }
    FrameLimiter::g_extGameFrame = on ? &OnGameFrame : nullptr;
}

const Stats& Current() {
    return g_stats;
}

int64_t Frequency() {
    return g_frequency;
}

void Reset() {
    g_stats.Reset();
}

}  // namespace FpsCounter
}  // namespace Ext
