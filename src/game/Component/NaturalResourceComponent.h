#pragma once
// ============================================================================
// src/game/Component/NaturalResourceComponent.h — ENDGAME B.3.8 block CIT
//
// `NaturalResourceComponent` (trees, rocks, ore: harvestable resource state) as a
// real C++ class with a compiler-generated vtable (B38_RULES.md D1-D4).
//
// RETAIL (Application-x64.exe, B38_SLOTS.md §6):
//   reflected "NaturalResourceComponent", parent "Component", ti 0x1401c7b30
//   create 0x140036550: MemAlloc(0x30, 8), then the ctor INLINED: Component ctor,
//                       stamp 0x140163e10, +0x28 = 1.0f, +0x2c (word) = 0x100
//                       (+0x2c mature = 0, +0x2d harvestable = 1). The ctor
//                       adds NO flag bits (flags stay Component's 0x80).
//                       0x140125dc0 is the same body out of line.
//   dtor   0x140125d80: stamp 0x140163e10, stamp 0x140162920, ~Internal;
//                       scalar 0x1400366b0 (83 B) + MemFree on &1
//   table  0x140163e10, 16 slots:
//     [0] 0x1400366b0 scalar deleting dtor   [1]/[2] TypeName/Type
//     [3] 0x140125cf0 Persist                [8] 0x140125c40 Tick (163 B)
//     every other slot = Component's
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "Component/Component.h"
#include "Object/exe_type.h"

class NaturalResourceComponent final : public Component {
    DECLARE_EXE_TYPE(NaturalResourceComponent)

public:
    NaturalResourceComponent();                                   // 0x140125dc0

    // 0x140125d80, no own work
    ~NaturalResourceComponent() override = default;               // [0] 0x1400366b0
    void Persist(IO::Stream& stream) override;                          // [3] 0x140125cf0
    // The removal-animation tick: clears the tick bit once the mesh stops animating and
    // removes the resource when the dead bit is set (defined in naturalresourcecomponent.cpp;
    // INSTALLED 2026-10-09, user decision -- known_port_issues row 5).
    void Tick(const System::Time* time) override;                 // [8] 0x140125c40
    // Set the dead bit 0x200000, refresh the entity, animation index 2 (defined in
    // growthcomponent.cpp, B4 COMP-c).
    void ResetGrowthState();                                      // 0x140126780
    void RemoveResource(ComponentEntity* const* citizenIBP, char harvestFlag);   // 0x1401260f0
    void SetAnimation(uint32_t animIdx);                          // 0x140126440
    uint64_t PlaceAtCell(int cellIndex);                          // 0x140126540

    // PUBLIC while the ported bodies read them (B38 decision 1).
    float    resourceScale; // +0x28 (ctor 1.0f)
    bool     mature;        // +0x2C Serialize(bool&) (ctor 0: the word store 0x100)
    bool     harvestable;   // +0x2D Serialize(bool&) (ctor 1)
    uint8_t  _pad2E[2];     // +0x2E
};

static_assert(sizeof(NaturalResourceComponent) == 0x30, "NaturalResourceComponent is 0x30 (create 0x140036550)");
static_assert(offsetof(NaturalResourceComponent, _pad2E) == 0x2E, "NaturalResourceComponent::_pad2E");
static_assert(alignof(NaturalResourceComponent) == 8, "NaturalResourceComponent alignment");
static_assert(offsetof(NaturalResourceComponent, resourceScale) == 0x28, "");
static_assert(offsetof(NaturalResourceComponent, mature) == 0x2C, "");
static_assert(offsetof(NaturalResourceComponent, harvestable) == 0x2D, "");
