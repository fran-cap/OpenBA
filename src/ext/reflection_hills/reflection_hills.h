#pragma once
// ============================================================================
// src/ext/reflection_hills/reflection_hills.h -- fix.reflection_hills (render only)
// ============================================================================
// NOT RETAIL. docs/retail_bugs.md section 35.
//
// RETAIL. WorldGeometry::AddDrawItems (0x1400ae490) culls the terrain in chunks against the group's cull
// camera, each chunk a box of its footprint and a FIXED height range, z -16 .. +16 (.rdata 0x14017228c,
// 0x1401721d0), whatever the ground in it does. The main view looks down and never notices. The water's
// reflection is drawn from the camera mirrored under the water plane, looking up: a hill taller than 16
// across a lake is in the mirrored view, but its 16-high box lies under the mirrored frustum's lower edge
// while the eye is low, so the whole chunk is dropped and the hill is missing from the reflection until the
// camera rises. Retail's nearest zoom (20) keeps the eye mostly above that; ext.better_camera's (6) does not.
//
// THE FIX. For the reflection's group only (shader 2: ReflectionImageBuffer::Render's scene entry), the
// chunk box's top is the highest ground the heightmap can hold (its int16 word / 256: under 128), so no
// chunk with ground in the mirrored view is dropped. The main view's and the shadow map's boxes are
// retail's. It costs the reflection the chunks retail wrongly dropped, close to the water at a low eye.
// The scene grid's node boxes (z -3 .. 8, Runtime 0x18016176c / 0x1801616ec) are not changed: no object was
// lost to them in the runs that found this.
//
// Standard C++ only: tests/unit/ext_reflection_hills_test.cpp links nothing but this header.
// ============================================================================

#include <cstdint>

namespace Ext {
namespace ReflectionHills {

constexpr int32_t kReflectionShader = 2;   // the reflection entry's shader (imagebuffer_derived.cpp)
constexpr float kTallZMax = 128.0f;        // above every height the int16 heightmap holds (32767 / 256)

// The top of a terrain chunk's cull box for a draw group. Row off, or any group but the reflection's:
// retail's own value, as given (the same bits come back).
inline float ChunkZMax(bool fixOn, int32_t groupShader, float retailZMax) {
    if (fixOn && groupShader == kReflectionShader && retailZMax < kTallZMax) return kTallZMax;
    return retailZMax;
}

}  // namespace ReflectionHills
}  // namespace Ext
