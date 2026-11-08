// src/game/Tutorial/tutorial_wave.cpp
//
// The second half of the Tutorial ResolveAddr gap: 12 exe-internal helpers the
// already-ported Tutorial module resolves by address and which are registered
// NOWHERE, so every one of them is a null function pointer.
//
// The eight from docs/history/notes/wave_remaining/lane2_tutorial.txt:
//   0x1400262B0  ExternalPtr<Tutorial>::Serialize        (fn_ExternalPtr_SerializeTutorial)
//   0x140028650  ExternalPtr<UI::Dialog>::Serialize      (fn_SerializeExternalPtr)
//   0x1400E1DB0  FinishTutorialInstance step driver      (fn_TutorialStepExecute)
//   0x1400E2330  TutorialSetupDialog::SetAllDialogFlags  (fn_TSD_SetDialogFlags)
//   0x1400E24F0  TutorialSetupDialog::FindHighlight      (fn_TSD_FindInstance)
//   0x1400E2700  TutorialSetupDialog::OpenHighlight      (fn_TSD_OpenDialog)
//   0x1400E29F0  TutorialSetupDialog::ShowListed         (fn_TSD_Slot9Helper)
//   0x1401001F0  FinishTutorialGoal tutorial-tree insert (fn_TutorialTreeInsert)
//
// plus four sub-callees of those that are equally unregistered and were not on
// the list (closure verified in Ghidra with get_function_callees on every one):
//   0x1400E2250  recursive element show/hide             (called by 2330)
//   0x14001AF90  ExternalRef copy-construct              (called by 2700)
//   0x14001AFE0  ExternalRef resolve + extract           (called by 2700)
//   0x1400E1260  FinishTutorialInstance step->UI refresh (called by 1DB0)
//
// Every address above was verified as a real function ENTRY with
// get_function_by_address (Entry == address) — the mid-function-resolve trap
// ([[reference-midfunction-raw-address-resolves]]) does not apply to any of them.
//
// STILL DARK, deliberately (see the report):
//   0x1400E0D70  "tutorial finished -> launch the scenario" transition. Its own
//                closure pulls in four more unported exe functions
//                (0x140013A60 / 0x140013AA0 / 0x140098060 GameUpdateLoop ctor /
//                0x140099130). TutWave_StepExecute logs and returns instead of
//                calling a null pointer, and says so.
// (0x1400E1870 FindUIElementByPath is now ported — see
//  src/game/Tutorial/find_ui_element_by_path.cpp. Both call sites here,
//  ShowListed and OpenHighlight's by-path branch, are live.)
//
// Decompiled from Application-x64.exe; control flow checked against the
// disassembly, not just the Ghidra pseudo-C, for 0x1400E01D0 / 0x1400E1260 /
// 0x1400E2700 where the decompiler mis-renders the stack arguments.

#include "Component/exe_bodies.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "system/String.h"
#include "system/MemoryAllocator.h"
#include "object/TypeInfo.h"
#include "Dialog/exe_bodies.h"   // 0x140014890
#include "Object/factory_registration.h"
#include "Tutorial/TutorialSetupRestrictPlayer.h"
#include "Tutorial/TutorialHighlightDialog.h"
#include "Tutorial/TutorialUI.h"
#include "Game/PlayerToolManager.h"
#include "ui/UserInterface.h"
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <new>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "object/External.h"   // ENDGAME B.3.6 UI callers (K4)
#include "ui/DialogInstance.h"   // ENDGAME B.3.6 UI callers (K4)
#include "ui/Element.h"        // ENDGAME B.3.6 UI callers (K4)
#include "ui/String.h"         // ENDGAME B.3.6 UI callers (K4)
#include "ui/TextElement.h"    // ENDGAME B.3.6 UI callers (K4)
#include "ui/TableElement.h"   // ENDGAME B.3.6 UI callers (K4)
#include "Tutorial/Tutorial.h"
#include "Tutorial/TutorialInstance.h"
#include "Tutorial/TutorialStep.h"
#include "Tutorial/TutorialSetup.h"
#include "Tutorial/TutorialSetupDialog.h"
#include "Tutorial/TutorialGoal.h"
#include "Tutorial/TutorialImage.h"
#include "Tutorial/exe_bodies.h"
#include "Entity/CitizenBehavior.h"
#include "Component/ComponentDescription.h"
#include "ui/Dialog.h"   // UI::Dialog (TutWave_SerializeDialogRef's ExternalPtr<UI::Dialog>)

namespace {

inline UI::UserInterface* UserInterface() {
    GameUpdateLoop* gw = GameUpdateLoop::_singleton;
    return gw ? gw->m_userInterface : nullptr;
}

// TutorialHighlightDialog type id (retail DAT_1401C6550) — the class's own
// registration (ENDGAME B.3.8: no lazy cache).
uint32_t HighlightTypeId() { return TutorialHighlightDialog::TypeId(); }
// TutorialUI type id (retail DAT_1401C67F0) — the class's own registration
// (ENDGAME B.3.8: no lazy cache). A 0 here makes StepExecute's controller
// narrow fail and the step panel is never bound.
uint32_t TutorialUITypeId() { return TutorialUI::TypeId(); }

// The CitizenBehavior +0x160 template tree (Entity/CitizenBehavior.h): key
// +0x00, entities +0x08, count +0x10, left +0x20, right +0x28.
TemplateBucket* BstFind(TemplateBucket* node, const Object::External* key) {
    while (node) {
        const Object::External* nodeKey = node->element.key;
        if (key < nodeKey)      node = node->left;
        else if (nodeKey < key) node = node->right;
        else                    return node;
    }
    return nullptr;
}

// A System::String is empty when it has no data or its length (data - 8) is 0.
inline bool HasText(const System::String& s) {
    return s._data != nullptr && s.GetLength() != 0;
}

}  // namespace

// ===========================================================================
// 0x1401001F0 — the tutorial's insert into the PlayerToolManager's +0x50
// tracked set (PlayerToolManager::TutorialTreeInsert).
//
// Upsert keyed by the tutorial's setup description. Existing node: active
// byte at +0x10 = 1, dword +0x08 = value, dword +0x0C = 0x20. Absent: build
// that same 0x18-byte record on the stack and hand it to the shared BST
// inserter at 0x140014890 (Avl38SetInsert, Dialog/exe_bodies.h).
// ===========================================================================
void PlayerToolManager::TutorialTreeInsert(ComponentDescription* tutorialId,
                                void* /*param3*/, uint32_t value) {
    const void* key = tutorialId;

    // Walk. The node is PinnedToolNode (Game/PlayerToolManager.h), keyed by
    // address.
    PinnedToolNode* node = pinnedToolDialogs.root;
    while (node) {
        const void* nodeKey = node->key;
        if (key < nodeKey) { node = node->left; continue; }
        if (nodeKey < key) { node = node->right; continue; }
        node->active = 1;
        node->x      = static_cast<int32_t>(value);
        node->y      = 0x20;
        return;
    }

    PinnedToolEntry rec;
    rec.key    = tutorialId;
    rec.x      = static_cast<int32_t>(value);
    rec.y      = 0x20;
    rec.active = 1;
    for (int i = 0; i < 7; ++i) rec._pad11[i] = 0;

    Avl38SetInsert(&pinnedToolDialogs, rec);   // 0x140014890
}

// ===========================================================================
// 0x1400E2250 — recursive element show/hide.
//
// SetFlagsOnOff(elem, show ? 0 : 0x1000000, show ? 0x1000000 : 0), then recurse
// into every child that resolves non-null. 0x1000000 is UI::kElementFlagSystemDisabled.
// The double Child() call is retail's, not a transcription slip: the first
// fetch is only a null test, the second is the one that is recursed into, and
// ChildCount is re-read every iteration because a child may add siblings.
// ===========================================================================
void TutorialSetupDialog::SetElementFlagsRecursive(Object::InternalPtr<UI::Element>* elemIBP, char show) {
    if (!elemIBP) return;

    UI::Element* elem = elemIBP->Get();
    if (!elem) return;

    uint32_t hideFlags = show ? 0u : static_cast<uint32_t>(UI::kElementFlagSystemDisabled);
    uint32_t showFlags = show ? static_cast<uint32_t>(UI::kElementFlagSystemDisabled) : 0u;
    elem->SetFlagsOnOff(hideFlags, showFlags);

    int i = 0;
    int n = elemIBP->Get()->ChildCount();
    while (i < n) {
        Object::InternalPtr<UI::Element> probe = elemIBP->Get()->Child(i);
        if (probe.Get() != nullptr) {
            Object::InternalPtr<UI::Element> child = elemIBP->Get()->Child(i);
            SetElementFlagsRecursive(&child, show);
        }
        ++i;
        n = elemIBP->Get()->ChildCount();
    }
}

// ===========================================================================
// 0x1400E2330 — show/hide EVERY open dialog's element tree.
//
// This is the "restrict the player to what the tutorial step allows" mechanic:
// with show=0 it hides the entire UI, and ShowListed (0x1400E29F0) then unhides
// only the paths named in the step's first string vector.
// ===========================================================================
void TutorialSetupDialog::SetAllDialogFlags(char show) {
    UI::UserInterface* ui = UserInterface();
    if (!ui) return;

    // `_dialogs.GetFirst()` IS retail's GetFirstDialog (0x180031150, which is
    // literally `*retval = this->_dialogs._head`); the iterator is the bare node
    // pointer, so `it.node` is what retail hands to CheckPtr.
    int dialogs = 0;
    for (auto it = ui->_dialogs.GetFirst(); it; ++it) {
        UI::DialogInstance* dialog = it.node->element;
        UI::Element* root = dialog->GetRoot().Get();
        if (root) {
            // Retail applies the flags to the root itself and then walks the
            // children — the root pass is inlined here, the children go through
            // the shared recursive helper (same code, same order).
            uint32_t hideFlags = show ? 0u : static_cast<uint32_t>(UI::kElementFlagSystemDisabled);
            uint32_t showFlags = show ? static_cast<uint32_t>(UI::kElementFlagSystemDisabled) : 0u;
            root->SetFlagsOnOff(hideFlags, showFlags);

            int i = 0;
            int n = root->ChildCount();
            while (i < n) {
                Object::InternalPtr<UI::Element> probe = root->Child(i);
                if (probe.Get() != nullptr) {
                    Object::InternalPtr<UI::Element> child = root->Child(i);
                    SetElementFlagsRecursive(&child, show);
                }
                ++i;
                n = root->ChildCount();
            }
        }
        ++dialogs;
    }
}

// ===========================================================================
// 0x1400E24F0 — find the currently open TutorialHighlightDialog.
//
// Early-out: if the setup lacks a highlight target path (+0x40, an empty
// System::String reads its length at ptr-8) or a resolved Dialog External
// (+0x50), it resolves the cached External at +0x58 and gives up when that is
// null too (0x1400e24fa: TEST RAX,RAX; JZ / CMP [RAX-8],0; JZ / CMP [RCX+0x50],0;
// JNZ -> scan). Otherwise it scans the open-dialog list for one whose IsA
// matches the TutorialHighlightDialog type id.
// ===========================================================================
Object::InternalPtr<UI::DialogInstance> TutorialSetupDialog::FindHighlightDialog() {
    bool havePath = HasText(HighlightPath());
    bool haveDialog = (highlightDialog != nullptr);
    if (!havePath || !haveDialog) {
        // FUN_14001B1D0 (ResolveExternalByName_Impl).
        toolbarRef.ResolveDescription();
        if (toolbarRef.resolved == nullptr) return nullptr;
    }

    UI::UserInterface* ui = UserInterface();
    if (!ui) return nullptr;
    uint32_t tid = HighlightTypeId();
    if (!tid) return nullptr;   // type not registered yet — a safe no-match

    // Same list, same order as retail's GetFirstDialog walk (see above).
    for (auto it = ui->_dialogs.GetFirst(); it; ++it) {
        UI::DialogInstance* dialog = it.node->element;
        if (dialog->IsA(tid)) {
            return dialog;
        }
    }
    return nullptr;
}

// ===========================================================================
// 0x14001AF90 — copy-construct a NamedExternalRef {String, generation, cached*}
//               (B4 Y GAMEA: NamedExternalRef's copy ctor, Goal/goal_exe_helpers.h).
// 0x14001AFE0 — resolve one and hand back the cached External*; it takes the
//               reference BY VALUE, so the copy's String dies with the call.
// Used as a pair by OpenHighlightDialog to look the toolbar External up
// without disturbing the setup's own cached reference.
// ===========================================================================
static const Object::External* TutWave_ExternalRefResolve(NamedExternalRef ref) {
    ref.ResolveDescription();
    return ref.resolved;
}

// ===========================================================================
// 0x1400E2700 — open the tutorial highlight dialog for the current setup.
//
// Two branches, both ending in MemAlloc(0xF0) + TutorialHighlightDialogCtor +
// UserInterface::AddDialog:
//
//  A. TOOLBAR-CELL branch, taken when the setup's cached External at +0x68 is
//     non-null AND the template's entity count exceeds the setup's index
//     (+0x70). The entity comes out of the CitizenBehavior +0x160 tree, keyed
//     by that External: node+0x08 is the entity array, node+0x10 the count.
//     Ctor gets parent=0, +0xE0=0, +0xD8=setup+0x48, +0xE8=entity.
//  B. ELEMENT-PATH branch otherwise: FindUIElementByPath(&elem, setup+0x40,
//     &dialog); bail if the element is not found. Ctor gets parent=dialog,
//     +0xE0=element, +0xD8=setup+0x48, +0xE8=0.
//
// Guard rails that are RETAIL, not ours: the same +0x40/+0x50/+0x58/+0x68
// early-out as FindHighlightDialog, and a hard skip while
// UserInterface::GetAddDialogCount() > 0 (never stack a highlight on a frame
// that is already adding dialogs). `checkExisting` non-zero additionally skips
// when a highlight dialog is already open.
// ===========================================================================
void TutorialSetupDialog::OpenHighlightDialog(char checkExisting) {
    // 0x1400e272a: no path, an empty path or no Dialog -> the toolbar fallback.
    bool havePath = HasText(HighlightPath());
    bool haveDialog = (highlightDialog != nullptr);
    if (!havePath || !haveDialog) {
        toolbarRef.ResolveDescription();
        if (toolbarRef.resolved == nullptr) return;
    }

    UI::UserInterface* ui = UserInterface();
    if (!ui) return;
    if (ui->GetAddDialogCount() > 0) return;

    Object::InternalPtr<UI::DialogInstance> existing = FindHighlightDialog();
    if (checkExisting != 0 && existing.Get() != nullptr) return;

    toolbarRef.ResolveDescription();

    ComponentEntity* targetElem = nullptr;          // -> ctor arg that lands at +0xE8 (branch A)
    UI::DialogInstance* parentIBP = nullptr;        // -> ctor R9 parent
    Object::InternalPtr<UI::Element> elemIBP;       // -> ctor arg that lands at +0xE0 (branch B)
    bool     branchA    = false;

    if (toolbarRef.resolved != nullptr) {
        GameUpdateLoop* gw = GameUpdateLoop::_singleton;
        CitizenBehavior* mgr = gw ? gw->m_citizenList : nullptr;
        TemplateBucket* root = mgr ? mgr->entitiesByTemplate._root : nullptr;

        const Object::External* key = TutWave_ExternalRefResolve(toolbarRef);

        TemplateBucket* found = BstFind(root, key);
        int count = found ? found->element.entities._size : 0;
        int index = toolbarIndex;

        if (TutorialSetupDialog::ToolbarCellInRange(count, index)) {
            const Object::External* key2 = TutWave_ExternalRefResolve(toolbarRef);
            TemplateBucket* found2 = BstFind(root, key2);
            if (found2 && found2->element.entities._data) {
                targetElem = found2->element.entities._data[index].Get();
            }
            branchA = true;
        }
    }

    if (!branchA) {
        // Branch B: FindUIElementByPath (exe 0x1400E1870).
        FindUIElementByPath_Impl(&elemIBP, &HighlightPath(), &parentIBP);
        if (elemIBP.Get() == nullptr) return;
    }

    // MemAlloc(0xF0) + TutorialHighlightDialog::TutorialHighlightDialog
    // (0x1400E01D0) — the real class now (ENDGAME B.3.8).
    void* mem = System::MemoryAllocator::MemAlloc(sizeof(TutorialHighlightDialog), 8);
    TutorialHighlightDialog* dlg = nullptr;
    if (mem) {
        int32_t valD8 = arrowRotation;
        const UI::Dialog* dialog = highlightDialog;
        if (branchA) {
            dlg = ::new (mem) TutorialHighlightDialog(ui, dialog,
                                                      Object::InternalPtr<UI::DialogInstance>(),
                                                      nullptr, valD8,
                                                      targetElem);
        } else {
            dlg = ::new (mem) TutorialHighlightDialog(ui, dialog,
                                                      Object::InternalPtr<UI::DialogInstance>(parentIBP),
                                                      elemIBP.Get(), valD8,
                                                      nullptr);
        }
    }

    Object::InternalPtr<UI::DialogInstance> dlgIBP(dlg);
    ui->AddDialog(&dlgIBP);
}

// ===========================================================================
// 0x1400E29F0 — hide everything, then unhide the paths this setup names.
//
// The first string vector (+0x20 data / +0x28 count) holds element paths;
// each resolves through FindUIElementByPath and is re-shown recursively. This
// is the whole "the tutorial greys out the UI except the button it wants you
// to press" effect.
// ===========================================================================
void TutorialSetupDialog::ShowListedElements() {
    SetAllDialogFlags('\0');

    int count = shownPaths.GetSize();
    if (count <= 0) return;

    System::String* arr = shownPaths.GetData();
    if (!arr) return;
    for (int i = 0; i < count; ++i) {
        Object::InternalPtr<UI::Element> elem;
        FindUIElementByPath_Impl(&elem, &arr[i], nullptr);
        if (elem.Get() != nullptr) {
            Object::InternalPtr<UI::Element> copy = elem;
            SetElementFlagsRecursive(&copy, '\x01');
        }
    }
}

// ===========================================================================
// 0x1400E1260 — push the current TutorialStep into the TutorialUI panel.
//
// Ported from the disassembly (0x1400E1260-0x1400E1513); the Ghidra decompile
// renders the two UI::String constructions with different arity, which is wrong
// — both are the (retbuf, ExternalPtr<StringTable>, String const&) overload
// through the same dispatch slot 0x140151300.
//
//   step = tutorial->steps[self->stepIndex]
//   1. every setup in step (+0x50 data / +0x58 count): vtable slot 8
//   2. preText  = UI::String(step+0x28 stringTable, step+0x30) -> ui+0x38
//      postText = UI::String(step+0x28 stringTable, step+0x38) -> ui+0x40
//   3. each text element is shown iff its own resolved UI::String
//      (element+0xD8/+0xE0) IsValid
//   4. the image table at ui+0x48 is emptied and re-grown to its previous child
//      count with null children
//   5. every image in step (+0x40 data / +0x48 count): vtable slot 9 builds an
//      element against the dialog at ui+0x18, placed with
//      TableElement::SetChild(elem, image+0x2C, image+0x28)
// The text elements are UI::TextElements and the image table a
// UI::TableElement (TutorialUI stores them as Elements; retail calls the
// subclass members on them directly).
// ===========================================================================
void TutorialInstance::RefreshStepUI() {
    // 0x1400e126b: the Tutorial (+0x30), its steps and the step are read with no test.
    Tutorial* tutorial = tutorialDef;
    // NOT RETAIL (review F-115): StepAt refuses a currentStep outside the steps array. Retail
    // reads steps[currentStep] unbounded, and a loaded save reaches this read before
    // StepExecute's own `stepIndex >= stepCount` test (its first tick after a load runs here).
    TutorialStep* step = StepAt(tutorial->steps, currentStep);
    if (step == nullptr)
        return;

    // 1. setup objects — vtable slot 8 (+0x40)
    {
        int n = step->setups.GetSize();
        TutorialSetup** data = step->setups.GetData();
        for (int i = 0; i < n; ++i) {
            data[i]->Begin();   // slot 8
        }
    }

    // 0x1400e12b7: the TutorialUI controller (+0x20) and its elements, no test.
    TutorialUI* ui = stepUI;

    // 2/3. the two text elements
    {
        UI::StringTable* stringTable = step->stringTable;

        const struct { System::String* srcString; UI::Element* elem; } texts[2] = {
            { &step->titleStr, ui->preText },
            { &step->descStr,  ui->postText },
        };
        for (const auto& t : texts) {
            UI::String tmp(Object::ExternalPtr<UI::StringTable>(stringTable), *t.srcString);
            static_cast<UI::TextElement*>(t.elem)->SetText(&tmp);
        }
        for (const auto& t : texts) {
            UI::Element* elem = t.elem;
            UI::String cur = static_cast<UI::TextElement*>(elem)->GetText();
            bool valid = cur.IsValid();
            elem->SetFlagsOnOff(valid ? 0u : 1u, valid ? 1u : 0u);
        }
    }

    // 4. clear + re-grow the image table
    UI::Element* table = ui->imageTable;
    {
        int n = table->ChildCount();
        table->RemoveChildren();
        for (int i = 0; i < n; ++i) {
            Object::InternalPtr<UI::Element> nullChild;
            table->AddChild(&nullChild);
        }
    }

    // 5. images — vtable slot 9 (+0x48) builds each element; the dialog is
    // re-read per image (0x1400e149f: [[ui+0x18]+0x18]).
    {
        int n = step->images.GetSize();
        TutorialImage** data = step->images.GetData();
        for (int i = 0; i < n; ++i) {
            TutorialImage* img = data[i];
            int32_t col = img->imageWidth;    // +0x28
            int32_t row = img->imageHeight;   // +0x2C
            const UI::Dialog* dialog = ui->_dialog->_dialog;
            Object::InternalPtr<UI::Element> retbuf;
            // slot 9, retslot shape (RCX this, RDX ret, R8 dialog)
            Object::InternalPtr<UI::Element>* elemPtr = img->CreateElement(&retbuf, dialog);
            static_cast<UI::TableElement*>(table)->SetChild(elemPtr, row, col);
        }
    }
}

// ===========================================================================
// 0x1400E1DB0 — the FinishTutorialInstance per-tick driver. This is the heart
// of the tutorial: it binds the TutorialUI controller on the first tick, ticks
// the current step's setups, polls its goals, and advances when all goals pass.
//
//   if self+0x18 (dialog IBP) is empty:
//       find the dialog hosting the tutorial's setup description
//         (PlayerToolManager::FindUtilityDialog, exe 0x140100270)
//       self+0x20 = that dialog's controller, if it IsDerivedFrom TutorialUI
//       RefreshStepUI(self)
//   step = tutorial->steps[self->stepIndex]
//   for each setup:  vtable slot 9 (+0x48)(setup, param2)
//   for each goal:   passed += vtable slot 8 (+0x40)(goal, param2, &selfIBP)
//   if passed == goalCount:
//       for each setup: vtable slot 10 (+0x50)(setup)
//       ++stepIndex
//       if more steps: RefreshStepUI(self)
//       else:          <tutorial complete>  (LoadNextScenario below)
// ===========================================================================
void TutorialInstance::StepExecute(const System::Time* param2, const Achievement::GoalList* param3, uint64_t param4) {
    // 0x1400e1dcc: the Tutorial (+0x30) is read with no test.
    Tutorial* tutorial = tutorialDef;

    if (stepDialog == nullptr) {
        UI::DialogInstance* dlg = PlayerToolManager::FindUtilityDialog(tutorial->setup);   // 0x140100270
        stepDialog = dlg;
        if (dlg == nullptr) {
            // Retail returns silently (0x1400e1de8); one line, once, says why the panel stays empty.
            static bool once = false;
            if (!once) { once = true;
                fprintf(stderr, "[Tutorial] StepExecute BAILED: FindDialogByControllerType found no "
                                "dialog for tutorial=%p dialogExt=%p\n",
                        static_cast<void*>(tutorial),
                        static_cast<const void*>(tutorial->setup));
                fflush(stderr); }
            return;
        }

        uint32_t uiTid = TutorialUITypeId();
        TutorialUI* controller = nullptr;
        {
            // Third arg is a TYPE FILTER, not padding: retail is
            //   MOV R8D,[0x1401c67f0]   ; TutorialUI type id
            //   CALL [0x140151768]      ; DialogInstance::GetController
            // (0x1400e1e08). Ghidra renders the call as two arguments and hides it.
            // Passing 0 made GetController return NOTHING, so the controller was null,
            // the TutorialUI narrow below could not run, and RefreshStepUI had no panel
            // to write into — the tutorial panel rendered as a bare title and two "Ok"
            // rows. Measured as `ctrl=0 tid=0 (?) wantTutorialUI=857087488`.
            UI::DialogController* ctrl = dlg->GetController(uiTid).Get();
            if (ctrl && uiTid && Object::IsDerivedFromNullSafe(ctrl, uiTid)) {
                controller = static_cast<TutorialUI*>(ctrl);
            }
        }
        stepUI = controller;
        RefreshStepUI();
    }

    int stepIndex = currentStep;
    int stepCount = tutorial->steps._size;

    if (stepIndex >= stepCount) return;   // 0x1400e1e98: CMP EAX,[RBX+0x30]; JGE

    // NOT RETAIL (review F-115): the JGE above is signed, so a negative currentStep from a
    // save passes it and retail reads before the steps array; StepAt refuses it.
    TutorialStep* step = StepAt(tutorial->steps, stepIndex);
    if (step == nullptr)
        return;

    // setups: slot 9
    {
        int n = step->setups.GetSize();
        TutorialSetup** data = step->setups.GetData();
        for (int i = 0; i < n; ++i) {
            data[i]->Update(param2);   // slot 9
        }
    }

    // goals: slot 8, counts the ones that report complete
    int passed = 0;
    int goalCount = step->goals.GetSize();
    {
        TutorialGoal** data = step->goals.GetData();
        for (int i = 0; i < goalCount; ++i) {
            Object::InternalPtr<TutorialInstance> selfIBP(this);
            // slot 8
            bool ok = data[i]->IsComplete(param2, &selfIBP);
            if (ok) ++passed;
        }
    }

    // 0x1400e1f50: CMP R12D,[RBX+0x68]; JNZ. A step with no goals passes at once.
    if (passed != goalCount) return;

    // step complete: slot 10 on every setup, once each, in index order
    // (0x1400e1f80: MOV RCX,[RDI+RAX]; CALL [RAX+0x50]; INC ESI; LEA RDI,[RDI+8]).
    {
        int n = step->setups.GetSize();
        TutorialSetup** data = step->setups.GetData();
        for (int i = 0; i < n; ++i) {
            data[i]->End();   // slot 10
        }
    }

    currentStep = stepIndex + 1;

    if (stepIndex + 1 < stepCount) {
        RefreshStepUI();
        return;
    }

    // Tutorial finished: toggle the tutorial's utility dialog (closes the open
    // tutorial window and clears its pinned-set open flag), then boot the next
    // scenario (tutorial_next_scenario.cpp).
    //   1400e1fbf: MOV RCX,[0x1401c3dd8]; MOV R8,[R8+0x20]; MOV RCX,[RCX+0x120]
    //   1400e1fd6: CALL 0x140101a80   ; PlayerToolManager::ToggleUtilityDialog(setup)
    //   1400e1fde: CALL 0x1400e0d70   ; LoadNextScenario
    (void)param3;
    (void)param4;
    GameUpdateLoop::_singleton->m_toolManager->ToggleUtilityDialog(tutorialDef->setup);
    LoadNextScenario();
}
