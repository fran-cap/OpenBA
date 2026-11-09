#pragma once

#include "render_impl.h"   // BANISHED_RENDER_IMPL_BEGIN / _END (several backends in one exe)
// ============================================================================
// src/render/common/fancy_terrain/terrain_light_backend.h -- ext.fancy_terrain: the terrain's self-shadowing,
// sky visibility and fake bounce on the ground (src/ext/fancy_terrain/horizon.h has the method)
// ============================================================================
// NOT RETAIL. No graphics-API types: src/ext/fancy_terrain/fancy_terrain.cpp includes it (the
// better_clouds/cloud_shadow_backend.h precedent). Every backend implements the three calls
// (src/render/<name>/fancy_terrain/); one that cannot answers Supported() == false and Draw is a no-op.
//
// WHEN. The ground-draw seam (src/ext/ground_draw.h), before the clouds' shadows: between the opaque and
// the blended lists of the main scene entry, on the drawing thread, the main colour and depth bound. The
// pass takes the shared depth copy, rebuilds every opaque pixel's world position, reads the horizon map and
// the ground height there, and multiplies the light it finds into the bound colour (terrain_light.hlsl),
// RGB only. Every binding is left as it was found.
//
// THE TEXTURES. The extension owns the data (CPU, under its own lock for the whole Draw call): two RGBA8
// horizon planes and the ground height (R16 float, world units), `width` x `height` heightmap vertices,
// row-major. The backend keeps GPU copies: a new `generation` (a new world, a new size) or a backend
// without copies (a new device, a backend switch) uploads everything; otherwise only `dirty` (the
// vertices rebuilt since the previous Draw; empty = none). The backend uploads before any other early
// out, so no edit is lost to a frame that does not draw.
// ============================================================================

#include <cstdint>

namespace Video {
class Context;
}

namespace TerrainLightBackend {

// The program's constants (terrain_light.hlsl LightConstants), in its order: 40 floats.
struct Frame {
    float invViewProj[16];   // clip -> world, row-vector convention (v * M), row-major
    float sun[4];            // xyz: toward the sun (unit, z up), w: the sun shadow's strength far away (0..1)
    float look[4];           // x: sky-visibility strength, y: bounce, z: sun edge softness (radians),
                             // w: the height above the ground over which the effect fades (world units)
    float screen[4];         // the backend fills xy (1 / the target size); z, w: fade from / to (world)
    float camera[4];         // xyz: the camera's world position, w: the sun shadow's share near the camera
    float map[4];            // x: half the grid (world x + half = vertex column), y: 1 / width, z: 1 / height,
                             // w: the ambient share of the ground's light (the rest is the sun's, times N.L)
    float tint[4];           // rgb: the bounce colour, w: the distance where the sun shadow reaches full strength
};
static_assert(sizeof(Frame) == 40 * sizeof(float), "the program's constant buffer");

struct Textures {
    int32_t width = 0;
    int32_t height = 0;
    const uint8_t* horizon0 = nullptr;   // directions 0..3, 4 bytes per vertex
    const uint8_t* horizon1 = nullptr;   // directions 4..7
    const uint16_t* ground = nullptr;    // half floats, 2 bytes per vertex
    uint32_t generation = 0;
    int32_t dirtyX0 = 0;                 // [x0, x1) x [y0, y1)
    int32_t dirtyY0 = 0;
    int32_t dirtyX1 = 0;
    int32_t dirtyY1 = 0;
};

BANISHED_RENDER_IMPL_BEGIN

// The backend can draw the pass on the live device. Any thread; false before the device exists.
bool Supported();

// The extension wants the pass. Main thread, idempotent, every frame the ground draw is armed and once
// more with false when it stops. A backend whose pass reads a depth it must ask for asks here. The GPU
// copies of the textures are kept until the device goes (the drawing thread may still use them).
void Request(bool on);

// The pass (drawing thread, the ground-draw seam). A no-op when unsupported or when no depth is bound,
// after the uploads.
void Draw(Video::Context& context, const Textures& textures, const Frame& frame);

BANISHED_RENDER_IMPL_END
}  // namespace TerrainLightBackend
