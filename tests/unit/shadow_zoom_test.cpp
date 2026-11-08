// ============================================================================
// tests/unit/shadow_zoom_test.cpp -- ext.max_shadow_zoom's selector
// (src/engine/graphics/shadow_zoom.h)
// ============================================================================
// The bottom-centre pixel's ground size against a top-down camera worked by
// hand; a ray that misses the ground is unknown; the shift steps down only
// past kStepDown, back up only below kStepUp (hysteresis), never below
// kMinSize; the projection scale maps clip [-1, 1] onto the map's first
// 1 / 2^shift and leaves depth alone.
// ============================================================================
#include "unit.h"
#include "graphics/shadow_zoom.h"

#include <cmath>
#include <cstdint>

namespace {

namespace SZ = Graphics::ShadowZoom;

bool Near(float a, float b, float tol) { return std::fabs(a - b) <= tol; }

// A camera looking straight down from height `h` above the ground plane: forward (axisY) = -z,
// up (axisZ) = +y. Near plane at n, half extents a (x) and b (z).
void TopDown(float h, float n, float a, float b, float corners[4][3], float xf[12]) {
    const float c[4][3] = { { -a, n, -b }, { a, n, -b }, { -a, n, b }, { a, n, b } };
    for (int i = 0; i < 4; ++i)
        for (int k = 0; k < 3; ++k)
            corners[i][k] = c[i][k];
    const float x[12] = { 1, 0, 0, 0, 0, -1, 0, 1, 0, 0, 0, SZ::kGroundZ + h };
    for (int i = 0; i < 12; ++i)
        xf[i] = x[i];
}

}  // namespace

UNIT_TEST(shadow_zoom_bottom_pixel_top_down) {
    float corners[4][3];
    float xf[12];
    TopDown(100.0f, 2.0f, 1.4f, 0.73f, corners, xf);
    float px = 0.0f;
    CHECK(SZ::BottomPixelWorld(corners, xf, 2560, 1440, px));
    const float across = 2.0f * 1.4f * 100.0f / (2.0f * 2560.0f);
    const float depth = 2.0f * 0.73f * 100.0f / (2.0f * 1440.0f);
    CHECK(Near(px, across < depth ? across : depth, 1e-4f));
}

UNIT_TEST(shadow_zoom_bottom_pixel_misses_ground) {
    float corners[4][3];
    float xf[12];
    TopDown(100.0f, 2.0f, 1.4f, 0.73f, corners, xf);
    xf[5] = 1.0f;   // forward points up
    float px = 0.0f;
    CHECK(!SZ::BottomPixelWorld(corners, xf, 2560, 1440, px));
    TopDown(100.0f, 2.0f, 1.4f, 0.73f, corners, xf);
    CHECK(!SZ::BottomPixelWorld(corners, xf, 0, 1440, px));
}

UNIT_TEST(shadow_zoom_select_hysteresis) {
    // 8192: down to 4096 once the half map gives >= 0.9, back only below 0.8.
    CHECK_EQ(SZ::SelectShift(0, 1.09f, 8192), 0);   // zoom 20, default tilt, 2560x1440
    CHECK_EQ(SZ::SelectShift(0, 1.79f, 8192), 0);
    CHECK_EQ(SZ::SelectShift(0, 1.90f, 8192), 1);   // zoom 130 (the engine's reading)
    CHECK_EQ(SZ::SelectShift(1, 1.65f, 8192), 1);   // inside the band: stays
    CHECK_EQ(SZ::SelectShift(1, 1.59f, 8192), 0);
    CHECK_EQ(SZ::SelectShift(0, 3.7f, 8192), 2);    // a quarter (2048) only past 3.6
    CHECK_EQ(SZ::SelectShift(2, 3.3f, 8192), 2);
    CHECK_EQ(SZ::SelectShift(2, 3.1f, 8192), 1);
    CHECK_EQ(SZ::SelectShift(2, 1.0f, 8192), 0);
}

UNIT_TEST(shadow_zoom_select_floor_and_unknown) {
    CHECK_EQ(SZ::SelectShift(0, 2.5f, 4096), 1);    // 4096 -> 2048
    CHECK_EQ(SZ::SelectShift(0, 9.0f, 4096), 1);    // never below 2048
    CHECK_EQ(SZ::SelectShift(1, 9.0f, 2048), 0);    // retail High: never shifted
    CHECK_EQ(SZ::SelectShift(1, 0.0f, 8192), 0);    // unknown = the whole map
    CHECK_EQ(SZ::SelectShift(1, NAN, 8192), 0);
    CHECK_EQ(SZ::SelectShift(1, INFINITY, 8192), 0);
}

UNIT_TEST(shadow_zoom_scale_projection) {
    // An orthographic projection (row-vector: clip = v * M): x scale 0.01, y scale 0.02, z row, w = 1.
    float m[16] = { 0.01f, 0, 0, 0, 0, 0.02f, 0, 0, 0, 0, 0.004f, 0, 0.3f, -0.2f, 0.5f, 1 };
    const float before[16] = { 0.01f, 0, 0, 0, 0, 0.02f, 0, 0, 0, 0, 0.004f, 0, 0.3f, -0.2f, 0.5f, 1 };
    SZ::ScaleProjection(m, 0);
    for (int i = 0; i < 16; ++i)
        CHECK_EQ(m[i], before[i]);
    SZ::ScaleProjection(m, 1);
    // A point at clip x = -1 stays at -1, x = 1 lands on 0; depth and w untouched.
    auto clipX = [&](float vx) { return vx * m[0] + m[12]; };
    const float vMin = (-1.0f - 0.3f) / 0.01f;
    const float vMax = (1.0f - 0.3f) / 0.01f;
    CHECK(Near(clipX(vMin), -1.0f, 1e-5f));
    CHECK(Near(clipX(vMax), 0.0f, 1e-5f));
    CHECK(Near(m[5], 0.01f, 1e-7f));
    CHECK(Near(m[13], -0.6f, 1e-6f));
    CHECK_EQ(m[10], before[10]);
    CHECK_EQ(m[14], before[14]);
    CHECK_EQ(m[15], before[15]);
}
