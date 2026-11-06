#pragma once
// ============================================================================
// src/ext/map_overlays/map_window.h -- ext.map_overlays: the overlay tabs in retail's map window
// ============================================================================
// NOT RETAIL. docs/extensions/handins/MAPOVERLAYS.md §1, §2; src/ext/ui/Dialog/MapOverlays.rsc. The one seam
// is in ToolUI::OnCreated (src/game/Dialog/toolui.cpp), after the UIDescription's controllers are added and
// only when Ext::MapOverlays::g_on is set.
// ============================================================================

namespace Object { class Base; }
namespace UI { class DialogInstance; }

namespace Ext {
namespace MapOverlays {

// A StandardDialog-based utility window was built. When it holds a MapUI (the UtilityMap tool's window), the
// overlay views are injected beside retail's map content the way the UIDescription injects that content: one
// element (Dialog/MapOverlays.rsc:ribbonMapOverlays) in the same group (userGroup0), the map content moved into
// its first cell, and a controller of ours that the dialog owns and deletes. Any other window is left as it is.
void AddMapWindowTabs(UI::DialogInstance* dialog, Object::Base* dataSource);

}  // namespace MapOverlays
}  // namespace Ext
