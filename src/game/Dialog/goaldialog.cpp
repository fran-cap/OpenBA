// src/game/Dialog/goaldialog.cpp
// Clean reimplementation of GoalDialog game logic.
//
// GoalDialog (0xC8 bytes) — achievement goal progress display.
// Extends DialogInstance (0xB8 base).
// +0xB8 = goals, one GoalDialogRow per scenario goal.
//
// Decompiled from Application-x64.exe.

#include "ui/TextElement.h"   // ENDGAME B.3.6 UI callers (K2)
#include "ui/TableElement.h"   // ENDGAME B.3.6 UI callers (K2)
#include "ui/ElementDescription.h"   // ENDGAME B.3.6 UI callers (K2)
#include "ui/Element.h"   // ENDGAME B.3.6 UI callers (K2)
#include "ui/DialogInstance.h"   // ENDGAME B.3.6 UI callers (K2)
#include "ui/UserInterface.h"   // ENDGAME B.3.6 UI callers (K2)
#include "ui/String.h"   // ENDGAME B.3.6 UI callers (K2)
#include "ui/StringTable.h"   // ENDGAME B.3.6 UI callers (K2)
#include "ui/Dialog.h"   // ENDGAME B.3.6 UI callers (K2)
#include "achievement/AchievementManager.h"   // ENDGAME B.3.7 callers (K1)
#include "object/External.h"   // ENDGAME B.3.6 UI callers (K2)
#include <new>   // ENDGAME B.3.6 UI callers (K2)
#include "Dialog/GoalDialog.h"
#include "Object/factory_registration.h"
#include "system/Value.h"
#include "ui/DialogInstance.h"
#include "object/TypeInfo.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "system/String.h"
#include <cstdint>
#include <cstring>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B

// ============================================================
// GoalDialog::OnOpened (vtable +0x20) — Setup: populate goal table
// ============================================================
// Finds "buttonOk" (ID 100), "tableGoal", and "textScenario".
// Gets the scenario's AchievementInstance from the game. For each goal:
//   - Creates a row element from dialog template[0]
//   - Inserts into tableGoal via SetChild
//   - Finds "textGoal", "imageComplete", "imageFail" sub-elements
//   - Sets goal text on textGoal via UI::String
//   - Hides both complete/fail images initially
//   - Appends {imageComplete, imageFail, textGoal} to `goals`
// If the scenario has an Achievement, sets its title on textScenario.
// Otherwise hides textScenario.
//
// Original: 0x140069810.

void GoalDialog::OnOpened() {
    // Find "buttonOk" and set element ID 100
    {
        System::String nameStr("buttonOk");
        UI::Element* buttonOk = FindElement(nameStr).Get();
        if (buttonOk) buttonOk->SetElementId(100);
    }

    // The scenario's AchievementInstance (the game's +0x150)
    Achievement::AchievementInstance* goalListObj = GameUpdateLoop::_singleton->m_scenarioInstance.Get();

    if (goalListObj != nullptr) {
        const Achievement::GoalList* goalList = &goalListObj->_goalList;

        // Find "tableGoal"
        UI::TableElement* tableGoal = nullptr;
        {
            System::String nameStr("tableGoal");
            tableGoal = static_cast<UI::TableElement*>(FindElement(nameStr).Get());
        }

        int goalCount = goalList->GetGoalCount();
        const UI::Dialog* dialog = _dialog;

        for (int i = 0; i < goalCount; i++) {
            // Create row element from dialog template[0]
            Object::ExternalPtr<UI::ElementDescription> elemDesc = dialog->GetTemplate(0);

            // ElementDescription slot 8 CreateInstance (RETSLOT)
            Object::InternalPtr<UI::Element> newElemPtr;
            if (elemDesc.Get()) {
                elemDesc->CreateInstance(&newElemPtr, dialog);
            }

            // Insert the new element as a child of the table
            if (tableGoal) {
                tableGoal->SetChild(&newElemPtr, i);
            }

            // Find sub-elements within the new row
            UI::Element* newElem = newElemPtr.Get();
            UI::Element* imageComplete = nullptr;
            UI::Element* imageFail = nullptr;
            UI::TextElement* textGoal = nullptr;

            {
                System::String nameStr("textGoal");
                textGoal = static_cast<UI::TextElement*>(newElem->FindElement(nameStr).Get());
            }
            {
                System::String nameStr("imageComplete");
                imageComplete = newElem->FindElement(nameStr).Get();
            }
            {
                System::String nameStr("imageFail");
                imageFail = newElem->FindElement(nameStr).Get();
            }

            // Get goal text and set on textGoal element
            {
                Object::InternalPtr<Achievement::GoalInstance> goalInst = goalList->GetGoal(i);

                // GoalInstance -> its Goal
                Achievement::Goal* goal = goalInst->_goal.Get();

                // Goal text (non-trivial return: a System::String)
                System::String textStr(goal->GetText());

                // UI::String from the Goal's StringTable + text
                UI::String uiStr(goal->_stringTable, textStr);
                uiStr.pad0C = 0;   // built in a zeroed 16-byte slot before

                // Set text on textGoal element
                if (textGoal) {
                    textGoal->SetText(&uiStr);
                }
            }

            // Hide both complete and fail images initially
            if (imageComplete) imageComplete->SetFlagsOnOff(UI::kElementFlagHidden, 0);
            if (imageFail) imageFail->SetFlagsOnOff(UI::kElementFlagHidden, 0);

            // Append {imageComplete, imageFail, textGoal}. GoalDialog::OnOpened's own
            // inlined copy of Collection::Array<T>::SetSize — NOT FUN_140011750
            // (that one has a 0x30 stride with a String at +0x20 — different array).
            GoalDialogRow row;
            row.imageComplete = Object::InternalPtr<UI::Element>(imageComplete);
            row.imageFail = Object::InternalPtr<UI::Element>(imageFail);
            row.textGoal = Object::InternalPtr<UI::Element>(textGoal);
            goals.SetSize(goals.GetSize() + 1, row);
        }
    }

    // Find "textScenario" element
    UI::TextElement* textScenario = nullptr;
    {
        System::String nameStr("textScenario");
        textScenario = static_cast<UI::TextElement*>(FindElement(nameStr).Get());
    }

    // If the scenario has an Achievement, set its title on textScenario
    if (goalListObj != nullptr && goalListObj->_achievement.Get() != nullptr) {
        Achievement::Achievement* achievement = goalListObj->_achievement.Get();

        // Title string (non-trivial return)
        System::String titleStr(achievement->GetTitle());

        // UI::String from the Achievement's StringTable + title
        UI::String uiStr(achievement->_stringTable, titleStr);
        uiStr.pad0C = 0;   // built in a zeroed 16-byte slot before

        // Convert UI::String to System::String for SetParameter
        System::String sysStr(uiStr.operator System::String());

        // Set as parameter 0 on textScenario
        if (textScenario) {
            textScenario->SetParameter(0, sysStr);
        }
    } else {
        // No achievement/scenario — hide textScenario
        if (textScenario) textScenario->SetFlagsOnOff(UI::kElementFlagHidden, 0);
    }
}

// ============================================================
// GoalDialog::OnUpdateLayout (vtable +0x30) — Update goal entry visibility
// ============================================================
// Gets the scenario's goal list, iterates each row.
// For each goal: checks IsMet/HasFailed, sets visibility flags on
// the row's two images.
//
// Original: 0x140069640.

void GoalDialog::OnUpdateLayout() {
    Achievement::AchievementInstance* goalListObj = GameUpdateLoop::_singleton->m_scenarioInstance.Get();

    int count = goals._size;

    for (int i = 0; i < count; i++) {
        // Goal at index i from the GoalList
        Object::InternalPtr<Achievement::GoalInstance> goalInst = goalListObj->_goalList.GetGoal(i);

        // Is the goal met / has it failed
        bool isMet = goalInst->IsMet();
        bool hasFailed = goalInst->HasFailed();

        // Met indicator: show if met, hide if not
        goals._data[i].imageComplete->SetFlagsOnOff(isMet ? 0 : 1, isMet ? 1 : 0);

        // Failed indicator: show if failed, hide if not
        goals._data[i].imageFail->SetFlagsOnOff(hasFailed ? 0 : 1, hasFailed ? 1 : 0);
    }
}

// ============================================================
// GoalDialog::OnUpdate (vtable +0x38) — no-op handler
// ============================================================
// 3-byte no-op: XOR EAX,EAX; RET. Returns 0.
// Original: 0x140069800.

void GoalDialog::OnUpdate(const System::Time& /*time*/) {}

// ============================================================
// GoalDialog::OnElementEvent (vtable +0x40) — OnCommand
// ============================================================
// When msg==0x12 (button click) and id==100 (buttonOk):
//   Wraps self in InternalBasePtr, calls UserInterface::CloseDialog.
//
// Original: 0x1400697C0.

void GoalDialog::OnElementEvent(Object::InternalPtr<UI::Element>* /*element*/, UI::EventId param3, int32_t param4, int32_t /*c*/, int32_t /*d*/, int32_t /*e*/, void* /*userData*/) {
    if (param3 == UI::EventId::Activated && param4 == 100) {
        Object::InternalPtr<UI::DialogInstance> selfIBP(this);
        _ui->CloseDialog(&selfIBP);
    }
}

// ============================================================
// Construction, destruction, reflection (ENDGAME B.3.8 block DLG)
// ============================================================

// The member Array zeroes its header (retail's two qword zero stores).
GoalDialog::GoalDialog() {}

// MainMenuDialog's opener inlines this ctor: DialogInstance(ui, dialog,
// nullParent, 0, 0, 0), then the fields zeroed.
GoalDialog::GoalDialog(UI::UserInterface* ui, const UI::Dialog* dialog,
             Object::InternalPtr<UI::DialogInstance> parent)
    : UI::DialogInstance(ui, dialog, parent, nullptr, nullptr, 0) {}

// Retail's dtor (inside [0] 0x140011c10) is the member ~Array: MemFree the row
// buffer and zero the header, then ~DialogInstance (GoalDialog.h).

// create 0x140011b60: MemAlloc(0xc8, 8), then the ctor.
::Object::Base* GoalDialog::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(GoalDialog), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(::new (p) GoalDialog());
}
// createInPlace 0x140011bc0 (the tree's in-place build is kept, B8-13).
::Object::Base* GoalDialog::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(::new (storage) GoalDialog());
}

// The reflection record stays at the TU position of the UI_DIALOG_STUB row it
// replaces (ui_dialog_stub_factories.cpp, B38 D4).
