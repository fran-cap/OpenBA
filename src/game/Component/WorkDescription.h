#pragma once
// ============================================================================
// src/game/Component/WorkDescription.h — ENDGAME B.3.8 K1 (B38_RULES.md D1-D5, §2.2)
//
// RETAIL (Application-x64.exe, B38_SLOTS.md §6):
//   reflected "WorkDescription", parent "Description", ti 0x1401c80a8
//   create 0x140039ed0: MemAlloc(0x38, 8), `??0External@Object@@`, the
//   Description stamp, the leaf stamp, the field defaults. createInPlace 0x140039f10.
//   table 0x140164c60, 10 slots:
//     [0] 0x140039f50 scalar deleting dtor   [1] 0x140039ec0 TypeName   [2] 0x140039eb0 Type
//     [3] 0x14012e960 GetFileVersion = 4   [4] External::Compile (import, inherited)
//     [5] 0x14012e8b0 Serialize (`MOV AL,1`)
//     [6] 0x140039f20 Reconstruct   [7] 0x140001640 Destruct (shared)
//     [8] 0x140039f40 GetComponentType: WorkComponent's TypeId
//     [9] 0x140039f10 Construct (folded with createInPlace)
//   sizeof 0x38.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "Component/Description.h"
#include "Object/exe_type.h"

class WorkDescription final : public Description {
    DECLARE_EXE_TYPE(WorkDescription)

public:
    WorkDescription();

    ~WorkDescription() override = default;                                 // [0] 0x140039f50
    int32_t           GetFileVersion() const override { return 4; }    // [3] 0x14012e960
    bool              Serialize(IO::Stream& stream) override;                    // [5] 0x14012e8b0
    Object::External* Reconstruct() override;                              // [6] 0x140039f20
    void              Destruct() override;                                 // [7] 0x140001640
    Object::Type*     GetComponentType(Object::Type* ret) const override;  // [8] 0x140039f40
    virtual Object::External* Construct();                                 // [9] 0x140039f10

    // ---- fields (PUBLIC: the components read them at raw offsets today) ----
    bool _allowCountChange;                                  // +0x20
    uint8_t _pad21[3];                          // +0x21 never written by the ctor
    int32_t _defaultWorkers;                               // +0x24
    int32_t _minWorkerCount;                               // +0x28
    int32_t _maxWorkerCount;                               // +0x2c
    int32_t _maxArea;                               // +0x30
    bool _scaleWorkersWithSize;                                  // +0x34
    uint8_t _pad35[3];                          // +0x35 never written by the ctor
};

static_assert(sizeof(WorkDescription) == 0x38, "WorkDescription is 0x38 (create 0x140039ed0 MemAlloc(0x38))");
static_assert(offsetof(WorkDescription, _pad21) == 0x21, "WorkDescription::_pad21");
static_assert(offsetof(WorkDescription, _pad35) == 0x35, "WorkDescription::_pad35");
static_assert(alignof(WorkDescription) == 8, "WorkDescription alignment");
static_assert(offsetof(WorkDescription, _allowCountChange) == 0x20, "WorkDescription::_allowCountChange @ +0x20");
static_assert(offsetof(WorkDescription, _defaultWorkers) == 0x24, "WorkDescription::_defaultWorkers @ +0x24");
static_assert(offsetof(WorkDescription, _minWorkerCount) == 0x28, "WorkDescription::_minWorkerCount @ +0x28");
static_assert(offsetof(WorkDescription, _maxWorkerCount) == 0x2c, "WorkDescription::_maxWorkerCount @ +0x2c");
static_assert(offsetof(WorkDescription, _maxArea) == 0x30, "WorkDescription::_maxArea @ +0x30");
static_assert(offsetof(WorkDescription, _scaleWorkersWithSize) == 0x34, "WorkDescription::_scaleWorkersWithSize @ +0x34");
