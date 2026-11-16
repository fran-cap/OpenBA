// src/game/Component/buildcomponent_sub8630.cpp
// Clean C++ port of BuildDescription_helper_sub_8630 @ 0x140108630.
//
// "Have all required construction materials been delivered to the site?"
// Returns true when, for every material in the BuildComponent's BuildDescription
// material list, the site StorageComponent holds at least (costQty * multiplier)
// units; returns false as soon as any material falls short.
//
// This is the gate the state-1 enter handler uses:
//   false -> spawn a GatherRawMaterials work (keep hauling materials)
//   true  -> transition to the build state.
//
// Faithful offset-by-offset port. Verified against Ghidra disassembly of
// 0x140108630 and its callees (Entity_FindComponent_Storage @0x140006270,
// InternalBasePtr::CheckPtr, Object::Base::IsDerivedFrom).
//
// BuildComponent layout used here:
//   +0x10  entity InternalBasePtr (site entity, owns the site StorageComponent)
//   +0x18  BuildDescription*  (type "BuildDescription", DWORD_1401c7040)
//   +0x4c  int  material multiplier  (required = costQty * multiplier)
// BuildDescription layout:
//   +0x20  material array base  { void* rawMaterial @0x00, int costQty @0x08 } stride 0x10
//   +0x28  int  material count
// StorageComponent (site) layout:
//   +0x28  slot array base      { void* type @0x00, int total @0x08, int reserved @0x0c } stride 0x10
//   +0x30  int  slot count
//   delivered(material) = slot.total - slot.reserved for the matching slot type
//

#include "object/TypeInfo.h"
#include "Component/BuildComponent.h"
#include "Component/BuildDescription.h"   // B.3.8 CB1a (TypeId)
#include "Component/StorageComponent.h"
#include "Entity/ComponentEntity.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "Object/factory_registration.h"

// Type-id for "BuildDescription" (DWORD_1401c7040). The .rdata BaseTypeInfo is
// created by BuildDescription_Register (0x14014e5f5):
//   Object::BaseTypeInfo::BaseTypeInfo(&DWORD_1401c7040,"BuildDescription",...)
// so the name is verified. Read via BuildDescription::TypeId().
static uint32_t BC8630_BuildDescTypeId() {
    return BuildDescription::TypeId();   // B.3.8: the lazy cache over it went (B6-5)
}

// 0x140108630
bool BuildComponent::HaveAllMaterials() {
    // --- BuildDescription at +0x18, type-checked ---
    Description* descObj = _description;
    uint32_t descTid = BC8630_BuildDescTypeId();
    BuildDescription* desc = nullptr;
    if (descObj && descTid && ::Object::IsDerivedFromNullSafe(descObj, descTid))
        desc = static_cast<BuildDescription*>(descObj);
    // Original derefs desc unconditionally; if desc were null it would loop over
    // count at desc+0x28 (crash). Guard: no BuildDescription => no required
    // materials => treat as "all delivered".
    if (!desc) return true;

    // --- the site StorageComponent (0x140006270 FindComponent<StorageComponent>) ---
    StorageComponent* storage = _entity->FindComponent<StorageComponent>();

    int matCount   = desc->_costCount;
    int multiplier = costMultiplier;
    BuildCost* matArr = desc->_costs;

    for (int i = 0; i < matCount; i++) {
        void* matType = matArr[i].material;   // +0x00 rawMaterial
        int   costQty = matArr[i].quantity;   // +0x08 costQty

        // Delivered amount = matching site-storage slot's (total - reserved), else 0.
        int delivered = 0;
        if (storage) {
            int slotCount = storage->storageSlots._size;
            StorageComponent::Item* slots = storage->storageSlots._data;
            if (slots) {
                for (int s = 0; s < slotCount; s++) {
                    if (slots[s]._resource == matType) {
                        int total    = slots[s]._count;     // +0x08
                        int reserved = slots[s]._reserve;   // +0x0c
                        delivered = total - reserved;
                        break;
                    }
                }
            }
        }

        int required = costQty * multiplier;
        if (delivered < required)
            return false;   // this material is not fully delivered yet
    }

    return true;   // every material satisfied (or matCount <= 0)
}
