// ============================================================================
// tests/unit/phone_seed_test.cpp -- src/platform/android/phone_seed.h (P8)
// ============================================================================
// The phone defaults an existing settings.cfg lacks: only absent keys are added, a key the file has
// keeps its value (0 included), CRLF lines and a missing final newline are read right. Header-only,
// so it runs on every host although the seed itself is Android's.
// ============================================================================
#include "unit.h"
#include "platform/android/phone_seed.h"

#include <string>

namespace {

const char kSeed[] = "GameEdgeScroll=false\nVideoRenderScale=50\next.fps_cap=1\next.fps_cap_rate=60\n";

}  // namespace

UNIT_TEST(phone_seed_adds_only_absent_keys) {
    using Platform::Android::MissingSeedLines;
    // A P7 phone's file: the fps cap keys never written.
    const std::string p7 = "GameEdgeScroll=false\nGameUIScale=100\nVideoRenderScale=25\next.phone_menu=1\n";
    CHECK(MissingSeedLines(p7, kSeed) == "ext.fps_cap=1\next.fps_cap_rate=60\n");
    // The player turned the cap off: kept off, the rate key still added.
    CHECK(MissingSeedLines("ext.fps_cap=0\n", kSeed) ==
          "GameEdgeScroll=false\nVideoRenderScale=50\next.fps_cap_rate=60\n");
    // A file with every key: nothing.
    CHECK(MissingSeedLines(kSeed, kSeed).empty());
    // An empty file: the whole seed.
    CHECK(MissingSeedLines("", kSeed) == kSeed);
}

UNIT_TEST(phone_seed_reads_crlf_and_a_last_line_without_newline) {
    using Platform::Android::MissingSeedLines;
    CHECK(MissingSeedLines("GameEdgeScroll=true\r\nVideoRenderScale=100\r\next.fps_cap=0", kSeed) ==
          "ext.fps_cap_rate=60\n");
    // A key is the whole text before '=': a longer key does not hide a shorter one.
    CHECK(MissingSeedLines("ext.fps_cap_rate=30\n", kSeed) ==
          "GameEdgeScroll=false\nVideoRenderScale=50\next.fps_cap=1\n");
}

UNIT_TEST(phone_seed_reads_keys_as_the_settings_parser_does) {
    using Platform::Android::MissingSeedLines;
    // review F-315: a key the settings parser reads is present, whatever its spelling on disk.
    // Before the fix each of these appended a seed line, which then overrode the player's value
    // (the parser keeps the LAST value of a duplicate key).
    CHECK(MissingSeedLines("VideoRenderScale = 75\n", "VideoRenderScale=50\n").empty());
    CHECK(MissingSeedLines("  VideoRenderScale=75\n", "VideoRenderScale=50\n").empty());
    CHECK(MissingSeedLines("\t VideoRenderScale\t=75\r\n", "VideoRenderScale=50\n").empty());
    CHECK(MissingSeedLines("\xEF\xBB\xBFVideoRenderScale=75\n", "VideoRenderScale=50\n").empty());
    // A comment line names no key: the seed line is still added.
    CHECK(MissingSeedLines("# VideoRenderScale=75\n", "VideoRenderScale=50\n") == "VideoRenderScale=50\n");
    CHECK(MissingSeedLines("#VideoRenderScale=75\n", "VideoRenderScale=50\n") == "VideoRenderScale=50\n");
}
