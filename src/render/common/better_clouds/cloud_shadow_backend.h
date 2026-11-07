#pragma once
// ============================================================================
// src/render/common/better_clouds/cloud_shadow_backend.h -- ext.better_clouds Fancy: the clouds' shadows
// on the ground (docs/extensions/handins/BETTERCLOUDS.md section 2.6)
// ============================================================================
// NOT RETAIL. No graphics-API types: src/ext/better_clouds/better_clouds.cpp includes it (the
// ao/ao_backend.h, better_water/scene_depth_backend.h precedent). Every backend implements both calls
// (src/render/<name>/better_clouds/); one that cannot answers Supported() == false and Draw is a no-op, so
// Fancy draws the sky's clouds and no shadows there.
//
// WHEN. The ground-draw seam (Graphics::Renderer::_extGroundDraw, src/ext/ground_draw.h): between the
// opaque and the blended lists of the main scene entry, on the drawing thread, with the main colour and
// depth bound. The pass copies the bound depth (as SceneDepthBackend does: no readable-depth rebuild),
// rebuilds every opaque pixel's world position and multiplies the clouds' shadow into the bound colour
// (cloud_shadow.hlsl). It leaves every binding as it found it.
// ============================================================================

namespace Video {
class Context;
class ImageBuffer;
}

namespace CloudShadowBackend {

// The program's constants (cloud_shadow.hlsl ShadowConstants), in its order: 40 floats.
struct Frame {
    float invViewProj[16];   // clip -> world, row-vector convention (v * M), row-major
    float off01[4];          // base xy, mid zw (the sky's offsets)
    float offW[4];           // zw: the warp offset
    float sun[4];            // toward the sun xyz (unit, z up), w: the layer height (world units)
    float look[4];           // noise tiles per world unit, coverage, strength, edge softness
    float screen[4];         // the backend fills xy (1 / the target size); z, w: fade from / to (world)
    float camera[4];         // xyz: the camera's world position
};
static_assert(sizeof(Frame) == 40 * sizeof(float), "the program's constant buffer");

// The backend can draw the pass on the live device. Any thread; false before the device exists.
bool Supported();

// The extension wants the pass (Fancy, the clouds drawing). Main thread, idempotent, every frame the
// ground draw is armed and once more with false when it stops. A backend whose pass reads a depth it
// must ask for (Vulkan: draw.h THE READABLE DEPTH) asks here; off releases what the pass holds.
void Request(bool on);

// The pass (drawing thread, the ground-draw seam). `noise` is the sky's noise texture (an engine image
// the backend reads through its own companion). A no-op when unsupported or when no depth is bound.
void Draw(Video::Context& context, const Video::ImageBuffer* noise, const Frame& frame);

}  // namespace CloudShadowBackend
