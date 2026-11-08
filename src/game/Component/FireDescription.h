#pragma once
// ============================================================================
// src/game/Component/FireDescription.h — ENDGAME B.3.8 K1 (B38_RULES.md D1-D5, §2.2)
//
// RETAIL (Application-x64.exe, B38_SLOTS.md §6):
//   reflected "FireDescription", parent "Description", ti 0x1401c4218
//   create 0x140005640: MemAlloc(0x50, 8), `??0External@Object@@`, the
//   Description stamp, the leaf stamp, the field defaults. createInPlace 0x140005680.
//   table 0x140159400, 10 slots:
//     [0] 0x1400056c0 scalar deleting dtor   [1] 0x140005630 TypeName   [2] 0x140005620 Type
//     [3] 0x140049d10 GetFileVersion = 4   [4] External::Compile (import, inherited)
//     [5] 0x140049c00 Serialize (`MOV AL,1`)
//     [6] 0x140005690 Reconstruct   [7] 0x140001640 Destruct (shared)
//     [8] 0x1400056b0 GetComponentType: FireComponent's TypeId
//     [9] 0x140005680 Construct (folded with createInPlace)
//   sizeof 0x50.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "Component/Description.h"
#include "Object/exe_type.h"

class FireDescription final : public Description {
    DECLARE_EXE_TYPE(FireDescription)

public:
    FireDescription();

    ~FireDescription() override = default;                                 // [0] 0x1400056c0
    int32_t           GetFileVersion() const override { return 4; }    // [3] 0x140049d10
    bool              Serialize(IO::Stream& stream) override;                    // [5] 0x140049c00
    Object::External* Reconstruct() override;                              // [6] 0x140005690
    void              Destruct() override;                                 // [7] 0x140001640
    Object::Type*     GetComponentType(Object::Type* ret) const override;  // [8] 0x1400056b0
    virtual Object::External* Construct();                                 // [9] 0x140005680

    // ---- fields (PUBLIC: the components read them at raw offsets today) ----
    float burnBase;                                 // +0x20
    float burnScale;                                 // +0x24
    int32_t particleEmitter;                               // +0x28
    int32_t audioEmitter;                               // +0x2c
    float spreadInterval;                                 // +0x30
    float spreadChance;                                 // +0x34
    int32_t dangerRadius;                               // +0x38
    int32_t bucketsToOut;                               // +0x3c
    int32_t maxFirefighters;                               // +0x40
    float igniteChance[2];                      // +0x44 the {rate, weight} pair FUN_1400a9290 rolls
    int32_t _pad4c;                               // +0x4c zeroed, never serialized
};

static_assert(sizeof(FireDescription) == 0x50, "FireDescription is 0x50 (create 0x140005640 MemAlloc(0x50))");
static_assert(alignof(FireDescription) == 8, "FireDescription alignment");
static_assert(offsetof(FireDescription, burnBase) == 0x20, "FireDescription::burnBase @ +0x20");
static_assert(offsetof(FireDescription, burnScale) == 0x24, "FireDescription::burnScale @ +0x24");
static_assert(offsetof(FireDescription, particleEmitter) == 0x28, "FireDescription::particleEmitter @ +0x28");
static_assert(offsetof(FireDescription, audioEmitter) == 0x2c, "FireDescription::audioEmitter @ +0x2c");
static_assert(offsetof(FireDescription, spreadInterval) == 0x30, "FireDescription::spreadInterval @ +0x30");
static_assert(offsetof(FireDescription, spreadChance) == 0x34, "FireDescription::spreadChance @ +0x34");
static_assert(offsetof(FireDescription, dangerRadius) == 0x38, "FireDescription::dangerRadius @ +0x38");
static_assert(offsetof(FireDescription, bucketsToOut) == 0x3c, "FireDescription::bucketsToOut @ +0x3c");
static_assert(offsetof(FireDescription, maxFirefighters) == 0x40, "FireDescription::maxFirefighters @ +0x40");
static_assert(offsetof(FireDescription, igniteChance) == 0x44, "FireDescription::igniteChance @ +0x44");
static_assert(offsetof(FireDescription, _pad4c) == 0x4c, "FireDescription::_pad4c @ +0x4c");
