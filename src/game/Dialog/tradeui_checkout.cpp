// src/game/Dialog/tradeui_checkout.cpp
// Wave D — the TradeUI CHECKOUT (buttonTrade, element id 0x4e20).
//
//   0x140081420  TradeUI_OnTradeButton          — the checkout itself
//   0x14005baa0  TradeComponent_RemoveBuyOrder  — its buy-order-list leaf
//   0x14005a490  TradeDescription_ExtendMerchantStay — its merchant-timer leaf
//
// ── What 0x140081420 actually does ───────────────────────────────────────────
// Ghidra names two of its callees "Game_DestroyEntity" (0x140091e60) and
// "Game_RemoveEntity" (0x140091a20). BOTH are mislabels, and believing them
// inverts the meaning of the whole second half of this function. Neither one
// destroys or removes anything:
//
//   0x140091e60 = RawMaterialComponent_RegisterRawMaterial
//                 append-if-absent into townMgr+0x110 (raw-material availability)
//   0x140091a20 = CitizenBehavior::RemoveEntity (the dedup append into +0x120)
//                 append-if-absent into townMgr+0x120 (crop/livestock/orchard
//                 availability) — see the block comment above the reimpl in
//                 src/game/Game/starttown.cpp, which already names FUN_140081420
//                 as the caller that grows this list after start-town seeds it.
//
// So the checkout is a two-phase commit of the panel's pending order, and the buy
// phase is what UNLOCKS newly purchased goods for the town, not a teardown:
//
//   Phase 1 — SELL (self+0x30 array, count +0x38, stride 0x20; qty at entry+0x18)
//     For each row with a non-zero quantity: subtract the goods from the trading
//     post's own StorageComponent, credit the town's per-category produced-goods
//     counter (townMgr+0x170[cat].+0xc), zero the row's spinner and the row's
//     pending quantity. Then add the sell total (read straight off the
//     numberSellTotal element at self+0x100, field +0xd8) to the town's trade
//     credit at townMgr+0x1ac.
//
//   Phase 2 — BUY (self+0x40 array, count +0x48, stride 0x18; qty at entry+0x10)
//     For each row with a non-zero quantity: retire that much of the order from
//     the TradeComponent's buy list, then dispatch on what was bought, by probing
//     the purchased entity's component array for a description type:
//       RawMaterialDescription  -> deliver into the post's storage, and register
//                                  the material as available to the town
//       NaturalResourceDescription -> register as available agriculture
//       LivestockDescription    -> register as available agriculture, and spawn
//                                  the purchased animals into the world
//                                  (0x14005ace0 — see the gate note below)
//     Then zero the row's spinner and pending quantity.
//
//   Phase 3 — refresh the whole panel and both totals.
//
// The tail of retail's function is UpdateBuyTotals (0x140082850) INLINED — same
// component scan, same self+0x40 walk, same write to numberBuyTotal at self+0x108.
// It is called out to the existing port here rather than duplicated.
//
// ── Why this is opt-in ───────────────────────────────────────────────────────
// UPDATE: the one genuinely missing dependency, 0x14005ace0 (the livestock
// spawn), is now ported in src/game/Component/tradecomponent_livestock.cpp and
// registered, so fn_Building_AddBuyToStorage resolves and all three buy arms are
// complete. Nothing in the checkout is unported any more.
//
// The gate BANISHED_LIVE_TRADE_CHECKOUT remains, because this function mutates
// storage, town resource counters and the town availability pools, and that has
// to be exercised before it can be trusted by default (the CLAUDE.md rule that
// unproven features start outside the default-on table). Once a run shows a purchase
// committing correctly, this moves into the default-on table and the flag becomes a
// kill-switch.
//
// With the gate OFF the click is inert — which is exactly the "pressing Trade
// does nothing" symptom, and is the gate working as designed, not a defect.

#include "Dialog/ui_panels.h"
#include <new>
#include "system/Value.h"
#include "ui/UserInterface.h"
#include "object/TypeInfo.h"
#include "Entity/exe_bodies.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "Entity/CitizenBehavior.h"
#include "Action/exe_bodies.h"      // B3R: StorageSubtract, StorageAdd, ResourceCategory
#include "Component/exe_bodies.h"   // B3R: TradeComponent_SpawnPurchasedLivestock
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "Component/RawMaterialDescription.h"
#include "Component/NaturalResourceDescription.h"
#include "Component/LivestockDescription.h"
#include "Component/TradeDescription.h"
#include "Entity/ComponentEntity.h"   // X::TypeId() (B.3.9 CG2)
#include "ui/SpinnerElement.h"    // ENDGAME B.3.6 UI callers round 2 (K1)
#include "Dialog/exe_bodies.h"
#include "Dialog/TradeUI.h"
#include "Component/TradeComponent.h"
#include "Component/StorageComponent.h"
#include "Component/ComponentDescription.h"
#include "ui/NumberElement.h"
#include "ext/map_overlays/map_overlays.h"   // EXT SEAM (NOT RETAIL, docs/extensions/handins/MAPOVERLAYS.md): ext.map_overlays

// ── Cross-module reimpls this calls ──────────────────────────────────────────
// 0x140091e60 RawMaterialComponent_RegisterRawMaterial: Component/exe_bodies.h.
// 0x140091a20 — CitizenBehavior::RemoveEntity (a misnomer: the dedup append into +0x120).

namespace {

// ── Exe leaves, called by name (B3R) ─────────────────────────────────────────
//   0x14012d150 ActRm_StorageSubtract_d150, 0x140088f90 ActRm_GetResourceCategory_88f90

// Probe a purchased entity's component array for an object of a given type-id.
//
// Retail inlines this scan three times, once per description type. All three
// copies share the same shape: walk entity+0x28 (object-pointer array, count at
// entity+0x30, stride 8), call vtable+0x10 (GetTypeId) on each element, and stop
// at the FIRST element whose type-id matches. Only then is IsDerivedFrom
// consulted; if it says no, retail breaks out of the loop rather than continuing
// the scan, so a null return here reproduces the fall-through to the next
// description type exactly.
void* FindComponentByTypeId(const ComponentDescription* entity, uint32_t typeId) {
    if (!entity) return nullptr;
    Description** objs = entity->_subDescs;   // COMP-c typed _subDescs (merge fix)
    int    count = entity->_subDescCount;
    if (!objs) return nullptr;
    for (int i = 0; i < count; ++i) {
        Object::Base* obj = objs[i];
        if (!obj) continue;
        // vtable slot +0x10 = GetTypeId()
        if (obj->Type() != typeId) continue;
        return ::Object::IsDerivedFromNullSafe(obj, typeId) ? obj : nullptr;  // match, derived-or-bust
    }
    return nullptr;
}

}  // namespace

// ============================================================
// TradeComponent_RemoveBuyOrder (0x14005baa0)
// ============================================================
// Retires `qty` from the TradeComponent's pending buy-order list.
//
//   trade+0x50  entry array   (stride 0x10: +0x00 resource entity ptr,
//                              +0x08 unit price int32, +0x0c quantity int32)
//   trade+0x58  entry count
//
// Every entry matching `resourceEntity` has its quantity reduced; an entry that
// reaches exactly zero is erased by shifting the tail down one slot and
// decrementing the count. The outer index is then rewound so the shifted-in
// entry is examined too — retail does this with `i -= 1` before the `i += 1`,
// which is why the loop below re-tests the same index after an erase.
//
// The erase loop in the decompile looks like it copies 16 bytes per step from
// puVar3[2]/puVar3[3]; that is the two-qword move of the NEXT entry into the
// current one, i.e. a plain forward shift.
extern "C" void TradeComponent_RemoveBuyOrder(TradeComponent* tradeComp,
                                              const ComponentDescription* resourceEntity, int qty) {
    if (!tradeComp) return;

    for (int i = 0; i < tradeComp->manifest._size; ++i) {
        TradeManifestLine* base  = tradeComp->manifest._data;
        TradeManifestLine* entry = base + i;
        if (entry->good != resourceEntity) continue;

        int* entryQty = &entry->stock;
        *entryQty -= qty;
        if (*entryQty != 0) continue;

        // Erase: shift every later entry down one slot, then shrink the count.
        int count = tradeComp->manifest._size;
        for (int j = i; j + 1 < count; ++j) {
            TradeManifestLine* dst = base + j;
            dst[0] = dst[1];
        }
        tradeComp->manifest._size = count - 1;
        --i;  // re-test this index: a different entry now occupies it
    }
}

// ============================================================
// TradeDescription_ExtendMerchantStay (0x14005a490)
// ============================================================
// Called once at the top of the checkout, on the trading post's TradeComponent.
// Reads the post's TradeDescription (component+0x18, type-id 0x1401c4bb8) and
// raises two high-water marks on the town manager (gameWorld+0xf0):
//
//   townMgr+0x1a8 (float) = max(itself, desc+0x68)  — merchant arrival weight
//   townMgr+0x1a0 (Time)  = max(itself, desc+0x6c * 300s)
//
// Both are raise-only. The Time value is built through the engine's
// System::Time::Time(float seconds) ctor, which writes EIGHT bytes through its
// return slot — so the destination below is a full int64, not the int32 the
// field's integer source might suggest.
//
// The multiplier 300.0f is DAT_140172234, read from the image.
void TradeComponent::ExtendMerchantStay() {
    Description* desc = _description;
    // 0x1401c4bb8 is TradeDescription's type-id cell (Table 1).
    const uint32_t tidTradeDesc = TradeDescription::TypeId();
    if (!desc || !::Object::IsDerivedFromNullSafe(desc, tidTradeDesc)) return;
    TradeDescription* d = static_cast<TradeDescription*>(desc);

    CitizenBehavior* town =
        GameUpdateLoop::_singleton->m_citizenList;
    if (!town) return;

    float weight = d->_f68;
    if (weight > town->field_0x1A8) town->field_0x1A8 = weight;

    int32_t units = d->_i6c;
    System::Time stay{};                               // 8 bytes — the ctor fills all of it
    RtSystem_Time_Ctor(&stay, static_cast<float>(units) * 300.0f);
    if (town->field_0x1A0._ticks < stay._ticks) town->field_0x1A0._ticks = stay._ticks;
}

// ============================================================
// TradeUI::OnTradeButton (0x140081420) — buttonTrade / element id 0x4e20
// ============================================================
// Retail takes only `this` (0x140081430 reads [RCX+0x28] first; RDX/R8/R9 are
// never read). The C spelling the harness calls is the B4 FORWARD below.
void TradeUI::OnTradeButton() {
    // The panel's subject entity (+0x28), narrowed to a ComponentEntity.
    ::Object::Base* source = _dataSource;
    ComponentEntity* entity = ::Object::IsDerivedFromNullSafe(source, ComponentEntity::TypeId())
        ? static_cast<ComponentEntity*>(source) : nullptr;

    // The trading post's own TradeComponent and StorageComponent.
    TradeComponent* trade = entity->FindComponent<TradeComponent>();

    StorageComponent* storage = entity->FindComponent<StorageComponent>();

    trade->ExtendMerchantStay();

    CitizenBehavior* town =
        GameUpdateLoop::_singleton->m_citizenList;

    // OUTCOME counters. A click that commits nothing and a click that commits a full
    // basket look identical from outside this function, and "the button did nothing" is
    // exactly the report this wave was opened on — so the tail below prints what actually
    // moved, not that the handler was entered.
    int soldRows = 0, soldUnits = 0, boughtRows = 0, boughtUnits = 0;
    const int creditBefore = town->field_0x1AC;
    const bool mapOverlays = Ext::MapOverlays::g_on;   // ext.map_overlays: tested once, above both loops

    // ── Phase 1: sell rows ───────────────────────────────────────────────────
    // _sellItems (+0x30, stride 0x20): resource +0x00, pending quantity +0x18.
    // _sellRows (+0x60) is the parallel UI row array (spinner at +0x18).
    {
        int sellCount = _sellItems._size;
        for (int i = 0; i < sellCount; ++i) {
            int qty = _sellItems[i].quantity;
            if (qty == 0) continue;
            ++soldRows; soldUnits += qty;

            ComponentDescription* resEntity = _sellItems[i].resource;

            storage->SubtractItem(resEntity, qty);   // 0x14012d150
            if (mapOverlays) {
                // ext.map_overlays: sold to the merchant, at the trading post
                Ext::MapOverlays::OnConsumedAt(resEntity, entity, qty);
            }

            // Credit the town's per-category produced-goods tally.
            //   town->professionResources (+0x170): the category array, running total at +0x0c
            {
                int cat = ResourceCategory(resEntity);   // 0x140088f90 (its first argument, the storage, is never read)
                if (cat >= 0) {
                    town->professionResources._data[cat].field_0x0C += qty;
                }
            }

            // Zero the row's spinner and its pending quantity.
            _sellRows[i].spinner->SetValue(0);
            _sellItems[i].quantity = 0;
        }
    }

    // Bank the sell total. It is read straight off the numberSellTotal element
    // (+0x100, current value at element+0xd8) rather than recomputed, so the
    // displayed figure and the credited figure cannot disagree.
    {
        town->field_0x1AC += _numberSellTotal->_intValue;
    }

    // ── Phase 2: buy rows ────────────────────────────────────────────────────
    // _buyItems (+0x40, stride 0x18): resource +0x00, pending quantity +0x10.
    // _buyRows (+0x70) is the parallel UI row array (spinner at +0x10).
    {
        int buyCount = _buyItems._size;
        for (int i = 0; i < buyCount; ++i) {
            int qty = _buyItems[i].quantity;
            if (qty == 0) continue;
            ++boughtRows; boughtUnits += qty;

            ComponentDescription* bought = _buyItems[i].resource;

            // Retire this much of the order from the post's buy list.
            TradeComponent_RemoveBuyOrder(trade, bought, qty);

            // Raw material: deliver into the post's storage, and make the
            // material available to the town.
            if (FindComponentByTypeId(bought, RawMaterialDescription::TypeId())) {
                storage->AddItem(bought, qty);   // 0x14012d000
                if (mapOverlays) {
                    // ext.map_overlays: bought from the merchant, at the trading post
                    Ext::MapOverlays::OnProducedAt(bought, entity, qty);
                }
                town->RegisterRawMaterial(bought);   // 0x140091e60
            }
            // Natural resource (crop / orchard seed): availability only.
            else if (FindComponentByTypeId(bought, NaturalResourceDescription::TypeId())) {
                town->RemoveEntity(bought);   // 0x140091a20
            }
            // Livestock: availability, plus spawn the animals into the world.
            else if (FindComponentByTypeId(bought, LivestockDescription::TypeId())) {
                town->RemoveEntity(bought);   // 0x140091a20
                // The door was named fn_Building_AddBuyToStorage; it is bound to
                // 0x14005ace0 TradeComponent_SpawnPurchasedLivestock (B3R: the binding
                // decides). The never-taken "did not resolve" arm went with the door.
                trade->SpawnPurchasedLivestock(bought,   // 0x14005ace0
                                                       static_cast<unsigned int>(qty));
            }

            // Zero the row's spinner and its pending quantity.
            _buyRows[i].spinner->SetValue(0);
            _buyItems[i].quantity = 0;
        }
    }

    fprintf(stderr, "[TradeCheckout] committed: sold %d unit(s) over %d row(s), "
                    "bought %d unit(s) over %d row(s), town trade credit %d -> %d\n",
            soldUnits, soldRows, boughtUnits, boughtRows,
            creditBefore, town->field_0x1AC);
    fflush(stderr);

    // ── Phase 3: repaint ─────────────────────────────────────────────────────
    RefreshDisplay();
    RefreshSellTable();
    RefreshBuyTable();
    UpdateSellTotals();
    // Retail inlines UpdateBuyTotals (0x140082850) here rather than calling it;
    // the inlined body is instruction-for-instruction the same walk, so the
    // existing port stands in for it.
    UpdateBuyTotals();
}
