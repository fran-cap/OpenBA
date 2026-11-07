// ============================================================================
// src/ext/debug_spawn_origin/debug_spawn_origin_rule.h -- fix.debug_spawn_origin: the pure rules
// ============================================================================
// NOT RETAIL. docs/retail_bugs.md §29. Standard C++ only, so
// tests/unit/ext_debug_spawn_origin_test.cpp links it without the world.
//
// The debug panel's "spawn citizens" button (DebugUI id 326, Game_SpawnCitizens 0x140060380)
// places four new citizens on the use nodes of the town's storage buildings, repeating its pass
// over the storage list until four are placed. A town whose storages have no use node (only
// stockpiles) places nobody on a pass, and the pass repeats for ever: the game hangs. With the
// row on, a pass that places nobody ends the loop by placing the rest at the free land cell
// nearest the world origin.
//
// The world origin is the map centre: a world position (x, y) lies on cell
// (x + size / 2, y + size / 2) (DropOffRawMaterial::DropOnGround 0x1400c78c0), and a cell's
// centre is at (cell - size / 2 + 0.5) on each axis (Map::FindPlacementCell 0x14009bd40).
// ============================================================================
#pragma once

#include <cstdint>

#include "Game/PathType.h"   // PathType / PathBits (standalone: <cstdint> and EnumFlags.h only)

namespace Ext {
namespace DebugSpawnOrigin {

// The terrain types Map::FillZone (0x14009c170) gives a zone: the land a citizen walks on.
constexpr PathBits kLandTypes = PathBits::Walk;

// The citizens one press places (Game_SpawnCitizens' loop bound).
constexpr int kSpawnCount = 4;

// A land cell with a zone, where a citizen can stand and walk from.
inline bool IsZonedLand(PathType type, uint16_t zone) {
    return ((1u << (static_cast<uint8_t>(type) & 0x1f)) & static_cast<uint32_t>(kLandTypes)) != 0 &&
           zone != 0;
}

// Whether the pass just made over the storage list ends the press at the origin: the row is on
// and the pass placed nobody (no storage has a use node), so retail would loop for ever.
inline bool PlaceAtOrigin(bool fixOn, int placedThisPass) {
    return fixOn && placedThisPass == 0;
}

// The cell holding the world origin on a size x size map.
inline int OriginCell(int size) {
    return (size / 2) * size + size / 2;
}

// The cell nearest the origin cell that `standable(cell)` accepts, or -1 when none on the map.
// Searched in square rings around the origin cell (ring r = Chebyshev distance r); within the
// first ring holding a match, the cell of least squared distance, the first in row order on a tie.
template <class Standable>
int NearestCell(int size, Standable standable) {
    if (size <= 0) return -1;
    const int cx = size / 2;
    const int cy = size / 2;
    for (int r = 0; r <= size; r++) {
        int best = -1;
        int bestDist = 0;
        for (int y = cy - r; y <= cy + r; y++) {
            if (y < 0 || y >= size) continue;
            const bool edgeRow = (y == cy - r || y == cy + r);
            const int step = edgeRow ? 1 : 2 * r;
            for (int x = cx - r; x <= cx + r; x += (step > 0 ? step : 1)) {
                if (x < 0 || x >= size) continue;
                const int cell = y * size + x;
                if (!standable(cell)) continue;
                const int dist = (x - cx) * (x - cx) + (y - cy) * (y - cy);
                if (best < 0 || dist < bestDist) {
                    best = cell;
                    bestDist = dist;
                }
            }
        }
        if (best >= 0) return best;
    }
    return -1;
}

}  // namespace DebugSpawnOrigin
}  // namespace Ext
