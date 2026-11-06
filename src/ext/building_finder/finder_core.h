// ============================================================================
// src/ext/building_finder/finder_core.h -- what the page and the favorites window share (NOT RETAIL)
// ============================================================================
// docs/extensions/handins/BUILDINGFINDER.md. The list of buildings the toolbar can place, the player's
// favorites file, the town's count of each building, and the rows of a list. The two users (toolbar_page.cpp:
// the page in the extension window, favorites_window.cpp: the small window) ask for the buildings with
// AcquireItems and give them back with ReleaseItems; the template pointers belong to the game that is
// running, so they are dropped when neither is open.
// ============================================================================
#pragma once

#include "ui/Element.h"
#include "object/Internal.h"

#include <cstdint>
#include <string>
#include <vector>

struct ComponentDescription;
namespace UI {
class Dialog;
class SpriteSheet;
class UserInterface;
}

namespace Ext {
namespace BuildingFinder {

struct Item {
    std::string key;                 // the template's path: the favorites file names a building by it
    std::string name;                // as the toolbar's tooltip shows it
    // the toolbar group it sits in ("Housing"); every one when a mod lists it in several
    std::string group;
    // what the search looks in: name and the whole group chain of every place it is
    std::string haystack;
    ComponentDescription* tmpl = nullptr;
    int32_t limit = -1;              // the ToolbarDescription's limit; negative = none
    UI::SpriteSheet* sheet = nullptr;   // the toolbar button's icon: the sheet it is on ...
    std::string sprite;                 // ... and its name there ("" = no icon)
    std::string tip;                    // the toolbar button's tooltip text, the building's description
};

// One row of a list: the cell and the parts the controller sets.
struct Cell {
    Object::InternalPtr<UI::Element> root;
    Object::InternalPtr<UI::Element> pick;
    Object::InternalPtr<UI::Element> pickText;
    Object::InternalPtr<UI::Element> star;
    Object::InternalPtr<UI::Element> starText;
};

// ---- the buildings ----
void AcquireItems();                       // walks the toolbar resources when it is the first user
void ReleaseItems();                       // drops them when it was the last
const std::vector<Item>& Items();          // every building, by name (empty when nobody holds them)

// ---- the favorites (building_favorites.txt, loaded on first use) ----
const std::vector<std::string>& Favorites();
// Adds the building to the favorites, or takes it out, and writes the file. Returns whether it is a favorite
// now; `saved` says whether the file could be written.
bool ToggleFavoriteAndSave(const Item& it, bool& saved);
// Empties the favorites and writes the file (`saved` as above).
void ClearFavoritesAndSave(bool& saved);
// Counts up on every change, so a list that shows the favorites knows when to build again.
uint32_t FavoritesVersion();
// The item index of each favorite this game has, in the order they were added; `missing` = the ones it has
// not.
std::vector<int> FavoriteItems(int& missing, int maxRows);

// ---- the hotkey (settings.cfg: ext.building_finder_hotkey; H until the player changes it) ----
int Hotkey();                              // a Win32 virtual key code
// remembers it and writes settings.cfg; false when the settings are not available
bool SetHotkey(int vk);

// ---- the town ----
int BuiltOf(const Item& it);
// The text of a row: the name, the group (when asked), and the limit note. `compact` (the small window): the
// name cut to 30 characters and a short note, so no row makes the window wider.
std::string RowText(const Item& it, bool withGroup, bool compact = false);
// True while the building is the tool the player has selected (false once placing ends: placed, cancelled,
// Esc).
bool IsPlacing(const Item& it);
// Starts placing the building, as the toolbar's button does. Returns what to tell the player.
std::string StartPlacing(const Item& it);

// ---- a list ----
struct FillSpec {
    const UI::Dialog* dialog = nullptr;   // the dialog the table is in (the cells are made for it)
    int32_t pickId0 = 0;                  // element id of row k's name button: pickId0 + k
    int32_t starId0 = 0;                  // and of its favorite button
    bool withGroup = false;               // show "[group]" after the name
    bool isFavorite = false;              // every row is a favorite: the button reads "-"
    bool compact = false;                 // RowText's compact form
};
// Fills `table` with one row per entry of `rows` (item indices). Returns the cells made.
std::vector<Cell> FillTable(UI::Element* table, const std::vector<int>& rows, const FillSpec& spec);

// Scrolls the list the table is in back to its top (a list that got shorter would show nothing below its
// end).
void ScrollToTop(UI::Element* table);

// The favorites window's grid: one picture button per entry of `rows`, the building's own toolbar icon, with
// the toolbar's hover tooltip (the name, the description and the limit note). `pickId0` + k is button k's
// element id. As many columns as the table's width allows. A building at its limit is drawn dim (a disabled
// button shows no tooltip).
struct IconSpec {
    const UI::Dialog* dialog = nullptr;
    int32_t pickId0 = 0;
};
std::vector<Cell> FillIconTable(UI::Element* table, const std::vector<int>& rows, const IconSpec& spec);

// The UserInterface an element of an open dialog belongs to (null when it is not in one).
UI::UserInterface* UiOfElement(UI::Element* e);

}  // namespace BuildingFinder
}  // namespace Ext
