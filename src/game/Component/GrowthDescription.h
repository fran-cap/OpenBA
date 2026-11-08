#pragma once
// ============================================================================
// src/game/Component/GrowthDescription.h — ENDGAME B.3.8 K1 (B38_RULES.md D1-D5, §2.2)
//
// RETAIL (Application-x64.exe, B38_SLOTS.md §6):
//   reflected "GrowthDescription", parent "Description", ti 0x1401c77e8
//   create 0x140034b70: MemAlloc(0x68, 8), `??0External@Object@@`, the
//   Description stamp, the leaf stamp, the field defaults. createInPlace 0x140034bb0.
//   table 0x140163610, 10 slots:
//     [0] 0x140034bf0 scalar deleting dtor   [1] 0x140034b60 TypeName   [2] 0x140034b50 Type
//     [3] 0x140120b00 GetFileVersion = 3   [4] External::Compile (import, inherited)
//     [5] 0x140120980 Serialize (`MOV AL,1`)
//     [6] 0x140034bc0 Reconstruct   [7] 0x140001640 Destruct (shared)
//     [8] 0x140034be0 GetComponentType: GrowthComponent's TypeId
//     [9] 0x140034bb0 Construct (folded with createInPlace)
//   sizeof 0x68.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "Component/Description.h"
#include "Object/exe_type.h"

class GrowthDescription final : public Description {
    DECLARE_EXE_TYPE(GrowthDescription)

public:
    GrowthDescription();

    ~GrowthDescription() override = default;                               // [0] 0x140034bf0
    int32_t           GetFileVersion() const override { return 3; }    // [3] 0x140120b00
    bool              Serialize(IO::Stream& stream) override;                    // [5] 0x140120980
    Object::External* Reconstruct() override;                              // [6] 0x140034bc0
    void              Destruct() override;                                 // [7] 0x140001640
    Object::Type*     GetComponentType(Object::Type* ret) const override;  // [8] 0x140034be0
    virtual Object::External* Construct();                                 // [9] 0x140034bb0
    long long CheckFootprint(int col, int row) const;                      // 0x140120c30 the 3x3 window round (col,row): occupied cells <= +0x58, own-growth cells <= +0x5c

    // ---- fields (PUBLIC: the components read them at raw offsets today) ----
    float _maxGrowth;                                 // +0x20
    float _maxGrowthTolerance;                                 // +0x24 ctor stores int 0
    float _growthInMonths;                                 // +0x28
    bool _temperatureDependent;                                  // +0x2c
    uint8_t _pad2d[3];                          // +0x2d never written by the ctor
    float _growthTemp;                                 // +0x30
    float _growthTempRange;                                 // +0x34
    bool _soilDependent;                                  // +0x38
    uint8_t _pad39[3];                          // +0x39 never written by the ctor
    float _soilQualityChangeOnRemove;                                 // +0x3c
    float _soilQualityChangeOnDecay;                                 // +0x40
    bool _autoSeed;                                  // +0x44
    uint8_t _pad45[3];                          // +0x45 never written by the ctor
    int32_t _seedChance;                               // +0x48
    int32_t _seedDistance;                               // +0x4c
    int32_t _seedTimeMonths;                               // +0x50
    float _growthForSeeding;                                 // +0x54
    int32_t _maxNeighbors;                               // +0x58
    int32_t _maxNeighborsSelf;                               // +0x5c
    float _minScale;                                 // +0x60 ctor stores int 0
    bool _zScale;                                  // +0x64
    uint8_t _pad65[3];                          // +0x65 never written by the ctor
};

static_assert(sizeof(GrowthDescription) == 0x68, "GrowthDescription is 0x68 (create 0x140034b70 MemAlloc(0x68))");
static_assert(offsetof(GrowthDescription, _pad2d) == 0x2D, "GrowthDescription::_pad2d");
static_assert(offsetof(GrowthDescription, _pad39) == 0x39, "GrowthDescription::_pad39");
static_assert(offsetof(GrowthDescription, _pad45) == 0x45, "GrowthDescription::_pad45");
static_assert(offsetof(GrowthDescription, _pad65) == 0x65, "GrowthDescription::_pad65");
static_assert(alignof(GrowthDescription) == 8, "GrowthDescription alignment");
static_assert(offsetof(GrowthDescription, _maxGrowth) == 0x20, "GrowthDescription::_maxGrowth @ +0x20");
static_assert(offsetof(GrowthDescription, _maxGrowthTolerance) == 0x24, "GrowthDescription::_maxGrowthTolerance @ +0x24");
static_assert(offsetof(GrowthDescription, _growthInMonths) == 0x28, "GrowthDescription::_growthInMonths @ +0x28");
static_assert(offsetof(GrowthDescription, _temperatureDependent) == 0x2c, "GrowthDescription::_temperatureDependent @ +0x2c");
static_assert(offsetof(GrowthDescription, _growthTemp) == 0x30, "GrowthDescription::_growthTemp @ +0x30");
static_assert(offsetof(GrowthDescription, _growthTempRange) == 0x34, "GrowthDescription::_growthTempRange @ +0x34");
static_assert(offsetof(GrowthDescription, _soilDependent) == 0x38, "GrowthDescription::_soilDependent @ +0x38");
static_assert(offsetof(GrowthDescription, _soilQualityChangeOnRemove) == 0x3c, "GrowthDescription::_soilQualityChangeOnRemove @ +0x3c");
static_assert(offsetof(GrowthDescription, _soilQualityChangeOnDecay) == 0x40, "GrowthDescription::_soilQualityChangeOnDecay @ +0x40");
static_assert(offsetof(GrowthDescription, _autoSeed) == 0x44, "GrowthDescription::_autoSeed @ +0x44");
static_assert(offsetof(GrowthDescription, _seedChance) == 0x48, "GrowthDescription::_seedChance @ +0x48");
static_assert(offsetof(GrowthDescription, _seedDistance) == 0x4c, "GrowthDescription::_seedDistance @ +0x4c");
static_assert(offsetof(GrowthDescription, _seedTimeMonths) == 0x50, "GrowthDescription::_seedTimeMonths @ +0x50");
static_assert(offsetof(GrowthDescription, _growthForSeeding) == 0x54, "GrowthDescription::_growthForSeeding @ +0x54");
static_assert(offsetof(GrowthDescription, _maxNeighbors) == 0x58, "GrowthDescription::_maxNeighbors @ +0x58");
static_assert(offsetof(GrowthDescription, _maxNeighborsSelf) == 0x5c, "GrowthDescription::_maxNeighborsSelf @ +0x5c");
static_assert(offsetof(GrowthDescription, _minScale) == 0x60, "GrowthDescription::_minScale @ +0x60");
static_assert(offsetof(GrowthDescription, _zScale) == 0x64, "GrowthDescription::_zScale @ +0x64");
