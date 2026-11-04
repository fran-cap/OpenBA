// ============================================================================
// tests/unit/review_tutorial_bounds_test.cpp -- review 2026-11-03, F-113 / F-114 / F-115 / F-165
//
// The index and arithmetic checks the tutorial and StatGoal bodies make on values that come
// from a tutorial resource, an achievement resource or a save. The bodies themselves
// (tutorial_wave.cpp, tutorial_next_scenario.cpp, statvaluedispatch.cpp) need a live world and
// the UI and do not link into this target; each of them calls the header helper tested here at
// the site the finding names, so the helper IS the check that site makes.
//
//   F-115  TutorialInstance::StepAt: RefreshStepUI / StepExecute read steps[currentStep], and a
//          save can hold currentStep == steps._size (the value after the last step) or < 0.
//   F-113  TutorialInstance::ScenarioIndexValid: LoadNextScenario indexes the scenario list
//          with Tutorial::currentStep (+0x38), tested for < 0 only.
//   F-114  TutorialSetupDialog::ToolbarCellInRange: OpenHighlightDialog indexes the template
//          bucket's entities with toolbarIndex (+0x70), tested against the count only.
//   F-165  StatGoal::ProductionWindowLo / Hi: ((year - statSubType) - 1) * 0x30 and
//          year * 0x30 - 0x30 + frac, int32 in retail (wrapping x86 arithmetic), signed
//          overflow in C++. Evaluated in a constant expression, so an overflow fails the build.
// ============================================================================
#include "unit.h"

#include "Tutorial/TutorialInstance.h"
#include "Tutorial/TutorialSetupDialog.h"
#include "Goal/StatGoal.h"

#include <climits>
#include <cstdint>

namespace {

// A one-step steps array whose buffer has a trap entry on each side of the real one, so
// an out-of-range read lands on a known pointer instead of foreign heap.
struct StepsFixture {
    TutorialStep* buffer[3];
    Collection::Array<TutorialStep*> steps;
    TutorialStep* real = reinterpret_cast<TutorialStep*>(uintptr_t{0x1000});
    TutorialStep* trap = reinterpret_cast<TutorialStep*>(uintptr_t{0xBAD0});

    StepsFixture() {
        buffer[0] = trap;
        buffer[1] = real;
        buffer[2] = trap;
        steps._data = &buffer[1];
        steps._size = 1;
        steps._capacity = 1;
    }
    ~StepsFixture() {
        steps._data = nullptr;
        steps._size = 0;
        steps._capacity = 0;
    }
};

// The value retail's 32-bit wrapping arithmetic produces.
int32_t Wrap(uint32_t v) {
    return static_cast<int32_t>(v);
}

}  // namespace

UNIT_TEST(review_tutorial_step_at_in_range_returns_the_step) {
    StepsFixture f;
    CHECK(TutorialInstance::StepAt(f.steps, 0) == f.real);
}

UNIT_TEST(review_tutorial_step_at_past_the_last_step_is_null) {
    StepsFixture f;
    // currentStep == steps._size: the value a save holds after the last step completes.
    CHECK(TutorialInstance::StepAt(f.steps, 1) == nullptr);
}

UNIT_TEST(review_tutorial_step_at_negative_is_null) {
    StepsFixture f;
    CHECK(TutorialInstance::StepAt(f.steps, -1) == nullptr);
}

UNIT_TEST(review_tutorial_scenario_index_bounds) {
    // Retail's own test: a negative index ends the chain.
    CHECK(!TutorialInstance::ScenarioIndexValid(-1, 4));
    CHECK(TutorialInstance::ScenarioIndexValid(0, 4));
    CHECK(TutorialInstance::ScenarioIndexValid(3, 4));
    // Past the scenario list: the read was scenarioList->_resources[4] of a 4-entry list.
    CHECK(!TutorialInstance::ScenarioIndexValid(4, 4));
    CHECK(!TutorialInstance::ScenarioIndexValid(INT_MAX, 4));
    CHECK(!TutorialInstance::ScenarioIndexValid(0, 0));
}

UNIT_TEST(review_tutorial_toolbar_cell_bounds) {
    // Retail's own test: the count must exceed the index.
    CHECK(TutorialSetupDialog::ToolbarCellInRange(3, 0));
    CHECK(TutorialSetupDialog::ToolbarCellInRange(3, 2));
    CHECK(!TutorialSetupDialog::ToolbarCellInRange(3, 3));
    CHECK(!TutorialSetupDialog::ToolbarCellInRange(0, 0));
    // A negative toolbarIndex passed `count > index` and read before the entity array.
    CHECK(!TutorialSetupDialog::ToolbarCellInRange(3, -1));
    CHECK(!TutorialSetupDialog::ToolbarCellInRange(0, INT_MIN));
}

UNIT_TEST(review_statgoal_production_window_ordinary_values) {
    // Year 10, the last 3 years: slots (10 - 3 - 1) * 48 .. 9 * 48 + frac.
    constexpr int32_t lo = StatGoal::ProductionWindowLo(10, 3);
    constexpr int32_t hi = StatGoal::ProductionWindowHi(10, 7);
    CHECK_EQ(lo, 6 * 0x30);
    CHECK_EQ(hi, 9 * 0x30 + 7);
}

UNIT_TEST(review_statgoal_production_window_wraps_as_retail) {
    // An achievement resource's statSubType of INT_MIN (the ledger's UBSan repro) and of
    // -50000000; a save's year of 50000000. Constant expressions: before the fix each of
    // these is an int32 overflow and does not compile.
    constexpr int32_t loMin = StatGoal::ProductionWindowLo(1, INT_MIN);
    constexpr int32_t loBig = StatGoal::ProductionWindowLo(5, -50000000);
    constexpr int32_t hiBig = StatGoal::ProductionWindowHi(50000000, 3);
    CHECK_EQ(loMin, Wrap((1u - 0x80000000u - 1u) * 0x30u));
    CHECK_EQ(loBig, Wrap((5u + 50000000u - 1u) * 0x30u));
    CHECK_EQ(hiBig, Wrap(50000000u * 0x30u - 0x30u + 3u));
}
