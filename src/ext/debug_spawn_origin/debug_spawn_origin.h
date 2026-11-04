// ============================================================================
// src/ext/debug_spawn_origin/debug_spawn_origin.h -- fix.debug_spawn_origin (fix tier, default ON)
// ============================================================================
// NOT RETAIL. docs/retail_bugs.md §29.
//
// The debug panel's "spawn citizens" button (Game_SpawnCitizens 0x140060380,
// src/game/Dialog/debugui.cpp) places its four citizens on the use nodes of the town's storage
// buildings and repeats its pass over the storage list until four are placed; a town whose
// storages have no use node hangs in that loop. With the row on, a pass that places nobody places
// the rest at the point OriginSpawnPoint gives and ends the press. The citizens themselves are made
// exactly as on the use-node path (the same template, gender, age, Initialize and RegisterEntity).
// ============================================================================
#pragma once

namespace Math {
class Vector3;
}

namespace Ext {
namespace DebugSpawnOrigin {

// The world position of the free land cell nearest the world origin (the map centre), on the
// terrain: the centre of the cell NearestCell picks among the zoned land cells with no blocker,
// no occupant and no footprint. False (and `out` untouched) when the map has no such cell.
bool OriginSpawnPoint(Math::Vector3* out);

}  // namespace DebugSpawnOrigin
}  // namespace Ext
