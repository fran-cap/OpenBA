// src/game/Component/tradedescription.cpp
// TradeDescription (entity description block) — trading post / merchant config.
// Extends Object::External (0x20 base). Vtable 0x14015ad60. Parent "Description".
// Size 0x70. GetVersion=7 (slot +0x18 @ 0x14005ca00: mov eax,7; ret).
//
// Ctor defaults (TradeDescription_sub_caa0): all fields zero except
//   float @ +0x68 = 0x3eaa7efa (~0.333)
//   int   @ +0x6c = 3
// Serialize (vtable +0x28, 0x14005c7f0) — Serializer ctor with version=1, stream order
// (verified disasm CALLs at 0x14005c7f0):
//   ExternalPtr @ +0x30  (FUN_140002340 -> Reimpl_ExternalPtr_Serialize_StringTable)
//   int count   @ +0x50 -> resize +0x48 merchant array (element 0x48), per merchant:
//        ExternalPtr @+0x00 (FUN_14000bf40)
//        String      @+0x08
//        int count   @+0x28 -> resize ext-array @+0x20 -> Desc_LoadExternalArray
//        int count   @+0x38 -> resize int-array @+0x30 -> per-elem enum vcall +0x98
//        int         @+0x40
//        enum @+0x10, enum @+0x14, enum @+0x18 (encoding vcall +0x98)
//   int count   @ +0x60 -> resize +0x58 array (element stride 0x14), per element:
//        int @+0x00, float @+0x04, float @+0x08, int @+0x0c, int @+0x10
//   ExternalPtr @ +0x20  (FUN_14000bd30 -> Reimpl_ExternalPtr_Serialize_StringTable)
//   ExternalPtr @ +0x28  (FUN_14000bf40 -> Reimpl_ExternalPtr_Serialize_StringTable)
//   int count   @ +0x40 -> resize +0x38 array (element stride 0x18), per element:
//        enum @+0x00 (encoding vcall +0x98), String @+0x08, String @+0x10
//   float @ +0x68
//   int   @ +0x6c
// ToolKit fields: merchants (ships / goods / prices).
// Decompiled READ-ONLY from Application-x64.exe.
//
// ENDGAME B.3.8 K1: a REAL class (TradeDescription.h) with a compiler-generated vtable;
// the hand-built 11-slot table and its RegisterExeAddr(0x14015ad60) row are gone
// (nothing in the tree read that address back — B38_RULES D5).

#include "Component/TradeDescription.h"
#include "Component/TradeComponent.h"
#include "object/Internal.h"      // Object::detail::InternalMemAlloc / InternalMemFree
#include "io/Serializer.h"
#include "Object/factory_registration.h"
#include "object/External.h"
#include "Component/ComponentDescription.h"   // the SerializeExternalArray<ComponentDescription> instantiation
#include "object/TypeInfo.h"
#include "Game/exe_bodies.h"
#include "system/String.h"
#include "system/MemoryAllocator.h"
#include <cstring>
#include <cstdint>
#include <new>
#include "ui/SpriteSheet.h"   // B8 RT2: ::Object::SerializeExternal<UI::SpriteSheet> (the downcast needs it complete)
#include "Component/ComponentDescription.h"   // B8 RT2: ::Object::SerializeExternal<ComponentDescription> (the downcast needs it complete)
#include "ui/StringTable.h"   // B8 RT2: ::Object::SerializeExternal<UI::StringTable> (the downcast needs it complete)

namespace {
// The retail ctor stores raw IEEE bit patterns; this keeps them exact.
inline float FloatBits(uint32_t bits) { float f; std::memcpy(&f, &bits, sizeof f); return f; }
}  // namespace


namespace {
// A loaded array's resize: retail's SetSize bodies (0x14000f370 merchants, 0x14000f6a0
// tiers, 0x14000f160 goods categories, 0x140001bb0 / 0x1400021d0 the merchant's inner
// arrays) on the fresh description's EMPTY array: size 0 -> n with capacity < n stores
// `capacity = n` (the `* 3` arm needs a non-empty array grown by one), MemAlloc(capacity *
// stride, 8), and zero-constructs the n new elements (the copy / destroy arms need old
// elements: a description is loaded once, into an empty array). The size itself is the
// count the caller just serialized into the field. B6 row 26: the capacity store is
// retail's; the dtor walks the capacity, so before it the loaded elements were never released.
template <class T>
T* TD_AllocArray(T*& base, int32_t& capacity, int n) {
    if (capacity < n) capacity = n;   // 0x14000f370: `MOV [RCX+0xc],EDX`
    T* buf = static_cast<T*>(System::MemoryAllocator::MemAlloc(
        static_cast<uint64_t>(capacity) * sizeof(T), 8));
    base = buf;
    if (buf) std::memset(static_cast<void*>(buf), 0, static_cast<size_t>(n) * sizeof(T));   // the new elements' zero stores
    return buf;
}
}  // namespace

// ctor (inlined into create 0x14000dbd0 / createInPlace 0x14000dc10):
// `??0External@Object@@` (Description()), the stamps (the compiler's), then
// the defaults.
TradeDescription::TradeDescription()
    : Description(),
      _resourceSpriteSheet(nullptr),
      _resourceStringTable(nullptr),
      _merchantTemplate(nullptr),
      _pairs(nullptr),
      _pairCount(0),
      _pairCap(0),
      _merchants(nullptr),
      _merchantCount(0),
      _merchantCap(0),
      _prices(nullptr),
      _priceCount(0),
      _priceCap(0),
      _sicknessAmount(FloatBits(0x3eaa7efa)),
      _sicknessTimeInMonths(3) {}

// dtor (slot 0 0x14000dc50 minus the stamps): frees the merchant (and its inner arrays/Strings), string-pair and price arrays.
// The compiler then chains ~Description -> ~External.
TradeDescription::~TradeDescription() {
    // merchant array (free its inner arrays + strings first; walks the CAPACITY
    // field, which the load path sets since B6 row 26)
    int merchN = _merchantCap;
    if (TradeMerchant* m = _merchants) {
        for (int i = 0; i < merchN; ++i, ++m) {
            if (m->goods) System::MemoryAllocator::MemFree(m->goods);
            if (m->naturalResources) System::MemoryAllocator::MemFree(m->naturalResources);
            m->name.~String();
        }
        System::MemoryAllocator::MemFree(_merchants);
    }
    // goods-category array (free strings first)
    int spN = _pairCap;
    if (TradeGoodsCategory* sp = _pairs) {
        for (int i = 0; i < spN; ++i, ++sp) {
            sp->spriteName.~String();
            sp->textName.~String();
        }
        System::MemoryAllocator::MemFree(_pairs);
    }
    if (_prices) System::MemoryAllocator::MemFree(_prices);
    _merchants = nullptr;
    _pairs = nullptr;
    _prices = nullptr;
}

// [5] 0x14005c7f0: stream order as retail.
bool TradeDescription::Serialize(IO::Stream& stream) {
    IO::Serializer ser(stream, 1);
    ::Object::SerializeExternal<ComponentDescription>(ser, _merchantTemplate);   // ExternalPtr +0x30

    // merchant array @ +0x48 (count @ +0x50)
    ser.Serialize(_merchantCount, nullptr);
    int merchN = _merchantCount;
    if (static_cast<int>(ser.GetDirection()) == 0 && merchN > 0) TD_AllocArray(_merchants, _merchantCap, merchN);
    if (merchN > 0) {
        TradeMerchant* m = _merchants;
        for (int i = 0; i < merchN && m; ++i, ++m) {
            ::Object::SerializeExternal<UI::StringTable>(ser, m->stringTable);   // ExternalPtr
            m->name.Serialize(ser);
            // goods: ExternalPtr array (count @ +0x28)
            ser.Serialize(m->goodsCount, nullptr);
            int extN = m->goodsCount;
            if (static_cast<int>(ser.GetDirection()) == 0 && extN > 0) TD_AllocArray(m->goods, m->goodsCap, extN);
            // the merchant element 0x14000f550, 0x14000f5d2 CALL 0x1400049c0: the
            // ComponentDescription instantiation (cell 0x1401c7318).
            Object::SerializeExternalArray<ComponentDescription>(ser, extN, m->goods);
            // the enum array @ +0x30 (count @ +0x38) through the uint door
            ser.Serialize(m->naturalResourceCount, nullptr);
            int intN = m->naturalResourceCount;
            if (static_cast<int>(ser.GetDirection()) == 0 && intN > 0) TD_AllocArray(m->naturalResources, m->naturalResourceCap, intN);
            uint32_t* ia = m->naturalResources;
            for (int j = 0; j < intN && ia; ++j, ++ia)
                ser.Serialize(*ia, nullptr);
            ser.Serialize(m->stockRolls, nullptr);       // int @+0x40
            ser.Serialize(m->sellFlags, nullptr);       // enum @+0x10
            ser.Serialize(m->fullPriceFlags, nullptr);   // enum @+0x14
            ser.Serialize(m->discountFlags, nullptr);    // enum @+0x18
        }
    }

    // tier array @ +0x58 (count @ +0x60)
    ser.Serialize(_priceCount, nullptr);
    int priceN = _priceCount;
    if (static_cast<int>(ser.GetDirection()) == 0 && priceN > 0) TD_AllocArray(_prices, _priceCap, priceN);
    if (priceN > 0) {
        TradeTier* p = _prices;
        for (int i = 0; i < priceN && p; ++i, ++p) {
            ser.Serialize(p->populationLimit, nullptr);
            ser.Serialize(p->delayBase, nullptr);
            ser.Serialize(p->delayTolerance, nullptr);
            ser.Serialize(p->rollDivisor, nullptr);
            ser.Serialize(p->stockDivisor, nullptr);
        }
    }

    ::Object::SerializeExternal<UI::SpriteSheet>(ser, _resourceSpriteSheet);   // ExternalPtr +0x20
    ::Object::SerializeExternal<UI::StringTable>(ser, _resourceStringTable);   // ExternalPtr +0x28

    // goods-category array @ +0x38 (count @ +0x40)
    ser.Serialize(_pairCount, nullptr);
    int spN = _pairCount;
    if (static_cast<int>(ser.GetDirection()) == 0 && spN > 0) TD_AllocArray(_pairs, _pairCap, spN);
    if (spN > 0) {
        TradeGoodsCategory* sp = _pairs;
        for (int i = 0; i < spN && sp; ++i, ++sp) {
            ser.Serialize(sp->rawMaterialFlags, nullptr);   // enum @+0x00
            sp->spriteName.Serialize(ser);                 // String @+0x08
            sp->textName.Serialize(ser);                   // String @+0x10
        }
    }
    ser.Serialize(_sicknessAmount, nullptr);
    ser.Serialize(_sicknessTimeInMonths, nullptr);
    return true;
}

// [6] 0x14000dc20: retail destroys and rebuilds in place: a virtual `this->~T()` (retail: slot 0, flags 0),
// then the ctor again on `this` (D9 #10).
Object::External* TradeDescription::Reconstruct() {
    this->~TradeDescription();
    return new (static_cast<void*>(this)) TradeDescription();
}

// [7] 0x140001640: retail's shared body, a virtual `this->~T()` (retail: slot 0, flags 0) (D9 #10).
void TradeDescription::Destruct() { this->~TradeDescription(); }

// [8] 0x14000dc40: TradeComponent's type id through the hidden return slot.
Object::Type* TradeDescription::GetComponentType(Object::Type* ret) const {
    ret->_type = TradeComponent::TypeId();
    return ret;
}

// [9] 0x14000dc10: placement-new on `this` (folded with createInPlace). The old
// table held the same construction in this slot.
Object::External* TradeDescription::Construct() {
    return new (static_cast<void*>(this)) TradeDescription();
}

// create 0x14000dbd0: MemAlloc(0x70, 8), then the (inlined) ctor.
::Object::Base* TradeDescription::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(TradeDescription), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) TradeDescription());
}
// createInPlace 0x14000dc10.
::Object::Base* TradeDescription::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) TradeDescription());
}

DEFINE_EXE_TYPE(TradeDescription, "Description")
