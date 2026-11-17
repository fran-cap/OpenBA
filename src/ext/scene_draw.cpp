// ============================================================================
// src/ext/scene_draw.cpp -- the extensions' draws over the finished scene (see scene_draw.h)
// ============================================================================
// NOT RETAIL. docs/extensions/DOF.md, docs/extensions/handins/MAPEDGE.md.
// ============================================================================

#include "ext/scene_draw.h"

#include "ext/depth_of_field/depth_of_field.h"
#include "ext/map_edge_fade/map_edge_fade.h"

#include <atomic>

namespace Ext {

namespace {

// The two draws while both are armed (main thread, SceneDraw; read by the drawing thread). Each extension
// hands out one function for its whole life, so the values never change once set.
std::atomic<SceneDrawFn> g_edge{nullptr};
std::atomic<SceneDrawFn> g_blur{nullptr};

// Both armed, in the frame's order: the rim fade, then the blur over it. The seam and ext.render_pipe's
// packet carry this one pointer; each draw's own backend ignores a call made after its request was
// withdrawn (a frame the render thread still holds).
void EdgeThenBlur(Video::Context& context, const Graphics::Camera& camera) {
    const SceneDrawFn edge = g_edge.load(std::memory_order_acquire);
    if (edge != nullptr) edge(context, camera);
    const SceneDrawFn blur = g_blur.load(std::memory_order_acquire);
    if (blur != nullptr) blur(context, camera);
}

}  // namespace

SceneDrawFn SceneDraw() {
    const SceneDrawFn edge = MapEdgeFade::SceneDraw();
    const SceneDrawFn blur = DepthOfField::SceneDraw();
    if (edge == nullptr) return blur;
    if (blur == nullptr) return edge;
    g_edge.store(edge, std::memory_order_release);
    g_blur.store(blur, std::memory_order_release);
    return &EdgeThenBlur;
}

}  // namespace Ext
