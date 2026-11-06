// TEMPORARY (the views agent's build stub; the coordinator drops this file at merge). Minimal definitions of
// what overlay_view.cpp / overlay_views.cpp call but the history agent owns.
#include "ext/map_overlays/map_overlays.h"

namespace Ext {
namespace MapOverlays {

bool g_on = false;

History& Town() {
    static History h;
    return h;
}

void History::SumFlow(Flow, const std::vector<uint8_t>&, int, std::vector<float>& out) const {
    out.assign(static_cast<size_t>(mapSize_) * mapSize_, 0.0f);
}

void History::SumTraffic(int, std::vector<float>& out) const {
    out.assign(static_cast<size_t>(mapSize_) * mapSize_, 0.0f);
}

}  // namespace MapOverlays
}  // namespace Ext
