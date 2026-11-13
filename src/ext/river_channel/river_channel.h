#pragma once
// ============================================================================
// src/ext/river_channel/river_channel.h -- ext.river_channel: a least channel along thin rivers
// ============================================================================
// NOT RETAIL. docs/extensions/handins/RIVERCHANNEL.md. Class B, off by default: a new map's heights differ
// from retail's for the same seed. The seam is the end of the height pipeline (Game/worldgen.cpp), so it
// acts on a new game only; a loaded save keeps the heights it was saved with.
// ============================================================================

struct TerrainHeightfield;

namespace Ext {
namespace RiverChannel {

// Carves the least channel along every feature polyline of `hf` (river_channel_math.h).
void Apply(TerrainHeightfield* hf);

}  // namespace RiverChannel
}  // namespace Ext
