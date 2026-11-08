#pragma once
// ============================================================================
// src/game/Action/PickupStoredRawMaterial.h — ENDGAME B.3.8 block ACT (B38_RULES.md D1-D5)
//
// `PickupStoredRawMaterial`, an exe Action leaf, as a real C++ class with a compiler vtable.
// RETAIL (Application-x64.exe, B38_SLOTS.md §6):
//   reflected "PickupStoredRawMaterial", parent "Action", ti 0x1401c6048
//   create 0x140022d60: MemAlloc(0x48, 8) + the inlined ctor
//   createInPlace = the IMPORTED `Internal::CreateInPlaceObject` (returns null);
//     the tree's in-place entry builds the object and that is KEPT (B8-13).
//   table 0x14015ff80, 10 slots; every slot not declared below is Action's (= parent)
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "Action/Action.h"
#include "Object/exe_type.h"
#include "object/Internal.h"   // Object::InternalPtr<T>

class CitizenComponent;       // Component/CitizenComponent.h
class ComponentDescription;   // Component/ComponentDescription.h
class ComponentEntity;        // Entity/ComponentEntity.h

class PickupStoredRawMaterial final : public Action {
    DECLARE_EXE_TYPE(PickupStoredRawMaterial)

public:
    // One element of the +0x30 list (stride 0x10; Persist 0x1400ca0d0 serializes the
    // ExternalPtr then the int; +0x0C is zeroed by the append).
    struct Entry {
        ComponentDescription* material;   // +0x00 ExternalPtr<ComponentDescription>
        int32_t               count;      // +0x08
        int32_t               reserved;   // +0x0C
    };
    // The Array<Entry> header {data, count, capacity} as a POD (no ctor, no dtor: the
    // class's ctor zeroes it and its dtor frees it, as retail's 0x140022dd0 does).
    struct EntryList {
        Entry*  data;       // +0x00
        int32_t count;      // +0x08
        int32_t capacity;   // +0x0C
    };

    PickupStoredRawMaterial();                                                   // create 0x140022d60
    PickupStoredRawMaterial(CitizenComponent* const* citizen, ComponentEntity* const* storageEntity,
                            ComponentDescription* material, int requested, bool flag);   // 0x1400ca520
    // 0x1400ca230: add {material, count} (or add to its entry) and reserve it in the storage.
    void AddMaterial(ComponentDescription* material, int count);
    // Retail 0x140022dd0: frees the +0x30 carrier list, then Action's dtor.
    ~PickupStoredRawMaterial() override;                       // [0] 0x140022dd0 (frees the +0x30 list)
    void Persist(IO::Stream& stream) override;                       // [3] 0x1400ca0d0
    int  AbstractImpl(ActionStack* actionStack) override;             // [4] 0x1400c9d40
    void CheckEntity() override;                               // [8] 0x1400c9c70
    bool GetTarget(ComponentEntity** param) override;                      // [9] 0x1400c9bf0

    // ---- fields, retail order (PUBLIC: the slot bodies are C functions) ----
    Object::InternalPtr<CitizenComponent> _citizen;   // +0x20 the carrying citizen (Persist checks CitizenComponent)
    Object::InternalPtr<ComponentEntity>  _storageEntity;   // +0x28 the storage entity (Persist checks ComponentEntity)
    // +0x30 Array<Entry> (H5: the raw triple as a POD; the dtor frees the data as retail's does)
    EntryList _materials;          // +0x30 list {data (stride 0x10), count +0x38, capacity +0x3C}
    bool      _allowPartial;          // +0x40 partial pickup allowed (Persist: the bool overload)
    uint8_t   _pad41[7];         // +0x41 padding
};

static_assert(sizeof(PickupStoredRawMaterial::Entry) == 0x10, "PickupStoredRawMaterial list stride 0x10");
static_assert(offsetof(PickupStoredRawMaterial::Entry, material) == 0x0, "Entry::material");
static_assert(offsetof(PickupStoredRawMaterial::Entry, count) == 0x08, "Entry +0x08");
static_assert(offsetof(PickupStoredRawMaterial::Entry, reserved) == 0x0C, "Entry +0x0C");
static_assert(offsetof(PickupStoredRawMaterial, _materials.capacity) == 0x3C, "PickupStoredRawMaterial +0x3C");
static_assert(sizeof(PickupStoredRawMaterial::EntryList) == 0x10, "Array<Entry> header");
static_assert(offsetof(PickupStoredRawMaterial::EntryList, data) == 0x0, "EntryList::data");
static_assert(offsetof(PickupStoredRawMaterial::EntryList, count) == 0x8, "EntryList::count");
static_assert(offsetof(PickupStoredRawMaterial::EntryList, capacity) == 0xC, "EntryList::capacity");

static_assert(sizeof(PickupStoredRawMaterial) == 0x48, "PickupStoredRawMaterial is 0x48 (create 0x140022d60 MemAlloc)");
static_assert(offsetof(PickupStoredRawMaterial, _pad41) == 0x41, "PickupStoredRawMaterial::_pad41");
static_assert(alignof(PickupStoredRawMaterial) == 8, "PickupStoredRawMaterial is 8-aligned");
static_assert(offsetof(PickupStoredRawMaterial, _citizen) == 0x20, "PickupStoredRawMaterial +0x20");
static_assert(offsetof(PickupStoredRawMaterial, _storageEntity) == 0x28, "PickupStoredRawMaterial +0x28");
static_assert(offsetof(PickupStoredRawMaterial, _materials) == 0x30, "PickupStoredRawMaterial +0x30");
static_assert(offsetof(PickupStoredRawMaterial, _materials.count) == 0x38, "PickupStoredRawMaterial +0x38");
static_assert(offsetof(PickupStoredRawMaterial, _allowPartial) == 0x40, "PickupStoredRawMaterial +0x40");
