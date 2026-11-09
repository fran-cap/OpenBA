#pragma once
// ============================================================================
// src/ext/fancy_terrain/fancy_terrain.h -- ext.fancy_terrain: the terrain shades itself (NOT RETAIL)
// ============================================================================
// Class A, drawing only, live. The terrain's self-shadowing, sky visibility and a fake bounce, from a
// horizon map of the heightmap (horizon.h), multiplied into the opaque scene at the ground-draw seam
// (src/ext/ground_draw.h; the pass: src/render/common/fancy_terrain/terrain_light_backend.h).
//
// THE MAP. Built from WorldGeometry's heightmap the first frame a world draws with the row on. Terrain edits
// need no hook: every heightmap writer bumps StillTerrain's epoch (quarries, terraforming, roads, the terrain
// tool, a load), so when the epoch moves the heightmap is compared with the copy the map was built from, and
// only the changed vertices' rectangle, grown by the search radius, is rebuilt and uploaded.
//
// THREADS. TerrainDraw on the main thread (GameUpdateLoop::Draw step 7, through Ext::GroundDraw): the map's
// build and rebuilds, the sun. The draw on the drawing thread (the render thread under ext.render_pipe),
// under the map's lock, so it never sees a half-built rectangle.
// ============================================================================

namespace Video { class Context; }
namespace Graphics { class Camera; }

namespace Ext {
namespace FancyTerrain {

using GroundFn = void (*)(Video::Context& context, const Graphics::Camera& camera);

bool Enabled();

// Main thread, once per frame the ground draw is asked for: the draw to arm, or null (the row off, no
// world, the backend without the pass). Keeps the map current.
GroundFn TerrainDraw();

}  // namespace FancyTerrain
}  // namespace Ext
