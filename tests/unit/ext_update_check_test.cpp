// ============================================================================
// tests/unit/ext_update_check_test.cpp -- ext.update_check's pure rules
// ============================================================================
// src/ext/update_check/release_tag.h without the world: a release tag packed and printed, the tag
// found in a releases/latest answer, the once-a-day rule and the menu line's text.
// ============================================================================
#include "unit.h"

#include "ext/update_check/release_tag.h"
#include "ext/registry.h"

#include <string>

using Ext::UpdateCheck::DueToday;
using Ext::UpdateCheck::kNoVersion;
using Ext::UpdateCheck::LatestFromAnswer;
using Ext::UpdateCheck::NoticeSuffix;
using Ext::UpdateCheck::PackVersion;
using Ext::UpdateCheck::VersionText;

UNIT_TEST(ext_update_check_pack_version_takes_a_tag_or_a_bare_version) {
    CHECK(PackVersion("1.0.4") == 1000004);
    CHECK(PackVersion("v1.0.4") == 1000004);
    CHECK(PackVersion("v12.34.567") == 12034567);
    CHECK(PackVersion("v1.10.0") > PackVersion("v1.9.9"));
    CHECK(PackVersion("v2.0.0") > PackVersion("v1.999.999"));
}

UNIT_TEST(ext_update_check_pack_version_refuses_anything_else) {
    CHECK(PackVersion(nullptr) == kNoVersion);
    CHECK(PackVersion("") == kNoVersion);
    CHECK(PackVersion("v") == kNoVersion);
    CHECK(PackVersion("1.0") == kNoVersion);
    CHECK(PackVersion("1.0.") == kNoVersion);
    CHECK(PackVersion("1.0.4.1") == kNoVersion);
    CHECK(PackVersion("v1.0.4-rc1") == kNoVersion);
    CHECK(PackVersion("V1.0.4") == kNoVersion);
    CHECK(PackVersion(" 1.0.4") == kNoVersion);
    CHECK(PackVersion("1.0.1000") == kNoVersion);
    CHECK(PackVersion("nightly") == kNoVersion);
    CHECK(PackVersion("0.0.0") == kNoVersion);
}

UNIT_TEST(ext_update_check_version_text_round_trips) {
    CHECK(VersionText(PackVersion("v1.0.4")) == "1.0.4");
    CHECK(VersionText(PackVersion("12.34.567")) == "12.34.567");
    CHECK(VersionText(kNoVersion).empty());
    CHECK(VersionText(-5).empty());
}

UNIT_TEST(ext_update_check_answer_gives_the_tag) {
    const std::string body =
        "{\"url\":\"https://api.github.com/repos/o/r/releases/1\",\"id\":1,"
        "\"author\":{\"login\":\"someone\"},\"tag_name\":\"v1.0.5\",\"name\":\"v1.0.5\","
        "\"draft\":false,\"prerelease\":false,\"body\":\"notes\"}";
    CHECK(LatestFromAnswer(body) == PackVersion("1.0.5"));
    // The pretty-printed form: blanks round the colon.
    CHECK(LatestFromAnswer("{\n  \"tag_name\" :\n  \"v2.3.4\"\n}") == PackVersion("2.3.4"));
}

UNIT_TEST(ext_update_check_answer_skips_a_quoted_key_in_the_notes) {
    // An escaped "tag_name" inside an earlier string is not the member.
    const std::string body =
        "{\"name\":\"about \\\"tag_name\\\":\\\"v9.9.9\\\"\",\"tag_name\":\"v1.0.5\"}";
    CHECK(LatestFromAnswer(body) == PackVersion("1.0.5"));
}

UNIT_TEST(ext_update_check_answer_without_a_release_version_is_none) {
    CHECK(LatestFromAnswer("") == kNoVersion);
    CHECK(LatestFromAnswer("{\"message\":\"Not Found\"}") == kNoVersion);
    CHECK(LatestFromAnswer("{\"message\":\"API rate limit exceeded\"}") == kNoVersion);
    CHECK(LatestFromAnswer("{\"tag_name\":null}") == kNoVersion);
    CHECK(LatestFromAnswer("{\"tag_name\":\"nightly\"}") == kNoVersion);
    CHECK(LatestFromAnswer("{\"tag_name\":\"v1.0.5") == kNoVersion);   // cut off inside the value
    CHECK(LatestFromAnswer("{\"tag_name\"") == kNoVersion);            // cut off after the key
    CHECK(LatestFromAnswer("<html>captive portal</html>") == kNoVersion);
}

UNIT_TEST(ext_update_check_asks_once_a_day) {
    CHECK(DueToday(0, 20778));        // never asked
    CHECK(!DueToday(20778, 20778));   // asked today
    CHECK(DueToday(20777, 20778));    // asked yesterday
    CHECK(DueToday(20779, 20778));    // the clock went back: ask again
}

UNIT_TEST(ext_update_check_notice_only_for_a_newer_release) {
    const int32_t mine = PackVersion("1.0.4");
    CHECK(NoticeSuffix(mine, PackVersion("v1.0.5")) == " - v1.0.5 available");
    CHECK(NoticeSuffix(mine, PackVersion("v1.1.0")) == " - v1.1.0 available");
    CHECK(NoticeSuffix(mine, mine).empty());
    CHECK(NoticeSuffix(mine, PackVersion("v1.0.3")).empty());   // an exe built ahead of the release
    CHECK(NoticeSuffix(mine, kNoVersion).empty());              // nothing seen yet
    CHECK(NoticeSuffix(kNoVersion, PackVersion("v1.0.5")).empty());
}

// This exe's own version is one PackVersion reads: a CMake VERSION it refuses would switch the
// notice off for good without a failing build.
UNIT_TEST(ext_update_check_this_build_has_a_release_version) {
    CHECK(PackVersion(OPENBA_VERSION) != kNoVersion);
}

UNIT_TEST(ext_update_check_row_is_class_a_default_on) {
    const Ext::Entry* e = Ext::FindEntry("update_check");
    CHECK(e != nullptr);
    if (e != nullptr) {
        CHECK(e->cls == Ext::Class::A);
        CHECK(e->defaultOn);
        CHECK(e->formatVersion == 0);
        CHECK(!Ext::Experimental(*e));
    }
}
