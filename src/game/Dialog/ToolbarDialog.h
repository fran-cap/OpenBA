#pragma once
// ============================================================================
// src/game/Dialog/ToolbarDialog.h — ENDGAME B.3.8 block DLG
//
// The in-game toolbar (Dialog/Toolbar.rsc) and each sub-toolbar it opens.
//
// RETAIL (Application-x64.exe, B38_SLOTS.md section 6):
//   reflected "ToolbarDialog", parent "UI::DialogInstance", ti 0x1401c5d38
//   create 0x140021710 (MemAlloc(0x108, 8)), ctor 0x1400b9910
//   table 0x14015fa00, 10 slots:
//     [0] 0x140021760 scalar deleting dtor (frees +0xF8, +0xD0, +0xC0; ~DialogInstance)
//     [1] TypeName  [2] Type  [3] DialogInstance::Persist (import)
//     [4] 0x1400b97c0 OnOpened (RET)
//     [5] 0x1400b97b0 OnClosed (JMP CloseSubDialog 0x1400b9730; no SaveSettings)
//     [6] 0x1400b8d40 OnUpdateLayout (sync the buttons with the game state)
//     [7] DialogInstance::OnUpdate (import)
//     [8] 0x1400b92a0 OnElementEvent (the button clicks)
//     [9] DialogInstance::OnSerializeSettings (import)
//   sizeof 0x108.
// ============================================================================

#include <cstddef>
#include <cstdint>
#include "ui/DialogInstance.h"
#include "Object/exe_type.h"
#include "collection/Collection.h"

class Toolbar;   // Game/Toolbar.h
class CitizenBehavior;   // Entity/CitizenBehavior.h
namespace UI { class RadioElement; class Element; }

class ToolbarDialog final : public UI::DialogInstance {
    DECLARE_EXE_TYPE(ToolbarDialog)

public:
    ToolbarDialog();                                                // create 0x140021710
    ToolbarDialog(UI::UserInterface* ui, const UI::Dialog* dialog,
                  Object::InternalPtr<UI::DialogInstance> parent,
                  const Toolbar* group);                            // 0x1400b9910

    ~ToolbarDialog() override;                                     // [0] 0x140021760
    void OnOpened() override;                                      // [4] 0x1400b97c0
    void OnClosed() override;                                      // [5] 0x1400b97b0
    void OnUpdateLayout() override;                                // [6] 0x1400b8d40
    void OnElementEvent(Object::InternalPtr<UI::Element>* element,
                        UI::EventId a, int32_t b, int32_t c,
                        int32_t d, int32_t e, void* userData) override;  // [8] 0x1400b92a0

    // FUN_1400b9730: close the open sub-toolbar dialog and reset the radio. Not a
    // slot: slot 5 is a JMP to it and button case 5 calls it on the parent.
    void CloseSubDialog();

    // FUN_1400b91b0: step the game speed (the tool manager's +0x34) one entry of
    // speedValues up (+1) or down (-1), clamped. Called by OnElementEvent.
    void ChangeSpeed(int direction);

    // FUN_1400b8fd0: enable the ShowGroup (1) / FocusEntity (4) buttons from
    // the town's per-template counts. The tail of OnUpdateLayout.
    void UpdateButtons();

public:
    UI::RadioElement* radioElement;       // +0xB8: the "toolBar" RadioElement (id 1000)
    // +0xC0 / +0xD0 / +0xF8: three Collection::Arrays; the dtor 0x140021760
    // frees +0xF8, +0xD0, +0xC0 in that order (the members' reverse order).
    Collection::Array<UI::Element*> buttonElements;  // +0xC0: one cloned button per item
    Collection::Array<const Toolbar*> toolbarItems;  // +0xD0: the Toolbar items shown, by priority
    const Toolbar* toolbarGroup; // +0xE0: the parent item whose group this (sub-)toolbar shows (null = the main bar)
    Object::InternalPtr<UI::DialogInstance> subDialog;  // +0xE8: the open sub-toolbar
    int32_t  openSubIndex;      // +0xF0: -1 = none
    int32_t  currentSpeedIndex; // +0xF4
    Collection::Array<int32_t> speedValues;          // +0xF8: the game speeds {1, 2, 5, 10, ...}
};

static_assert(offsetof(ToolbarDialog, radioElement) == 0xB8, "ToolbarDialog +0xb8");
static_assert(offsetof(ToolbarDialog, buttonElements) == 0xC0, "ToolbarDialog +0xc0");
static_assert(offsetof(ToolbarDialog, toolbarItems) == 0xD0, "ToolbarDialog +0xd0");
static_assert(offsetof(ToolbarDialog, toolbarGroup) == 0xE0, "ToolbarDialog +0xe0");
static_assert(offsetof(ToolbarDialog, subDialog) == 0xE8, "ToolbarDialog +0xe8");
static_assert(offsetof(ToolbarDialog, openSubIndex) == 0xF0, "ToolbarDialog +0xf0");
static_assert(offsetof(ToolbarDialog, currentSpeedIndex) == 0xF4, "ToolbarDialog +0xf4");
static_assert(offsetof(ToolbarDialog, speedValues) == 0xF8, "ToolbarDialog +0xf8");
static_assert(sizeof(ToolbarDialog) == 0x108, "ToolbarDialog is 0x108 (create 0x140021710)");
static_assert(alignof(ToolbarDialog) == 8, "ToolbarDialog is qword-aligned");

// How many entities the town has built from `tmpl` (a toolbar item's template): the count the toolbar's own
// buttons test against a ToolbarDescription's limit. 0 for a template the town has none of.
int32_t Toolbar_BuiltCount(const CitizenBehavior* town, const void* tmpl);

