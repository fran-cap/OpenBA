// ============================================================================
// tests/unit/ext_nomad_arrival_test.cpp -- fix.nomad_arrival's rule
// ============================================================================
// src/ext/nomad_arrival/nomad_arrival_rule.h without the world: off, every idle nomad has
// arrived (retail); on, only one near its meeting point, else one retry, then it gives up.
// ============================================================================
#include "unit.h"

#include "ext/nomad_arrival/nomad_arrival_rule.h"

using Ext::NomadArrival::AfterGiveUp;
using Ext::NomadArrival::Decide;
using Ext::NomadArrival::HallStep;
using Ext::NomadArrival::Step;

UNIT_TEST(ext_nomad_arrival_off_is_retail) {
    CHECK(Decide(false, 0.0f, 0.0f, false) == Step::Arrived);
    CHECK(Decide(false, 200.0f, -150.0f, false) == Step::Arrived);
    CHECK(Decide(false, 200.0f, -150.0f, true) == Step::Arrived);
}

UNIT_TEST(ext_nomad_arrival_near_has_arrived) {
    CHECK(Decide(true, 0.0f, 0.0f, false) == Step::Arrived);
    CHECK(Decide(true, 2.5f, -2.5f, false) == Step::Arrived);
    CHECK(Decide(true, 4.0f, 0.0f, true) == Step::Arrived);
}

UNIT_TEST(ext_nomad_arrival_far_retries_then_gives_up) {
    CHECK(Decide(true, 4.5f, 0.0f, false) == Step::Retry);
    CHECK(Decide(true, -3.0f, 3.0f, false) == Step::Retry);
    CHECK(Decide(true, 120.0f, 80.0f, false) == Step::Retry);
    CHECK(Decide(true, 120.0f, 80.0f, true) == Step::GiveUp);
}

UNIT_TEST(ext_nomad_arrival_hall_after_give_up) {
    CHECK(AfterGiveUp(0, 0) == HallStep::Empty);
    CHECK(AfterGiveUp(0, 3) == HallStep::None);
    CHECK(AfterGiveUp(2, 3) == HallStep::None);
    CHECK(AfterGiveUp(3, 3) == HallStep::AllArrived);
}
