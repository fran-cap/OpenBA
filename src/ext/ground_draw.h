#pragma once
// ============================================================================
// src/ext/ground_draw.h -- the extensions' draws ON the ground (Graphics::Renderer::_extGroundDraw)
// ============================================================================
// NOT RETAIL. docs/extensions/handins/MAPOVERLAYS.md. Renderer::Draw pass 2 calls the ground draw between the
// Opaque and the Blended lists of the main scene entry, with that entry's camera (Camera.h), so a picture on
// the ground lies under what pass 1 draws over the ground: the water, the particles and the status icons. The
// tool-draw seam (GameUpdateLoop::Draw step 11) runs after the whole scene and paints over those.
//
// GameUpdateLoop::Draw step 7 arms the seam with GroundDraw() for its one Renderer::Draw; ext.render_pipe
// records the same value in its packet for the render thread's pass 2. Null while no ground draw is on, so
// with every row off the seam is one null test a frame.
// ============================================================================

namespace Video { class Context; }
namespace Graphics { class Camera; }

namespace Ext {

using GroundDrawFn = void (*)(Video::Context& context, const Graphics::Camera& camera);

// The function to arm the seam with this frame, or null when nothing draws on the ground.
GroundDrawFn GroundDraw();

}  // namespace Ext
