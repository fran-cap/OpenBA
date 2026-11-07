#pragma once
// ============================================================================
// src/game/Dialog/ScenarioDialog.h — ENDGAME B.3.8 block DLG
//
// The Scenario / Tutorial picker (one class; the byte at +0xB8 tells them apart).
//
// RETAIL (Application-x64.exe, B38_SLOTS.md section 6):
//   reflected "ScenarioDialog", parent "UI::DialogInstance", ti 0x1401c5558
//   create 0x140013970 (MemAlloc(0xe8, 8)), createInPlace 0x1400139f0
//   ctors 0x140078650, 0x1400786b0 (the tree's ScenarioDialog_Ctor), 0x140078750
//   table 0x14015d2d0, 10 slots:
//     [0] 0x140013af0 scalar deleting dtor: MemFree(+0xd8), +0xd8/+0xe0 = 0,
//         ~DialogInstance, MemFree on &1
//     [1] TypeName  [2] Type  [3] DialogInstance::Persist (import)
//     [4] 0x140078010 OnOpened  [5] DialogInstance::OnClosed
//     [6] 0x140077be0 OnUpdateLayout  [7] 0x140078000 OnUpdate `RET 0`
//     [8] 0x140077f80 OnElementEvent  [9] DialogInstance::OnSerializeSettings
//   sizeof 0xe8.
// ============================================================================

#include <cstddef>
#include <cstdint>
#include "ui/DialogInstance.h"
#include "collection/Collection.h"
#include "Object/exe_type.h"
class MainMenuDialog;

class Scenario;   // Game/Scenario.h
namespace UI { class Element; class ImageElement; class TextElement; }

// One row of the picker (0x38; OnOpened 0x140078010 builds it, pushes it with
// 0x140013b60): the table row cloned from the dialog's template and the
// scenario behind it.
struct ScenarioDialogEntry {
    UI::Element*      element;          // +0x00 the cloned row, element id 1000 + row
    UI::Element*      selectSave;       // +0x08 "selectSave": shown on the selected row only
    UI::ImageElement* imageIcon;        // +0x10 "imageIcon": the scenario's sprite
    UI::TextElement*  textTitle;        // +0x18 "textTitle"
    UI::TextElement*  textDescription;  // +0x20 "textDescription"
    Scenario*         scenario;         // +0x28 the ScenarioList resource
    int32_t           resourceIndex;    // +0x30 its index in the ScenarioList
    uint8_t           _pad34[4];        // +0x34
};
static_assert(sizeof(ScenarioDialogEntry) == 0x38, "ScenarioDialogEntry stride 0x38");
static_assert(offsetof(ScenarioDialogEntry, element) == 0x0, "ScenarioDialogEntry::element");
static_assert(offsetof(ScenarioDialogEntry, _pad34) == 0x34, "ScenarioDialogEntry::_pad34");
static_assert(offsetof(ScenarioDialogEntry, selectSave) == 0x08, "");
static_assert(offsetof(ScenarioDialogEntry, imageIcon) == 0x10, "");
static_assert(offsetof(ScenarioDialogEntry, textTitle) == 0x18, "");
static_assert(offsetof(ScenarioDialogEntry, textDescription) == 0x20, "");
static_assert(offsetof(ScenarioDialogEntry, scenario) == 0x28, "");
static_assert(offsetof(ScenarioDialogEntry, resourceIndex) == 0x30, "");

class ScenarioDialog final : public UI::DialogInstance {
    DECLARE_EXE_TYPE(ScenarioDialog)

public:
    ScenarioDialog();                                               // create 0x140013970
    ScenarioDialog(UI::UserInterface* ui, const UI::Dialog* dialog,
                   Object::InternalPtr<UI::DialogInstance> parent,
                   MainMenuDialog* parentMenu, uint8_t difficulty);  // 0x1400786b0

    // 0x140013af0's body: the entries Array's dtor (MemFree(+0xd8), +0xd8/+0xe0 = 0)
    ~ScenarioDialog() override = default;                          // [0] 0x140013af0
    void OnOpened() override;                                      // [4] 0x140078010
    // [5] OnClosed: DialogInstance's (SaveSettings), inherited (B38 D3.1).
    void OnUpdateLayout() override;                                // [6] 0x140077be0
    void OnUpdate(const System::Time& time) override;              // [7] 0x140078000 RET
    void OnElementEvent(Object::InternalPtr<UI::Element>* element,
                        UI::EventId a, int32_t b, int32_t c,
                        int32_t d, int32_t e, void* userData) override;  // [8] 0x140077f80

    // 0x140077cb0: start the selected scenario (a new game from its generators,
    // or the game in its save).
    void LoadSelected();

public:
    uint8_t  difficulty;                   // +0xB8: 0 scenario, 1 tutorial (Scenario +0x69)
    uint8_t  _padB9[7];                    // +0xB9
    MainMenuDialog* parentMenu;            // +0xC0: InternalPtr, the MainMenuDialog that opened it (both opening paths pass `this`)
    int32_t  selectedIndex;                // +0xC8: ctor -1
    uint8_t  _padCC[4];                    // +0xCC
    UI::Element* buttonOk;                 // +0xD0: "buttonOk", id 101, enabled with a selection
    Collection::Array<ScenarioDialogEntry> entries;  // +0xD8: the rows
};

static_assert(offsetof(ScenarioDialog, difficulty) == 0xB8, "ScenarioDialog +0xb8");
static_assert(offsetof(ScenarioDialog, parentMenu) == 0xC0, "ScenarioDialog +0xc0");
static_assert(offsetof(ScenarioDialog, selectedIndex) == 0xC8, "ScenarioDialog +0xc8");
static_assert(offsetof(ScenarioDialog, buttonOk) == 0xD0, "ScenarioDialog +0xd0");
static_assert(offsetof(ScenarioDialog, entries) == 0xD8, "ScenarioDialog +0xd8");
static_assert(sizeof(ScenarioDialog) == 0xE8, "ScenarioDialog is 0xe8 (create 0x140013970)");
static_assert(offsetof(ScenarioDialog, _padB9) == 0xB9, "ScenarioDialog::_padB9");
static_assert(offsetof(ScenarioDialog, _padCC) == 0xCC, "ScenarioDialog::_padCC");
static_assert(alignof(ScenarioDialog) == 8, "ScenarioDialog is qword-aligned");

