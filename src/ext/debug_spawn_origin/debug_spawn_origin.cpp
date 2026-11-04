// ============================================================================
// src/ext/debug_spawn_origin/debug_spawn_origin.cpp -- fix.debug_spawn_origin
// ============================================================================
// NOT RETAIL. docs/retail_bugs.md §29. The seam is Game_SpawnCitizens
// (src/game/Dialog/debugui.cpp); the ring search and the land test are debug_spawn_origin_rule.h.
// ============================================================================
#include "ext/debug_spawn_origin/debug_spawn_origin.h"
#include "ext/debug_spawn_origin/debug_spawn_origin_rule.h"

#include "Game/GameUpdateLoop.h"
#include "Game/Map.h"
#include "math/Math.h"

namespace Ext {
namespace DebugSpawnOrigin {

bool OriginSpawnPoint(Math::Vector3* out) {
    Map* map = GameUpdateLoop::_singleton->m_game;
    const int size = map->size;
    const MapCell* cells = map->cells.GetData();

    // A cell a citizen can stand on and walk from: land with a zone (no water, no mountain, no
    // building footprint), nothing blocking it and nobody on it. The zones are read as the last
    // walk query left them (Map::FillZones runs at the next one); nothing on the map is written.
    const int cell = NearestCell(size, [cells](int c) -> bool {
        const MapCell& at = cells[c];
        return IsZonedLand(at.terrainType, at.zone) && at.blocker == nullptr && at.occupant == nullptr &&
               at.footprintCount == 0;
    });
    if (cell < 0) return false;

    // The cell's centre (Map::FindPlacementCell 0x14009bd40's cell-to-world step), on the terrain
    // (Map::GetTerrainPos 0x14009f3e0, flag 0, as DropOnGround's piles and MoveTo's steps).
    const float half = static_cast<float>(size >> 1) - 0.5f;
    const float xy[2] = {static_cast<float>(cell % size) - half, static_cast<float>(cell / size) - half};
    float pos[3] = {0.0f, 0.0f, 0.0f};
    map->GetTerrainPos(pos, xy, '\0', nullptr);
    *out = Math::Vector3(pos[0], pos[1], pos[2]);
    return true;
}

}  // namespace DebugSpawnOrigin
}  // namespace Ext
