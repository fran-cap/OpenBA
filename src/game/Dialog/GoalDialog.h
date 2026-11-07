#pragma once
// ============================================================================
// src/game/Dialog/GoalDialog.h — ENDGAME B.3.8 block DLG
//
// The scenario-goals list opened from the pause menu.
//
// RETAIL (Application-x64.exe, B38_SLOTS.md section 6):
//   reflected "GoalDialog", parent "UI::DialogInstance", ti 0x1401c5018
//   create 0x140011b60 (MemAlloc(0xc8, 8)), createInPlace 0x140011bc0
//   ctors 0x140069e70 / 0x140069ee0; MainMenuDialog::OpenGoal 0x1400b3220
//   inlines one (+0xb8, +0xc0 = 0)
//   table 0x14015beb0, 10 slots:
//     [0] 0x140011c10 scalar deleting dtor: MemFree(+0xb8), +0xb8/+0xc0 = 0,
//         ~DialogInstance, MemFree on &1
//     [1] TypeName  [2] Type  [3] DialogInstance::Persist (import)
//     [4] 0x140069810 OnOpened  [5] DialogInstance::OnClosed
//     [6] 0x140069640 OnUpdateLayout  [7] 0x140069800 OnUpdate `RET 0`
//     [8] 0x1400697c0 OnElementEvent  [9] DialogInstance::OnSerializeSettings
//   sizeof 0xc8.
// ============================================================================

#include <cstddef>
#include <cstdint>
#include "ui/DialogInstance.h"
#include "collection/Collection.h"
#include "Object/exe_type.h"

// One goal row (0x18): OnOpened 0x140069810 appends it (the Array's inlined
// SetSize); OnUpdateLayout 0x140069640 shows / hides the two images.
struct GoalDialogRow {
    Object::InternalPtr<UI::Element> imageComplete;   // +0x00 shown when the goal is met
    Object::InternalPtr<UI::Element> imageFail;       // +0x08 shown when the goal has failed
    Object::InternalPtr<UI::Element> textGoal;        // +0x10 the goal's text (a TextElement)
};
static_assert(sizeof(GoalDialogRow) == 0x18, "GoalDialogRow is 0x18 (the +0xb8 array stride)");
static_assert(offsetof(GoalDialogRow, imageComplete) == 0x0, "GoalDialogRow::imageComplete");
static_assert(offsetof(GoalDialogRow, imageFail) == 0x8, "GoalDialogRow::imageFail");
static_assert(offsetof(GoalDialogRow, textGoal) == 0x10, "GoalDialogRow::textGoal");

class GoalDialog final : public UI::DialogInstance {
    DECLARE_EXE_TYPE(GoalDialog)

public:
    GoalDialog();                                                   // create 0x140011b60
    GoalDialog(UI::UserInterface* ui, const UI::Dialog* dialog,
               Object::InternalPtr<UI::DialogInstance> parent);     // 0x1400b3220 inlined

    // 0x140011c10: the member ~Array (MemFree, the header zeroed), ~DialogInstance.
    ~GoalDialog() override = default;                              // [0] 0x140011c10
    void OnOpened() override;                                      // [4] 0x140069810
    // [5] OnClosed: DialogInstance's (SaveSettings), inherited (B38 D3.1).
    void OnUpdateLayout() override;                                // [6] 0x140069640
    void OnUpdate(const System::Time& time) override;              // [7] 0x140069800 RET
    void OnElementEvent(Object::InternalPtr<UI::Element>* element,
                        UI::EventId a, int32_t b, int32_t c,
                        int32_t d, int32_t e, void* userData) override;  // [8] 0x1400697c0

public:
    Collection::Array<GoalDialogRow> goals;   // +0xB8 one row per scenario goal
};

static_assert(offsetof(GoalDialog, goals) == 0xB8, "GoalDialog +0xb8");
static_assert(sizeof(GoalDialog) == 0xC8, "GoalDialog is 0xc8 (create 0x140011b60)");
static_assert(alignof(GoalDialog) == 8, "GoalDialog is qword-aligned");
