#pragma once
// ============================================================================
// src/ext/stable_order/weather_noise.h -- fix.stable_order: the weather's first noise
// ============================================================================
// NOT RETAIL (docs/retail_bugs.md §36, docs/extensions/handins/STABLEORDER.md).
// Retail's WeatherSimulation ctor (0x140093f60, weather_sim.cpp NewWeatherNoise) builds the
// temperature Perlin noise from two stack `System::Random`s made with the default ctor, which
// seeds from the wall clock (`_time64`, 0x1800c7f20). The noise is not saved, and it is only
// replaced, from the world Random, when the calendar reaches a half-year keyframe
// (WeatherTypeStateMachine, kf 0 or 24). Until then the town's temperature follows the second
// the game was started or loaded at, and the sim reads it: at 34 F and below the weather takes
// no ambient-spawn roll from the world Random (WeatherSimulation::Update part 6), houses burn
// wood, citizens grow cold. Two runs of one seed then part on the first warm Rain tick.
//
// With the row on, GameUpdateLoop::Load rebuilds that first noise from a Random of its own seeded
// with the map seed: the same shape (two draws, retail's persistence and octaves), so every game of
// one seed starts with one temperature curve. A new game rebuilds it right after the ctor; a load
// after the save has restored the map seed (the load loop is built with seed 0). The world Random is
// not touched. Nothing is saved: a save made before the first half-year keyframe continues on the
// same curve after a load; one made after it gets the map seed's noise back until the next keyframe,
// where the unbroken game had the noise of its last keyframe (retail gets a clock-seeded one).
// ============================================================================
#include <cstdint>

class WeatherSimulation;

namespace Ext {
namespace StableOrder {

void SeedWeatherNoise(WeatherSimulation& weather, uint32_t mapSeed);

}  // namespace StableOrder
}  // namespace Ext
