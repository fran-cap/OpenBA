// ============================================================================
// tests/unit/pass_split_test.cpp -- issue #253: the stress counters' slot helpers
// ============================================================================
// graphics/gather_prefetch.h: GatherShaderSlot maps a draw group's `_shader` to the
// four counter slots (0 main, 1 shadow, 2 reflection, 3 anything else).
// graphics/pass_split.h: the per-scene-group CPU split. With the split off nothing is
// timed; SunLight's share is counted only inside an open pass-1 group and is moved out
// of that group's slot into the shadow slot, so the two always sum to the group's span.
// ============================================================================
#include "unit.h"

#include <cstdint>

#include "graphics/gather_prefetch.h"
#include "graphics/pass_split.h"

namespace {

void ClearSplit() {
    for (int k = 0; k < PassSplit::kSlots; ++k) PassSplit::g_ticks[k].store(0);
    PassSplit::g_p1Group = -1;
}

}  // namespace

UNIT_TEST(gather_shader_slot_maps_the_three_groups_and_the_rest) {
    CHECK_EQ(GatherShaderSlot(0), 0);
    CHECK_EQ(GatherShaderSlot(1), 1);
    CHECK_EQ(GatherShaderSlot(2), 2);
    CHECK_EQ(GatherShaderSlot(3), kGatherShaderSlots - 1);
    CHECK_EQ(GatherShaderSlot(-1), kGatherShaderSlots - 1);
    CHECK_EQ(GatherShaderSlot(0x7fffffff), kGatherShaderSlots - 1);
}

UNIT_TEST(pass_split_entry_slots) {
    CHECK_EQ(PassSplit::P1Slot(0), static_cast<int>(PassSplit::kP1Main));
    CHECK_EQ(PassSplit::P1Slot(2), static_cast<int>(PassSplit::kP1Refl));
    CHECK_EQ(PassSplit::P1Slot(1), -1);
    CHECK_EQ(PassSplit::P2Slot(0), static_cast<int>(PassSplit::kP2Main));
    CHECK_EQ(PassSplit::P2Slot(2), static_cast<int>(PassSplit::kP2Refl));
    CHECK_EQ(PassSplit::P2Slot(5), -1);
}

UNIT_TEST(pass_split_off_times_nothing) {
    ClearSplit();
    PassSplit::g_on = 0;
    const int64_t t0 = PassSplit::Begin();
    CHECK_EQ(t0, static_cast<int64_t>(0));
    PassSplit::End(PassSplit::kP2Main, t0);
    PassSplit::g_p1Group = PassSplit::kP1Main;
    PassSplit::EndSun(t0);
    for (int k = 0; k < PassSplit::kSlots; ++k) CHECK_EQ(PassSplit::g_ticks[k].load(), static_cast<int64_t>(0));
    ClearSplit();
}

UNIT_TEST(pass_split_sun_share_moves_out_of_its_group) {
    ClearSplit();
    PassSplit::g_on = 1;
    const int64_t groupT0 = PassSplit::Begin();
    CHECK(groupT0 != 0);
    PassSplit::g_p1Group = PassSplit::kP1Main;
    const int64_t sunT0 = PassSplit::Begin();
    volatile int spin = 0;
    for (int i = 0; i < 100000; ++i) spin = spin + 1;
    PassSplit::EndSun(sunT0);
    PassSplit::End(PassSplit::kP1Main, groupT0);
    PassSplit::g_p1Group = -1;

    const int64_t sun  = PassSplit::g_ticks[PassSplit::kP1Shadow].load();
    const int64_t main = PassSplit::g_ticks[PassSplit::kP1Main].load();
    CHECK(sun >= 0);
    CHECK(main >= 0);   // the group's span contains the sun's
    CHECK_EQ(PassSplit::g_ticks[PassSplit::kP1Refl].load(), static_cast<int64_t>(0));

    // Outside an open pass-1 group (the faithful Renderer::Draw) the sun is not counted.
    const int64_t lone = PassSplit::Begin();
    PassSplit::EndSun(lone);
    CHECK_EQ(PassSplit::g_ticks[PassSplit::kP1Shadow].load(), sun);
    PassSplit::g_on = 0;
    ClearSplit();
}
