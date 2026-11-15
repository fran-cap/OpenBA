// src/game/Component/minecomponent.cpp
// Clean reimplementation of MineComponent lifecycle methods from Application-x64.exe.
//
// MineComponent (0x28 bytes) — simplest possible component, no additional fields.
// Vtable at 0x140159ed0. Only Persist has real logic.
//
// Decompiled from Application-x64.exe using Ghidra.

#include "Component/MineComponent.h"
#include "io/Serializer.h"
#include "io/Stream.h"
#include <cstdint>
#include <new>

// ============================================================================
// ENDGAME B.3.8 VIS — MineComponent as a REAL class (MineComponent.h).
//
// Before: COMPONENT_INERT_FACTORY_SER — a 48-slot table filled per create,
// Inert_ScalarDtor in slot 0 (only ~Internal). After: the compiler's 16-slot
// vtable and a real ctor/dtor chain (B38_RULES D1, D2).
// ============================================================================
#include "Object/factory_registration.h"

// 0x14004f2e0: Component() is the CALL 0x1401188b0; the 0x140159ed0 stamp is
// the compiler's; no own store.
MineComponent::MineComponent() {}

// [3] 0x14004f260 — Component::Persist + one empty Serializer scope.
// Retail calls the base body directly (a qualified call).
void MineComponent::Persist(IO::Stream& stream) {
    Component::Persist(stream);
    IO::Serializer scope(stream, 0);
}
// [6] 0x14004f250 / [7] 0x14004f240 — the class's own `RET` bodies.
void MineComponent::OnAdded() {}
void MineComponent::OnRemove() {}

// create 0x140008dd0: MemAlloc(0x28, 8), then the ctor.
::Object::Base* MineComponent::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(MineComponent), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) MineComponent());
}
// Retail's createInPlace is `Internal::CreateInPlaceObject` (returns null); the
// tree's in-place entry built the object, and that is kept (B38 B8-13).
::Object::Base* MineComponent::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) MineComponent());
}

DEFINE_EXE_TYPE(MineComponent, "Component")
