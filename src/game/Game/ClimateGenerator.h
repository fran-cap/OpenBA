#pragma once
// ============================================================================
// src/game/Game/ClimateGenerator.h — ENDGAME B.3.8 block LOOP (B38_RULES D4, sec. 2.2)
//
// `ClimateGenerator` (0x110): one climate's weather-generation parameters (an
// element of Game/Climate/ClimateList.rsc), a real `Object::External` subclass.
// Field layout from ctor 0x1400930c0 + Serialize 0x140092fa0.
//
// RETAIL (Application-x64.exe, B38_SLOTS.md sec. 6):
//   reflected "ClimateGenerator", parent "Object::External", table
//   0x14015e140, 9 slots, sizeof 0x110.
//   [0] 0x140018f60 scalar deleting dtor (~String(+0x28), ~External, MemFree)
//   [1] 0x140018ee0 TypeName   [2] 0x140018ed0 Type
//   [3] 0x1400930b0 GetFileVersion `MOV EAX,3` (External::Load gates the
//       element Serialize on it: a wrong constant silently drops the element)
//   [4] IMPORT Object::External::Compile
//   [5] 0x140092fa0 Serialize (AL=1)
//   [6] 0x140018f40 Reconstruct (`~T()` (slot 0, flags 0), ctor again)
//   [7] 0x140001640 Destruct (`~T()` (slot 0, flags 0), the shared body)
//   [8] 0x140018f30 Construct (placement-new, null-checked)
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "object/External.h"
#include "system/String.h"   // System::String (_name)
#include "Object/exe_type.h"

namespace UI { class StringTable; }

class ClimateGenerator final : public Object::External {
    DECLARE_EXE_TYPE(ClimateGenerator)

public:
    ClimateGenerator();                                           // 0x1400930c0

    ~ClimateGenerator() override;                                 // [0] 0x140018f60
    int32_t GetFileVersion() const override { return 3; }         // [3] 0x1400930b0
    // [4] Compile: inherited (retail's slot is the Object::External import)
    bool Serialize(IO::Stream& stream) override;                        // [5] 0x140092fa0
    Object::External* Reconstruct() override;                     // [6] 0x140018f40
    void Destruct() override;                                     // [7] 0x140001640 this->~T()
    virtual Object::External* Construct();                        // [8] 0x140018f30

public:
    UI::StringTable* _stringTable;   // +0x20: ExternalPtr<StringTable> (serialized via FUN_14000bf40)
    System::String _text;     // +0x28: name (ctor 0x1400930c0 stores qword 0; dtor 0x140018f60 runs ~String first)

    bool     _exposed;           // +0x30: 1 (bool, enabled?; retail serializes it through Serialize(bool&))
    uint8_t  _pad31[3];          // +0x31: padding
    float    temperatureBase;    // +0x34: 50.0f (base temp in F?)
    float    tempVariation;      // +0x38: 30.0f
    float    precipitation;      // +0x3C: 5.0f
    float    windSpeed;          // +0x40: 10.0f
    float    _climateData[48];   // +0x44: 48 floats (seasonal/biome data, memset to 0; Serialize(0x30, float*))
    float    _rainBiasScale;          // +0x104: 0.0f (serialized as float)
    float    _tornadoIntervalYears;          // +0x108: 1.0f
    float    _tornadoRarity;          // +0x10C: 1.0f
};

static_assert(sizeof(ClimateGenerator) == 0x110, "ClimateGenerator is 0x110");
static_assert(offsetof(ClimateGenerator, _pad31) == 0x31, "ClimateGenerator::_pad31");
static_assert(offsetof(ClimateGenerator, tempVariation) == 0x38, "ClimateGenerator::tempVariation");
static_assert(offsetof(ClimateGenerator, precipitation) == 0x3C, "ClimateGenerator::precipitation");
static_assert(alignof(ClimateGenerator) == 8, "qword-aligned");
static_assert(offsetof(ClimateGenerator, _stringTable) == 0x20, "+0x20");
static_assert(offsetof(ClimateGenerator, _text) == 0x28, "+0x28");
static_assert(offsetof(ClimateGenerator, _exposed) == 0x30, "+0x30");
static_assert(offsetof(ClimateGenerator, temperatureBase) == 0x34, "+0x34");
static_assert(offsetof(ClimateGenerator, windSpeed) == 0x40, "+0x40");
static_assert(offsetof(ClimateGenerator, _climateData) == 0x44, "+0x44");
static_assert(offsetof(ClimateGenerator, _rainBiasScale) == 0x104, "+0x104");
static_assert(offsetof(ClimateGenerator, _tornadoIntervalYears) == 0x108, "+0x108");
static_assert(offsetof(ClimateGenerator, _tornadoRarity) == 0x10C, "+0x10C");
