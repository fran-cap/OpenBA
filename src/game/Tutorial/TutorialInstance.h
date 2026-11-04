#pragma once
// ============================================================================
// src/game/Tutorial/TutorialInstance.h — ENDGAME B.3.8 block GOAL (B38_RULES D1-D5)
//
// The live tutorial: which step is running, the step dialog and its controller.
// FinishTutorialInstance::Startup builds one and owns it through +0x20.
//
// RETAIL (Application-x64.exe, B38_SLOTS.md section 6):
//   reflected "TutorialInstance", parent "Object::Internal", ti 0x1401c6668
//   create 0x140028120 (MemAlloc(0x38, 8)), createInPlace = the IMPORTED
//   Internal::CreateInPlaceObject (null; the tree keeps its in-place build,
//   B8-13). The ctor is inlined in create and in FinishTutorialInstance's
//   Startup 0x1400d7a90: Internal(), stamp, +0x10 = -1, InternalBasePtr(+0x18,
//   null), InternalBasePtr(+0x20, null), +0x30 = 0 (+0x28 is not written).
//   table 0x140160bf8, 4 slots:
//     [0] 0x1400281a0 scalar deleting dtor: stamp, ~Internal, MemFree on &1
//     [1] TypeName  [2] Type  [3] 0x1400e1520 Persist
//   sizeof 0x38.
// ============================================================================

#include <cstddef>
#include <cstdint>
#include "object/Internal.h"
#include "Object/exe_type.h"
#include "collection/Collection.h"   // StepAt
namespace System { class Time; }
namespace Achievement { class GoalList; }

namespace UI { class DialogInstance; }
class TutorialUI;
class Tutorial;
class TutorialStep;

class TutorialInstance final : public Object::Internal {
    DECLARE_EXE_TYPE(TutorialInstance)

public:
    TutorialInstance();                                            // inlined in 0x140028120

    ~TutorialInstance() override = default;                        // [0] 0x1400281a0
    void          Persist(IO::Stream& stream) override;                  // [3] 0x1400e1520

public:
    int32_t  currentStep;        // +0x10: current step index (-1 = uninitialized)
    uint8_t  _pad14[4];          // +0x14: padding
    UI::DialogInstance* stepDialog; // +0x18: InternalPtr<UI::DialogInstance> — the step dialog
    TutorialUI* stepUI;          // +0x20: InternalPtr<TutorialUI> — its controller
    uint8_t  _pad28[8];          // +0x28: never written by the ctor
    Tutorial* tutorialDef;       // +0x30: ExternalPtr<Tutorial> — the definition

    // ---- non-virtual members (tutorial_wave.cpp / tutorial_next_scenario.cpp) ----
    // 0x1400e1db0: the per-tick step driver FinishTutorialInstance::Update calls.
    void StepExecute(const System::Time* time, const Achievement::GoalList* goals, uint64_t param4);   // B4 X7-GAME-A: RDX / R8 of FinishTutorialInstance::Update(const Time&, const GoalList&) (0x1400d7b90 tail JMP)
    void RefreshStepUI();        // 0x1400e1260: push the current step into the TutorialUI panel
    void LoadNextScenario();     // 0x1400e0d70: the tutorial finished, boot the next scenario

    // The step `index` names (RefreshStepUI 0x1400e1260: `MOVSXD RDX,[RCX+0x10]; MOV RDI,[RDI+RDX*8]`).
    // NOT RETAIL (review F-115): null when `index` is outside the steps array. Retail reads
    // steps[currentStep] unbounded; a save can hold currentStep == steps._size (the value
    // after the last step) or a negative value, and the read then lands outside the array.
    static TutorialStep* StepAt(const Collection::Array<TutorialStep*>& steps, int index) {
        if (index < 0 || index >= steps._size)
            return nullptr;
        return steps._data[index];
    }
    // LoadNextScenario's test of the scenario index against the scenario list's count
    // (0x1400e0da6: `CMP [RDX+0x38],R14D; JL` -> return).
    // NOT RETAIL (review F-113): retail tests `index < 0` only; an index at or past the
    // list's count (a tutorial resource's +0x38) read past the scenario list.
    static bool ScenarioIndexValid(int index, int count) {
        return index >= 0 && index < count;
    }
};

static_assert(offsetof(TutorialInstance, currentStep) == 0x10, "+0x10");
static_assert(offsetof(TutorialInstance, stepDialog) == 0x18, "+0x18");
static_assert(offsetof(TutorialInstance, stepUI) == 0x20, "+0x20");
static_assert(offsetof(TutorialInstance, tutorialDef) == 0x30, "+0x30");
static_assert(sizeof(TutorialInstance) == 0x38, "TutorialInstance is 0x38 (create 0x140028120 MemAlloc(0x38))");
static_assert(offsetof(TutorialInstance, _pad14) == 0x14, "TutorialInstance::_pad14");
static_assert(offsetof(TutorialInstance, _pad28) == 0x28, "TutorialInstance::_pad28");
static_assert(alignof(TutorialInstance) == 8, "qword-aligned");
