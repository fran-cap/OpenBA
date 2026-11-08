#pragma once
// ============================================================================
// src/game/Component/FruitDescription.h — ENDGAME B.3.8 K1 (B38_RULES.md D1-D5, §2.2)
//
// RETAIL (Application-x64.exe, B38_SLOTS.md §6):
//   reflected "FruitDescription", parent "Description", ti 0x1401c4288
//   create 0x140005980: MemAlloc(0x38, 8), `??0External@Object@@`, the
//   Description stamp, the leaf stamp, the field defaults. createInPlace 0x1400059f0.
//   table 0x1401594d0, 10 slots:
//     [0] 0x140005ac0 scalar deleting dtor   [1] 0x140005970 TypeName   [2] 0x140005960 Type
//     [3] 0x14004a260 GetFileVersion = 1   [4] External::Compile (import, inherited)
//     [5] 0x14004a1e0 Serialize (`MOV AL,1`)
//     [6] 0x140005a50 Reconstruct   [7] 0x140001640 Destruct (shared)
//     [8] 0x140005ab0 GetComponentType: FruitComponent's TypeId
//     [9] 0x1400059f0 Construct (folded with createInPlace)
//   sizeof 0x38.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "Component/Description.h"
#include "Object/exe_type.h"

class ComponentDescription;

class FruitDescription final : public Description {
    DECLARE_EXE_TYPE(FruitDescription)

public:
    FruitDescription();

    ~FruitDescription() override = default;                                // [0] 0x140005ac0
    int32_t           GetFileVersion() const override { return 1; }    // [3] 0x14004a260
    bool              Serialize(IO::Stream& stream) override;                    // [5] 0x14004a1e0
    Object::External* Reconstruct() override;                              // [6] 0x140005a50
    void              Destruct() override;                                 // [7] 0x140001640
    Object::Type*     GetComponentType(Object::Type* ret) const override;  // [8] 0x140005ab0
    virtual Object::External* Construct();                                 // [9] 0x1400059f0

    // ---- fields (PUBLIC: the components read them at raw offsets today) ----
    float _ripenSeasons;                        // +0x20 seasons to ripen (Update 0x140049de0: 0.98 / (x * 300))
    float _optimalTemp;                         // +0x24 ctor stores int 0
    float _tempRange;                           // +0x28 ctor stores int 0; |temp - optimal| / range
    uint8_t _pad2c[4];                          // +0x2c never written by the ctor
    ComponentDescription* _rawMaterial;               // +0x30 ExternalPtr<ComponentDescription> (Serialize 0x14004a1e0: the component-ref serializer); the fruit's raw material
};

static_assert(sizeof(FruitDescription) == 0x38, "FruitDescription is 0x38 (create 0x140005980 MemAlloc(0x38))");
static_assert(offsetof(FruitDescription, _pad2c) == 0x2C, "FruitDescription::_pad2c");
static_assert(alignof(FruitDescription) == 8, "FruitDescription alignment");
static_assert(offsetof(FruitDescription, _ripenSeasons) == 0x20, "FruitDescription::_ripenSeasons @ +0x20");
static_assert(offsetof(FruitDescription, _optimalTemp) == 0x24, "FruitDescription::_optimalTemp @ +0x24");
static_assert(offsetof(FruitDescription, _tempRange) == 0x28, "FruitDescription::_tempRange @ +0x28");
static_assert(offsetof(FruitDescription, _rawMaterial) == 0x30, "FruitDescription::_rawMaterial @ +0x30");
