#pragma once
// ============================================================================
// src/ext/map_overlays/overlay_views_internal.h -- ext.map_overlays: what overlay_view.cpp takes from
// overlay_views.cpp (the views agent's two files; not part of the public contract)
// ============================================================================
// NOT RETAIL. docs/extensions/handins/MAPOVERLAYS.md §2.
// ============================================================================
#include <cstdint>
#include <string>
#include <vector>

#include "ext/map_overlays/overlay_core.h"

namespace Ext {
namespace MapOverlays {

// The history views' resource mask over `resources` (History::Resources), one byte per table entry: 1 when
// the entry is the selection's resourcePath (case-insensitive), or, with no path, when the loaded material
// of that path has any of the selection's categoryFlags. Empty (every resource) when the selection names
// neither. A path the running game does not load matches only by its name.
void BuildResourceMask(const Selection& s, const std::vector<std::string>& resources, std::vector<uint8_t>& mask);

// Drops the material caches (a new town may come with another mod list).
void ForgetMaterials();

}  // namespace MapOverlays
}  // namespace Ext
