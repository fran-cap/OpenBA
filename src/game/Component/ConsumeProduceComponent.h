#pragma once
// ============================================================================
// src/game/Component/ConsumeProduceComponent.h — ENDGAME B.3.8 block MI-B
//
// `ConsumeProduceComponent` (every consume -> produce workshop: tavern,
// blacksmith, tailor, ...) as a real C++ class with a compiler-generated vtable
// (B38_RULES.md D1-D6).
//
// RETAIL (B38_SLOTS.md §6, bytes read 2026-09-26):
//   reflected "ConsumeProduceComponent", parent "ResourceProduceComponent",
//   ti 0x1401c7350; create 0x140032300: MemAlloc(0x70, 8) + ctor
//   ctor 0x140119050: RP ctor 0x14012b3d0; stamp; +0x50 = -1; +0x58 = +0x60 = 0
//     (qwords); +0x68 = 0 (dword); `_flags |= 0x80` (BTS 7).
//   dtor 0x140118fd0: stamp; MemFree(+0x58); +0x58 = +0x60 = 0 (~Array of
//     consumeRates, 0x140118ff0..0x140119000); the RP/member/
//     ControllerComponent/Component restamps; ~Internal (tail JMP).
//   [0] 0x140032460: CALL the dtor, MemFree on &1.
//   table 0x140162a80, 22 slots: [3] 0x140118e00 Persist, [4] 0x140118ef0
//     Startup (read the recipe from the description), [18] 0x140118d00
//     CreateWork; every other slot = RP's.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "Component/ResourceProduceComponent.h"
#include "Object/exe_type.h"
#include "collection/Collection.h"   // Collection::Array (consumeRates)

class ConsumeProduceComponent final : public ResourceProduceComponent {
    DECLARE_EXE_TYPE(ConsumeProduceComponent)

public:
    ConsumeProduceComponent();                                           // 0x140119050

    // 0x140118fd0
    ~ConsumeProduceComponent() override;                                // [0]  0x140032460
    void        Persist(IO::Stream& stream) override;                         // [3]  0x140118e00
    void        Startup() override;                                     // [4]  0x140118ef0
    ::Object::InternalPtr<Work>* CreateWork(::Object::InternalPtr<Work>* out) override;                    // [18] 0x140118d00

    // ---- non-virtual members (retail addresses) ----
    // 0x140119470: the selected recipe, or a weighted random one when it is -1
    // (action_child_ctors2.cpp).
    uint64_t PickConsumeRecipe() const;
    // 0x1401193a0: consumeState below the ConsumeProduceDescription's +0x48 limit (a
    // negative limit is "always") (work_leaf_hunt.cpp).
    bool     CanProduce() const;

    int32_t  consumeResourceId; // +0x50: the selected recipe (-1 from the ctor)
    uint8_t  _pad54[4];         // +0x54
    // +0x58: one rate per recipe (count +0x60, capacity +0x64). float's trait-derived row
    // is exe 0x140023a00's: stride 4, align 4, the grown tail left as-found, no destroy.
    Collection::Array<float> consumeRates;
    int32_t  consumeState;      // +0x68
    uint8_t  _pad6C[4];         // +0x6c
};

static_assert(sizeof(ConsumeProduceComponent) == 0x70, "ConsumeProduceComponent is 0x70 (create 0x140032300)");
static_assert(offsetof(ConsumeProduceComponent, _pad54) == 0x54, "ConsumeProduceComponent::_pad54");
static_assert(offsetof(ConsumeProduceComponent, _pad6C) == 0x6C, "ConsumeProduceComponent::_pad6C");
static_assert(alignof(ConsumeProduceComponent) == 8, "ConsumeProduceComponent alignment");
static_assert(offsetof(ConsumeProduceComponent, consumeResourceId) == 0x50, "ConsumeProduceComponent +0x50");
static_assert(offsetof(ConsumeProduceComponent, consumeRates) == 0x58, "ConsumeProduceComponent +0x58");
static_assert(offsetof(ConsumeProduceComponent, consumeRates) + offsetof(Collection::Array<float>, _size) == 0x60, "ConsumeProduceComponent +0x60");
static_assert(offsetof(ConsumeProduceComponent, consumeRates) + offsetof(Collection::Array<float>, _capacity) == 0x64, "ConsumeProduceComponent +0x64");
static_assert(offsetof(ConsumeProduceComponent, consumeState) == 0x68, "ConsumeProduceComponent +0x68");
