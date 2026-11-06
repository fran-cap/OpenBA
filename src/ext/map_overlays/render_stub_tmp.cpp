// ============================================================================
// src/ext/map_overlays/render_stub_tmp.cpp -- TEMPORARY (the render agent's test driver; drop before merge)
// ============================================================================
// Stands in for the UI agent's map window controller until it lands:
//   * TmpTick (GameUpdateLoop::Update, TMP line): while the row is on, a Production view with "on terrain"
//     is selected once per town, and MapWindowUpdate runs on the open MapUI (mapui.cpp's TMP lines record
//     it and call MapWindowClosed from ~MapUI).
// Everything else (g_on, the selection, Image(), BeginFrame -> RenderFrame) is the real code.
// ============================================================================
#include "ext/map_overlays/map_overlays.h"
#include "Dialog/MapUI.h"

#include <cstdint>
#include <cstdio>

namespace Ext {
namespace MapOverlays {

MapUI* g_tmpMap = nullptr;

void TmpTick() {
    if (!g_on) return;
    if (CurrentSelection().view == View::Map) {
        Selection s = CurrentSelection();
        s.view = View::Production;
        s.onTerrain = true;
        SetSelection(s);
        std::fprintf(stderr, "[MapOverlaysTmp] selected Production, on terrain\n");
    }
    if (g_tmpMap != nullptr) MapWindowUpdate(*g_tmpMap);
}

}  // namespace MapOverlays
}  // namespace Ext
