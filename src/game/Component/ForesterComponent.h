#pragma once
// ============================================================================
// src/game/Component/ForesterComponent.h — ENDGAME B.3.8 block MI-B
//
// `ForesterComponent` (the forester lodge) as a real C++ class with a
// compiler-generated vtable (B38_RULES.md D1-D6).
//
// RETAIL (B38_SLOTS.md §6, bytes read 2026-09-26):
//   reflected "ForesterComponent", parent "ResourceProduceComponent", ti 0x1401c7740
//   create 0x140034870: MemAlloc(0x58, 8) + ctor
//   ctor 0x14011fa40: RP ctor 0x14012b3d0; stamp; `MOV word [RBX+0x50],0x101`
//     (plant ON, cut ON).
//   [0] 0x140034a70: restamps + ~Internal + MemFree on &1 (no body of its own).
//   table 0x140163460, 22 slots: [3] 0x14011f960 Persist, [13] 0x14011f920
//     CommandMask, [14] 0x14011f890 Command, [18] 0x14011f780 CreateWork
//     (Ghidra: "ForesterWork_CopyCtor"); every other slot = RP's.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "Component/ResourceProduceComponent.h"
#include "Object/exe_type.h"

class ForesterComponent final : public ResourceProduceComponent {
    DECLARE_EXE_TYPE(ForesterComponent)

public:
    ForesterComponent();                                                 // 0x14011fa40

    // 0x140034a70: no body
    ~ForesterComponent() override = default;                            // [0]  0x140034a70
    void        Persist(IO::Stream& stream) override;                         // [3]  0x14011f960
    uint32_t    CommandMask(int command) override;                      // [13] 0x14011f920
    void        Command(int command) override;                          // [14] 0x14011f890
    ::Object::InternalPtr<Work>* CreateWork(::Object::InternalPtr<Work>* out) override;                    // [18] 0x14011f780

    // Persist 0x14011f960: Serialize(bool&) +0x50, Serialize(bool&) +0x51; the ctor
    // stores the pair as one word 0x101 (both on).
    bool     plantTrees;        // +0x50 command 1
    bool     cutTrees;          // +0x51 command 2
    uint8_t  _pad52[6];         // +0x52
};

static_assert(sizeof(ForesterComponent) == 0x58, "ForesterComponent is 0x58 (create 0x140034870)");
static_assert(offsetof(ForesterComponent, _pad52) == 0x52, "ForesterComponent::_pad52");
static_assert(alignof(ForesterComponent) == 8, "ForesterComponent alignment");
static_assert(offsetof(ForesterComponent, plantTrees) == 0x50, "ForesterComponent +0x50");
static_assert(offsetof(ForesterComponent, cutTrees) == 0x51, "ForesterComponent +0x51");
