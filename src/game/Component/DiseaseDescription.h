#pragma once
// ============================================================================
// src/game/Component/DiseaseDescription.h — ENDGAME B.3.8 K1 (B38_RULES.md D1-D5, §2.2)
//
// RETAIL (Application-x64.exe, B38_SLOTS.md §6):
//   reflected "DiseaseDescription", parent "Description", ti 0x1401c41a8
//   create 0x140004e10: MemAlloc(0x48, 8), `??0External@Object@@`, the
//   Description stamp, the leaf stamp, the field defaults. createInPlace 0x140004e70.
//   table 0x140159310, 10 slots:
//     [0] 0x140004f20 scalar deleting dtor   [1] 0x140004e00 TypeName   [2] 0x140004df0 Type
//     [3] 0x140048f20 GetFileVersion = 1   [4] External::Compile (import, inherited)
//     [5] 0x140048e40 Serialize (`MOV AL,1`)
//     [6] 0x140004ec0 Reconstruct   [7] 0x140001640 Destruct (shared)
//     [8] 0x140004f10 GetComponentType: DiseaseComponent's TypeId
//     [9] 0x140004e70 Construct (folded with createInPlace)
//   the ctor stores no defaults in retail; the tree zeroes +0x20..+0x47 (kept)
//   engine/types/DiseaseComponent.h's `DiseaseDescription` POD forwards here (B8-4)
//   sizeof 0x48.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "Component/Description.h"
#include "Object/exe_type.h"

class DiseaseDescription final : public Description {
    DECLARE_EXE_TYPE(DiseaseDescription)

public:
    DiseaseDescription();

    ~DiseaseDescription() override = default;                              // [0] 0x140004f20
    int32_t           GetFileVersion() const override { return 1; }    // [3] 0x140048f20
    bool              Serialize(IO::Stream& stream) override;                    // [5] 0x140048e40
    Object::External* Reconstruct() override;                              // [6] 0x140004ec0
    void              Destruct() override;                                 // [7] 0x140001640
    Object::Type*     GetComponentType(Object::Type* ret) const override;  // [8] 0x140004f10
    virtual Object::External* Construct();                                 // [9] 0x140004e70

    // ---- fields (PUBLIC: the components read them at raw offsets today) ----
    // The two rotation fields are named BACKWARDS in the authored .rsc, and the
    // authored values make the mistake convincing rather than obvious — swap them
    // and the mechanic still "works" in a trace, just inverted. The probe at
    // 0x14009fb60 returns TRUE when the cured crop is STILL planted, and retail
    // 0x140048610 reads +0x2c on TRUE, +0x28 on FALSE. So:
    //     +0x28 = _lowSpawnChance  = 200 years  <- crop CHANGED  (rotating is SAFER)
    //     +0x2c = _highSpawnChance = 150 years  <- crop UNCHANGED (repeating is RISKIER)
    // "low/high chance" refers to the chance, so the LOW-chance field carries the
    // LONGER interval. Both are mean YEARS, not probabilities.
    int32_t firstMonth;                         // +0x20 earliest month an outbreak may start
    int32_t lastMonth;                          // +0x24 latest month
    float yearsIfChanged;                       // +0x28 _lowSpawnChance: mean YEARS, crop CHANGED since cure
    float yearsIfSame;                          // +0x2c _highSpawnChance: mean YEARS, crop UNCHANGED
    float rollMonths;                           // +0x30 months per infection roll
    float killMonths;                           // +0x34 months per kill tick
    float spreadMonths;                         // +0x38 months per spread attempt
    float durationMonths;                       // +0x3c outbreak length
    int32_t spreadRadius;                       // +0x40 spread offset, tiles
    int32_t _pad44;                               // +0x44 zeroed by the tree's ctor, never serialized
};

static_assert(sizeof(DiseaseDescription) == 0x48, "DiseaseDescription is 0x48 (create 0x140004e10 MemAlloc(0x48))");
static_assert(alignof(DiseaseDescription) == 8, "DiseaseDescription alignment");
static_assert(offsetof(DiseaseDescription, firstMonth) == 0x20, "DiseaseDescription::firstMonth @ +0x20");
static_assert(offsetof(DiseaseDescription, lastMonth) == 0x24, "DiseaseDescription::lastMonth @ +0x24");
static_assert(offsetof(DiseaseDescription, yearsIfChanged) == 0x28, "DiseaseDescription::yearsIfChanged @ +0x28");
static_assert(offsetof(DiseaseDescription, yearsIfSame) == 0x2c, "DiseaseDescription::yearsIfSame @ +0x2c");
static_assert(offsetof(DiseaseDescription, rollMonths) == 0x30, "DiseaseDescription::rollMonths @ +0x30");
static_assert(offsetof(DiseaseDescription, killMonths) == 0x34, "DiseaseDescription::killMonths @ +0x34");
static_assert(offsetof(DiseaseDescription, spreadMonths) == 0x38, "DiseaseDescription::spreadMonths @ +0x38");
static_assert(offsetof(DiseaseDescription, durationMonths) == 0x3c, "DiseaseDescription::durationMonths @ +0x3c");
static_assert(offsetof(DiseaseDescription, spreadRadius) == 0x40, "DiseaseDescription::spreadRadius @ +0x40");
static_assert(offsetof(DiseaseDescription, _pad44) == 0x44, "DiseaseDescription::_pad44 @ +0x44");
