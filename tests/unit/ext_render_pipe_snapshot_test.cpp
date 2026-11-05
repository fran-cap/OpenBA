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
