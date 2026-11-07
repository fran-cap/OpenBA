// ============================================================================
// src/ext/better_clouds/cloud_noise.h -- the clouds' tileable noise texture (pure)
// ============================================================================
// NOT RETAIL. docs/extensions/handins/BETTERCLOUDS.md section 3. Standard C++ only (the unit tests link
// it). Our own data, made at load: no game content.
//
// One square RGBA8 tile that wraps on both axes (every lattice is periodic in the tile), four channels
// the sky program reads (src/render/common/better_clouds/cloud_sky.hlsl):
//   R  Perlin-Worley: Perlin fBm (5 octaves, period 4) dilated by inverted Worley fBm: the base shape
//   G  Perlin fBm (5 octaves, period 8, another seed): the mid octave, and with A the warp field
//   B  inverted Worley fBm (3 octaves, period 16): billowy detail
//   A  Perlin fBm (4 octaves, period 4, a third seed): the warp field with G
// Each channel is histogram-equalised to 0..255, so the program's coverage is close to the fraction of
// sky it covers. Deterministic: the same bytes on every platform.
// ============================================================================
#pragma once

#include <cstdint>
#include <vector>

namespace Ext {
namespace BetterClouds {

constexpr int kNoiseSize = 256;

// size x size texels, RGBA8, row-major from the top. `size` is a power of two.
std::vector<uint8_t> BakeNoise(int size);

}  // namespace BetterClouds
}  // namespace Ext
