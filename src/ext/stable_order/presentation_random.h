#pragma once
// ============================================================================
// src/ext/stable_order/presentation_random.h -- fix.stable_order: the sound's own Random
// ============================================================================
// NOT RETAIL (docs/retail_bugs.md §36, docs/extensions/handins/STABLEORDER.md).
// Retail feeds the WORLD Random (GameUpdateLoop +0xd8) to two per-frame consumers that
// are not the simulation:
//   * Audio::Scene::Update (GameUpdateLoop::Update 0x1400966aa: R9 = [gw+0xd8], the call
//     at 0x1400966bb) draws twice per sample it starts (the variant, then the pitch);
//   * the MusicPlayer (0x1400a40e0) draws the next track when the current one stops.
// Both happen when a sound ends on the audio device, which is wall-clock time, so the
// number of world draws between two simulation steps depends on the machine. The next
// simulation draw (a citizen's disease roll, a wander target) then gets another value,
// and two runs of one seed part.
//
// With the row on those two consumers draw from this Random instead: presentation only
// (which variant of a sound, its pitch, which track), seeded once with a constant, not
// saved. The world Random then advances only inside the simulation.
// ============================================================================
#include <cstdint>

#include "system/Value.h"   // System::Random

namespace Ext {
namespace StableOrder {

inline System::Random& PresentationRandom() {
    static System::Random rng(0x5eed0001u);
    return rng;
}

}  // namespace StableOrder
}  // namespace Ext
