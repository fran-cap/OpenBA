// ============================================================================
// tests/unit/ext_road_upgrade_test.cpp -- ext.road_upgrade
// ============================================================================
// The registry row (class A, ON) and the tool's rules (src/ext/road_upgrade/road_upgrade_math.h): the
// rectangle a drag spans, which cells of it are upgraded, and which of a toolbar group's road tools the tool
// upgrades to. The tool, its button and the orders it places need the running game.
// ============================================================================
#include "unit.h"
#include "ext/road_upgrade/road_upgrade_math.h"
#include "ext/registry.h"
#include "io/settings_file.h"

#include <string>

namespace {

using IO::SettingsFile::Entries;
using Ext::RoadUpgrade::kNoRoad;
using Ext::RoadUpgrade::PickTarget;
using Ext::RoadUpgrade::Rect;
using Ext::RoadUpgrade::RectOf;
using Ext::RoadUpgrade::Upgradable;

bool EntriesLookup(void* ctx, const char* key, std::string& value) {
    const Entries* entries = static_cast<const Entries*>(ctx);
    for (const IO::SettingsFile::Entry& e : *entries) {
        if (e.key == key) {
            value = e.value;
            return true;
        }
    }
    return false;
}

Ext::State ResolveText(const char* text) {
    Entries entries = IO::SettingsFile::Parse(std::string(text));
    return Ext::Resolve(&EntriesLookup, &entries, nullptr, nullptr);
}

constexpr uint8_t kStone = 0;   // CreateRoadDescription::_index of the stone road
constexpr uint8_t kDirt = 1;    // ... and of the dirt road

}  // namespace

UNIT_TEST(ext_road_upgrade_registry_row) {
    const Ext::Entry* e = Ext::FindEntry("road_upgrade");
    CHECK(e != nullptr);
    if (e == nullptr) return;
    CHECK(e->tier == Ext::Tier::Extension);
    CHECK(e->cls == Ext::Class::A);   // the orders are the stone road tool's own; nothing is kept
    CHECK(e->defaultOn);              // ruled ON (2026-11-11)
    CHECK(e->formatVersion == 0);
    CHECK(Ext::SettingsKey(*e) == "ext.road_upgrade");
    CHECK(ResolveText("").ext.road_upgrade);
    CHECK(ResolveText("ext.road_upgrade=1\n").ext.road_upgrade);
    CHECK(!ResolveText("ext.road_upgrade=0\n").ext.road_upgrade);
    CHECK(!ResolveText("profile=faithful\next.road_upgrade=1\n").ext.road_upgrade);
}

UNIT_TEST(ext_road_upgrade_rect_spans_both_cells_in_any_order) {
    const int size = 100;
    const int a = 100 * 7 + 20;    // column 20, row 7
    const int b = 100 * 3 + 45;    // column 45, row 3
    for (const Rect& r : { RectOf(a, b, size), RectOf(b, a, size) }) {
        CHECK(r.minCol == 20 && r.maxCol == 45);
        CHECK(r.minRow == 3 && r.maxRow == 7);
        CHECK(r.Width() == 26 && r.Height() == 5);
    }
    const Rect one = RectOf(a, a, size);
    CHECK(one.Width() == 1 && one.Height() == 1 && one.minCol == 20 && one.minRow == 7);
}

UNIT_TEST(ext_road_upgrade_rect_of_no_cell_is_empty) {
    for (const Rect& r : { RectOf(-1, 5, 100), RectOf(5, -1, 100), RectOf(5, 5, 0) }) {
        CHECK(r.Width() <= 0 || r.Height() <= 0);
    }
}

UNIT_TEST(ext_road_upgrade_only_a_lower_road_is_upgraded) {
    // a built dirt road, and a dirt road still queued on open ground or over a tree
    CHECK(Upgradable(kDirt, kStone, PathType::Fast, false));
    CHECK(Upgradable(kDirt, kStone, PathType::Normal, false));
    CHECK(Upgradable(kDirt, kStone, PathType::Obstacle, false));
    // no road, the same road, a better road: Map::PlaceRoadCell's own no-op cases
    CHECK(!Upgradable(kNoRoad, kStone, PathType::Normal, false));
    CHECK(!Upgradable(kStone, kStone, PathType::Faster, false));
    CHECK(!Upgradable(kStone, kDirt, PathType::Faster, false));
}

UNIT_TEST(ext_road_upgrade_skips_cells_the_road_tool_refuses) {
    // a blocker on the cell, and the terrain types outside the road tool's set
    CHECK(!Upgradable(kDirt, kStone, PathType::Fast, true));
    for (PathType t : { PathType::Occasional, PathType::Immovable, PathType::Unusable, PathType::Water,
                        PathType::DeepWater, PathType::Walkable }) {
        CHECK(!Upgradable(kDirt, kStone, t, false));
    }
    for (PathType t : { PathType::Faster, PathType::Fast, PathType::Normal, PathType::Obstacle,
                        PathType::Bridge, PathType::Tunnel }) {
        CHECK(Upgradable(kDirt, kStone, t, false));
    }
}

UNIT_TEST(ext_road_upgrade_target_is_the_best_road_of_the_group) {
    // retail's roads group: dirt road, stone road, bridge, tunnel
    const int retail[] = { 1, 0, -1, -1 };
    CHECK(PickTarget(retail, 4) == 1);
    // the first of several tools laying the best road (a mod's second stone road)
    const int modded[] = { -1, 1, 0, 1, 0 };
    CHECK(PickTarget(modded, 5) == 2);
}

UNIT_TEST(ext_road_upgrade_no_target_without_two_road_types) {
    const int none[] = { -1, -1, -1 };
    const int oneRoad[] = { -1, 0, -1 };
    const int sameRoads[] = { 1, 1, -1 };
    CHECK(PickTarget(none, 3) == -1);
    CHECK(PickTarget(oneRoad, 3) == -1);
    CHECK(PickTarget(sameRoads, 3) == -1);
    CHECK(PickTarget(nullptr, 0) == -1);
}
