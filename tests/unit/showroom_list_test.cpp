// ============================================================================
// tests/unit/showroom_list_test.cpp -- the BANISHED_SHOWROOM list parser
// (src/diag/showroom_list.h): building paths, tree lines, month lines, comments, the
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
