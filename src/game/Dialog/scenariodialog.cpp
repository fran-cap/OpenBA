// engine/src/ui/scenariodialog.cpp
// Clean reimplementation of ScenarioDialog methods.
//
// ScenarioDialog (0xE8 bytes): Scenario selection screen.
// Layout: +0xC8 = selected index (int32), +0xD8 = scenario array,
//         +0xE0 = scenario count

#include "ui/TextElement.h"   // ENDGAME B.3.6 UI callers (K2)
#include "ui/ImageElement.h"   // ENDGAME B.3.6 UI callers (K2)
#include "ui/TableElement.h"   // ENDGAME B.3.6 UI callers (K2)
#include "ui/ElementDescription.h"   // ENDGAME B.3.6 UI callers (K2)
#include "ui/Element.h"   // ENDGAME B.3.6 UI callers (K2)
#include "ui/DialogInstance.h"   // ENDGAME B.3.6 UI callers (K2)
#include "ui/UserInterface.h"   // ENDGAME B.3.6 UI callers (K2)
#include "ui/String.h"   // ENDGAME B.3.6 UI callers (K2)
#include "ui/StringTable.h"   // ENDGAME B.3.6 UI callers (K2)
#include "ui/Sprite.h"   // ENDGAME B.3.6 UI callers (K2)
#include "ui/SpriteSheet.h"   // ENDGAME B.3.6 UI callers (K2)
#include "ui/Dialog.h"   // ENDGAME B.3.6 UI callers (K2)
#include "achievement/AchievementManager.h"   // ENDGAME B.3.7 callers (K1)
#include "save/SaveLoad.h"   // ENDGAME B.3.7 callers (K1)
#include "Dialog/ScenarioDialog.h"
#include "Dialog/MainMenuDialog.h"
#include "Dialog/ui_dialogs.h"
#include "Object/factory_registration.h"
#include "Dialog/dialog_exe_helpers.h"
#include "system/Value.h"
#include "ui/DialogInstance.h"
#include "object/TypeInfo.h"
#include "Game/exe_bodies.h"
#include "Dialog/exe_bodies.h"
#include "Game/game.h"   // B3R STORAGE: g_gameContainer
#include "system/String.h"
#include "system/Filename.h"
#include "io/Persistence.h"   // ENDGAME B.3.4 caller batch P
#include "io/FileSystem.h"    // ENDGAME B.3.4 caller batch P
#include <new>
#include "Object/factory_registration.h"
#include "object/External.h"   // ENDGAME B.3.3 step 6A
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "Game/Scenario.h"   // X::TypeId() (B.3.9 CG2)
#include "ui/ButtonElement.h"
#include "io/Persistence.h"
#include "system/MemoryAllocator.h"   // B3R: the allocator doors

// ============================================================
// ScenarioDialog::Slot4 (vtable +0x20) — Setup
// ============================================================
// Populates the scenario selection table. For each scenario matching
// the current difficulty (self+0xB8), creates a table row with icon,
// title, description, and selection button. Stores 0x38-byte entries
// in the dynamic array at +0xD8.
//
// Original: 0x140078010 (348 instructions, retail).
// Profile: FUN_14005f490 — confirms element names:
//   "buttonCancel", "buttonOk", "tableScenario", "imageIcon",
//   "textTitle", "textDescription", "selectSave"
// Assertion: "table->ChildCount() >= scenarioCount"
// Source: Dialog\ScenarioDialog.cpp, line 0x3B

// Scenario's two Strings (+0x58 the name, +0x88 the save name): System::String
// members since B.4 GAME-b (the tree's ctor / dtor already zeroed / released them).
static const System::String& ScenarioString(const System::String& field) {
    return field;   // GAME-b: Scenario's two Strings are System::String members
}

// The element of that name under `root` (the tree does not narrow it)
static UI::Element* FindIn(UI::Element* root, const char* name) {
    return root->FindElement(System::String(name)).Get();
}

void ScenarioDialog::OnOpened() {
    // The scenario list (container +0x08) was filled at boot by GameContainer_Init
    // 0x140099db0 (InnerLoaderLoop::Load); the GetResourceCount below reads it unguarded.
    printf("[ScenarioDialog::Slot4] Setup called, this=%p, difficulty=%d\n",
           static_cast<void*>(this), difficulty);

    // --- Find "buttonCancel" → set element ID 100, shortcut key 8 ---
    {
        UI::ButtonElement* cancelBtn =
            static_cast<UI::ButtonElement*>(FindElement(System::String("buttonCancel")).Get());
        cancelBtn->SetElementId(100);
        cancelBtn->_keyIndex = 8;
    }

    // --- Find "buttonOk" → store at +0xD0, set ID 0x65 (101), disable it ---
    {
        UI::Element* okBtn = FindElement(System::String("buttonOk")).Get();
        buttonOk = okBtn;
        okBtn->SetElementId(0x65);
        okBtn->Enable(false);
    }

    // --- The scenario list: the game container's ScenarioList.rsc ---
    GameContainer* container = g_gameContainer;
    const Object::ExternalList* scenarioList = container->scenarioList.Get();

    // --- Find "tableScenario" element ---
    UI::TableElement* table =
        static_cast<UI::TableElement*>(FindElement(System::String("tableScenario")).Get());

    // --- Get scenario count and iterate ---
    int scenarioCount = scenarioList->GetResourceCount();
    int entryIndex = 0;  // counts matched scenarios (for button IDs and array)
    printf("[ScenarioDialog::Slot4] scenarioCount=%d, mgmtObj=%p, scenarioList=%p\n",
           scenarioCount, static_cast<void*>(container), static_cast<const void*>(scenarioList));

    for (int i = 0; i < scenarioCount; i++) {
        Object::External* resource = scenarioList->_resources[i];

        // Check IsDerivedFrom(Scenario). ENDGAME B.3.9 CG2: TypeId_Scenario (cell
        // 0x1401c5b08) -> Scenario::TypeId(); the "unregistered" null guard was
        // scaffolding for the unresolved global and goes with the conversion.
        // This rejection used to be silent, which made an empty scenario table
        // indistinguishable from "no scenarios installed". Report it once.
        bool isScenario = ::Object::IsDerivedFromNullSafe(resource, Scenario::TypeId());
        Scenario* scenario = isScenario ? static_cast<Scenario*>(resource) : nullptr;
        if (!scenario) {
            fprintf(stderr, "[ScenarioDialog] resource[%d]=%p rejected by IsDerivedFrom(typeId=%u)\n",
                    i, static_cast<void*>(resource), Scenario::TypeId());
            fflush(stderr);
            continue;
        }

        // Filter by difficulty: Scenario +0x69 must match this one's
        uint8_t scenDifficulty = static_cast<uint8_t>(scenario->tutorial);
        printf("[ScenarioDialog::Slot4] scenario[%d] at %p: difficulty=%d (want %d), isScenario=%d\n",
               i, static_cast<void*>(scenario), scenDifficulty, difficulty, isScenario);
        if (scenDifficulty != difficulty) continue;

        // --- Clone the dialog's template 0 into a table row ---
        // (UI::ElementDescription slot 8 CreateInstance, RETSLOT per B6-2)
        Object::ExternalPtr<UI::ElementDescription> rowTemplate = _dialog->GetTemplate(0);
        Object::InternalPtr<UI::Element> clone;
        rowTemplate.Get()->CreateInstance(&clone, _dialog);
        UI::Element* clonedElem = clone.Get();

        // --- Insert clone into table ---
        table->SetChild(&clone, entryIndex);

        // --- Build the entry ---
        ScenarioDialogEntry entry = {};
        entry.element = clonedElem;

        // The element raises element events (hover, press, tooltip; flag 0x10000)
        clonedElem->SetFlagsOnOff(UI::kElementFlagRaisesEvents, 0);

        // Set element ID = entryIndex + 1000
        clonedElem->SetElementId(entryIndex + 1000);

        // --- Find sub-elements in the clone ---
        entry.imageIcon       = static_cast<UI::ImageElement*>(FindIn(clonedElem, "imageIcon"));
        entry.textTitle       = static_cast<UI::TextElement*>(FindIn(clonedElem, "textTitle"));
        entry.textDescription = static_cast<UI::TextElement*>(FindIn(clonedElem, "textDescription"));
        entry.selectSave      = FindIn(clonedElem, "selectSave");

        // Store scenario pointer and resource index
        entry.scenario = scenario;
        entry.resourceIndex = i;

        // --- Set icon sprite from scenario ---
        {
            System::String spriteName = scenario->GetSpriteName();
            UI::Sprite sprite(scenario->GetSpriteSheet(), spriteName);
            entry.imageIcon->SetSprite(&sprite);
        }

        // --- Set title text from scenario ---
        {
            System::String title = scenario->GetTitle();
            UI::String text(scenario->GetStringTable(), title);
            entry.textTitle->SetText(&text);
        }

        // --- Set description text from scenario ---
        {
            System::String description = scenario->GetDescription();
            UI::String text(scenario->GetStringTable(), description);
            entry.textDescription->SetText(&text);
        }

        // --- Push the entry (FUN_140013b60's inlined SetSize) ---
        entries.SetSize(entries.GetSize() + 1, entry);

        printf("[ScenarioDialog::Slot4] Added entry[%d] for scenario[%d]\n", entryIndex, i);
        // Identity dump: which scenario record backs each list row, and which .sav
        // it starts from. A tutorial opening on an already-developed town is
        // expected -- the series is progressive -- so this is the only way to tell
        // "tutorial N legitimately continues N-1's town" from a real off-by-one.
        {
            const char* nameData = ScenarioString(scenario->scenarioName)._data;
            const char* saveData = ScenarioString(scenario->saveGame)._data;
            int nameLen = nameData ? RtSystem::StringLength(nameData) : 0;
            int saveLen = saveData ? RtSystem::StringLength(saveData) : 0;
            printf("[ScenarioID] entry[%d] scenario[%d] name=\"%.*s\" (len=%d) "
                   "save=\"%.*s\" (len=%d)\n",
                   entryIndex, i,
                   nameLen, nameData, nameLen,
                   saveLen, saveData, saveLen);
        }
        entryIndex++;
    }
    printf("[ScenarioDialog::Slot4] Done. Total entries: %d\n", entryIndex);
}

// ============================================================
// ScenarioDialog::Slot7 (vtable +0x38, exe 0x140078000): RET 0 (3 bytes)
// ============================================================
// No-op handler; returns 0.
void ScenarioDialog::OnUpdate(const System::Time& /*time*/) {
    }

// ============================================================
// ScenarioDialog::Slot6 (vtable +0x30) — Update entry visibility
// ============================================================
// Shows the selectSave mark of the selected row, hides the others, and
// enables the OK button when a row is selected.
//
// Original: 0x140077BE0.

static int s_slot6LogCount = 0;
void ScenarioDialog::OnUpdateLayout() {
    int count = entries.GetSize();
    int selected = selectedIndex;
    if (s_slot6LogCount < 3) {
        printf("[ScenarioDialog::Slot6] Draw: count=%d, selected=%d\n", count, selected);
        s_slot6LogCount++;
    }

    // Show selected entry, hide all others
    for (int i = 0; i < count; i++) {
        UI::Element* mark = entries[i].selectSave;
        bool isNotSelected = (i != selected);
        mark->SetFlagsOnOff(isNotSelected ? 1 : 0, isNotSelected ? 0 : 1);
    }

    // Enable/disable confirm button based on valid selection
    buttonOk->Enable(selected >= 0);
}

// ============================================================
// ScenarioDialog::LoadSelected — reimplementation of FUN_140077cb0
// ============================================================
// Loads the selected scenario. Two paths:
// 1. New scenario (no save name): reads terrain/climate/seed params from scenario
//    resource and starts a new game via StartNewGame logic.
// 2. Saved scenario (has save name): loads the save file first, then uses the
//    saved state to start the game.
//
// Scenario resource: +0x58 = display name, +0x60 = seed, +0x64 = terrainSize,
//   +0x68 = disasterOff, +0x70 = mapGen, +0x78 = terrainGen, +0x80 = climateGen,
//   +0x88 = save name string (empty = new scenario).

void ScenarioDialog::LoadSelected() {
    printf("[ScenarioDialog::LoadSelected] Called, self=%p\n", static_cast<void*>(this));

    int selectedIdx = selectedIndex;
    const ScenarioDialogEntry& selectedEntry = entries[selectedIdx];
    Scenario* scenario = selectedEntry.scenario;

    // The save name (+0x88) and the display name (+0x58)
    System::String saveNameStr = ScenarioString(scenario->saveGame);
    System::String displayNameStr = ScenarioString(scenario->scenarioName);

    const char* saveStrPtr = saveNameStr._data;
    bool hasSaveState = (saveStrPtr != nullptr && RtSystem::StringLength(saveStrPtr) > 0);
    printf("[ScenarioDialog::LoadSelected] selectedIdx=%d, scenario=%p, hasSaveState=%d, saveStrPtr=%p\n",
           selectedIdx, static_cast<void*>(scenario), hasSaveState, static_cast<const void*>(saveStrPtr));

    if (!hasSaveState) {
        // New scenario: start fresh game with scenario parameters
        uint32_t seedVal = static_cast<uint32_t>(scenario->randomSeed);
        uint32_t terrainSizeVal = static_cast<uint32_t>(scenario->terrainSize);
        char disasterOff = static_cast<char>(scenario->disasters);
        // Scenario generator triple. These offsets were labelled climate/terrain/map;
        // retail's own LoadSelected (0x140077cb0, the call at +0x243) proves the outer
        // two are the other way round: it passes +0x70 as arg6 and +0x80 as arg8, and
        // 0x1400b4540 stores args 6/7/8 into GameUpdateLoop +0x40/+0x48/+0x50, which
        // Load reads as mapGen / terrainGen / climateGen. The positional order passed
        // below is unchanged and already matched retail - only the names were wrong.
        MapGenerator*     mapGen     = scenario->mapGenerator;
        TerrainGenerator* terrainGen = scenario->terrainGenerator;
        ClimateGenerator* climateGen = scenario->climateGenerator;

        // The scenario's index in the ScenarioList
        uint32_t scenarioResIdx = static_cast<uint32_t>(selectedEntry.resourceIndex);

        // The parent MainMenuDialog (+0xC0)
        MainMenuDialog* parentCtrl = parentMenu;

        // The saved-state InternalPtr: null
        ::Object::InternalPtr<Achievement::AchievementInstance> nullIBP(nullptr);

        printf("[ScenarioDialog::LoadSelected] NEW SCENARIO: seed=%u, terrainSize=%u, disasterOff=%d, scenarioResIdx=%u\n",
               seedVal, terrainSizeVal, (int)disasterOff, scenarioResIdx);
        printf("[ScenarioDialog::LoadSelected] mapGen=%p, terrainGen=%p, climateGen=%p, parentCtrl=%p\n",
               static_cast<void*>(mapGen), static_cast<void*>(terrainGen), static_cast<void*>(climateGen),
               static_cast<void*>(parentCtrl));

        // 0x1400b4540 MainMenuDialog::StartNewGame — constructs GameUpdateLoop ->
        // GameLoadDialogLoopT -> LoaderLoop chain and sets next loop. Retail hands
        // seed / terrainSize in pointer-width slots and reads their low dword; the
        // member takes the two uint32 values (the StartNewGame_Reg forwarder that
        // did the narrowing is gone). parentMenu is the MainMenuDialog that opened
        // this dialog (ScenarioDialog.h +0xC0).
        static_cast<MainMenuDialog*>(parentCtrl)->StartNewGame(
                         displayNameStr, seedVal, terrainSizeVal,
                         disasterOff != 0, mapGen, terrainGen, climateGen,
                         scenarioResIdx, &nullIBP);
        printf("[ScenarioDialog::LoadSelected] fn_StartNewGame returned\n");
    } else {
        fflush(stderr);
        {
            const char* missing = nullptr;
            if (!::Save::SaveLoad::_singleton) missing = "SaveLoad singleton";
            else if (!::IO::Persistence::_singleton) missing = "Persistence singleton";
            else if (!::IO::FileSystem::_singleton) missing = "FileSystem singleton";
            if (missing) {
                fprintf(stderr, "[ScenarioDialog] SAVED branch ABORTED - %s is NULL\n", missing);
                fflush(stderr);
                return;
            }
        }
        Save::SaveLoad* saveLoad = ::Save::SaveLoad::_singleton;
        saveLoad->SetOperation(Save::SaveLoad::kOpLoadFromFile, 0);
        saveLoad->SetSaveName(saveNameStr, 2);

        // Get save path and set data directory
        // Original flow (from assembly at 0x140077d41-0x140077d96):
        //   1. GetSavePath → String at RSP+0xa8 (kept alive until after loop)
        //   2. GetDataDirectory → temp at RSP+0xb0, returns ref in RAX
        //   3. persistence+0x90 = RAX (data dir)
        //   4. Destroy RSP+0xb0 (GetDataDirectory temp)
        //   5. SaveLoad::Update loop
        //   6. persistence+0x90 = RSP+0xa8 (restore from GetSavePath result)
        IO::Persistence* persistence = ::IO::Persistence::_singleton;
        System::String savePathBuf = persistence->GetSavePath();  // kept alive until after the SaveLoad::Update loop
        System::String& persistenceDataDir = persistence->_savePath;
        {
            System::Filename dataDir = ::IO::FileSystem::_singleton->GetDataDirectory();
            persistenceDataDir = dataDir.AsString();
        }   // (matches retail's early release of the data-directory temp)

        // Run SaveLoad::Update loop until operation completes
        Save::SaveLoad::Operation operation = saveLoad->GetOperation();
        printf("[ScenarioDialog::LoadSelected] SAVED SCENARIO: entering SaveLoad loop, initial operation=%d\n",
               static_cast<int>(operation));
        int loopCount = 0;
        while (operation != Save::SaveLoad::kOpNone) {
            System::Time zeroTime;
            RtSystem_Time_Ctor(&zeroTime, 0.0f);
            saveLoad->Update(zeroTime);
            operation = saveLoad->GetOperation();
            loopCount++;
            if (loopCount % 100 == 0) {
                printf("[ScenarioDialog::LoadSelected] SaveLoad loop iteration %d, operation=%d\n", loopCount,
                       static_cast<int>(operation));
            }
        }
        printf("[ScenarioDialog::LoadSelected] SaveLoad loop done after %d iterations\n", loopCount);

        // Restore persistence path from GetSavePath result (matches original at 0x140077df0)
        persistenceDataDir = savePathBuf;
        // Retail destroys the GetSavePath String only after WorldSetup_LoadFromSave
        // has run (String::~String(local_res10) at the tail of 0x140077cb0):
        // savePathBuf's scope ends below.

        // The parent MainMenuDialog and a null InternalPtr
        MainMenuDialog* parentCtrl = parentMenu;
        ::Object::InternalBasePtr nullIBP(nullptr);

        uint32_t scenarioResIdx = static_cast<uint32_t>(selectedEntry.resourceIndex);
        Save::SavedState* savedState = saveLoad->GetSaveState();

        parentCtrl->LoadFromSave(savedState,   // 0x1400b49f0
                                static_cast<int32_t>(scenarioResIdx),
                                &nullIBP);
    }
}

// ============================================================
// ScenarioDialog::Slot8 (vtable +0x40) — OnCommand
// ============================================================
// Slot8 (0x140077F80): OnCommand — selection change and button dispatch.
// param3=2 (selection change): updates selected index from button IDs 1000+
// param3=0x12 (button click): id=100 → close, id=101 → load selected scenario
void ScenarioDialog::OnElementEvent(Object::InternalPtr<UI::Element>* /*element*/, UI::EventId param3, int32_t param4, int32_t /*c*/, int32_t /*d*/, int32_t /*e*/, void* /*userData*/) {
    printf("[ScenarioDialog::Slot8] OnCommand: param3=0x%x, param4=%d\n", param3, param4);

    if (param3 == UI::EventId::LeftPress) {
        // Selection change — button IDs are 1000 + index
        int maxIndex = entries.GetSize();
        if (param4 > 999 && param4 < maxIndex + 1000) {
            selectedIndex = param4 - 1000;
        }
    } else if (param3 == UI::EventId::Activated) {
        if (param4 == 100) {
            // Close button
            Object::InternalPtr<UI::DialogInstance> self(this);
            _ui->CloseDialog(&self);
        } else if (param4 == 0x65) {
            // Confirm/load button — FUN_140077cb0
            LoadSelected();
        }
    }
}

// ============================================================
// ScenarioDialog vtable + ctor (was ResolveAddr(Module::Exe, 0x1400786b0) /
// unregistered vtable_ScenarioDialog @ 0x14015d2d0)
// ============================================================
// ScenarioDialog_CopyCtor (0x1400786b0) resolved to null in standalone, so both
// MainMenuDialog_OpenScenario and MainMenuDialog_OpenTutorial called it UNGUARDED
// (EXEC-of-0). It also hardcodes `self->vtable = &vtable_ScenarioDialog` inline —
// that raw .rdata address (0x14015d2d0) was itself never registered, so even a
// null-safe ctor would have handed the dialog a null vtable. Both are ported here.
//
// vtable_ScenarioDialog layout, dumped straight from .rdata (tools/dump_vtable.py
// 0x14015d2d0) — same 10-slot shape as AchievementDialog/GoalDialog
// (menu_dialog_standalone_vtables.cpp):
//   [0] 0x140013af0 ScalarDtor   (frees +0xD8 array, NOT a bare base thunk)
//   [1] 0x140013960 static getter (returns DAT_1401c5560 — use GetTypeIdByName instead)
//   [2] 0x140013950 GetTypeId     (returns DAT_1401c5558 — ditto)
//   [3] 0x14003a948 Persist       (base thunk)
//   [4] 0x140078010 Setup         -> ScenarioDialog_Slot4 (ported)
//   [5] 0x14003a94e Shutdown      (base thunk)
//   [6] 0x140077be0 OnUpdate      -> ScenarioDialog_Slot6 (ported)
//   [7] 0x140078000 OnUpdateLayout-> ScenarioDialog_Slot7 (ported)
//   [8] 0x140077f80 OnCommand     -> ScenarioDialog_Slot8 (ported)
//   [9] 0x14003a954 OnSerialize   (base thunk)

ScenarioDialog::ScenarioDialog()
    : difficulty(0), parentMenu(nullptr), selectedIndex(-1), buttonOk(nullptr) {}

// ScenarioDialog_CopyCtor (0x1400786b0): shared by ScenarioDialog and the
// TutorialDialog (same class; difficulty byte at +0xB8 differentiates the two —
// MainMenuDialog_OpenScenario passes 0, MainMenuDialog_OpenTutorial passes 1).
//   base ctor: DialogInstance(ui, extRes, parent, 0, 0, 0)
//   +0xB8 (u8)  = difficulty
//   +0xC0       = the parent menu  (its InternalPtr)
//   +0xC8 (i32) = -1               (selectedIndex, read by OnElementEvent)
//   +0xD0       = 0                (buttonOk)
//   +0xD8 / +0xE0 = 0              (the entries Array)
// ~ScenarioDialog (0x140013af0 before ~DialogInstance) is the entries Array's
// dtor: MemFree(+0xD8), the header cleared (the entries are plain copies).
ScenarioDialog::ScenarioDialog(UI::UserInterface* ui, const UI::Dialog* dialog,
                               Object::InternalPtr<UI::DialogInstance> parent,
                               MainMenuDialog* parentMenuArg, uint8_t difficultyArg)
    : UI::DialogInstance(ui, dialog, parent, nullptr, nullptr, 0),
      difficulty(difficultyArg), parentMenu(parentMenuArg), selectedIndex(-1),
      buttonOk(nullptr) {}

// create 0x140013970: MemAlloc(0xe8, 8), then the ctor.
::Object::Base* ScenarioDialog::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(ScenarioDialog), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(::new (p) ScenarioDialog());
}
// createInPlace 0x1400139f0 (the tree's in-place build is kept, B8-13).
::Object::Base* ScenarioDialog::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(::new (storage) ScenarioDialog());
}

// The reflection record stays at the TU position of the UI_DIALOG_STUB row it
// replaces (ui_dialog_stub_factories.cpp, B38 D4). Nothing reads 0x14015d2d0
// back by address any more, so its registration (reg_nullsweep.cpp) is gone.

