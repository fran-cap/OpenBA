// src/game/Component/tradecomponent_serialize.cpp
// TradeComponent::Persist (vtable slot [3], +0x18, retail 0x14005bd50).
//
// Ghidra names this function "Shutdown" (same off-by-one mislabel family as
// DiseaseComponent's slot-3/4/6/7 — see that file's header). tradecomponent.cpp
// already flagged this exact address as "delegated to original exe (too
// complex for safe reimpl)"; this file is that port, done in full.
//
// Field table (self is the 0xa8-byte TradeComponent body):
//   +0x50/+0x58  ComponentRef table, stride 0x10, count @+0x58
//                elem+0x00 ComponentRef (ExternalPtr<ComponentDescription>,
//                          ::Object::SerializeExternal / retail 0x140002340)
//                elem+0x08 int   elem+0x0c int
//                resize (load only) through the member's SetSize (retail 0x14000f090,
//                the generic 0x10-stride Array<{u64,u64}>::SetSize).
//   +0x2c        int — selected-merchant index (-1 = none; see the ctor note
//                in tradecomponent.cpp).
//   +0x60        AVL tree {root@+0x00, count@+0x08} keyed by ComponentRef,
//                value int. See the TradeBst_* block below.
//   +0x30/+0x38  merchant-order array, stride 0x20, count @+0x38 (the SAME array
//                TradeComponent::MerchantOrderArray::SetSize in tradecomponent.cpp
//                grows/shrinks — reused here, not re-implemented).
//                elem+0x00 String (System::String::Serialize, retail 0x140151238)
//                elem+0x08 int
//                elem+0x10 ptr-array {ptr,count:i32,cap:i32} of
//                          ExternalPtr<ComponentDescription>; count is
//                          elem+0x18, resized (load only) via
//                          TradeMerchantOrder::GoodsList::SetSize
//                          (retail 0x140001bb0), contents via
//                          ::Object::SerializeExternalArray<ComponentDescription>
//                          (retail 0x1400049c0, object/External.h).
//   +0x40/+0x48  buy orders, stride 0xc, count @+0x48
//                (Collection::Array<TradeBuyOrder>::SetSize, reused from tradecomponent.cpp).
//                elem+0x00 int resourceId — an ENUM, serialized through the
//                          serializer's own "encoding" object (ser+8's vtable
//                          +0x98), the same idiom already ported repeatedly
//                          elsewhere (see storagedescription.cpp's rawMaterialFlags).
//                elem+0x08 int   elem+0x04 int   (retail's own field order —
//                          +0x08 before +0x04, kept faithful)
//   +0x28        int (plain)
//   +0x78        Time
//   +0x71        bool
//   +0x80        InternalBasePtr, validated against ComponentEntity
//                (ComponentEntity::TypeId()) and nulled on type mismatch —
//                same SerializeValidatedIBP idiom as animalwave_serialize.cpp.
//   +0x70        bool
//
// Retail also constructs+destructs an empty version-0 Serializer right at
// entry (0x14005bd7d/0x14005bd87) before building the real one used for
// everything else. It is not free in the stream, and Persist below opens it too.
//
// ---------------------------------------------------------------------------
// +0x60 AVL tree — node layout (0x30 bytes), verified byte-exact from the
// disassembly of 0x14000ebe0 (this field's insert/save body), 0x14000e360
// (AVL retrace), and 0x14000e580/0x14000e450 (the two single rotations):
//   +0x00 key (ComponentRef, 8B)   +0x08 value (int, low 32 bits only)
//   +0x10 parent   +0x18 left   +0x20 right
//   +0x28 balance (int16, right-height - left-height)   +0x2a height (int16)
// This is a SEPARATE compiler instantiation of the same AVL-tree template the
// project already ported once for MarketWork (marketwork_bst_a9f0.cpp) — same
// shape, different field offsets, so it needed its own port rather than reuse.
//
// The node's value scratch is never zero-initialized by retail before the
// int-serialize call that fills only its low 32 bits, so retail's node ends
// up with garbage high 32 bits that nothing ever reads back. We zero-init
// instead of replicating that specific piece of undefined behaviour.

#include "Component/tradecomponent_funcs.h"
#include "Entity/ComponentEntity.h"
#include "object/TypeInfo.h"
#include "system/String.h"
#include "io/Serializer.h"   // ENDGAME B.3.4 caller batch A
#include <cstdint>
#include <new>
#include "object/Internal.h"   // ENDGAME B.3.4 caller batch G
#include "system/Value.h"   // ENDGAME B.3.4b System caller batch
#include "system/MemoryAllocator.h"
#include "Component/exe_bodies.h"
#include "Game/exe_bodies.h"
#include "Component/ComponentDescription.h"   // B8 RT2: ::Object::SerializeExternal<ComponentDescription> (the downcast needs it complete)

namespace {

// True when the serializer is READING (IO::Serializer::GetDirection, retail 0x180006210).
inline bool Loading(IO::Serializer& ser) {
    return static_cast<int>(ser.GetDirection()) == 0;
}

// ============================================================================
// +0x60 field — AVL tree of ComponentRef -> int
// ============================================================================
inline int16_t TradeBst_ChildHeightPlus1(const TradeResourceNode* child) {
    return child ? static_cast<int16_t>(child->height + 1) : 0;
}
inline void TradeBst_Recompute(TradeResourceNode* n) {
    int16_t lh = TradeBst_ChildHeightPlus1(n->left);
    int16_t rh = TradeBst_ChildHeightPlus1(n->right);
    n->balance = static_cast<int16_t>(rh - lh);
    n->height  = (rh > lh) ? rh : lh;
}

// 0x14000e580 — rotate RIGHT at `node` (node's LEFT child becomes the subtree root).
TradeResourceNode* TradeBst_RotateRight(TradeResourceNode** rootSlot, TradeResourceNode* node) {
    TradeResourceNode* pivot = node->left;
    if (node == *rootSlot) *rootSlot = pivot;
    if (TradeResourceNode* parent = node->parent) {
        if (parent->left == node)
            parent->left = pivot;
        else
            parent->right = pivot;
    }
    pivot->parent = node->parent;
    TradeResourceNode* pivotRight = pivot->right;
    node->left = pivotRight;
    if (pivotRight) pivotRight->parent = node;
    pivot->right = node;
    node->parent = pivot;

    if (TradeResourceNode* pl = pivot->left) TradeBst_Recompute(pl);
    if (TradeResourceNode* pr = pivot->right) TradeBst_Recompute(pr);
    TradeBst_Recompute(pivot);
    return pivot;
}

// 0x14000e450 — rotate LEFT at `node` (node's RIGHT child becomes the subtree root).
TradeResourceNode* TradeBst_RotateLeft(TradeResourceNode** rootSlot, TradeResourceNode* node) {
    TradeResourceNode* pivot = node->right;
    if (node == *rootSlot) *rootSlot = pivot;
    if (TradeResourceNode* parent = node->parent) {
        if (parent->left == node)
            parent->left = pivot;
        else
            parent->right = pivot;
    }
    pivot->parent = node->parent;
    TradeResourceNode* pivotLeft = pivot->left;
    node->right = pivotLeft;
    if (pivotLeft) pivotLeft->parent = node;
    pivot->left = node;
    node->parent = pivot;

    if (TradeResourceNode* pl = pivot->left) TradeBst_Recompute(pl);
    if (TradeResourceNode* pr = pivot->right) TradeBst_Recompute(pr);
    TradeBst_Recompute(pivot);
    return pivot;
}

} // namespace

// 0x14000e360 — AVL retrace/rebalance, walking from `startNode` up to the root.
void TradeStockTree::Retrace(TradeResourceNode* startNode) {
    TradeResourceNode** rootSlot = &root;   // RCX: the header, whose +0x00 is the root
    TradeResourceNode* node = startNode;
    while (node) {
        TradeResourceNode* parent = node->parent;
        TradeBst_Recompute(node);

        int16_t bal = node->balance;
        if (bal > 1) {
            TradeResourceNode* right = node->right;
            if (right) {
                int16_t rbal = right->balance;
                if (rbal < 0) TradeBst_RotateRight(rootSlot, right);
                TradeBst_RotateLeft(rootSlot, node);
            }
        }
        bal = node->balance;
        if (bal < -1) {
            TradeResourceNode* left = node->left;
            if (left) {
                int16_t lbal = left->balance;
                if (lbal > 0) TradeBst_RotateLeft(rootSlot, left);
                TradeBst_RotateRight(rootSlot, node);
            }
        }
        node = parent;
    }
}

TradeResourceNode* TradeResourceTree_NewNode(ComponentDescription* key, int32_t value, TradeResourceNode* parent) {
    TradeResourceNode* n = static_cast<TradeResourceNode*>(System::MemoryAllocator::MemAlloc(0x30, 8));
    if (!n) return nullptr;
    n->resource = key;
    n->quantity = value;
    n->_pad0C[0] = n->_pad0C[1] = n->_pad0C[2] = n->_pad0C[3] = 0;   // the tree's zero high dword
    n->parent   = parent;
    n->left     = nullptr;
    n->right    = nullptr;
    n->balance  = 0;
    n->height   = 0;
    return n;
}

namespace {

// 0x14000ebe0 — the whole +0x60 field body: count-prefixed, then either an
// ordered insert-if-absent loop (loading) or an in-order walk of the existing
// tree (saving, no mutation).
void TradeComp_BstSerialize(TradeComponent& trade, IO::Serializer& ser) {
    int32_t count = trade.stock.count;
    ser.Serialize(count, nullptr);

    if (Loading(ser)) {
        for (int32_t i = 0; i < count; ++i) {
            ComponentDescription* key = nullptr;
            ::Object::SerializeExternal<ComponentDescription>(ser, key);
            int32_t value = 0;
            ser.Serialize(value, nullptr);

            TradeResourceNode* root = trade.stock.root;
            if (!root) {
                trade.stock.root = TradeResourceTree_NewNode(key, value, nullptr);
                trade.stock.count += 1;
                continue;
            }

            TradeResourceNode* cur = root;
            for (;;) {
                ComponentDescription* curKey = cur->resource;
                if (key < curKey) {
                    TradeResourceNode* left = cur->left;
                    if (!left) {
                        cur->left = TradeResourceTree_NewNode(key, value, cur);
                        trade.stock.Retrace(cur);
                        trade.stock.count += 1;
                        break;
                    }
                    cur = left;
                    continue;
                }
                if (curKey >= key) break;   // duplicate key: retail skips, no update
                TradeResourceNode* right = cur->right;
                if (!right) {
                    cur->right = TradeResourceTree_NewNode(key, value, cur);
                    trade.stock.Retrace(cur);
                    trade.stock.count += 1;
                    break;
                }
                cur = right;
            }
        }
        return;
    }

    // SAVE: in-order walk, no mutation.
    TradeResourceNode* n = trade.stock.root;
    if (!n) return;
    while (TradeResourceNode* l = n->left) n = l;
    while (n) {
        ::Object::SerializeExternal<ComponentDescription>(ser, n->resource);
        ser.Serialize(n->quantity, nullptr);

        TradeResourceNode* r = n->right;
        if (r) {
            n = r;
            while (TradeResourceNode* l = n->left) n = l;
        } else {
            TradeResourceNode* p = n->parent;
            while (p && p->left != n) {
                n = p;
                p = p->parent;
            }
            n = p;
        }
    }
}

} // namespace

// ---------------------------------------------------------------------------
// TradeStock_SeedForTest — TEST FIXTURE, not retail. See src/diag/trade_buy_harness.cpp.
// ---------------------------------------------------------------------------
// The trade panel builds its SELL rows by walking this same +0x60 AVL tree and
// skipping any node whose value is 0, so a post whose tree is empty shows an empty
// sell table however much the town is holding -- which is exactly what an unattended
// run produces, because filling the tree is the player's job (they tick resources for
// trade and haulers deliver). With no rows there is nothing to sell, units-traded
// stays 0, and six achievements are unreachable.
//
// This inserts-or-updates one {resource, quantity} pair using the file's own AVL
// helpers, i.e. the same insert retail's load leg performs -- with one deliberate
// difference: retail's loader SKIPS a duplicate key (it is reading a stream it trusts
// to be unique), whereas a fixture called every visit has to be able to top a resource
// back up, so an existing key has its value overwritten.
extern "C" void TradeStock_SeedForTest(TradeComponent* tradeComp, ComponentDescription* resourceEntity, int qty) {
    if (!tradeComp || !resourceEntity) return;
    TradeComponent& trade = *tradeComp;
    ComponentDescription* const key = resourceEntity;

    TradeResourceNode* root = trade.stock.root;
    if (!root) {
        TradeResourceNode* node = TradeResourceTree_NewNode(key, qty, nullptr);
        if (!node) return;
        trade.stock.root = node;
        trade.stock.count += 1;
        return;
    }

    TradeResourceNode* cur = root;
    for (;;) {
        ComponentDescription* const curKey = cur->resource;
        if (key == curKey) {
            cur->quantity = qty;   // top up, see note above (the tree wrote the whole qword)
            cur->_pad0C[0] = cur->_pad0C[1] = cur->_pad0C[2] = cur->_pad0C[3] = qty < 0 ? 0xff : 0;
            return;
        }
        TradeResourceNode** childSlot = (key < curKey) ? &cur->left : &cur->right;
        if (!*childSlot) {
            TradeResourceNode* node = TradeResourceTree_NewNode(key, qty, cur);
            if (!node) return;
            *childSlot = node;
            trade.stock.Retrace(cur);
            trade.stock.count += 1;
            return;
        }
        cur = *childSlot;
    }
}


// ============================================================
// TradeComponent::Persist (vtable slot [3], 0x14005bd50)
// ============================================================
void TradeComponent::Persist(IO::Stream& stream) {
    Component::Persist(stream);   // retail: CALL 0x1401189b0 (qualified)

    // Retail 0x14005bd50 opens TWO Serializer scopes: an EMPTY one that is
    // constructed and immediately destroyed (the controller-section stub), and
    // only then the scope that carries the body.
    //
    //     Component_Serialize(param_1, param_2);
    //     IO::Serializer::Serializer(local_30, param_2, 0);   <- this one
    //     IO::Serializer::~Serializer(local_30);
    //     IO::Serializer::Serializer(&local_40, param_2, 0);  <- the body
    //
    // An empty scope is NOT free: it is 16 real bytes in the save encoding. This
    // port opened only the second, so it read 16 bytes too few at the very start
    // of the body -- every field below then came from the wrong offset, refCount
    // and nameCount decoded as poison, and the merchant-order resize walked a
    // garbage array. Same trap as the two empty scopes in AbsorbResourceComponent
    // and HealerComponent.
    { IO::Serializer stub(stream, 0); }

    IO::Serializer ser(stream, 0);

    // +0x50/+0x58: the manifest, stride 0x10 {ComponentRef, int, int}.
    int32_t refCount = manifest._size;
    ser.Serialize(refCount, nullptr);
    if (Loading(ser))
        manifest.SetSize(refCount);
    {
        TradeManifestLine* elem = manifest._data;
        for (int32_t i = 0; i < refCount && elem; ++i, ++elem) {
            ::Object::SerializeExternal<ComponentDescription>(ser, elem->good);
            ser.Serialize(elem->price, nullptr);
            ser.Serialize(elem->stock, nullptr);
        }
    }

    // +0x2c: the docked merchant's index.
    ser.Serialize(tradeId, nullptr);

    // +0x60: AVL tree, ComponentRef -> int.
    TradeComp_BstSerialize(*this, ser);

    // +0x30/+0x38: the merchant orders, stride 0x20.
    int32_t nameCount = merchantOrders._size;
    ser.Serialize(nameCount, nullptr);
    if (Loading(ser))
        merchantOrders.SetSize(nameCount);
    {
        TradeMerchantOrder* elem = merchantOrders._data;
        for (int32_t i = 0; i < nameCount && elem; ++i, ++elem) {
            elem->merchantName.Serialize(ser);
            int32_t innerCount = elem->goods._size;
            ser.Serialize(innerCount, nullptr);
            // retail 0x140001bb0 IS Collection::Array<T>::SetSize(n): stride 8 align 8,
            // new tail zeroed (kSpec_PtrZero).
            if (Loading(ser))
                elem->goods.SetSize(innerCount);
            ::Object::SerializeExternalArray<ComponentDescription>(ser, innerCount, elem->goods._data);   // 0x1400049c0
            ser.Serialize(elem->orderKind, nullptr);
        }
    }

    // +0x40/+0x48: the buy orders, stride 0xc {flags(enum), index, amount}.
    int32_t itemCount = buyOrders._size;
    ser.Serialize(itemCount, nullptr);
    if (Loading(ser))
        buyOrders.SetSize(itemCount);
    {
        TradeBuyOrder* elem = buyOrders._data;
        for (int32_t i = 0; i < itemCount && elem; ++i, ++elem) {
            // The enum through the UINT door (vtable +0x98), as storagedescription.cpp's
            // rawMaterialFlags. Retail's own field order: +0x08 before +0x04.
            ser.Serialize(elem->rawMaterialFlags, nullptr);
            ser.Serialize(elem->amount, nullptr);
            ser.Serialize(elem->categoryIndex, nullptr);
        }
    }

    // +0x28: plain int.
    ser.Serialize(tradeState, nullptr);

    // +0x78: Time.
    merchantTimer.Serialize(ser);

    // +0x71: bool.
    ser.Serialize(tradeFlag, nullptr);

    // +0x80: InternalBasePtr, nulled when it is not a ComponentEntity.
    merchant.Serialize(ser, ComponentEntity::TypeId());

    // +0x70: bool.
    ser.Serialize(workEnabled, nullptr);
}
