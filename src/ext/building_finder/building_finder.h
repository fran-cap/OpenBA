// ============================================================================
// src/ext/building_finder/building_finder.h -- ext.building_finder: the Find Buildings page
// ============================================================================
// NOT RETAIL. docs/extensions/handins/BUILDINGFINDER.md, src/ext/ui/Dialog/BuildingFinderPage.rsc. A search
// box over every building the toolbar can place, and a favorites list the player keeps. The toolbar's own
// buttons are the "ShowGroup" items of the Toolbar resources every loaded package brings (the walk
// ToolbarDialog::OnOpened makes); the page walks the same resources, shows them all (the ones the town cannot
// build now too, with the reason), and a press does what the toolbar's button does:
// PlayerToolManager::SelectTool(template).
//
// Class A: it keeps nothing in the game. The favorites are the player's own and live in a text file beside
// settings.cfg (building_favorites.txt, one template path per line), so they follow the player between towns
// and are never in a save.
// ============================================================================
#pragma once

#include <string>

namespace UI { class UserInterface; }

namespace Ext {
namespace BuildingFinder {

constexpr const char* kId = "building_finder";
constexpr const char* kPageCaption = "FinderTab";
constexpr const char* kPageRsc = "Dialog/BuildingFinderPage.rsc:pageBuildingFinder";

// The favorites window (favorites_window.cpp): a small utility window with the player's favorite buildings,
// one button each. Opens it, or brings it to the front when it is open already.
void OpenFavoritesWindow(UI::UserInterface* ui);
// GameUpdateLoop::Update, once per frame: the hotkey (H until changed) that opens or closes the favorites
// window.
void BeginFrame(UI::UserInterface* ui);
// The hotkey button on the Buildings page: the next key pressed on the map becomes the hotkey (Esc gives up).
void BeginHotkeyCapture();
bool HotkeyCapturing();
// What the last capture came to ("Hotkey is now F5.", "H is taken by ...") once, then "" until the next one.
std::string TakeHotkeyMessage();
std::string HotkeyLabel();   // "Favorites Window Hotkey: H", or "Press a key..." while it waits

}  // namespace BuildingFinder
}  // namespace Ext
