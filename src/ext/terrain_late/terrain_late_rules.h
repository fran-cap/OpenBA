// ============================================================================
// src/ext/terrain_late/terrain_late_rules.h -- ext.terrain_late's pure parts
// ============================================================================
// NOT RETAIL. docs/extensions/handins/TERRAINLATE.md. Standard C++ only, no engine types: the unit
// tests (tests/unit/ext_terrain_late_test.cpp) include this alone.
//
// 1. THE MOVE. Retail sorts the main entry's Opaque list by detail id first (the draw-item sort's
//    first term, draw_sort.h): the terrain's items (detail id 1) come before every other opaque item
//    (2), only the sky (no depth test, no depth write) before them. MoveMatchingToBack moves every
//    terrain item behind the rest, keeping the order inside both groups: the terrain items keep their
//    row-major chunk order and every other item keeps retail's sorted position relative to the others,
//    so instancing batches, draw_batch runs and state changes among them are retail's.
//
// 2. WHY THE PICTURE IS RETAIL'S. Every opaque item after the sky tests depth and writes it. A sample's
//    final colour is the nearest fragment's, whatever the draw order, EXCEPT at an exact depth tie:
//    retail's less-or-equal test gives a tie to the LATER draw, i.e. to the object over the terrain.
//    Drawn last, the terrain therefore tests LESS (LateDepthFunc): a tie fails and the object keeps
//    the sample, as in retail. The depth buffer is the same either way (a tie writes the same value).
//    Measured on the stress town (handin section 3): 160 of 13.6 M samples are such ties.
//
// 3. WHEN IT IS SAFE (LateSafe). Only a terrain pass that tests and writes depth with less or
//    less-or-equal and does not blend can be moved: a terrain that blended, or did not write depth,
//    would paint over or show through what is now drawn before it. Any other state (a mod's material)
//    keeps retail's order.
// ============================================================================
#pragma once

#include <algorithm>
#include <cstdint>

namespace Ext {
namespace TerrainLate {

// Video::RenderState::Depth::func values (src/render/dx11/renderstatedata.cpp kCompareFunc).
constexpr uint32_t kFuncLess      = 3;
constexpr uint32_t kFuncLessEqual = 4;

// The terrain pass's render state, as LateSafe reads it.
struct PassState {
    bool     depthTest  = false;
    bool     depthWrite = false;
    uint32_t depthFunc  = 0;
    bool     blend      = false;
};

inline bool LateSafe(const PassState& s) {
    const bool lessTest = s.depthFunc == kFuncLess || s.depthFunc == kFuncLessEqual;
    return s.depthTest && s.depthWrite && !s.blend && lessTest;
}

// The depth test the moved terrain draws with: less-or-equal becomes less (section 2).
inline uint32_t LateDepthFunc(uint32_t func) {
    return func == kFuncLessEqual ? kFuncLess : func;
}

// Moves every item `match` accepts behind every item it rejects, in place, keeping the relative order
// inside both groups (a stable partition, without an allocation: one rotate per run of matching items,
// found from the back). Returns the number of items moved. The usual list has one run (the terrain's
// items are adjacent after the sort), so the cost is one scan and one rotate.
template <class T, class Match>
int32_t MoveMatchingToBack(T* items, int32_t count, Match match) {
    int32_t tail  = count;   // [tail, count) holds the items moved so far, in their order
    int32_t moved = 0;
    int32_t i     = count;
    while (i > 0) {
        if (!match(items[i - 1])) {
            --i;
            continue;
        }
        const int32_t runEnd = i;
        while (i > 0 && match(items[i - 1])) --i;
        const int32_t runBegin = i;
        if (runEnd != tail) std::rotate(items + runBegin, items + runEnd, items + tail);
        const int32_t len = runEnd - runBegin;
        tail -= len;
        moved += len;
    }
    return moved;
}

}  // namespace TerrainLate
}  // namespace Ext
