// ============================================================================
// tests/unit/ext_autotrade_free_goods_test.cpp -- fix.autotrade_free_goods's payment rule
// ============================================================================
// src/ext/autotrade_free_goods/autotrade_free_goods_rule.h without the world: which goods
// TradeComponent::AutoTrade's payment loop (retail 0x14005a177..0x14005a330, the port's
// tradecomponent_dock.cpp) passes over. Off, none; on, a good the merchant prices 0 or less.
// ============================================================================
#include "unit.h"
#include "ext/autotrade_free_goods/autotrade_free_goods_rule.h"

using namespace Ext::AutotradeFreeGoods;

UNIT_TEST(ext_autotrade_free_goods_rule) {
    CHECK(!SkipInPayment(false, 0));
    CHECK(!SkipInPayment(false, 5));
    CHECK(SkipInPayment(true, 0));
    CHECK(SkipInPayment(true, -1));
    CHECK(!SkipInPayment(true, 1));
}
