#pragma once
// ============================================================================
// src/game/Component/HerdComponent.h — ENDGAME B.3.8 block CIT
//
// `HerdComponent` (a wild-animal herd: grazing, migration, size) as a real C++
// class with a compiler-generated vtable (B38_RULES.md D1-D4).
//
// RETAIL (Application-x64.exe, B38_SLOTS.md §6):
//   reflected "HerdComponent", parent "Component", ti 0x1401c7820
//   create 0x140034cd0: MemAlloc(0x60, 8) + ctor 0x140122150
//   ctor   0x140122150: Component ctor, stamp 0x140163680, +0x28 = 0, +0x38 =
//                       +0x40 = 0, +0x48 (byte) = 0, +0x4c = -1, +0x50 = +0x58
//                       = 0, flags |= 0x0b. +0x30 is NOT written.
//   dtor   0x140034ea0 (scalar): restamp, MemFree(+0x38), +0x38 = +0x40 = 0
//                       (~members, the inlined ~Array), stamp 0x140162920, ~Internal,
//                       MemFree on &1
//   table  0x140163680, 16 slots:
//     [0] 0x140034ea0 scalar deleting dtor   [1]/[2] TypeName/Type
//     [3] 0x140121f90 Persist                [6] 0x140121f40 OnAdded
//     [7] 0x140121e20 OnRemove               [9] 0x140121bf0 Update
//     every other slot = Component's (slot 4 is the base `RET`)
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "Component/Component.h"
#include "system/Value.h"   // System::Time
#include "Object/exe_type.h"
#include "collection/Collection.h"   // Collection::Array (members)
#include "object/Internal.h"         // Object::InternalPtr<T>

// The member list grows through retail's stride-8 rows (0x1400032e0 / 0x140003110:
// stride 8, align 8, the new tail ZEROED, no element destroy) and is freed by the
// inlined ~Array (dtor 0x140034ec7..0x140034ed7): kSpec_PtrZero (Entity/EntityList.h).
namespace rtcoll { extern const ArraySpec kSpec_PtrZero; }   // 0x180008d60

class PopulationComponent;
class WildAnimalComponent;

// Field types (B.4 COMP-d) from Persist 0x140121f90 (int count +0x40 with the
// load-side resize of the +0x38 IBP array, each validated against
// WildAnimalComponent 0x1401c8000; bool +0x48; Time +0x50 / +0x58; int +0x4c;
// IBP +0x28 validated against PopulationComponent 0x1401c4790; int +0x30) and
// Update 0x140121bf0 (+0x30 is handed to PopulationComponent 0x140053f20 as the
// grid cell; +0x4c is a map tile, signed `/` and `%` by the map width).
class HerdComponent final : public Component {
    DECLARE_EXE_TYPE(HerdComponent)

public:
    HerdComponent();                                              // 0x140122150

    // 0x140034ea0: the body is empty; ~members frees and zeroes +0x38 / +0x40 (the
    // InternalPtr at +0x28 releases nothing).
    ~HerdComponent() override = default;                          // [0] 0x140034ea0
    void Persist(IO::Stream& stream) override;                          // [3] 0x140121f90
    // [4] inherited: Component's `RET` 0x140118a50 (the ctor writes +0x4c = -1).
    void OnAdded() override;                                      // [6] 0x140121f40
    void OnRemove() override;                                     // [7] 0x140121e20
    void Update(const System::Time* time) override;                             // [9] 0x140121bf0

    // ---- non-virtual members (retail addresses) ----
    char CheckMigrationDone();                                    // 0x140121980
    void AdjustSize(int targetSize);                              // 0x140121450
    void AddMember(WildAnimalComponent* const* member);           // 0x140121ad0
    void Populate(int count);                                     // 0x140120f50
    void StartMigration();                                        // 0x140122340

    // PUBLIC while the ported bodies read them (B38 decision 1).
    Object::InternalPtr<PopulationComponent> population;   // +0x28 InternalPtr<PopulationComponent>
    int32_t               populationCell; // +0x30 population-map cell; the ctor zeroes it (retail's leaves it)
    uint8_t               _pad34[4];      // +0x34
    using MemberList = Collection::Array<Object::InternalPtr<WildAnimalComponent>, Collection::DefaultAllocator,
                                         Collection::ExplicitSpec<&rtcoll::kSpec_PtrZero>>;
    MemberList            members;        // +0x38 the herd's animals (count +0x40, capacity +0x44)
    bool                  migrating;      // +0x48 phase (false grazing, true migrating)
    uint8_t               _pad49[3];      // +0x49
    int32_t               tile;           // +0x4C herd centre tile (ctor -1)
    System::Time          grazeTimer;     // +0x50 counts down while grazing
    System::Time          spawnTimer;     // +0x58 accumulates to the description's interval
};

static_assert(sizeof(HerdComponent) == 0x60, "HerdComponent is 0x60 (create 0x140034cd0)");
static_assert(offsetof(HerdComponent, _pad49) == 0x49, "HerdComponent::_pad49");
static_assert(alignof(HerdComponent) == 8, "HerdComponent alignment");
static_assert(offsetof(HerdComponent, population) == 0x28, "");
static_assert(offsetof(HerdComponent, populationCell) == 0x30, "");
static_assert(offsetof(HerdComponent, _pad34) == 0x34, "");
static_assert(offsetof(HerdComponent, members) == 0x38, "");
static_assert(offsetof(HerdComponent, members) + offsetof(HerdComponent::MemberList, _size) == 0x40, "");
static_assert(offsetof(HerdComponent, members) + offsetof(HerdComponent::MemberList, _capacity) == 0x44, "");
static_assert(offsetof(HerdComponent, migrating) == 0x48, "");
static_assert(offsetof(HerdComponent, tile) == 0x4C, "");
static_assert(offsetof(HerdComponent, grazeTimer) == 0x50, "");
static_assert(offsetof(HerdComponent, spawnTimer) == 0x58, "");
