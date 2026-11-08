#pragma once
// ============================================================================
// src/game/Component/AttendanceDescription.h — ENDGAME B.3.8 K1 (B38_RULES.md D1-D5, §2.2)
//
// RETAIL (Application-x64.exe, B38_SLOTS.md §6):
//   reflected "AttendanceDescription", parent "Description", ti 0x1401c3f78
//   create 0x1400026f0: MemAlloc(0x28, 8), `??0External@Object@@`, the
//   Description stamp, the leaf stamp, the field defaults. createInPlace 0x140002750.
//   table 0x140158d70, 10 slots:
//     [0] 0x140002800 scalar deleting dtor   [1] 0x1400026e0 TypeName   [2] 0x1400026d0 Type
//     [3] 0x140044700 GetFileVersion = 1   [4] External::Compile (import, inherited)
//     [5] 0x1400446b0 Serialize (`MOV AL,1`)
//     [6] 0x1400027a0 Reconstruct   [7] 0x140001640 Destruct (shared)
//     [8] 0x1400027f0 GetComponentType: AttendanceComponent's TypeId
//     [9] 0x140002750 Construct (folded with createInPlace)
//   sizeof 0x28.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "Component/Description.h"
#include "Object/exe_type.h"

class AttendanceDescription final : public Description {
    DECLARE_EXE_TYPE(AttendanceDescription)

public:
    AttendanceDescription();

    ~AttendanceDescription() override = default;                           // [0] 0x140002800
    int32_t           GetFileVersion() const override { return 1; }    // [3] 0x140044700
    bool              Serialize(IO::Stream& stream) override;                    // [5] 0x1400446b0
    Object::External* Reconstruct() override;                              // [6] 0x1400027a0
    void              Destruct() override;                                 // [7] 0x140001640
    Object::Type*     GetComponentType(Object::Type* ret) const override;  // [8] 0x1400027f0
    virtual Object::External* Construct();                                 // [9] 0x140002750

    // ---- fields (PUBLIC: the components read them at raw offsets today) ----
    int32_t _maxCitizens;                               // +0x20
    uint8_t _pad24[4];                          // +0x24 never written by the ctor
};

static_assert(sizeof(AttendanceDescription) == 0x28, "AttendanceDescription is 0x28 (create 0x1400026f0 MemAlloc(0x28))");
static_assert(offsetof(AttendanceDescription, _pad24) == 0x24, "AttendanceDescription::_pad24");
static_assert(alignof(AttendanceDescription) == 8, "AttendanceDescription alignment");
static_assert(offsetof(AttendanceDescription, _maxCitizens) == 0x20, "AttendanceDescription::_maxCitizens @ +0x20");
