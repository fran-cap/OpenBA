// ============================================================================
// tests/unit/ext_storage_metrics_test.cpp -- ext.storage_metrics' sums and texts
// (src/ext/storage_metrics/storage_math.h)
// ============================================================================
// What the Town Hall's Storage tab shows: a building's kind from its components, the used and total space
// per kind and per building type, which lines appear, and the two texts ("12,400 / 15,000", "82.6%").
// ============================================================================
#include "unit.h"
#include "ext/storage_metrics/storage_math.h"

#include <string>
#include <vector>

namespace {

using namespace Ext::StorageMetrics;

// stand-ins for three templates, compared by address only
const int kYard = 0;
const int kBarn = 0;
const int kModBarn = 0;
const int kPost = 0;

}  // namespace

UNIT_TEST(ext_storage_metrics_kind_is_decided_by_components) {
    CHECK(Classify(false, true) == Kind::Stockpile);
    CHECK(Classify(false, false) == Kind::Barn);
    // a trade component wins over the storage's limit rule
    CHECK(Classify(true, false) == Kind::TradingPost);
    CHECK(Classify(true, true) == Kind::TradingPost);
}

UNIT_TEST(ext_storage_metrics_only_a_standing_building_counts) {
    // the game's BuildingState ids (src/game/Component/BuildingState.h)
    CHECK(!Standing(0));    // ClearSite
    CHECK(!Standing(1));    // GatherMaterials
    CHECK(!Standing(8));    // Construct
    CHECK(Standing(2));     // Built
    for (int taken_down = 3; taken_down <= 7; ++taken_down) {
        CHECK(!Standing(taken_down));   // Demolish .. Rubble
    }
    CHECK(Standing(13));    // Producing: a finished workplace that stores
    CHECK(Standing(14));    // Paused
    CHECK(Standing(18));    // a trading post with a merchant inbound
    CHECK(Standing(19));    // ... and docked
}

UNIT_TEST(ext_storage_metrics_the_request_example_reads_as_asked) {
    Space s;
    s.Add(12400, 15000);
    CHECK_STREQ(SpaceText(s).c_str(), "12,400 / 15,000");
    CHECK_EQ(FullTenths(s), 827);   // 82.666..% rounds to nearest
    CHECK_STREQ(FullText(s).c_str(), "82.7%");
}

UNIT_TEST(ext_storage_metrics_grouping_and_percent_edges) {
    CHECK_STREQ(Grouped(0).c_str(), "0");
    CHECK_STREQ(Grouped(999).c_str(), "999");
    CHECK_STREQ(Grouped(1000).c_str(), "1,000");
    CHECK_STREQ(Grouped(123456).c_str(), "123,456");
    CHECK_STREQ(Grouped(1234567).c_str(), "1,234,567");
    CHECK_STREQ(Grouped(-1234).c_str(), "-1,234");

    Space none;
    CHECK_EQ(FullTenths(none), -1);
    CHECK_STREQ(FullText(none).c_str(), "-");
    CHECK_STREQ(SpaceText(none).c_str(), "0 / 0");

    Space empty;
    empty.Add(0, 6000);
    CHECK_STREQ(FullText(empty).c_str(), "0.0%");

    Space full;
    full.Add(6000, 6000);
    CHECK_STREQ(FullText(full).c_str(), "100.0%");

    // a store holding more than its limit reads above 100
    Space over;
    over.Add(66000, 60000);
    CHECK_STREQ(FullText(over).c_str(), "110.0%");

    // sums past 32 bits stay exact (many large modded stores)
    Space big;
    for (int i = 0; i < 4; ++i) {
        big.Add(1500000000, 2000000000);
    }
    CHECK_STREQ(SpaceText(big).c_str(), "6,000,000,000 / 8,000,000,000");
    CHECK_STREQ(FullText(big).c_str(), "75.0%");
}

UNIT_TEST(ext_storage_metrics_a_negative_reading_adds_nothing) {
    Space s;
    s.Add(-5, -10);
    CHECK_EQ(s.used, 0);
    CHECK_EQ(s.capacity, 0);
    CHECK_EQ(s.buildings, 1);
}

UNIT_TEST(ext_storage_metrics_an_empty_town_shows_three_empty_totals) {
    const std::vector<Line> lines = Lines(Snapshot());
    CHECK_EQ(static_cast<int>(lines.size()), 3);
    CHECK(lines[0].total && lines[0].kind == Kind::Stockpile);
    CHECK(lines[1].total && lines[1].kind == Kind::Barn);
    CHECK(lines[2].total && lines[2].kind == Kind::TradingPost);
    CHECK_EQ(lines[1].space.buildings, 0);
}

UNIT_TEST(ext_storage_metrics_one_type_per_kind_shows_totals_only) {
    Snapshot s;
    s.Add(&kYard, "Stock Pile", Kind::Stockpile, 500, 4000);
    s.Add(&kYard, "Stock Pile", Kind::Stockpile, 1500, 2000);
    s.Add(&kBarn, "Storage Barn", Kind::Barn, 3000, 6000);
    s.Add(&kPost, "Trading Post", Kind::TradingPost, 100, 60000);
    const std::vector<Line> lines = Lines(s);
    CHECK_EQ(static_cast<int>(lines.size()), 3);
    CHECK_EQ(lines[0].space.buildings, 2);
    CHECK_EQ(lines[0].space.used, 2000);
    CHECK_EQ(lines[0].space.capacity, 6000);
    CHECK_EQ(lines[1].space.used, 3000);
    CHECK_EQ(lines[2].space.capacity, 60000);
    CHECK_EQ(static_cast<int>(s.types.size()), 3);
}

UNIT_TEST(ext_storage_metrics_a_kind_with_two_types_lists_them_by_name) {
    Snapshot s;
    s.Add(&kBarn, "Storage Barn", Kind::Barn, 3000, 6000);
    s.Add(&kModBarn, "Root Cellar", Kind::Barn, 100, 900);
    s.Add(&kBarn, "Storage Barn", Kind::Barn, 6000, 6000);
    s.Add(&kPost, "Trading Post", Kind::TradingPost, 0, 60000);
    const std::vector<Line> lines = Lines(s);
    // stockpiles total, barns total, the two barn types by name, trading posts total
    CHECK_EQ(static_cast<int>(lines.size()), 5);
    CHECK(lines[1].total && lines[1].kind == Kind::Barn);
    CHECK_EQ(lines[1].space.buildings, 3);
    CHECK_EQ(lines[1].space.used, 9100);
    CHECK_EQ(lines[1].space.capacity, 12900);
    CHECK(!lines[2].total);
    CHECK_STREQ(lines[2].name.c_str(), "Root Cellar");
    CHECK_EQ(lines[2].space.buildings, 1);
    CHECK_STREQ(lines[3].name.c_str(), "Storage Barn");
    CHECK_EQ(lines[3].space.buildings, 2);
    CHECK_EQ(lines[3].space.used, 9000);
    CHECK(lines[4].total && lines[4].kind == Kind::TradingPost);
}

UNIT_TEST(ext_storage_metrics_two_templates_under_one_name_are_one_line) {
    static const int kOtherBarn = 0;
    Snapshot s;
    s.Add(&kBarn, "Storage Barn", Kind::Barn, 1000, 7500);
    s.Add(&kOtherBarn, "Storage Barn", Kind::Barn, 370, 7500);
    // one name only: the total says it all
    CHECK_EQ(static_cast<int>(Lines(s).size()), 3);
    s.Add(&kModBarn, "Root Cellar", Kind::Barn, 100, 900);
    const std::vector<Line> lines = Lines(s);
    CHECK_EQ(static_cast<int>(lines.size()), 5);
    CHECK_STREQ(lines[2].name.c_str(), "Root Cellar");
    CHECK_STREQ(lines[3].name.c_str(), "Storage Barn");
    CHECK_EQ(lines[3].space.buildings, 2);
    CHECK_EQ(lines[3].space.used, 1370);
    CHECK_EQ(lines[3].space.capacity, 15000);
}

UNIT_TEST(ext_storage_metrics_a_row_is_reused_while_its_line_is_the_same) {
    Line a;
    a.total = true;
    a.kind = Kind::Barn;
    Line b = a;
    b.space.Add(10, 20);   // the figures changed, the row did not
    CHECK(a.SameRow(b));
    b.kind = Kind::Stockpile;
    CHECK(!a.SameRow(b));
    Line c;
    c.kind = Kind::Barn;
    c.name = "Root Cellar";
    Line d = c;
    d.name = "Storage Barn";
    CHECK(!c.SameRow(d));
    CHECK(!a.SameRow(c));
}
