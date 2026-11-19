// src/ext/stable_order/weather_noise.cpp -- see weather_noise.h. NOT RETAIL: fix.stable_order.
#include "ext/stable_order/weather_noise.h"

#include "Game/WeatherSimulation.h"
#include "Game/exe_constants.h"   // kRdata_140172100 / kRdata_140172194: the ctor's persistence and octaves
#include "system/Value.h"         // System::Random
#include "utility/PerlinNoise.h"  // RtUtility_PerlinNoise_Ctor

namespace Ext {
namespace StableOrder {

namespace {
// Keeps this Random's sequence apart from the world Random's, which is seeded with the same map seed.
constexpr uint32_t kWeatherNoiseSalt = 0x5eed0002u;
}  // namespace

void SeedWeatherNoise(WeatherSimulation& weather, uint32_t mapSeed) {
    System::Random rng(mapSeed ^ kWeatherNoiseSalt);
    // Retail's order (0x140093faf..0x140093ff7): the first Random's draw, then the second's,
    // handed to the ctor second-then-first.
    const uint32_t seed1 = rng.Rand();
    const uint32_t seed2 = rng.Rand();
    RtUtility::PerlinNoise noise;
    RtUtility_PerlinNoise_Ctor(&noise, kRdata_140172100, kRdata_140172194, static_cast<int>(seed2),
                               static_cast<int>(seed1));
    weather.noise = noise;
}

}  // namespace StableOrder
}  // namespace Ext
