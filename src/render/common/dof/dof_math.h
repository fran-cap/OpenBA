#pragma once
// ============================================================================
// src/render/common/dof/dof_math.h -- ext.depth_of_field: the depth blur's CoC, in C++
// ============================================================================
// NOT RETAIL. docs/extensions/DOF.md section 3.1. The C++ twin of dof.hlsl's CoC (that file's header has
// the method) and the max CoC's scaling, used by the backends (dof_backend.h) and checked by
// tests/unit/ext_depth_of_field_test.cpp. No graphics API, no engine type.
// ============================================================================

#include <cmath>

namespace DofBackend {

// dof.hlsl CocOf: the signed blur radius in half-resolution pixels of a pixel `distance` from the camera,
// with the focus at `focus` (negative: nearer than the focus).
inline float CocOf(float distance, float focus, float gain, float maxCoc, float band) {
    float x = (distance - focus) / (distance > 1e-3f ? distance : 1e-3f);
    const float mag = std::fabs(x) - band;
    x = mag > 0.0f ? (x < 0.0f ? -mag : mag) : 0.0f;
    float c = x * gain;
    c = c < -1.0f ? -1.0f : (c > 1.0f ? 1.0f : c);
    return c * maxCoc;
}

// The max CoC for a target `height` lines tall: the frame's value is for 1080 lines, so the blur covers
// the same share of the picture at any resolution (and at any render scale). At least one pixel.
inline float MaxCocFor(float maxCocAt1080, int height) {
    const float scaled = maxCocAt1080 * static_cast<float>(height) / 1080.0f;
    return scaled < 1.0f ? 1.0f : scaled;
}

}  // namespace DofBackend
