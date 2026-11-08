// src/game/Dialog/tradeui_price.cpp
// Wave A — 0x14005a520, the per-resource PRICE lookup, plus its sub-callee
// 0x14000ea80 and the diagnostic that can tell a correct port from a plausible
// wrong one.
//
// ⚠ THE REGISTERED NAME IS WRONG. `fn_Building_GetSellableCount` does not count
// anything sellable; it returns a PRICE. The name is kept because ~4 call sites use
// it (UpdateSellTotals, RefreshSellSpinnerValues, RefreshSellTable, and the merchant
// auto-buy routine at 0x140059ad0). An earlier comment in engine_funcs.h calling it
// "queries available sell count" has already been corrected there.
//
// ── What it returns, from the 0x14005a520 decompile + disassembly ─────────────
//
//   tradeState + 0x2c   int32   index of the DOCKED merchant; negative = none
//   tradeState + 0x18   void*   TradeDescription holder (IsDerivedFrom-checked)
//   tradeDesc  + 0x48   void*   merchant array, stride 0x48
//   merchant[n]+ 0x14   uint32  resource-category bitmask  (see the caveat below)
//   merchant[n]+ 0x18   uint32  resource-category bitmask
//   resourceComp+0x5c   uint32  the resource's own category flags
//   valueDesc  + 0x40   int32   the base value
//
//   no merchant  ->  valueDesc + 0x40
//   +0x14 mask hits ->  valueDesc + 0x40
//   +0x18 mask hits ->  (int)((float)(valueDesc + 0x40) * 0.75f)
//   neither         ->  0
//
// The masks are tested in that order, +0x14 first, and only one multiplier exists in
// the whole function.
//
// ── Two things the decompile hides ───────────────────────────────────────────
//
// 1. The two branches do NOT share one read of `valueDesc+0x40`. With no merchant,
//    a520 scans the component list for ValueDescription ITSELF. With a merchant, the
//    scan in a520 looks for RawMaterialDescription — that supplies only the flags at
//    +0x5c — and the VALUE comes from a separate call to 0x14000ea80. They agree
//    only because ea80's scan is the identical ValueDescription search; that was
//    checked, not assumed, because if ea80 had been anything else the "both paths
//    return the same base value" invariant would have been testing nothing.
//
// 2. When a resource has NO ValueDescription, retail reads `*(int*)(0 + 0x40)` — a
//    real null dereference, on BOTH branches. Not a decompiler artifact. Standalone
//    returns 0 there instead; that is a deliberate divergence, and the probe counts
//    it so a town full of value-less resources cannot hide behind a quiet zero.
//
// ── DAT_140172124 = 0.75f ────────────────────────────────────────────────────
// Read directly out of the image at 0x140172124: bytes 00 00 40 3f = 0x3f400000 =
// 0.75f. That agrees with the independently-derived constant already in the tree at
// standalone/reg_citizenai_actionctors.cpp:51. Two sources, one value, and it is
// below 1.0 as a discount must be.
#include "object/Internal.h"   // Object::Base::Type (B.3.8 callers)
#include "object/TypeInfo.h"
#include "Component/ValueDescription.h"   // ENDGAME B.3.9 TR: ValueDescription::TypeId()
#include "Component/TradeDescription.h"   // ENDGAME B.3.9 TR: TradeDescription::TypeId()
#include "Component/RawMaterialDescription.h"   // X::TypeId() (B.3.9 CG2)
#include "Component/ComponentDescription.h"
#include "Component/TradeComponent.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include "Dialog/exe_bodies.h"

namespace {

constexpr float kTradeDiscount = 0.75f;      // DAT_140172124

// Scan a resource template's descriptions for a type id (ComponentDescription
// _subDescs / _subDescCount); each one's type id comes from Type(). Retail
// follows the id match with an IsDerivedFrom confirmation and keeps the
// description only if that passes.
::Object::Base* PriceFindComponentByTypeId(const ComponentDescription* entity, uint32_t typeId) {
    if (!entity) return nullptr;
    const int32_t count = entity->_subDescCount;
    Description* const* arr = entity->_subDescs;   // COMP-c typed _subDescs (merge fix)
    if (!arr || count <= 0) return nullptr;
    for (int32_t i = 0; i < count; ++i) {
        ::Object::Base* comp = arr[i];
        if (!comp) continue;
        if (comp->Type() != typeId) continue;
        // Retail stops at the FIRST id match and then confirms; a failed confirm
        // yields null rather than continuing the scan.
        if (!::Object::IsDerivedFromNullSafe(comp, typeId)) return nullptr;
        return comp;
    }
    return nullptr;
}

// 0x14000ea80 — find the resource's ValueDescription. Retail returns it by
// value through the caller's slot (an ExternalPtr); the one caller is a520.
::Object::Base* TradeUI_FindValueDescription_ea80(const ComponentDescription* resourceEntity) {
    return PriceFindComponentByTypeId(resourceEntity, ValueDescription::TypeId());
}

} // namespace

// The BANISHED_TRADE_PRICE_PROBE invariant probe was deleted with the flag (ENDGAME C).
// src/diag/tool_harness.cpp still calls the report entry point; the stub goes with it.


// ── 0x14005a520 ──────────────────────────────────────────────────────────────
int TradeComponent::GetResourcePrice(const ComponentDescription* resource) const {
    // No resource test (B8 RES, row 1104): 0x14005a544 (undocked) and the docked branch
    // read [RDX+0x30] untested; every caller hands a storage slot's / sell row's resource.
    const int32_t merchantIdx = tradeId;

    // ── No merchant docked: the base value, straight out of ValueDescription.
    if (merchantIdx < 0) {
        const ValueDescription* vd = static_cast<const ValueDescription*>(
            PriceFindComponentByTypeId(resource, ValueDescription::TypeId()));
        const int32_t base = vd ? vd->_baseValue : 0;   // retail: *(int*)(0 + 0x40)
        return base;
    }

    // ── Merchant docked: the RawMaterialDescription supplies the category flags.
    const RawMaterialDescription* rawMat = static_cast<const RawMaterialDescription*>(
        PriceFindComponentByTypeId(resource, RawMaterialDescription::TypeId()));
    const uint32_t flags = rawMat ? rawMat->_flags : 0;

    Description* description = _description;
    const TradeDescription* tradeDesc = nullptr;
    if (description && ::Object::IsDerivedFromNullSafe(description, TradeDescription::TypeId()))
        tradeDesc = static_cast<const TradeDescription*>(description);   // retail keeps null on a failed check
    if (!tradeDesc) return 0;                      // retail would deref null here
    if (!tradeDesc->_merchants) return 0;

    // The merchant entry (0x48 stride): the two category masks at +0x14 / +0x18.
    const TradeMerchant& m = tradeDesc->_merchants[merchantIdx];
    const uint32_t mask14 = m.fullPriceFlags;
    const uint32_t mask18 = m.discountFlags;

    const ValueDescription* vd = static_cast<const ValueDescription*>(
        TradeUI_FindValueDescription_ea80(resource));   // 0x14000ea80
    const int32_t base = vd ? vd->_baseValue : 0;        // retail: *(int*)(0 + 0x40)

    int32_t result;
    if ((mask14 & flags) != 0) {
        result = base;
    } else if ((mask18 & flags) != 0) {
        result = static_cast<int32_t>(static_cast<float>(base) * kTradeDiscount);
    } else {
        result = 0;
    }
    return result;
}
