// src/game/Component/quarrycomponent.cpp
// Clean reimplementation of QuarryComponent lifecycle methods from Application-x64.exe.
//
// QuarryComponent (0x28 bytes) — minimal component, same as MineComponent.
// Vtable at 0x14015a490. The real logic is OnEvent / OnMessage (quarrycomponent_deform.cpp).
//

#include "Component/QuarryComponent.h"
#include "io/Serializer.h"
#include "io/Stream.h"
#include <cstdint>
#include <new>

// ============================================================================
// ENDGAME B.3.8 VIS — QuarryComponent as a REAL class (QuarryComponent.h).
//
// Before: a 48-slot QuarryComponent_LiveVtable re-filled at every create
// (InitInertTyped + InstallLive), Inert_ScalarDtor in slot 0. After: the
// compiler's 16-slot vtable and a real ctor/dtor chain (B38_RULES D1, D2).
// The pit a quarry digs is a heightmap carve driven from slots 12 and 15 —
// bodies in quarrycomponent_deform.cpp.
// ============================================================================
#include "Object/factory_registration.h"

// 0x1400552b0: Component() is the CALL 0x1401188b0; the 0x14015a490 stamp is
// the compiler's; flags |= 3 (0x83 with Component's 0x80).
QuarryComponent::QuarryComponent() {
    OrFlagsInCtor(0x3);
}

// [3] 0x140055230 — Component::Persist (a direct call in retail) + one empty
// Serializer scope.
void QuarryComponent::Persist(IO::Stream& stream) {
    Component::Persist(stream);
    IO::Serializer scope(stream, 0);
}
// [6] 0x140055220 / [7] 0x140055210 — the class's own `RET` bodies.
void QuarryComponent::OnAdded() {}
void QuarryComponent::OnRemove() {}
// [12] 0x1400550f0 OnEvent / [15] 0x140054b80 OnMessage live in quarrycomponent_deform.cpp.

// create 0x14000b3d0: MemAlloc(0x28, 8), then the ctor.
::Object::Base* QuarryComponent::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(QuarryComponent), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) QuarryComponent());
}
// Retail's createInPlace is `Internal::CreateInPlaceObject` (returns null); the
// tree's in-place entry built the object, and that is kept (B38 B8-13).
::Object::Base* QuarryComponent::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) QuarryComponent());
}

DEFINE_EXE_TYPE(QuarryComponent, "Component")
