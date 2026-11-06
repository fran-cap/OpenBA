// ============================================================================
// tests/unit/ext_map_overlays_math_test.cpp -- ext.map_overlays: blur and colour
// ============================================================================
// src/ext/map_overlays/overlay_math.{h,cpp} on their own: the slider's radius, the blur (identity at
// radius 0, total kept away from the edge, symmetric, spread), the range (a high percentile of the
// non-zero tiles), the ramp (transparent at 0, blue at the bottom, red at the top, alpha rising) and
// Colourise.
// ============================================================================
#include "unit.h"
#include "ext/map_overlays/overlay_math.h"

#include <cmath>
#include <cstdint>
#include <vector>

namespace {

using namespace Ext::MapOverlays;

bool Near(float a, float b, float eps) {
    return std::fabs(a - b) <= eps;
}

double Sum(const std::vector<float>& g) {
    double s = 0.0;
    for (float v : g) {
        s += v;
    }
    return s;
}

uint32_t R(uint32_t c) {
    return c & 0xff;
}
uint32_t G(uint32_t c) {
    return (c >> 8) & 0xff;
}
uint32_t B(uint32_t c) {
    return (c >> 16) & 0xff;
}
uint32_t A(uint32_t c) {
    return c >> 24;
}

}  // namespace

UNIT_TEST(ext_map_overlays_blur_radius_from_slider) {
    CHECK(BlurRadiusTiles(0.0f) == 0.0f);
    CHECK(BlurRadiusTiles(-1.0f) == 0.0f);
    CHECK(Near(BlurRadiusTiles(1.0f), kMaxBlurTiles, 1e-5f));
    CHECK(Near(BlurRadiusTiles(2.0f), kMaxBlurTiles, 1e-5f));
    CHECK(BlurRadiusTiles(0.25f) < BlurRadiusTiles(0.5f));
}

UNIT_TEST(ext_map_overlays_blur_radius_zero_is_identity) {
    const int size = 16;
    std::vector<float> g(size * size, 0.0f);
    g[5 * size + 7] = 3.0f;
    g[10 * size + 2] = 1.5f;
    const std::vector<float> before = g;
    Blur(g, size, 0.0f);
    CHECK(g == before);
    Blur(g, size, 0.3f);   // a box of one tile: still exact
    CHECK(g == before);
}

UNIT_TEST(ext_map_overlays_blur_keeps_mass_and_spreads) {
    const int size = 64;
    std::vector<float> g(size * size, 0.0f);
    g[32 * size + 32] = 1000.0f;
    g[20 * size + 40] = 250.0f;
    const double before = Sum(g);
    Blur(g, size, 8.0f);
    CHECK(Near(static_cast<float>(Sum(g)), static_cast<float>(before), 0.01f));
    CHECK(g[32 * size + 32] < 1000.0f);
    CHECK(g[32 * size + 36] > 0.0f);
    for (float v : g) {
        CHECK(v >= 0.0f);
    }
}

UNIT_TEST(ext_map_overlays_blur_loses_mass_only_at_the_edge) {
    const int size = 32;
    std::vector<float> g(size * size, 0.0f);
    g[0] = 100.0f;   // the corner: much of the box lies outside the map
    Blur(g, size, 6.0f);
    const double after = Sum(g);
    CHECK(after < 100.0);
    CHECK(after > 0.0);
}

UNIT_TEST(ext_map_overlays_blur_is_symmetric) {
    const int size = 41;
    std::vector<float> g(size * size, 0.0f);
    const int c = 20;
    g[c * size + c] = 50.0f;
    Blur(g, size, 10.0f);
    for (int d = 1; d < 12; ++d) {
        const float right = g[c * size + c + d];
        CHECK(Near(right, g[c * size + c - d], 1e-4f));
        CHECK(Near(right, g[(c + d) * size + c], 1e-4f));
        CHECK(Near(right, g[(c - d) * size + c], 1e-4f));
    }
    CHECK(g[c * size + c] > g[c * size + c + 5]);
}

UNIT_TEST(ext_map_overlays_blur_ignores_a_wrong_size) {
    std::vector<float> g(10, 1.0f);
    const std::vector<float> before = g;
    Blur(g, 4, 5.0f);
    CHECK(g == before);
}

UNIT_TEST(ext_map_overlays_range_max_is_a_high_percentile) {
    CHECK(RangeMax(std::vector<float>(100, 0.0f)) == 0.0f);
    CHECK(RangeMax(std::vector<float>()) == 0.0f);
    std::vector<float> g(400, 0.0f);
    for (int i = 0; i < 100; ++i) {
        g[i * 4] = static_cast<float>(i + 1);   // 1..100 among zeros
    }
    const float m = RangeMax(g);
    CHECK(m >= 94.0f && m <= 96.0f);
    g[1] = 1e6f;   // one barn does not wash the rest out
    CHECK(RangeMax(g) < 1000.0f);
    std::vector<float> one(9, 0.0f);
    one[4] = 7.0f;
    CHECK(RangeMax(one) == 7.0f);
}

UNIT_TEST(ext_map_overlays_ramp_endpoints) {
    CHECK(Ramp(0.0f) == 0u);
    CHECK(Ramp(-3.0f) == 0u);
    CHECK(Ramp(std::nanf("")) == 0u);
    const uint32_t top = Ramp(1.0f);
    CHECK(R(top) == 255 && G(top) == 0 && B(top) == 0);
    CHECK(A(top) >= 190 && A(top) <= 215);   // ~0.8 opaque
    CHECK(Ramp(5.0f) == top);
    const uint32_t low = Ramp(0.01f);
    CHECK(B(low) > 200 && R(low) == 0);
    CHECK(A(low) > 0 && A(low) < A(top));
    const uint32_t mid = Ramp(0.5f);
    CHECK(G(mid) == 255 && R(mid) == 0 && B(mid) == 0);
    uint32_t lastA = 0;
    for (int i = 1; i <= 20; ++i) {
        const uint32_t a = A(Ramp(static_cast<float>(i) / 20.0f));
        CHECK(a >= lastA);
        lastA = a;
    }
}

UNIT_TEST(ext_map_overlays_colourise) {
    std::vector<float> g = {0.0f, 5.0f, 10.0f, 20.0f};
    std::vector<uint32_t> px;
    Colourise(g, 10.0f, px);
    CHECK(px.size() == 4);
    CHECK(px[0] == 0u);
    CHECK(px[1] == Ramp(0.5f));
    CHECK(px[2] == Ramp(1.0f));
    CHECK(px[3] == Ramp(1.0f));   // clamped
    Colourise(g, 0.0f, px);
    CHECK(px.size() == 4);
    for (uint32_t c : px) {
        CHECK(c == 0u);
    }
}
