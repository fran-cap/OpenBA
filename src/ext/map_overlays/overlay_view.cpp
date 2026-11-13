// ============================================================================
// src/ext/map_overlays/overlay_view.cpp -- ext.map_overlays: the view (selection, image, legend)
// ============================================================================
// NOT RETAIL. docs/extensions/handins/MAPOVERLAYS.md §2, §5; the API is map_overlays.h.
//
// The image is the selection's grid (history views from the town's History, instant views from
// overlay_views.cpp), blurred by the scale slider, ranged and colourised. It is rebuilt lazily: at once
// after SetSelection, then at most every kRebuildFrames frames while a view shows. The frame clock is the
// game's own render-frame token (GameUpdateLoop_CurrentFrame), so nothing here runs per frame but a
// compare. `version` moves only when the pixels change, so the renderers upload only then.
// ============================================================================
#include "ext/map_overlays/map_overlays.h"
#include "ext/registry.h"   // Ext::Ext() (OnGameStart reads the lock)
#include "ext/map_overlays/overlay_math.h"
#include "ext/map_overlays/overlay_render.h"   // OnGameStartRender
#include "ext/map_overlays/overlay_views_internal.h"

#include <cstring>
#include <vector>

#include "Game/GameUpdateLoop.h"   // GameUpdateLoop_CurrentFrame
#include "Game/Map.h"

namespace Ext {
namespace MapOverlays {

namespace {

constexpr float kCitizensFullScale = 100.0f;   // the Citizens view is in percent, full colour at 100

Selection s_selection;
bool s_stale = true;
int s_lastFrame = 0;
int s_size = 0;
uint32_t s_version = 0;
float s_legend = 0.0f;
std::vector<uint32_t> s_pixels;
std::vector<float> s_grid;   // kept between rebuilds: no allocation once the size is known
std::vector<uint8_t> s_mask;
const std::vector<uint32_t> s_empty;

bool IsHistoryView(View v) {
    return v == View::Production || v == View::Consumption || v == View::Traffic;
}

int ClampMonths(int months) {
    if (months < 1) {
        return 1;
    }
    if (months > kMonths) {
        return kMonths;
    }
    return months;
}

// The selection's grid; false when there is nothing to show (no game, no map).
bool BuildGrid(const Selection& s, int& size, std::vector<float>& grid) {
    if (IsHistoryView(s.view)) {
        const History& h = Town();
        size = h.MapSize();
        if (size <= 0) {
            return false;
        }
        const int months = ClampMonths(s.months);
        if (s.view == View::Traffic) {
            const uint8_t filter = s.kind < static_cast<uint8_t>(TrafficFilter::Count) ? s.kind : 0;
            h.SumTraffic(static_cast<TrafficFilter>(filter), months, grid);
            return true;
        }
        BuildResourceMask(s, h.Resources(), s_mask);
        const Flow flow = s.view == View::Production ? Flow::Produced : Flow::Consumed;
        h.SumFlow(flow, s_mask, months, grid);
        return true;
    }
    GameUpdateLoop* g = GameUpdateLoop::_singleton;
    if (g == nullptr || g->m_game == nullptr) {
        return false;
    }
    size = g->m_game->size;
    if (size <= 0 || size > kMaxMapSize) {
        return false;
    }
    return BuildInstantTiles(s, size, grid);
}

void Rebuild() {
    int size = 0;
    const bool ok = BuildGrid(s_selection, size, s_grid);
    if (!ok || size <= 0 || s_grid.size() != static_cast<size_t>(size) * size) {
        if (s_size != 0 || !s_pixels.empty()) {
            s_pixels.clear();
            s_size = 0;
            ++s_version;
        }
        s_legend = 0.0f;
        return;
    }
    std::vector<uint32_t> pixels;
    if (s_selection.view == View::Land) {
        // Classes, not amounts: exact per tile, one flat colour each, no range.
        s_legend = 0.0f;
        ColouriseLand(s_grid, pixels);
    } else {
        Blur(s_grid, size, BlurRadiusTiles(s_selection.scale));
        const float maxValue = s_selection.view == View::Citizens ? kCitizensFullScale : RangeMax(s_grid);
        s_legend = maxValue;
        Colourise(s_grid, maxValue, pixels);
    }
    const bool same = size == s_size && pixels.size() == s_pixels.size() &&
                      std::memcmp(pixels.data(), s_pixels.data(), pixels.size() * sizeof(uint32_t)) == 0;
    if (!same) {
        s_pixels.swap(pixels);
        s_size = size;
        ++s_version;
    }
}

}  // namespace

const Selection& CurrentSelection() {
    return s_selection;
}

void SetSelection(const Selection& s) {
    s_selection = s;
    s_stale = true;
}

const std::vector<uint32_t>& Image(int& size, uint32_t& version) {
    version = s_version;
    if (!g_on || s_selection.view == View::Map) {
        size = 0;
        return s_empty;
    }
    const int frame = GameUpdateLoop_CurrentFrame();
    // A frame clock that went back is a new loop: rebuild then too.
    if (s_stale || frame - s_lastFrame >= kRebuildFrames || frame < s_lastFrame) {
        s_stale = false;
        s_lastFrame = frame;
        Rebuild();
    }
    size = s_size;
    version = s_version;
    return s_size > 0 ? s_pixels : s_empty;
}

float LegendMax() {
    if (!g_on || s_selection.view == View::Map) {
        return 0.0f;
    }
    return s_legend;
}

void OnGameStart() {
    // The lock is taken just before (GameUpdateLoop::Load): set g_on now, not at the first BeginFrame, so a map
    // window that a load reopens before that frame gets its tabs.
    g_on = ::Ext::Ext().map_overlays;
    s_selection = Selection();
    s_stale = true;
    s_lastFrame = 0;
    s_pixels.clear();
    s_grid.clear();
    s_mask.clear();
    s_size = 0;
    s_legend = 0.0f;
    ++s_version;   // the renderers drop what they uploaded for the last town
    ForgetMaterials();
    OnGameStartRender();   // the renderers' objects over the last game's Externals
}

}  // namespace MapOverlays
}  // namespace Ext
