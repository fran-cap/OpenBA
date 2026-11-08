// src/game/Work/pickup_gate.cpp
// Pickup "does the town want this resource?" gate — faithful 1:1 ports of
// 0x14008d3c0 + its two helpers 0x14012d990 and 0x14012d560.
//
// 0x14008d3c0 is called from Pickup::Execute (0x1400f2f00) BEFORE anything else
// happens; if it answers false the whole pickup is abandoned ("no work available").
#include "Work/exe_bodies.h"
// It walks the town manager's storage-location list and asks, for each entry:
// "does this storage location accept this resource category, and can it physically
// take this amount?".
//
// Type-id cells (ENDGAME B.3.9 D5: read BY CELL through the class's own TypeId(),
// names recovered from their RegisterType DATA writes via Ghidra xrefs):
//   0x1401c7ee8 = StorageLocationComponent    (StorageLocationComponent_RegisterType) -- registered
//   0x1401c7f20 = StorageLocationDescription  (StorageLocationDescription_Register)  -- UNREGISTERED
//   0x1401c7e78 = StorageComponent            -- registered
//   0x1401c7cb8 = RawMaterialDescription      -- registered
//
// Sub-callees (all already registered): 0x140006270 Entity_FindComponent_Storage,
// 0x14012cd80 StorageComponent_impl_cd80 (CanAdd). Plus engine dispatch slots
// 0x140152a90 IBP ctor / 0x140152a98 IsDerivedFrom.
//
// Bodies are unconditional so this compiles as-is in the standalone exe.

#include "object/TypeInfo.h"
#include "Action/exe_bodies.h"      // B3R: 0x14012cd80
#include "object/Internal.h"
#include <new>
#include <cstdint>
#include "Object/factory_registration.h"
#include "Component/StorageLocationDescription.h"   // ENDGAME B.3.8 CB3: StorageLocationDescription::TypeId()
#include "Component/RawMaterialDescription.h"   // ENDGAME B.3.8 CB3: RawMaterialDescription::TypeId()
#include "Component/StorageLocationComponent.h"   // ENDGAME B.3.8 CB3: StorageLocationComponent::TypeId()
#include "Component/StorageComponent.h"   // ENDGAME B.3.8 CB3: StorageComponent::TypeId()
#include "Component/ComponentDescription.h"
#include "Component/Description.h"
#include "Entity/ComponentEntity.h"
#include "Entity/CitizenBehavior.h"

namespace {

// B3R: slot 0x140152a90 is ??0InternalBasePtr, slot 0x140152a98 is the raw member
// Object::Base::IsDerivedFrom; 0x14012cd80 is called by name.

// ⚠️ THE TWO SEARCHES BELOW WALK DIFFERENT STRUCTURES WITH DIFFERENT LAYOUTS. They look
// interchangeable and are NOT — sharing one helper crashed twice, once per call site:
//   8d3c0 searches a ComponentEntity's COMPONENTS:      _components / _components._size
//   12d990 searches a description object's DESCRIPTIONS: _subDescs / _subDescCount
// (12d990's param_2 is the entity's ComponentDescription, not the entity — and it looks
// for RawMaterialDESCRIPTION, not a component.) Both verified in their own disassembly.

// 8d3c0's search: the ComponentEntity's components. Per-slot CheckPtr, as the original,
// and no null-slot test (0x14008d410-0x14008d42d: [slot] -> [vtbl+0x10] directly).
template <class T>
T* PG_FindComp(ComponentEntity* entity) {
    const uint32_t tid = T::TypeId();
    int count = entity->_components._size;
    for (int i = 0; i < count; ++i) {
        Component* comp = entity->_components[i].Get();
        if (comp->Type() != tid) continue;
        Component* c = entity->_components[i].Get();
        return c->IsDerivedFrom(tid) ? static_cast<T*>(c) : nullptr;
    }
    return nullptr;
}

// 12d990's search: the description list. Faithful to the original's shape: NO per-slot
// CheckPtr, and it BREAKS on the first type-id match (returning null when that hit fails
// IsDerivedFrom, rather than scanning on). No null-entry test (0x14012d9c0-0x14012d9d1).
RawMaterialDescription* PG_FindRawMaterialDesc(ComponentDescription* descObj) {
    const uint32_t tid = RawMaterialDescription::TypeId();
    int count = descObj->_subDescCount;
    for (int i = 0; i < count; ++i) {
        Description* d = descObj->_subDescs[i];
        if (d->Type() != tid) continue;
        Description* c = descObj->_subDescs[i];
        return c->IsDerivedFrom(tid) ? static_cast<RawMaterialDescription*>(c) : nullptr;
    }
    return nullptr;
}

} // namespace

// ============================================================
// StorageLocationComponent::AcceptMask (0x14012d560) — accept-mask of a storage location.
//   m = (its description +0x18, as StorageLocationDescription) +0x20
//   if (m == 0) m = (StorageComponent of its entity (+0x10)) +0x3c
// NOTE: the original dereferences the IsDerivedFrom result unguarded (+0x20) and the
// found StorageComponent unguarded (+0x3c). Kept faithful — do not add null guards
// without re-deriving why the original is safe here.
// ============================================================
int StorageLocationComponent::AcceptMask() {
    Description* obj = _description;
    // 0x1401c7f20 (StorageLocationDescription's own cell) is UNREGISTERED -- the old
    // PG_Tid raw-.rdata read always failed and fell through to this same
    // StorageLocationDescription::TypeId() call, so calling it directly is not a
    // behaviour change (ENDGAME B.3.9 D5 note).
    uint32_t tidDesc = StorageLocationDescription::TypeId();
    StorageLocationDescription* desc =
        obj->IsDerivedFrom(tidDesc) ? static_cast<StorageLocationDescription*>(obj) : nullptr;

    int mask = static_cast<int>(desc->_locationType);
    if (mask == 0) {
        // the StorageComponent finder 0x140006270
        StorageComponent* storage = _entity->FindComponent<StorageComponent>();
        mask = static_cast<int>(storage->rawMaterialFlags);
    }
    return mask;
}

// ============================================================
// Pickup_helper_d990 (0x14012d990)
//   "Does this storage location's accept-mask cover this resource's category?"
//     resCat = (RawMaterialDescription of the resource's description) + 0x5c
//     return (resCat & slComp->AcceptMask()) != 0
// It reads no field of its first argument (it hands it to 0x14012d560): a free function.
// NOTE: the original dereferences the RawMaterialDescription result unguarded (+0x5c)
// when the component is absent. Kept faithful.
// ============================================================
static uint64_t PickupGate_Accepts_12d990(StorageLocationComponent* slComp, ComponentDescription* resEntity) {

    // NOTE: description-list layout, NOT the component layout — see the warning above
    // PG_FindComp. `resEntity` here is the entity's description object.
    RawMaterialDescription* rawDesc = PG_FindRawMaterialDesc(resEntity);

    uint32_t resCat = rawDesc->_flags;
    uint32_t mask   = static_cast<uint32_t>(slComp->AcceptMask());
    return ((resCat & mask) != 0) ? 1u : 0u;
}

// ============================================================
// CitizenBehavior::HasStorageFor (Pickup_helper_d3c0, 0x14008d3c0) — THE gate.
//   this     = the town manager (GameUpdateLoop::m_citizenList); its +0x00 entity list
//   resource = the resource's ComponentDescription
//   amount   = amount
// For each storage-location entity in the list: find its StorageLocationComponent and
// its StorageComponent, then require BOTH d990 (accepts this category) and
// StorageComponent_impl_cd80 (can physically take `amount`). Returns true on the first
// match, false if none — including when the list is empty. (Retail answers in AL.)
//
// Measured 2026-07-16: list0.count == 1 in standalone at game start (the Barn), so this
// loop does run; the "empty list" hypothesis was ruled out by probe, not assumed.
// No null-entry test: 0x14008d400 / 0x14008d45f read the entry and its +0x38 count directly.
// ============================================================
bool CitizenBehavior::HasStorageFor(ComponentDescription* resource, int amount) {
    // `if (0 < (int)param_1[1])` — the list's int32 count.
    int count = storageLocations._size;
    Object::InternalPtr<ComponentEntity>* arr = storageLocations._data;

    for (int i = 0; i < count; ++i) {
        Object::InternalPtr<ComponentEntity>* slot = &arr[i];

        ComponentEntity* ent = slot->Get();
        StorageLocationComponent* slComp = PG_FindComp<StorageLocationComponent>(ent);

        // The original re-reads the list entry (and re-CheckPtrs) for the second scan.
        ent = slot->Get();
        StorageComponent* stComp = PG_FindComp<StorageComponent>(ent);

        if (PickupGate_Accepts_12d990(slComp, resource)) {
            if (static_cast<char>(stComp->CanAdd(resource, amount)))   // 0x14012cd80; the door read AL
                return true;
        }
    }
    return false;
}
