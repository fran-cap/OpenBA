#pragma once

#include "render_impl.h"   // BANISHED_RENDER_IMPL_BEGIN / _END (several backends in one exe)
// ============================================================================
// src/render/common/dof/dof_backend.h -- ext.depth_of_field: the depth blur's pass, the backend contract
// ============================================================================
// NOT RETAIL. docs/extensions/DOF.md section 3.1 (the live tier). No graphics-API types: the extension
// (src/ext/depth_of_field/) includes it, as ao/ao_backend.h and better_clouds/cloud_shadow_backend.h are.
// Every backend implements the three calls (src/render/<name>/dof/); one that cannot answers
// Supported() == false and Draw is a no-op, so the scene stays sharp there (Metal).
//
// WHEN. The scene-draw seam (Graphics::Renderer::_extSceneDraw, src/ext/scene_draw.h): after the last of
// the main scene entry's three lists, on the drawing thread, with the main colour and depth bound, before
// the tool draw and the UI. The pass
//   1. takes THE SHARED DEPTH COPY (depth_copy/depth_copy_backend.h; made here when nothing fresher is),
//   2. copies the main colour as it stands (single-sampled: the resolved frame so far),
//   3. runs dof.hlsl's prefilter, gather and tent at half resolution into two 8-bit RGBA targets of its own,
//   4. blends the result over the bound main colour (the composite; RGB only),
// and leaves every binding as it found it.
// ============================================================================

#include "dof/dof_math.h"   // CocOf, MaxCocFor (no graphics API, unit-tested)

namespace Video {
class Context;
}

namespace DofBackend {

// The program's constants (dof.hlsl DofConstants), in its order: 36 floats. The backend fills `screen`,
// `halfRes` and scales `lens.z` for the target it finds bound; the extension fills the rest.
struct Frame {
    float invViewProj[16];   // clip -> world, row-vector convention (v * M), row-major
    float screen[4];         // the backend's: xy 1 / the full size, zw the full size
    float halfRes[4];        // the backend's: xy 1 / the half size, zw the half size
    float lens[4];           // x focus distance (world; <= 0: the depth at the screen's centre), y gain,
                             // z max CoC (half-resolution pixels at 1080 lines; the backend scales it), w band
    float camera[4];         // xyz the camera's world position, w the highlight boost
    float look[4];           // x the highlight threshold (luma), y > 0.5: the debug view of the blur map
};
static_assert(sizeof(Frame) == 36 * sizeof(float), "the program's constant buffer");

BANISHED_RENDER_IMPL_BEGIN

// The backend can draw the pass on the live device. Any thread; false before the device exists.
bool Supported();

// The extension wants the pass (the row on, a game drawing). Main thread, idempotent, every frame the seam
// is armed and once more with false when it stops. A backend whose pass reads a depth it must ask for
// (Vulkan: draw.h THE READABLE DEPTH) asks here; off releases what the pass holds.
void Request(bool on);

// The pass (drawing thread, the scene-draw seam). A no-op when unsupported or when nothing is bound.
void Draw(Video::Context& context, const Frame& frame);

BANISHED_RENDER_IMPL_END
}  // namespace DofBackend
