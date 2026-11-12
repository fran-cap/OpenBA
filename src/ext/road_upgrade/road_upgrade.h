#pragma once
// ============================================================================
// src/ext/road_upgrade/road_upgrade.h -- ext.road_upgrade (class A; NOT RETAIL)
// ============================================================================
// docs/extensions/handins/ROADUPGRADE.md. A drag tool with its own button in the toolbar's roads group: drag
// a rectangle and every lower road in it (dirt, built or still queued) is ordered rebuilt as the group's best
// road (stone), through the order the stone road tool itself places (Map::PlaceRoadCell), so the builders,
// the stone it costs and a cancel with the Remove tools are the game's own.
//
// The button is the toolbar host's (src/ext/toolbar: InsertAt / ButtonTemplate / Style / Click / SyncButton,
// the EXT SEAM arms of ToolbarDialog); this header is what the host asks.
// ============================================================================

#include <cstdint>

namespace UI {
class CheckElement;
}
class Toolbar;   // Game/Toolbar.h: the retail toolbar item (global)
class ComponentDescription;   // Component/ComponentDescription.h

namespace Ext {
namespace RoadUpgrade {

// The button's element name, its tooltip's title and text (keys of Dialog/ExtensionsStringTable.rsc) and its
// icon (a sprite of Dialog/ExtensionsSprites.rsc).
constexpr const char* kButtonName = "roadUpgrade";
constexpr const char* kButtonTitle = "RoadUpgradeTitle";
constexpr const char* kButtonTip = "RoadUpgradeTip";
constexpr const char* kButtonSprite = "ToolbarRoadUpgrade";

// Where in the sorted `items` (count `n`) of toolbar group `group` the button goes, or -1 for none: the row
// is off, or the group has no road to upgrade to (road_upgrade_math.h's PickTarget). The slot is the one
// after the road tool it upgrades to. Remembers the group and that road tool's template for the calls below.
int InsertAt(const ::Toolbar* group, const ::Toolbar* const* items, int n);
// Whether `group` is the group InsertAt last gave a slot in.
bool OwnsGroup(const ::Toolbar* group);
// The template of the road tool the button upgrades to (null before InsertAt found one): the button carries
// it as its user data, so its tooltip shows that road's cost.
ComponentDescription* Target();

// The button was pressed (checked) or released: starts the tool, or drops back to the game's own tool.
void Click(UI::CheckElement* button);
// Every layout pass of the group's toolbar: the button stays pressed only while the tool is the one in use.
void SyncButton(UI::CheckElement* button);

// True from the moment the tool is installed until the player's tool changes.
bool Running();

}  // namespace RoadUpgrade
}  // namespace Ext
