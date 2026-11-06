#pragma once
// ============================================================================
// src/ext/draw_gather/dispatch_par.h -- ext.gather_threads: the fused gather's two
// dispatches on the helper pool (issue #253), the order-keeping half
// ============================================================================
// NOT RETAIL. Pure C++ (no game types), so the unit tests drive it
// (tests/unit/ext_dispatch_par_test.cpp); draw_gather.cpp has the engine half.
//
// The fused shadow gather (draw_gather.cpp FUSED) holds both visible lists of the
// frame's sun: the shadow group's, which it dispatches at once, and the main group's,
// which the main gather dispatches after the rest of the main group's globals. With
// ext.gather_threads on, the two dispatches run as two pool tasks, each into a buffer
// of its own (a stand-in Renderer whose one draw group is a copy of the real group's
// header). A task dispatches only the geometries whose AddDrawItems is known to touch
// nothing but the geometry, its mesh and materials, the group header, the renderer's
// alpha and its draw-group counters slot; it leaves every other one to the main thread,
// recording a CUT: that item's index and where the three pass buffers stood.
//
// The replay then appends, per pass, the buffered items in order and dispatches each
// cut item live on the real renderer at its place, so each pass list receives the
// same items in the same order as the serial dispatch: a serial dispatch appends the
// items of list entry i to the three pass lists before those of entry i+1, and the
// task's buffers hold exactly the safe entries' items in that order.
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <vector>

namespace Ext {
namespace DrawGather {

constexpr int kDispatchPasses = 3;   // a draw group's three pass lists

// A list entry the task left to the main thread, and the buffers' sizes when it met it.
struct DispatchCut {
    int32_t index = 0;
    int32_t at[kDispatchPasses] = {};
};

// The task side, over list entries [0, n). `safe(i)`: the entry may be dispatched into
// the buffers; `dispatch(i)`: dispatches it there; `size(k)`: buffer k's item count.
// `cuts` is cleared and filled in list order; `ends` gets the final buffer sizes.
template <class Safe, class Dispatch, class Size>
void RunDispatchTask(int32_t n, Safe&& safe, Dispatch&& dispatch, Size&& size, std::vector<DispatchCut>& cuts,
                     int32_t (&ends)[kDispatchPasses]) {
    cuts.clear();
    for (int32_t i = 0; i < n; ++i) {
        if (safe(i)) {
            dispatch(i);
            continue;
        }
        DispatchCut c;
        c.index = i;
        for (int k = 0; k < kDispatchPasses; ++k) c.at[k] = size(k);
        cuts.push_back(c);
    }
    for (int k = 0; k < kDispatchPasses; ++k) ends[k] = size(k);
}

// The main-thread side. `append(k, first, count)`: appends buffered items
// [first, first + count) of pass k to the real list; `live(index)`: dispatches list entry
// `index` on the real renderer. Calls them in the serial dispatch's order.
template <class Append, class Live>
void ReplayDispatch(const std::vector<DispatchCut>& cuts, const int32_t (&ends)[kDispatchPasses], Append&& append,
                    Live&& live) {
    int32_t cursor[kDispatchPasses] = {};
    for (const DispatchCut& c : cuts) {
        for (int k = 0; k < kDispatchPasses; ++k) {
            if (c.at[k] > cursor[k]) append(k, cursor[k], c.at[k] - cursor[k]);
            cursor[k] = c.at[k];
        }
        live(c.index);
    }
    for (int k = 0; k < kDispatchPasses; ++k) {
        if (ends[k] > cursor[k]) append(k, cursor[k], ends[k] - cursor[k]);
    }
}

}  // namespace DrawGather
}  // namespace Ext
