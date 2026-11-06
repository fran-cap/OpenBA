// ============================================================================
// tests/unit/ext_dynamic_professions_test.cpp -- ext.dynamic_professions' list rules
// ============================================================================
// src/ext/dynamic_professions/professions_math.h without the game.
// ============================================================================
#include "unit.h"
#include "ext/dynamic_professions/professions_math.h"

#include <vector>

using namespace Ext::DynamicProfessions;

namespace {

Usage Make(int pin, int workplaces, int holders) {
    Usage p;
    p.pin = pin;
    p.workplaces = workplaces;
    p.holders = holders;
    return p;
}

}  // namespace

UNIT_TEST(ext_dynamic_professions_laborer_and_builder_always_shown_first) {
    // the game's list: farmer, builder, laborer, miner (the builder and the laborer are not first in it)
    std::vector<Usage> probes = {Make(2, 3, 5), Make(1, 0, 0), Make(0, 0, 0), Make(2, 0, 0)};
    std::vector<int> linger;
    const std::vector<int> shown = VisibleProfessions(probes, linger);
    CHECK_EQ(shown.size(), static_cast<size_t>(3));
    CHECK_EQ(shown[0], 2);   // the laborer
    CHECK_EQ(shown[1], 1);   // the builder
    CHECK_EQ(shown[2], 0);   // the farmer; the miner has neither a workplace nor a holder
}

UNIT_TEST(ext_dynamic_professions_a_workplace_or_a_holder_is_enough) {
    std::vector<Usage> probes = {Make(2, 1, 0), Make(2, 0, 2), Make(2, 0, 0)};
    std::vector<int> linger;
    const std::vector<int> shown = VisibleProfessions(probes, linger);
    CHECK_EQ(shown.size(), static_cast<size_t>(2));
    CHECK_EQ(shown[0], 0);
    CHECK_EQ(shown[1], 1);
}

UNIT_TEST(ext_dynamic_professions_the_rest_keep_the_games_order) {
    std::vector<Usage> probes = {Make(2, 1, 0), Make(2, 1, 0), Make(2, 1, 0), Make(0, 0, 0)};
    std::vector<int> linger;
    const std::vector<int> shown = VisibleProfessions(probes, linger);
    CHECK_EQ(shown.size(), static_cast<size_t>(4));
    CHECK_EQ(shown[0], 3);
    CHECK_EQ(shown[1], 0);
    CHECK_EQ(shown[2], 1);
    CHECK_EQ(shown[3], 2);
}

UNIT_TEST(ext_dynamic_professions_a_row_that_stops_qualifying_lingers_then_goes) {
    std::vector<Usage> probes = {Make(2, 0, 1)};
    std::vector<int> linger;
    CHECK_EQ(VisibleProfessions(probes, linger).size(), static_cast<size_t>(1));
    probes[0].holders = 0;   // the player lowered it to zero and no workplace is left
    int checks = 0;
    while (!VisibleProfessions(probes, linger, 10).empty() && checks < 1000) ++checks;
    CHECK_EQ(checks, kLingerFrames / 10 - 1 + 1);   // it stayed about kLingerFrames, then went
    CHECK(VisibleProfessions(probes, linger, 10).empty());
}

UNIT_TEST(ext_dynamic_professions_a_returning_profession_resets_the_linger) {
    std::vector<Usage> probes = {Make(2, 0, 1)};
    std::vector<int> linger;
    VisibleProfessions(probes, linger);
    probes[0].holders = 0;
    VisibleProfessions(probes, linger, 60);   // half the linger gone
    probes[0].holders = 1;
    VisibleProfessions(probes, linger);       // back: the full linger again
    probes[0].holders = 0;
    VisibleProfessions(probes, linger, 60);
    CHECK_EQ(VisibleProfessions(probes, linger, 10).size(), static_cast<size_t>(1));   // still showing
}

UNIT_TEST(ext_dynamic_professions_spinner_max_is_holders_plus_laborers) {
    CHECK_EQ(SpinnerMax(3, 4), 7);
    CHECK_EQ(SpinnerMax(0, 0), 0);
}
