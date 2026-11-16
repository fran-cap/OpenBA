#pragma once
// ============================================================================
// src/game/Component/HerbalistComponent.h — ENDGAME B.3.8 block MI-B
//
// `HerbalistComponent` (the herbalist AND the gatherer's hut: GathererShelter.rsc
// carries HerbalistDescription) as a real C++ class with a compiler-generated
// vtable (B38_RULES.md D1-D6). No fields beyond ResourceProduceComponent.
//
// RETAIL (B38_SLOTS.md §6, bytes read 2026-09-26):
//   reflected "HerbalistComponent", parent "ResourceProduceComponent", ti 0x1401c43a0
//   create 0x140006b40: MemAlloc(0x50, 8) + ctor
//   ctor 0x14004b2a0: RP ctor 0x14012b3d0; stamp. Nothing else.
//   [0] 0x140006d60: restamps + ~Internal + MemFree on &1 (no body of its own).
//   table 0x140159860, 22 slots: [3] 0x14004b200 Persist, [18] 0x14004b160
//     CreateWork (Ghidra: "HerbalistWork_Ctor"); every other slot = RP's.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "Component/ResourceProduceComponent.h"
#include "Object/exe_type.h"

class HerbalistComponent final : public ResourceProduceComponent {
    DECLARE_EXE_TYPE(HerbalistComponent)

public:
    HerbalistComponent();                                                // 0x14004b2a0

    // 0x140006d60: no body
    ~HerbalistComponent() override = default;                           // [0]  0x140006d60
    void        Persist(IO::Stream& stream) override;                         // [3]  0x14004b200
    ::Object::InternalPtr<Work>* CreateWork(::Object::InternalPtr<Work>* out) override;                    // [18] 0x14004b160
};

static_assert(sizeof(HerbalistComponent) == 0x50, "HerbalistComponent is 0x50 (create 0x140006b40)");
static_assert(alignof(HerbalistComponent) == 8, "HerbalistComponent alignment");
