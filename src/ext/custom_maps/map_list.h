#pragma once
// ============================================================================
// src/ext/custom_maps/map_list.h -- ext.custom_maps (class A): the maps in a folder
// ============================================================================
// NOT RETAIL. docs/extensions/handins/CUSTOMMAPS.md. The Custom map picker (map_picker.h) lists the .bmap
// files of the maps folder (<Documents>\Banished\Maps when it exists, else <Documents>\Banished) the way the
// Load screen lists saves: newest first, each with its thumbnail, name and date. ScanMaps reads every file in
// the folder with the loader's own checks (ReadMapFile: Parse, then Validate), keeps what the list needs and
// drops the heights, so a long list does not hold every map's terrain.
//
// Standard C++ only: tests/unit/ext_custom_maps_test.cpp links this header and map_list.cpp.
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "ext/custom_maps/bmap.h"

namespace Ext {
namespace CustomMaps {

constexpr size_t kMaxMapFile = 64u * 1024u * 1024u;   // the largest map file is about 8 MB
constexpr size_t kMaxListed  = 200;                   // the picker lists at most this many, newest first

// Reads, parses and checks a map file. On success `out` is the map; on failure `why` says what is wrong.
bool ReadMapFile(const std::filesystem::path& path, Map& out, std::string& why);

// One row of the picker.
struct ListedMap {
    std::filesystem::path path;
    std::string           name;        // the map's name, or the file's name without .bmap when it has none
    std::string           author;      // the map's author, "" when it names none
    // the names of the mods its resources came from (bmap.h ModRef), in file order; empty: it lists none
    std::vector<std::string> mods;
    uint32_t              mapSize = 0;
    int64_t               modified = 0;   // seconds since 1970 (the file's last write)
    bool                  valid = false;  // ReadMapFile accepted it
    std::string           problem;        // when not valid: what is wrong
    std::vector<uint8_t>  thumbnail;      // PNG bytes, empty = none
};

// Newest first; equal times by name, then path, so the order is the same on every run.
void SortNewestFirst(std::vector<ListedMap>& maps);

// Every *.bmap file of `dir` (not its subfolders), newest first, at most kMaxListed. A missing or unreadable
// folder gives an empty list.
std::vector<ListedMap> ScanMaps(const std::filesystem::path& dir);

// The text the picker shows for a row, in the pieces a row has room for (one short line each, in a small
// font). "512 x 512": the map's cells on a side, so a player can tell what 512 means.
std::string SizeText(uint32_t mapSize);
// "Needs: MegaMod", "Needs: A, B +2" (as many names as fit `maxBytes`, then how many more); "No mods listed"
// for a map that lists none.
std::string ModsText(const std::vector<std::string>& mods, size_t maxBytes);
// `text` cut to at most `maxBytes` bytes at a character boundary, with "..." after a cut (the result may be
// 3 bytes longer).
std::string CutText(const std::string& text, size_t maxBytes);

// The file name a map chosen in the system's file picker is copied under in the maps folder, when the picker
// handed back something that is not a path (Android's content:// URIs). The last part of `picked`, with its
// %XX escapes decoded ("content://.../document/primary%3ADownload%2FIsland%20map.bmap" -> "Island map.bmap");
// when that is not a .bmap name, `mapName` with ".bmap". A character a file name cannot hold becomes '_';
// never empty, never a hidden file, never a path.
std::string PickedFileName(const std::string& picked, const std::string& mapName);

// "11/12/2026 11:15 PM" in the local time zone, as the Load screen shows a save's date. "" for a time before
// 1970 or one the platform cannot convert.
std::string DateText(int64_t seconds);

}  // namespace CustomMaps
}  // namespace Ext
