// ============================================================================
// src/ext/winter_ice/ice_town.h -- the town's lake ice, as the game tick keeps it, and ext.walk_on_ice's
// save-tail codec (pure)
// ============================================================================
// NOT RETAIL. docs/extensions/handins/WINTERICE.md section 5. Standard C++ only: the unit test
// (tests/unit/ext_winter_ice_test.cpp) links this file with ice_model.h and the save tail alone.
//
// ONE STATE, TWO ROWS. ext.winter_ice (class A) draws the ice; ext.walk_on_ice (class C) lets citizens
// cross a lake while it is solid. Both read this one state, which the game tick advances (winter_ice.cpp
// Tick) whenever either row is on. With the walk row on it is the town's own state: saved in the row's
// record of the save tail and restored on load, so the lake freezes and opens on the same day as in an
// unbroken game. With only the drawing on nothing is saved and a town starts from a guess.
//
// Wire format v1 (little-endian, Writer / Reader of src/ext/savetail.h):
//   u32  flags     bit 0 known (the fronts below are this town's), bit 1 walkable (the frozen cells
//                  are open to walkers); no other bit set
//   u32  freeze    the freeze front, an IEEE float's bits (ice_model.h IceState)
//   u32  thaw      the thaw front, likewise
// Decode accepts exactly what Encode writes; a cut payload, trailing bytes, an unknown flag or a front
// outside [0, kFreezeCap] (NaN included) is refused: the state is emptied (a guess on the next tick),
// one line goes to stderr, and the game plays on.
// ============================================================================
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "ext/winter_ice/ice_model.h"

namespace Ext {
namespace SaveTail { struct StateCodec; }
namespace WinterIce {

inline constexpr const char* kWalkId = "walk_on_ice";
inline constexpr int32_t kWalkWireVersion = 1;

struct IceTown {
    IceState ice;
    bool known = false;               // ice holds this town's fronts (else the next tick guesses)
    bool adopt = false;               // decoded from a save: the next tick takes the town's weather as its own
    bool walkable = false;            // the frozen lake cells are open to walkers (WINTERICE.md 5.2)
    const void* weather = nullptr;    // the WeatherSimulation the fronts belong to (not saved)

    void Clear() { *this = IceTown{}; }
    void Encode(std::vector<uint8_t>& payload) const;
    // False on a malformed payload; the state is then unchanged.
    bool Decode(const uint8_t* bytes, size_t size);
};

// The one town state (main thread: the game tick, the per-frame block, the load and save arms).
IceTown& Town();

// ext.walk_on_ice's record in the save tail (src/ext/state_codecs.cpp).
const SaveTail::StateCodec& WalkCodec();

}  // namespace WinterIce
}  // namespace Ext
