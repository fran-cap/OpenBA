// src/game/Action/dropoffrawmaterial.cpp
// Clean C++ reimplementation of DropOffRawMaterial action methods.
// Source: Game/Action/DropOffRawMaterial.cpp (from profile build debug strings)
//
// DropOffRawMaterial — 0x40 bytes. Drops off raw material to storage.
// Inherits: Object::Internal -> Action (0x20) -> DropOffRawMaterial
// Factory: 0x140022600, Vtable: 0x14015fd80
//
// Vtable (10 slots):
//   [0] +0x00 ScalarDtor    = 0x140022670
//   [1] +0x08 VectorDtor    = 0x1400225f0 (returns type ptr, unhookable)
//   [2] +0x10 BaseDtor      = 0x1400225e0 (returns TypeInfo, unhookable)
//   [3] +0x18 Serialize     = 0x1400c7b10
//   [4] +0x20 AbstractImpl  = 0x1400c7300 (complex resource transfer)
//   [5] +0x28 Execute       = 0x1400c4a90 (base Action, returns 0)
//   [6] +0x30 Enter         = 0x1400c4b90 (base Action, void nop)
//   [7] +0x38 Exit          = 0x1400c4ba0 (base Action, void nop)
//   [8] +0x40 CheckEntity   = 0x1400c72f0 (JMP thunk to 0x1400c7570, unhookable)
//   [9] +0x48 GetTarget     = 0x1400c7270
//
// Field layout (0x40 bytes):
//   +0x00-0x1F: Action base (vtable, Internal base, entity ptr, status byte)
//   +0x20: uint8_t  flag          (bool: if set, create raw material on drop failure)
//   +0x24: int32_t  count         (max count to drop, -1 = unlimited)
//   +0x28: void*    resourceType  (InternalBasePtr, ExternalPtr to resource description)
//   +0x30: void*    targetStorage (InternalBasePtr, storage entity to drop into)
//   +0x38: void*    sourceEntity  (InternalBasePtr, source entity carrying resources)
//
// Decompiled from Application-x64.exe using Ghidra.

#include "DropOffRawMaterial.h"
#include "object/TypeInfo.h"
#include "Component/StorageComponent.h"
#include "Action/exe_bodies.h"        // DropOffExec 0x1400c7620, SpillAllStorage 0x1400c7570
#include "Game/exe_bodies.h"          // Reimpl_ExternalPtr_Serialize_StringTable
#include "system/MemoryAllocator.h"   // System::MemoryAllocator::MemAlloc / MemFree
#include "io/Serializer.h"   // ENDGAME B.3.4 caller batch C
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <new>
#include "object/Internal.h"   // ENDGAME B.3.4 caller batch G
#include "Component/CitizenComponent.h"   // ENDGAME B.3.9 C4: CitizenComponent::TypeId()
#include "Entity/ComponentEntity.h"
#include "Component/ComponentDescription.h"   // B8 RT2: ::Object::SerializeExternal<ComponentDescription> (the downcast needs it complete)

// ============================================================
// Helpers, called by name (B3R)
//   0x140002340 Reimpl_ExternalPtr_Serialize_StringTable -- ExternalPtr serialize (resource
//               type at +0x28)
//   0x1400c7620 Pf_DropOffExec_7620 (DropOffRawMaterial_Serialize_sub_7620) -- the resource
//               transfer sub-helper (self, actionStack, storageIBP, resourceType)
//   0x1400c7570 Pf_SpillAllStorage_7570 (Entity_impl_7570) -- drop off ALL inventory items
//               (called when +0x38 is null; slot 8 is a JMP to it)
//
// ENDGAME B.3.9 CB: retail reads this record field ONLY in the class's own TypeName
// slot (0x1401c5f38 = the NAME field (+8) of DropOffRawMaterial's record, read by
// DropOffRawMaterial_Slot1 0x1400225f0); DECLARE_EXE_TYPE answers that slot, so the tree's copy of it is gone.
// ============================================================

// ============================================================
// Serialize (0x1400c7b10)
// Serializes: status(+0x18), storage(+0x30), source(+0x38), resource(+0x28), flag(+0x20), count(+0x24)
// ============================================================
void DropOffRawMaterial::Persist(IO::Stream& stream) {
    Action::Persist(stream);   // inlined in retail: Serializer(stream, 0), status +0x18

    // Serialize entity refs, resource type, flag, and count
    IO::Serializer ser(stream, 0);

    // The carrying citizen (+0x30)
    _citizen.InternalBasePtr::Serialize(ser);
    if (!_citizen.IsNull()) {
        if (!::Object::IsDerivedFromNullSafe(_citizen, CitizenComponent::TypeId())) {
            _citizen = nullptr;
        }
    }

    // The destination entity (+0x38)
    _destination.InternalBasePtr::Serialize(ser);
    if (!_destination.IsNull()) {
        if (!::Object::IsDerivedFromNullSafe(_destination, ComponentEntity::TypeId())) {
            _destination = nullptr;
        }
    }

    // Serialize resource type ExternalPtr (+0x28)
    ::Object::SerializeExternal<ComponentDescription>(ser, _material);   // 0x140002340

    // Serialize flag (+0x20) and count (+0x24)
    ser.Serialize(_searchOtherStorage, nullptr);
    ser.Serialize(_maxCount, nullptr);
}

// ============================================================
// AbstractImpl (0x1400c7300) — slot 4
// Complex resource transfer logic.
// If the destination (+0x38) is null: drops all inventory via SpillAllStorage (0x1400c7570).
// If resource type (+0x28) is specified: transfer that one type via DropOffExec (0x1400c7620).
// If resource type is null: enumerate every stack in the citizen's own storage, build an
//   array, transfer each via DropOffExec.
// Returns 1 (needs more work) or 2 (action pushed, e.g. MoveTo for full storage).
// ============================================================
int  DropOffRawMaterial::AbstractImpl(ActionStack* actionStack) {
    ActionStack* stack = actionStack;

    // If the destination is null, drop all inventory items
    if (_destination.Get() == nullptr) {
        SpillAllStorage();   // 0x1400c7570
        return 1;
    }

    // The destination's StorageComponent
    StorageComponent* storage = _destination->FindComponent<StorageComponent>();   // 0x140006270

    // If resource type (+0x28) is specified, transfer just that type
    if (_material != nullptr) {
        uint64_t result = Deposit(stack, &storage, _material);   // 0x1400c7620
        if ((int)result == 2) {
            return 2;
        }
        return 1;
    }

    // Resource type not specified: copy every stack of the carrying citizen's own storage,
    // then transfer each.
    StorageComponent* parentStorage = _citizen->_entity->FindComponent<StorageComponent>();   // 0x140006270

    // Build dynamic array of (resourceType, count) pairs from the citizen's storage
    StorageComponent::Item* itemArray = nullptr;
    uint32_t itemCount = 0;
    uint32_t itemCapacity = 0;

    int numItems = parentStorage->storageSlots._size;

    int srcIdx = 0;
    if (numItems > 0) {
        do {
            StorageComponent::Item* inventoryArray = parentStorage->storageSlots._data;

            uint32_t newCount = itemCount + 1;
            if (newCount != itemCount) {
                // Grow array if needed
                if ((int)itemCapacity < (int)newCount) {
                    itemCapacity = newCount;
                    if (itemCount != 0) {
                        itemCapacity = newCount * 3;
                    }
                    StorageComponent::Item* newArray = static_cast<StorageComponent::Item*>(System::MemoryAllocator::MemAlloc((int64_t)(int)itemCapacity * 16, 8));

                    // Copy existing and new entries
                    uint32_t copyStart = 0;
                    if (itemArray == newArray) {
                        copyStart = itemCount;
                    }
                    for (uint32_t i = copyStart; (int)i < (int)newCount; i++) {
                        if (newArray != nullptr) {
                            StorageComponent::Item* dst = newArray + (int64_t)(int)i;
                            const StorageComponent::Item* src;
                            if ((int)i < (int)itemCount) {
                                src = itemArray + (int64_t)(int)i;
                            } else {
                                // New entry: copy from inventory
                                src = inventoryArray + (int64_t)srcIdx;
                            }
                            *dst = *src;
                        }
                    }

                    if (itemArray != newArray) {
                        System::MemoryAllocator::MemFree(itemArray);
                    }
                    itemArray = newArray;
                } else {
                    // Array has capacity, copy new entry at the end
                    for (uint32_t i = itemCount; (int)i < (int)newCount; i++) {
                        if (itemArray != nullptr) {
                            itemArray[(int64_t)(int)i] = inventoryArray[(int64_t)srcIdx];
                        }
                    }
                }
                itemCount = newCount;
            }

            srcIdx++;
        } while (srcIdx < parentStorage->storageSlots._size);
    }

    // Transfer each resource type that has positive count
    bool anyPushed = false;
    if ((int)itemCount > 0) {
        uint64_t remaining = (uint64_t)itemCount;
        StorageComponent::Item* ptr = itemArray;
        do {
            if (ptr->_count > 0) {
                uint64_t result = Deposit(stack, &storage, ptr->_resource);   // 0x1400c7620
                anyPushed = anyPushed || ((int)result == 2);
            }
            ptr++;
            remaining--;
        } while (remaining != 0);

        if (anyPushed) {
            System::MemoryAllocator::MemFree(itemArray);
            return 2;
        }
    }

    System::MemoryAllocator::MemFree(itemArray);
    return 1;
}

// ============================================================
// CheckEntity (0x1400c72f0) — slot 8
// JMP thunk to 0x1400c7570 (drops off all inventory items).
// Only 5 bytes (JMP rel32), not hookable.
// ============================================================
void DropOffRawMaterial::CheckEntity() {
    SpillAllStorage();   // 0x1400c7570
}

// ============================================================
// GetTarget (0x1400c7270) — slot 9
// Returns true if param matches the destination at +0x38, or the carrying citizen's
// (+0x30) owning entity (Component +0x10).
// ============================================================
bool DropOffRawMaterial::GetTarget(ComponentEntity** param) {
    void* target = *param;

    if (target == _destination.Get()) {
        return true;
    }

    return _citizen->_entity.Get() == target;
}

// ============================================================
// class DropOffRawMaterial (DropOffRawMaterial.h) — ENDGAME B.3.8 ACT.
// The hand-built g_DropOffRawMaterial_Vtable[10], its per-class type-id cache and the
// Action_Alloc / Action_InitInPlace factories are gone: the compiler emits
// the table, DECLARE_EXE_TYPE answers slots 1/2.
// ============================================================
#include "Object/factory_registration.h"

// create 0x140022600 (inlined ctor). The tree's factory zeroed every field above
// +0x10; kept (not a behaviour change of this step).
DropOffRawMaterial::DropOffRawMaterial()
    : _searchOtherStorage(false),
      _pad21{},
      _maxCount(0),
      _material(nullptr),
      _citizen(nullptr),
      _destination(nullptr) {}

// [3] 0x1400c7b10
// [4] 0x1400c7300
// [8] 0x1400c72f0 (JMP 0x1400c7570)
// [9] 0x1400c7270

::Object::Base* DropOffRawMaterial::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(DropOffRawMaterial), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) DropOffRawMaterial());
}
::Object::Base* DropOffRawMaterial::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) DropOffRawMaterial());
}


// ============================================================
// FUN_1400c7cd0 — DropOffRawMaterial_Action_CopyCtor
//   RCX=self, RDX=p2, R8=p3, R9=p4(u64), [stack]=p5(u32), p6(byte).
//   +0x20=p6(byte) ; +0x24=p5(u32) ; +0x28=p4(u64) ; +0x30=*p2 ; +0x38=*p3.
// ============================================================
// 0x1400c7cd0: +0x20 = p6 (byte) ; +0x24 = p5 ; +0x28 = p4 ; +0x30 = *p2 ; +0x38 = *p3.
DropOffRawMaterial::DropOffRawMaterial(CitizenComponent* const* citizen, ComponentEntity* const* destination,
                                       ComponentDescription* material, uint32_t count, bool flag)
    : _searchOtherStorage(flag),
      _maxCount(static_cast<int32_t>(count)),
      _material(material),
      _citizen(*citizen),
      _destination(*destination) {}

DEFINE_EXE_TYPE(DropOffRawMaterial, "Action")
