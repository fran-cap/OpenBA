// ============================================================================
// tests/unit/ext_save_failure_notice_test.cpp -- fix.save_failure_notice's notice rule
// ============================================================================
// src/ext/save_failure_notice/save_failure_notice_rule.h without the world: the first failure
// after a success (or boot) opens the notice, failures in a row do not, a success re-arms it.
// ============================================================================
#include "unit.h"

#include "ext/save_failure_notice/save_failure_notice_rule.h"

using Ext::SaveFailureNotice::ShouldNotify;

UNIT_TEST(ext_save_failure_notice_success_is_quiet) {
    bool shown = false;
    CHECK(!ShouldNotify(true, shown));
    CHECK(!shown);
}

UNIT_TEST(ext_save_failure_notice_first_failure_notifies_once) {
    bool shown = false;
    CHECK(ShouldNotify(false, shown));
    CHECK(shown);
    CHECK(!ShouldNotify(false, shown));
    CHECK(!ShouldNotify(false, shown));
}

UNIT_TEST(ext_save_failure_notice_success_rearms) {
    bool shown = false;
    CHECK(ShouldNotify(false, shown));
    CHECK(!ShouldNotify(true, shown));
    CHECK(!shown);
    CHECK(ShouldNotify(false, shown));
}
