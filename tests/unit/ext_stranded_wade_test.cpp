// ============================================================================
// tests/unit/ext_stranded_wade_test.cpp -- fix.stranded_wade's stranded rule
// ============================================================================
// src/ext/stranded_wade/stranded_wade_rule.h without the world. A citizen is stranded when he stands
// on zoned land outside his anchor zone (home, or the town hall when homeless) and, since he entered
// that zone, a walk of his toward the anchor zone was refused on the land-to-land zone mismatch. It
// clears when he stands in the anchor zone; a success inside his zone, or a refusal toward another
// zone, does not reset it. The rescue goes home first, then to the town hall.
// ============================================================================
#include "unit.h"

#include "ext/stranded_wade/stranded_wade_rule.h"

using namespace Ext::StrandedWade;

namespace {

constexpr uint8_t kGrass = 2;
constexpr uint8_t kShore = 7;
constexpr uint8_t kWater = 8;
constexpr uint16_t kTown = 3;     // the anchor zone (his home's)
constexpr uint16_t kIsland = 2;   // the zone across the river
constexpr uint16_t kFar = 5;      // a third zone

// One walk from the island toward home, refused on the zone test.
State RefusedHome(State s) {
    return OnRefusal(s, 0xe1f, kGrass, kIsland, kGrass, kTown, kTown);
}

}  // namespace

UNIT_TEST(ext_stranded_wade_zone_refusal) {
    CHECK(IsZoneRefusal(0xe1f, kGrass, kIsland, kGrass, kTown));
    CHECK(IsZoneRefusal(0xe1f, 1, kIsland, 10, kTown));
    CHECK(!IsZoneRefusal(0xe1f, kGrass, kTown, kGrass, kTown));
    // A zone-less endpoint (types 5-8) or pass flag: RequestPath bypasses the test.
    for (uint8_t t = 5; t <= 8; ++t) {
        CHECK(!IsZoneRefusal(0xe1f, t, 0, kGrass, kTown));
        CHECK(!IsZoneRefusal(0xe1f, kGrass, kIsland, t, 0));
    }
    CHECK(!IsZoneRefusal(kRescueFlags, kGrass, kIsland, kGrass, kTown));
    CHECK(!IsZoneRefusal(0xe1f, kGrass, 0, kGrass, kTown));
}

UNIT_TEST(ext_stranded_wade_one_refusal_home_strands) {
    State s;
    CHECK(!IsStranded(s, kGrass, kIsland, kTown));
    s = RefusedHome(s);
    CHECK(IsStranded(s, kGrass, kIsland, kTown));
    CHECK(s.zone == kIsland);
}

UNIT_TEST(ext_stranded_wade_success_in_his_zone_does_not_reset) {
    State s = RefusedHome(State());
    s = OnWalkEnd(s, true, false);   // a wander walk on the island arrives
    s = OnWalkEnd(s, false, false);  // another fails for some other reason
    CHECK(IsStranded(s, kGrass, kIsland, kTown));
}

UNIT_TEST(ext_stranded_wade_refusal_to_another_zone_does_not_count) {
    // On the island, a walk toward a third zone is refused: not toward the anchor.
    State s = OnRefusal(State(), 0xe1f, kGrass, kIsland, kGrass, kFar, kTown);
    CHECK(!IsStranded(s, kGrass, kIsland, kTown));
    // ... and does not reset a record already made.
    s = RefusedHome(State());
    s = OnRefusal(s, 0xe1f, kGrass, kIsland, kGrass, kFar, kTown);
    CHECK(IsStranded(s, kGrass, kIsland, kTown));
}

UNIT_TEST(ext_stranded_wade_home_in_his_own_zone_is_never_stranded) {
    // A town citizen whose walks to a target across the river are refused: his home is in his zone.
    State s;
    for (int i = 0; i < 45; ++i) s = OnRefusal(s, 0xe1f, kGrass, kTown, kGrass, kIsland, kTown);
    CHECK(!IsStranded(s, kGrass, kTown, kTown));
    // A citizen living across the river by the player's choice: his anchor is the island.
    State t;
    for (int i = 0; i < 10; ++i) t = OnRefusal(t, 0xe1f, kGrass, kIsland, kGrass, kTown, kIsland);
    CHECK(!IsStranded(t, kGrass, kIsland, kIsland));
}

UNIT_TEST(ext_stranded_wade_clears_in_the_anchor_zone_and_goes_stale_elsewhere) {
    State s = RefusedHome(State());
    State home = Settle(s, kGrass, kTown, kTown);
    CHECK((home.flags & kRefused) == 0);
    CHECK(!IsStranded(home, kGrass, kIsland, kTown));
    // Standing on another zone than the one the refusal was made in: stale.
    State moved = Settle(s, kGrass, kFar, kTown);
    CHECK(!IsStranded(moved, kGrass, kFar, kTown));
    // On water or shore (no zone) the record is kept, but he is not stranded there.
    State wading = Settle(s, kWater, 0, kTown);
    CHECK((wading.flags & kRefused) != 0);
    CHECK(!IsStranded(wading, kShore, 0, kTown));
    // No anchor (homeless and no town hall): never stranded.
    CHECK(!IsStranded(s, kGrass, kIsland, 0));
}

UNIT_TEST(ext_stranded_wade_rescue_anchor_then_fallback) {
    State s = RefusedHome(State());
    CHECK(RescueTarget(s) == Target::Anchor);
    s = OnRescuePushed(s, Target::Anchor);
    CHECK((s.flags & kPending) != 0);
    // The wading walk home fails (an empty path): the next rescue goes to the town hall.
    s = OnWalkEnd(s, false, true);
    CHECK((s.flags & kPending) == 0);
    CHECK(IsStranded(s, kGrass, kIsland, kTown));
    CHECK(RescueTarget(s) == Target::Fallback);
    s = OnRescuePushed(s, Target::Fallback);
    // The walk to the hall fails too: the record is dropped, the next rescue tries home again.
    s = OnWalkEnd(s, false, true);
    CHECK(!IsStranded(s, kGrass, kIsland, kTown));
    s = RefusedHome(s);
    CHECK(RescueTarget(s) == Target::Anchor);
}

UNIT_TEST(ext_stranded_wade_rescue_arrival) {
    State s = OnRescuePushed(RefusedHome(State()), Target::Anchor);
    s = OnWalkEnd(s, true, true);
    CHECK((s.flags & (kPending | kAnchorFailed)) == 0);
    // Home is the anchor zone: settling there clears the record.
    s = Settle(s, kGrass, kTown, kTown);
    CHECK(s.flags == 0);
}

UNIT_TEST(ext_stranded_wade_interrupted_rescue_is_forgotten) {
    // The rescue walk was cleared before it ended; the next outcome is a normal walk's.
    State s = OnRescuePushed(RefusedHome(State()), Target::Anchor);
    s = OnWalkEnd(s, false, false);
    CHECK((s.flags & kPending) == 0);
    CHECK((s.flags & kAnchorFailed) == 0);
    CHECK(RescueTarget(s) == Target::Anchor);
}

UNIT_TEST(ext_stranded_wade_hall_in_his_zone_needs_no_rescue) {
    CHECK(!NeedsRescue(kGrass, kIsland, 4, kIsland));
    CHECK(NeedsRescue(kGrass, kIsland, 4, kTown));
    CHECK(NeedsRescue(kShore, 0, kGrass, kTown));
}

UNIT_TEST(ext_stranded_wade_no_target_drops_the_record) {
    State s = OnNoTarget(RefusedHome(State()));
    CHECK(!IsStranded(s, kGrass, kIsland, kTown));
}

UNIT_TEST(ext_stranded_wade_anchor_order) {
    CHECK(ChooseAnchor(true, true, true, true) == AnchorKind::Home);
    CHECK(ChooseAnchor(false, true, true, true) == AnchorKind::LostHome);
    CHECK(ChooseAnchor(false, false, true, true) == AnchorKind::Hall);
    CHECK(ChooseAnchor(false, false, false, true) == AnchorKind::Storage);
    CHECK(ChooseAnchor(false, false, false, false) == AnchorKind::None);
}

UNIT_TEST(ext_stranded_wade_lost_home_is_remembered_until_home_zone_or_new_home) {
    // SetHome(nullptr) on a housed citizen remembers that home's cell (+ 1).
    uint32_t lost = OnHomeChange(0, true, 1234, false);
    CHECK(lost == 1235);
    // Another SetHome(nullptr) with no home keeps it.
    CHECK(OnHomeChange(lost, false, -1, false) == lost);
    // A new home forgets it.
    CHECK(OnHomeChange(lost, false, -1, true) == 0);
    CHECK(OnHomeChange(lost, true, 99, true) == 0);
    // Standing on land of the lost home's zone forgets it; elsewhere, or on water, it is kept.
    CHECK(SettleLostHome(lost, kGrass, kTown, kTown) == 0);
    CHECK(SettleLostHome(lost, kGrass, kIsland, kTown) == lost);
    CHECK(SettleLostHome(lost, kWater, 0, kTown) == lost);
}

UNIT_TEST(ext_stranded_wade_lost_home_anchor_strands_the_river_citizen) {
    // His walk home was refused from the island, then retail dropped the unreachable home: the lost
    // home's zone is the anchor, he is still stranded and the rescue goes to it.
    State s = RefusedHome(State());
    CHECK(IsStranded(s, kGrass, kIsland, kTown));
    CHECK(RescueTarget(s) == Target::Anchor);
}
