// src/game/Action/pickupstoredrawmaterial.cpp
// Clean C++ reimplementation of PickupStoredRawMaterial action methods.
// Source: Game/Action/PickupStoredRawMaterial.cpp (from profile build debug strings)
//
// PickupStoredRawMaterial -- 0x48 bytes. Picks up materials from a storage building.
// Inherits: Object::Internal -> Action (0x20) -> PickupStoredRawMaterial
// Factory: 0x140022d60, Vtable: 0x14015ff80
//
// Vtable (10 slots):
//   [0] +0x00 ScalarDtor    = (base Action)
//   [1] +0x08 VectorDtor    = (base Action, unhookable)
//   [2] +0x10 BaseDtor      = (base Action, unhookable)
//   [3] +0x18 Serialize     = 0x1400ca0d0
//   [4] +0x20 AbstractImpl  = 0x1400c9d40 (complex storage query/transfer)
//   [5] +0x28 Execute       = 0x1400c4a90 (base Action, returns 0)
//   [6] +0x30 Enter         = 0x1400c4b90 (base Action, void nop)
//   [7] +0x38 Exit          = 0x1400c4ba0 (base Action, void nop)
//   [8] +0x40 CheckEntity   = 0x1400c9c70 (undo pickup on entity remove)
//   [9] +0x48 GetTarget     = 0x1400c9bf0
//
// Field layout (0x48 bytes):
//   +0x00-0x1F: Action base (vtable, Internal base, entity ptr, status byte)
//   +0x20: InternalBasePtr  targetBuilding  (building with storage to pick from)
//   +0x28: InternalBasePtr  pickupEntity    (entity doing the pickup, e.g. citizen)
//   +0x30: void*            materialList    (array of {ExternalPtr, int32_t count} pairs, 0x10 each)
//   +0x38: int32_t          materialCount   (number of entries in material list)
//   +0x40: bool             partialAllowed  (if true, can pick up less than requested)
//
// Decompiled from Application-x64.exe using Ghidra.

#include "PickupStoredRawMaterial.h"
#include "object/TypeInfo.h"
#include "Component/ComponentDescription.h"
#include "Component/Description.h"
#include "Action/exe_bodies.h"   // ActRm_* helpers
#include "Game/exe_bodies.h"     // Reimpl_ExternalPtr_Serialize_StringTable
#include "Work/exe_bodies.h"     // WorkBase_FindComponent_6270, WorkLeaf_FreeStorageCapacity_116540
#include "system/MemoryAllocator.h"   // System::MemoryAllocator::MemAlloc / MemFree
#include "io/Serializer.h"   // ENDGAME B.3.4 caller batch C
#include <cstdio>
#include <cstdint>
#include <new>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "Component/CitizenComponent.h"
#include "Component/RawMaterialDescription.h"
#include "Component/StorageComponent.h"
#include "Entity/ComponentEntity.h"

// ============================================================
// Helpers, called by name (B3R)
//   0x14012d150 ActRm_StorageSubtract_d150 -- StorageComponent::SubtractItem(storage, itemType,
//               count); if the count reaches 0, removes the slot
//   0x14012d000 ActRm_StorageAdd_d000      -- StorageComponent::AddItem(storage, itemType, count)
//   0x14000da70 ActRm_ArrayResize16_da70   -- array resize for 16-byte elements (material list)
//   0x140002340 Reimpl_ExternalPtr_Serialize_StringTable -- ExternalPtr serialize
// ============================================================

// ============================================================
// The RawMaterialDescription among a material template's sub-descriptions (retail
// inlines the walk: slot 2 Type(), then IsDerivedFrom; a Type() match that is not
// derived answers null).
// ============================================================
static RawMaterialDescription* FindRawMaterialDescription(const ComponentDescription* material) {
    int compCount = material->_subDescCount;
    for (int i = 0; i < compCount; i++) {
        Description* comp = material->_subDescs[i];
        uint32_t typeId = comp->Type();   // slot 2
        if (typeId == RawMaterialDescription::TypeId()) {
            if (comp->IsDerivedFrom(RawMaterialDescription::TypeId())) {
                return static_cast<RawMaterialDescription*>(comp);
            }
            return nullptr;
        }
    }
    return nullptr;
}

// ============================================================
// PickupStoredRawMaterial::Serialize (0x1400ca0d0) -- slot 3
// Serializes: status(+0x18), citizen(+0x20), storage entity(+0x28),
//             material count(+0x38), material list(+0x30), partial flag(+0x40)
// ============================================================
void PickupStoredRawMaterial::Persist(IO::Stream& stream) {
    Action::Persist(stream);   // inlined in retail: Serializer(stream, 0), status +0x18

    // Serialize entity refs, material list, and flags
    IO::Serializer ser(stream, 0);

    // The carrying citizen (+0x20)
    _citizen.InternalBasePtr::Serialize(ser);
    if (!_citizen.IsNull()) {
        if (!::Object::IsDerivedFromNullSafe(_citizen, CitizenComponent::TypeId())) {
            _citizen = nullptr;
        }
    }

    // The storage entity (+0x28)
    _storageEntity.InternalBasePtr::Serialize(ser);
    if (!_storageEntity.IsNull()) {
        if (!::Object::IsDerivedFromNullSafe(_storageEntity, ComponentEntity::TypeId())) {
            _storageEntity = nullptr;
        }
    }

    // Serialize material count (+0x38)
    int matCount = _materials.count;
    ser.Serialize(matCount, nullptr);

    // If loading, resize the material array
    if (static_cast<int>(ser.GetDirection()) == 0) {   // IO::Serializer::GetDirection, retail 0x180006210
        // LEFT: the shared 16-byte Array SetSize 0x14000da70 is ACT-a's file-local body,
        // reached only through its int64_t* forwarder.
        MaterialArraySetSize(&_materials, matCount);   // 0x14000da70
    }

    // Serialize each material entry (ExternalPtr + int count, 0x10 bytes each)
    Entry* entry = _materials.data;
    if (matCount > 0) {
        uint64_t remaining = (uint64_t)matCount;
        do {
            ::Object::SerializeExternal<ComponentDescription>(ser, entry->material);   // 0x140002340
            ser.Serialize(entry->count, nullptr);
            ++entry;
            remaining--;
        } while (remaining != 0);
    }

    // Serialize partial pickup flag (+0x40)
    ser.Serialize(_allowPartial, nullptr);
}

// ============================================================
// PickupStoredRawMaterial::AbstractImpl (0x1400c9d40) -- slot 4
// Storage query and transfer:
// For each material in the list (+0x30):
//   1. Find RawMaterialDescription on the material to get its unit weight
//   2. If requested count <= 0, compute it from the citizen's free storage capacity
//   3. Check the quantity in the storage entity's StorageComponent
//   4. If partial not allowed and insufficient: return 2 (fail)
//   5. If partial allowed, clamp to available; if 0: return 2
//   6. Release the reservation in the storage
//   7. Move the material: SubtractItem there, AddItem into the citizen's storage
// Returns 0 on success, 2 on insufficient materials.
// ============================================================
int  PickupStoredRawMaterial::AbstractImpl(ActionStack* actionStack) {
    (void)actionStack;

    // The storage entity's StorageComponent (+0x28)
    StorageComponent* storage = _storageEntity->FindComponent<StorageComponent>();   // 0x140006270

    int matIdx = 0;
    if (matIdx >= _materials.count) {
        return 0;
    }

    do {
        ComponentDescription* materialType = _materials.data[matIdx].material;

        // Find RawMaterialDescription on the material template
        RawMaterialDescription* rawMatDesc = FindRawMaterialDescription(materialType);

        // Requested count
        int requestedCount = _materials.data[matIdx].count;

        if (requestedCount < 1) {
            // Count not specified: compute from the citizen's storage capacity
            StorageComponent* parentStorage = _citizen->_entity->FindComponent<StorageComponent>();

            int maxCapacity = parentStorage->capacity;
            int currentTotal = parentStorage->QueryCount();   // 0x14012cf10

            // Compute how many units we can pick up based on remaining capacity
            // B6 (14d G69): (cap - stored) / [desc+0x48] through the narrowed (or 0)
            // pointer with no test (0x1400c9d40, after LAB_1400c9e36).
            int quantityPerUnit = rawMatDesc->_weight;
            requestedCount = (maxCapacity - currentTotal) / quantityPerUnit;
        }

        // Look up available quantity of this material in the storage
        int availableCount = 0;
        int slotCount = storage->storageSlots._size;
        if (slotCount > 0) {
            StorageComponent::Item* itemArray = storage->storageSlots._data;
            for (int i = 0; i < slotCount; i++) {
                if (itemArray[i]._resource == _materials.data[matIdx].material) {
                    if (i >= 0) {
                        availableCount = itemArray[i]._count;
                    }
                    break;
                }
            }
        }

        // Check availability
        bool partialAllowed = _allowPartial;
        if (!partialAllowed) {
            if (availableCount < requestedCount) {
                return 2;  // Insufficient, fail
            }
        } else {
            if (availableCount < requestedCount) {
                requestedCount = availableCount;
            }
            if (requestedCount == 0) {
                return 2;  // Nothing to pick up
            }
        }

        // Release the reservation in the storage
        slotCount = storage->storageSlots._size;
        if (slotCount > 0) {
            StorageComponent::Item* itemArray = storage->storageSlots._data;
            for (int i = 0; i < slotCount; i++) {
                if (itemArray[i]._resource == _materials.data[matIdx].material) {
                    if (i >= 0) {
                        itemArray[i]._reserve -= requestedCount;
                        if (itemArray[i]._reserve < 0) {
                            itemArray[i]._reserve = 0;
                        }
                    }
                    break;
                }
            }
        }

        // Take it out of the storage
        storage->SubtractItem(_materials.data[matIdx].material, requestedCount);   // 0x14012d150

        // Put it into the citizen's storage
        ComponentDescription* itemType = _materials.data[matIdx].material;
        StorageComponent* parentStorage = _citizen->_entity->FindComponent<StorageComponent>();
        parentStorage->AddItem(itemType, requestedCount);   // 0x14012d000

        matIdx++;
    } while (matIdx < _materials.count);

    return 0;
}

// ============================================================
// PickupStoredRawMaterial::CheckEntity (0x1400c9c70) -- slot 8
// Undoes the pickup when the entity is removed: releases every entry's reservation
// in the storage.
// ============================================================
void PickupStoredRawMaterial::CheckEntity() {
    // The storage entity's StorageComponent (+0x28)
    StorageComponent* storage = _storageEntity->FindComponent<StorageComponent>();   // 0x140006270

    int matCount = _materials.count;
    if (matCount <= 0) return;

    for (int i = 0; i < matCount; i++) {
        const Entry& entry = _materials.data[i];

        int slotCount = storage->storageSlots._size;
        if (slotCount > 0) {
            StorageComponent::Item* itemArray = storage->storageSlots._data;
            for (int j = 0; j < slotCount; j++) {
                if (itemArray[j]._resource == entry.material) {
                    if (j >= 0) {
                        itemArray[j]._reserve -= entry.count;
                        if (itemArray[j]._reserve < 0) {
                            itemArray[j]._reserve = 0;
                        }
                    }
                    break;
                }
            }
        }
    }
}

// ============================================================
// PickupStoredRawMaterial::GetTarget (0x1400c9bf0) -- slot 9
// Returns true if param matches the storage entity at +0x28, or the citizen's (+0x20)
// owning entity (Component +0x10).
// ============================================================
bool PickupStoredRawMaterial::GetTarget(ComponentEntity** param) {
    void* target = *param;

    if (target == _storageEntity.Get()) {
        return true;
    }

    return _citizen->_entity.Get() == target;
}

// ============================================================
// class PickupStoredRawMaterial (PickupStoredRawMaterial.h) — ENDGAME B.3.8 ACT.
// The hand-built g_PickupStoredRawMaterial_Vtable[10], its per-class type-id cache and the
// Action_Alloc / Action_InitInPlace factories are gone: the compiler emits
// the table, DECLARE_EXE_TYPE answers slots 1/2.
// ============================================================
#include "Object/factory_registration.h"

// create 0x140022d60 (inlined ctor). The tree's factory zeroed every field above
// +0x10; kept (not a behaviour change of this step).
PickupStoredRawMaterial::PickupStoredRawMaterial()
    : _citizen(nullptr),
      _storageEntity(nullptr),
      _materials{nullptr, 0, 0},
      _allowPartial(false),
      _pad41{} {}

// The dtor body of 0x140022dd0, between its own stamp and the Action stamp:
//   MOV RCX,[this+0x30]; CALL MemFree (IAT 0x140152b68, unconditional);
//   [this+0x30] = 0; [this+0x38] = 0 (the qword: count +0x38 and capacity +0x3c).
// The tree's shared Action_ScalarDtor skipped it and leaked the carrier list.
PickupStoredRawMaterial::~PickupStoredRawMaterial() {
    Object::detail::InternalMemFree(_materials.data);
    _materials.data = nullptr;
    _materials.count = 0;   // H7: retail clears count and capacity with one qword store
    _materials.capacity = 0;
}

// [3] 0x1400ca0d0
// [4] 0x1400c9d40
// [8] 0x1400c9c70
// [9] 0x1400c9bf0

::Object::Base* PickupStoredRawMaterial::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(PickupStoredRawMaterial), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) PickupStoredRawMaterial());
}
::Object::Base* PickupStoredRawMaterial::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) PickupStoredRawMaterial());
}


// ============================================================
// FUN_1400ca230 — PickupStoredRawMaterial::AddMaterial (Resource_TransferToCarrier)
// Add `count` of `material` to the list at +0x30 (count +0x38, cap +0x3c, stride 0x10:
// material, count, reserved), and bump the matching storage item's reserve by `count`.
// ============================================================
void PickupStoredRawMaterial::AddMaterial(ComponentDescription* material, int count) {
    StorageComponent* storage = _storageEntity->FindComponent<StorageComponent>();   // 0x140006270

    // bump the storage item's reserve for the matching material
    int invCount = storage->storageSlots._size;
    if (0 < invCount) {
        StorageComponent::Item* p = storage->storageSlots._data;
        int idx = 0;
        while (true) {
            if (p->_resource == material) {
                if (idx >= 0) {
                    storage->storageSlots[idx]._reserve += count;
                }
                break;
            }
            idx++; p++;
            if (idx >= invCount) break;
        }
    }

    // list find: if the material is already present, add to its count and return
    int carCount = _materials.count;
    if (0 < carCount) {
        Entry* p = _materials.data;
        int idx = 0;
        while (true) {
            if (p->material == material) {
                _materials.data[idx].count += count;
                return;
            }
            idx++; p++;
            if (idx >= carCount) break;
        }
    }

    // not found: append {material, count, reserved=0} (grow-if-full)
    uint32_t oldCount = static_cast<uint32_t>(_materials.count);
    uint32_t newCount = oldCount + 1;
    if (newCount == oldCount) return;
    Entry newEntry;
    newEntry.material = material;
    newEntry.count = count;
    newEntry.reserved = 0;
    Entry* oldArr = _materials.data;
    _materials.count = static_cast<int32_t>(newCount);
    if (_materials.capacity < (int)newCount) {
        _materials.capacity = static_cast<int32_t>(newCount);
        if (oldCount != 0) _materials.capacity = static_cast<int32_t>(newCount * 3);
        _materials.data = System::MemoryAllocator::_singleton
            ? static_cast<Entry*>(System::MemoryAllocator::MemAlloc((uint64_t)((int64_t)_materials.capacity << 4), 8))
            : nullptr;
    }
    int copyStart = 0;
    if (oldArr == _materials.data) copyStart = (int)oldCount;
    int64_t i = copyStart;
    if (i < (int)newCount) {
        do {
            Entry* dst = &_materials.data[i];
            if (dst != nullptr) {
                const Entry* src = (i < (int)oldCount) ? &oldArr[i] : &newEntry;
                *dst = *src;
            }
            i++;
        } while (i < (int)newCount);
    }
    if (oldArr != _materials.data) System::MemoryAllocator::MemFree(oldArr);
}


// ============================================================
// FUN_1400ca520 — PickupStoredRawMaterial_Action_CopyCtor
//   RCX=self, RDX=param_2, R8=param_3, R9=param_4(material),
//   [stack]=param_5(int requested), param_6(byte @ +0x40).
// Field-init then, if param_4!=0: locate the material's RawMaterialDescription
// (sub-description scan vs type-id 0x1401c7cb8) to get its unit weight at +0x48;
// if requested(param_5)<=0 compute it from FUN_140116540(self+0x20)/unitWeight;
// clamp to the storage availability (count-reserved) for the material; then add.
// ============================================================
// 0x1400ca520 — the ctor body: the field init is the initialiser list (+0x30 / +0x38 one
// qword each in retail: data, then count + capacity); everything after it is the retail
// body.
PickupStoredRawMaterial::PickupStoredRawMaterial(CitizenComponent* const* citizen,
                                                 ComponentEntity* const* storageEntity,
                                                 ComponentDescription* material, int param_5,
                                                 bool flag)
    : _citizen(*citizen),
      _storageEntity(*storageEntity),
      _materials{nullptr, 0, 0},
      _allowPartial(flag) {
    StorageComponent* storage = _storageEntity->FindComponent<StorageComponent>();   // 0x140006270

    if (material == nullptr) return;

    uint32_t wantTid = RawMaterialDescription::TypeId();

    // sub-description scan of the material for Type() == wantTid, IsDerivedFrom-confirm
    RawMaterialDescription* rawMatDesc = nullptr;
    int srcCount = material->_subDescCount;
    if (0 < srcCount) {
        int idx = 0;
        while (true) {
            Description* child = material->_subDescs[idx];
            uint32_t tid = child->Type();   // slot 2
            if (tid == wantTid) {
                Description* match = material->_subDescs[idx];
                if (match->IsDerivedFrom(RawMaterialDescription::TypeId()))
                    rawMatDesc = static_cast<RawMaterialDescription*>(match);
                break;
            }
            idx++;
            if (idx >= srcCount) break;
        }
    }

    int requested = param_5;
    if (requested <= 0) {
        // LEFT: 0x140116540 is declared in Work/exe_bodies.h (WORK) taking the citizen as intptr_t.
        int total = _citizen.Get()->FreeStorageCapacity();   // 0x140116540
        // B6 (14d G69): param_5 = total / [desc+0x48] through the narrowed (or 0) pointer,
        // no null or zero test (0x1400ca520).
        requested = total / rawMatDesc->_weight;
    }

    // storage availability for the material = count - reserved; clamp requested down to it
    int avail = 0;
    {
        int invCount = storage->storageSlots._size;
        if (0 < invCount) {
            StorageComponent::Item* inv = storage->storageSlots._data;
            int idx = 0; StorageComponent::Item* p = inv;
            while (true) {
                if (p->_resource == material) {
                    if (idx >= 0)
                        avail = inv[idx]._count - inv[idx]._reserve;
                    break;
                }
                idx++; p++;
                if (idx >= invCount) break;
            }
        }
    }
    if (avail < requested) {
        // re-scan to set requested = avail (count - reserved)
        requested = 0;
        int invCount = storage->storageSlots._size;
        if (0 < invCount) {
            StorageComponent::Item* inv = storage->storageSlots._data;
            int idx = 0; StorageComponent::Item* p = inv;
            while (true) {
                if (p->_resource == material) {
                    if (idx >= 0)
                        requested = inv[idx]._count - inv[idx]._reserve;
                    break;
                }
                idx++; p++;
                if (idx >= invCount) break;
            }
        }
    }

    AddMaterial(material, requested);   // 0x1400ca230
}

DEFINE_EXE_TYPE(PickupStoredRawMaterial, "Action")
