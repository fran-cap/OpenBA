// ============================================================================
// tests/unit/ext_building_finder_test.cpp -- ext.building_finder's search, favorites and limit note
// ============================================================================
// src/ext/building_finder/finder_math.h without the game.
// ============================================================================
#include "unit.h"
#include "ext/building_finder/finder_math.h"

#include <string>
#include <vector>

using namespace Ext::BuildingFinder;

UNIT_TEST(ext_building_finder_words_ignore_case_and_separators) {
    const std::vector<std::string> w = QueryWords("  Stone,  HOUSE\tx ");
    CHECK_EQ(w.size(), static_cast<size_t>(3));
    CHECK(w[0] == "stone");
    CHECK(w[1] == "house");
    CHECK(w[2] == "x");
    CHECK(QueryWords("").empty());
    CHECK(QueryWords("   ").empty());
}

UNIT_TEST(ext_building_finder_words_are_capped) {
    const std::vector<std::string> w = QueryWords("a b c d e f g h i j k l");
    CHECK_EQ(w.size(), kMaxQueryWords);
}

UNIT_TEST(ext_building_finder_match_needs_every_word) {
    CHECK(Matches("Stone House  Housing", ""));
    CHECK(Matches("Stone House  Housing", "stone"));
    CHECK(Matches("Stone House  Housing", "HOUS stone"));
    CHECK(Matches("Stone House  Housing", "housing hou"));
    CHECK(!Matches("Stone House  Housing", "stone barn"));
    CHECK(!Matches("", "x"));
}

UNIT_TEST(ext_building_finder_favorites_round_trip) {
    std::vector<std::string> f;
    CHECK(ToggleFavorite(f, "Building/House.rsc"));
    CHECK(ToggleFavorite(f, "Building/Barn.rsc"));
    CHECK(IsFavorite(f, "Building/House.rsc"));
    const std::vector<std::string> back = ParseFavorites(FormatFavorites(f));
    CHECK(back == f);
    CHECK(!ToggleFavorite(f, "Building/House.rsc"));
    CHECK(!IsFavorite(f, "Building/House.rsc"));
    CHECK_EQ(f.size(), static_cast<size_t>(1));
}

UNIT_TEST(ext_building_finder_parse_skips_noise_and_repeats) {
    const std::vector<std::string> f =
        ParseFavorites("# a comment\r\n\r\n  Building/House.rsc  \r\nBuilding/Barn.rsc\n"
                       "Building/House.rsc\n\t\n#x\n");
    CHECK_EQ(f.size(), static_cast<size_t>(2));
    CHECK(f[0] == "Building/House.rsc");
    CHECK(f[1] == "Building/Barn.rsc");
    CHECK(ParseFavorites("").empty());
    CHECK(ParseFavorites("no newline at the end").size() == 1);
}

UNIT_TEST(ext_building_finder_favorites_are_capped) {
    std::string text;
    for (size_t i = 0; i < kMaxFavorites + 50; ++i) {
        text += "Building/B" + std::to_string(i) + ".rsc\n";
    }
    CHECK_EQ(ParseFavorites(text).size(), kMaxFavorites);
    std::vector<std::string> f = ParseFavorites(text);
    CHECK(!ToggleFavorite(f, "Building/New.rsc"));   // full: not added
    CHECK(!IsFavorite(f, "Building/New.rsc"));
}

UNIT_TEST(ext_building_finder_limit_note) {
    CHECK(LimitNote(0, 1).empty());
    CHECK(LimitNote(5, -1).empty());
    CHECK(LimitNote(2, 3).empty());
    CHECK(LimitNote(3, 3) == "  (limit reached: 3 of 3)");
    CHECK(LimitNote(4, 3) == "  (limit reached: 4 of 3)");
    CHECK(AtLimit(1, 1));
    CHECK(!AtLimit(0, 1));
    CHECK(!AtLimit(9, -1));
}

UNIT_TEST(ext_building_finder_hotkey) {
    CHECK(HotkeyAllowed('H') && HotkeyAllowed('7') && HotkeyAllowed(0x74));
    CHECK(!HotkeyAllowed(0x1b) && !HotkeyAllowed(' ') && !HotkeyAllowed(0x7c));
    CHECK(HotkeyName('H') == "H" && HotkeyName('7') == "7");
    CHECK(HotkeyName(0x70) == "F1" && HotkeyName(0x7b) == "F12");
    CHECK(HotkeyName(0x1b) == "?");
}

UNIT_TEST(ext_building_finder_clip) {
    CHECK(Clip("short", 10) == "short");
    CHECK(Clip("exactly10!", 10) == "exactly10!");
    CHECK(Clip("a much longer name", 10) == "a much ...");
    CHECK(Clip("abcdef", 3) == "abc");
    CHECK(Clip("", 5).empty());
}

UNIT_TEST(ext_building_finder_leaf_name) {
    CHECK(LeafName("Building/Housing/StoneHouse.rsc") == "StoneHouse");
    CHECK(LeafName("Building\\Barn.rsc") == "Barn");
    CHECK(LeafName("Barn") == "Barn");
    CHECK(LeafName("").empty());
}
