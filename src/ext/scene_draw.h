#pragma once
// ============================================================================
// src/ext/scene_draw.h -- the extensions' draws OVER the finished scene (Graphics::Renderer::_extSceneDraw)
// ============================================================================
// NOT RETAIL. docs/extensions/DOF.md. Renderer::Draw pass 2 calls the scene draw after the last of the main
// scene entry's three lists, with that entry's camera and the main colour and depth bound: everything the
// scene draws is in the picture (the water, the particles, the status icons), the tool (GameUpdateLoop::Draw
// step 11) and the UI (step 12) are not yet. The ground draw (ground_draw.h) is the earlier seam, between the
// Opaque and Blended lists.
//
// GameUpdateLoop::Draw step 7 arms the seam with SceneDraw() for its one Renderer::Draw; ext.render_pipe
// records the same value in its packet for the render thread's pass 2. Null while no scene draw is on, so
// with every row off the seam is one null test a frame.
//
// The draws: ext.depth_of_field's depth blur (src/ext/depth_of_field/depth_of_field.h). How several blurs
// combine here (DOF.md section 5.1) is not ruled yet; there is one.
// ============================================================================

namespace Video { class Context; }
namespace Graphics { class Camera; }

namespace Ext {

using SceneDrawFn = void (*)(Video::Context& context, const Graphics::Camera& camera);

// Main thread, once per frame: the function to arm the seam with, or null when nothing draws over the scene.
SceneDrawFn SceneDraw();

}  // namespace Ext
