#pragma once
// ============================================================================
// src/ext/custom_maps/new_game_map.h -- ext.custom_maps: the New Game dialog's "Custom map" row
// ============================================================================
// NOT RETAIL. docs/extensions/handins/CUSTOMMAPS.md. While ext.custom_maps is on, one more row is added to
// the New Game dialog in use (the game's, or a mod's such as MegaMod's: the row is built from
// Dialog/NewGameMap.rsc and added when the dialog opens, the dialog's own resource is not replaced): a Load
// custom map button, a Clear button and the name of the map that was picked. The button opens the Custom map
// picker (map_picker.h) when the maps folder (<Documents>\Banished\Maps when it exists, else
// <Documents>\Banished) holds maps, else the system's file picker (Windows; other platforms say so in the
// row); the chosen file is read and checked (bmap.h Parse and Validate), its map becomes the one the next new
// game is built on (SetPending), and the game is built at the map's own size (the Map size row has no say
// then, and is greyed). With the row off nothing here runs and the game's own dialog opens.
// ============================================================================

#include <cstdint>
#include <filesystem>

class NewGameDialog;

namespace Ext {
namespace CustomMaps {

constexpr int32_t kLoadMapButtonId  = 20;   // the dialog's own buttons are 0 to 4
constexpr int32_t kClearMapButtonId = 21;

// NewGameDialog::OnOpened, after the dialog's own setup: add the row, number its buttons and show the map
// already picked, if any.
void NewGameMapOpened(NewGameDialog* dialog);

// NewGameDialog::OnElementEvent for a button press. True when the id is one of ours (the press is handled).
bool NewGameMapButton(NewGameDialog* dialog, int32_t id);

// What the Custom map picker (map_picker.h) hands back: the player chose this file (read, checked and picked
// as the system's file picker's choice is), or pressed Browse (the system's file picker, then the same).
void MapChosen(NewGameDialog* dialog, const std::filesystem::path& file);
// True when the player picked a file (it was used or refused, the row says which); false when they cancelled,
// so the picker stays open.
bool BrowseForMap(NewGameDialog* dialog);

}  // namespace CustomMaps
}  // namespace Ext
