#pragma once
// ============================================================================
// src/engine/graphics/shadow_zoom.h -- ext.max_shadow_zoom: how much of the sun
// map the shadow casters are drawn into, chosen from the view
// ============================================================================
//
// NOT RETAIL (docs/extensions/handins/SHADOWZOOM.md section 6; a value key of
// ext.max_shadow_res). Retail fits the sun box to the view's ground footprint
// (SunLight::GetSunCamera), so the box grows with the zoom like the screen's
// pixels do; what changes the ratio is the camera's pitch. At retail's default
// tilt (PlayerToolManager::manualPitch 0.75: -34 degrees at zoom 20, -70 at 130)
// an 8192 map gives about 1.1 shadow texels per bottom-edge screen pixel at zoom
// 20 and 1.9 at zoom 130 (2560x1440). So the zoomed-out view draws twice the
// texels the screen can show.
//
// The map stays allocated at its full size (no rebuild, no hitch). The sun
// camera's projection is scaled in clip space so that the box lands in the
// first 1 / 2^shift of the map along x and y: the casters rasterise into that
// sub-rectangle and DrawLight's sampling matrix, built from the same projection,
// reads it back. The PCF texel size (1 / Width()) is still one texel, and the
// rest of the map holds the cleared depth (lit), as retail's guard border does.
//
// THE RULE (the selector below): the shift is the largest one that keeps at
// least kStepDown texels per screen pixel at the bottom-centre pixel, in its
// smaller direction (the binding one: the nearest ground is the finest), and a
// level is left only when it drops below kStepUp (hysteresis, so a zoom near
// the line does not flicker). The map never goes below kMinSize.
//
// Header-only and free of game types: tests/unit/shadow_zoom_test.cpp drives it.
// ============================================================================

#include <cmath>
#include <cstdint>

namespace Graphics {
namespace ShadowZoom {

// Halve the drawn size when the halved map still gives this many texels per pixel. The engine reads
// 1.90 texels per bottom pixel for the full 8192 map at zoom 130 (2560x1440, default tilt), so 0.9
// switches from about zoom 115 with a margin; 8192 at zoom 20 gives 1.09 (shadow_zoom_test.cpp).
constexpr float kStepDown = 0.9f;
// Go back up a level when the current one gives fewer than this (about zoom 90 on the same screen).
constexpr float kStepUp = 0.8f;
// The smallest drawn size (retail's own High).
constexpr int32_t kMinSize = 2048;
// The largest shift (a quarter of the map along each axis).
constexpr int32_t kMaxShift = 2;
// GetSunCamera's ground plane.
constexpr float kGroundZ = -2.0f;

namespace Detail {

inline bool GroundHit(const float xf[12], const float p[3], float out[3]) {
    const float dx = xf[0] * p[0] + xf[3] * p[1] + xf[6] * p[2];
    const float dy = xf[1] * p[0] + xf[4] * p[1] + xf[7] * p[2];
    const float dz = xf[2] * p[0] + xf[5] * p[1] + xf[8] * p[2];
    if (!(dz < 0.0f))
        return false;
    const float t = (kGroundZ - xf[11]) / dz;
    if (!(t > 0.0f))
        return false;
    out[0] = xf[9] + t * dx;
    out[1] = xf[10] + t * dy;
    out[2] = xf[11] + t * dz;
    return true;
}

inline float Distance(const float a[3], const float b[3]) {
    const float x = a[0] - b[0];
    const float y = a[1] - b[1];
    const float z = a[2] - b[2];
    return std::sqrt(x * x + y * y + z * z);
}

}  // namespace Detail

// The world size of the bottom-centre screen pixel on the ground plane, the smaller of its width
// and its depth. `corners`: the view frustum's four near-plane corners in camera space (x right,
// y forward, z up; Volume::Frustum::GetCorners' first four). `xf`: Camera::_cameraToWorld as
// axisX, axisY, axisZ, translation. False when a ray misses the ground (a level or upward view).
inline bool BottomPixelWorld(const float corners[4][3], const float xf[12], int32_t width, int32_t height,
                             float& out) {
    if (width <= 0 || height <= 0)
        return false;
    // The two corners with the smaller z are the bottom edge.
    int order[4] = { 0, 1, 2, 3 };
    for (int i = 0; i < 4; ++i) {
        for (int j = i + 1; j < 4; ++j) {
            if (corners[order[j]][2] < corners[order[i]][2]) {
                const int swap = order[i];
                order[i] = order[j];
                order[j] = swap;
            }
        }
    }
    const float* const b0 = corners[order[0]];
    const float* const b1 = corners[order[1]];
    const float* const t0 = corners[order[2]];
    const float* const t1 = corners[order[3]];
    float bottom[3];
    float up[3];
    float right[3];
    for (int k = 0; k < 3; ++k) {
        bottom[k] = 0.5f * (b0[k] + b1[k]);
        const float top = 0.5f * (t0[k] + t1[k]);
        up[k] = bottom[k] + (top - bottom[k]) / static_cast<float>(height);
        right[k] = bottom[k] + (b1[k] - b0[k]) / static_cast<float>(width);
    }
    float gBottom[3];
    float gUp[3];
    float gRight[3];
    if (!Detail::GroundHit(xf, bottom, gBottom) || !Detail::GroundHit(xf, up, gUp) ||
        !Detail::GroundHit(xf, right, gRight))
        return false;
    const float depth = Detail::Distance(gUp, gBottom);
    const float across = Detail::Distance(gRight, gBottom);
    out = depth < across ? depth : across;
    return std::isfinite(out) && out > 0.0f;
}

// The next shift from the current one. `texelsPerPixel`: the full map's texels per bottom-centre
// pixel (BottomPixelWorld / (sun box side / fullSize)); not positive or not finite = unknown, the
// full map. `fullSize`: the map's Width().
inline int32_t SelectShift(int32_t current, float texelsPerPixel, int32_t fullSize) {
    if (!(texelsPerPixel > 0.0f) || !std::isfinite(texelsPerPixel))
        return 0;
    int32_t maxShift = 0;
    while (maxShift < kMaxShift && (fullSize >> (maxShift + 1)) >= kMinSize)
        ++maxShift;
    int32_t k = current < 0 ? 0 : (current > maxShift ? maxShift : current);
    while (k > 0 && texelsPerPixel / static_cast<float>(1 << k) < kStepUp)
        --k;
    while (k < maxShift && texelsPerPixel / static_cast<float>(1 << (k + 1)) >= kStepDown)
        ++k;
    return k;
}

// Maps the projection's clip x and y from [-1, 1] onto [-1, 2 / 2^shift - 1]: x' = f x + (f - 1) w
// for the row-vector Math::Matrix (clip = v * M, so column j is output j). Depth and w unchanged.
inline void ScaleProjection(float m[16], int32_t shift) {
    if (shift <= 0)
        return;
    const float f = 1.0f / static_cast<float>(1 << shift);
    for (int row = 0; row < 4; ++row) {
        const float w = m[row * 4 + 3];
        m[row * 4 + 0] = f * m[row * 4 + 0] + (f - 1.0f) * w;
        m[row * 4 + 1] = f * m[row * 4 + 1] + (f - 1.0f) * w;
    }
}

}  // namespace ShadowZoom
}  // namespace Graphics
