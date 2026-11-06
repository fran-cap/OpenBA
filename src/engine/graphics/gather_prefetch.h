// ============================================================================
// src/engine/graphics/gather_prefetch.h -- GATHERPF (NOT RETAIL; faithful perf
// work, hints only): the prefetch-ahead switches of the faithful scene gather
// `Graphics::Scene::AddCulledDrawItems` (cull.cpp), and the gather's counters
// for the stress harness's COMPPROF `gather` row.
// ============================================================================
//
// docs/extensions/handins/GATHERPF.md has the cache-line table these arms are
// derived from, the A/B protocol and the predictions.
//
// Every arm issues `__builtin_prefetch` only (prefetcht0: no architectural
// effect, never faults). The only loads an arm adds are EARLIER copies of loads
// the faithful walk performs on the same object later in the same call (the
// next chain node's `_object` / `_next`, the next visible list entry, that
// entry's vptr, a MeshInstance's `_materials._data`); each is listed beside its
// arm in cull.cpp. No arm reorders, skips or guards a faithful load or store,
// so the visible list, its order and every draw item are unchanged: there is
// no shadow to run.
//
// The switches are `static constexpr`: `if constexpr` arms, so a `false` arm
// compiles to nothing. No flag, no Opt() field, no settings key. To A/B an arm
// flip its line and rebuild the perf exe (only cull.cpp recompiles); count the
// prefetch instructions with
//   objdump -d build/cmake-perf/CMakeFiles/OpenBA.dir/src/engine/graphics/cull.cpp.obj | grep -c prefetch
// against the hand-in's table before running.
//
// When ext.draw_gather is ON the faithful body is not reached at all (its seam
// returns at the top of AddCulledDrawItems), so none of this runs and the
// counters stay 0.
// ============================================================================
#pragma once

#include <cstdint>

namespace Graphics {
namespace GatherPf {

// ---- arm 1: the cell chain walk (default ON) --------------------------------
// While object i of a cell's ring is tested, load node i+S (`_object`, `_next`:
// the walk reads both on its next visit), prefetch that Geometry's cull lines
// (+0x28 `_detailId` .. +0x63 `_worldBounds.max.z`: one or two lines), and
// prefetch node i+S+1's SceneNode line. The ring gives the next node for free;
// S > 1 hops the ring S-1 more times (loads of nodes the walk visits anyway).
inline constexpr bool kChain = true;
// PREFETCH (the tick walk) measured stride 1 best and every farther stride worse
// (+3/+4/+8% at 2/4/8, PREFETCH.md section 9.2). Kept as a constant so a sweep is
// repeatable, not as a tuning knob.
inline constexpr int  kChainStride = 1;

// ---- arm 2: the visible-list dispatch (default ON) --------------------------
// The visible list is an array of Geometry pointers, so lookahead costs no
// pointer chase. Two stages, one dispatch apart:
//   i+2S  the vptr line (+0x00) and the line at +0xc0 (`geo + 1`, a
//         MeshInstance's _mesh / _materials / the head of _animation);
//   i+S   by dynamic type (the vptr, resident since i+2S, compared with the
//         compiler's MeshInstance vtable): +0x80 (_opacityPrev), +0xd8
//         (_animation), +0x11c (_sortBias).
inline constexpr bool kDispatch = true;
inline constexpr int  kDispatchStride = 1;
// Sub-arm of 2 (needs kDispatch): at i+S also load a MeshInstance's
// `_materials._data` (the line was prefetched at i+2S) and prefetch the first
// line of that per-instance array, which AddDrawItems indexes for every
// triangle list. A dependent line behind a pointer in the object: PREFETCHCOV's
// winning arm P had this shape.
inline constexpr bool kDispatchMaterials = true;

// ---- not built (reasons in the hand-in) -------------------------------------
// * the draw-item array write side (Renderer::AddDrawItem / DrawItemArrayResize):
//   IBS_20261013 measured 0.78% / 0.05% L1 miss there: a sequential append the
//   hardware stream prefetcher already covers;
// * the cull stack (CullStackPushChildren): a 9 KB local array, always in L1.

}  // namespace GatherPf
}  // namespace Graphics

// ---- the counters (cull.cpp; COMPPROF >= 1 prints them as the `gather` row) --
// Reset by the harness per window. `visible` is added once per call; draw items once
// per append in AddDrawItem. (The per-cell and per-ring-object counts were removed by
// CLEANUP: a memory add per ring object in the walk; their numbers are in GATHERPF.md's
// Results.)
//
// Issue #253: every counter is split by the current draw group's `_shader` (the slot
// GatherShaderSlot picks: 0 main, 1 shadow, 2 reflection, 3 any other value), so the
// report can tell the scene groups apart. The report sums the slots for the totals.
// A slot is written by one thread at a time: the main thread, except during
// ext.gather_threads' fused dispatch (src/ext/draw_gather/dispatch_par.h), where the
// shadow group's slot and the main group's slot (never the same slot) are each written
// by the one task that dispatches that group. Each slot has a cache line of its own
// (128 bytes, Apple's line size), so those two tasks never write the same line.
constexpr int kGatherShaderSlots = 4;

inline int GatherShaderSlot(int32_t shader) {
    return (shader >= 0 && shader < kGatherShaderSlots - 1) ? static_cast<int>(shader) : kGatherShaderSlots - 1;
}

// Issue #253: MeshInstance::AddDrawItems' LOD pick (mesh.cpp), bumped once per call.
// `lodMulti` = calls whose mesh has more than one detail level; `lodFallback` = those whose
// level loop found no level (past the last switch distance), so retail draws level 0;
// `lodLevelItems` = triangle lists queued per chosen level (index 3 = level 3 and up).
constexpr int kLodLevelSlots = 4;

struct alignas(128) GatherSlotCounters {
    uint64_t visible;                         // visible-list entries dispatched (slot 3 calls)
    uint64_t drawItems;                       // draw items appended to the current group's three
                                              // pass lists during the dispatch phase
    uint64_t lodMulti;
    uint64_t lodFallback;
    uint64_t lodLevelItems[kLodLevelSlots];
};
static_assert(sizeof(GatherSlotCounters) == 128, "one slot per 128-byte line");

extern GatherSlotCounters g_gatherSlots[kGatherShaderSlots];
