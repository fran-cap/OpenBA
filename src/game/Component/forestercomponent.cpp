// src/game/Component/forestercomponent.cpp
// Clean reimplementation of ForesterComponent lifecycle methods from Application-x64.exe.
//
// ForesterComponent (0x58 bytes) extends ResourceProduceComponent with:
//   +0x50: bool plantTrees, +0x51: bool cutTrees (each serialized as bool)
//
// Vtable at 0x140163460. Overrides: Persist [3] (0x14011f960) — ResourceProduceComponent::Persist
//   + own serializer — CommandMask/Command [13]/[14] and CreateWork [18].
//
// Decompiled from Application-x64.exe using Ghidra.

#include "Component/ForesterComponent.h"
#include <cstdint>
#include "object/Internal.h"
#include "object/TypeInfo.h"
#include "io/Serializer.h"   // ENDGAME B.3.4 caller batch A
#include <cstdio>
#include <new>

// ============================================================
// ForesterComponent::Shutdown (vtable slot [3], 0x14011f960)
// ============================================================
// ResourceProduceComponent::Shutdown(self, stream)
// Serializer s(stream, 0)
// s.Serialize(&self[0x50], null)  // bool at +0x50 (plantTrees)
// s.Serialize(&self[0x51], null)  // bool at +0x51 (cutTrees)
// ~Serializer()

// ============================================================
// Vtable slots 13/14 — the panel COMMAND pair
// ============================================================
// Unlike ConsumeProduce/Market (which inherit the ResourceProduce base pair verbatim),
// ForesterComponent OVERRIDES both. Byte-verified from vtable_ForesterComponent
// (0x140163460): slot 13 = 0x14011f920, slot 14 = 0x14011f890.
//
// The forester lodge has TWO independent work toggles, not one:
//   self+0x50 = "plant trees"   (command id 1)
//   self+0x51 = "cut trees"     (command id 2)
// The base pair has no concept of that, which is why this class overrides.

// ── slot 13 — retail 0x14011f920 — per-command availability mask ──────────────────
// Retail body:
//     if (cmd == 1) return -(*(char*)(self+0x50) != 0) & 2 | 1;
//     if (cmd == 2) return -(*(char*)(self+0x51) != 0) & 2 | 1;
//     return 1;
// `-(x) & 2 | 1` is the compiler's branchless form of `x ? 3 : 1`: bit 0 = "this command
// is offered", bit 1 = "it is currently ON". So an unknown command id still returns 1
// (offered, off) rather than 0 — returning 0 is what greys a ControlUI checkbox, and
// retail deliberately never does that here.
//
// ⚠ Unlike the ResourceProduce base slot 13, this one DOES read its command-id argument.
uint32_t ForesterComponent::CommandMask(int cmd) {
    if (cmd == 1) return plantTrees ? 3 : 1;
    if (cmd == 2) return cutTrees ? 3 : 1;
    return 1;
}

// ── slot 14 — retail 0x14011f890 — toggle one flag, notify only on edge ───────────
// Retail snapshots "is any flag set" BEFORE the toggle, flips the flag the command
// names, recomputes "is any flag set" AFTER, and calls the base slot-14 body only when
// that aggregate CHANGED (none->some or some->none). Toggling "cut" while "plant" is
// already on leaves the lodge working, so no state-machine event is posted — the edge
// test is the whole point of the override, not an optimisation.
//
// The tail call Ghidra prints as `HerbalistComponent_Slot14(param_1)` is address
// 0x14012b5a0 — the ResourceProduce BASE slot-14 body (ResourceProduceComponent::
// Command, a direct call: qualified here). The "Herbalist" name is a subclass label
// Ghidra attached to a base-vtable body; it is not the herbalist's function.
void ForesterComponent::Command(int cmd) {
    const bool cutBefore = cutTrees;
    const bool wasIdle   = (!cutBefore && !plantTrees);
    const bool wasActive = !wasIdle;

    if (cmd == 1)      plantTrees = !plantTrees;
    else if (cmd == 2) cutTrees   = !cutBefore;

    const bool nowIdle   = (!cutTrees && !plantTrees);
    const bool nowActive = !nowIdle;

    if ((wasIdle && nowActive) || (wasActive && nowIdle))
        ResourceProduceComponent::Command(cmd);
}

// ============================================================================
// ENDGAME B.3.8 MI-B — ForesterComponent as a REAL class (ForesterComponent.h).
//
// Before: a hand-expanded CONTROLLER_LIVE_FACTORY_STATES (48-slot table filled per
// create, Inert_ScalarDtor in slot 0, the shared RP dispatcher stamped at +0x28, the
// ctor's 0x101 store written by the factory). After: the compiler's 22-slot vtable,
// a real ctor/dtor chain, the controller the base's member (B38_RULES D1, D2, D6).
// ============================================================================
#include "Object/factory_registration.h"


// 0x14011fa40: the RP ctor (CALL 0x14012b3d0), the compiler's stamp, then
// `MOV word [RBX+0x50],0x101` — a freshly built lodge plants AND cuts.
ForesterComponent::ForesterComponent()
    : ResourceProduceComponent(), plantTrees(true), cutTrees(true), _pad52{} {}

// create 0x140034870: MemAlloc(0x58, 8), then the ctor.
::Object::Base* ForesterComponent::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(ForesterComponent), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) ForesterComponent());
}
// Retail's createInPlace is `Internal::CreateInPlaceObject` (returns null); the
// tree's in-place entry built the object, and that is kept (B38 B8-13).
::Object::Base* ForesterComponent::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) ForesterComponent());
}

void       ForesterComponent::Persist(IO::Stream& stream) {
    // Base ResourceProduceComponent::Shutdown (0x14012b4a0), a direct call in retail.
    ResourceProduceComponent::Persist(stream);

    // Serialize ForesterComponent's own fields
    IO::Serializer ser(stream, 0);
    ser.Serialize(plantTrees, nullptr);
    ser.Serialize(cutTrees, nullptr);
}
// [18] ForesterComponent::CreateWork is defined in resourceproduce_creatework.cpp.

DEFINE_EXE_TYPE(ForesterComponent, "ResourceProduceComponent")
