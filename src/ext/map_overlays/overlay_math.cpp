// ============================================================================
// src/ext/map_overlays/overlay_math.cpp -- ext.map_overlays: blur and colour (standard C++ only)
// ============================================================================
// NOT RETAIL. docs/extensions/handins/MAPOVERLAYS.md §5; the API is overlay_math.h. Unit tested
// (tests/unit/ext_map_overlays_math_test.cpp).
//
// The blur is three box passes per axis (a box convolved with itself three times is close to a Gaussian),
// each pass a running sum: O(n) per row whatever the radius. Outside the map counts as zero, so a value
// spreads evenly over the tiles its box covers and the total is kept, apart from what crosses the edge.
// ============================================================================
#include "ext/map_overlays/overlay_math.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace Ext {
namespace MapOverlays {

namespace {

constexpr int kPasses = 3;
constexpr float kPercentile = 0.95f;   // RangeMax: the share of non-zero tiles at or below full colour
constexpr float kMaxAlpha = 204.0f;    // the ramp's alpha at t = 1 (0.8 opaque)

// The half-widths of the kPasses boxes whose sum approximates a Gaussian of `sigma` (the standard
// "boxes for Gauss" split: odd widths wl and wl + 2, m of them the smaller one).
void BoxHalfWidths(float sigma, int out[kPasses]) {
    const double s2 = static_cast<double>(sigma) * sigma;
    const double ideal = std::sqrt(12.0 * s2 / kPasses + 1.0);
    int wl = static_cast<int>(std::floor(ideal));
    if (wl % 2 == 0) {
        --wl;
    }
    if (wl < 1) {
        wl = 1;
    }
    const int wu = wl + 2;
    const double mIdeal = (12.0 * s2 - kPasses * wl * wl - 4.0 * kPasses * wl - 3.0 * kPasses) / (-4.0 * wl - 4.0);
    const int m = static_cast<int>(std::lround(mIdeal));
    for (int i = 0; i < kPasses; ++i) {
        const int w = i < m ? wl : wu;
        out[i] = (w - 1) / 2;
    }
}

// One horizontal box pass of half-width h over every row, src -> dst (zero outside the row).
void BoxRows(const std::vector<float>& src, std::vector<float>& dst, int size, int h) {
    const double inv = 1.0 / (2.0 * h + 1.0);
    for (int z = 0; z < size; ++z) {
        const float* in = src.data() + static_cast<size_t>(z) * size;
        float* out = dst.data() + static_cast<size_t>(z) * size;
        double sum = 0.0;
        for (int x = 0; x <= h && x < size; ++x) {
            sum += in[x];
        }
        for (int x = 0; x < size; ++x) {
            const double v = sum * inv;
            out[x] = v > 0.0 ? static_cast<float>(v) : 0.0f;
            const int add = x + h + 1;
            const int sub = x - h;
            if (add < size) {
                sum += in[add];
            }
            if (sub >= 0) {
                sum -= in[sub];
            }
        }
    }
}

// One vertical box pass of half-width h, src -> dst, row by row with one running sum per column (the
// rows are read in order, so the pass is as cache-friendly as the horizontal one).
void BoxColumns(const std::vector<float>& src, std::vector<float>& dst, int size, int h, std::vector<double>& sums) {
    const double inv = 1.0 / (2.0 * h + 1.0);
    sums.assign(static_cast<size_t>(size), 0.0);
    for (int z = 0; z <= h && z < size; ++z) {
        const float* in = src.data() + static_cast<size_t>(z) * size;
        for (int x = 0; x < size; ++x) {
            sums[x] += in[x];
        }
    }
    for (int z = 0; z < size; ++z) {
        float* out = dst.data() + static_cast<size_t>(z) * size;
        for (int x = 0; x < size; ++x) {
            const double v = sums[x] * inv;
            out[x] = v > 0.0 ? static_cast<float>(v) : 0.0f;
        }
        const int add = z + h + 1;
        const int sub = z - h;
        if (add < size) {
            const float* in = src.data() + static_cast<size_t>(add) * size;
            for (int x = 0; x < size; ++x) {
                sums[x] += in[x];
            }
        }
        if (sub >= 0) {
            const float* in = src.data() + static_cast<size_t>(sub) * size;
            for (int x = 0; x < size; ++x) {
                sums[x] -= in[x];
            }
        }
    }
}

// Running sums leave rounding residue (1e-12 and the like) where the grid should be empty; it would count
// as a non-zero tile in RangeMax. Anything below this share of the grid's peak is zero.
void SnapResidue(std::vector<float>& grid, float peak) {
    const float floor = peak * 1e-6f;
    for (float& v : grid) {
        if (v < floor) {
            v = 0.0f;
        }
    }
}

uint8_t Channel(float v) {
    if (!(v > 0.0f)) {
        return 0;
    }
    if (v >= 255.0f) {
        return 255;
    }
    return static_cast<uint8_t>(v + 0.5f);
}

}  // namespace

float BlurRadiusTiles(float scale) {
    if (!(scale > 0.0f)) {
        return 0.0f;
    }
    if (scale > 1.0f) {
        scale = 1.0f;
    }
    // Squared: the slider's first half stays fine (0..6 tiles), where most of the reading happens.
    return scale * scale * kMaxBlurTiles;
}

void Blur(std::vector<float>& grid, int size, float radiusTiles) {
    if (size <= 0 || !(radiusTiles > 0.0f)) {
        return;
    }
    const size_t n = static_cast<size_t>(size) * size;
    if (grid.size() != n) {
        return;
    }
    // The radius is about two standard deviations: ~95% of a tile's value lands inside it.
    int half[kPasses];
    BoxHalfWidths(radiusTiles * 0.5f, half);
    if (half[0] == 0 && half[1] == 0 && half[2] == 0) {
        return;
    }
    float peak = 0.0f;
    for (float v : grid) {
        peak = std::max(peak, v);
    }
    if (!(peak > 0.0f)) {
        return;
    }
    std::vector<float> tmp(n);
    std::vector<double> sums;
    for (int i = 0; i < kPasses; ++i) {
        if (half[i] == 0) {
            continue;
        }
        BoxRows(grid, tmp, size, half[i]);
        BoxColumns(tmp, grid, size, half[i], sums);
    }
    SnapResidue(grid, peak);
}

float RangeMax(const std::vector<float>& grid) {
    std::vector<float> values;
    for (float v : grid) {
        if (v > 0.0f) {
            values.push_back(v);
        }
    }
    if (values.empty()) {
        return 0.0f;
    }
    const size_t k = static_cast<size_t>(kPercentile * static_cast<float>(values.size() - 1));
    std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(k), values.end());
    return values[k];
}

uint32_t Ramp(float t) {
    if (!(t > 0.0f)) {
        return 0;
    }
    if (t > 1.0f) {
        t = 1.0f;
    }
    // Colour stops: blue, cyan, green, yellow, red at t = 0, 0.25, 0.5, 0.75, 1.
    static const float kStops[5][3] = {
        {0.0f, 0.0f, 255.0f},
        {0.0f, 255.0f, 255.0f},
        {0.0f, 255.0f, 0.0f},
        {255.0f, 255.0f, 0.0f},
        {255.0f, 0.0f, 0.0f},
    };
    const float pos = t * 4.0f;
    int i = static_cast<int>(pos);
    if (i > 3) {
        i = 3;
    }
    const float f = pos - static_cast<float>(i);
    const float r = kStops[i][0] + (kStops[i + 1][0] - kStops[i][0]) * f;
    const float g = kStops[i][1] + (kStops[i + 1][1] - kStops[i][1]) * f;
    const float b = kStops[i][2] + (kStops[i + 1][2] - kStops[i][2]) * f;
    // Alpha rises fast from 0 (a faint value is still seen) to kMaxAlpha at t = 1.
    const float a = kMaxAlpha * std::sqrt(t);
    return static_cast<uint32_t>(Channel(r)) | (static_cast<uint32_t>(Channel(g)) << 8) |
           (static_cast<uint32_t>(Channel(b)) << 16) | (static_cast<uint32_t>(Channel(a)) << 24);
}

void Colourise(const std::vector<float>& grid, float maxValue, std::vector<uint32_t>& out) {
    out.assign(grid.size(), 0u);
    if (!(maxValue > 0.0f)) {
        return;
    }
    const float inv = 1.0f / maxValue;
    for (size_t i = 0; i < grid.size(); ++i) {
        const float v = grid[i];
        if (v > 0.0f) {
            out[i] = Ramp(v * inv);
        }
    }
}

uint32_t LandColour(LandClass c) {
    // 0xAABBGGRR. The alpha is lower than the ramp's top: the classes cover most of the map, and the
    // ground under them should still read.
    switch (c) {
        case LandClass::Open:
            return 0x8c50c85au;   // green  (90, 200, 80)
        case LandClass::NeedsClearing:
            return 0x8c28b4f0u;   // amber  (240, 180, 40)
        case LandClass::Unbuildable:
            return 0x8c3232dcu;   // red    (220, 50, 50)
        default:
            return 0u;
    }
}

void ColouriseLand(const std::vector<float>& grid, std::vector<uint32_t>& out) {
    out.assign(grid.size(), 0u);
    for (size_t i = 0; i < grid.size(); ++i) {
        const float v = grid[i];
        if (v >= 1.0f && v < static_cast<float>(LandClass::Count)) {
            out[i] = LandColour(static_cast<LandClass>(static_cast<int>(v)));
        }
    }
}

}  // namespace MapOverlays
}  // namespace Ext
