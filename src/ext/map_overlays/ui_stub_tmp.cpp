// ============================================================================
// src/ext/map_overlays/ui_stub_tmp.cpp -- TEMPORARY stubs for the UI agent's build (DELETE at the merge)
// ============================================================================
// NOT RETAIL. The render agent's map-window bodies (overlay_render.cpp, MAPOVERLAYS.md §6) that
// map_window.cpp calls. This file goes when overlay_render.cpp lands.
// ============================================================================
#include "ext/map_overlays/map_overlays.h"

namespace Ext {
namespace MapOverlays {

void MapWindowUpdate(MapUI& /*map*/) {}

void MapWindowClosed(MapUI& /*map*/) {}

}  // namespace MapOverlays
}  // namespace Ext
