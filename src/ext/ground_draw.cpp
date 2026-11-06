// ============================================================================
// src/ext/ground_draw.cpp -- the extensions' draws ON the ground (see ground_draw.h)
// ============================================================================
// NOT RETAIL. docs/extensions/handins/MAPOVERLAYS.md.
// ============================================================================

#include "ext/ground_draw.h"

#include "ext/map_overlays/map_overlays.h"

namespace Ext {

namespace {

// ext.map_overlays' heat map.
void Draw(Video::Context& context, const Graphics::Camera& camera) {
    if (MapOverlays::g_on)
        MapOverlays::DrawOnTerrain(context, camera);
}

}  // namespace

GroundDrawFn GroundDraw() {
    if (MapOverlays::g_on)
        return &Draw;
    return nullptr;
}

}  // namespace Ext
