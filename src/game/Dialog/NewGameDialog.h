#pragma once
// ============================================================================
// src/game/Dialog/NewGameDialog.h — ENDGAME B.3.8 block DLG
//
// The New Game dialog (map / terrain / climate / condition / disaster, name,
// seed). Original source: C:\Projects\Buildy\code\Game\Dialog/NewGameDialog.h
//
// RETAIL (Application-x64.exe, B38_SLOTS.md section 6):
//   reflected "NewGameDialog", parent "UI::DialogInstance", ti 0x1401c52f0
//   create 0x140012c70 (MemAlloc(0xae8, 8)), createInPlace 0x140012cb0
//   ctor 0x140071040 (5 args; Ghidra's "CopyCtor")
//   table 0x14015c920, 10 slots:
//     [0] 0x140012cc0 scalar deleting dtor  [1] TypeName  [2] Type
//     [3] DialogInstance::Persist (import)  [4] 0x1400706d0 OnOpened
//     [5] 0x1400706c0 OnClosed `RET 0`      [6] 0x140070650 OnUpdateLayout
//     [7] DialogInstance::OnUpdate          [8] 0x140070110 OnElementEvent
//     [9] DialogInstance::OnSerializeSettings
//   sizeof 0xae8 (a System::Random at +0x118).
// ============================================================================

#include <cstddef>
#include <cstdint>
#include "ui/DialogInstance.h"
#include "system/Value.h"
#include "Object/exe_type.h"

namespace UI {
class ComboElement; class EditElement; class NumberEditElement;
class ButtonElement; class TextElement;
}
namespace Object { class ExternalList; }
class MainMenuDialog;   // Dialog/MainMenuDialog.h

class NewGameDialog final : public UI::DialogInstance {
    DECLARE_EXE_TYPE(NewGameDialog)

public:
    NewGameDialog();                                                // create 0x140012c70
    NewGameDialog(UI::UserInterface* ui, const UI::Dialog* dialog,
                  Object::InternalPtr<UI::DialogInstance> parent,
                  Object::InternalPtr<MainMenuDialog> parentMenu);  // 0x140071040

    ~NewGameDialog() override = default;                           // [0] 0x140012cc0
    void OnOpened() override;                                      // [4] 0x1400706d0
    void OnClosed() override;                                      // [5] 0x1400706c0 RET
    void OnUpdateLayout() override;                                // [6] 0x140070650
    void OnElementEvent(Object::InternalPtr<UI::Element>* element,
                        UI::EventId a, int32_t b, int32_t c,
                        int32_t d, int32_t e, void* userData) override;  // [8] 0x140070110

    void StartGame();          // OnElementEvent's buttonOk arm (StartNewGame 0x1400b4540 on the menu)
    void UpdateDescription();  // the comboCondition text (OnOpened / OnElementEvent)

public:
    UI::ComboElement*      comboTerrain;         // +0xB8: "comboTerrain"
    UI::ComboElement*      comboTerrainSize;     // +0xC0: "comboTerrainSize"
    UI::ComboElement*      comboClimate;         // +0xC8: "comboClimate"
    UI::ComboElement*      comboCondition;       // +0xD0: "comboCondition" (map generator)
    UI::ComboElement*      comboDisaster;        // +0xD8: "comboDisaster"
    UI::EditElement*       editName;             // +0xE0: "editName"
    UI::NumberEditElement* editSeed;             // +0xE8: "editSeed"
    UI::ButtonElement*     buttonOk;             // +0xF0: "buttonOk"
    UI::TextElement*       textDescription;      // +0xF8: "textDescription"
    Object::ExternalList*  mapGeneratorList;     // +0x100: GameContainer::startConditions
    Object::ExternalList*  climateGeneratorList; // +0x108: GameContainer::climateList
    Object::ExternalList*  terrainGeneratorList; // +0x110: GameContainer::terrainList
    System::Random         random;               // +0x118: ??0Random (to +0xADC)
    uint32_t               _padADC;              // +0xADC: gap
    Object::InternalPtr<MainMenuDialog> mainMenu; // +0xAE0: the owning menu (StartGame builds through it)
};

static_assert(offsetof(NewGameDialog, comboTerrain) == 0xB8, "NewGameDialog +0xb8");
static_assert(offsetof(NewGameDialog, comboTerrainSize) == 0xC0, "NewGameDialog +0xc0");
static_assert(offsetof(NewGameDialog, comboClimate) == 0xC8, "NewGameDialog +0xc8");
static_assert(offsetof(NewGameDialog, comboCondition) == 0xD0, "NewGameDialog +0xd0");
static_assert(offsetof(NewGameDialog, comboDisaster) == 0xD8, "NewGameDialog +0xd8");
static_assert(offsetof(NewGameDialog, editName) == 0xE0, "NewGameDialog +0xe0");
static_assert(offsetof(NewGameDialog, editSeed) == 0xE8, "NewGameDialog +0xe8");
static_assert(offsetof(NewGameDialog, buttonOk) == 0xF0, "NewGameDialog +0xf0");
static_assert(offsetof(NewGameDialog, textDescription) == 0xF8, "NewGameDialog +0xf8");
static_assert(offsetof(NewGameDialog, mapGeneratorList) == 0x100, "NewGameDialog +0x100");
static_assert(offsetof(NewGameDialog, climateGeneratorList) == 0x108, "NewGameDialog +0x108");
static_assert(offsetof(NewGameDialog, terrainGeneratorList) == 0x110, "NewGameDialog +0x110");
static_assert(offsetof(NewGameDialog, random) == 0x118, "NewGameDialog +0x118");
static_assert(offsetof(NewGameDialog, _padADC) == 0xADC, "NewGameDialog +0xadc");
static_assert(offsetof(NewGameDialog, mainMenu) == 0xAE0, "NewGameDialog +0xae0");
static_assert(sizeof(NewGameDialog) == 0xAE8, "NewGameDialog is 0xae8 (create 0x140012c70)");
static_assert(alignof(NewGameDialog) == 8, "NewGameDialog is qword-aligned");

// The dialog MainMenuDialog::OpenNewGame just built, for the auto-new-game
// driver (defined in src/diag/autostart.cpp).
extern "C" NewGameDialog* g_StandaloneNewGameDialog;
