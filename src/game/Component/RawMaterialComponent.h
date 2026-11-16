#pragma once
// ============================================================================
// src/game/Component/RawMaterialComponent.h — ENDGAME B.3.8 block VIS
//
// `RawMaterialComponent` as a real C++ class with a compiler-generated vtable
// (B38_RULES.md D1-D4).
//
// RETAIL (Application-x64.exe, B38_SLOTS.md §6):
//   reflected "RawMaterialComponent", parent "Component", ti 0x1401c7c80
//   create 0x140037270: MemAlloc(0x40, 8) + ctor 0x140129400
//   ctor   0x140129400: Component ctor, stamp 0x140164170, +0x30 = +0x38 = 0
//                       (+0x28 NOT written), flags |= 0x89
//   dtor   0x1401293c0 (slot 0 0x1400373c0, 83 B: the plain restamp + ~Internal shape)
//   table  0x140164170, 16 slots:
//     [0] 0x1400373c0 scalar deleting dtor   [1]/[2] TypeName/Type
//     [3] 0x140129330 Persist                [4] 0x1401292c0 Startup (Ghidra "Serialize")
//     [6] 0x1401291d0 OnAdded (Ghidra "Draw", posts the Pickup work)
//     [9] 0x140129130 Update (expiry timer)
//     every other slot = Component's
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "Component/Component.h"
#include "Object/exe_type.h"
#include "system/Value.h"   // System::Time

class Profession;   // Game/Profession.h

class RawMaterialComponent final : public Component {
    DECLARE_EXE_TYPE(RawMaterialComponent)

public:
    RawMaterialComponent();                                       // 0x140129400

    // 0x1401293c0
    ~RawMaterialComponent() override = default;                   // [0] 0x1400373c0
    void Persist(IO::Stream& stream) override;                          // [3] 0x140129330
    void Startup() override;                                      // [4] 0x1401292c0
    void OnAdded() override;                                      // [6] 0x1401291d0
    void Update(const System::Time* time) override;                             // [9] 0x140129130

    // PUBLIC while the ported bodies read them (B38 decision 1).
    // Persist 0x140129330: Serialize(int&) +0x28, System::Time::Serialize +0x38, then
    // (version >= 2) ExternalPtr<Profession>::Serialize 0x140018020 (type cell 0x1401c5a98) +0x30.
    int32_t      quantity;    // +0x28 units in the pile (Startup: RawMaterialDescription::_lowCreateCount; 0x140105ac9 MOV EDI,[RAX+0x28])
    uint8_t      _pad2c[4];   // +0x2C never written
    Profession*  profession;  // +0x30 ExternalPtr<Profession> (ctor 0)
    System::Time age;         // +0x38 time on the ground: Update 0x140129130 adds the frame time and expires the pile past 3600 s (ctor 0)
};

static_assert(sizeof(RawMaterialComponent) == 0x40, "RawMaterialComponent is 0x40 (create 0x140037270)");
static_assert(offsetof(RawMaterialComponent, _pad2c) == 0x2C, "RawMaterialComponent::_pad2c");
static_assert(alignof(RawMaterialComponent) == 8, "RawMaterialComponent alignment");
static_assert(offsetof(RawMaterialComponent, quantity) == 0x28, "RawMaterialComponent::quantity @ +0x28 (Persist 0x140129364)");
static_assert(offsetof(RawMaterialComponent, profession) == 0x30, "RawMaterialComponent::profession @ +0x30 (Persist 0x140129395)");
static_assert(offsetof(RawMaterialComponent, age) == 0x38, "RawMaterialComponent::age @ +0x38 (Update 0x140129144)");
