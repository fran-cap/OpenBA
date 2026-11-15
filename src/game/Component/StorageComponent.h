#pragma once
// ============================================================================
// src/game/Component/StorageComponent.h — ENDGAME B.3.8 block VIS
//
// `StorageComponent` as a real C++ class with a compiler-generated vtable
// (B38_RULES.md D1-D4).
//
// RETAIL (Application-x64.exe, B38_SLOTS.md §6):
//   reflected "StorageComponent", parent "Component", ti 0x1401c7e78
//   create 0x140038b50: MemAlloc(0x40, 8) + ctor 0x14012c8d0
//   ctor   0x14012c8d0: Component ctor, stamp 0x140164720, +0x28 = +0x30 = 0,
//                       flags |= 0x81
//   dtor   0x14012c870: MemFree(+0x28), +0x28 = +0x30 = 0, restamp, ~Internal
//   table  0x140164720, 16 slots:
//     [0] 0x140038d60 scalar deleting dtor   [1]/[2] TypeName/Type
//     [3] 0x14012c730 Persist                [4] 0x14012c720 own `RET`
//     [6] 0x14012c640 OnAdded (Ghidra "Draw": capacity + template slots)
//     every other slot = Component's
//
// B.4 W0-H2 fields (retail ctor 0x14012c8d0, dtor 0x14012c870, Persist
// 0x14012c730, OnAdded 0x14012c640; the profile assertions name the item
// fields `_items[index]._count` / `._reserve`):
//   +0x28 Collection::Array<Item> _items: data +0x28, count +0x30, capacity
//         +0x34 (SetSize 0x14000f090; 0x10-byte items {ExternalPtr resource,
//         int32 _count, int32 _reserve}; the retail dtor MemFrees the data).
//         A real Collection::Array over that row (kSpec_Pod16): its ~Array is
//         the dtor's MemFree + the +0x28 / +0x30 zero qwords (0x14012c890).
//   +0x38 int32  capacity: Serialize(int&); OnAdded = StorageDescription
//         _pickupValue (+0x24) times the footprint area. Not written by the
//         retail ctor (the tree's ctor zeroes it).
//   +0x3C uint32 rawMaterialFlags: serialized through the uint encoding slot
//         (+0x98); OnAdded and the version-0 load copy StorageDescription
//         _rawMaterialFlags (+0x28). Not written by the retail ctor.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "Component/Component.h"
#include "Object/exe_type.h"
#include "collection/Collection.h"   // Collection::Array (storageSlots)

class ComponentDescription;   // Component/ComponentDescription.h
// 0x14000f090's row (Array<{u64,u64}>: copy two qwords, zero the new tail, no element
// destroy); defined in storagecomponent.cpp, shared with CitizenBehavior +0x180.
namespace CitizenBehaviorRows { extern const rtcoll::ArraySpec kSpec_Pod16; }

class StorageComponent final : public Component {
    DECLARE_EXE_TYPE(StorageComponent)

public:
    // One `_items` element, 0x10 bytes (SetSize 0x14000f090 copies two qwords;
    // Persist 0x14012c730 serializes _count, _reserve, then the ExternalPtr
    // through ExternalPtr<ComponentDescription>::Serialize 0x140002340; the
    // profile build's assertions name `_count` / `._reserve`).
    struct Item {
        ComponentDescription* _resource; // +0x00 ExternalPtr<ComponentDescription>: the stored material's template
        int32_t               _count;    // +0x08 units stored
        int32_t               _reserve;  // +0x0C units promised to a pickup
    };

    StorageComponent();                                           // 0x14012c8d0

    // 0x14012c870: the body is empty; ~storageSlots frees the +0x28 item array and
    // zeroes +0x28 / +0x30 (0x14012c890..0x14012c8a0), then ~Component.
    ~StorageComponent() override = default;                       // [0] 0x140038d60
    void Persist(IO::Stream& stream) override;                          // [3] 0x14012c730
    void Startup() override;                                      // [4] 0x14012c720 own RET
    void OnAdded() override;                                      // [6] 0x14012c640

    int  QueryAvailable(const ComponentDescription* material) const;  // 0x14012ce70 count - reserve of the material's item, 0 if none
    int  QueryCount() const;                                      // 0x14012cf10 sum of count * RawMaterialDescription::_weight
    bool AcceptsMaterial(const ComponentDescription* material) const;  // 0x14012c560 the material's RawMaterialDescription _flags & rawMaterialFlags
    bool CanAdd(const ComponentDescription* material, int count);      // 0x14012cd80 accepts it and count * weight + QueryCount fits capacity
    void AddItem(ComponentDescription* material, int count);           // 0x14012d000 add to the material's item, else append {material, count, 0}
    void SubtractItem(const ComponentDescription* material, int count); // 0x14012d150 subtract; an item reaching 0 is removed

    // PUBLIC while the ported bodies read them (B38 decision 1).
    using ItemArray = Collection::Array<Item, Collection::DefaultAllocator,
                                        Collection::ExplicitSpec<&CitizenBehaviorRows::kSpec_Pod16>>;
    ItemArray storageSlots;     // +0x28 _items (count +0x30, capacity +0x34; freed by the dtor)
    int32_t  capacity;          // +0x38 total capacity (Serialize int&)
    uint32_t rawMaterialFlags;  // +0x3C StorageDescription::_rawMaterialFlags copy
};

static_assert(sizeof(StorageComponent) == 0x40, "StorageComponent is 0x40 (create 0x140038b50)");
static_assert(alignof(StorageComponent) == 8, "StorageComponent alignment");
static_assert(sizeof(StorageComponent::Item) == 0x10, "StorageComponent item stride 0x10 (0x14000f090)");
static_assert(offsetof(StorageComponent::Item, _resource) == 0x00, "item ExternalPtr +0x00 (Persist 0x14012c730)");
static_assert(offsetof(StorageComponent::Item, _count) == 0x08, "item _count +0x08 (Persist 0x14012c730)");
static_assert(offsetof(StorageComponent::Item, _reserve) == 0x0C, "item _reserve +0x0C (Persist 0x14012c730)");
static_assert(offsetof(StorageComponent, storageSlots) == 0x28, "");
static_assert(offsetof(StorageComponent, storageSlots) + offsetof(StorageComponent::ItemArray, _size) == 0x30, "");
static_assert(offsetof(StorageComponent, storageSlots) + offsetof(StorageComponent::ItemArray, _capacity) == 0x34, "StorageComponent _items capacity @ +0x34 (0x14000f090)");
static_assert(offsetof(StorageComponent, capacity) == 0x38, "StorageComponent::capacity @ +0x38 (Serialize int&)");
static_assert(offsetof(StorageComponent, rawMaterialFlags) == 0x3C, "StorageComponent::rawMaterialFlags @ +0x3C");
