// src/ext/better_clouds/cloud_noise.cpp -- cloud_noise.h (BETTERCLOUDS)
//
// NOT RETAIL. Periodic Perlin and Worley noise (every lattice index taken modulo the period, so the
// tile wraps exactly), after the recipe in docs/extensions/handins/BETTERCLOUDS.md section 3.

#include "ext/better_clouds/cloud_noise.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace Ext {
namespace BetterClouds {

namespace {

uint32_t Hash(int x, int y, uint32_t seed) {
    uint32_t h = static_cast<uint32_t>(x) * 0x8da6b343u ^ static_cast<uint32_t>(y) * 0xd8163841u ^
                 seed * 0xcb1ab31fu;
    h ^= h >> 13;
    h *= 0x5bd1e995u;
    h ^= h >> 15;
    return h;
}

int Wrap(int i, int period) { return ((i % period) + period) % period; }

float Fade(float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); }

float Lerp(float a, float b, float t) { return a + (b - a) * t; }

float Grad(int ix, int iy, float fx, float fy, int period, uint32_t seed) {
    const float a = static_cast<float>(Hash(Wrap(ix, period), Wrap(iy, period), seed)) * (6.2831853f / 4294967296.0f);
    return std::cos(a) * fx + std::sin(a) * fy;
}

// About -0.7 .. 0.7.
float Perlin(float u, float v, int period, uint32_t seed) {
    const float x = u * static_cast<float>(period);
    const float y = v * static_cast<float>(period);
    const int ix = static_cast<int>(std::floor(x));
    const int iy = static_cast<int>(std::floor(y));
    const float fx = x - static_cast<float>(ix);
    const float fy = y - static_cast<float>(iy);
    const float n00 = Grad(ix, iy, fx, fy, period, seed);
    const float n10 = Grad(ix + 1, iy, fx - 1.0f, fy, period, seed);
    const float n01 = Grad(ix, iy + 1, fx, fy - 1.0f, period, seed);
    const float n11 = Grad(ix + 1, iy + 1, fx - 1.0f, fy - 1.0f, period, seed);
    const float sx = Fade(fx);
    const float sy = Fade(fy);
    return Lerp(Lerp(n00, n10, sx), Lerp(n01, n11, sx), sy);
}

float PerlinFbm(float u, float v, int period0, int octaves, uint32_t seed) {
    float sum = 0.0f;
    float amp = 0.5f;
    float norm = 0.0f;
    for (int o = 0; o < octaves; ++o) {
        sum += amp * Perlin(u, v, period0 << o, seed + static_cast<uint32_t>(o) * 101u);
        norm += amp;
        amp *= 0.5f;
    }
    return sum / norm;
}

// F1 distance to the nearest feature point, about 0..1.
float Worley(float u, float v, int period, uint32_t seed) {
    const float x = u * static_cast<float>(period);
    const float y = v * static_cast<float>(period);
    const int ix = static_cast<int>(std::floor(x));
    const int iy = static_cast<int>(std::floor(y));
    float best = 1e9f;
    for (int j = -1; j <= 1; ++j) {
        for (int i = -1; i <= 1; ++i) {
            const uint32_t h = Hash(Wrap(ix + i, period), Wrap(iy + j, period), seed);
            const float px = static_cast<float>(ix + i) + static_cast<float>(h & 0xffffu) / 65536.0f;
            const float py = static_cast<float>(iy + j) + static_cast<float>(h >> 16) / 65536.0f;
            best = std::min(best, (px - x) * (px - x) + (py - y) * (py - y));
        }
    }
    return std::min(std::sqrt(best), 1.0f);
}

// Billows: 1 at a cell's centre.
float WorleyFbmInv(float u, float v, int period0, uint32_t seed) {
    return 0.625f * (1.0f - Worley(u, v, period0, seed)) + 0.25f * (1.0f - Worley(u, v, period0 * 2, seed + 1u)) +
           0.125f * (1.0f - Worley(u, v, period0 * 4, seed + 2u));
}

float Remap(float x, float a, float b, float c, float d) { return c + (x - a) * (d - c) / (b - a); }

// The channel's values replaced by their rank: 0..255, evenly filled.
void Equalise(const std::vector<float>& values, std::vector<uint8_t>& out, int channel) {
    const size_t n = values.size();
    std::vector<uint32_t> order(n);
    std::iota(order.begin(), order.end(), 0u);
    std::stable_sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b) { return values[a] < values[b]; });
    for (size_t r = 0; r < n; ++r) {
        const uint32_t texel = order[r];
        out[static_cast<size_t>(texel) * 4u + static_cast<size_t>(channel)] =
            static_cast<uint8_t>((r * 256u) / n);
    }
}

}  // namespace

std::vector<uint8_t> BakeNoise(int size) {
    const size_t count = static_cast<size_t>(size) * static_cast<size_t>(size);
    std::vector<float> r(count);
    std::vector<float> g(count);
    std::vector<float> b(count);
    std::vector<float> a(count);
    const float inv = 1.0f / static_cast<float>(size);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const float u = (static_cast<float>(x) + 0.5f) * inv;
            const float v = (static_cast<float>(y) + 0.5f) * inv;
            const size_t i = static_cast<size_t>(y) * static_cast<size_t>(size) + static_cast<size_t>(x);
            const float perlin01 = PerlinFbm(u, v, 4, 5, 11u) * 0.7f + 0.5f;
            const float billow = WorleyFbmInv(u, v, 4, 23u);
            r[i] = Remap(perlin01, billow - 1.0f, 1.0f, 0.0f, 1.0f);
            g[i] = PerlinFbm(u, v, 8, 5, 37u);
            b[i] = WorleyFbmInv(u, v, 16, 53u);
            a[i] = PerlinFbm(u, v, 4, 4, 71u);
        }
    }
    std::vector<uint8_t> out(count * 4u);
    Equalise(r, out, 0);
    Equalise(g, out, 1);
    Equalise(b, out, 2);
    Equalise(a, out, 3);
    return out;
}

}  // namespace BetterClouds
}  // namespace Ext
