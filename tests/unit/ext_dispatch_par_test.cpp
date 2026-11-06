// ============================================================================
// tests/unit/ext_dispatch_par_test.cpp -- DISPATCHPAR: the fused gather's two dispatches
// on the helper pool (src/ext/draw_gather/dispatch_par.h), the order-keeping half
// ============================================================================
// A model list whose entries each append a scripted run of items to three pass lists,
// some of them "unsafe" (left to the main thread). The serial dispatch appends every
// entry's items in list order; the task + replay must give the same three lists, item
// for item, over random lists (no unsafe entry, all unsafe, unsafe at the ends, entries
// with no items). Also: two tasks on the real HelperPool, each with its own buffers.
// ============================================================================
#include "unit.h"
#include "ext/draw_gather/dispatch_par.h"
#include "ext/gather_threads/gather_threads.h"

#include <random>
#include <vector>

namespace {

using Ext::DrawGather::DispatchCut;
using Ext::DrawGather::kDispatchPasses;
using Ext::DrawGather::ReplayDispatch;
using Ext::DrawGather::RunDispatchTask;

struct Entry {
    bool safe = true;
    std::vector<std::pair<int, int>> items;   // (pass, value), in append order
};

using Lists = std::vector<int>[kDispatchPasses];

void Emit(const Entry& e, Lists& out) {
    for (const auto& it : e.items) out[it.first].push_back(it.second);
}

std::vector<Entry> RandomList(std::mt19937& rng, int n, int unsafePercent) {
    std::vector<Entry> list(static_cast<size_t>(n));
    int value = 0;
    for (Entry& e : list) {
        e.safe = static_cast<int>(rng() % 100) >= unsafePercent;
        const int count = static_cast<int>(rng() % 5);   // 0..4 items
        for (int i = 0; i < count; ++i) e.items.emplace_back(static_cast<int>(rng() % kDispatchPasses), value++);
    }
    return list;
}

// Prefix items already in the real lists (the globals' items before the gather).
void Prefill(Lists& l) {
    for (int k = 0; k < kDispatchPasses; ++k) l[k] = { -1 - k, -10 - k };
}

void Parallel(const std::vector<Entry>& list, Lists& real) {
    Lists buf;
    std::vector<DispatchCut> cuts;
    int32_t ends[kDispatchPasses];
    RunDispatchTask(
        static_cast<int32_t>(list.size()), [&](int32_t i) { return list[static_cast<size_t>(i)].safe; },
        [&](int32_t i) { Emit(list[static_cast<size_t>(i)], buf); },
        [&](int k) { return static_cast<int32_t>(buf[k].size()); }, cuts, ends);
    for (const DispatchCut& c : cuts) CHECK(!list[static_cast<size_t>(c.index)].safe);
    ReplayDispatch(
        cuts, ends,
        [&](int k, int32_t first, int32_t count) {
            real[k].insert(real[k].end(), buf[k].begin() + first, buf[k].begin() + first + count);
        },
        [&](int32_t index) { Emit(list[static_cast<size_t>(index)], real); });
}

}  // namespace

UNIT_TEST(ext_dispatch_par_replay_matches_serial_order) {
    std::mt19937 rng(253);
    const int unsafe[] = { 0, 3, 30, 100 };
    for (int round = 0; round < 400; ++round) {
        const int n = static_cast<int>(rng() % 60);
        const std::vector<Entry> list = RandomList(rng, n, unsafe[round % 4]);
        Lists serial;
        Lists par;
        Prefill(serial);
        Prefill(par);
        for (const Entry& e : list) Emit(e, serial);
        Parallel(list, par);
        for (int k = 0; k < kDispatchPasses; ++k) CHECK(serial[k] == par[k]);
    }
}

UNIT_TEST(ext_dispatch_par_cuts_record_buffer_sizes) {
    // safe(2 items to pass 0), unsafe, safe(1 item to pass 2), unsafe
    std::vector<Entry> list(4);
    list[0].items = { { 0, 1 }, { 0, 2 } };
    list[1].safe  = false;
    list[2].items = { { 2, 3 } };
    list[3].safe  = false;
    Lists buf;
    std::vector<DispatchCut> cuts;
    int32_t ends[kDispatchPasses];
    RunDispatchTask(
        4, [&](int32_t i) { return list[static_cast<size_t>(i)].safe; },
        [&](int32_t i) { Emit(list[static_cast<size_t>(i)], buf); },
        [&](int k) { return static_cast<int32_t>(buf[k].size()); }, cuts, ends);
    CHECK_EQ(cuts.size(), static_cast<size_t>(2));
    CHECK_EQ(cuts[0].index, 1);
    CHECK_EQ(cuts[0].at[0], 2);
    CHECK_EQ(cuts[0].at[2], 0);
    CHECK_EQ(cuts[1].index, 3);
    CHECK_EQ(cuts[1].at[2], 1);
    CHECK_EQ(ends[0], 2);
    CHECK_EQ(ends[1], 0);
    CHECK_EQ(ends[2], 1);
}

namespace {

struct PoolCtx {
    const std::vector<Entry>* lists[2];
    Lists buf[2];
    std::vector<DispatchCut> cuts[2];
    int32_t ends[2][kDispatchPasses];
};

void PoolTask(void* ctx, int32_t task, int32_t /*worker*/) {
    PoolCtx& c = *static_cast<PoolCtx*>(ctx);
    const std::vector<Entry>& list = *c.lists[task];
    Lists& buf = c.buf[task];
    RunDispatchTask(
        static_cast<int32_t>(list.size()), [&](int32_t i) { return list[static_cast<size_t>(i)].safe; },
        [&](int32_t i) { Emit(list[static_cast<size_t>(i)], buf); },
        [&](int k) { return static_cast<int32_t>(buf[k].size()); }, c.cuts[task], c.ends[task]);
}

}  // namespace

UNIT_TEST(ext_dispatch_par_two_tasks_on_the_pool) {
    std::mt19937 rng(17);
    Ext::GatherThreads::HelperPool pool(1);
    pool.Wake();
    for (int round = 0; round < 50; ++round) {
        const std::vector<Entry> a = RandomList(rng, 200, 5);
        const std::vector<Entry> b = RandomList(rng, 200, 5);
        PoolCtx c;
        c.lists[0] = &a;
        c.lists[1] = &b;
        pool.Publish(&PoolTask, &c, 2);
        const auto r = pool.Finish();
        CHECK_EQ(r.tasks, 2);
        for (int t = 0; t < 2; ++t) {
            const std::vector<Entry>& list = t == 0 ? a : b;
            Lists serial;
            Lists par;
            for (const Entry& e : list) Emit(e, serial);
            ReplayDispatch(
                c.cuts[t], c.ends[t],
                [&](int k, int32_t first, int32_t count) {
                    par[k].insert(par[k].end(), c.buf[t][k].begin() + first, c.buf[t][k].begin() + first + count);
                },
                [&](int32_t index) { Emit(list[static_cast<size_t>(index)], par); });
            for (int k = 0; k < kDispatchPasses; ++k) CHECK(serial[k] == par[k]);
        }
    }
    pool.Park();
}
