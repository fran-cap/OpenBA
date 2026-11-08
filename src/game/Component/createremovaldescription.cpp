// src/game/Component/createremovaldescription.cpp
// Clean reimplementation of CreateRemovalDescription (entity description block).
//
// CreateRemovalDescription (0x48 bytes) — removal/demolish placement tool.
// Extends Object::External (0x20 base). Vtable at 0x140162d80. Parent "Description".
//   +0x20: bool
//   +0x28: void* heap ExternalPtr array base (8-byte elements)
//   +0x30: int   count
//   +0x38: ExternalPtr (single)
//   +0x40..+0x43: 4 bools
//
// Factory  : MemAlloc 0x48 + External ctor + vtable + field defaults (zero).
// Serialize (vtable +0x28, 0x140119ac0 CreateRemovalDescription_Slot5), stream order:
//   bool  @ +0x20
//   int count (local copy of +0x30)
//   [if loading: resize array @+0x28 to count via FUN_140001bb0 = Array<ptr>::SetSize (kSpec_PtrZero)]
//   ExternalRefArray(count) @ *(+0x28)  (FUN_1400049c0 / fn_ExternalRefArray_Serialize)
//   ExternalPtr @ +0x38  (FUN_140002340 / Reimpl_ExternalPtr_Serialize_StringTable)
//   bool  @ +0x40   bool @ +0x41   bool @ +0x42   bool @ +0x43
// GetVersion (vtable +0x18, 0x140119bc0): mov eax,1; ret -> version 1.
// Dtor (vtable +0x00, 0x140032ca0): MemFree(array @+0x28) + External::~External + free.
//
// NOTE: count is serialized into a local; the on-disk count then drives array resize +
// the per-element load. The resize is `_removeTypes.SetSize` (kSpec_PtrZero).
// fn_ExternalRefArray_Serialize -> exe FUN_1400049c0, null in standalone until 0x1400049c0
// is registered (REPORTED). param4 is a dead init -> nullptr safe.
//
// Field knowledge harvested from engine/reimpl/createremovaldescription.cpp.
// Decompiled from Application-x64.exe (READ-ONLY recon).
//
// ENDGAME B.3.8 K1: a REAL class (CreateRemovalDescription.h) with a compiler-generated vtable;
// the hand-built 11-slot table and its RegisterExeAddr(0x140162d80) row are gone
// (nothing in the tree read that address back — B38_RULES D5).

#include "Component/CreateRemovalDescription.h"
#include "object/Internal.h"      // Object::detail::InternalMemAlloc / InternalMemFree
#include "io/Serializer.h"
#include "Object/factory_registration.h"
#include "object/External.h"
#include "object/TypeInfo.h"
#include <cstdint>
#include <new>
#include "Game/exe_bodies.h"
#include "system/MemoryAllocator.h"
#include "Component/ComponentDescription.h"   // B8 RT2: ::Object::SerializeExternal<ComponentDescription> (the downcast needs it complete)

// ctor (inlined into create 0x140032b70 / createInPlace 0x140032be0):
// `??0External@Object@@` (Description()), the stamps (the compiler's), then
// the defaults (+0x28 = +0x30 = 0 is _removeTypes' Array ctor, 0x140032bbe / 0x140032bc2).
CreateRemovalDescription::CreateRemovalDescription()
    : Description(),
      _removeResources(false),
      _remover(nullptr),
      _removeRoads(false),
      _removeBuildings(false),
      _cancelClear(false),
      _increasePriority(false) {}

// dtor (slot 0 0x140032ca0 minus the stamps): the body is empty. ~_removeTypes frees the
// ExternalPtr array unconditionally and zeroes +0x28 / +0x30 (0x140032cc7..0x140032cd7; the
// entries are non-owning, no element release); the compiler then chains ~Description -> ~External.
CreateRemovalDescription::~CreateRemovalDescription() {
}

// [5] 0x140119ac0: stream order as retail.
bool CreateRemovalDescription::Serialize(IO::Stream& stream) {
    IO::Serializer ser(stream, 0);
    ser.Serialize(_removeResources, nullptr);
    int count = _removeTypes._size;
    ser.Serialize(count, nullptr);   // a local copy, as retail
    // On load (stream mode == 0): resize the heap ExternalPtr array at +0x28.
    if (ser._stream && static_cast<int>(ser.GetDirection()) == 0) {
        // retail 0x140001bb0 IS Collection::Array<T>::SetSize(n): stride 8 align 8, new tail zeroed.
        _removeTypes.SetSize(count);
    }
    ::Object::SerializeExternalArray<ComponentDescription>(ser, count, _removeTypes._data);   // ExtPtr[count] (0x1400049c0)
    ::Object::SerializeExternal<ComponentDescription>(ser, _remover);
    ser.Serialize(_removeRoads, nullptr);
    ser.Serialize(_removeBuildings, nullptr);
    ser.Serialize(_cancelClear, nullptr);
    ser.Serialize(_increasePriority, nullptr);
    return true;
}

// [6] 0x140032c40: retail destroys and rebuilds in place: a virtual `this->~T()` (retail: slot 0, flags 0),
// then the ctor again on `this` (D9 #10).
Object::External* CreateRemovalDescription::Reconstruct() {
    this->~CreateRemovalDescription();
    return new (static_cast<void*>(this)) CreateRemovalDescription();
}

// [7] 0x140001640: retail's shared body, a virtual `this->~T()` (retail: slot 0, flags 0) (D9 #10).
void CreateRemovalDescription::Destruct() { this->~CreateRemovalDescription(); }

// [8] 0x140004950 (shared by 19 tables): `MOV dword [RDX],0; MOV RAX,RDX; RET` —
// a data-only description builds no component.
Object::Type* CreateRemovalDescription::GetComponentType(Object::Type* ret) const {
    ret->_type = 0;
    return ret;
}

// [9] 0x140032be0: placement-new on `this` (folded with createInPlace). The old
// table held the same construction in this slot.
Object::External* CreateRemovalDescription::Construct() {
    return new (static_cast<void*>(this)) CreateRemovalDescription();
}

// create 0x140032b70: MemAlloc(0x48, 8), then the (inlined) ctor.
::Object::Base* CreateRemovalDescription::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(CreateRemovalDescription), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) CreateRemovalDescription());
}
// createInPlace 0x140032be0.
::Object::Base* CreateRemovalDescription::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) CreateRemovalDescription());
}

DEFINE_EXE_TYPE(CreateRemovalDescription, "Description")
