// ============================================================================
// tests/unit/showroom_list_test.cpp -- the BANISHED_SHOWROOM list parser
// (src/diag/showroom_list.h): building paths, tree lines, month lines, forest and bench lines, comments, the
// ranking TSV's first column and the errors a bad line reports.
// ============================================================================
#include "unit.h"
#include "showroom_list.h"

#include <string>

UNIT_TEST(showroom_list_buildings_keep_working) {
    const ShowroomList::List list = ShowroomList::Parse(
        "# a comment line\n"
        "Template\\WoodHouse.rsc\n"
        "  Template/StoneHouse.rsc   # trailing comment\r\n"
        "Template\\Barn.rsc\t17\tsome ranking column\n"
        "\n");
    CHECK_EQ(list.items.size(), (size_t)3);
    CHECK(list.months.empty());
    CHECK(list.errors.empty());
    CHECK(!list.items[0].tree);
    CHECK_STREQ(list.items[0].path.c_str(), "Template\\WoodHouse.rsc");
    CHECK_STREQ(list.items[0].name.c_str(), "WoodHouse");
    CHECK_STREQ(list.items[1].path.c_str(), "Template\\StoneHouse.rsc");
    CHECK_STREQ(list.items[1].name.c_str(), "StoneHouse");
    CHECK_STREQ(list.items[2].path.c_str(), "Template\\Barn.rsc");
}

UNIT_TEST(showroom_list_trees_and_months) {
    const ShowroomList::List list = ShowroomList::Parse(
        "tree Oak\n"
        "TREE   walnut  # nut tree\n"
        "month 0.3\n"
        "months 0.45 6 11.5\n"
        "Template\\WoodHouse.rsc");
    CHECK_EQ(list.items.size(), (size_t)3);
    CHECK(list.errors.empty());
    CHECK(list.items[0].tree);
    CHECK_STREQ(list.items[0].path.c_str(), "oak");
    CHECK_STREQ(list.items[0].name.c_str(), "tree_oak");
    CHECK(list.items[1].tree);
    CHECK_STREQ(list.items[1].path.c_str(), "walnut");
    CHECK(!list.items[2].tree);
    CHECK_EQ(list.months.size(), (size_t)4);
    CHECK(list.months[0] == 0.3f);
    CHECK(list.months[1] == 0.45f);
    CHECK(list.months[2] == 6.0f);
    CHECK(list.months[3] == 11.5f);
}

UNIT_TEST(showroom_list_bad_lines_report) {
    const ShowroomList::List list = ShowroomList::Parse(
        "tree\n"
        "tree oak birch\n"
        "month\n"
        "month 1 2\n"
        "months 1 x2 -1 3\n");
    CHECK(list.items.empty());
    CHECK_EQ(list.errors.size(), (size_t)6);
    CHECK_EQ(list.months.size(), (size_t)2);   // the good words of the last line: 1 and 3
    CHECK(list.months[0] == 1.0f);
    CHECK(list.months[1] == 3.0f);
    CHECK(list.errors[0].find("line 1:") == 0);
    CHECK(list.errors[4].find("line 5:") == 0);
}

UNIT_TEST(showroom_list_forest_and_bench) {
    const ShowroomList::List list = ShowroomList::Parse(
        "forest Mixed 60 60 2   # the bench forest\n"
        "bench 130\n");
    CHECK(list.errors.empty());
    CHECK(list.items.empty());
    CHECK(list.forest.set);
    CHECK_STREQ(list.forest.species.c_str(), "mixed");
    CHECK_EQ(list.forest.columns, 60);
    CHECK_EQ(list.forest.rows, 60);
    CHECK_EQ(list.forest.spacing, 2);
    CHECK(list.bench.set);
    CHECK(list.bench.dist == 130.0f);
    CHECK(!list.bench.hasYaw);
    CHECK(!list.bench.sweep);
}

UNIT_TEST(showroom_list_bench_yaw_and_sweep) {
    const ShowroomList::List yaw = ShowroomList::Parse("forest pine 10 20 1\nbench 90 135\n");
    CHECK(yaw.errors.empty());
    CHECK_STREQ(yaw.forest.species.c_str(), "pine");
    CHECK_EQ(yaw.forest.columns, 10);
    CHECK_EQ(yaw.forest.rows, 20);
    CHECK_EQ(yaw.forest.spacing, 1);
    CHECK(yaw.bench.hasYaw);
    CHECK(yaw.bench.yaw == 135.0f);
    CHECK(!yaw.bench.sweep);
    const ShowroomList::List sweep = ShowroomList::Parse("bench 130 SWEEP\n");
    CHECK(sweep.errors.empty());
    CHECK(!sweep.forest.set);
    CHECK(sweep.bench.sweep);
    CHECK(!sweep.bench.hasYaw);
    CHECK(sweep.bench.dist == 130.0f);
    const ShowroomList::List both = ShowroomList::Parse("bench 60 -45 sweep\n");
    CHECK(both.errors.empty());
    CHECK(both.bench.sweep);
    CHECK(both.bench.yaw == -45.0f);
}

UNIT_TEST(showroom_list_forest_and_bench_bad_lines_report) {
    const ShowroomList::List list = ShowroomList::Parse(
        "forest oak 60 60\n"      // 1: too few words
        "forest oak 60 60 0\n"    // 2: spacing below 1
        "forest oak 60 60 7\n"    // 3: spacing above 6
        "forest oak 0 60 2\n"     // 4: no columns
        "forest oak 60 201 2\n"   // 5: too many rows
        "forest oak 6x 60 2\n"    // 6: not a number
        "forest birch 4 4 3\n"    // 7: good
        "forest pine 4 4 3\n"     // 8: a second forest
        "bench\n"                 // 9: no distance
        "bench far\n"             // 10: bad distance
        "bench 0.5\n"             // 11: distance below 1
        "bench 100 1 2\n"         // 12: too many words
        "bench 100 east\n"        // 13: bad yaw
        "bench 80\n"              // 14: good
        "bench 90\n");            // 15: a second bench
    CHECK_EQ(list.errors.size(), (size_t)13);
    CHECK(list.forest.set);
    CHECK_STREQ(list.forest.species.c_str(), "birch");
    CHECK_EQ(list.forest.spacing, 3);
    CHECK(list.bench.set);
    CHECK(list.bench.dist == 80.0f);
    CHECK(list.errors[0].find("line 1:") == 0);
    CHECK(list.errors[6].find("line 8:") == 0);
    CHECK(list.errors[12].find("line 15:") == 0);
    CHECK(list.items.empty());
}
