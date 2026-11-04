#pragma once
// ============================================================================
// src/game/Tutorial/TutorialSetupDialog.h — ENDGAME B.3.8 block GOAL
//
// The tutorial setup that greys the UI out except the elements a step names,
// and opens a TutorialHighlightDialog on the one it wants clicked. NOT a
// DialogInstance — an Object::External loaded from Tutorial.rsc.
//
// RETAIL (Application-x64.exe, B38_SLOTS.md section 6):
//   reflected "TutorialSetupDialog", parent "TutorialSetup", ti 0x1401c6710
//   create 0x1400285b0 (MemAlloc(0x78, 8)), createInPlace 0x1400285f0
//   ctor 0x1400e2cc0
//   table 0x140160d20, 12 slots:
//     [0] 0x140028620 scalar deleting dtor: the dtor 0x1400e2bf0 (stamp,
//         ~String +0x58, ~String +0x40, ~Array<String> +0x30 and +0x20,
//         stamp TutorialSetup, ~External), MemFree on &1
//     [1] TypeName  [2] Type  [3] 0x1400e2be0 GetFileVersion `MOV EAX,6`
//     [4] External::Compile (inherited)  [5] 0x1400e2a60 Serialize
//     [6] 0x140028600 Reconstruct  [7] 0x140001640 Destruct
//     [8] 0x1400e25f0 Begin  [9] 0x1400e24c0 Update  [10] 0x1400e2460 End
//     [11] 0x1400285f0 Construct
//   sizeof 0x78.
//
// The helpers the slots call (show-listed 0x1400e29f0, open-highlight
// 0x1400e2700, set-all-flags 0x1400e2330, find-highlight 0x1400e24f0) are
// ported in tutorial_wave.cpp and reached through engine_funcs' fn_TSD_*.
// ============================================================================

#include <cstddef>
#include <cstdint>
#include "Tutorial/TutorialSetup.h"
#include "Object/exe_type.h"
#include "Goal/goal_exe_helpers.h"   // NamedExternalRef
#include "object/Internal.h"         // Object::InternalPtr
#include "collection/Collection.h"   // Collection::Array
#include "system/String.h"

namespace System { class String; }
namespace UI { class Dialog; class DialogInstance; class Element; }

class TutorialSetupDialog final : public TutorialSetup {
    DECLARE_EXE_TYPE(TutorialSetupDialog)

public:
    TutorialSetupDialog();                                         // 0x1400e2cc0

    // 0x1400e2bf0
    ~TutorialSetupDialog() override;                               // [0] 0x140028620
    int32_t           GetFileVersion() const override { return 6; }// [3] 0x1400e2be0
    bool              Serialize(IO::Stream& stream) override;            // [5] 0x1400e2a60
    Object::External* Reconstruct() override;                      // [6] 0x140028600
    void              Destruct() override;                         // [7] 0x140001640
    void              Begin() override;                            // [8] 0x1400e25f0
    void              Update(const System::Time* time) override;                 // [9] 0x1400e24c0
    void              End() override;                              // [10] 0x1400e2460
    virtual Object::External* Construct();                         // [11] 0x1400285f0

    // ---- non-virtual members (tutorial_wave.cpp) ----
    // 0x1400e2250: show / hide an element tree (retail passes this through unread).
    void SetElementFlagsRecursive(Object::InternalPtr<UI::Element>* elem, char show);
    void SetAllDialogFlags(char show);                             // 0x1400e2330
    Object::InternalPtr<UI::DialogInstance> FindHighlightDialog(); // 0x1400e24f0
    void OpenHighlightDialog(char checkExisting);                  // 0x1400e2700
    void ShowListedElements();                                     // 0x1400e29f0

    // OpenHighlightDialog's toolbar-cell test: the template bucket's entity count
    // against toolbarIndex (0x1400e2805: `CMP EAX,[RDI+0x70]; JLE`).
    // NOT RETAIL (review F-114): retail tests `count > index` only; a negative
    // toolbarIndex from a tutorial resource read before the entity array.
    static bool ToolbarCellInRange(int count, int index) { return index >= 0 && count > index; }

    // The +0x40 String. Defined in tutorialsetupdialog.cpp.
    System::String& HighlightPath();

public:
    // Element paths ("Dialog.Element") left visible while everything else is
    // hidden. Retail's ctor 0x1400e2cc0 zeroes +0x20 / +0x28 (Array()); its dtor
    // 0x1400e2bf0 destroys +0x30 then +0x20 after the two Strings, the order the
    // compiler destroys these members in after ~TutorialSetupDialog's body.
    Collection::Array<System::String> shownPaths;    // +0x20
    // Dialog names Begin() walks.
    Collection::Array<System::String> dialogNames;   // +0x30
    System::String highlightPath; // +0x40: the element to highlight (B4 X7-GAME-A, H5: the member; destroyed after the body, before the two Arrays: retail 0x1400e2bf0)
    int32_t  arrowRotation;      // +0x48: degrees, lands at highlight +0xD8
    uint8_t  _pad4C[4];          // +0x4C: padding
    UI::Dialog* highlightDialog;       // +0x50: ExternalPtr<UI::Dialog> — the highlight .rsc (read only; the slot
                                       //        0x140028650 serializes is the External* itself)
    // +0x58..+0x6f: the toolbar template, by name: String, factory generation
    //   (-1 = unresolved), cached External* (resolved by 0x14001b1d0)
    NamedExternalRef toolbarRef;
    int32_t  toolbarIndex;       // +0x70: the toolbar cell to point at
    uint8_t  _pad74[4];          // +0x74: padding
};

static_assert(offsetof(TutorialSetupDialog, shownPaths) == 0x20, "+0x20");
static_assert(offsetof(TutorialSetupDialog, dialogNames) == 0x30, "+0x30");
static_assert(offsetof(TutorialSetupDialog, highlightPath) == 0x40, "+0x40");
static_assert(offsetof(TutorialSetupDialog, arrowRotation) == 0x48, "+0x48");
static_assert(offsetof(TutorialSetupDialog, highlightDialog) == 0x50, "+0x50");
static_assert(offsetof(TutorialSetupDialog, toolbarRef) == 0x58, "+0x58");
static_assert(offsetof(TutorialSetupDialog, toolbarRef) + offsetof(NamedExternalRef, generation) == 0x60, "+0x60");
static_assert(offsetof(TutorialSetupDialog, toolbarRef) + offsetof(NamedExternalRef, resolved) == 0x68, "+0x68");
static_assert(offsetof(TutorialSetupDialog, toolbarIndex) == 0x70, "+0x70");
static_assert(sizeof(TutorialSetupDialog) == 0x78, "TutorialSetupDialog is 0x78 (create 0x1400285b0)");
static_assert(offsetof(TutorialSetupDialog, _pad4C) == 0x4C, "TutorialSetupDialog::_pad4C");
static_assert(offsetof(TutorialSetupDialog, _pad74) == 0x74, "TutorialSetupDialog::_pad74");
static_assert(alignof(TutorialSetupDialog) == 8, "qword-aligned");
