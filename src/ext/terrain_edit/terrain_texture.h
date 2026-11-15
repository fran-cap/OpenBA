#pragma once
// ============================================================================
// src/ext/terrain_edit/terrain_texture.h -- ext.terrain_edit, the ground texture of an edit (NOT RETAIL)
// ============================================================================
// docs/extensions/handins/TERRAINEDIT.md. Standard C++ only, so tests/unit/ext_terrain_edit_test.cpp can
// check it without the game. The game paints the ground from a texture it builds once, when the world is
// generated (WorldGeometry::_heightBytes, uploaded as _heightImage; worldgen.cpp wg_BlendTexture_cf440 and
// Game/worldgen_cells.cpp): four bytes per cell of the height grid, R the rock weight (the brown), G the
// grass weight, B the ambient occlusion, A never written. This computes the bytes of a cell again from the
// heights as they are now, by the same formulas and constants, so that ground the brush has lowered stops
// looking like mountain, a new mountain gets its rock, and a dug lake gets its brown bed and shore.
//
// One difference from the game's own: R's scatter is a perlin noise whose seeds the game drew at generation
// and does not keep. A cell whose height the edit changed takes a smooth value noise of its own instead
// (the same range, squared); a cell whose height did not change keeps the game's byte.
// ============================================================================

#include <cmath>
#include <cstdint>

#include "ext/terrain_edit/terrain_edit_math.h"

namespace Ext {
namespace TerrainEdit {

// The game's constants (worldgen_cells.cpp, read from the exe's .rdata).
constexpr float kRockFloor  = -0.5f;        // below this height a cell is all rock (the lake bed)
constexpr float kRockHigh   = 8.0f;         // rock ramps in above this height ...
constexpr float kGrassHigh  = 7.0f;         // ... and the grass byte above this one
constexpr float kBandDiv    = 3.0f;         // ... over this many units
constexpr float kGrassUpper = 0.998969f;    // the slope's normal.z above which the grass byte is 0
constexpr float kGrassLower = 0.85f;        // ... and at or below which it is 255
constexpr float kGrassBand  = 0.149047f;    // the feathering between them

struct Texel {
    uint8_t rock;
    uint8_t grass;
    uint8_t occlusion;
};

// The surface normal of the height grid at (col, row): the game's cf150, central differences of the
// heights, normal.z from 2. `height(col, row)` is the height in world units at a grid vertex; coordinates
// are clamped to [0, dim] here before `height` is called.
template <class HeightFn>
inline void CellNormal(HeightFn height, int dim, int col, int row, float out[3]) {
    auto clampTo = [dim](int v) { return v < 0 ? 0 : (v > dim ? dim : v); };
    const int c = clampTo(col);
    const int r = clampTo(row);
    const float nx = -(height(clampTo(c + 1), r) - height(clampTo(c - 1), r));
    const float ny = -(height(c, clampTo(r + 1)) - height(c, clampTo(r - 1)));
    const float nz = 2.0f;
    const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
    out[0] = nx / len;
    out[1] = ny / len;
    out[2] = nz / len;
}

// The rock byte of a cell: all rock below the floor, else the scatter (a byte, 0 to 255: the game's is a
// perlin noise squared), and rock ramping in above kRockHigh.
inline uint8_t RockByte(float height, uint32_t scatter) {
    uint32_t rock = height >= kRockFloor ? scatter : 255u;
    if (height > kRockHigh) {
        const float t = ClampFloat((height - kRockHigh) / kBandDiv, 0.0f, 1.0f);
        const uint32_t ramp = static_cast<uint32_t>(t * 255.0f);
        if (ramp > rock) rock = ramp;
    }
    return static_cast<uint8_t>(rock & 0xff);
}

// The grass byte of a cell from its surface normal's z and its height.
inline uint8_t GrassByte(float height, float normalZ) {
    uint32_t grass;
    if (normalZ >= kGrassUpper || normalZ <= kGrassLower) {
        grass = normalZ <= kGrassLower ? 255u : 0u;
    } else {
        grass = static_cast<uint32_t>((1.0f - (normalZ - kGrassLower) / kGrassBand) * 255.0f);
    }
    if (height > kGrassHigh) {
        const float t = ClampFloat((height - kGrassHigh) / kBandDiv, 0.0f, 1.0f);
        const uint32_t ramp = static_cast<uint32_t>(t * 255.0f);
        if (ramp > grass) grass = ramp;
    }
    return static_cast<uint8_t>(grass & 0xff);
}

// The ambient occlusion byte of a cell: the game's cf440 third pass. A circle of radius 10 of neighbours
// (r^2 < 101): 1 minus the mean of max(dot(the cell's normal, the direction to the neighbour), 0), times
// the z of the sum of the 3 x 3 normals around the cell, normalised; scaled to 255.
template <class HeightFn>
inline uint8_t OcclusionByte(HeightFn height, int dim, int col, int row) {
    float cn[3];
    CellNormal(height, dim, col, row, cn);
    const float cx = static_cast<float>(col);
    const float cy = static_cast<float>(row);
    const float ch = height(col, row);
    float accumDot = 0.0f;
    float count = 0.0f;
    float sumNx = 0.0f;
    float sumNy = 0.0f;
    float sumNz = 0.0f;
    for (int dz = -10; dz < 10; ++dz) {
        const int nrow = row + dz;
        if (nrow < 0 || nrow >= dim) continue;
        for (int dx = -10; dx < 11; ++dx) {
            const int ncol = col + dx;
            if (ncol < 0 || ncol >= dim) continue;
            if (dx * dx + dz * dz >= 0x65) continue;   // the circle
            float nn[3];
            CellNormal(height, dim, ncol, nrow, nn);
            float dirx = static_cast<float>(ncol) - cx;
            float diry = static_cast<float>(nrow) - cy;
            float dirz = height(ncol, nrow) - ch;
            const float len = std::sqrt(dirx * dirx + diry * diry + dirz * dirz);
            if (len > 0.0f) {
                dirx /= len;
                diry /= len;
                dirz /= len;
            }
            float dot = cn[0] * dirx + cn[1] * diry + cn[2] * dirz;
            if (dot < 0.0f) dot = 0.0f;
            accumDot += dot;
            count += 1.0f;
            if (dx > -2 && dx < 2 && dz > -2 && dz < 2) {   // the 3 x 3 around the cell
                sumNx += nn[0];
                sumNy += nn[1];
                sumNz += nn[2];
            }
        }
    }
    const float sl = std::sqrt(sumNx * sumNx + sumNy * sumNy + sumNz * sumNz);
    const float snz = sl > 0.0f ? sumNz / sl : 0.0f;
    const float avg = count > 0.0f ? accumDot / count : 0.0f;
    const float ao = (1.0f - avg) * snz * 255.0f;
    return static_cast<uint8_t>(static_cast<uint32_t>(static_cast<long long>(ao)) & 0xff);
}

// The rock scatter noise of an edited cell, in [0, 1]: smooth value noise from terrain_edit_math.h, three
// octaves from a lattice step of 2 cells (the first version used steps of 4 and made splotches bigger than
// the game's), under a seed of the world's own.
inline float ScatterNoise01(int col, int row, uint32_t seed) {
    float sum = 0.0f;
    float norm = 0.0f;
    float amp = 1.0f;
    float freq = 0.5f;
    for (int octave = 0; octave < 3; ++octave) {
        sum += amp * ValueNoise(static_cast<float>(col) * freq, static_cast<float>(row) * freq,
                                seed + static_cast<uint32_t>(octave) * 0x632be5abu);
        norm += amp;
        amp *= 0.5f;
        freq *= 2.0f;
    }
    return ClampFloat((sum / norm + 1.0f) * 0.5f, 0.0f, 1.0f);
}

// The mean and the standard deviation of ScatterNoise01 over a 64 x 64 patch: what to take off and divide
// by to make it a unit noise.
inline void ScatterNoiseStats(uint32_t seed, float& mean, float& spread) {
    double sum = 0.0;
    double sum2 = 0.0;
    const int n = 64;
    for (int r = 0; r < n; ++r) {
        for (int c = 0; c < n; ++c) {
            const double v = static_cast<double>(ScatterNoise01(c, r, seed));
            sum += v;
            sum2 += v * v;
        }
    }
    const double count = static_cast<double>(n) * static_cast<double>(n);
    const double m = sum / count;
    const double var = sum2 / count - m * m;
    mean = static_cast<float>(m);
    spread = static_cast<float>(var > 1e-12 ? std::sqrt(var) : 1.0);
}

// What the game's own scatter looks like on ordinary ground: the mean and the standard deviation of its
// rock bytes. An edit measures these from the world's own texture (cells between -0.4 and 7.5 high, not
// repainted) so its noise matches the map it is in, whatever the game's perlin did there.
struct ScatterLook {
    float mean = 64.0f;
    float spread = 40.0f;
};

// The scatter byte of an edited cell: the unit noise, scaled to the game's look, held to 0 to 255.
inline uint32_t ScatterByte(float noise01, float noiseMean, float noiseSpread, const ScatterLook& look) {
    const float unit = (noise01 - noiseMean) / noiseSpread;
    const float v = ClampFloat(look.mean + look.spread * unit, 0.0f, 255.0f);
    return static_cast<uint32_t>(v);
}

}  // namespace TerrainEdit
}  // namespace Ext
