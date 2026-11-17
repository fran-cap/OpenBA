#pragma once

#include "render_impl.h"   // BANISHED_RENDER_IMPL_BEGIN / _END (several backends in one exe)
// ============================================================================
// src/render/common/map_edge/map_edge_backend.h -- ext.map_edge_fade: the rim fade's pass, the backend contract
// ============================================================================
// NOT RETAIL. docs/extensions/handins/MAPEDGE.md. No graphics-API types: the extension
// (src/ext/map_edge_fade/) includes it, as dof/dof_backend.h and better_clouds/cloud_shadow_backend.h are.
// Every backend implements the three calls (src/render/<name>/map_edge/); one that cannot answers
// Supported() == false and Draw is a no-op, so the map's edge stays as retail draws it there (Metal).
//
// WHEN. The scene-draw seam (Graphics::Renderer::_extSceneDraw, src/ext/scene_draw.h): after the last of
// the main scene entry's three lists, on the drawing thread, with the main colour and depth bound, before
// the tool draw and the UI, and before ext.depth_of_field's blur. The pass takes THE SHARED DEPTH COPY
// (depth_copy/depth_copy_backend.h; made here when nothing fresher is), multiplies the bound main colour by
// map_edge.hlsl's factor (blend ZERO / SRC_COLOR, RGB only) and leaves every binding as it found it.
// ============================================================================

namespace Video {
class Context;
}

namespace MapEdgeBackend {

// The program's constants (map_edge.hlsl EdgeConstants), in its order: 28 floats.
struct Frame {
    float invViewProj[16];   // clip -> world, row-vector convention (v * M), row-major
    float map[4];            // x: where the fade starts (world, from the map's centre), y: 1 / the fade's
                             // width, z: the horizon band's slope
    float screen[4];         // the backend fills xy (1 / the target size)
    float camera[4];         // xyz: the camera's world position
};
static_assert(sizeof(Frame) == 28 * sizeof(float), "the program's constant buffer");

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
}  // namespace MapEdgeBackend
