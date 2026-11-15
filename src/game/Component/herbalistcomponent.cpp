// src/game/Component/herbalistcomponent.cpp
// Clean reimplementation of HerbalistComponent::Shutdown from Application-x64.exe.
//
// HerbalistComponent (0x50 bytes) is a ResourceProduceComponent subclass that
// manages herbalist buildings AND the gatherer's hut (GathererShelter.rsc carries
// HerbalistDescription — there is no separate GathererComponent). No additional
// fields beyond ResourceProduceComponent.
//
// Vtable at 0x140159860. Only non-trivial override:
//   Shutdown (0x14004b200) -- calls parent Shutdown + empty serializer block.
//
// Ghidra decompilation:
//   ResourceProduceComponent_Controller_Slot13(param_1, param_2)
//   Serializer(local, param_2, 0); ~Serializer(local)
//
// Decompiled from Application-x64.exe using Ghidra.

#include "Component/HerbalistComponent.h"
#include <cstdint>
#include "object/Internal.h"
#include "object/TypeInfo.h"
#include "io/Serializer.h"   // ENDGAME B.3.4 caller batch A
#include <cstdio>
#include <new>

// ============================================================
// HerbalistComponent::Shutdown (vtable slot [3], 0x14004b200)
// ============================================================
// Calls parent ResourceProduceComponent::Shutdown, then creates
// an empty serializer block (no additional data to serialize).

// ============================================================================
// ENDGAME B.3.8 MI-B — HerbalistComponent as a REAL class (HerbalistComponent.h).
//
// Before: CONTROLLER_LIVE_FACTORY_STATES (48-slot table filled per create, slots
// 3/13/14/18 written from InstallLive, Inert_ScalarDtor in slot 0, the shared RP
// dispatcher stamped at +0x28). After: the compiler's 22-slot vtable (13/14/16/17
// and 19-21 inherited from ResourceProduceComponent, as retail's table holds), a
// real ctor/dtor chain, the controller the base's member (B38_RULES D1, D2, D6).
// ============================================================================
#include "Object/factory_registration.h"


// 0x14004b2a0: the RP ctor (CALL 0x14012b3d0) and the compiler's stamp. Nothing else.
HerbalistComponent::HerbalistComponent() : ResourceProduceComponent() {}

// create 0x140006b40: MemAlloc(0x50, 8), then the ctor.
::Object::Base* HerbalistComponent::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(HerbalistComponent), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) HerbalistComponent());
}
// Retail's createInPlace is `Internal::CreateInPlaceObject` (returns null); the
// tree's in-place entry built the object, and that is kept (B38 B8-13).
::Object::Base* HerbalistComponent::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) HerbalistComponent());
}

void       HerbalistComponent::Persist(IO::Stream& stream) {
    // Base ResourceProduceComponent::Persist (0x14012b4a0), a direct call in retail.
    ResourceProduceComponent::Persist(stream);

    // Empty serializer block (subclass compat)
    { IO::Serializer scope(stream, 0); }
}
// [18] HerbalistComponent::CreateWork is defined in resourceproduce_creatework.cpp.

DEFINE_EXE_TYPE(HerbalistComponent, "ResourceProduceComponent")
