// src/game/Component/herbalistdescription.cpp
// HerbalistDescription (entity description block) — herbalist gathering parameters.
// Extends Object::External (0x20 base). Vtable 0x140159910. Parent "Description".
// Size 0x38. GetVersion=2 (slot +0x18 @ 0x14004b380: mov eax,2; ret).
//
// Ctor defaults (HerbalistDescription_Factory):
//   External*-array @ +0x20 = {ptr=0, count=0 @ +0x28}  (herb material refs)
//   int/enum        @ +0x30 = 6
// Serialize (vtable +0x28, 0x14004b2e0), stream order:
//   int count @ +0x28 -> resize +0x20 External*-array -> per-element FindOrLoad
//   (FUN_1400049c0 == Desc_LoadExternalArray)
//   enum @ +0x30 (encoding vcall +0x98)
// Decompiled READ-ONLY from Application-x64.exe.
//
// ENDGAME B.3.8 K1: a REAL class (HerbalistDescription.h) with a compiler-generated vtable;
// the hand-built 11-slot table and its RegisterExeAddr(0x140159910) row are gone
// (nothing in the tree read that address back — B38_RULES D5).

#include "Component/HerbalistDescription.h"
#include "Component/HerbalistComponent.h"   // B.3.8 CB1a (TypeId)
#include "object/Internal.h"      // Object::detail::InternalMemAlloc / InternalMemFree
#include "io/Serializer.h"
#include "Object/factory_registration.h"
#include "object/External.h"
#include "Component/ComponentDescription.h"   // the SerializeExternalArray<ComponentDescription> instantiation
#include "object/TypeInfo.h"
#include "system/MemoryAllocator.h"   // System::MemoryAllocator::MemAlloc / MemFree
#include <cstdint>
#include <new>

namespace {
// retail's `*(int*)(*(serializer) + 8) == 0` is the stream's direction word —
// IO::Serializer::GetDirection (0x180006210) — 0 = reading. The tree's check before
// it allocates an array.
inline bool Reading(IO::Serializer& ser) {
    return static_cast<int>(ser.GetDirection()) == 0;
}
}  // namespace


// ctor (inlined into create 0x140006bb0 / createInPlace 0x140006c20):
// `??0External@Object@@` (Description()), the stamps (the compiler's), then
// the defaults.
HerbalistDescription::HerbalistDescription()
    : Description(),
      _herbs(nullptr),
      _herbCount(0),
      _herbCap(0),
      _resourceLimit(6) {}

// dtor (slot 0 0x140006cf0 minus the stamps): frees the External* array buffer (entries are non-owning).
// The compiler then chains ~Description -> ~External.
HerbalistDescription::~HerbalistDescription() {
    if (_herbs) System::MemoryAllocator::MemFree(_herbs);
    _herbs = nullptr;
}

// [5] 0x14004b2e0: stream order as retail.
bool HerbalistDescription::Serialize(IO::Stream& stream) {
    IO::Serializer ser(stream, 0);
    // External*-array @ +0x20 (count @ +0x28, stride 8)
    ser.Serialize(_herbCount, nullptr);
    int n = _herbCount;
    if (Reading(ser) && n > 0 && System::MemoryAllocator::_singleton) {
        _herbs = static_cast<ComponentDescription**>(System::MemoryAllocator::MemAlloc((uint64_t)n * 8, 8));
        _herbCap = n;
        if (_herbs) for (int i = 0; i < n; ++i) _herbs[i] = nullptr;
    }
    // 0x14004b345 CALL 0x1400049c0: the ComponentDescription instantiation (cell 0x1401c7318).
    Object::SerializeExternalArray<ComponentDescription>(ser, n, _herbs);
    ser.Serialize(_resourceLimit, nullptr);
    return true;
}

// [6] 0x140006c80: retail destroys and rebuilds in place: a virtual `this->~T()` (retail: slot 0, flags 0),
// then the ctor again on `this` (D9 #10).
Object::External* HerbalistDescription::Reconstruct() {
    this->~HerbalistDescription();
    return new (static_cast<void*>(this)) HerbalistDescription();
}

// [7] 0x140001640: retail's shared body, a virtual `this->~T()` (retail: slot 0, flags 0) (D9 #10).
void HerbalistDescription::Destruct() { this->~HerbalistDescription(); }

// [8] 0x140006ce0: HerbalistComponent's type id through the hidden return slot.
Object::Type* HerbalistDescription::GetComponentType(Object::Type* ret) const {
    ret->_type = HerbalistComponent::TypeId();
    return ret;
}

// [9] 0x140006c20: placement-new on `this` (folded with createInPlace). The old
// table held the same construction in this slot.
Object::External* HerbalistDescription::Construct() {
    return new (static_cast<void*>(this)) HerbalistDescription();
}

// create 0x140006bb0: MemAlloc(0x38, 8), then the (inlined) ctor.
::Object::Base* HerbalistDescription::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(HerbalistDescription), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) HerbalistDescription());
}
// createInPlace 0x140006c20.
::Object::Base* HerbalistDescription::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) HerbalistDescription());
}

DEFINE_EXE_TYPE(HerbalistDescription, "Description")
