// ============================================================================
// src/ext/terrain_batch/terrain_batch_rules.h -- ext.terrain_batch's pure parts
// ============================================================================
// NOT RETAIL. docs/extensions/handins/TERRAINBATCH.md. Standard C++ only, no engine
// types (Game/WorldDrawKey.h is <cstdint> only): the unit tests
// (tests/unit/ext_terrain_batch_test.cpp) include this alone.
//
// 1. THE RUN KEY. Retail's WorldGeometry::AddDrawItems gives each visible terrain chunk
//    one draw item whose key2 is (row << 6) | col (draw mode 0, the top three bits 0).
//    The row merges consecutive visible chunks of one grid row into one item. Its key2
//    carries the top two bits set (0xC000: draw modes 0xC000 / 0xE000, which retail never
//    emits) and in the low 14 bits the row, the first column and the length - 1:
//
//        key2 = 0xC000 | row << (coordBits + lenBits) | col << lenBits | (len - 1)
//
//    coordBits is the width of a chunk coordinate for this grid, lenBits = 14 - 2 x
//    coordBits. Ascending key2 is therefore row-major by first chunk, the order retail's
//    keys sort the chunk items in (the draw-item sort's arg5 term), so the GPU sees the
//    chunks in retail's order.
//
// 2. THE RUN LENGTH CAP. The run is one DrawIndexed of len x indicesPerChunk 16-bit
//    indices over a buffer holding retail's chunk index list repeated with +m x
//    vertsPerChunk offsets; the largest index is len x vertsPerChunk - 1, so len <=
//    65536 / vertsPerChunk (60 for retail's 33 x 33 chunks). Also len <= 1 << lenBits and
//    len <= chunksPerSide.
//
// 3. THE SHADOW CASTER BOX (ideas_passes.md rank 4). In the shadow pass a chunk is kept
//    only if the box its shadow can reach touches the VIEW frustum, the rule retail gives
//    objects in Scene::AddCulledDrawItems (grown by the radius and 11 units along the sun
//    box's plane 0), made height-aware: the chunk box grown by `margin` on every side,
//    swept along the light direction until its top is below its own bottom. See
//    CasterSweepBox.
// ============================================================================
#pragma once

#include <cstdint>
#include "Game/WorldDrawKey.h"   // WorldDrawKey::kModeExtTerrainRun (the run tag)

namespace Ext {
namespace TerrainBatch {

// The two top key bits: a run item (WorldGeometry::Draw's EXT SEAM tests this).
constexpr uint32_t kRunTag = WorldDrawKey::kModeExtTerrainRun;
// The payload width under the tag.
constexpr int32_t kPayloadBits = 14;
// The largest 16-bit index + 1.
constexpr int32_t kIndexLimit = 0x10000;
// Retail's caster extension along the sun box's plane 0 (cull.cpp kShadowExtend).
constexpr float kRetailExtend = 11.0f;
// Below this |light z| the sweep is unbounded: the chunk is kept.
constexpr float kMinLightDown = 1.0e-3f;

inline bool IsRunKey(uint32_t key) {
    return (key & kRunTag) == kRunTag;
}

struct RunLayout {
    int32_t coordBits = 0;   // bits per chunk row / column
    int32_t lenBits   = 0;   // bits for len - 1
    int32_t maxRun    = 0;   // longest run; < 1 = the layout cannot encode this grid
};

// The layout for a grid of chunksPerSide x chunksPerSide chunks of vertsPerChunk
// vertices each. maxRun 0 when the grid does not fit the payload (more than 64 chunks a
// side) or the parameters are not positive.
inline RunLayout LayoutFor(int32_t chunksPerSide, int32_t vertsPerChunk) {
    RunLayout l;
    if (chunksPerSide <= 0 || vertsPerChunk <= 0) return l;
    int32_t bits = 0;
    while ((1 << bits) < chunksPerSide) {
        ++bits;
    }
    if (bits < 1) bits = 1;
    const int32_t lenBits = kPayloadBits - 2 * bits;
    if (lenBits < 1) return l;
    int32_t maxRun = 1 << lenBits;
    if (maxRun > chunksPerSide) maxRun = chunksPerSide;
    const int32_t byIndex = kIndexLimit / vertsPerChunk;
    if (maxRun > byIndex) maxRun = byIndex;
    l.coordBits = bits;
    l.lenBits   = lenBits;
    l.maxRun    = maxRun;
    return l;
}

inline uint16_t EncodeRun(const RunLayout& l, int32_t row, int32_t col, int32_t len) {
    const uint32_t key = kRunTag
                       | (static_cast<uint32_t>(row) << (l.coordBits + l.lenBits))
                       | (static_cast<uint32_t>(col) << l.lenBits)
                       | static_cast<uint32_t>(len - 1);
    return static_cast<uint16_t>(key);
}

struct Run {
    int32_t row = 0;
    int32_t col = 0;
    int32_t len = 0;
};

inline Run DecodeRun(const RunLayout& l, uint32_t key) {
    const uint32_t coordMask = (1u << l.coordBits) - 1u;
    const uint32_t lenMask   = (1u << l.lenBits) - 1u;
    Run r;
    r.row = static_cast<int32_t>((key >> (l.coordBits + l.lenBits)) & coordMask);
    r.col = static_cast<int32_t>((key >> l.lenBits) & coordMask);
    r.len = static_cast<int32_t>(key & lenMask) + 1;
    return r;
}

// One grid row: visible[0..n) per chunk. Calls emit(col, len) for each maximal run of
// visible chunks, split every maxRun chunks, left to right.
template <class Emit>
void ForEachRun(const uint8_t* visible, int32_t n, int32_t maxRun, Emit&& emit) {
    int32_t j = 0;
    while (j < n) {
        if (!visible[j]) {
            ++j;
            continue;
        }
        int32_t len = 1;
        while (j + len < n && len < maxRun && visible[j + len]) {
            ++len;
        }
        emit(j, len);
        j += len;
    }
}

// The run index list: `copies` copies of retail's chunk list `pattern` (perChunk
// indices), copy m offset by m x vertsPerChunk. `out` holds copies x perChunk entries.
inline void FillRunIndices(const uint16_t* pattern, int32_t perChunk, int32_t vertsPerChunk,
                           int32_t copies, uint16_t* out) {
    for (int32_t m = 0; m < copies; ++m) {
        const uint32_t offset = static_cast<uint32_t>(m) * static_cast<uint32_t>(vertsPerChunk);
        for (int32_t i = 0; i < perChunk; ++i) {
            out[static_cast<int64_t>(m) * perChunk + i] = static_cast<uint16_t>(pattern[i] + offset);
        }
    }
}

struct Aabb {
    float min[3];
    float max[3];
};

// The margin the caster box is grown by: 8 texels of a 512 map over the sun box's
// width (`sunBoxWidth`, the distance between its side planes), at least 2 units. It
// covers the shadow filter's reach into texels next to a receiver's own, at any map
// size the game builds (512 is the smallest, Low).
inline float CasterMargin(float sunBoxWidth) {
    const float m = sunBoxWidth * (8.0f / 512.0f);
    return m > 2.0f ? m : 2.0f;
}

// The box a terrain chunk's shadow can reach. `light` is the unit direction the light
// travels (the sun box's plane-0 normal, local space; z < 0 is downwards). The chunk
// box grown by `margin` on each side is swept along `light` by
//     T = max(11, (grown top - grown bottom) / -light.z)
// so the swept copy's top lies at or below the grown bottom: every point of the chunk
// shadows only points on its ray before that, and every receiver within `margin` of such
// a ray lies inside the union of the grown box and its swept copy, whose bounding box is
// `out`. Returns false (no finite box: keep the chunk) when the light is not downwards.
inline bool CasterSweepBox(const Aabb& chunk, const float light[3], float margin, Aabb& out) {
    if (!(light[2] < -kMinLightDown)) return false;
    Aabb grown;
    for (int a = 0; a < 3; ++a) {
        grown.min[a] = chunk.min[a] - margin;
        grown.max[a] = chunk.max[a] + margin;
    }
    float t = (grown.max[2] - grown.min[2]) / -light[2];
    if (t < kRetailExtend) t = kRetailExtend;
    for (int a = 0; a < 3; ++a) {
        const float shifted = light[a] * t;
        const float lo = grown.min[a] + shifted;
        const float hi = grown.max[a] + shifted;
        out.min[a] = lo < grown.min[a] ? lo : grown.min[a];
        out.max[a] = hi > grown.max[a] ? hi : grown.max[a];
    }
    return true;
}

}  // namespace TerrainBatch
}  // namespace Ext
