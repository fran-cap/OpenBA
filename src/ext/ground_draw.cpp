// ============================================================================
// src/ext/ground_draw.cpp -- the extensions' draws ON the ground (see ground_draw.h)
// ============================================================================
// NOT RETAIL. docs/extensions/handins/MAPOVERLAYS.md, SHOWGRID.md.
// ============================================================================

#include "ext/ground_draw.h"

#include "ext/map_overlays/map_overlays.h"
#include "ext/registry.h"
#include "ext/show_grid/show_grid.h"

namespace Ext {

namespace {

bool GridShown() {
    return ::Ext::Ext().show_grid && ShowGrid::Shown();
}

// ext.map_overlays' heat map, then ext.show_grid's tile grid over it.
void Draw(Video::Context& context, const Graphics::Camera& camera) {
    if (MapOverlays::g_on)
        MapOverlays::DrawOnTerrain(context, camera);
    if (GridShown())
        ShowGrid::Draw(&context, camera);
}

}  // namespace

GroundDrawFn GroundDraw() {
    if (MapOverlays::g_on || GridShown())
        return &Draw;
    return nullptr;
}

}  // namespace Ext
