// ============================================================================
// src/ext/terrain_resort/terrain_resort.cpp -- fix.terrain_resort: the map walk
// ============================================================================
// NOT RETAIL. terrain_resort.h has the contract, terrain_resort_rule.h the rule;
// docs/extensions/handins/TERRAINRESORT.md the evidence.
// ============================================================================
#include "ext/terrain_resort/terrain_resort.h"
#include "ext/terrain_resort/terrain_resort_rule.h"
#include "Entity/ComponentEntity.h"   // InternalPtr<ComponentEntity>::Get needs the class complete
#include "Game/Map.h"
#include "err_line.h"

namespace Ext {
namespace TerrainResort {

namespace {

static_assert(kNoRoad == MapCell::kNoRoadBatch, "the rule's road byte is MapCell's no-road value");

bool CellShouldHeal(Map* map, int index) {
    const MapCell& cell = map->cells.GetData()[index];
    // Most cells: skip the terrain query.
    if (static_cast<uint8_t>(cell.terrainType) != kTypeSteep) return false;
    CellView view;
    view.terrainType    = static_cast<uint8_t>(cell.terrainType);
    view.footprintCount = cell.footprintCount;
    view.road           = cell.field_0x2A;
    view.blocked        = cell.blocker.Get() != nullptr;
    view.occupied       = cell.occupant.Get() != nullptr;
    return ShouldHeal(view, map->ClassifyTerrain(index));   // 0x14009e2c0
}

}  // namespace

void HealAround(Map* map, int x0, int y0, int x1, int y1) {
    const int size = map->size;
    const int left   = x0 - 1 < 0 ? 0 : x0 - 1;
    const int top    = y0 - 1 < 0 ? 0 : y0 - 1;
    const int right  = x1 + 1 >= size ? size - 1 : x1 + 1;
    const int bottom = y1 + 1 >= size ? size - 1 : y1 + 1;
    for (int y = top; y <= bottom; ++y) {
        for (int x = left; x <= right; ++x) {
            const int index = y * size + x;
            if (CellShouldHeal(map, index)) {
                map->SetTerrainType(index, kTypeBuildable);   // 0x14009f2f0
            }
        }
    }
}

int HealAllOnLoad(Map* map) {
    const int count = map->size * map->size;
    int healed = 0;
    for (int index = 0; index < count; ++index) {
        if (CellShouldHeal(map, index)) {
            // 2 and 10 are both in the walkable set 0xe1f: no zone or region change.
            map->cells.GetData()[index].terrainType = static_cast<PathType>(kTypeBuildable);
            ++healed;
        }
    }
    if (healed > 0) ErrLine("[TerrainResort] load healed %d cells\n", healed);
    return healed;
}

}  // namespace TerrainResort
}  // namespace Ext
