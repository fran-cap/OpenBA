#pragma once
// ============================================================================
// src/game/Game/PathType.h — the map cell type and the sets of cell types
// ============================================================================
//
// PathType is MapCell::terrainType (+0x29): what kind of ground a cell is.
// Retail's own names: the kit's profile build (ToolKit/bin/x64/
// Application-x64-profile.exe, NOT the retail exe) carries a {u64 value,
// char* name} reflection table for PathType at 0x140211550, and the kit's
// templates spell the type (`PathType _pathType = Immovable;` in
// ToolKit/resource/Template/*.rsc). The stored values put Unusable at 6 and
// Water / DeepWater at 7 / 8, though the table lists the names in another
// order.
//
// PathBits is a set of PathTypes: bit N is PathType N (the same profile build's
// table at 0x140211620, {1 << n, name}; the templates use it as `PathBits
// _placeBits`, `_allowedPathing`, `_pathBits`, ...). Every reader forms a cell's
// bit as 1u << (terrainType & 0x1f) (x86 SHL masks its count to 5 bits) and
// tests it against a mask: PathBitOf spells that once. The composite masks
// below (Walk, Zoneless, Access, Boat, the road sets) carry no retail name; the
// names come from what their users do with them.
//
// Standalone (<cstdint> and EnumFlags.h only) so the unit-linked ext rule
// headers can use it without the world. Map.h includes it.
// ============================================================================

#include <cstdint>
#include "system/EnumFlags.h"

enum class PathType : uint8_t {
    Faster     = 0,    // stone road (StoneRoad.rsc)
    Fast       = 1,    // dirt road (DirtRoad.rsc)
    Normal     = 2,    // open buildable ground; ClassifyTerrain's flat case (0x14009e2c0)
    Occasional = 3,    // field / orchard / pasture / storage-yard ground
    Obstacle   = 4,    // a cell holding a natural resource or pile (tree, rock, ore)
    Immovable  = 5,    // building footprint, no walk; the quarry pit writes it
    Unusable   = 6,    // high mountain (ClassifyTerrain >= 2.0)
    Water      = 7,    // shallow water / shore (ClassifyTerrain < -0.25)
    DeepWater  = 8,    // deep water (ClassifyTerrain < -1.4)
    Bridge     = 9,    // bridge deck: walkers cross, boats pass (WoodBridge*.rsc)
    Walkable   = 10,   // steep ground: walkable, not buildable or roadable
    Tunnel     = 11,   // tunnel floor (Tunnel1..8.rsc)
};

enum class PathBits : uint32_t {
    Faster     = 0x001,   // 1 << PathType::Faster
    Fast       = 0x002,   // 1 << PathType::Fast
    Normal     = 0x004,   // 1 << PathType::Normal
    Occasional = 0x008,   // 1 << PathType::Occasional
    Obstacle   = 0x010,   // 1 << PathType::Obstacle
    Immovable  = 0x020,   // 1 << PathType::Immovable
    Unusable   = 0x040,   // 1 << PathType::Unusable
    Water      = 0x080,   // 1 << PathType::Water
    DeepWater  = 0x100,   // 1 << PathType::DeepWater
    Bridge     = 0x200,   // 1 << PathType::Bridge
    Walkable   = 0x400,   // 1 << PathType::Walkable
    Tunnel     = 0x800,   // 1 << PathType::Tunnel

    // Faster | Fast: a road cell (IsGroundClear's road test, the road-cell count fix-up).
    Road       = 0x003,
    // retail 0xe1f = Faster|Fast|Normal|Occasional|Obstacle|Bridge|Walkable|Tunnel: the
    // types a citizen walks on. FillZone (0x14009c170) zones them and the region flood
    // spreads over them; MoveTo's citizen pass flags, the ValidateCell / FindCell /
    // RequestPath reachability set, PopulationDescription's default habitat.
    Walk       = 0xe1f,
    // retail 0x1e0 = Immovable|Unusable|Water|DeepWater: the types FillZone never zones (the
    // complement of Walk). An endpoint (or the flags) carrying one skips RequestPath's and
    // ValidateCell's zone-equality test, and SetTerrainType flips the zones stale across it.
    Zoneless   = 0x1e0,
    // retail 0xe07 = Walk without Occasional and Obstacle: an open cell a citizen stands on to
    // reach a target (FindCell's access-cell search, the fire water-source search, the
    // immigrant arrival cell).
    Access     = 0xe07,
    // retail 0x380 = Water|DeepWater|Bridge: the cells a trader boat sails
    // (BoatRoute_FindWaterPath's query, the start-town dock flood).
    Boat       = 0x380,
    // retail 0xf9f = Walk|Water|DeepWater: an animal's MoveTo pass flags (animals cross water).
    AnimalWalk = 0xf9f,
    // retail 0xa17 = Faster|Fast|Normal|Obstacle|Bridge|Tunnel: the road A*'s strict pass set
    // (cells a road may be drawn over), also the road preview's green / red test.
    RoadRoute  = 0xa17,
    // retail 0xff7 = All without Occasional: the road tool's fallback search.
    RoadRouteRelaxed = 0xff7,
    // retail 0x5e0 = Immovable|Unusable|Water|DeepWater|Walkable: a road endpoint carrying any
    // of these refuses the road.
    NoRoad     = 0x5e0,
    // retail 0xfff: every type (Walk | Zoneless).
    All        = 0xfff,
};
OPENBA_ENUM_FLAGS(PathBits)

// A cell type's bit: exactly retail's 1u << (type & 0x1f). For a bit that is
// stored or combined before it is tested (ValidateCell's typeSrc | typeDest).
constexpr PathBits PathBitOf(PathType t) noexcept {
    return static_cast<PathBits>(1u << (static_cast<uint8_t>(t) & 0x1f));
}

// Is type t in set? Retail's one-expression test `(set & (1u << (t & 0x1f))) != 0`,
// spelled in ONE function on purpose: the compiler folds that whole expression
// into a shift-and-test ((set >> t) & 1), and it only does so when it sees the
// shift and the AND together. Any(set & PathBitOf(t)) is the same value but
// compiles to different instructions; use HasType wherever the original tested
// the cell's bit in the same expression.
constexpr bool HasType(PathBits set, PathType t) noexcept {
    return (static_cast<uint32_t>(set) & (1u << (static_cast<uint8_t>(t) & 0x1f))) != 0;
}
