#pragma once
// ============================================================================
// src/ext/ground_draw.h -- the extensions' draws ON the ground (Graphics::Renderer::_extGroundDraw)
// ============================================================================
// NOT RETAIL. docs/extensions/handins/MAPOVERLAYS.md, SHOWGRID.md. Renderer::Draw pass 2 calls the ground
// draw between the Opaque and the Blended lists of the main scene entry, with that entry's camera (Camera.h),
// so a picture on the ground lies under what pass 1 draws over the ground: the water, the particles and the
// status icons (which write no depth). The tool-draw seam (GameUpdateLoop::Draw step 11) runs after the whole
// scene and paints over those. Being part of the scene, the pictures are also in what the scene's other
// readers see: ext.town_screenshot's tiles (each with its own camera) and the game's screenshot (step 9).
//
// GameUpdateLoop::Draw step 7 arms the seam with GroundDraw() for its one Renderer::Draw; ext.render_pipe
// records the same value in its packet for the render thread's pass 2. Null while no ground draw is on, so
// with every row off the seam is one null test a frame.
//
// The draws, in order: ext.fancy_terrain's terrain light (a full-screen multiply from a horizon map of the
// heightmap, src/ext/fancy_terrain/fancy_terrain.h), ext.better_clouds Fancy's cloud shadows (a full-screen multiply over the opaque
// scene, BETTERCLOUDS.md section 2.6), ext.map_overlays' heat map (MapOverlays::DrawOnTerrain), ext.show_grid's tile grid
// (ShowGrid::Draw) over it. The tool (Tool::Draw, step 11) still draws after the scene, over both.
// ============================================================================

namespace Video { class Context; }
namespace Graphics { class Camera; }

namespace Ext {

using GroundDrawFn = void (*)(Video::Context& context, const Graphics::Camera& camera);

// The function to arm the seam with this frame, or null when nothing draws on the ground.
GroundDrawFn GroundDraw();

}  // namespace Ext
