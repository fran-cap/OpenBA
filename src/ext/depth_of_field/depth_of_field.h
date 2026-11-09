#pragma once
// ============================================================================
// src/ext/depth_of_field/depth_of_field.h -- ext.depth_of_field: the depth blur (NOT RETAIL)
// ============================================================================
// Class A, drawing only, live. docs/extensions/DOF.md section 3.1 (the live tier): a half-resolution
// scatter-as-gather depth of field over the finished main scene, at the scene-draw seam (src/ext/scene_draw.h;
// the pass: src/render/common/dof/dof_backend.h, DX11 and Vulkan; Metal answers "not supported").
//
// THE FOCUS is the camera's own target: the player camera orbits a point on the ground at `distance`, and
// that point is the centre of the screen, so what the player looks at is sharp at every zoom. While
// ext.eye_camera follows a citizen the frame asks the pass for the depth at the centre of the screen
// instead. ext.town_screenshot's tiles are drawn without it (each tile's camera is its own).
//
// THREADS. SceneDraw on the main thread (GameUpdateLoop::Draw step 7, through Ext::SceneDraw): the request
// and the focus. The draw on the drawing thread (the render thread under ext.render_pipe).
// ============================================================================

namespace Video { class Context; }
namespace Graphics { class Camera; }

namespace Ext {
namespace DepthOfField {

using SceneFn = void (*)(Video::Context& context, const Graphics::Camera& camera);

bool Enabled();

// The look: dof_params.h (the debug tweak overlay moves a copy).

// Main thread, once per frame: the draw to arm, or null (the row off, no game, the backend without the pass,
// a town screenshot being taken). Requests or releases the backend's pass.
SceneFn SceneDraw();

}  // namespace DepthOfField
}  // namespace Ext
