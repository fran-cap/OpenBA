// ============================================================================
// src/ext/ground_draw.cpp -- the extensions' draws ON the ground (see ground_draw.h)
// ============================================================================
// NOT RETAIL. docs/extensions/handins/MAPOVERLAYS.md, SHOWGRID.md.
// ============================================================================

#include "ext/ground_draw.h"

#include "ext/better_clouds/better_clouds.h"
#include "ext/fancy_terrain/fancy_terrain.h"
#include "ext/map_overlays/map_overlays.h"
#include "ext/registry.h"
#include "ext/show_grid/show_grid.h"

#include <atomic>

namespace Ext {

namespace {

bool GridShown() {
    return ::Ext::Ext().show_grid && ShowGrid::Shown();
}

// The clouds' shadows armed for this frame (main thread, GroundDraw; read by Draw on the drawing thread
// of the frame; with ext.render_pipe the render thread may still be drawing the previous frame while the
// main thread arms the next, hence the atomic: either value is a whole frame's answer).
std::atomic<BetterClouds::GroundFn> g_shadows{nullptr};
// ext.fancy_terrain's terrain light, armed the same way.
std::atomic<FancyTerrain::GroundFn> g_terrain{nullptr};

// ext.fancy_terrain's terrain light and ext.better_clouds' shadows (they darken the ground, not the pictures
// on it), ext.map_overlays' heat map, then ext.show_grid's tile grid over it.
void Draw(Video::Context& context, const Graphics::Camera& camera) {
    if (const FancyTerrain::GroundFn terrain = g_terrain.load(std::memory_order_acquire))
        terrain(context, camera);
    if (const BetterClouds::GroundFn shadows = g_shadows.load(std::memory_order_acquire))
        shadows(context, camera);
    if (MapOverlays::g_on)
        MapOverlays::DrawOnTerrain(context, camera);
    if (GridShown())
        ShowGrid::Draw(&context, camera);
}

}  // namespace

GroundDrawFn GroundDraw() {
    const BetterClouds::GroundFn shadows = BetterClouds::ShadowDraw();
    g_shadows.store(shadows, std::memory_order_release);
    const FancyTerrain::GroundFn terrain = FancyTerrain::TerrainDraw();
    g_terrain.store(terrain, std::memory_order_release);
    if (shadows == nullptr && terrain == nullptr && !MapOverlays::g_on && !GridShown())
        return nullptr;
    return &Draw;
}

}  // namespace Ext
