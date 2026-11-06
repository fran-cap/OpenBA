// ============================================================================
// src/ext/map_overlays/ui_stub_tmp.cpp -- TEMPORARY stubs for the UI agent's build (DELETE at the merge)
// ============================================================================
// NOT RETAIL. The bodies of map_overlays.h / overlay_math.h that map_window.cpp calls, owned by the history,
// views and render agents (MAPOVERLAYS.md §6). This file goes when theirs land.
// ============================================================================
#include "ext/map_overlays/map_overlays.h"
#include "ext/map_overlays/overlay_math.h"

#include <algorithm>
#include <cstdio>

namespace Ext {
namespace MapOverlays {

bool g_on = false;   // TMP: flip to true to see the tabs in game

namespace {
Selection g_selection;
}  // namespace

const Selection& CurrentSelection() {
    return g_selection;
}

void SetSelection(const Selection& s) {
    g_selection = s;
    std::fprintf(stderr, "[MapOverlays TMP] selection view=%d flags=0x%x path='%s' kind=%d months=%d scale=%.2f "
                 "terrain=%d\n", static_cast<int>(s.view), s.categoryFlags, s.resourcePath.c_str(),
                 static_cast<int>(s.kind), s.months, static_cast<double>(s.scale), s.onTerrain ? 1 : 0);
}

float LegendMax() {
    return g_selection.view == View::Map ? 0.0f : 42.0f;
}

void MapWindowUpdate(MapUI& /*map*/) {}

void MapWindowClosed(MapUI& /*map*/) {
    std::fprintf(stderr, "[MapOverlays TMP] MapWindowClosed\n");
}

uint32_t Ramp(float t) {
    t = std::max(0.0f, std::min(1.0f, t));
    const uint32_t r = static_cast<uint32_t>(255.0f * t);
    const uint32_t b = 255u - r;
    const uint32_t a = static_cast<uint32_t>(255.0f * t);
    return (a << 24) | (b << 16) | (64u << 8) | r;
}

}  // namespace MapOverlays
}  // namespace Ext
