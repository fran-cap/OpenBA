#pragma once
// ============================================================================
// src/game/Game/WorldDrawKey.h — WorldGeometry's draw item key (WorldGeometry::DrawKey)
// ============================================================================
//
// The item key AddDrawItems (0x1400AE490) hands Renderer::AddDrawItem and Draw
// (0x1400AEA10) decodes: the mode in bits 13-15, the index in the rest. No retail
// string names the modes; the names come from what Draw does with each one.
//
// Standalone (<cstdint> only) so the unit-linked ext headers (render_pipe's
// snapshot_table.h, terrain_batch_rules.h) can use it without the world.
// WorldGeometry.h includes it and spells it WorldGeometry::DrawKey.
// ============================================================================

#include <cstdint>

struct WorldDrawKey {
    static constexpr uint32_t kModeMask          = 0xE000u;       // the mode bits (Draw: key & 0xE000)
    static constexpr uint32_t kIndexMask         = 0xFFFF1FFFu;   // the index: key & ~kModeMask, Draw's 32-bit form
    static constexpr uint32_t kModeTerrainChunk  = 0x0000u;       // a terrain chunk, index (row << 6) | col
    static constexpr uint32_t kModeDecal         = 0x2000u;       // a decal batch
    static constexpr uint32_t kModeRoad          = 0x4000u;       // a road batch
    static constexpr uint32_t kModeRiverRange    = 0x6000u;       // a _contourRangesA range (river water)
    static constexpr uint32_t kModeLakeRange     = 0x8000u;       // a _contourRangesB range (lakes)
    static constexpr uint32_t kModeExtTerrainRun = 0xC000u;       // NOT RETAIL: ext.terrain_batch's run item
};
