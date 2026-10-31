// ============================================================================
// tests/unit/game_data_check_test.cpp -- the pure half of the missing-game-data check
// ============================================================================
// Issue #72. src/app/game_data_check.cpp: the verdict from what WinData holds (missing, empty,
// packages without data0.pkg, present), the /pathpkg and /bin switches that skip the check, and
// the message naming the folder. No disk, no platform: runs on every host.
// ============================================================================
#include "unit.h"

#include <string>
#include <vector>

#include "game_data_check.h"

using GameData::Judge;
using GameData::Verdict;

UNIT_TEST(game_data_folder_missing) {
    CHECK(Judge(false, {}) == Verdict::FolderMissing);
    // Names cannot come from a folder that is not there; a missing folder wins regardless.
    CHECK(Judge(false, {"data0.pkg"}) == Verdict::FolderMissing);
}

UNIT_TEST(game_data_folder_empty) {
    // The Android first start: the app made WinData/ and nothing is in it yet.
    CHECK(Judge(true, {}) == Verdict::NoPackages);
}

UNIT_TEST(game_data_packages_without_base) {
    CHECK(Judge(true, {"patch0.pkg", "patch1.pkg"}) == Verdict::NoBasePackage);
    CHECK(Judge(true, {"data1.pkg"}) == Verdict::NoBasePackage);
}

UNIT_TEST(game_data_packages_present) {
    CHECK(Judge(true, {"data0.pkg", "data1.pkg", "patch0.pkg", "patch1.pkg"}) == Verdict::Present);
    CHECK(Judge(true, {"data0.pkg"}) == Verdict::Present);
    // FindFiles matches without case on every platform; so does the check.
    CHECK(Judge(true, {"DATA0.PKG"}) == Verdict::Present);
    CHECK(Judge(true, {"Data0.Pkg"}) == Verdict::Present);
    CHECK(Judge(true, {"data0.pkg.bak"}) == Verdict::NoBasePackage);
}

UNIT_TEST(game_data_verdict_names) {
    CHECK_STREQ(GameData::VerdictName(Verdict::Present), "present");
    CHECK_STREQ(GameData::VerdictName(Verdict::FolderMissing), "folder-missing");
    CHECK_STREQ(GameData::VerdictName(Verdict::NoPackages), "no-packages");
    CHECK_STREQ(GameData::VerdictName(Verdict::NoBasePackage), "no-data0");
}

UNIT_TEST(game_data_command_line_switches) {
    using GameData::CommandLineMovesPackages;
    CHECK(!CommandLineMovesPackages(nullptr));
    CHECK(!CommandLineMovesPackages(""));
    CHECK(!CommandLineMovesPackages("/windowed /pathres:x"));
    CHECK(CommandLineMovesPackages("/pathpkg:D:\\Banished\\WinData\\"));
    CHECK(CommandLineMovesPackages("/windowed /PathPkg:x"));
    CHECK(CommandLineMovesPackages("/bin"));
    CHECK(CommandLineMovesPackages("/ bin"));   // ParseCommand skips blanks after the '/'
    CHECK(!CommandLineMovesPackages("/binary"));
    CHECK(!CommandLineMovesPackages("pathpkg"));   // no '/': not a switch
}

UNIT_TEST(game_data_message_names_the_folder) {
    GameData::ProbeResult r;
    r.verdict = Verdict::NoPackages;
    r.folder = "C:\\Games\\OpenBA\\WinData";
    const std::string text = GameData::MessageText(r, "HELP-TEXT");
    CHECK(text.find("C:\\Games\\OpenBA\\WinData") != std::string::npos);
    CHECK(text.find("No game data (.pkg files) in:") == 0);
    CHECK(text.find("HELP-TEXT") != std::string::npos);

    r.verdict = Verdict::FolderMissing;
    CHECK(GameData::MessageText(r, nullptr).find("does not exist") != std::string::npos);
    r.verdict = Verdict::NoBasePackage;
    CHECK(GameData::MessageText(r, nullptr).find("No data0.pkg in:") == 0);
}
