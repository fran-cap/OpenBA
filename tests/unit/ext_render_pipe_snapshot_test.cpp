// ============================================================================
// tests/unit/ext_render_pipe_snapshot_test.cpp -- ext.render_pipe's snapshot builder,
// its pure parts (src/ext/render_pipe/snapshot_table.h)
// ============================================================================
// BuildSnapshot itself (snapshot.cpp) is not linked here: it needs the engine's geometry
// types. What it decides with these two pieces is:
//   * PointerTable: the instanced-state table is Reset for 64 keys, and a packet can hold
//     more distinct instanced poses than that. Review F-218: the table never grew, so
//     once its 128 slots were taken a new key probed forever (the main thread hung in
//     BuildSnapshot). It now keeps its load at one half whatever Reset was told.
//   * WorldItemCopied: only a WorldGeometry's decal / road items draw from the arena copy;
//     the terrain chunks, the water ranges, ext.terrain_batch's runs and ext.better_water's
//     strip keep the live object (P13_OVERLAP 2.2). Review F-219: pass C re-pointed every
//     item of the WorldGeometry, and ext.better_water took a strip slot per arena address.
//   * LastEntry + the recorded route (issue #253): the lean builder's pass C replays the entry
//     pass A recorded per item instead of probing again; the record must equal a probe.
// ============================================================================
#include "unit.h"
#include "ext/render_pipe/snapshot_table.h"
#include "ext/better_water/better_water.h"   // kStripDrawKey

#include <cstdint>
#include <vector>

namespace {

using Ext::RenderPipe::PointerTable;
using Ext::RenderPipe::WorldItemCopied;

}  // namespace

// F-218: more keys than Reset was sized for. Every insert must leave a free slot (load at
// most one half), and every key must be found again. The loop stops at the first full-ish
// table instead of probing a full one forever.
UNIT_TEST(ext_render_pipe_snapshot_table_grows_past_its_reset_size) {
    std::vector<unsigned char> keys(4096);
    PointerTable table;
    table.Reset(64);
    bool loadHeld = true;
    int32_t inserted = 0;
    for (int32_t i = 0; i < 1000; ++i) {
        if (table.Find(&keys[static_cast<size_t>(i) * 4], i) != -1) {
            loadHeld = false;
            break;
        }
        inserted = i + 1;
        if (table.Capacity() < static_cast<size_t>(inserted) * 2) {
            loadHeld = false;
            break;
        }
    }
    CHECK(loadHeld);
    CHECK(inserted == 1000);
    bool allFound = true;
    for (int32_t i = 0; i < inserted; ++i) {
        if (table.Find(&keys[static_cast<size_t>(i) * 4], -1) != i) allFound = false;
    }
    CHECK(allFound);
    CHECK(table.Find(&keys[4001], -1) == -1);
}

// A Reset after a grown frame starts empty again, at the grown size.
UNIT_TEST(ext_render_pipe_snapshot_table_reset_clears_a_grown_table) {
    std::vector<unsigned char> keys(1024);
    PointerTable table;
    table.Reset(64);
    for (int32_t i = 0; i < 200 && table.Capacity() >= static_cast<size_t>(i + 1) * 2; ++i) {
        table.Find(&keys[static_cast<size_t>(i)], i);
    }
    table.Reset(64);
    CHECK(table.Find(&keys[0], -1) == -1);
    CHECK(table.Find(&keys[7], 3) == -1);
    CHECK(table.Find(&keys[7], -1) == 3);
}

// F-219: which WorldGeometry items draw from the copy (WorldGeometry::AddDrawItems' keys).
UNIT_TEST(ext_render_pipe_snapshot_world_items_copied_only_for_decals_and_roads) {
    CHECK(WorldItemCopied(0x2000u | 5u));                  // a decal
    CHECK(WorldItemCopied(0x4000u | 0x1234u));             // a road batch
    CHECK(!WorldItemCopied((10u << 6) | 3u));              // a terrain chunk (i << 6 | j)
    CHECK(!WorldItemCopied(0x6000u | 2u));                 // a river range
    CHECK(!WorldItemCopied(0x8000u | 2u));                 // a lake range
    CHECK(!WorldItemCopied(Ext::BetterWater::kStripDrawKey));   // ext.better_water's strip
}

// Issue #253: the lean builder records each item's entry in pass A (the previous item's entry
// without a probe, else one probe, else an insert) and pass C replays the record. Over a walk
// with runs, repeats and never-seen keys, the record equals what a second probe per item
// (the reference's pass C) answers, and the prefetch hints change no answer.
UNIT_TEST(ext_render_pipe_snapshot_lean_record_matches_a_second_probe) {
    std::vector<unsigned char> keys(512);
    std::vector<const void*> walk;
    uint32_t seed = 12345u;
    for (int32_t i = 0; i < 3000; ++i) {
        seed = seed * 1664525u + 1013904223u;
        const size_t key = (seed >> 8) % 300u;
        const int32_t run = 1 + static_cast<int32_t>((seed >> 20) % 3u);
        for (int32_t r = 0; r < run; ++r) walk.push_back(&keys[key]);
    }
    PointerTable table;
    table.Reset(16);   // grows on the way: Prefetch must follow the grown slots
    std::vector<int32_t> record(walk.size(), -1);
    Ext::RenderPipe::LastEntry last;
    int32_t next = 0;
    int32_t reused = 0;
    for (size_t i = 0; i < walk.size(); ++i) {
        if (i + 8 < walk.size()) table.Prefetch(walk[i + 8]);
        if (last.Hit(walk[i])) {
            record[i] = last.index;
            ++reused;
            continue;
        }
        int32_t index = table.Find(walk[i], -1);
        if (index >= 0) {
            ++reused;
        } else {
            index = next++;
            table.Find(walk[i], index);
        }
        record[i] = index;
        last.Set(walk[i], index);
    }
    bool same = true;
    for (size_t i = 0; i < walk.size(); ++i) same = same && table.Find(walk[i], -1) == record[i];
    CHECK(same);
    CHECK(next <= 300);
    CHECK(reused + next == static_cast<int32_t>(walk.size()));
}

// LastEntry never hits a null key (the builders refuse a null geometry before asking), and a
// cleared one hits nothing.
UNIT_TEST(ext_render_pipe_snapshot_last_entry_hits_only_its_key) {
    unsigned char a = 0;
    unsigned char b = 0;
    Ext::RenderPipe::LastEntry last;
    CHECK(!last.Hit(nullptr));
    CHECK(!last.Hit(&a));
    last.Set(&a, 7);
    CHECK(last.Hit(&a));
    CHECK(!last.Hit(&b));
    CHECK(last.index == 7);
    last.Clear();
    CHECK(!last.Hit(&a));
    CHECK(!last.Hit(nullptr));
    CHECK(last.index == Ext::RenderPipe::kKeepLive);
    PointerTable empty;
    empty.Prefetch(&a);   // no slots yet: a no-op
    CHECK(empty.Capacity() == 0);
}
