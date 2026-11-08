#pragma once
// ============================================================================
// src/game/Tutorial/FinishTutorialGoal.h — ENDGAME B.3.8 block GOAL (B38_RULES D8)
//
// The tutorial's only goal: met when the TutorialInstance has run every step.
//
// RETAIL (Application-x64.exe, B38_SLOTS.md section 6):
//   reflected "FinishTutorialGoal", parent "Achievement::Goal", ti 0x1401c6240
//   create 0x1400260f0 (MemAlloc(0x38, 8)), createInPlace 0x140026150
//   ctor 0x1400d79e0, copy ctor 0x1400d7a10
//   table 0x140160420, 11 slots:
//     [0] 0x140026260 scalar deleting dtor: stamp, ~Goal, MemFree on &1
//     [1] TypeName  [2] Type  [3] 0x1400d79d0 GetFileVersion `MOV EAX,1`
//     [4] External::Compile (inherited)  [5] 0x1400d7970 Serialize
//     [6] 0x1400261a0 Reconstruct  [7] 0x140001640 Destruct
//     [8] 0x1400d7960 CheckGoalState `XOR EAX,EAX` (the instance decides)
//     [9] 0x1400261f0 GetInstanceType — RETSLOT, FinishTutorialInstance's id
//     [10] 0x140026150 Construct
//   The old tree table left 1/4/7 null (B38 D9 #5) and carried a 12th entry,
//   the first slot of the NEXT retail table (the instance's dtor).
//   sizeof 0x38.
// ============================================================================

#include <cstddef>
#include <cstdint>
#include "achievement/Goal.h"
#include "Object/exe_type.h"

class Tutorial;

class FinishTutorialGoal final : public Achievement::Goal {
    DECLARE_EXE_TYPE(FinishTutorialGoal)

public:
    FinishTutorialGoal();                                          // 0x1400d79e0

    ~FinishTutorialGoal() override = default;                      // [0] 0x140026260
    int32_t           GetFileVersion() const override { return 1; }// [3] 0x1400d79d0
    bool              Serialize(IO::Stream& stream) override;            // [5] 0x1400d7970
    Object::External* Reconstruct() override;                      // [6] 0x1400261a0
    void              Destruct() override;                         // [7] 0x140001640
    GoalState         CheckGoalState() const override { return kNotMet; }  // [8] 0x1400d7960
    Object::Type*     GetInstanceType(Object::Type* ret) const override;   // [9] 0x1400261f0
    virtual Object::External* Construct();                         // [10] 0x140026150

public:
    Tutorial* _tutorial;          // +0x30: ExternalPtr<Tutorial> — the tutorial definition
};

static_assert(offsetof(FinishTutorialGoal, _tutorial) == 0x30, "FinishTutorialGoal +0x30 tutorial");
static_assert(sizeof(FinishTutorialGoal) == 0x38, "FinishTutorialGoal is 0x38 (create 0x1400260f0 MemAlloc(0x38))");
static_assert(alignof(FinishTutorialGoal) == 8, "FinishTutorialGoal is qword-aligned");
