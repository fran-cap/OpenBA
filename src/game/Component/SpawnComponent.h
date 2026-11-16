#pragma once
// ============================================================================
// src/game/Component/SpawnComponent.h — ENDGAME B.3.8 block CIT
//
// `SpawnComponent` (per-entry spawn timers for wild food / trees) as a real C++
// class with a compiler-generated vtable (B38_RULES.md D1-D4).
//
// RETAIL (Application-x64.exe, B38_SLOTS.md §6):
//   reflected "SpawnComponent", parent "Component", ti 0x1401c48e0
//   create 0x14000c170: MemAlloc(0x38, 8) + ctor 0x140056030
//   ctor   0x140056030: Component ctor, stamp 0x14015a670, +0x28 = +0x30 = 0,
//                       flags |= 0x88
//   dtor   0x14000c350 (scalar): restamp, MemFree(+0x28), +0x28 = +0x30 = 0
//                       (the timer array), stamp 0x140162920, ~Internal,
//                       MemFree on &1
//   table  0x14015a670, 16 slots:
//     [0] 0x14000c350 scalar deleting dtor   [1]/[2] TypeName/Type
//     [3] 0x140055ee0 Persist                [4] 0x140055e90 Startup
//     [9] 0x140055ad0 Update                 every other slot = Component's
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "Component/Component.h"
#include "Object/exe_type.h"
#include "system/Value.h"   // System::Time
#include "collection/Collection.h"   // Collection::Array (timers)

class SpawnDescription;   // Component/SpawnDescription.h

// The timer array grows through retail's stride-8 row (0x140001bb0 SetSize(n): stride 8,
// align 8, the new tail ZEROED, no element destroy) and is freed by the inlined ~Array
// (dtor 0x14000c377: MemFree(+0x28), then +0x28 = +0x30 = 0): kSpec_PtrZero (Entity/EntityList.h).
namespace rtcoll { extern const ArraySpec kSpec_PtrZero; }   // 0x180008d60

class SpawnComponent final : public Component {
    DECLARE_EXE_TYPE(SpawnComponent)

public:
    SpawnComponent();                                             // 0x140056030

    // 0x14000c350: the body is empty; ~timers frees the timer array (+0x28) after it.
    ~SpawnComponent() override;                                   // [0] 0x14000c350
    void Persist(IO::Stream& stream) override;                          // [3] 0x140055ee0
    void Startup() override;                                      // [4] 0x140055e90
    void Update(const System::Time* time) override;                             // [9] 0x140055ad0

    // 0x140055cc0: the worldgen seed (one DoSpawn per entry in age, repeated by age).
    void InitialSpawn();
    // 0x140055780: roll one spawn of `desc->_entries[entryIndex]` near the owner;
    // `randomGrowth` sets the new entity's growth to a random value.
    void DoSpawn(SpawnDescription* desc, int entryIndex, char randomGrowth);

    // PUBLIC while the ported bodies read them (B38 decision 1). Persist 0x140055ee0.
    using TimerList = Collection::Array<System::Time, Collection::DefaultAllocator,
                                        Collection::ExplicitSpec<&rtcoll::kSpec_PtrZero>>;
    TimerList timers;      // +0x28 one Time per spawn entry (count +0x30, capacity +0x34)
};

static_assert(sizeof(SpawnComponent) == 0x38, "SpawnComponent is 0x38 (create 0x14000c170)");
static_assert(alignof(SpawnComponent) == 8, "SpawnComponent alignment");
static_assert(offsetof(SpawnComponent, timers) == 0x28, "");
static_assert(offsetof(SpawnComponent, timers) + offsetof(SpawnComponent::TimerList, _size) == 0x30, "SpawnComponent +0x30 timer count");
static_assert(offsetof(SpawnComponent, timers) + offsetof(SpawnComponent::TimerList, _capacity) == 0x34, "SpawnComponent +0x34 timer capacity");
