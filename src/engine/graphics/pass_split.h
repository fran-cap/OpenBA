#pragma once
// ============================================================================
// src/engine/graphics/pass_split.h -- the per-scene-group CPU split (NOT RETAIL)
// ============================================================================
// Issue #253: stress-harness counters only, identical output. ext.render_pipe's two
// loops (src/ext/render_pipe/passes.cpp) time each scene group of pass 1 and each entry's
// shadow-map and light loops of pass 2; SunLight::AddDrawItems (wave28_lane_partsys.cpp)
// times its own share, which runs inside the main group's globals walk and is moved from
// that group's slot to the shadow slot. The stress harness sets g_on when it is enabled
// and prints the slots on its [Stress::pipe] line once per window; with g_on clear every
// call is one branch and reads no clock. Like SkinMemo's counters, the engine does not
// include the harness.
// ============================================================================

#include <atomic>
#include <cstdint>

#include "platform_time.h"   // Platform::PerfCounter

namespace PassSplit {

enum Slot : int {
    kP1Main = 0,   // pass 1, the scene entry with shader 0 (less the sun's share)
    kP1Shadow,     // pass 1, SunLight::AddDrawItems (the shadow group's gather)
    kP1Refl,       // pass 1, the scene entry with shader 2 (the reflection)
    kP2Shadow,     // pass 2, every entry's DrawShadowMap loop
    kP2Refl,       // pass 2, the reflection entry's light loops
    kP2Main,       // pass 2, the main entry's light loops (with the AO seam)
    kSlots
};

inline int g_on = 0;                          // set by the stress harness
inline std::atomic<int64_t> g_ticks[kSlots];  // performance-counter ticks, taken per window
inline int g_p1Group = -1;                    // main thread: the open pass-1 slot, -1 outside pass 1

// The scene entry's slot, or -1 for an entry that is neither main nor reflection.
inline int P1Slot(int32_t shader) {
    if (shader == 0) return kP1Main;
    if (shader == 2) return kP1Refl;
    return -1;
}
inline int P2Slot(int32_t shader) {
    if (shader == 0) return kP2Main;
    if (shader == 2) return kP2Refl;
    return -1;
}

// 0 when the split is off.
inline int64_t Begin() {
    if (g_on == 0) return 0;
    int64_t t = 0;
    Platform::PerfCounter(&t);
    return t;
}

inline void End(int slot, int64_t t0) {
    if (t0 == 0 || slot < 0) return;
    int64_t t = 0;
    Platform::PerfCounter(&t);
    g_ticks[slot].fetch_add(t - t0, std::memory_order_relaxed);
}

// SunLight::AddDrawItems' span: counted only inside a timed pass-1 group, and moved out
// of that group's slot.
inline void EndSun(int64_t t0) {
    if (t0 == 0 || g_p1Group < 0) return;
    int64_t t = 0;
    Platform::PerfCounter(&t);
    g_ticks[kP1Shadow].fetch_add(t - t0, std::memory_order_relaxed);
    g_ticks[g_p1Group].fetch_sub(t - t0, std::memory_order_relaxed);
}

}  // namespace PassSplit
