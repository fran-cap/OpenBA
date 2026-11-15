#pragma once
// ============================================================================
// src/game/Component/MarketComponent.h — ENDGAME B.3.8 block MI-B
//
// `MarketComponent` (the market's produce side; the carrier routes live on
// MarketLocationComponent) as a real C++ class with a compiler-generated vtable
// (B38_RULES.md D1-D6). No fields beyond ResourceProduceComponent.
//
// RETAIL (B38_SLOTS.md §6, bytes read 2026-09-26):
//   reflected "MarketComponent", parent "ResourceProduceComponent", ti 0x1401c44f0
//   create 0x140008220: MemAlloc(0x50, 8) + ctor
//   ctor 0x14004eb70: RP ctor 0x14012b3d0; stamp. Nothing else.
//   [0] 0x140008440: restamps + ~Internal + MemFree on &1 (no body of its own).
//   table 0x140159c40, 22 slots: [3] 0x14004ead0 Persist, [18] 0x14004ea40
//     CreateWork (Ghidra: "MarketWork_Ctor"); every other slot = RP's.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "Component/ResourceProduceComponent.h"
#include "Object/exe_type.h"

class MarketComponent final : public ResourceProduceComponent {
    DECLARE_EXE_TYPE(MarketComponent)

public:
    MarketComponent();                                                   // 0x14004eb70

    // 0x140008440: no body
    ~MarketComponent() override = default;                              // [0]  0x140008440
    void        Persist(IO::Stream& stream) override;                         // [3]  0x14004ead0
    ::Object::InternalPtr<Work>* CreateWork(::Object::InternalPtr<Work>* out) override;                    // [18] 0x14004ea40
};

static_assert(sizeof(MarketComponent) == 0x50, "MarketComponent is 0x50 (create 0x140008220)");
static_assert(alignof(MarketComponent) == 8, "MarketComponent alignment");
