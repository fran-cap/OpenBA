#pragma once
// ============================================================================
// src/game/Tutorial/TutorialUI.h — ENDGAME B.3.8 block GOAL
//
// The controller on the tutorial step panel: caches the panel's text, image
// table and buttons, and handles "quit tutorial".
//
// RETAIL (Application-x64.exe, B38_SLOTS.md section 6):
//   reflected "TutorialUI", parent "UI::DialogController", ti 0x1401c67f0
//   create 0x140029400: MemAlloc(0x60, 8), ??0DialogController, stamp,
//   +0x30..+0x58 zeroed; createInPlace is DialogController's import (B8-13)
//   ctors 0x1400e3ab0 / copy 0x1400e3ae0
//   table 0x140160f30, 11 slots:
//     [0] 0x140029470 scalar deleting dtor: stamp, ~DialogController, MemFree on &1
//     [1] TypeName  [2] Type
//     [3] 0x1400e3aa0 Persist `RET 0` (its own copy of Internal's empty body)
//     [4] 0x1400e37a0 OnCreated: cache the elements, number the buttons
//     [5] DialogController::OnOpened (import) — inherited
//     [6] DialogController::OnClosed (import) — inherited
//     [7] 0x1400e3700 OnUpdateLayout: next/quit button visibility
//     [8] DialogController::OnUpdate (import) — inherited
//     [9] 0x1400e36e0 OnElementEvent: quitButton (0x579) click -> main menu
//     [10] DialogController::OnSerializeSettings (import) — inherited
//   sizeof 0x60.
// ============================================================================

#include <cstddef>
#include <cstdint>
#include "ui/DialogController.h"
#include "Object/exe_type.h"

class TutorialUI final : public UI::DialogController {
    DECLARE_EXE_TYPE(TutorialUI)

public:
    TutorialUI();                                                  // inlined in 0x140029400

    // 0x140029470 minus the free: the restamp; the compiler chains
    // ~DialogController (and so ~Internal), which the tree's hand table skipped.
    ~TutorialUI() override = default;                              // [0] 0x140029470
    void Persist(IO::Stream&/*stream*/) override {}                     // [3] 0x1400e3aa0 RET
    void OnCreated() override;                                     // [4] 0x1400e37a0
    void OnUpdateLayout() override;                                // [7] 0x1400e3700
    void OnElementEvent(Object::InternalPtr<UI::Element>* element,
                        UI::EventId a, int32_t b, int32_t c,
                        int32_t d, int32_t e, void* userData) override;  // [9] 0x1400e36e0

private:
    void QuitTutorial();                                           // 0x1400e34e0

public:
    uint64_t unusedElement;             // +0x30: zeroed, never read by the class
    UI::Element* preText;        // +0x38: InternalPtr<UI::Element> "preText"
    UI::Element* postText;       // +0x40: InternalPtr<UI::Element> "postText"
    UI::Element* imageTable;     // +0x48: InternalPtr<UI::Element> "imageTable"
    UI::Element* nextButton;     // +0x50: InternalPtr<UI::Element> "nextButton" (id 0x578)
    UI::Element* quitButton;     // +0x58: InternalPtr<UI::Element> "quitButton" (id 0x579)
};

static_assert(offsetof(TutorialUI, unusedElement) == 0x30, "+0x30");
static_assert(offsetof(TutorialUI, preText) == 0x38, "+0x38");
static_assert(offsetof(TutorialUI, postText) == 0x40, "+0x40");
static_assert(offsetof(TutorialUI, imageTable) == 0x48, "+0x48");
static_assert(offsetof(TutorialUI, nextButton) == 0x50, "+0x50");
static_assert(offsetof(TutorialUI, quitButton) == 0x58, "+0x58");
static_assert(sizeof(TutorialUI) == 0x60, "TutorialUI is 0x60 (create 0x140029400 MemAlloc(0x60))");
static_assert(alignof(TutorialUI) == 8, "qword-aligned");
