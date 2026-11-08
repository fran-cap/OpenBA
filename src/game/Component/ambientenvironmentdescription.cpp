// src/game/Component/ambientenvironmentdescription.cpp
// Clean reimplementation of AmbientEnvironmentDescription (entity description block).
//
// AmbientEnvironmentDescription (0x38 bytes) — ambient-zone environment override.
// Extends Object::External (0x20 base). Vtable at 0x140158bf0. Parent "Description".
//   +0x20: void* dynamic entry-array base (0x28-byte / 5-qword elements)
//   +0x28: int   count   (+0x2c cap)
//   +0x30: float (default 0x42000000 = 32.0f)
//   +0x34: float (default 0x42340000 = 45.0f)
//   Each array element (0x28 bytes):
//     +0x00 enum   +0x08 ExternalPtr  +0x10 ExternalPtr  +0x18 ExternalPtr  +0x20 int
//
// Factory  : MemAlloc 0x38 + External ctor + vtable + defaults
//            (+0x20=0, +0x28=0, +0x30=32.0f, +0x34=45.0f).
// Serialize (vtable +0x28, 0x140043950), stream order:
//   int count @ +0x28
//   [if loading: FUN_140002540(+0x20, count)  resize 0x28-byte-element array]
//   for each element (stride 0x28):
//     enum        @ elem+0x00  (encoding vtable +0x98)
//     ExternalPtr @ elem+0x08  (FUN_140002340 / fn_ExternalPtr_BuildSerialize)
//     ExternalPtr @ elem+0x10  (FUN_1400019a0)
//     ExternalPtr @ elem+0x18  (FUN_1400019a0)
//     int         @ elem+0x20
//   float @ +0x30
//   float @ +0x34
// GetVersion (vtable +0x18, 0x140043a70): mov eax,2; ret -> version 2.
// Dtor (vtable +0x00, 0x140002100): External::~External + free.
//
// The exe helpers FUN_1400019a0 (single ambient ExternalPtr serialize) and
// FUN_140002540 (0x28-stride array resize) are called directly (B.3R).
//
// Decompiled from Application-x64.exe (READ-ONLY recon).
//
// ENDGAME B.3.8 K1: a REAL class (AmbientEnvironmentDescription.h) with a compiler-generated vtable;
// the hand-built 11-slot table and its RegisterExeAddr(0x140158bf0) row are gone
// (nothing in the tree read that address back — B38_RULES D5).

#include "Component/AmbientEnvironmentDescription.h"
#include "Component/AmbientEnvironmentComponent.h"   // B.3.8 CB1a (TypeId)
#include "object/Internal.h"      // Object::detail::InternalMemAlloc / InternalMemFree
#include "io/Serializer.h"
#include "Object/factory_registration.h"
#include "object/External.h"
#include "object/TypeInfo.h"
#include <cstdint>
#include <new>
#include "Component/exe_bodies.h"
#include "Game/exe_bodies.h"
#include "system/MemoryAllocator.h"
#include "collection/Collection.h"   // Collection::Array (the typed view of the raw triple)
#include "audio/SoundEffect.h"   // B8 RT2: ::Object::SerializeExternal<Audio::SoundEffect> (the downcast needs it complete)
#include "Component/ComponentDescription.h"   // B8 RT2: ::Object::SerializeExternal<ComponentDescription> (the downcast needs it complete)

// ============================================================
// Reimpl of FUN_140002540 (grow/shrink the 0x28-byte-element entry array)
// ============================================================
// Resizes the array {ptr@arr+0, count@arr+8, cap@arr+0xc} (stride 0x28). On grow
// past capacity: cap = newCount (or newCount*3 when growing by exactly one from a
// non-empty array), MemAlloc(cap*0x28, 8), copy existing elements, zero-init the new
// tail with elem+0x20 = 1 default, free the old block. 1:1 port of the exe helper.
// Retail 0x140002540 is Array<AmbientEnvironmentBiome>::SetSize (RCX = the array): B8 (RT)
// made it that member, an explicit specialization of Array<T>::SetSize.
// B6 row 570 dropped the one step retail lacked (MemAlloc skipped on a null
// System::MemoryAllocator::_singleton; 0x140002540 calls it at 0x140002596 with no test).
// The new tail writes +0x00
// (dword), +0x08 / +0x10 / +0x18 (qwords), +0x20 (dword); the two padding dwords stay untouched.
template <> void Collection::Array<AmbientEnvironmentBiome>::SetSize(int newCount) {   // 0x140002540
    int oldCount = _size;
    if (newCount == oldCount) return;

    AmbientEnvironmentBiome* oldPtr = _data;
    _size = newCount;
    if (_capacity < newCount) {
        _capacity = newCount;
        if (newCount == oldCount + 1 && oldCount != 0) {
            _capacity = newCount * 3;
        }
        _data = static_cast<AmbientEnvironmentBiome*>(
            ::System::MemoryAllocator::MemAlloc((uint64_t)_capacity * 0x28, 8));
    }

    int start = 0;
    if (oldPtr == _data) start = oldCount;  // grown in place: keep existing

    AmbientEnvironmentBiome* base = _data;
    for (int i = start; i < newCount; ++i) {
        AmbientEnvironmentBiome* dst = base + i;
        if (!dst) continue;
        if (i < oldCount) {
            *dst = oldPtr[i];                    // the 0x28 bytes, padding members included
        } else {
            dst->terrainMask  = 0;               // +0x00 dword
            dst->entityFilter = nullptr;         // +0x08
            dst->daySound     = nullptr;         // +0x10
            dst->nightSound   = nullptr;         // +0x18
            dst->weight       = 1;               // +0x20 = 1 default
        }
    }

    if (oldPtr != _data && oldPtr) {
        ::System::MemoryAllocator::MemFree(oldPtr);
    }
}

// ctor (inlined into create 0x140001f40 / createInPlace 0x140001fb0):
// `??0External@Object@@` (Description()), the stamps (the compiler's), then
// the defaults.
AmbientEnvironmentDescription::AmbientEnvironmentDescription()
    : Description(),
      _entries(),
      _minTemp(32.0f),
      _maxTemp(45.0f) {}

// dtor (slot 0 0x140002100 minus the stamps): the entry array's ~Array frees it
// (MemFree, then the data and count+capacity qwords zeroed), then the compiler chains
// ~Description -> ~External.
AmbientEnvironmentDescription::~AmbientEnvironmentDescription() {
}

// [5] 0x140043950: stream order as retail.
bool AmbientEnvironmentDescription::Serialize(IO::Stream& stream) {
    IO::Serializer ser(stream, 0);

    int count = _entries._size;
    ser.Serialize(count, nullptr);   // int count +0x28

    // On load: resize the 0x28-byte-element array at +0x20 (0x140002540).
    if (static_cast<int>(ser.GetDirection()) == 0) {
        _entries.SetSize(count);
    }

    if (count > 0) {
        AmbientEnvironmentBiome* elem = _entries._data;
        for (int i = 0; i < count; ++i, ++elem) {
            ser.Serialize(elem->terrainMask, nullptr);                          // enum
            ::Object::SerializeExternal<ComponentDescription>(ser, elem->entityFilter);  // ExternalPtr, 0x140002340
            ::Object::SerializeExternal<Audio::SoundEffect>(ser, elem->daySound);           // ExternalPtr, 0x1400019a0
            ::Object::SerializeExternal<Audio::SoundEffect>(ser, elem->nightSound);         // ExternalPtr, 0x1400019a0
            ser.Serialize(elem->weight, nullptr);                               // int
        }
    }

    // +0x30 / +0x34: retail's float overload (import 0x140152a60 at 0x140043a25
    // and 0x140043a37).
    ser.Serialize(_minTemp, nullptr);
    ser.Serialize(_maxTemp, nullptr);
    return true;
}

// [6] 0x140002010: retail destroys and rebuilds in place: a virtual `this->~T()` (retail: slot 0, flags 0),
// then the ctor again on `this` (D9 #10).
Object::External* AmbientEnvironmentDescription::Reconstruct() {
    this->~AmbientEnvironmentDescription();
    return new (static_cast<void*>(this)) AmbientEnvironmentDescription();
}

// [7] 0x140001640: retail's shared body, a virtual `this->~T()` (retail: slot 0, flags 0) (D9 #10).
void AmbientEnvironmentDescription::Destruct() { this->~AmbientEnvironmentDescription(); }

// [8] 0x140002070: AmbientEnvironmentComponent's type id through the hidden return slot.
Object::Type* AmbientEnvironmentDescription::GetComponentType(Object::Type* ret) const {
    ret->_type = AmbientEnvironmentComponent::TypeId();
    return ret;
}

// [9] 0x140001fb0: placement-new on `this` (folded with createInPlace). The old
// table held the same construction in this slot.
Object::External* AmbientEnvironmentDescription::Construct() {
    return new (static_cast<void*>(this)) AmbientEnvironmentDescription();
}

// create 0x140001f40: MemAlloc(0x38, 8), then the (inlined) ctor.
::Object::Base* AmbientEnvironmentDescription::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(AmbientEnvironmentDescription), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) AmbientEnvironmentDescription());
}
// createInPlace 0x140001fb0.
::Object::Base* AmbientEnvironmentDescription::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) AmbientEnvironmentDescription());
}

DEFINE_EXE_TYPE(AmbientEnvironmentDescription, "Description")
