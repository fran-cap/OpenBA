// src/game/Component/fishingcomponent.cpp
// Clean reimplementation of FishingComponent lifecycle methods from Application-x64.exe.
//
// FishingComponent (0x58 bytes) extends ResourceProduceComponent with:
//   +0x50: uint8  fishingFlag  (serialized as bool)
//   +0x54: float  fishingRate  (serialized)
//
// Vtable at 0x140163230. Overrides slot [3] Persist (0x14011f050) — ResourceProduceComponent::Persist
//   + own serializer — and slots [18]-[21] (listed at the end of this file).
//
// Decompiled from Application-x64.exe using Ghidra.

#include "Component/FishingComponent.h"
#include <cstdint>
#include "object/Internal.h"
#include "object/TypeInfo.h"
#include "io/Serializer.h"   // ENDGAME B.3.4 caller batch A
#include <cstdio>
#include <new>

// ============================================================
// FishingComponent::Shutdown (vtable slot [3], 0x14011f050)
// ============================================================
// ResourceProduceComponent::Shutdown(self, stream)
// Serializer s(stream, 0)
// s.Serialize(&self[0x50], null)  // bool: fishingFlag
// s.Serialize(&self[0x54], null)  // float: fishingRate
// ~Serializer()

// ============================================================================
// ENDGAME B.3.8 MI-B — FishingComponent as a REAL class (FishingComponent.h).
//
// Before: CONTROLLER_LIVE_FACTORY_STATES — a 48-slot hand-built table filled per
// create (slots 3/13/14/18-21 written from InstallLive), Inert_ScalarDtor in slot 0
// and the shared RP dispatcher stamped into raw bytes at +0x28. After: the
// compiler's 22-slot vtable (13/14/16/17 inherited from ResourceProduceComponent,
// which is what retail's table holds), a real ctor/dtor chain, the controller the
// base's member (B38_RULES D1, D2, D6).
// ============================================================================
#include "Object/factory_registration.h"

// 0x14011f130: the RP ctor (CALL 0x14012b3d0), the compiler's stamp, then
// `MOV byte [RBX+0x50],0`. Retail leaves +0x54 unwritten; the tree's factory
// zeroed it and that is kept (no reader precedes the first ProduceTick/Persist).
FishingComponent::FishingComponent()
    : ResourceProduceComponent(), fishingFlag(false), _pad51{}, fishingRate(0.0f) {}

// create 0x140034350: MemAlloc(0x58, 8), then the ctor.
::Object::Base* FishingComponent::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(FishingComponent), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) FishingComponent());
}
// Retail's createInPlace is `Internal::CreateInPlaceObject` (returns null); the
// tree's in-place entry built the object, and that is kept (B38 B8-13).
::Object::Base* FishingComponent::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) FishingComponent());
}

void       FishingComponent::Persist(IO::Stream& stream) {
    // Base ResourceProduceComponent::Shutdown (0x14012b4a0), a direct call in retail.
    ResourceProduceComponent::Persist(stream);

    // Serialize FishingComponent's own fields
    IO::Serializer ser(stream, 0);
    ser.Serialize(fishingFlag, nullptr);
    ser.Serialize(fishingRate, nullptr);
}
// [18] 0x14011efe0 CreateWork is defined in resourceproduce_creatework.cpp.
// [19] 0x14011ee80 / [20] 0x14011ee50 / [21] 0x14011ec10: OnEnterProducing,
// OnExitProducing and ProduceTick live in fishingcomponent_produce.cpp with the
// coverage helpers they share.

DEFINE_EXE_TYPE(FishingComponent, "ResourceProduceComponent")
