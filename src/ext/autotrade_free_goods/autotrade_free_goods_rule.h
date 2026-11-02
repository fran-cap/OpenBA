// ============================================================================
// src/ext/autotrade_free_goods/autotrade_free_goods_rule.h -- fix.autotrade_free_goods: the pure rule
// ============================================================================
// NOT RETAIL. docs/retail_bugs.md §24. Standard C++ only, so
// tests/unit/ext_autotrade_free_goods_test.cpp links it without the world.
//
// TradeComponent::AutoTrade (retail 0x140059ad0, src/game/Component/tradecomponent_dock.cpp) pays
// for what it bought by selling the post's stored goods unit by unit, slot by slot, until the
// money paid covers the money spent. Each unit sold adds the good's price for the docked merchant
// (TradeComponent::GetResourcePrice 0x14005a520), which is 0 for a good outside both of the
// merchant's price masks. A unit priced 0 never raises the sum paid, so retail's loop
// (0x14005a1c0..0x14005a330) hands over every unit of that good for nothing before it reaches the
// next slot. With the row on, a good priced 0 is not used to pay.
// ============================================================================
#pragma once

namespace Ext {
namespace AutotradeFreeGoods {

// Whether the payment loop passes over a stored good of unit price `unitPrice`. Off: retail
// sells it whatever its price.
inline bool SkipInPayment(bool fixOn, int unitPrice) {
    return fixOn && unitPrice <= 0;
}

}  // namespace AutotradeFreeGoods
}  // namespace Ext
