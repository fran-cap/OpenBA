// ============================================================================
// tests/unit/ext_stranded_wade_test.cpp -- fix.stranded_wade's counting rule
// ============================================================================
// src/ext/stranded_wade/stranded_wade_rule.h without the world: only a land-to-land zone refusal
// counts, two in a row make a citizen stranded, one normal success (or any other failure) resets
// the count, and the rescue goes home first, then to the town hall when the wading walk home failed.
// ============================================================================
#include "unit.h"

#include "ext/stranded_wade/stranded_wade_rule.h"

using namespace Ext::StrandedWade;

UNIT_TEST(ext_stranded_wade_classify_refusal) {
    // Two land cells (grass 2, road 1) in different zones: the zone mismatch.
    CHECK(ClassifyRefusal(0xe1f, 2, 3, 2, 7) == Outcome::ZoneRefused);
    CHECK(ClassifyRefusal(0xe1f, 1, 3, 10, 7) == Outcome::ZoneRefused);
    // Same zone: not a zone refusal.
    CHECK(ClassifyRefusal(0xe1f, 2, 3, 2, 3) == Outcome::OtherFailed);
    // A zone-less endpoint (types 5-8): RequestPath bypasses the test, so never a zone refusal.
    for (uint8_t t = 5; t <= 8; ++t) {
        CHECK(ClassifyRefusal(0xe1f, t, 0, 2, 7) == Outcome::OtherFailed);
        CHECK(ClassifyRefusal(0xe1f, 2, 3, t, 0) == Outcome::OtherFailed);
    }
    // Zone-less pass flags (the rescue walk's own) bypass the test too.
    CHECK(ClassifyRefusal(kRescueFlags, 2, 3, 2, 7) == Outcome::OtherFailed);
    // A land cell with no zone yet is not a mismatch between two zones.
    CHECK(ClassifyRefusal(0xe1f, 2, 0, 2, 7) == Outcome::OtherFailed);
    CHECK(ClassifyRefusal(0xe1f, 2, 3, 2, 0) == Outcome::OtherFailed);
}

UNIT_TEST(ext_stranded_wade_two_zone_refusals_strand) {
    uint8_t s = 0;
    s = OnOutcome(s, Outcome::ZoneRefused, false);
    CHECK(Count(s) == 1);
    CHECK(!IsStranded(s));
    s = OnOutcome(s, Outcome::ZoneRefused, false);
    CHECK(IsStranded(s));
    s = OnOutcome(s, Outcome::ZoneRefused, false);   // saturates
    CHECK(Count(s) == 2);
    CHECK(IsStranded(s));
}

UNIT_TEST(ext_stranded_wade_success_or_other_failure_resets) {
    uint8_t s = OnOutcome(0, Outcome::ZoneRefused, false);
    s = OnOutcome(s, Outcome::Arrived, false);
    CHECK(Count(s) == 0);
    s = OnOutcome(s, Outcome::ZoneRefused, false);
    s = OnOutcome(s, Outcome::OtherFailed, false);   // breaks "the last two"
    s = OnOutcome(s, Outcome::ZoneRefused, false);
    CHECK(!IsStranded(s));
    // Only failures never strand a citizen whose walks fail for other reasons.
    uint8_t t = 0;
    for (int i = 0; i < 10; ++i) t = OnOutcome(t, Outcome::OtherFailed, false);
    CHECK(!IsStranded(t));
}

UNIT_TEST(ext_stranded_wade_rescue_home_then_hall) {
    uint8_t s = OnOutcome(OnOutcome(0, Outcome::ZoneRefused, false), Outcome::ZoneRefused, false);
    CHECK(RescueTarget(s, true) == Target::Home);
    CHECK(RescueTarget(s, false) == Target::Hall);   // homeless
    s = OnRescuePushed(s, Target::Home);
    CHECK(!IsStranded(s));
    // The wading walk home fails (an empty path): the next rescue goes to the town hall.
    s = OnOutcome(s, Outcome::OtherFailed, true);
    CHECK(Count(s) == 0);
    s = OnOutcome(OnOutcome(s, Outcome::ZoneRefused, false), Outcome::ZoneRefused, false);
    CHECK(IsStranded(s));
    CHECK(RescueTarget(s, true) == Target::Hall);
    s = OnRescuePushed(s, Target::Hall);
    // The walk to the hall fails too: the next cycle tries home again.
    s = OnOutcome(s, Outcome::OtherFailed, true);
    CHECK(RescueTarget(s, true) == Target::Home);
}

UNIT_TEST(ext_stranded_wade_rescue_arrival_clears) {
    uint8_t s = OnOutcome(OnOutcome(0, Outcome::ZoneRefused, false), Outcome::ZoneRefused, false);
    s = OnRescuePushed(s, Target::Home);
    s = OnOutcome(s, Outcome::Arrived, true);
    CHECK(s == 0);
}

UNIT_TEST(ext_stranded_wade_interrupted_rescue_is_forgotten) {
    // The rescue walk was cleared before it ended; the next outcome is a normal walk's.
    uint8_t s = OnOutcome(OnOutcome(0, Outcome::ZoneRefused, false), Outcome::ZoneRefused, false);
    s = OnRescuePushed(s, Target::Home);
    s = OnOutcome(s, Outcome::OtherFailed, false);
    CHECK((s & kPending) == 0);
    CHECK((s & kHomeFailed) == 0);
    CHECK(RescueTarget(s, true) == Target::Home);
}

UNIT_TEST(ext_stranded_wade_goal_in_his_own_zone_needs_no_rescue) {
    // A town citizen whose walks to a target across the river were refused: home is in his zone.
    CHECK(!NeedsRescue(2, 3, 2, 3));
    CHECK(!NeedsRescue(1, 3, 4, 3));
    // Cut off from home: a rescue.
    CHECK(NeedsRescue(2, 2, 4, 3));
    // A zone-less cell on either side (water, shore, mountain) or a cell with no zone: a rescue.
    CHECK(NeedsRescue(7, 0, 2, 3));
    CHECK(NeedsRescue(2, 3, 8, 0));
    CHECK(NeedsRescue(2, 0, 2, 3));
}

UNIT_TEST(ext_stranded_wade_no_target_restarts) {
    uint8_t s = OnOutcome(OnOutcome(0, Outcome::ZoneRefused, false), Outcome::ZoneRefused, false);
    s = OnNoTarget(s);
    CHECK(!IsStranded(s));
}
