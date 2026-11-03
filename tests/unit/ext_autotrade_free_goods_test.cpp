// ============================================================================
// tests/unit/ext_autotrade_free_goods_test.cpp -- fix.autotrade_free_goods's payment loop
// ============================================================================
// src/ext/autotrade_free_goods/autotrade_free_goods_rule.h without the world. Pay() is
// TradeComponent::AutoTrade's step 4 (retail 0x14005a177..0x14005a330, the port's
// tradecomponent_dock.cpp) over a plain list of {unit price, units available}: off, a good
// priced 0 loses its whole stack; on, it keeps it and the valued goods pay as before.
// ============================================================================
#include "unit.h"
#include "ext/autotrade_free_goods/autotrade_free_goods_rule.h"

#include <vector>

using namespace Ext::AutotradeFreeGoods;

namespace {

struct Slot {
    int unit;
    int avail;
};

// Returns the units sold from each slot; `paid` receives the money credited.
std::vector<int> Pay(bool fixOn, const std::vector<Slot>& slots, int spent, int* paid) {
    std::vector<int> soldPerSlot(slots.size(), 0);
    int sum = 0;
    for (size_t i = 0; i < slots.size(); ++i) {
        const int unit = slots[i].unit;
        const int avail = slots[i].avail;
        if (avail == 0) continue;
        if (SkipInPayment(fixOn, unit)) continue;
        int sold = 0;
        while (sum < spent) {
            if (sold >= avail) break;
            sum += unit;
            ++sold;
        }
        soldPerSlot[i] = sold;
    }
    *paid = sum;
    return soldPerSlot;
}

}  // namespace

UNIT_TEST(ext_autotrade_free_goods_rule) {
    CHECK(!SkipInPayment(false, 0));
    CHECK(!SkipInPayment(false, 5));
    CHECK(SkipInPayment(true, 0));
    CHECK(SkipInPayment(true, -1));
    CHECK(!SkipInPayment(true, 1));
}

UNIT_TEST(ext_autotrade_free_goods_off_gives_the_stack_away) {
    // 120 logs the merchant prices 0, then 40 tools at 6: a 12-unit purchase.
    int paid = 0;
    std::vector<int> sold = Pay(false, {{0, 120}, {6, 40}}, 12, &paid);
    CHECK_EQ(sold[0], 120);   // retail: every log goes, for nothing
    CHECK_EQ(sold[1], 2);
    CHECK_EQ(paid, 12);
}

UNIT_TEST(ext_autotrade_free_goods_on_keeps_the_stack) {
    int paid = 0;
    std::vector<int> sold = Pay(true, {{0, 120}, {6, 40}}, 12, &paid);
    CHECK_EQ(sold[0], 0);
    CHECK_EQ(sold[1], 2);
    CHECK_EQ(paid, 12);
}

UNIT_TEST(ext_autotrade_free_goods_on_matches_off_without_free_goods) {
    const std::vector<Slot> slots = {{3, 5}, {6, 40}, {1, 100}};
    for (int spent : {0, 1, 14, 15, 16, 300, 1000}) {
        int paidOff = 0;
        int paidOn = 0;
        std::vector<int> off = Pay(false, slots, spent, &paidOff);
        std::vector<int> on = Pay(true, slots, spent, &paidOn);
        CHECK(off == on);
        CHECK_EQ(paidOff, paidOn);
    }
}
