#pragma once
// ============================================================================
// src/ext/map_edge_fade/map_edge_fade.h -- ext.map_edge_fade: the map's rim fades to black (NOT RETAIL)
// ============================================================================
// Class A, drawing only, live. docs/extensions/handins/MAPEDGE.md. Retail draws the terrain (the playable
// map and the border of bare ground around it) up to its last row and stops: the cut edge of the ground and
// whatever lies behind it show when the camera is near the rim. With the row on, a full-screen pass over the
// finished main scene darkens the border's last tiles toward black and blacks out everything past the
// terrain's edge; the playable tiles are not touched (the scene-draw seam, src/ext/scene_draw.h; the pass:
// src/render/common/map_edge/map_edge_backend.h, DX11 and Vulkan; Metal answers "not supported"). The look
// and its arithmetic: edge_math.h. ext.town_screenshot's tiles are drawn without it.
//
// THREADS. SceneDraw on the main thread (GameUpdateLoop::Draw step 7, through Ext::SceneDraw): the request
// and the terrain's size. The draw on the drawing thread (the render thread under ext.render_pipe).
// ============================================================================

namespace Video { class Context; }
namespace Graphics { class Camera; }

namespace Ext {
namespace MapEdgeFade {

using SceneFn = void (*)(Video::Context& context, const Graphics::Camera& camera);

// Main thread, once per frame: the draw to arm, or null (the row off, no game, the backend without the pass,
// a town screenshot being taken). Requests or releases the backend's pass.
SceneFn SceneDraw();

}  // namespace MapEdgeFade
}  // namespace Ext
