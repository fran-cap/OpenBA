#pragma once
// ============================================================================
// src/game/Component/DecalComponent.h — ENDGAME B.3.8 block VIS
//
// `DecalComponent` as a real C++ class with a compiler-generated vtable
// (B38_RULES.md D1-D4). The engine/types POD is a forwarder to this file.
//
// RETAIL (Application-x64.exe, B38_SLOTS.md §6):
//   reflected "DecalComponent", parent "Component", ti 0x1401c7580
//   create 0x1400337c0: MemAlloc(0x38, 8) + ctor 0x14011b8f0
//   ctor   0x14011b8f0: Component ctor, stamp 0x140162fd0, +0x28 = 0,
//                       +0x2c = -1, +0x30 = 1.0f, flags |= 0xc3
//   dtor   0x14011b8b0 (slot 0 0x1400338d0, 83 B: the plain restamp + ~Internal shape)
//   table  0x140162fd0, 16 slots:
//     [0]  0x1400338d0 scalar deleting dtor  [1]/[2] TypeName/Type
//     [3]  0x14011b820 Persist               [4] 0x14011b7d0 Startup
//     [6]  0x14011b560 OnAdded (Ghidra "Draw", AddDecal)
//     [7]  0x14011b510 OnRemove (Ghidra "OnCreate", release the decal slot)
//     [11] 0x14011b810 Slot11 (7 B, = OnAdded on load) [12] 0x14011b760 OnEvent (events 1, 5)
//     every other slot = Component's (row 1202: [4] is retail's body and [8] is
//     Component's again since Phase D SLOTS)
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "Component/Component.h"
#include "Object/exe_type.h"
namespace Graphics { class MaterialInstance; }

class DecalComponent final : public Component {
    DECLARE_EXE_TYPE(DecalComponent)

public:
    DecalComponent();                                             // 0x14011b8f0

    // 0x14011b8b0
    ~DecalComponent() override = default;                         // [0]  0x1400338d0
    void Persist(IO::Stream& stream) override;                          // [3]  0x14011b820
    void Startup() override;                                      // [4]  0x14011b7d0
    void OnAdded() override;                                      // [6]  0x14011b560
    void OnRemove() override;                                     // [7]  0x14011b510
    void Slot11() override;                                       // [11] 0x14011b810 tail OnAdded (PostLoad)
    void OnEvent(int eventId) override;                           // [12] 0x14011b760 1: opaque ramp, 5: rebuild

    // PUBLIC while the ported bodies read them (B38 decision 1).
    int32_t  decalState;   // +0x28 (ctor 0); signed: OnAdded indexes with MOVSXD (0x14011b683)
    int32_t  decalId;      // +0x2C terrain decal slot (ctor -1 = none)
    float    decalScale;   // +0x30 (ctor 1.0f)
    uint8_t  _pad34[4];    // +0x34
};

static_assert(sizeof(DecalComponent) == 0x38, "DecalComponent is 0x38 (create 0x1400337c0)");
static_assert(offsetof(DecalComponent, _pad34) == 0x34, "DecalComponent::_pad34");
static_assert(alignof(DecalComponent) == 8, "DecalComponent alignment");
static_assert(offsetof(DecalComponent, decalState) == 0x28, "");
static_assert(offsetof(DecalComponent, decalId) == 0x2C, "");
static_assert(offsetof(DecalComponent, decalScale) == 0x30, "");

// The material DecalComponent::OnAdded (0x14011b560) hands AddDecal: the description's
// `_materials[decalState]` (0x14011b683 MOVSXD RDI,[RBP+0x28]; 0x14011b6f4 MOV RAX,[RBX+RDI*8]).
inline Graphics::MaterialInstance* DecalStateMaterial(Graphics::MaterialInstance* const* materials,
                                                      int32_t materialCount, int32_t decalState) {
    // NOT RETAIL (review F-108): retail reads the array with no bound. decalState comes from
    // the save (Persist 0x14011b820) or a DestroyDescription's damaged decal, and one outside
    // [0, _materialCount) reads past the array (undefined behaviour) and hands AddDecal a wild
    // MaterialInstance*. Such a state names no material: null, which AddDecal turns into no
    // decal (its own null test, 0x1400b0dbb). Every state valid data holds is in range.
    if (decalState < 0 || decalState >= materialCount)
        return nullptr;
    return materials[decalState];
}

// The alpha byte DecalComponent::OnAdded hands AddDecal: decalScale * 255
// (0x14011b696 MULSS [0x14017222c]), its low byte (0x14011b70e MOV [RSP+0x28],R10B).
// The conversion is to int64 as retail's (0x14011b69e CVTTSS2SI R10,XMM0), not int.
constexpr uint8_t DecalAlphaByte(float decalScale) {
    const float product = decalScale * 255.0f;
    // NOT RETAIL (review F-163): retail has no range test. A NaN or a product outside int64's
    // range (a saved decalScale) is undefined in a C++ conversion; CVTTSS2SI gives the
    // indefinite integer 0x8000000000000000 there, whose low byte is 0, so this returns 0.
    if (!(product > -9223372036854775808.0f && product < 9223372036854775808.0f))
        return 0;
    return static_cast<uint8_t>(static_cast<int64_t>(product));
}
