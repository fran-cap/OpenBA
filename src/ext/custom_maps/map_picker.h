#pragma once
// ============================================================================
// src/ext/custom_maps/map_picker.h -- ext.custom_maps: the Custom map picker (NOT RETAIL)
// ============================================================================
// docs/extensions/handins/CUSTOMMAPS.md. The New Game dialog's Load custom map button opens this dialog when
// the maps folder (<Documents>\Banished\Maps when it exists, else <Documents>\Banished) holds .bmap files:
// the game's Load screen for maps. A grid of thumbnails, each with the map's name, author, size ("512 x
// 512"), the file's date and the mods it lists, newest first; click one, then Load (or Cancel). Choosing a
// map hands its file back to the New Game row (MapChosen, new_game_map.h), which reads and checks it as it
// did for the system's file picker.
//
// THE RESOURCE IS OURS: Dialog/CustomMapLoad.rsc (src/ext/ui/Dialog/), the game's SaveLoad.rsc objects taken
// by reference with our title and no Save / Delete. Not a reflected type (no factory registration): it is
// only built by OpenMapPicker, the way ExtensionsDialog is. The thumbnails are the files' PNGs (png.h)
// scaled to the size of the Load screen's own thumbnails (FitCover), turned into game images the way
// ext.winter_ice makes its textures, and shown through the row's own image element. A map without a
// thumbnail keeps the Load screen's empty-slot picture; a file the loader refuses shows the Load screen's
// "corrupt" overlay and cannot be loaded.
// ============================================================================

#include <vector>

#include "ext/custom_maps/map_list.h"

class NewGameDialog;

namespace Ext {
namespace CustomMaps {

// Opens the picker over the New Game dialog `newGame` with `maps` (map_list.h ScanMaps order). False when it
// cannot be built (the resource did not load, nothing to show): nothing was opened and the caller falls back
// to the system's file picker.
bool OpenMapPicker(NewGameDialog* newGame, std::vector<ListedMap> maps);

}  // namespace CustomMaps
}  // namespace Ext
