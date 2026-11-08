#pragma once
// ============================================================================
// src/game/Tutorial/FinishTutorialInstance.h — ENDGAME B.3.8 block GOAL (B38_RULES D8)
//
// The live half of FinishTutorialGoal: owns the TutorialInstance and steps it.
//
// RETAIL (Application-x64.exe, B38_SLOTS.md section 6):
//   reflected "FinishTutorialInstance", parent "Achievement::GoalInstance",
//   ti 0x1401c6278, create 0x140026070 (MemAlloc(0x28, 8)), createInPlace =
//   the IMPORTED Internal::CreateInPlaceObject (null; the tree keeps its
//   in-place build, B8-13); ctor 0x1400d7c70, copy ctor 0x1400d7cb0
//   table 0x140160478, 7 slots:
//     [0] 0x140026200 scalar deleting dtor: stamp, InternalBasePtr::DeletePtr
//         on +0x20, ~GoalInstance, MemFree on &1
//     [1] TypeName  [2] Type
//     [3] 0x1400d7bf0 Persist
//     [4] 0x1400d7a90 Startup (builds the TutorialInstance; R8 is passed through
//         untouched to 0x1401001f0, which ignores it — read 2026-09-26)
//     [5] 0x1400d7a50 Update (tail-JMP 0x1400e1db0 on the TutorialInstance)
//     [6] 0x1400d7bb0 CheckGoalState
//   sizeof 0x28.
// ============================================================================

#include <cstddef>
#include <cstdint>
#include "achievement/Goal.h"
#include "Object/exe_type.h"
#include "object/Internal.h"   // Object::InternalPtr

class TutorialInstance;

class FinishTutorialInstance final : public Achievement::GoalInstance {
    DECLARE_EXE_TYPE(FinishTutorialInstance)

public:
    FinishTutorialInstance();                                      // 0x1400d7c70

    ~FinishTutorialInstance() override;                            // [0] 0x140026200
    void Persist(IO::Stream& stream) override;                           // [3] 0x1400d7bf0
    void Startup() override;                                       // [4] 0x1400d7a90
    void Update(const System::Time& time,
                const Achievement::GoalList& list) override;       // [5] 0x1400d7a50
    Achievement::Goal::GoalState CheckGoalState() const override;  // [6] 0x1400d7bb0

public:
    Object::InternalPtr<TutorialInstance> _tutorialInstance;   // +0x20: the tutorial instance (Startup builds it)
};

static_assert(offsetof(FinishTutorialInstance, _tutorialInstance) == 0x20, "FinishTutorialInstance +0x20 tutorial instance");
static_assert(sizeof(FinishTutorialInstance) == 0x28, "FinishTutorialInstance is 0x28 (create 0x140026070 MemAlloc(0x28))");
static_assert(alignof(FinishTutorialInstance) == 8, "FinishTutorialInstance is qword-aligned");
