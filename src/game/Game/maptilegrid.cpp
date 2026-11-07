// src/game/Game/maptilegrid.cpp
// Gameplay tile-type grid — Map::CreateNaturalResource
// (CreateNaturalResource_impl_ef20, exe 0x14009ef20).
//
// New-game Load step 31 builds the terrain mesh (worldgen.cpp) and then this
// per-cell TILE GRID at Game+0x80: a dim*dim array of 0x30-byte cells, each
// tagged with a terrain TYPE (buildable / shore / water / mountain). The grid is
// the prerequisite for the start-town spiral spawn-finder (FUN_1400a2880), which
// scans for a square of buildable (type 2) cells and otherwise loops forever.
//
// MapTileGrid_Build is the whole of 0x14009ef20 (D.4 RESIDUE, row 1292): the classify
// pass, FillZones 0x14009c480, the region system build 0x14009e8c0 and the shore
// water-access loop (0x140088b80).
//
// Cell layout (0x30 bytes), fresh-init values from sub_aa90's new-element branch:
//   +0x18 dword = 0xffffffff,  +0x2a byte = 0xff,  type @+0x29 = 0, flag @+0x2d = 0.
//
// Classifier (helper_e2c0) thresholds are the exact exe .rdata float constants
// (read_memory, verified 2026-05-31). Slope is the vertical component of the
// surface normal (fn_MapGetTerrainPos's outTri[2]); see mapquery.cpp.

#include "Game/exe_bodies.h"
#include "load_time.h"   // [LoadTime] stamps (NOT RETAIL, print-only; block LOADSTAMP)
#include "Component/still_terrain.h"   // STILLTERRAIN: StillTerrain::Bump (the terrain epoch)
#include "Game/Map.h"
#include "Game/GameUpdateLoop.h"      // [0x1401c3dd8] +0xf0, the town
#include "Entity/CitizenBehavior.h"    // CitizenBehavior::AddWaterAccess (0x140088b80)
#include "system/MemoryAllocator.h"   // System::MemoryAllocator::MemAlloc / MemFree
#include "Game/terrain_sidecar.h"
#include <cstdint>
#include <cstring>
#include "Game/cell_stamp.h"   // AMBMEMO: CellStamp::NoteWrite after a cell store

// --- classifier thresholds (exe .rdata) -------------------------------------
static constexpr float kCenterBias   = 0.5f;    // DAT_140172114 (cell-center offset)
static constexpr float kBuildableMax = 0.75f;   // DAT_140172124
static constexpr float kSlopeMin     = 0.9f;    // DAT_140172134
static constexpr float kMountainMax  = 2.0f;    // DAT_140172174
static constexpr float kShoreMax     = -0.25f;  // DAT_140172274
static constexpr float kDeepWaterMax = -1.4f;   // DAT_140172284

// CreateNaturalResource_helper_e2c0 — classify one cell into a terrain type.
static int MapTileGrid_Classify(Map* game, int dim, int idx) {
    const float center = (float)(dim >> 1) - kCenterBias;
    float in[3]  = { (float)(idx % dim) - center, (float)(idx / dim) - center, 0.0f };
    float out[3] = { 0, 0, 0 };
    float tri[4] = { 0, 0, 0, 0 };
    game->GetTerrainPos(out, in, 1, tri);   // flag=1 => height-only, no picking refine
    const float h     = out[2];
    const float slope = tri[2];
    if (h < kDeepWaterMax) return static_cast<int>(PathType::DeepWater);
    if (h < kShoreMax)     return static_cast<int>(PathType::Water);
    if (h >= kShoreMax && h <= kBuildableMax && slope >= kSlopeMin) return static_cast<int>(PathType::Normal);
    return (h < kMountainMax) ? static_cast<int>(PathType::Walkable) : static_cast<int>(PathType::Unusable);
}

// The worldgen map create 0x14009ef20, in retail's order (no test of the map or dim;
// the caller tests both):
//   1. size = dim; cells.SetSize(dim * dim) (0x14001aa90)
//   2. classify every cell's terrain type (+0x29; 0x14009e2c0)
//   3. Map::FillZones (0x14009c480): the map ctor left the zones stale (+0xA2 = 1)
//   4. the region system build 0x14009e8c0 (its two halves, Pathfinder_Seed /
//      Region_Seed; row 1292 folds them into one member)
//   5. the shore loop: every buildable cell (type 2) with a shore (7) or deep-water (8)
//      cell in its 3x3 (rows then columns, in bounds of dim) records a water-access
//      point with the town (0x140088b80, the FIRST such neighbour; town =
//      [0x1401c3dd8] +0xf0, read once). The local container retail tears down after the
//      loop (0x14009f08a-0x14009f0be) is never filled.
extern "C" void MapTileGrid_Build(Map* game, int dim) {
    game->size = dim;
    const int count = dim * dim;
    game->cells.SetSize(count);

    LoadTime::Stamp("tg.classify");   // NOT RETAIL: print-only
    for (int i = 0; i < count; ++i)
        game->cells[i].terrainType = static_cast<PathType>(MapTileGrid_Classify(game, dim, i));
    TerrainSidecar::Invalidate();   // [terrain sidecar] fresh grid: mirror rebuilds on first read
    CellStamp::Invalidate();        // [AMBMEMO] fresh grid: every block re-stamped on the next read
    StillTerrain::Bump();           // [STILLTERRAIN] a new size and cell array (every floorCount 0)

    LoadTime::Stamp("tg.fillzones");   // NOT RETAIL: print-only
    game->FillZones();            // 0x14009c480
    LoadTime::Stamp("tg.pfseed");   // NOT RETAIL: print-only
    Pathfinder_Seed(game, dim);   // 0x14009e8c0, first half
    LoadTime::Stamp("tg.regionseed");   // NOT RETAIL: print-only
    Region_Seed(game, dim);       // 0x14009e8c0, second half

    LoadTime::Stamp("tg.shore");   // NOT RETAIL: print-only
    CitizenBehavior* town = GameUpdateLoop::_singleton->m_citizenList;
    for (int y = 0; y < game->size; ++y) {
        for (int x = 0; x < game->size; ++x) {
            const int size = game->size;
            const int cell = size * y + x;
            if (game->cells.GetData()[cell].terrainType != PathType::Normal) continue;
            bool linked = false;
            for (int ny = y - 1; ny <= y + 1 && !linked; ++ny) {
                for (int nx = x - 1; nx <= x + 1; ++nx) {
                    if (nx < 0 || ny < 0 || nx >= dim || ny >= dim) continue;
                    const int neighbour = size * ny + nx;
                    const uint8_t t = static_cast<uint8_t>(game->cells.GetData()[neighbour].terrainType);
                    if (static_cast<uint8_t>(t - static_cast<int>(PathType::Water)) <= 1) {   // Water or DeepWater
                        town->AddWaterAccess(cell, neighbour);         // 0x140088b80
                        linked = true;
                        break;
                    }
                }
            }
        }
    }
}
