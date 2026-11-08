#pragma once
// ============================================================================
// src/game/Component/NaturalResourceDescription.h — ENDGAME B.3.8 K1 (B38_RULES.md D1-D5, §2.2)
//
// RETAIL (Application-x64.exe, B38_SLOTS.md §6):
//   reflected "NaturalResourceDescription", parent "Description", ti 0x1401c7b68
//   create 0x1400365d0: MemAlloc(0x88, 8), `??0External@Object@@`, the
//   Description stamp, the leaf stamp, the field defaults. createInPlace 0x140036610.
//   table 0x140163e90, 10 slots:
//     [0] 0x140036650 scalar deleting dtor   [1] 0x1400365c0 TypeName   [2] 0x1400365b0 Type
//     [3] 0x140125fc0 GetFileVersion = 4   [4] External::Compile (import, inherited)
//     [5] 0x140125e10 Serialize (`MOV AL,1`)
//     [6] 0x140036620 Reconstruct   [7] 0x140001640 Destruct (shared)
//     [8] 0x140036640 GetComponentType: NaturalResourceComponent's TypeId
//     [9] 0x140036610 Construct (folded with createInPlace)
//   slot 0: the tree's hand-built slot 0 never ran ~External; the compiler chain now does (B38 D2, behaviour)
//   sizeof 0x88.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "Component/Description.h"
#include "Object/exe_type.h"
#include "system/Value.h"
class Profession;   // Game/Profession.h

class ComponentDescription;   // Component/ComponentDescription.h
class NaturalResourceDescription final : public Description {
    DECLARE_EXE_TYPE(NaturalResourceDescription)

public:
    NaturalResourceDescription();

    ~NaturalResourceDescription() override = default;                      // [0] 0x140036650
    int32_t           GetFileVersion() const override { return 4; }    // [3] 0x140125fc0
    bool              Serialize(IO::Stream& stream) override;                    // [5] 0x140125e10
    Object::External* Reconstruct() override;                              // [6] 0x140036620
    void              Destruct() override;                                 // [7] 0x140001640
    Object::Type*     GetComponentType(Object::Type* ret) const override;  // [8] 0x140036640
    virtual Object::External* Construct();                                 // [9] 0x140036610

    // ---- fields (PUBLIC: the components read them at raw offsets today) ----
    ComponentDescription* _building;            // +0x20 ExternalPtr<ComponentDescription> (0x140002340 serialize: IsDerivedFrom 0x1401c7318)
    float _noisePersistance;                                 // +0x28
    float _noiseOctaves;                                 // +0x2c
    float _noiseScale;                                 // +0x30
    float _noiseCutoff;                                 // +0x34
    float _spawnChance;                                 // +0x38
    float _minHeight;                                 // +0x3c
    float _maxHeight;                                 // +0x40
    float _maxAngle;                                 // +0x44 0.95
    bool _groupModels;                                  // +0x48
    uint8_t _pad49[3];                          // +0x49 never written by the ctor
    float _groupPersistance;                                 // +0x4c
    float _groupOctaves;                                 // +0x50
    float _groupScale;                                 // +0x54
    float _groupCutOff;                                 // +0x58
    int32_t _groupRandom;                               // +0x5c
    float _positionTolerance;                                 // +0x60 high dword of the ctor's 8-byte +0x5c store
    bool _animated;                                  // +0x64
    uint8_t _pad65[3];                          // +0x65 never written by the ctor
    System::Time _preAnimateTime;                       // +0x68 System::Time(1.0f)
    System::Time _postAnimateTime;                       // +0x70 System::Time(1.0f)
    Profession* _profession;                    // +0x78 ExternalPtr<Profession> (0x140018020 serialize; typed B8 RT2)
    uint32_t _toolType;                              // +0x80 enum, default 11
    uint8_t _pad84[4];                          // +0x84 never written by the ctor
};

static_assert(sizeof(NaturalResourceDescription) == 0x88, "NaturalResourceDescription is 0x88 (create 0x1400365d0 MemAlloc(0x88))");
static_assert(offsetof(NaturalResourceDescription, _pad49) == 0x49, "NaturalResourceDescription::_pad49");
static_assert(offsetof(NaturalResourceDescription, _pad65) == 0x65, "NaturalResourceDescription::_pad65");
static_assert(offsetof(NaturalResourceDescription, _pad84) == 0x84, "NaturalResourceDescription::_pad84");
static_assert(alignof(NaturalResourceDescription) == 8, "NaturalResourceDescription alignment");
static_assert(offsetof(NaturalResourceDescription, _building) == 0x20, "NaturalResourceDescription::_building @ +0x20");
static_assert(offsetof(NaturalResourceDescription, _noisePersistance) == 0x28, "NaturalResourceDescription::_noisePersistance @ +0x28");
static_assert(offsetof(NaturalResourceDescription, _noiseOctaves) == 0x2c, "NaturalResourceDescription::_noiseOctaves @ +0x2c");
static_assert(offsetof(NaturalResourceDescription, _noiseScale) == 0x30, "NaturalResourceDescription::_noiseScale @ +0x30");
static_assert(offsetof(NaturalResourceDescription, _noiseCutoff) == 0x34, "NaturalResourceDescription::_noiseCutoff @ +0x34");
static_assert(offsetof(NaturalResourceDescription, _spawnChance) == 0x38, "NaturalResourceDescription::_spawnChance @ +0x38");
static_assert(offsetof(NaturalResourceDescription, _minHeight) == 0x3c, "NaturalResourceDescription::_minHeight @ +0x3c");
static_assert(offsetof(NaturalResourceDescription, _maxHeight) == 0x40, "NaturalResourceDescription::_maxHeight @ +0x40");
static_assert(offsetof(NaturalResourceDescription, _maxAngle) == 0x44, "NaturalResourceDescription::_maxAngle @ +0x44");
static_assert(offsetof(NaturalResourceDescription, _groupModels) == 0x48, "NaturalResourceDescription::_groupModels @ +0x48");
static_assert(offsetof(NaturalResourceDescription, _groupPersistance) == 0x4c, "NaturalResourceDescription::_groupPersistance @ +0x4c");
static_assert(offsetof(NaturalResourceDescription, _groupOctaves) == 0x50, "NaturalResourceDescription::_groupOctaves @ +0x50");
static_assert(offsetof(NaturalResourceDescription, _groupScale) == 0x54, "NaturalResourceDescription::_groupScale @ +0x54");
static_assert(offsetof(NaturalResourceDescription, _groupCutOff) == 0x58, "NaturalResourceDescription::_groupCutOff @ +0x58");
static_assert(offsetof(NaturalResourceDescription, _groupRandom) == 0x5c, "NaturalResourceDescription::_groupRandom @ +0x5c");
static_assert(offsetof(NaturalResourceDescription, _positionTolerance) == 0x60, "NaturalResourceDescription::_positionTolerance @ +0x60");
static_assert(offsetof(NaturalResourceDescription, _animated) == 0x64, "NaturalResourceDescription::_animated @ +0x64");
static_assert(offsetof(NaturalResourceDescription, _preAnimateTime) == 0x68, "NaturalResourceDescription::_preAnimateTime @ +0x68");
static_assert(offsetof(NaturalResourceDescription, _postAnimateTime) == 0x70, "NaturalResourceDescription::_postAnimateTime @ +0x70");
static_assert(offsetof(NaturalResourceDescription, _profession) == 0x78, "NaturalResourceDescription::_profession @ +0x78");
static_assert(offsetof(NaturalResourceDescription, _toolType) == 0x80, "NaturalResourceDescription::_toolType @ +0x80");
