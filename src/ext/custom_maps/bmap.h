#pragma once
// ============================================================================
// src/ext/custom_maps/bmap.h -- ext.custom_maps (class A): the .bmap map file
// ============================================================================
// NOT RETAIL. docs/extensions/handins/CUSTOMMAPS.md. A custom map is one small file that holds what the
// terrain stage of world creation would otherwise generate: the height map, the water paths, and
// optionally where the resources stand and where the town starts.
//
// RETAIL. World creation builds a float height map of (gridSize + 1)^2 vertices, gridSize being twice the
// map size (the Terrain/*.rsc rows list sizes 256, 384 and 512). The playable square is the middle half.
// The game keeps the heights as int16 in 1/256 units. The water mesh follows the polylines in hf->paths
// (a point is {x, y, ..., half width}; x and y are cells with the map centre at 0, 0, so they run from
// -mapSize to +mapSize) and the underwater cells of the height map. A merchant boat sails along path 0
// only: the points before the path first enters the playable square (the middle half, -mapSize / 2 up
// to mapSize / 2), a water search through the square to the dock and back, and the points after the
// path leaves it again. A map whose path 0 does not start outside, cross the square and end outside
// has no boats.
//
// FILE (version 1, little endian):
//   "BMAP"            4 bytes
//   version           u32  (1)
//   mapSize           u32  playable cells; a multiple of 8 from 64 to 1024
//   flags             u32  bit 0: a start cell follows; bit 1: a resource list follows;
//                     bit 2: a mod list follows; bit 3: a provenance block follows
//   name, author      u16 length + bytes each (at most 64)
//   note              u16 length + bytes (at most 512)
//   heights           i16 x (2 * mapSize + 1)^2, row major, value = height * 256
//   paths             u32 count (at most 64); each: u32 point count (2 .. 4096) +
//                     {f32 x, f32 y, f32 half width} per point
//   resources         only with flag bit 1: u32 count (at most 500000); each: u16 length + template path,
//                     u16 cell x, u16 cell y, u8 variant, u8 growth
//   mods              only with flag bit 2: u16 count (at most 64); each: name and author (u16 length +
//                     bytes, at most 64), u16 template count (at most 4096), then that many u16 length +
//                     template path. The mods the map's resources came from, so the game can say which
//                     ones the map needs.
//   start             only with flag bit 0: u16 cell x, u16 cell y
//   provenance        only with flag bit 3, 112 bytes: content ID (32), origin ID (32), parent ID (32),
//                     created (i64 seconds since 1970 UTC), saved (i64). See PROVENANCE below.
//   thumbnail         u32 length (at most 262144) + PNG bytes (0 = none)
//   crc               u32  CRC-32 of every byte before it
// A cell is a playable cell, 0 .. mapSize - 1 on each axis; its height vertex is (cell + mapSize / 2).
// RESOURCE VALUES. variant is the model group of the template (a tree's look); 255 = the game chooses, as its
// own scatter does (group by noise, then a random model inside the group). growth is the growth progress
// times 250: 0 just planted, 250 fully grown; 255 = random like the game's scatter (mixed ages). Any other
// value above 250 means fully grown. The loader skips an entry whose cell is outside the play area, taken,
// claimed by a building, under water or inside the town's clear disk (the start area's radius around the
// start spot, as the game's own scatter leaves it clear).
//
// PROVENANCE. An ID is the SHA-256 of the map's content: the heights, the water paths, the resources and the
// start, and nothing else (not the name, author, note, thumbnail or mod list), so a copy that only changes
// those has the same ID. The content ID is that of this very file; the origin ID is the content ID of the
// first version of the map, kept by every editor that saves it again; the parent ID is the content ID of the
// version this one was saved from (all zeros: none); created is when the origin version was first saved (0:
// not known) and saved is when this version was. The game uses none of it except to say when the content ID
// does not match the content (the file was edited without updating it). It proves nothing by itself, since
// anyone can recompute or strip it; it makes a chain that honest editors keep and a copy with a new name easy
// to recognize. The canonical bytes ContentId hashes are written down at its declaration below; the editor
// works them out the same way.
//
// MODS. The list is information: the game never refuses a map for it. The New Game row asks the game's own
// mod list whether a mod of each listed name is enabled and names the ones that are not
// (CheckModsInstalled, ModsLine; mod_check.h).
//
// Standard C++ only: tests/unit/ext_custom_maps_test.cpp links this header and bmap.cpp.
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace Ext {
namespace CustomMaps {

constexpr uint32_t kVersion         = 1;
constexpr uint32_t kMinMapSize      = 64;
constexpr uint32_t kMaxMapSize      = 1024;
constexpr size_t   kMaxNameLength   = 64;
constexpr size_t   kMaxNoteLength   = 512;
constexpr uint32_t kMaxPaths        = 64;
constexpr uint32_t kMinPathPoints   = 2;
constexpr uint32_t kMaxPathPoints   = 4096;
constexpr uint32_t kMaxResources    = 500000;
constexpr uint32_t kMaxMods         = 64;
constexpr uint32_t kMaxModTemplates = 4096;
constexpr size_t   kMaxTemplatePath = 200;
constexpr uint32_t kMaxThumbnail    = 262144;
constexpr float    kMinHalfWidth    = 0.5f;    // cells; a channel narrower than this has no water mesh
constexpr float    kMaxHalfWidth    = 64.0f;
constexpr int16_t  kWaterLevel      = -64;     // -0.25 in 1/256 units: at or below, a cell is shore or water

// One point of a water path: world cells, the map centre at 0, 0.
struct PathPoint {
    float x         = 0.0f;
    float y         = 0.0f;
    float halfWidth = 0.0f;
};

struct Path {
    std::vector<PathPoint> points;
};

struct Resource {
    std::string templatePath;   // a resource template, e.g. "Template/NaturalResourceRock.rsc"
    uint16_t    cellX   = 0;
    uint16_t    cellY   = 0;
    uint8_t     variant = 0;
    uint8_t     growth  = 0;
};

// A mod (package) the map's resources came from: its name and author, and the templates the map uses from it.
struct ModRef {
    std::string              name;
    std::string              author;
    std::vector<std::string> templates;   // "Template/NaturalResourceRock.rsc"
};

// The provenance block (see PROVENANCE above). All IDs are SHA-256 digests.
struct Provenance {
    uint8_t contentId[32] = {};
    uint8_t originId[32]  = {};
    uint8_t parentId[32]  = {};   // all zeros: none
    int64_t created       = 0;    // seconds since 1970, UTC; 0: not known
    int64_t saved         = 0;
};

struct Map {
    uint32_t             mapSize = 0;
    std::string          name;
    std::string          author;
    std::string          note;
    std::vector<int16_t> heights;      // (2 * mapSize + 1)^2, value = height * 256
    std::vector<Path>    paths;
    bool                 hasResources = false;   // false: the game scatters its own
    std::vector<Resource> resources;
    std::vector<ModRef>  mods;                       // empty: the map does not say
    bool                 hasStart = false;       // false: the game's spiral search
    uint16_t             startX = 0;
    uint16_t             startY = 0;
    std::vector<uint8_t> thumbnail;    // PNG bytes, empty = none
    bool                 hasProvenance = false;
    Provenance           provenance;
};

enum class Status : int {
    Ok = 0,
    TooShort,        // not even a header
    BadMagic,
    BadVersion,
    BadSize,         // the map size is not allowed
    BadText,         // a name or note is too long
    Truncated,       // the data ends inside a section
    TooMany,         // a count is over its cap
    BadCrc,
    TrailingBytes,   // data after the checksum
    OutOfMemory
};

const char* StatusText(Status status);

// Parses a file image. On anything but Ok `out` is left empty. Never throws.
Status Parse(const uint8_t* data, size_t size, Map& out);

// Serializes a map (the checksum is computed here). Returns an empty vector for a map Parse would refuse
// for size or count reasons (the same caps).
std::vector<uint8_t> Write(const Map& map);

// The content ID of a map: SHA-256 over these bytes, in order, everything little endian:
//   terrain = SHA-256( "bmap-terrain-v1\0" (16 bytes), u32 mapSize, the heights as i16 )
//   id      = SHA-256( "bmap-map-v1\0" (12 bytes), terrain (32 bytes), u32 path count, per path: u32 point
//                      count and per point f32 x, f32 y, f32 half width; u32 resource count (0 when the map
//                      lists none), then the resources sorted by template path (byte order), then cell y,
//                      then cell x: u16 path length, the path bytes, u16 cell x, u16 cell y, u8 variant,
//                      u8 growth;
//                      then the start: byte 0, or byte 1 followed by u16 x and u16 y )
void ContentId(const Map& map, uint8_t out[32]);

// 64 lowercase hex digits.
std::string IdHex(const uint8_t id[32]);

// CRC-32 (the zlib polynomial) of a byte range, exposed for the tests.
uint32_t Crc32(const uint8_t* data, size_t size);

// Vertices per side and in all for a map size.
inline uint32_t GridVertices(uint32_t mapSize) {
    return 2u * mapSize + 1u;
}

inline size_t HeightCount(uint32_t mapSize) {
    const size_t side = GridVertices(mapSize);
    return side * side;
}

// What is wrong with a parsed map for the game. An Error makes the loader refuse the map; a Warning is
// shown and the map is used.
struct Issue {
    bool        error = false;
    std::string text;
};

std::vector<Issue> Validate(const Map& map);

// What the game finds of one listed mod: how many of its templates resolve.
struct ModCheck {
    std::string name;
    size_t      total   = 0;
    size_t      missing = 0;   // templates that do not resolve
};

// Runs `resolves` (the game's template lookup) on every template of every listed mod. A mod that lists no
// template is skipped.
std::vector<ModCheck> CheckMods(const Map& map, const std::function<bool(const std::string&)>& resolves);

// The same check by the mods' names, without touching a resource: `installed(mod)` says whether the game has
// a mod of that name enabled. A mod that is not counts every template it lists (at least one) missing, so
// ModsLine names it. The New Game row uses this one: looking a template up with the game's resource loader
// from the menu loads it outside a loading screen's tracking, and quitting the game then left a loaded
// resource pointing at one the cleanup had freed (a crash in the next loader pass, 2026-11-13).
std::vector<ModCheck> CheckModsInstalled(const Map& map, const std::function<bool(const ModRef&)>& installed);

// The short line for the New Game row: "" when every listed template resolves, else the mods that are missing
// (all of their templates) or partly missing, at most two named: "Needs: MegaMod", "Needs: A, B and 2 more".
std::string ModsLine(const std::vector<ModCheck>& checks);

// Whether the world's boat polyline (polyline 0: points of `strideFloats` floats from `xy`, x then y, in
// cells with the map centre at 0) is one the merchant-boat route builder (BoatComponent::SetDestination)
// can work from: at least two points and one of them inside the play area of a `mapSize`-cell map.
// Without that the builder reads a missing polyline (a map with no water path) or builds a route of no
// points and then indexes it at nearest - 2, so the game must not send a boat (the trading post checks
// this before it summons one).
bool BoatRouteExists(const float* xy, size_t count, size_t strideFloats, uint32_t mapSize);

inline bool HasError(const std::vector<Issue>& issues) {
    for (const Issue& issue : issues) {
        if (issue.error) {
            return true;
        }
    }
    return false;
}

}  // namespace CustomMaps
}  // namespace Ext
