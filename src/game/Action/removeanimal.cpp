// src/game/Action/removeanimal.cpp
// Clean C++ reimplementation of RemoveAnimal action methods.
// Source: Game/Action/RemoveAnimal.cpp (from profile build debug strings)
//
// RemoveAnimal -- smallest action (0x28 bytes). Removes an animal entity from the world.
// Inherits: Object::Internal -> Action (0x20) -> RemoveAnimal
// Factory: 0x140022e70, Vtable: 0x14015ffe0
//
// Vtable (10 slots):
//   [0] +0x00 ScalarDtor    = 0x140022ee0
//   [1] +0x08 VectorDtor    = 0x140022e60 (returns type ptr, unhookable)
//   [2] +0x10 BaseDtor      = 0x140022e50 (returns TypeInfo, unhookable)
//   [3] +0x18 Serialize     = 0x1400ca870
//   [4] +0x20 AbstractImpl  = 0x1400ca7f0  (DISABLED: RIP-relative MOV)
//   [5] +0x28 Execute       = 0x1400c4a90  (base Action, returns 0)
//   [6] +0x30 Enter         = 0x1400c4b90  (base Action, void nop)
//   [7] +0x38 Exit          = 0x1400c4ba0  (base Action, void nop)
//   [8] +0x40 CheckEntity   = 0x1400ca760  (DISABLED: RIP-relative CALL)
//   [9] +0x48 GetTarget     = 0x1400ca720
//
// Decompiled from Application-x64.exe using Ghidra.

#include "RemoveAnimal.h"
#include "object/TypeInfo.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "Action/exe_bodies.h"   // MarkEntityForRemoval, the 0x140003110 forwarder
#include "Component/AnimalComponent.h"
#include "Entity/ComponentEntity.h"
#include "Entity/EntityList.h"
#include "io/Serializer.h"   // ENDGAME B.3.4 caller batch C
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <new>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B

// ============================================================
// Helpers, called by name (B3R)
//   0x1400c2ea0 ActHunt_MarkEntityRemoval_c2ea0 -- marks entity for removal (sets bit 10 of
//               flags at +0x40)
//   0x140005500 ActHunt_FindComponent_5500 -- get model component from entity (entity, outIBP)
//   0x1400c3470 ActHunt_NotifyEntityChanged_c3470 -- notify entity changed (visual update)
//
// FUN_140003110 (array push) is the exe-side Collection::Array<Ptr8>::
// SetSize(n,fill) monomorphisation -- ENDGAME B.3.2 step 2 converted its one
// call site below to a member call on the typed array, so its
// resolved-pointer stand-in is gone.
//
// ENDGAME B.3.9 CB: retail reads this record field ONLY in the class's own TypeName
// slot (the type-id cell 0x1401c6080 by RemoveAnimal_Slot2 0x140022e50 and the
// NAME field 0x1401c6088 (+8) by RemoveAnimal_Slot1 0x140022e60 -- both slots); DECLARE_EXE_TYPE answers that slot, so the tree's copy of it is gone.
// ============================================================

// ============================================================
// Slot 3: Serialize (0x1400ca870)
// Serializes status byte (+0x18) and entity ref at +0x20 with ComponentEntity
// type validation on deserialization.
// ============================================================
void RemoveAnimal::Persist(IO::Stream& stream) {
    Action::Persist(stream);   // inlined in retail: Serializer(stream, 0), status +0x18

    // Serialize entity ref at +0x20
    IO::Serializer ser(stream, 0);
    _field20.InternalBasePtr::Serialize(ser);

    // Type-check: a loaded ref that is not a ComponentEntity is cleared
    // (0x1400ca8d8 TEST / 0x1400ca8e1 MOV EDX,[0x1401c5d70] / 0x1400ca8f1 store 0).
    // NOT RETAIL (F-143): null and type tests on the held Internal*, before any
    // downcast to ComponentEntity*; the answer is the same.
    if (!_field20.IsNull()) {
        if (!::Object::IsDerivedFromNullSafe(_field20, ComponentEntity::TypeId())) {
            _field20 = nullptr;
        }
    }
}

// ============================================================
// Slot 4: AbstractImpl (0x1400ca7f0) -- DISABLED hook (RIP-relative MOV in prologue)
// Marks the animal entity at +0x20 for removal, queues it on the removal list.
// Returns 0 (action complete).
// ============================================================
int  RemoveAnimal::AbstractImpl(ActionStack* actionStack) {
    (void)actionStack;
    ComponentEntity* entityRef = _field20.Get();

    // ~310 kills produced ZERO component dumps below, and the dump sits inside the
    // `entityRef != nullptr` arm — so "RemoveAnimal never ran" and "RemoveAnimal ran with
    // no animal" look identical from there. Count at the TOP, before any branch, and
    // report both arms.
    {
        static long long s_calls = 0, s_null = 0;
        ++s_calls;
        if (!entityRef) ++s_null;
    }

    // The world's EntityList
    EntityList* cleanupMgr = GameUpdateLoop::_singleton->m_entityList;

    if (entityRef != nullptr) {
        // Copy to local for CheckPtr
        ComponentEntity* localRef = entityRef;

        // Check bit 10 of the entity flags (already marked for removal?)
        uint32_t flags = localRef->_entityFlags;
        if ((flags & Bits(ComponentFlags::MarkedForRemoval)) == 0) {

            // Mark entity for removal
            localRef->MarkForRemoval();   // 0x1400c2ea0

            // Append to EntityList::removalList (+0xD0) through 0x140003110 (Array SetSize(n, fill))
            cleanupMgr->removalList.SetSize(cleanupMgr->removalList._size + 1, localRef);   // 0x140003110
        }
    }
    return 0;
}

// ============================================================
// Slot 8: CheckEntity (0x1400ca760) -- DISABLED hook (RIP-relative CALL in prologue)
// Clears the "in use" bit (bit 20) of the animal's AnimalComponent flags.
// If the low 16 bits of flags changed, notifies the owning entity.
// ============================================================
void RemoveAnimal::CheckEntity() {
    AnimalComponent* animal = _field20->FindComponent<AnimalComponent>();   // 0x140005500

    uint32_t flagsOld = animal->_flags;

    if ((flagsOld >> 20) & 1) {
        flagsOld = animal->_flags;
        uint32_t flagsNew = flagsOld & 0xFFEFFFFF;  // clear bit 20
        animal->_flags = flagsNew;

        if (((flagsNew ^ flagsOld) & 0xFFFF) != 0) {
            if (animal->_entity != nullptr) {
                animal->_entity.Get()->RefreshEntityFlags();   // 0x1400c3470
            }
        }
    }
}

// ============================================================
// Slot 9: GetTarget (0x1400ca720)
// Returns true if *param matches the entity at +0x20.
// ============================================================
bool RemoveAnimal::GetTarget(ComponentEntity** param) {
    return *param == _field20.Get();
}

// ============================================================
// class RemoveAnimal (RemoveAnimal.h) — ENDGAME B.3.8 ACT.
// The hand-built g_RemoveAnimal_Vtable[10], its per-class type-id cache and the
// Action_Alloc / Action_InitInPlace factories are gone: the compiler emits
// the table, DECLARE_EXE_TYPE answers slots 1/2.
// ============================================================
#include "Object/factory_registration.h"

// create 0x140022e70 (inlined ctor). The tree's factory zeroed every field above
// +0x10; kept (not a behaviour change of this step).
RemoveAnimal::RemoveAnimal()
    : _field20(nullptr) {}

// Retail has no out-of-line RemoveAnimal ctor: 0x1400bbea0 inlines it
// (Internal(), Action stamp, +0x10/+0x18 = 0, RemoveAnimal stamp, +0x20 = animal).
RemoveAnimal::RemoveAnimal(ComponentEntity* animal)
    : _field20(animal) {}

// [3] 0x1400ca870
// [4] 0x1400ca7f0
// [8] 0x1400ca760
// [9] 0x1400ca720

::Object::Base* RemoveAnimal::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(RemoveAnimal), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) RemoveAnimal());
}
::Object::Base* RemoveAnimal::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) RemoveAnimal());
}


DEFINE_EXE_TYPE(RemoveAnimal, "Action")
