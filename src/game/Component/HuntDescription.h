#pragma once
// ============================================================================
// src/game/Component/HuntDescription.h — ENDGAME B.3.8 K1 (B38_RULES.md D1-D5, §2.2)
//
// RETAIL (Application-x64.exe, B38_SLOTS.md §6):
//   reflected "HuntDescription", parent "Description", ti 0x1401c78c8
//   create 0x140034f90: MemAlloc(0x28, 8), `??0External@Object@@`, the
//   Description stamp, the leaf stamp, the field defaults. createInPlace 0x140034ff0.
//   table 0x140163820, 10 slots:
//     [0] 0x1400350a0 scalar deleting dtor   [1] 0x140034f80 TypeName   [2] 0x140034f70 Type
//     [3] 0x1401229e0 GetFileVersion = 3   [4] External::Compile (import, inherited)
//     [5] 0x140122960 Serialize (`MOV AL,1`)
//     [6] 0x140035040 Reconstruct   [7] 0x140001640 Destruct (shared)
//     [8] 0x140035090 GetComponentType: HuntComponent's TypeId
//     [9] 0x140034ff0 Construct (folded with createInPlace)
//   sizeof 0x28.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "Component/Description.h"
#include "Object/exe_type.h"

class HuntDescription final : public Description {
    DECLARE_EXE_TYPE(HuntDescription)

public:
    HuntDescription();

    ~HuntDescription() override = default;                                 // [0] 0x1400350a0
    int32_t           GetFileVersion() const override { return 3; }    // [3] 0x1401229e0
    bool              Serialize(IO::Stream& stream) override;                    // [5] 0x140122960
    Object::External* Reconstruct() override;                              // [6] 0x140035040
    void              Destruct() override;                                 // [7] 0x140001640
    Object::Type*     GetComponentType(Object::Type* ret) const override;  // [8] 0x140035090
    virtual Object::External* Construct();                                 // [9] 0x140034ff0

    // ---- fields (PUBLIC: the components read them at raw offsets today) ----
    float _killTimeInMonths;                                 // +0x20
    uint32_t _type;                             // +0x24 enum (serialized through the uint door)
};

static_assert(sizeof(HuntDescription) == 0x28, "HuntDescription is 0x28 (create 0x140034f90 MemAlloc(0x28))");
static_assert(alignof(HuntDescription) == 8, "HuntDescription alignment");
static_assert(offsetof(HuntDescription, _killTimeInMonths) == 0x20, "HuntDescription::_killTimeInMonths @ +0x20");
static_assert(offsetof(HuntDescription, _type) == 0x24, "HuntDescription::_type @ +0x24");
