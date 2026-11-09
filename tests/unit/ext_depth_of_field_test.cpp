// ============================================================================
// tests/unit/ext_depth_of_field_test.cpp -- ext.depth_of_field's CoC (src/render/common/dof/dof_math.h)
// ============================================================================
// The C++ twin of dof.hlsl's CoC: zero at the focus and inside the in-focus band, positive behind the focus
// and negative in front of it, growing with the distance from it, clamped to the max; the far field tends
// to the max, the near field reaches it; the max CoC scales with the target's height. The passes themselves
// need the running game.
// ============================================================================
#include "unit.h"
#include "dof/dof_math.h"

#include <cmath>

namespace {

constexpr float kGain = 2.5f;
constexpr float kMax = 8.0f;
constexpr float kBand = 0.04f;

float Coc(float distance, float focus) {
    return DofBackend::CocOf(distance, focus, kGain, kMax, kBand);
}

bool Near(float a, float b) { return std::fabs(a - b) < 1e-4f; }

}  // namespace

UNIT_TEST(ext_dof_coc_zero_in_focus) {
    CHECK(Near(Coc(100.0f, 100.0f), 0.0f));
    // Inside the band: (d - f) / d within 0.04 either way.
    CHECK(Near(Coc(103.0f, 100.0f), 0.0f));
    CHECK(Near(Coc(97.0f, 100.0f), 0.0f));
}

UNIT_TEST(ext_dof_coc_sign_and_growth) {
    const float behind = Coc(150.0f, 100.0f);
    const float further = Coc(200.0f, 100.0f);
    const float front = Coc(80.0f, 100.0f);
    CHECK(behind > 0.0f);
    CHECK(further > behind);
    CHECK(front < 0.0f);
    // (150 - 100) / 150 = 1/3, less the band, times the gain, times the max.
    CHECK(Near(behind, (1.0f / 3.0f - kBand) * kGain * kMax));
}

UNIT_TEST(ext_dof_coc_clamped) {
    // The sky (a huge distance): (d - f) / d tends to 1, clamped to the max.
    CHECK(Near(Coc(100000.0f, 100.0f), kMax));
    // Close to the camera: the near field grows without bound before the clamp.
    CHECK(Near(Coc(10.0f, 100.0f), -kMax));
    CHECK(Near(Coc(0.0f, 100.0f), -kMax));
}

UNIT_TEST(ext_dof_max_coc_scales_with_height) {
    CHECK(Near(DofBackend::MaxCocFor(8.0f, 1080), 8.0f));
    CHECK(Near(DofBackend::MaxCocFor(8.0f, 2160), 16.0f));
    CHECK(Near(DofBackend::MaxCocFor(8.0f, 540), 4.0f));
    CHECK(Near(DofBackend::MaxCocFor(8.0f, 10), 1.0f));   // at least a pixel
}
