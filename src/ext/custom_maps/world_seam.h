#pragma once
// ============================================================================
// src/ext/custom_maps/world_seam.h -- ext.custom_maps (class A): the world creation seam
// ============================================================================
// NOT RETAIL. docs/extensions/handins/CUSTOMMAPS.md. Engine side: this header and world_seam.cpp are not in
// the unit build (they name the heightfield record); custom_maps.h and bmap.h are.
// ============================================================================

struct TerrainHeightfield;

namespace Ext {
namespace CustomMaps {

// Called by the terrain stage of world creation (worldgen.cpp, wg_HeightmapPipeline_f390) after it has set
// the grid size. With the row on and a map picked, fills the heightfield's height map, water paths and water
// features from the map and returns true: the generator's stages are then skipped. Returns false, having
// changed nothing, when there is nothing to do (the row is off, no map is picked, or the map's size is not
// this game's), and the generator runs as usual.
bool ApplyToHeightfield(TerrainHeightfield* hf);

// The map size (cells) a new game is built at. `rowSize` is the terrain generator's size row; while
// ext.custom_maps is on and a map is picked, the map's own size replaces it, so a map may be any size the
// file allows (a multiple of 8, 64 to 1024), not only the sizes the terrain lists. BuildSize is read by the
// terrain stage (the picked map is still pending then); LoadedSize by the code that sizes the game's tile
// arrays after it (the map is active by then).
unsigned BuildSize(unsigned rowSize);
unsigned LoadedSize(unsigned rowSize);

}  // namespace CustomMaps
}  // namespace Ext
