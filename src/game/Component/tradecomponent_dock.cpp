// src/game/Component/tradecomponent_dock.cpp
// TradeComponent controller wave, LAYER 3 — the DOCKED state (0x13).
//
// Layer 1 (tradecomponent_states.cpp) built the machine and the idle state; layer 2
// (boatcomponent_route.cpp) made the merchant arrive. This file is what happens once
// the boat is tied up: the merchant's stock list is generated, the stay timer is armed,
// and — if the post is configured for it — the post trades with the merchant by itself.
//
//   0x14005b580  TradeComponent::BuildMerchantManifest  the merchant's stock list
//   0x140059ad0  TradeComponent::AutoTrade              buy against the standing orders
//   0x14005a800  TradeComponent::EnterDocked            state 0x13 ENTER
//   0x14005a6d0  TradeComponent::LeaveDocked            state 0x13 UPDATE (leaving)
//
// ── The two TradeComponent arrays these functions live on ─────────────────────────
// They are easy to swap, and swapping them silently produces plausible garbage:
//
//   +0x40 base / +0x48 count, stride 0x0c — the player's STANDING BUY ORDERS.
//       +0x00 uint32 raw-material category MASK (ANDed with RawMaterialDescription+0x5c)
//       +0x08 int32  how many units the post wants
//   +0x50 base / +0x58 count / +0x5c capacity, stride 0x10 — the docked merchant's
//       MANIFEST, generated fresh on every arrival and freed on every departure.
//       +0x00 entity/description pointer   +0x08 int32 unit price   +0x0c int32 stock
//   +0x30 base / +0x38 count, stride 0x20 — the post's per-merchant-slot records
//       (built by TradeComponent::Setup / 0x14000e760).
//       +0x00 String merchant name   +0x08 int32 order kind
//       +0x10 ptr / +0x18 count / +0x1c cap — a hand-authored goods list
//   +0x2c int32 index of the DOCKED merchant, -1 when none
//   +0x28 int32 auto-trade policy: 1 = trade on arrival, 2 = trade on departure
//   +0x71 "mid-trade" flag (checked by the stay timer, not written here)
//   +0x78 Time  stay countdown        +0x80 merchant entity reference
//
// The description side: desc+0x48 merchant-record array / desc+0x50 count, stride 0x48;
// desc+0x58 tier array / desc+0x60 count, stride 0x14.
//
// ── Deviations, all deliberate ────────────────────────────────────────────────────
//  * 0x140059ad0's AVL insert is written out THREE times by the compiler (a probe pass,
//    a duplicate-locate pass and an insert pass, all with identical comparisons). It is
//    collapsed here into one search-then-insert. The tree is deterministic and the
//    comparisons are unchanged, so the resulting tree is the same one.
//  * Retail's new tree node writes eight bytes at +0x08 whose upper four are an
//    uninitialised stack slot; only the lower int32 is ever read. This port writes a
//    clean 1.
//  * 0x140088f90 is called with the unit PRICE in RCX where its other callers pass a
//    StorageComponent. That is retail's own code and it is harmless — the function
//    ignores its first parameter (see ActRm_GetResourceCategory_88f90) — so the price
//    is passed through rather than "corrected".
//  * The candidate cursor is kept in one variable. Retail keeps two (R12D and a
//    pointer-sized copy) that are incremented and reset together.
//
// ── Where Ghidra's decompile of 0x140059ad0 cannot be trusted ─────────────────────
// The function has a 0x98-byte frame and the decompiler folds distinct variables onto
// one name. Everything here is read off the disassembly. In particular:
//   [RBP+0x77] is the buy-order byte offset for the first three instructions of each
//              outer iteration and then becomes `spent` for the rest of it;
//   R13D is the storage volume in the purchase loop and `paid so far` in the sell loop;
//   the committed storage volume lives in [RBP+0x7f], NOT in R13 — the purchase loop
//              works on a copy and writes it back only when a unit is actually bought.
//
// GATED: reached only through BANISHED_LIVE_TRADE_CTRL. Nothing else in the build calls
// any of these four addresses.

#include <new>
#include "object/External.h"
#include "object/TypeInfo.h"
#include "Component/exe_bodies.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "Component/ValueDescription.h"   // ENDGAME B.3.9 TR: ValueDescription::TypeId()
#include "Component/TradeDescription.h"   // ENDGAME B.3.9 TR: TradeDescription::TypeId()
#include "Component/TradeComponent.h"
#include "Component/tradecomponent_funcs.h"   // the AVL map's NewNode / Retrace
#include "Component/ControllerComponent.h"
#include "Component/ComponentDescription.h"
#include "Component/RawMaterialDescription.h"
#include "Component/StorageComponent.h"
#include "Component/StateMachineComponent.h"
#include "Entity/ComponentEntity.h"   // ComponentEntity::FindComponent<T>
#include "Entity/CitizenBehavior.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "system/Value.h"   // ENDGAME B.3.4b System caller batch
#include "system/MemoryAllocator.h"
#include "Action/exe_bodies.h"   // StorageAdd, ResourceCategory
#include "Dialog/exe_bodies.h"   // TradeUI_GetBuyEntries_59670, TradeUI_GetResourcePrice_a520, TradeDescription_ExtendMerchantStay
#include "Work/exe_bodies.h"     // WorkBase_TreeNodeFree_e320
#include "ext/registry.h"        // Ext::Fix().autotrade_free_goods
#include "ext/autotrade_free_goods/autotrade_free_goods_rule.h"   // fix.autotrade_free_goods

namespace {

// ── Leaves, called by the body each retail address is registered to ───────────────
// Two of these carry names that are wrong for this use (see the notes at the calls):
//   0x140059670 TradeUI_GetBuyEntries_59670        0x14005a520 TradeUI_GetResourcePrice_a520
//                                                     (NOT a count: a520 returns a PRICE)
//   0x14005a490 TradeDescription_ExtendMerchantStay 0x140006270 FindComponent<StorageComponent>
//   0x140003ea0 FindComponent<StateMachineComponent> 0x14012cf10 CitizenComp_StorageComponent_QueryCount
//   0x14012d000 ActRm_StorageAdd_d000               0x140088f90 ActRm_GetResourceCategory_88f90
//   0x140091e60 RawMaterialComponent_RegisterRawMaterial (NOT "Game_DestroyEntity")
//   0x14000e360 TradeStockTree::Retrace (the rebalance; the rotations 0x14000e450 /
//               0x14000e580 are its callees)
//   0x14000e320 WorkBase_TreeNodeFree_e320
// The three TradeUI_* / TradeDescription_* leaves are TradeComponent bodies by retail
// that live in DLG-e's files; they are called here with the typed receiver.

// The TradeDescription off +0x18, cast exactly as retail does it.
TradeDescription* TradeDesc(const TradeComponent* comp) {
    Description* raw = comp->_description;
    return Object::IsDerivedFromNullSafe(raw, TradeDescription::TypeId())
        ? static_cast<TradeDescription*>(raw) : nullptr;
}

// Find the first sub-description of an entity template (ComponentDescription::_subDescs)
// whose GetTypeId (vtable +0x10) equals T's. Retail inlines this scan six times across
// the two big functions; every copy stops at the FIRST id match and only then consults
// IsDerivedFrom, breaking out rather than continuing the scan if that says no. A null
// return therefore reproduces retail's fall-through exactly. The null tests on the
// template, the id, the array and a slot are the tree's.
template <class T>
T* FindSubDescription(const ComponentDescription* owner) {
    const uint32_t typeId = T::TypeId();
    if (!owner || !typeId) return nullptr;
    const int count = owner->_subDescCount;
    Description** objs = owner->_subDescs;
    if (!objs) return nullptr;
    for (int i = 0; i < count; ++i) {
        Description* obj = objs[i];
        if (!obj) continue;
        if (obj->Type() != typeId) continue;
        return (Object::IsDerivedFromNullSafe(obj, typeId)) ? static_cast<T*>(obj) : nullptr;
    }
    return nullptr;
}

// ── The purchase tally: an AVL map from bought good -> unit count ────────────────
// The node is TradeResourceNode (TradeComponent.h), read off the disassembly at
// 0x140059ef1. The rebalance (0x140059f90 / 0x14005a081, the tail of retail's insert,
// run twice verbatim — once per insert side) is statement for statement the stock
// tree's retrace 0x14000e360, over the same rotations 0x14000e450 / 0x14000e580.

// Insert `key`, or bump the count of an existing node. Returns true if a NODE was
// added (retail increments its node counter only then).
//
// Retail emits the same descent three times — a probe, a duplicate-locate and an
// insert. Collapsed here to one descent; the comparisons and the resulting shape are
// identical because the tree is not mutated between the copies.
bool AvlInsert(TradeStockTree& tree, ComponentDescription* key) {
    TradeResourceNode* parent = nullptr;
    bool goLeft = false;
    for (TradeResourceNode* cur = tree.root; cur; ) {
        const ComponentDescription* k = cur->resource;
        if (key == k) {                                   // already bought: one more unit
            cur->quantity += 1;
            return false;
        }
        parent = cur;
        goLeft = key < k;
        cur = goLeft ? cur->left : cur->right;
    }

    // Retail stores an 8-byte stack qword at +0x08 whose upper half is uninitialised;
    // only the lower int32 is ever read back (`INC dword [n+8]` / `MOV R8D,[n+8]`).
    // The node is {key, 1, parent, 0, 0, balance 0, height 0}.
    TradeResourceNode* node = TradeResourceTree_NewNode(key, 1, parent);
    if (!parent) {
        tree.root = node;                                 // first node: no rebalance
        return true;
    }
    (goLeft ? parent->left : parent->right) = node;
    tree.Retrace(parent);
    return true;
}

TradeResourceNode* AvlLeftmost(TradeResourceNode* n) {
    while (n && n->left) n = n->left;
    return n;
}

// In-order successor, 1:1 with 0x14005a3f5.
TradeResourceNode* AvlNext(TradeResourceNode* n) {
    if (TradeResourceNode* r = n->right) return AvlLeftmost(r);
    TradeResourceNode* node = n;
    TradeResourceNode* up = node->parent;
    if (!up) return node->parent;
    if (up->left == node) return up;
    while (up && up->right == node) {
        node = up;
        up = node->parent;
    }
    return node->parent;
}

// ── The two inline dynamic arrays 0x14005b580 builds on its stack ─────────────────
// Both are the engine's {ptr, int32 count, int32 capacity} header. The goods array is
// handed to 0x140059670, which writes it through that exact layout, so it must stay
// binary-compatible with it.
template <class T>
struct StackArray { T* base; int32_t count; int32_t cap; };
static_assert(sizeof(StackArray<ComponentDescription*>) == 0x10,
              "StackArray must match the engine's array header");

template <class T>
void StackArrayPush(StackArray<T>& a, T value, int align) {
    const int32_t oldN = a.count;
    const int32_t newN = oldN + 1;
    T* oldBuf = a.base;
    a.count = newN;
    if (a.cap < newN) {
        a.cap = (oldN != 0) ? newN * 3 : newN;
        a.base = static_cast<T*>(System::MemoryAllocator::MemAlloc(
            static_cast<uint64_t>(a.cap) * sizeof(T), align));
    }
    const int32_t start = (oldBuf == a.base) ? oldN : 0;
    for (int32_t i = start; i < newN; ++i) {
        T* dst = a.base + i;
        if (!dst) continue;
        *dst = (i < oldN) ? oldBuf[i] : value;
    }
    if (oldBuf != a.base) System::MemoryAllocator::MemFree(oldBuf);
}

}  // namespace

// ============================================================
// 0x14005b580 — generate the docked merchant's MANIFEST
// ============================================================
// Runs once, on arrival. It throws away any previous manifest, rolls which of the
// description's merchants turned up, and fills +0x50 with the goods that merchant is
// offering and what it wants for them.
//
// Where the goods come from depends on the post's per-merchant record at +0x30:
//   order kind (+0x08) is 0, or its list is empty
//        -> the generic buy list from 0x140059670 (every raw material the town knows
//           about, plus the merchant's own goods list and select-type matches), at the
//           ValueDescription's base price;
//   otherwise
//        -> exactly the goods on that record's list, at 1.25x (DAT_140172158) the base
//           price. Retail then FREES that list unless the kind is 2, so a kind-1 order
//           is consumed by the arrival it was placed for.
//
// The number of distinct rolls is merchant[+0x40] / tier[+0x0c], at least 1. Each roll
// picks a good at random; a repeat roll adds to the existing manifest line's stock
// rather than adding a second line.
//
// Per-good figures come from the good's ValueDescription:
//   +0x40 base price  +0x44 stock numerator (divided by tier[+0x10])  +0x48 stock floor
void TradeComponent::BuildMerchantManifest() {
    // Drop the previous manifest. +0x58 is cleared as one qword: count AND capacity.
    System::MemoryAllocator::MemFree(manifest._data);
    manifest._data = nullptr;
    manifest._size = 0;
    manifest._capacity = 0;
    tradeId = -1;

    System::Random* rng = GameUpdateLoop::_singleton->m_random;
    TradeDescription* desc = TradeDesc(this);

    // Roll which merchant arrived, and remember it: everything below indexes on it, and
    // so does the panel.
    const int merchantIdx = static_cast<int>(rng->Rand(
        static_cast<uint32_t>(desc->_merchantCount)));
    tradeId = merchantIdx;

    const TradeMerchant& merchantRec = desc->_merchants[merchantIdx];
    const int tierIdx = PickMerchantTier();
    const TradeTier& tier = desc->_prices[tierIdx];

    StackArray<ComponentDescription*> goods  = {nullptr, 0, 0};   // the goods on offer
    StackArray<ValueDescription*>     values = {nullptr, 0, 0};   // their ValueDescriptions, index-aligned

    // Where the goods list comes from, and whether it is marked up.
    TradeMerchantOrder& rec = merchantOrders._data[merchantIdx];
    bool fromOrder = false;
    if (rec.orderKind == 0 || rec.goods._size < 1) {
        GetBuyEntries(&goods);
    } else {
        fromOrder = true;
        const int32_t n = rec.goods._size;
        goods.count = n;
        goods.cap   = n;
        goods.base  = static_cast<ComponentDescription**>(
            System::MemoryAllocator::MemAlloc(static_cast<uint64_t>(n) * 8, 8));
        ComponentDescription** src = rec.goods._data;
        for (int32_t i = 0; i < n && goods.base; ++i)
            goods.base[i] = src[i];
    }

    // Consume the order unless it is the persistent kind.
    if (rec.orderKind != 2) {
        System::MemoryAllocator::MemFree(rec.goods._data);
        rec.goods._data = nullptr;
        rec.goods._size = 0;    // count and capacity together
        rec.goods._capacity = 0;
        rec.orderKind = 0;
    }

    // Each good's ValueDescription, pushed even when absent so the two arrays stay
    // index-aligned — the roll below indexes both with the same number.
    for (int32_t i = 0; i < goods.count; ++i)
        StackArrayPush(values, FindSubDescription<ValueDescription>(goods.base[i]), 8);   // cell 0x1401c4bf0

    // STANDALONE-ONLY guard, not a softened retail deref. Retail rolls at least once and
    // indexes `values` without checking, because its data guarantees both a non-empty buy
    // list and a ValueDescription on every entry. In standalone both can be empty for a
    // reason that has nothing to do with the data: the ComponentDescription and
    // ValueDescription type-ids (0x1401c7318 / 0x1401c4bf0) sit behind
    // BANISHED_LIVE_TRADE_TIDS, and with that gate off 0x140059670's ExternalFactory walk
    // matches nothing and every ValueDescription lookup returns null. Faulting on a flag
    // the player can turn off reproduces nothing, so name the flag instead.
    if (values.count <= 0) {
        std::fprintf(stderr, "[TradeDock] manifest: NO GOODS to offer (buy list empty). "
                             "The ExternalFactory walk over ComponentDescription "
                             "matched nothing.\n");
        std::fflush(stderr);
        System::MemoryAllocator::MemFree(values.base);
        System::MemoryAllocator::MemFree(goods.base);
        return;
    }

    int rolls = merchantRec.stockRolls / tier.rollDivisor;
    if (rolls < 1) rolls = 1;

    for (int r = 0; r < rolls; ++r) {
        const int pick = static_cast<int>(rng->Rand(static_cast<uint32_t>(values.count)));
        const ValueDescription* vd = values.base[pick];
        if (!vd) {
            // Same standalone-only reasoning as above: with the type-id gate off EVERY
            // entry is null. Say so once rather than faulting on vd+0x44.
            static bool warned = false;
            if (!warned) {
                warned = true;
                std::fprintf(stderr, "[TradeDock] manifest: good #%d has NO ValueDescription "
                                     "(ValueDescription type-id %u) — skipping the roll\n",
                             pick, ValueDescription::TypeId());
                std::fflush(stderr);
            }
            continue;
        }

        int stock = vd->_baseValue / tier.stockDivisor;
        if (stock < vd->_weight)
            stock = vd->_weight;
        if (stock < 1) stock = 1;

        int price = vd->_i40;
        if (fromOrder) price = static_cast<int>(static_cast<float>(price) * 1.25f);  // DAT_140172158
        if (price < 1) price = 1;

        ComponentDescription* good = goods.base[pick];

        // Already on the manifest? Add to its stock and move on.
        bool merged = false;
        const int lineCount = manifest._size;
        for (int i = 0; i < lineCount; ++i) {
            TradeManifestLine& line = manifest._data[i];
            if (line.good != good) continue;
            line.stock += stock;
            merged = true;
            break;
        }
        if (merged) continue;

        // Append a new manifest line {good, price, stock}, growing +0x50 in place.
        TradeManifestLine entry;
        entry.good = good;
        entry.price = price;
        entry.stock = stock;

        const int oldN = manifest._size;
        const int newN = oldN + 1;
        TradeManifestLine* oldBuf = manifest._data;
        manifest._size = newN;
        if (manifest._capacity < newN) {
            manifest._capacity = (oldN != 0) ? newN * 3 : newN;
            manifest._data = static_cast<TradeManifestLine*>(System::MemoryAllocator::MemAlloc(
                      static_cast<uint64_t>(manifest._capacity) << 4, 8));
        }
        const int start = (oldBuf == manifest._data) ? oldN : 0;
        for (int i = start; i < newN; ++i) {
            TradeManifestLine* dst = manifest._data + i;
            if (!dst) continue;
            const TradeManifestLine* src = (i < oldN) ? oldBuf + i : &entry;
            *dst = *src;
        }
        if (oldBuf != manifest._data)
            System::MemoryAllocator::MemFree(oldBuf);
    }

    System::MemoryAllocator::MemFree(values.base);
    values.base = nullptr; values.count = 0; values.cap = 0;
    System::MemoryAllocator::MemFree(goods.base);

    // OUTCOME, not intent: the line count is the only thing that says the merchant has
    // anything to sell. "The manifest was generated" and "the manifest has goods in it"
    // are different claims and only the readback proves the second.
}

// ============================================================
// 0x140059ad0 — AUTO-TRADE against the standing buy orders
// ============================================================
// The post trades with the docked merchant with no player involvement. For each
// standing buy order, in order:
//
//   1. price up everything unreserved in the post's own storage — that sum is the
//      post's spending limit for this order;
//   2. collect the manifest lines whose raw-material flags (+0x5c) hit the order's
//      category mask, and how many units of them the merchant has;
//   3. buy ONE unit at a time, round-robin over those lines, until the order is
//      filled, the money runs out, the merchant runs out, or the next unit would
//      not fit in the post's storage volume (StorageComponent+0x38). Each purchase
//      decrements the merchant's stock and tallies the good in an AVL map;
//   4. pay for it by selling the post's own stored goods, unit by unit, crediting
//      the town's trade total (townMgr+0x1ac) and per-category counter
//      (townMgr+0x170[cat]+0x0c) as each unit goes.
//
// Only then, once every order has been processed, are the purchases DELIVERED: the
// tally is walked in key order and each good is registered as available to the town
// (0x140091e60 — which is NOT "Game_DestroyEntity", see tradeui_checkout.cpp) and added
// to the post's storage.
//
// Deliveries are deferred to the end on purpose: the sell phase walks the same storage
// array, and adding the purchases first would make the post pay for its new goods with
// its new goods.
//
// LEFT: the leaves 0x14012cf10 / 0x14012d000 / 0x140088f90 take their operands as integers (their owners'
// declarations).
void TradeComponent::AutoTrade() {
    ExtendMerchantStay();

    StorageComponent* storage = _entity->FindComponent<StorageComponent>();   // 0x140006270

    CitizenBehavior* const townMgr = GameUpdateLoop::_singleton ? GameUpdateLoop::_singleton->m_citizenList : nullptr;

    TradeStockTree tally;   // the purchase tally (0x140059b2b / 0x140059b2f; its count: nothing reads it back)

    int volume = storage->QueryCount();   // 0x14012cf10
    const int volumeCap = storage->capacity;

    const int orderCount = buyOrders._size;
    for (int o = 0; o < orderCount; ++o) {
        const TradeBuyOrder& order = buyOrders._data[o];
        const int orderAmount = order.amount;
        if (orderAmount == 0) {
            // OUTCOME, not intent: an order row with amount 0 is a category the player
            // never asked for. Retail skips it, and a post with 14 all-zero rows buying
            // nothing is CORRECT behaviour, not a broken buy loop.
            continue;
        }
        const uint32_t orderMask = order.rawMaterialFlags;

        // ── 1. what the post can afford: everything unreserved in its storage ──────
        int sellValue = 0;
        {
            const int n = storage->storageSlots._size;
            for (int j = 0; j < n; ++j) {
                const StorageComponent::Item& item = storage->storageSlots[j];
                const int have = item._count - item._reserve;
                const int unit = GetResourcePrice(item._resource);
                sellValue += unit * have;
            }
        }

        // ── 2. which manifest lines this order matches ────────────────────────────
        StackArray<int32_t> candidates = {nullptr, 0, 0};
        int availQty = 0;
        {
            const int lines = manifest._size;
            for (int k = 0; k < lines; ++k) {
                const TradeManifestLine& line = manifest._data[k];
                RawMaterialDescription* rawMat =
                    FindSubDescription<RawMaterialDescription>(line.good);   // cell 0x1401c7cb8 (D5)
                if (!rawMat) continue;
                if ((rawMat->_flags & orderMask) == 0) continue;
                StackArrayPush(candidates, static_cast<int32_t>(k), 4);
                availQty += line.stock;
            }
        }

        // ── 3. buy, one unit per round-robin step ─────────────────────────────────
        int bought = 0, spent = 0, cursor = 0;
        if (availQty > 0) {
            for (;;) {
                if (bought >= orderAmount) break;      // order filled
                if (spent >= sellValue) break;         // out of money

                const int lineIdx = candidates.base[cursor];
                TradeManifestLine& line = manifest._data[lineIdx];
                // Every candidate was selected BY having a RawMaterialDescription, so
                // this second lookup on the same good cannot miss. Retail derefs its
                // result unguarded for exactly that reason.
                const RawMaterialDescription* rawMat =
                    FindSubDescription<RawMaterialDescription>(line.good);

                if (line.stock > 0) {
                    const int next = volume + rawMat->_weight;
                    if (next > volumeCap) break;       // no room for the next unit
                    volume = next;
                    spent += line.price;
                    if (AvlInsert(tally, line.good))
                        ++tally.count;
                    line.stock -= 1;
                    ++bought;
                }

                if (++cursor >= candidates.count) cursor = 0;
                if (bought >= availQty) break;         // merchant is out of stock
            }
        }

        // ── 4. sell to cover `spent` ──────────────────────────────────────────────
        {
            int paid = 0;
            for (int idx = 0; idx < storage->storageSlots._size; ++idx) {
                const StorageComponent::Item& entry = storage->storageSlots[idx];
                ComponentDescription* const resource = entry._resource;
                const int unit = GetResourcePrice(resource);
                const int avail = entry._count - entry._reserve;
                if (avail == 0) continue;
                // FIX autotrade_free_goods (NOT RETAIL, docs/retail_bugs.md §24): a good the merchant
                // prices 0 pays nothing; retail sells the whole stack of it here.
                if (Ext::AutotradeFreeGoods::SkipInPayment(Ext::Fix().autotrade_free_goods, unit)) continue;

                int sold = 0;
                while (paid < spent) {
                    if (sold >= avail) break;
                    paid += unit;
                    ++sold;

                    townMgr->field_0x1AC += unit;
                    // Retail passes the unit price where every other caller of 0x140088f90
                    // passes a StorageComponent. The function ignores its first parameter,
                    // so the body (which reads the material only) is called with it alone.
                    const int cat = ResourceCategory(resource);   // 0x140088f90
                    if (cat >= 0) {
                        townMgr->professionResources._data[cat].field_0x0C += 1;
                    }

                    // Take the unit out of storage, by identity. An entry that reaches
                    // zero is erased and the tail shifted down.
                    const int n = storage->storageSlots._size;
                    for (int j = 0; j < n; ++j) {
                        StorageComponent::Item& slot = storage->storageSlots[j];
                        if (slot._resource != resource) continue;
                        slot._count -= 1;
                        if (storage->storageSlots[j]._count == 0) {
                            for (int m = j; m + 1 < storage->storageSlots._size; ++m) {
                                StorageComponent::Item* p = &storage->storageSlots[m];
                                if (!p) continue;
                                p[0] = p[1];
                            }
                            storage->storageSlots._size -= 1;
                        }
                        break;
                    }

                    // The volume freed by that unit. Same "cannot miss" reasoning as the
                    // purchase loop: the entry is in storage, so it is a raw material.
                    const RawMaterialDescription* soldRaw = FindSubDescription<RawMaterialDescription>(resource);
                    volume -= soldRaw->_weight;
                }

                // The entry was drained to nothing and therefore erased: re-test this
                // index, because a different entry now occupies it.
                if (avail == sold) --idx;
            }
        }

        System::MemoryAllocator::MemFree(candidates.base);
    }

    // ── DELIVER the purchases ─────────────────────────────────────────────────────
    int delivered = 0, deliveredLines = 0;
    for (TradeResourceNode* n = AvlLeftmost(tally.root); n != nullptr; n = AvlNext(n)) {
        ComponentDescription* good = n->resource;
        const int units = n->quantity;
        townMgr->RegisterRawMaterial(good);   // 0x140091e60
        storage->AddItem(good, units);   // 0x14012d000
        delivered += units;
        ++deliveredLines;
    }
    (void)delivered; (void)deliveredLines;
    // ~tally at the scope's end: retail's inlined release, 0x14005a440..0x14005a470.

    // OUTCOME, not intent: "auto-trade ran" and "the post bought something" are separate
    // claims. A run with orders but zero deliveries is the interesting case — it means
    // the post could not afford, could not fit, or was offered nothing that matched.
}

// ============================================================
// state 0x13 ENTER — 0x14005a800
// ============================================================
// The merchant has tied up. Generate what it brought, give it 900 game-seconds
// (DAT_140172244) before it leaves again, and auto-trade now if the post is set to
// trade on arrival (+0x28 == 1).
//
// Retail's tail is `MOV AL,1; RET`.
char TradeComponent::EnterDocked() {
    BuildMerchantManifest();

    // The Time ctor writes EIGHT bytes through its return slot — see
    // reference_time_ctor_8byte_clobber.
    {
        System::Time stay = {0};
        merchantTimer = *RtSystem_Time_Ctor(&stay, 900.0f);
    }

    if (tradeState == 1) AutoTrade();
    return 1;
}

// ============================================================
// state 0x13 UPDATE — 0x14005a6d0
// ============================================================
// The merchant is leaving. Auto-trade now if the post is set to trade on departure
// (+0x28 == 2), then post event 0 to the merchant boat's own state machine — that is
// what makes it cast off — drop the reference, and free the manifest.
//
// This is the UPDATE slot (+0x20), which the generic dispatcher only invokes when a
// transition out of the state is pending. Read off 0x14000dd70's disassembly: its
// arguments land as id -> +0x00, RDX -> +0x08 enter, R8 -> +0x20 update, and the
// FIFTH argument (on the stack at [RSP+0x170], which the decompiler renders as an
// `in_R9` phantom) -> +0x18 trans38. So c650's `dd70(0x13, a800, a6d0, _, a780)`
// means enter=a800, update=a6d0, per-frame=a780.
char TradeComponent::LeaveDocked() {
    if (tradeState == 2) AutoTrade();

    StateMachineComponent* smComp = merchant->FindComponent<StateMachineComponent>();   // 0x140003ea0

    // Retail 0x14005a6d0 derefs the chain unguarded (CheckPtr, then `[[sm+0x48]]+0x10`): a
    // merchant boat always carries a StateMachineComponent (B6 G3: the tree's null arm is
    // gone). Node vtable slot 2 (+0x10) is PostEvent(node, sm, curStateIdx, eventId); the
    // current index is the machine's activeState (smComp+0x51). Event 0 is "you are released".
    ControllerComponent::Controller* node = smComp->stateMachine.current;
    node->PostEvent(&smComp->stateMachine, smComp->stateMachine.activeState, 0);

    merchant = nullptr;

    System::MemoryAllocator::MemFree(manifest._data);
    manifest._data = nullptr;
    manifest._size = 0;
    manifest._capacity = 0;
    tradeId = -1;
    return 1;
}
