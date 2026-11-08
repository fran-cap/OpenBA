// src/game/Action/pickuprawmaterial.cpp
// Clean C++ reimplementation of PickupRawMaterial action methods.
// Source: Game/Action/PickupRawMaterial.cpp (from profile build debug strings)
//
// PickupRawMaterial -- 0x30 bytes. Picks up a raw material from a source entity.
// Inherits: Object::Internal -> Action (0x20) -> PickupRawMaterial
// Factory: 0x140022c60, Vtable: 0x14015ff10
//
// Vtable (10 slots):
//   [0] +0x00 ScalarDtor    = (base Action)
//   [1] +0x08 VectorDtor    = (base Action, unhookable)
//   [2] +0x10 BaseDtor      = (base Action, unhookable)
//   [3] +0x18 Serialize     = 0x1400c99e0
//   [4] +0x20 AbstractImpl  = 0x1400c98d0 (picks up raw material)
//   [5] +0x28 Execute       = 0x1400c4a90 (base Action, returns 0)
//   [6] +0x30 Enter         = 0x1400c4b90 (base Action, void nop)
//   [7] +0x38 Exit          = 0x1400c4ba0 (base Action, void nop)
//   [8] +0x40 CheckEntity   = 0x1400c4a90 (base Action)
//   [9] +0x48 GetTarget     = 0x1400c9860
//
// Field layout (0x30 bytes):
//   +0x00-0x1F: Action base (vtable, Internal base, entity ptr, status byte)
//   +0x20: InternalBasePtr  sourceBuilding  (building to pick up from)
//   +0x28: InternalBasePtr  sourceEntity    (entity carrying the raw material)
//
// Decompiled from Application-x64.exe using Ghidra.

#include "PickupRawMaterial.h"
#include "object/TypeInfo.h"
#include "Component/RawMaterialComponent.h"
#include "Component/StorageComponent.h"
#include "Entity/EntityList.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "Action/exe_bodies.h"   // ActRm_*, ActHunt_* helpers
#include "io/Serializer.h"   // ENDGAME B.3.4 caller batch C
#include "Component/CitizenComponent.h"
#include "Entity/ComponentEntity.h"
#include <cstdio>
#include <cstdint>
#include <new>
#include "object/Internal.h"   // ENDGAME B.3.4 caller batch G

// ============================================================
// Helpers, called by name (B3R)
//   0x1400223c0 ActRm_FindComponent_223c0 -- FindComponent for the RawMaterial component
//               (type ID DAT_1401c7c80): (entity, retval_IBP), returns the IBP
//   0x14012d000 ActRm_StorageAdd_d000 -- StorageComponent::AddItem(storage, itemType, count)
//   0x1400c2ea0 ActHunt_MarkEntityRemoval_c2ea0 -- entity flag set (sets bit 0x400 on entity
//               +0x40 flags, optionally exits components)
//
// FUN_140003110 (DynArrayPush 8-byte elements, used to add the entity to the game
// world list) is the exe-side Collection::Array<Ptr8>::SetSize(n,fill) mono-
// morphisation -- ENDGAME B.3.2 step 2 converted its one call site below to a
// member call on the typed array, so its resolved-pointer stand-in
// is gone.
// ============================================================

// ============================================================
// PickupRawMaterial::Serialize (0x1400c99e0)
// Serializes: status(+0x18), entity(+0x20) with type check DAT_1401c7158,
//             entity(+0x28) with type check ComponentEntity
// ============================================================
void PickupRawMaterial::Persist(IO::Stream& stream) {
    Action::Persist(stream);   // inlined in retail: Serializer(stream, 0), status +0x18

    // Serialize entity refs
    IO::Serializer ser(stream, 0);

    // The carrying citizen (+0x20)
    _citizen.InternalBasePtr::Serialize(ser);
    if (!_citizen.IsNull()) {
        if (!::Object::IsDerivedFromNullSafe(_citizen, CitizenComponent::TypeId())) {
            _citizen = nullptr;
        }
    }

    // The raw-material pile entity (+0x28)
    _pile.InternalBasePtr::Serialize(ser);
    if (!_pile.IsNull()) {
        if (!::Object::IsDerivedFromNullSafe(_pile, ComponentEntity::TypeId())) {
            _pile = nullptr;
        }
    }
}

// ============================================================
// PickupRawMaterial::AbstractImpl (0x1400c98d0) -- slot 4
// Picks up the raw-material pile (+0x28):
// 1. Finds the pile's RawMaterialComponent (0x1400223c0)
// 2. Its quantity and the pile's template (ComponentEntity::_description)
// 3. Finds the StorageComponent on the citizen's (+0x20) entity
// 4. Adds the material to the storage (0x14012d000)
// 5. Marks the pile for removal and queues it on the EntityList's removal list
// Returns 0 (action complete).
// ============================================================
int  PickupRawMaterial::AbstractImpl(ActionStack* actionStack) {
    (void)actionStack;

    // The pile's RawMaterialComponent and its quantity
    RawMaterialComponent* rawMaterial = _pile->FindComponent<RawMaterialComponent>();   // 0x1400223c0
    int quantity = rawMaterial->quantity;

    // The pile's template
    ComponentDescription* itemType = _pile->_description;

    // The StorageComponent on the citizen's entity
    StorageComponent* storage = _citizen->_entity->FindComponent<StorageComponent>();   // 0x140006270

    // Add the material to the storage
    storage->AddItem(itemType, quantity);   // 0x14012d000

    // Check the pile entity again, flag it for removal if not yet flagged
    ComponentEntity* entityRef = _pile.Get();
    EntityList* worldList = GameUpdateLoop::_singleton->m_entityList;

    if (entityRef != nullptr) {
        ComponentEntity* localEntity = entityRef;

        // Check bit 10 of the entity flags (+0x40)
        uint32_t flags = localEntity->_entityFlags;
        if ((flags & Bits(ComponentFlags::MarkedForRemoval)) == 0) {
            // Not yet flagged -- flag it and queue it on the removal list
            localEntity->MarkForRemoval();   // 0x1400c2ea0

            // Append to EntityList::removalList (+0xD0) through 0x140003110 (Array SetSize(n, fill))
            worldList->removalList.SetSize(worldList->removalList._size + 1, localEntity);   // 0x140003110
        }
    }

    return 0;
}

// ============================================================
// PickupRawMaterial::GetTarget (0x1400c9860) -- slot 9
// Returns true if param matches the pile at +0x28, or the citizen's (+0x20) owning
// entity (Component +0x10).
// ============================================================
bool PickupRawMaterial::GetTarget(ComponentEntity** param) {
    void* paramVal = *param;

    if (_pile.Get() == paramVal) {
        return true;
    }

    return _citizen->_entity.Get() == paramVal;
}

// ============================================================
// class PickupRawMaterial (PickupRawMaterial.h) — ENDGAME B.3.8 ACT.
// The hand-built g_PickupRawMaterial_Vtable[10], its per-class type-id cache and the
// Action_Alloc / Action_InitInPlace factories are gone: the compiler emits
// the table, DECLARE_EXE_TYPE answers slots 1/2.
// ============================================================
#include "Object/factory_registration.h"

// create 0x140022c60 (inlined ctor). The tree's factory zeroed every field above
// +0x10; kept (not a behaviour change of this step).
PickupRawMaterial::PickupRawMaterial()
    : _citizen(nullptr),
      _pile(nullptr) {}

// [3] 0x1400c99e0
// [4] 0x1400c98d0
// [9] 0x1400c9860

::Object::Base* PickupRawMaterial::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(PickupRawMaterial), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) PickupRawMaterial());
}
::Object::Base* PickupRawMaterial::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) PickupRawMaterial());
}


DEFINE_EXE_TYPE(PickupRawMaterial, "Action")
