// src/game/Work/autopickupwork.cpp
// AutoPickupWork — the abstract Work subclass of the six gatherer works.
// ENDGAME B.3.8 (WRK): a REAL class (Work/AutoPickupWork.h) with a compiler-generated
// vtable. The retail evidence for every slot is in the header. Before: a hand-built
// 13-slot `g_AutoPickupWork_Vtable` (slots 1/2 answered null / a lazily cached id),
// the free functions AutoPickupWork_{Ctor,ScalarDtor,Serialize,Execute,OnRemove} and a
// file-level lazy resolve of five pointers.
//
// Inherits: Object::Internal -> Work (0x48) -> AutoPickupWork (adds no bytes).
// Concrete derived types: ForesterWork, HarvestArea, HarvestOrchard, HerbalistWork,
// HuntWork, PastureWork.
//
// Decompiled from Application-x64.exe.

#include "Work/AutoPickupWork.h"
#include "Object/factory_registration.h"   // Object::GetTypeIdByName
#include "Object/exe_type.h"               // DEFINE_EXE_TYPE
#include "object/Internal.h"               // Object::detail::InternalMemFree
#include "system/MemoryAllocator.h"
#include "Work/exe_bodies.h"
#include "Entity/ComponentEntity.h"            // FindComponent<T>
#include "Component/AutoPickupComponent.h"
#include "io/Serializer.h"   // ENDGAME B.3.4 caller batch C
#include <cstdint>

// [1] TypeName / [2] Type, the record and retail's abstract factory (create =
// the logging stub, in-place = null): ENDGAME B.3.9 RF.
DEFINE_EXE_ABSTRACT_TYPE(AutoPickupWork, "Work")

// [3] 0x1400e3f80: Work::Persist for the base fields (a direct call in retail — a
// QUALIFIED call here, or it would recurse through slot 3), then one EMPTY
// Serializer(stream, 0) scope: AutoPickupWork has no fields of its own, but the
// empty scope still occupies bytes in the save encoding.
void AutoPickupWork::Persist(IO::Stream& stream) {
    Work::Persist(stream);
    IO::Serializer scope(stream, 0);
}

// [8] 0x1400e3f30: find the AutoPickupComponent on the owning entity (+0x10) through
// FUN_140022520 (FindComponent<AutoPickupComponent>), then free its item list (+0x30)
// and zero the list pair (+0x30/+0x38). Retail's two CheckPtr calls are bare RETs.
int AutoPickupWork::Cleanup() {
    AutoPickupComponent* component = field_10->FindComponent<AutoPickupComponent>();
    System::MemoryAllocator::MemFree(component->field30._data);

    component->field30._data = nullptr;
    component->field30._size = 0;      // H7: retail zeroes +0x38 with one qword store
    component->field30._capacity = 0;
    return 0;
}


// No RegisterExeAddr(0x140160fa0): its only by-address readers were the leaves' dtors
// re-stamping it on the way down the chain, and every AutoPickupWork leaf is a real
// class now (the compiler's chain does that stamp).
