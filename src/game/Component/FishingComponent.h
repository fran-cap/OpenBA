#pragma once
// ============================================================================
// src/game/Component/FishingComponent.h — ENDGAME B.3.8 block MI-B
//
// `FishingComponent` (the fishing dock) as a real C++ class with a
// compiler-generated vtable (B38_RULES.md D1-D6). The shared RP controller
// member is ResourceProduceComponent's.
//
// RETAIL (B38_SLOTS.md §6, bytes read 2026-09-26):
//   reflected "FishingComponent", parent "ResourceProduceComponent", ti 0x1401c7660
//   create 0x140034350: MemAlloc(0x58, 8) + the inlined ctor 0x14011f130
//   ctor 0x14011f130: RP ctor 0x14012b3d0; stamp; `MOV byte [RBX+0x50],0`.
//     +0x54 is NOT written by retail (the tree's factory zeroed it; kept 0).
//   [0] 0x140034560: restamps + ~Internal + MemFree on &1 (no body of its own).
//   table 0x140163230, 22 slots: [3] 0x14011f050 Persist, [18] 0x14011efe0
//     CreateWork, [19] 0x14011ee80 OnEnterProducing, [20] 0x14011ee50
//     OnExitProducing, [21] 0x14011ec10 ProduceTick; every other slot = RP's.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "Component/ResourceProduceComponent.h"
#include "Object/exe_type.h"

class FishingComponent final : public ResourceProduceComponent {
    DECLARE_EXE_TYPE(FishingComponent)

public:
    FishingComponent();                                                  // 0x14011f130

    // 0x140034560: no body
    ~FishingComponent() override = default;                             // [0]  0x140034560
    void        Persist(IO::Stream& stream) override;                         // [3]  0x14011f050
    ::Object::InternalPtr<Work>* CreateWork(::Object::InternalPtr<Work>* out) override;                    // [18] 0x14011efe0
    char        OnEnterProducing() override;                            // [19] 0x14011ee80
    char        OnExitProducing() override;                             // [20] 0x14011ee50
    char        ProduceTick(const System::Time* time) override;                      // [21] 0x14011ec10

    bool     fishingFlag;       // +0x50: the dock's coverage-registered latch (Persist 0x14011f050: Serialize(bool&))
    uint8_t  _pad51[3];         // +0x51
    float    fishingRate;       // +0x54: the per-tick production rate (serialized)
};

static_assert(sizeof(FishingComponent) == 0x58, "FishingComponent is 0x58 (create 0x140034350)");
static_assert(offsetof(FishingComponent, _pad51) == 0x51, "FishingComponent::_pad51");
static_assert(alignof(FishingComponent) == 8, "FishingComponent alignment");
static_assert(offsetof(FishingComponent, fishingFlag) == 0x50, "FishingComponent +0x50");
static_assert(offsetof(FishingComponent, fishingRate) == 0x54, "FishingComponent +0x54");
