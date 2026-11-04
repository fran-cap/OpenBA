// ============================================================================
// tests/unit/settings_file_test.cpp — the settings.cfg codec (Phase D.4 SETTINGS E.1)
// ============================================================================
// IO::SettingsFile::Parse / Serialize / Merge standing on their own: the
// escapes round-trip, the reader's leniencies (BOM, CRLF, comments, blank
// lines, spaces around `=`, duplicates, no final newline) and the merge rule
// (disk-only keys survive, the map wins). src/engine/io/settings_file.h has
// the format.
// ============================================================================
#include "unit.h"
#include "io/settings_file.h"

#include <string>

using IO::SettingsFile::Entries;
using IO::SettingsFile::Entry;
using IO::SettingsFile::Escape;
using IO::SettingsFile::Merge;
using IO::SettingsFile::Parse;
using IO::SettingsFile::Serialize;

// CHECK_STREQ keeps a `const char*`, so a temporary std::string's c_str() would
// dangle by the time it is compared: hold both sides first.
#define CHECK_SSTREQ(a, b) do { const std::string _sa = (a); const std::string _sb = (b);     CHECK_STREQ(_sa.c_str(), _sb.c_str()); } while (0)

namespace {

// Serialize one entry, Parse it back: exactly one entry, unchanged.
bool RoundTrips(const std::string& key, const std::string& value) {
    const std::string text = Serialize(Entries{Entry{key, value}});
    const Entries back = Parse(text);
    return back.size() == 1 && back[0].key == key && back[0].value == value;
}

}  // namespace

UNIT_TEST(settings_file_plain_entry) {
    const std::string text = Serialize(Entries{Entry{"VideoWidth", "1280"}});
    CHECK_STREQ(text.c_str(), "VideoWidth=1280\n");
    const Entries e = Parse(text);
    CHECK_EQ(e.size(), size_t(1));
    CHECK_STREQ(e[0].key.c_str(), "VideoWidth");
    CHECK_STREQ(e[0].value.c_str(), "1280");
}

UNIT_TEST(settings_file_escape_spellings) {
    CHECK_SSTREQ(Escape("a=b", true), "a\\=b");
    CHECK_SSTREQ(Escape("a=b", false), "a=b");
    CHECK_SSTREQ(Escape("a\\b", false), "a\\\\b");
    CHECK_SSTREQ(Escape("a\nb\rc\td", false), "a\\nb\\rc\\td");
    CHECK_SSTREQ(Escape(" a b ", false), "\\sa b\\s");
    CHECK_SSTREQ(Escape(" ", false), "\\s");
    CHECK_SSTREQ(Escape("#k#", true), "\\#k#");
    CHECK_SSTREQ(Escape("#v", false), "#v");
}

UNIT_TEST(settings_file_round_trips_specials_in_key_and_value) {
    CHECK(RoundTrips("ModEnabled_a=b", "x=y=z"));
    CHECK(RoundTrips("back\\slash", "C:\\Games\\Banished\\"));
    CHECK(RoundTrips("line\nfeed", "two\nlines"));
    CHECK(RoundTrips("carriage\rreturn", "a\rb"));
    CHECK(RoundTrips("tab\tkey", "\tlead and trail\t"));
    CHECK(RoundTrips(" lead", "trail "));
    CHECK(RoundTrips("  both  ", "  both  "));
    CHECK(RoundTrips("ModEnabled_My Mod", "true"));
    CHECK(RoundTrips("#hash", "#value"));
    CHECK(RoundTrips("\\=\\", "\\\\s"));
    // UTF-8: bytes >= 0x80 pass through untouched.
    CHECK(RoundTrips("ModEnabled_\xC3\xA9t\xC3\xA9 \xE6\x9D\x91", "\xF0\x9F\x8C\xB2"));
}

UNIT_TEST(settings_file_empty_key_and_value) {
    CHECK(RoundTrips("Key", ""));
    CHECK_SSTREQ(Serialize(Entries{Entry{"Key", ""}}), "Key=\n");
    const Entries e = Parse("Key=\n");
    CHECK_EQ(e.size(), size_t(1));
    CHECK_STREQ(e[0].value.c_str(), "");
}

UNIT_TEST(settings_file_reader_skips_bom_comments_blank_lines) {
    const char text[] =
        "\xEF\xBB\xBF"
        "# a comment\n"
        "\n"
        "   \t\n"
        "   # an indented comment\n"
        "A=1\n"
        "no equals sign here\n"
        "B=2\n";
    const Entries e = Parse(text, sizeof(text) - 1);
    CHECK_EQ(e.size(), size_t(2));
    CHECK_STREQ(e[0].key.c_str(), "A");
    CHECK_STREQ(e[0].value.c_str(), "1");
    CHECK_STREQ(e[1].key.c_str(), "B");
    CHECK_STREQ(e[1].value.c_str(), "2");
}

// Review F-085 (fuzz input crashes/settings_file/bom_key_roundtrip): a doubled BOM keeps
// the second one as the first bytes of the key. Serialize then wrote that key at offset 0,
// and the next Parse stripped it as a BOM, renaming the key from "\xEF\xBB\xBFk" to "k".
UNIT_TEST(settings_file_bom_leading_key_round_trips) {
    const unsigned char fuzz[] = {0xef, 0xbb, 0xbf, 0xef, 0xbb, 0xbf, 0x6b, 0x3d, 0x76};
    const Entries e = Parse(reinterpret_cast<const char*>(fuzz), sizeof(fuzz));
    CHECK_EQ(e.size(), size_t(1));
    CHECK_SSTREQ(e[0].key, "\xEF\xBB\xBFk");
    CHECK_SSTREQ(e[0].value, "v");
    const Entries back = Parse(Serialize(e));
    CHECK_EQ(back.size(), size_t(1));
    CHECK_SSTREQ(back[0].key, "\xEF\xBB\xBFk");
    CHECK_SSTREQ(back[0].value, "v");
    CHECK(RoundTrips("\xEF\xBB\xBF", "v"));
    // A file whose first key does not start with the BOM bytes is still written without one.
    CHECK_SSTREQ(Serialize(Entries{Entry{"k\xEF\xBB\xBF", "v"}}), "k\xEF\xBB\xBF=v\n");
}

UNIT_TEST(settings_file_reader_accepts_crlf_and_no_final_newline) {
    const Entries e = Parse(std::string("A=1\r\nB=two words\r\nC=3"));
    CHECK_EQ(e.size(), size_t(3));
    CHECK_STREQ(e[0].value.c_str(), "1");
    CHECK_STREQ(e[1].value.c_str(), "two words");
    CHECK_STREQ(e[2].key.c_str(), "C");
    CHECK_STREQ(e[2].value.c_str(), "3");
}

UNIT_TEST(settings_file_reader_trims_around_equals) {
    const Entries e = Parse(std::string("  Video Width \t=\t 1280  \n"));
    CHECK_EQ(e.size(), size_t(1));
    CHECK_STREQ(e[0].key.c_str(), "Video Width");
    CHECK_STREQ(e[0].value.c_str(), "1280");
    // An escaped space or tab is content, not trimmed.
    const Entries f = Parse(std::string("\\sK\\s = \\tv\\s \n"));
    CHECK_EQ(f.size(), size_t(1));
    CHECK_STREQ(f[0].key.c_str(), " K ");
    CHECK_STREQ(f[0].value.c_str(), "\tv ");
}

UNIT_TEST(settings_file_reader_splits_on_first_unescaped_equals) {
    const Entries e = Parse(std::string("a\\=b=c=d\n"));
    CHECK_EQ(e.size(), size_t(1));
    CHECK_STREQ(e[0].key.c_str(), "a=b");
    CHECK_STREQ(e[0].value.c_str(), "c=d");
}

UNIT_TEST(settings_file_reader_unknown_and_trailing_escapes) {
    const Entries e = Parse(std::string("K=a\\qb\\\n"));
    CHECK_EQ(e.size(), size_t(1));
    CHECK_STREQ(e[0].value.c_str(), "a\\qb\\");
}

UNIT_TEST(settings_file_duplicate_key_last_wins) {
    const Entries e = Parse(std::string("A=1\nB=2\nA=3\n"));
    CHECK_EQ(e.size(), size_t(2));
    CHECK_STREQ(e[0].key.c_str(), "A");      // first position kept
    CHECK_STREQ(e[0].value.c_str(), "3");    // last value
    CHECK_STREQ(e[1].key.c_str(), "B");
}

UNIT_TEST(settings_file_serialize_sorts_by_strcmp) {
    const Entries e{
        Entry{"VideoWidth", "1"},
        Entry{"Ach12", "true"},
        Entry{"ModEnabled_b", "true"},
        Entry{"Ach2", "true"},
        Entry{"AudioUIVolume", "100"},
        Entry{"ModEnabled_B", "false"},
    };
    CHECK_SSTREQ(Serialize(e),
                "Ach12=true\n"
                "Ach2=true\n"
                "AudioUIVolume=100\n"
                "ModEnabled_B=false\n"
                "ModEnabled_b=true\n"
                "VideoWidth=1\n");
    CHECK_SSTREQ(Serialize(Entries{}), "");
}

UNIT_TEST(settings_file_merge_keeps_disk_only_keys_and_map_wins) {
    const Entries disk{
        Entry{"Ach3", "true"},
        Entry{"VideoWidth", "800"},
        Entry{"ModEnabled_Foo", "true"},
    };
    const Entries map{
        Entry{"VideoWidth", "1280"},
        Entry{"AudioMusicVolume", "50"},
    };
    const Entries m = Merge(disk, map);
    CHECK_EQ(m.size(), size_t(4));
    CHECK_STREQ(m[0].key.c_str(), "Ach3");
    CHECK_STREQ(m[0].value.c_str(), "true");
    CHECK_STREQ(m[1].key.c_str(), "AudioMusicVolume");
    CHECK_STREQ(m[1].value.c_str(), "50");
    CHECK_STREQ(m[2].key.c_str(), "ModEnabled_Foo");
    CHECK_STREQ(m[2].value.c_str(), "true");
    CHECK_STREQ(m[3].key.c_str(), "VideoWidth");
    CHECK_STREQ(m[3].value.c_str(), "1280");
    // An empty map (after -reset) writes the disk back unchanged.
    CHECK_SSTREQ(Serialize(Merge(disk, Entries{})), Serialize(disk));
}

UNIT_TEST(settings_file_parse_serialize_is_stable) {
    const std::string text =
        "Ach1=true\n"
        "InputCamera Forward=87\n"
        "ModEnabled_ odd\\=name\\s=true\n"
        "VideoSettings=true\n";
    CHECK_SSTREQ(Serialize(Parse(text)), text);
}
