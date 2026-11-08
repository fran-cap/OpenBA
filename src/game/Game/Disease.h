#pragma once
// ============================================================================
// src/game/Game/Disease.h — ENDGAME B.3.8 block LOOP (B38_RULES D4, sec. 2.2)
//
// `Disease`, one disease resource definition (an element of
// Game/Disease/Diseases.rsc), a real `Object::External` subclass.
//
// RETAIL (Application-x64.exe, B38_SLOTS.md sec. 6):
//   reflected "Disease", parent "Object::External", table 0x14015e190, 9 slots,
//   sizeof 0x48. create 0x14008fd0: MemAlloc(0x48, 8); ??0External; the field
//   defaults below.
//   [0] 0x140018f60 scalar deleting dtor (shared with ClimateGenerator:
//       ~String(+0x28), ~External, MemFree on &1)
//   [1] 0x140018fc0 TypeName   [2] 0x140018fb0 Type
//   [3] 0x140093390 GetFileVersion `MOV EAX,2`
//   [4] IMPORT Object::External::Compile
//   [5] 0x1400932d0 Serialize (MOV AL,1)
//   [6] 0x1400190a0 Reconstruct   [7] 0x140001640 Destruct (`~T()` (slot 0, flags 0))
//   [8] 0x140019040 Construct (placement-new of the class)
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "object/External.h"
#include "system/String.h"   // System::String (_name)
#include "Object/exe_type.h"

namespace UI { class StringTable; }

class Disease final : public Object::External {
    DECLARE_EXE_TYPE(Disease)

public:
    Disease();                                                    // create 0x14008fd0's init

    ~Disease() override;                                          // [0] 0x140018f60
    int32_t GetFileVersion() const override { return 2; }         // [3] 0x140093390
    // [4] Compile: inherited (retail's slot is the Object::External import)
    bool Serialize(IO::Stream& stream) override;                        // [5] 0x1400932d0
    Object::External* Reconstruct() override;                     // [6] 0x1400190a0
    void Destruct() override;                                     // [7] 0x140001640 this->~T()
    virtual Object::External* Construct();                        // [8] 0x140019040

public:
    UI::StringTable* _stringTable;   // +0x20  ExternalPtr<StringTable> (weak)
    System::String   _name;          // +0x28  (Construct 0x140019040 stores qword 0; dtor 0x140018f60 runs ~String first)

    float   _spreadIntervalMonths;       // +0x30  1.0f
    float   _contagion;       // +0x34  0.0f
    float   _spreadRadius;       // +0x38  1.0f
    float   _catchWeight;       // +0x3c  10.0f
    float   _deathOdds;       // +0x40  0.0f
    uint8_t _pad44[4];      // +0x44
};

static_assert(sizeof(Disease) == 0x48, "Disease is 0x48 (create 0x14008fd0 MemAlloc(0x48))");
static_assert(offsetof(Disease, _contagion) == 0x34, "Disease::_contagion");
static_assert(offsetof(Disease, _spreadRadius) == 0x38, "Disease::_spreadRadius");
static_assert(offsetof(Disease, _catchWeight) == 0x3C, "Disease::_catchWeight");
static_assert(offsetof(Disease, _pad44) == 0x44, "Disease::_pad44");
static_assert(alignof(Disease) == 8, "qword-aligned");
static_assert(offsetof(Disease, _stringTable) == 0x20, "+0x20");
static_assert(offsetof(Disease, _name)        == 0x28, "+0x28");
static_assert(offsetof(Disease, _spreadIntervalMonths)     == 0x30, "+0x30");
static_assert(offsetof(Disease, _deathOdds)     == 0x40, "+0x40");
