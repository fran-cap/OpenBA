#pragma once
// ============================================================================
// src/game/Component/CropFieldDescription.h — ENDGAME B.3.8 K1 (B38_RULES.md D1-D5, §2.2)
//
// RETAIL (Application-x64.exe, B38_SLOTS.md §6):
//   reflected "CropFieldDescription", parent "Description", ti 0x1401c7548
//   create 0x140032f30: MemAlloc(0x28, 8), `??0External@Object@@`, the
//   Description stamp, the leaf stamp, the field defaults. createInPlace 0x140032f90.
//   table 0x140162f50, 10 slots:
//     [0] 0x140033040 scalar deleting dtor   [1] 0x140032f20 TypeName   [2] 0x140032f10 Type
//     [3] 0x14011b2f0 GetFileVersion = 3   [4] External::Compile (import, inherited)
//     [5] 0x14011b270 Serialize (`MOV AL,1`)
//     [6] 0x140032fe0 Reconstruct   [7] 0x140001640 Destruct (shared)
//     [8] 0x140033030 GetComponentType: CropFieldComponent's TypeId
//     [9] 0x140032f90 Construct (folded with createInPlace)
//   sizeof 0x28.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "Component/Description.h"
#include "Object/exe_type.h"

class CropFieldDescription final : public Description {
    DECLARE_EXE_TYPE(CropFieldDescription)

public:
    CropFieldDescription();

    ~CropFieldDescription() override = default;                            // [0] 0x140033040
    int32_t           GetFileVersion() const override { return 3; }    // [3] 0x14011b2f0
    bool              Serialize(IO::Stream& stream) override;                    // [5] 0x14011b270
    Object::External* Reconstruct() override;                              // [6] 0x140032fe0
    void              Destruct() override;                                 // [7] 0x140001640
    Object::Type*     GetComponentType(Object::Type* ret) const override;  // [8] 0x140033030
    virtual Object::External* Construct();                                 // [9] 0x140032f90

    // ---- fields (PUBLIC: the components read them at raw offsets today) ----
    uint32_t _type;                             // +0x20 enum (serialized through the uint door)
    float _growthPercentOnTend;                                 // +0x24
};

static_assert(sizeof(CropFieldDescription) == 0x28, "CropFieldDescription is 0x28 (create 0x140032f30 MemAlloc(0x28))");
static_assert(alignof(CropFieldDescription) == 8, "CropFieldDescription alignment");
static_assert(offsetof(CropFieldDescription, _type) == 0x20, "CropFieldDescription::_type @ +0x20");
static_assert(offsetof(CropFieldDescription, _growthPercentOnTend) == 0x24, "CropFieldDescription::_growthPercentOnTend @ +0x24");
