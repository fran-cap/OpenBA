#pragma once
// ============================================================================
// src/render/common/depth_bias.h -- RenderState depth bias in rasterizer units
// ============================================================================
// Every backend turns the RenderState's normalized depth bias (RenderState+0x3c, serialized as a
// raw float) into D24 rasterizer units the way retail VideoDX11 does at 0x180003827-0x18000384b:
// MULSS by 16777215.0f (2^24 - 1), then CVTTSS2SI.
// ============================================================================

#include <cstdint>

namespace Render {

constexpr float kDepthBiasScale = 16777215.0f;   // 0x180018c5c = 0x4B7FFFFF

inline int32_t DepthBiasUnits(float bias) {
    const float product = bias * kDepthBiasScale;
    // NOT RETAIL: CVTTSS2SI yields INT_MIN (integer indefinite) for an out-of-range or NaN product; the C++
    // cast is undefined there ([conv.fpint]). The bias is a raw float from the RenderState resource, so a
    // modded or crafted one reaches this. Same value as retail on x86, without the undefined cast.
    if (!(product >= -2147483648.0f && product < 2147483648.0f)) {
        return INT32_MIN;
    }
    return static_cast<int32_t>(product);
}

}  // namespace Render
