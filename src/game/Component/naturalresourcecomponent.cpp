// src/game/Component/naturalresourcecomponent.cpp
// Clean reimplementation of NaturalResourceComponent lifecycle methods.
//
// NaturalResourceComponent (0x30 bytes) extends Component with:
//   +0x28: float  resourceScale  (initialized to 1.0f)
//   +0x2C: bool   mature, +0x2D bool harvestable (initialized by one word store 0x100)
//
// Vtable at 0x140163e10. Two non-trivial overrides:
//   Persist (0x140125cf0) — Serialize resourceScale + 2 bool fields
//   Tick    (0x140125c40) — Waits for the removal animation, then removes the resource
//

// graphics/Mesh.h comes FIRST: it pulls src/engine/volume/Volume.h, whose
// global-scope `struct Rectangle` is hidden by <wingdi.h>'s `Rectangle()`
// function once <windows.h> has been seen, which turns
// Volume.h's own `sizeof(Rectangle)` assert into a hard error.
#include "graphics/Mesh.h"   // ENDGAME B.3.5 caller batch I2
#include "Component/NaturalResourceComponent.h"
#include "Component/ModelComponent.h"   // OnRemove: FindComponent<ModelComponent>, meshInstance
#include "object/TypeInfo.h"
#include "graphics/Camera.h"
#include "Action/exe_bodies.h"
#include "Entity/ComponentEntity.h"
#include "io/Serializer.h"   // ENDGAME B.3.4 caller batch A
#include <cstdint>
#include <cstdio>
#include <new>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "Component/exe_bodies.h"

// ============================================================
// NaturalResourceComponent::Tick (vtable slot [8], 0x140125c40)
// ============================================================
// The per-tick body a resource runs while its removal animation plays (the tick bit 0x4 is
// set by NaturalResourceSetAnimation 0x140126440 when the dead bit goes on): once the
// ModelComponent's MeshInstance stops animating it clears the tick bit, refreshes the
// entity's flag summary, and -- if the dead bit 0x200000 is set -- removes the resource
// (0x1401260f0). The profile build's name for this body is OnRemove (its error string
// "natrual resource is alread being removed!"); the slot it sits in is Component's [8]
// Tick(const Time*) (vtable 0x140163e10: [7] = Component's 0x140118830, [8] = 0x140125c40).
// The time argument is unread (retail: no read of RDX).
//
// INSTALLED 2026-10-09 (USER DECISION, B7 close -- known_port_issues row 5 / 552): until then
// slot 8 was an inert override and the body a free function nothing called, so a cut tree or a
// quarried rock never ran this and the entity was removed by the dead-bit path alone.
// The body is retail's: 0x140125c60 `CALL 0x140003f60` = FindComponent<ModelComponent>
// (cell 0x1401c7a50), then 0x140125c75 `ADD RCX,0x28; CALL [0x1401514b0]` = the
// ModelComponent's MeshInstance::IsAnimating (row 552 fixed in B6: the tree called the
// ParticleComponent finder 0x140005200). B3R: its three doors (0x140005200, 0x1400c3470,
// 0x1401260f0, filled only by an init function that nothing called) are the bodies by name.
void NaturalResourceComponent::Tick(const System::Time*) {
    ModelComponent* model = _entity->FindComponent<ModelComponent>();   // 0x140003f60
    bool isAnimating = model->meshInstance.IsAnimating();
    if (isAnimating) return;

    // Clear bit 2 of the flags
    uint32_t oldFlags = _flags;
    uint32_t newFlags = oldFlags & ~0x4u;
    _flags = newFlags;

    // If flags changed in low 16 bits and entity exists, update
    if (((newFlags ^ oldFlags) & 0xFFFF) != 0 && _entity != nullptr) {
        _entity->RefreshEntityFlags();   // 0x1400c3470
    }

    // If bit 21 of flags is set, remove the resource
    if ((_flags >> 0x15) & 1) {
        ComponentEntity* nullRef = nullptr;   // InternalPtr (the ctor is a plain store)
        RemoveResource(&nullRef, 0);   // 0x1401260f0 (was its ActCNR_RemoveNatRes_1260f0 forwarder)
    }
}

// ===== ENDGAME B.3.8 CIT — NaturalResourceComponent as a REAL class =====
// (NaturalResourceComponent.h). Before: a hand-expanded COMPONENT_INERT_FACTORY
// (48-slot NaturalResourceComponent_InertVtable, slot 3 the serializer,
// Inert_ScalarDtor in slot 0) plus the ctor defaults patched in after the
// zero-fill. After: the compiler's 16-slot vtable and a real ctor/dtor chain
// (B38_RULES D1, D2); the ctor defaults are the ctor's own.
#include "Object/factory_registration.h"
#include "Component/NaturalResourceComponent.h"

// 0x140125dc0 (heap twin inlined in create 0x140036550). Component() is the
// CALL 0x1401188b0; the 0x140163e10 stamp is the compiler's; then
//   140125de6: MOV dword ptr [RBX+0x28], 0x3f800000   ; resourceScale = 1.0f
//   140125ded: MOV word  ptr [RBX+0x2c], 0x100        ; +0x2c = 0, +0x2d = 1
// That second store is a PACKED 16-bit write covering BOTH bools: +0x2c (mature)
// = 0, +0x2d (harvestable) = 1. Missing the +0x2d half is not cosmetic:
// harvestable = 0 makes FUN_1401260f0 early-out, so a chopped tree/rock/ore
// yields no resource pile. No flag bits are added (flags stay 0x80).
NaturalResourceComponent::NaturalResourceComponent()
    : resourceScale(1.0f), mature(false), harvestable(true), _pad2E{} {}

// ============================================================
// NaturalResourceComponent::Persist (vtable slot [3], 0x140125cf0)
// ============================================================

void NaturalResourceComponent::Persist(IO::Stream& stream) {
    Component::Persist(stream);   // retail: CALL 0x1401189b0 (qualified)

    IO::Serializer ser(stream, 0);

    ser.Serialize(resourceScale, nullptr);
    ser.Serialize(mature, nullptr);
    ser.Serialize(harvestable, nullptr);
}

// create 0x140036550: MemAlloc(0x30, 8), then the (inlined) ctor.
::Object::Base* NaturalResourceComponent::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(NaturalResourceComponent), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) NaturalResourceComponent());
}
// Retail's createInPlace is `Internal::CreateInPlaceObject` (returns null); the
// tree's in-place entry built the object, and that is kept (B38 B8-13).
::Object::Base* NaturalResourceComponent::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) NaturalResourceComponent());
}

DEFINE_EXE_TYPE(NaturalResourceComponent, "Component")
