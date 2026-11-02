// src/game/Dialog/mainmenudialog.cpp
// Clean reimplementation of MainMenuDialog methods.
//
// MainMenuDialog (0xC8 bytes): Main menu / pause menu screen.
// Layout: DialogInstance base, +0xB8 = the running game (GameUpdateLoop*, non-zero
// in the pause menu), +0xC0 = the owning MenuDialogLoopT (front end).
//
// Original source: C:\Projects\Buildy\code\Game\Dialog/MainMenuDialog.h
//                  Dialog\MainMenuDialog.cpp

#include "load_time.h"   // [LoadTime] stamps (NOT RETAIL, print-only; block LOADSTAMP)
#include "ui/TextElement.h"   // ENDGAME B.3.6 UI callers (K2)
#include "ui/ButtonElement.h"
#include "ui/Element.h"   // ENDGAME B.3.6 UI callers (K2)
#include "ui/DialogInstance.h"   // ENDGAME B.3.6 UI callers (K2)
#include "ui/UserInterface.h"   // ENDGAME B.3.6 UI callers (K2)
#include "ui/String.h"   // ENDGAME B.3.6 UI callers (K2)
#include "ui/Dialog.h"   // ENDGAME B.3.6 UI callers (K2)
#include "ui/Cursor.h"   // ENDGAME B.3.6 UI callers (K2)
#include <new>   // ENDGAME B.3.6 UI callers (K2)
#include "application/Application.h"   // ENDGAME B.3.7 callers (K1)
#include "audio/Audio.h"     // ENDGAME B.3.7 callers (K4)
#include "Dialog/MainMenuDialog.h"
#include "Game/MenuDialogLoopT.h"       // ENDGAME B.3.9 EXA: the real loop classes
#include "Game/GameLoadDialogLoopT.h"
#include "Game/GameLoaderLoop.h"
#include "Dialog/ModDialog.h"
#include "Dialog/ExtensionsDialog.h"   // EXT SEAM (NOT RETAIL, docs/extensions/handins/EXTSCAFFOLD.md): the Extensions button
#include "ext/phone_menu/phone_menu.h"   // EXT SEAM (NOT RETAIL, docs/extensions/handins/PHONEMENU.md): the menu safe area
#include "ext/registry.h"                // Ext::Fix().mods_persist
#include "Dialog/NewGameDialog.h"
#include "Dialog/MapParamsDialog.h"
#include "Dialog/AchievementDialog.h"
#include "Dialog/GoalDialog.h"
#include "Dialog/ui_dialogs.h"
#include "options.h"
#include "system/Value.h"
#include "ui/DialogInstance.h"
#include "Game/game.h"   // B3R STORAGE: g_gameContainer
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "Dialog/ScenarioDialog.h"      // ScenarioDialog (placement-constructed)
#include "Dialog/SaveLoadDialog.h"      // SaveLoadDialog's constructor 0x1400b8270
#include "Dialog/OptionsDialog.h"       // OptionsDialog's constructor 0x140075150
#include "system/Configuration.h"      // B3R: System::Configuration, RtSystem_Configuration_CurrentBuildId
#include "openba_build_info.h"         // OPENBA_GIT_HASH / OPENBA_BUILD_DATE (generated into build/<flavour>/gen)
#include "save/SaveLoad.h"             // B3R: RtSave_SaveLoad_Dtor
#include "Dialog/dialog_exe_helpers.h"
#include "system/String.h"
#include "system/Filename.h"
#include "object/External.h"   // ENDGAME B.3.3 step 6A
#include <cstdint>
#include <cstdio>
#include "platform_window.h"  // Platform::Window::PostQuitMessage: the Quit handler's fallback
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "object/TypeInfo.h"   // ENDGAME B.3.3 step 6C
#include "Dialog/exe_bodies.h"
#include "system/MemoryAllocator.h"   // B3R: the allocator doors

// ============================================================
// Button element IDs (assigned in OnOpened, dispatched in OnElementEvent)
// ============================================================
enum MainMenuButtonId {
    kButton_New         = 0,
    kButton_Resume      = 1,
    kButton_Load        = 2,
    kButton_Save        = 3,
    kButton_Options     = 4,
    kButton_Menu        = 5,
    kButton_Quit        = 6,
    kButton_Mod         = 7,
    kButton_Achievement = 8,
    kButton_Scenario    = 9,
    kButton_Goal        = 10,
    kButton_Map         = 11,
    kButton_Tutorial    = 12,
};

// ============================================================
// Helper: load .rsc resource for dialog construction
// ============================================================
// Retail validates Dialog/*.rsc resources against UI::Dialog's type id, read through
// the import slot 0x140151728 (?_typeInfo@Dialog@UI@@). ENDGAME B.3.9 TR: the class
// accessor (was the slot, zero in standalone, back-filled by a by-name factory lookup).

static UI::Dialog* LoadDialogResource(const char* rscPath) {
    System::Filename filenameBuf(rscPath);
    // The dialog doors of the other blocks take the resource as a non-const void*.
    ::Object::External* resource =
        ::Object::ExternalFactory::_singleton->FindOrLoadAs<::Object::External>(filenameBuf);
    printf("[LoadDialogResource] '%s' -> FindOrLoad=%p\n", rscPath, (const void*)resource);
    UI::Dialog* extRes = nullptr;
    if (resource) {
        const uint32_t typeId = UI::Dialog::TypeId();
        bool derived = (typeId && ::Object::IsDerivedFromNullSafe(resource, typeId));
        printf("[LoadDialogResource] '%s' typeId=%u IsDerivedFrom=%d\n",
               rscPath, typeId, derived ? 1 : 0);
        if (derived) {
            extRes = static_cast<UI::Dialog*>(resource);
        }
    }
    return extRes;
}

// ============================================================
// Helper: wrap dialog in InternalPtr and add via UI
// ============================================================
static void AddDialogToUI(MainMenuDialog* self, UI::DialogInstance* dlgObj) {
    Object::InternalPtr<UI::DialogInstance> ibp(dlgObj);
    self->_ui->AddDialog(&ibp);
}

// ============================================================
// MainMenuDialog Open* handlers
// ============================================================
// Reimplemented from decompiled exe functions.
// Pattern: Load .rsc -> MemAlloc(size) -> call dialog ctor -> AddDialog

// OpenNewGame (0x1400B3620): "Dialog/NewGame.rsc", size 0xAE8
// Ctor: 0x140071040(self, ui, extRes, nullParent, parentMenuIBP)
// Passes this menu as the new-game dialog's owner (StartGame builds through it).
void MainMenuDialog::OpenNewGame() {
    UI::Dialog* extRes = LoadDialogResource("Dialog/NewGame.rsc");
    NewGameDialog* dlgObj = nullptr;
    void* dlgMem = System::MemoryAllocator::MemAlloc(0xAE8, 8);
    if (dlgMem) {
        printf("[NewGameDialog_Ctor] self=%p ui=%p extRes=%p parentMenu=%p\n",
               dlgMem, (void*)_ui, (const void*)extRes, (void*)this);
        dlgObj = ::new (dlgMem) NewGameDialog(_ui, extRes,   // 0x140071040
                                              Object::InternalPtr<UI::DialogInstance>(),
                                              Object::InternalPtr<MainMenuDialog>(this));
        // Publish the just-constructed dialog so the auto-start driver
        // (src/diag/autostart.cpp) can locate it and fire Start once OnOpened has
        // populated the combos + OK button. Harmless when autostart is disabled.
        g_StandaloneNewGameDialog = dlgObj;
    }
    AddDialogToUI(this, dlgObj);
}

// OpenLoad (0x1400B3770): "Dialog/SaveLoad.rsc", size 0x160, mode=1 (load)
// Ctor: 0x1400b8270(self, ui, extRes, nullParent, mode, menuLoop, game)
void MainMenuDialog::OpenLoad() {
    // The AchievementManager got its list at boot (GameContainer_Init 0x140099db0,
    // SetAchievements on container [0]), as in retail; the load path needs nothing here.

    UI::Dialog* extRes = LoadDialogResource("Dialog/SaveLoad.rsc");
    UI::DialogInstance* dlgObj = nullptr;
    void* dlgMem = System::MemoryAllocator::MemAlloc(0x160, 8);
    if (dlgMem) {
        Object::InternalPtr<UI::DialogInstance> nullIBP;
        dlgObj = ::new (dlgMem) SaveLoadDialog(_ui, extRes, nullIBP,   // 0x1400b8270
                                             1,  // mode = load
                                             menuLoop,   // the menu loop
                                             game);      // the running game
    }
    AddDialogToUI(this, dlgObj);
    // Standalone: expose the instance so the BANISHED_AUTOLOAD driver
    // (src/diag/autostart.cpp) can select a save + inject the Load click.
    extern void* g_StandaloneSaveLoadDialog;
    g_StandaloneSaveLoadDialog = dlgObj;
}

// OpenSave (0x1400B38B0): "Dialog/SaveLoad.rsc", size 0x160, mode=0 (save)
void MainMenuDialog::OpenSave() {
    UI::Dialog* extRes = LoadDialogResource("Dialog/SaveLoad.rsc");
    UI::DialogInstance* dlgObj = nullptr;
    void* dlgMem = System::MemoryAllocator::MemAlloc(0x160, 8);
    if (dlgMem) {
        Object::InternalPtr<UI::DialogInstance> nullIBP;
        dlgObj = ::new (dlgMem) SaveLoadDialog(_ui, extRes, nullIBP,   // 0x1400b8270
                                             0,  // mode = save
                                             menuLoop,   // the menu loop
                                             game);      // the running game
    }
    AddDialogToUI(this, dlgObj);
}

// OpenOptions (0x1400B3A10): "Dialog/Options.rsc", size 0x1C0
// Ctor: 0x140075150(self, ui, extRes, nullParent)
void MainMenuDialog::OpenOptions() {
    UI::Dialog* extRes = LoadDialogResource("Dialog/Options.rsc");
    UI::DialogInstance* dlgObj = nullptr;
    void* dlgMem = System::MemoryAllocator::MemAlloc(0x1C0, 8);
    if (dlgMem) {
        Object::InternalPtr<UI::DialogInstance> nullIBP;
        printf("[OptionsDialog_Ctor] self=%p ui=%p extRes=%p parentIBP=%p\n",
               dlgMem, _ui, extRes, &nullIBP);
        dlgObj = ::new (dlgMem) OptionsDialog(_ui, extRes, nullIBP);   // 0x140075150
    }
    AddDialogToUI(this, dlgObj);
}

// ReturnToMenu (0x1400B4790): Destroys SaveLoad, creates MenuDialogLoopT
// + GameLoadDialogLoopT (with "Dialog/Loading.rsc") + outer LoaderLoop.
// Sets next loop on either the menu loop or the running game.
void MainMenuDialog::ReturnToMenu() {
    GameUpdateLoop* gameWorld = game;

    if (gameWorld == nullptr) {
        // No game active: destroy the menu loop's SaveLoad.
        Save::SaveLoad* saveLoad = menuLoop->_saveLoad;
        if (saveLoad) {
            RtSave_SaveLoad_Dtor(saveLoad);   // ??1SaveLoad 0x1800c3c90
            System::MemoryAllocator::MemFree(saveLoad);
        }
        menuLoop->_saveLoad = nullptr;
    } else {
        // Game active: destroy the game's SaveLoad
        Save::SaveLoad* saveLoad = gameWorld->m_saveLoad;
        if (saveLoad) {
            RtSave_SaveLoad_Dtor(saveLoad);   // ??1SaveLoad 0x1800c3c90
            System::MemoryAllocator::MemFree(saveLoad);
        }
        gameWorld->m_saveLoad = nullptr;
    }

    // ENDGAME B.3.9 EXA: the three real loop classes, built as retail's
    // 0x1400B4790 builds them (same allocations, same ctor arguments).
    // Step 1: MenuDialogLoopT (0xC00 bytes) with "Dialog/MainMenu.rsc" (ctor 0x14009bb00)
    Engine::UpdateLoop* newMenuLoop = nullptr;
    void* menuMem = System::MemoryAllocator::MemAlloc(0xC00, 8);
    if (menuMem) {
        System::String menuStr("Dialog/MainMenu.rsc");
        newMenuLoop = ::new (menuMem) MenuDialogLoopT(menuStr);
    }

    // Step 2: GameLoadDialogLoopT (0xB60 bytes) wrapping the menu loop (the
    // subclass ctor retail inlines here)
    Engine::UpdateLoop* dlLoop = nullptr;
    void* dlMem = System::MemoryAllocator::MemAlloc(0xB60, 4);
    if (dlMem) {
        System::Time delayTime;
        delayTime._ticks = 0;
        RtSystem_Time_Ctor(&delayTime, 1.0f);
        System::String loadingStr("Dialog/Loading.rsc");
        dlLoop = ::new (dlMem) GameLoadDialogLoopT(loadingStr, newMenuLoop, delayTime);
    }

    // Step 3: outer GameLoaderLoop (0x48 bytes) wrapping the dialog loader
    GameLoaderLoop* outerLoop = nullptr;
    void* outerMem = System::MemoryAllocator::MemAlloc(0x48, 4);
    if (outerMem) {
        System::Time zeroTime;
        zeroTime._ticks = 0;
        RtSystem_Time_Ctor(&zeroTime, 0.0f);
        outerLoop = ::new (outerMem) GameLoaderLoop(dlLoop, zeroTime);
    }

    // Step 4: Set next loop on the appropriate target
    if (gameWorld == nullptr) {
        // No game: fade the menu loop's UI and queue the chain
        menuLoop->_ui.SetFullscreenFade(true, 0xFF000000, false);
        menuLoop->_pendingLoop = outerLoop;
    } else {
        // Game active: set cursor, fade, store as the game's next loop
        UI::UserInterface* ui = gameWorld->m_userInterface;
        ui->SetCursor(g_gameContainer->cursorHourglass);
        ui->SetFullscreenFade(true, 0xFF000000, false);
        gameWorld->m_nextLoop = outerLoop;
    }
}

// OpenAchievement (0x1400B34C0): "Dialog/Achievement.rsc", size 0xD0
// The AchievementDialog ctor, inlined by retail.
void MainMenuDialog::OpenAchievement() {
    // The achievement list and the manager were set up at boot by GameContainer_Init
    // 0x140099db0 (InnerLoaderLoop::Load), as in retail.

    UI::Dialog* extRes = LoadDialogResource("Dialog/Achievement.rsc");
    AchievementDialog* dlgObj = nullptr;
    void* dlgMem = System::MemoryAllocator::MemAlloc(sizeof(AchievementDialog), 8);
    if (dlgMem) {
        // ENDGAME B.3.8 DLG: the real class (retail inlines this ctor here).
        dlgObj = ::new (dlgMem) AchievementDialog(
            _ui, extRes, Object::InternalPtr<UI::DialogInstance>());
    }
    AddDialogToUI(this, dlgObj);
}

// OpenScenario (0x1400B3380): "Dialog/Scenario.rsc", size 0xE8
// Ctor: 0x1400786b0 (shared with the Tutorial dialog)
// Passes this menu as the owner and difficulty=0 for scenarios from main menu.
void MainMenuDialog::OpenScenario() {
    UI::Dialog* extRes = LoadDialogResource("Dialog/Scenario.rsc");
    ScenarioDialog* dlgObj = nullptr;
    void* dlgMem = System::MemoryAllocator::MemAlloc(0xE8, 8);
    if (dlgMem) {
        Object::InternalPtr<MainMenuDialog> parentIBP(this);
        Object::InternalPtr<UI::DialogInstance> nullIBP;
        dlgObj = ::new (dlgMem) ScenarioDialog(_ui, extRes, nullIBP,   // 0x1400786b0
                                               parentIBP.Get(),
                                               0);  // difficulty (main menu = 0)
    }
    AddDialogToUI(this, dlgObj);
}

// OpenGoal (0x1400B3220): "Dialog/Goal.rsc", size 0xC8 (200 decimal)
// The GoalDialog ctor, inlined by retail.
void MainMenuDialog::OpenGoal() {
    UI::Dialog* extRes = LoadDialogResource("Dialog/Goal.rsc");
    GoalDialog* dlgObj = nullptr;
    void* dlgMem = System::MemoryAllocator::MemAlloc(sizeof(GoalDialog), 8);
    if (dlgMem) {
        // ENDGAME B.3.8 DLG: the real class (retail inlines this ctor here).
        dlgObj = ::new (dlgMem) GoalDialog(
            _ui, extRes, Object::InternalPtr<UI::DialogInstance>());
    }
    AddDialogToUI(this, dlgObj);
}

// OpenMap (0x1400B3120): "Dialog/MapParams.rsc", size 0xF8
// Ctor: 0x14006bd80(self, ui, extRes, nullParent)
void MainMenuDialog::OpenMap() {
    UI::Dialog* extRes = LoadDialogResource("Dialog/MapParams.rsc");
    MapParamsDialog* dlgObj = nullptr;
    void* dlgMem = System::MemoryAllocator::MemAlloc(0xF8, 8);
    if (dlgMem) {
        dlgObj = ::new (dlgMem) MapParamsDialog(   // 0x14006bd80
            _ui, extRes, Object::InternalPtr<UI::DialogInstance>());
    }
    AddDialogToUI(this, dlgObj);
}

// OpenTutorial (0x1400B2FE0): "Dialog/Tutorial.rsc", size 0xE8
// Uses same ctor as Scenario (0x1400786b0).
void MainMenuDialog::OpenTutorial() {
    printf("[MainMenuDialog] OpenTutorial called, self=%p\n", (void*)this);
    UI::Dialog* extRes = LoadDialogResource("Dialog/Tutorial.rsc");
    printf("[MainMenuDialog] OpenTutorial: extRes=%p\n", (const void*)extRes);
    ScenarioDialog* dlgObj = nullptr;
    void* dlgMem = System::MemoryAllocator::MemAlloc(0xE8, 8);
    printf("[MainMenuDialog] OpenTutorial: dlgMem=%p\n", dlgMem);
    if (dlgMem) {
        Object::InternalPtr<MainMenuDialog> parentIBP(this);
        Object::InternalPtr<UI::DialogInstance> nullIBP;
        dlgObj = ::new (dlgMem) ScenarioDialog(_ui, extRes, nullIBP,   // 0x1400786b0
                                               parentIBP.Get(),
                                               1);  // difficulty: 1 = tutorial mode
        printf("[MainMenuDialog] OpenTutorial: dlgObj=%p after ctor\n", dlgObj);
    }
    {
        // Expose the instance so the BANISHED_AUTOTUTORIAL driver in
        // src/diag/autostart.cpp can select an entry and inject the Load click,
        // the same way OpenLoad exposes the SaveLoadDialog.
        extern void* g_StandaloneScenarioDialog;
        g_StandaloneScenarioDialog = dlgObj;
    }
    AddDialogToUI(this, dlgObj);
    printf("[MainMenuDialog] OpenTutorial: dialog added to UI\n");
}

// OpenMod (0x1400B2EA0): "Dialog/Mod.rsc", size 0xD8
// Ctor: 0x140070030(self, ui, extRes, parentIBP, parentMenuIBP)
void MainMenuDialog::OpenMod() {
    UI::Dialog* extRes = LoadDialogResource("Dialog/Mod.rsc");
    ModDialog* dlgObj = nullptr;
    void* dlgMem = System::MemoryAllocator::MemAlloc(0xD8, 8);
    if (dlgMem) {
        dlgObj = ::new (dlgMem) ModDialog(_ui, extRes,   // 0x140070030
                                          Object::InternalPtr<UI::DialogInstance>(),
                                          Object::InternalPtr<MainMenuDialog>(this));
    }
    AddDialogToUI(this, dlgObj);
}

// (MainMenuDialog_Slot8, the C copy of retail's OnElementEvent 0x1400b3b50, is
// deleted: its dispatch is the member's below, which adds only the standalone
// harness wiring — the Mods opener's autostart hook, the Quit fallback and the
// log lines. No retail difference a player sees.)


// ============================================================
// MainMenuDialog as a real class (ENDGAME B.3.8 block DLG)
// ============================================================
// Dialog/MainMenuDialog.h carries the retail slot list. The live bodies below
// are the ones the old hand-built 20-slot table installed (its slots 10-19 were
// a phantom "DialogController secondary interface" retail does not have).
// Slots 6/7 are INHERITED: the old table called DialogInstance's bodies there
// with a qualified call, which is exactly what retail's import thunks reach.

#include "Object/factory_registration.h"

// ============================================================
// [4] OnOpened — retail 0x1400b3cd0 (445 instructions)
// ============================================================
// Phase D.4 GAPS (row 1275): the full body. It was the free function
// MainMenuDialog_Slot4 while the member ran a slim standalone Setup (element ids,
// Resume's hot key since SDL-C, the BuildId child) that skipped the audio pause
// and the in-game button hiding. Retail, in order:
//
//   1. FindElement the 13 buttons, in the .rsc-name order New, Resume, Load, Save,
//      Options, Menu, Quit, Achievement, Scenario, Goal, Map, Tutorial, Mod.
//   2. SetElementId each found one, in the order New 0, Resume 1 (+ hot key:
//      `MOV dword ptr [elem+0xc0], 8`, the Cancel input, Esc), Options 4, Quit 6,
//      Load 2, Save 3, Menu 5, Achievement 8, Scenario 9, Goal 10, Map 11,
//      Tutorial 12, Mod 7. (Each preceded by CheckPtr, a bare RET.)
//   3. if (this+0xb8 /*game*/ != 0):
//        Audio::Scene::Pause(GameUpdateLoop::_singleton+0x78, 4 / 3 / 2, true);
//        if (_singleton+0x15c /*scenario index*/ < 0) buttonGoal->SetFlagsOnOff(1, 0);
//        else                                          buttonMap->SetFlagsOnOff(1, 0);
//        if (game+0xd0 bit 10) buttonSave->vtbl[+0x40](0)   // Enable(false)
//        if (game+0xd0 bit 10) buttonMod->vtbl[+0x40](0)    // (flag re-read)
//   4. the BuildId child (0x1400b4282): FindOrLoad "Dialog/BuildId.rsc", kept only
//      if IsDerivedFrom(?_typeInfo@Dialog@UI@@) (else null); MemAlloc(0xb8, 8);
//      DialogInstance(_ui, rsc, parent=this, 0, 0, 0), table 0x14015f5a0;
//      _ui->AddDialog; FindElement("textBuildNotice"), kept only if
//      IsDerivedFrom(?_typeInfo@TextElement@UI@@) (import slot 0x140151508, read
//      2026-10-11; Ghidra mislabels it fn_LabelElement_SetSprite) else null;
//      SetParameter(0, CurrentVersion()), SetParameter(1, CurrentBuildId()).
//
// Retail guards nothing in step 4 but the allocation: a missing BuildId.rsc or
// a non-text textBuildNotice reaches the ctor / SetParameter with a null, and
// so does this body (CLAUDE.md: a faithful deref is load-bearing). The ONE
// guard is `_ui`: MainMenuDialog_FromMenuDLT's bare-bones fallback (a default-
// constructed dialog when the loop has no resource, a standalone-only path
// retail does not have) leaves _ui null, and only that dialog skips step 4.
void MainMenuDialog::OnOpened() {
    auto find = [this](const char* name) -> UI::Element* {
        return FindElement(System::String(name)).Get();
    };
    UI::Element* buttonNew         = find("buttonNew");
    UI::Element* buttonResume      = find("buttonResume");
    UI::Element* buttonLoad        = find("buttonLoad");
    UI::Element* buttonSave        = find("buttonSave");
    UI::Element* buttonOptions     = find("buttonOptions");
    UI::Element* buttonMenu        = find("buttonMenu");
    UI::Element* buttonQuit        = find("buttonQuit");
    UI::Element* buttonAchievement = find("buttonAchievement");
    UI::Element* buttonScenario    = find("buttonScenario");
    UI::Element* buttonGoal        = find("buttonGoal");
    UI::Element* buttonMap         = find("buttonMap");
    UI::Element* buttonTutorial    = find("buttonTutorial");
    UI::Element* buttonMod         = find("buttonMod");

    // Which buttons does this .rsc actually contain? A null here means the click can
    // never dispatch, because no element ever receives the id.
    LoadTime::Stamp("menu.opened");   // NOT RETAIL: print-only
    fprintf(stderr, "[MainMenu] OnOpened self=%p game=%p buttons: new=%p resume=%p "
                    "load=%p save=%p options=%p menu=%p quit=%p achv=%p scenario=%p "
                    "goal=%p map=%p tutorial=%p mod=%p\n",
            (void*)this, (void*)game,
            (void*)buttonNew, (void*)buttonResume, (void*)buttonLoad, (void*)buttonSave,
            (void*)buttonOptions, (void*)buttonMenu, (void*)buttonQuit,
            (void*)buttonAchievement, (void*)buttonScenario, (void*)buttonGoal,
            (void*)buttonMap, (void*)buttonTutorial, (void*)buttonMod);
    fflush(stderr);

    // --- 2. element ids, in retail's order ---
    if (buttonNew) buttonNew->SetElementId(kButton_New);
    if (buttonResume) {
        buttonResume->SetElementId(kButton_Resume);
        // The hot key 8 (the "Cancel" input, Esc): ButtonElement::Update fires
        // Resume on KeyMap::Pressed(0, 8), which is how Esc closes the pause menu
        // (the toolbar's menu button, also key 8, opens it). Phase D.4 SDL-C put
        // this store into the slim Setup; it is retail's, and stays here.
        static_cast<UI::ButtonElement*>(buttonResume)->_keyIndex = 8;   // +0xc0
    }
    if (buttonOptions)     buttonOptions->SetElementId(kButton_Options);
    if (buttonQuit)        buttonQuit->SetElementId(kButton_Quit);
    if (buttonLoad)        buttonLoad->SetElementId(kButton_Load);
    if (buttonSave)        buttonSave->SetElementId(kButton_Save);
    if (buttonMenu)        buttonMenu->SetElementId(kButton_Menu);
    if (buttonAchievement) buttonAchievement->SetElementId(kButton_Achievement);
    if (buttonScenario)    buttonScenario->SetElementId(kButton_Scenario);
    if (buttonGoal)        buttonGoal->SetElementId(kButton_Goal);
    if (buttonMap)         buttonMap->SetElementId(kButton_Map);
    if (buttonTutorial)    buttonTutorial->SetElementId(kButton_Tutorial);
    if (buttonMod)         buttonMod->SetElementId(kButton_Mod);

    // --- 3. the pause menu only (game set by the toolbar's pause path) ---
    if (game != nullptr) {
        // The scene is read through the GameUpdateLoop singleton, as retail
        // reads g_pGameWorldSingleton+0x78 for each call; the game flags below
        // are read through this->game, as retail reads this+0xb8.
        GameUpdateLoop::_singleton->m_audioScene->Pause(4, true);
        GameUpdateLoop::_singleton->m_audioScene->Pause(3, true);
        GameUpdateLoop::_singleton->m_audioScene->Pause(2, true);

        // A scenario game (index >= 0) hides Map; a free game hides Goal.
        if (GameUpdateLoop::_singleton->m_scenarioIndex < 0) {
            if (buttonGoal) buttonGoal->SetFlagsOnOff(1, 0);
        } else if (buttonMap) {
            buttonMap->SetFlagsOnOff(1, 0);
        }

        // Game flag bit 10 disables Save and Mod (retail re-reads it per button).
        if (((game->m_gameFlags >> 10) & 1) != 0 && buttonSave) buttonSave->Enable(false);
        if (((game->m_gameFlags >> 10) & 1) != 0 && buttonMod)  buttonMod->Enable(false);
        // FIX mods_persist (docs/retail_bugs.md section 23): retail lets the player change mods in a
        // game, then drops the change at the next menu (Loader::Load re-reads the list) after
        // reloading every changed resource twice. With the row on, Mods is greyed out in the pause
        // menu, as retail does for a game with flag bit 10; the list is changed from the main menu.
        if (Ext::Fix().mods_persist && buttonMod) buttonMod->Enable(false);
    }

    // --- 4. the BuildId child dialog (version + build id overlay) ---
    if (_ui == nullptr) {
        // Standalone-only: the bare-bones fallback dialog (see the banner).
        printf("[BuildId] no UserInterface (bare-bones menu dialog), skipping\n");
        return;
    }
    const UI::Dialog* buildIdRsc = nullptr;
    {
        System::Filename filename("Dialog/BuildId.rsc");
        const ::Object::External* resource =
            ::Object::ExternalFactory::_singleton->FindOrLoad(filename);
        if (resource && ::Object::IsDerivedFromNullSafe(resource, UI::Dialog::TypeId()))
            buildIdRsc = static_cast<const UI::Dialog*>(resource);
    }
    // The child is a plain UI::DialogInstance (retail stamps the exe's copy of its
    // table, 0x14015f5a0; the real class's compiler table is that table).
    UI::DialogInstance* child = nullptr;
    if (void* mem = System::MemoryAllocator::MemAlloc(0xB8, 8)) {
        child = ::new (mem) UI::DialogInstance(_ui, buildIdRsc,
                                               Object::InternalPtr<UI::DialogInstance>(this),
                                               nullptr, nullptr, 0);
    }
    Object::InternalPtr<UI::DialogInstance> childPtr(child);
    _ui->AddDialog(&childPtr);

    UI::Element* textElem = childPtr->FindElement(System::String("textBuildNotice")).Get();
    UI::TextElement* text = nullptr;
    if (::Object::IsDerivedFromNullSafe(textElem, UI::TextElement::TypeId()))
        text = static_cast<UI::TextElement*>(textElem);
    Object::InternalPtr<UI::TextElement> textBuildNotice(text);
    printf("[BuildId] rsc=%p child=%p textBuildNotice=%p\n",
           (const void*)buildIdRsc, (void*)child, (void*)text);

    // ?CurrentVersion@Configuration@System@@ is a by-value static.
    {
        System::String versionStr(System::Configuration::CurrentVersion());
        textBuildNotice->SetParameter(0, versionStr);
    }
    // ?CurrentBuildId@ has no member yet (Configuration.h note 2): its body is the
    // retslot function, called by name on the caller's zeroed slot.
    {
        System::String buildIdStr;
        RtSystem_Configuration_CurrentBuildId(&buildIdStr);
        // Standalone-only (README "Versioning"): retail's version and build id
        // stay on the left, untouched; OpenBA's own stamp follows on the right:
        // "17Se10  OpenBA v1.0.0 2424a9093 2026-10-19". OPENBA_VERSION is the
        // CMake project VERSION; the commit and date come from the header the
        // openba-build-info target regenerates on every build.
        buildIdStr += "  OpenBA v" OPENBA_VERSION " " OPENBA_GIT_HASH " " OPENBA_BUILD_DATE;
        printf("[BuildId] build text \"%s\"\n", buildIdStr._data);
        textBuildNotice->SetParameter(1, buildIdStr);
    }

    // EXT SEAM (NOT RETAIL, docs/extensions/handins/EXTSCAFFOLD.md): DESIGN.md §3 "Menu" -- the
    // Extensions button beside Mods. Retail's steps above are untouched; under
    // the gate's command-line faithful profile this adds nothing.
    ExtensionsDialog_AttachMenuButton(this);

    // EXT SEAM (NOT RETAIL, docs/extensions/handins/PHONEMENU.md): ext.phone_menu learns which
    // dialogs are the menu's (this one and the version line) for its layouts and backdrop fit;
    // `game` set means the pause menu.
    Ext::PhoneMenu::MenuOpened(_dialog, buildIdRsc, game != nullptr);
}

// ============================================================
// [5] OnClosed — retail 0x1400b3c60
// ============================================================
//   if (this+0xb8 != 0) {
//       Audio::Scene::Pause(g_pGameWorldSingleton+0x78, 4, false);
//       Audio::Scene::Pause(g_pGameWorldSingleton+0x78, 3, false);
//       Audio::Scene::Pause(g_pGameWorldSingleton+0x78, 2, false);   // tail JMP
//   }
// Phase D.4 GAPS (row 1275): was the free function MainMenuDialog_Slot5 while the
// member was a no-op, so closing the pause menu left the three sound types paused.
void MainMenuDialog::OnClosed() {
    if (game == nullptr) return;
    GameUpdateLoop::_singleton->m_audioScene->Pause(4, false);
    GameUpdateLoop::_singleton->m_audioScene->Pause(3, false);
    GameUpdateLoop::_singleton->m_audioScene->Pause(2, false);
}

// [8] OnElementEvent — button click dispatch. The runtime sends msg=0x12 with
// the element ID as the second int.
static void StandaloneOpenMapParams_UI(UI::UserInterface* ui);  // defined below (map dialog bring-up)

void MainMenuDialog::OnElementEvent(Object::InternalPtr<UI::Element>* /*element*/,
                                    int32_t msg, int32_t id, int32_t /*c*/,
                                    int32_t /*d*/, int32_t /*e*/, void* /*userData*/) {
    if (msg != 0x12) return;
    printf("[MainMenuDialog::OnCommand] self=%p msg=0x%x id=%d\n", (void*)this, msg, id);

    switch (id) {
    case kButton_New:
        printf("[MainMenuDialog::OnCommand] -> OpenNewGame\n");
        OpenNewGame();
        break;
    case kButton_Resume: {
        // Resume = close this in-game menu, which removes it from the UI's
        // dialog list. UI::UserInterface::PausesGame then returns false and
        // GameUpdateLoop::Update resumes ticking the simulation.
        printf("[MainMenuDialog::OnCommand] -> Resume (CloseDialog)\n");
        Object::InternalPtr<UI::DialogInstance> selfPtr(this);
        _ui->CloseDialog(&selfPtr);
        break;
    }
    case kButton_Quit:
        printf("[MainMenuDialog::OnCommand] Quit: Application::_singleton=%p\n",
               (void*)::Application::Application::_singleton);
        if (::Application::Application::_singleton) {
            printf("[MainMenuDialog::OnCommand] -> App::Quit\n");
            ::Application::Application::_singleton->Quit();
        } else {
            printf("[MainMenuDialog::OnCommand] WARN: App::Quit not resolved — falling back to PostQuitMessage\n");
            // Last resort — close the window so the user isn't stuck
            Platform::Window::PostQuitMessage(0);
        }
        break;
    case kButton_Menu:
        // "Quit to Main Menu" -- in-game only (buttonMenu is not in MainMenu.rsc,
        // only in Pause.rsc, so this case is unreachable from the front end).
        // ReturnToMenu takes the game != 0 branch: destroy the GameUpdateLoop's
        // SaveLoad, build MenuDialogLoopT -> GameLoadDialogLoopT -> LoaderLoop,
        // fade to black, and park the chain in m_nextLoop (+0xC8).
        // GameUpdateLoop::Update step 17 picks that up once the fade completes
        // (UI+0xF0 == 0) and calls MainLoop::SetNextLoop, which runs the ported
        // teardown (gameupdateloop_teardown.cpp) on the way out.
        printf("[MainMenuDialog::OnCommand] -> ReturnToMenu (gameUpdateLoop=%p)\n",
               (void*)game);
        ReturnToMenu();
        break;
    case kButton_Options:
        printf("[MainMenuDialog::OnCommand] -> OpenOptions\n");
        OpenOptions();
        break;
    case kButton_Load:
        // Save/load wave 2026-08-27: SaveLoadDialog ctor + vtable are now
        // registered, the menu-side Save::SaveLoad exists (+0xb70) and is
        // ticked — the retail load-from-save path is standalone-ready.
        printf("[MainMenuDialog::OnCommand] -> OpenLoad (SaveLoadDialog)\n");
        OpenLoad();
        break;
    case kButton_Save:
        printf("[MainMenuDialog::OnCommand] -> OpenSave (SaveLoadDialog)\n");
        OpenSave();
        break;
    case kButton_Map:
        printf("[MainMenuDialog::OnCommand] -> OpenMap (MapParamsDialog)\n");
        StandaloneOpenMapParams_UI(_ui);
        break;
    case kButton_Tutorial:
        // The main menu .rsc has NO buttonScenario - the [Setup] log prints it
        // "NOT FOUND". Scenarios live behind the Tutorial button, which opens the same
        // ScenarioDialog with difficulty=1.
        printf("[MainMenuDialog::OnCommand] -> OpenTutorial (ScenarioDialog, difficulty=1)\n");
        OpenTutorial();
        break;
    case kButton_Scenario:
        printf("[MainMenuDialog::OnCommand] -> OpenScenario (ScenarioDialog, difficulty=0)\n");
        OpenScenario();
        break;
    case kButton_Achievement:
        // Wired 2026-09-05 (front end AND in-game pause menu, same dialog class).
        printf("[MainMenuDialog::OnCommand] -> OpenAchievement (AchievementDialog)\n");
        OpenAchievement();
        break;
    case kButton_Goal:
        printf("[MainMenuDialog::OnCommand] -> OpenGoal (GoalDialog)\n");
        OpenGoal();
        break;
    case kButton_Mod:
        // ModDialog/ModDetailsDialog live in src/game/Dialog/mod_dialog_vtables.cpp.
        // Default-on since 2026-09-05 (BANISHED_LIVE_MODDIALOG=0 is the kill-switch).
        printf("[MainMenuDialog::OnCommand] -> OpenMods (ModDialog)\n");
        ModDialog_OpenFromMainMenu(this);
        break;
    case kExtensionsMenuButtonId:
        // EXT SEAM (NOT RETAIL, docs/extensions/handins/EXTSCAFFOLD.md): the Extensions button ExtensionsDialog_
        // AttachMenuButton adds in OnOpened. Retail has no id 13.
        printf("[MainMenuDialog::OnCommand] -> OpenExtensions (ExtensionsDialog)\n");
        ExtensionsDialog_OpenFromMainMenu(this);
        break;
    default:
        printf("[MainMenuDialog::OnCommand] button id=%d not yet wired in standalone\n", id);
        break;
    }
}

// ============================================================
// MapParamsDialog open path — in-game "Map" button (id 11)
// ============================================================
// buttonMap -> Dialog/MapParams.rsc (the map name/seed/terrain/climate/
// condition/disaster info dialog). Builds the real MapParamsDialog
// (mapparamsdialog.cpp) and AddDialog's it (which dispatches its OnOpened).
static void StandaloneOpenMapParams_UI(UI::UserInterface* ui) {
    if (!ui) { printf("[OpenMap] ui null\n"); return; }
    if (!System::MemoryAllocator::_singleton) {   // B3R: the g_pMemAllocSingleton snapshot, read live
        printf("[OpenMap] core fns unresolved\n"); return;
    }
    UI::Dialog* extRes = LoadDialogResource("Dialog/MapParams.rsc");
    if (!extRes) { printf("[OpenMap] Dialog/MapParams.rsc not loaded\n"); return; }
    void* dlgMem = System::MemoryAllocator::MemAlloc(sizeof(MapParamsDialog), 8);
    if (!dlgMem) return;
    MapParamsDialog* dlg = ::new (dlgMem) MapParamsDialog(
        ui, extRes, Object::InternalPtr<UI::DialogInstance>());
    Object::InternalPtr<UI::DialogInstance> ibp(dlg);
    ui->AddDialog(&ibp);
    printf("[OpenMap] MapParamsDialog opened + AddDialog'd (dlg=%p)\n", (void*)dlg);
}

// Harness entry — open the map dialog headlessly (the game's UserInterface).
extern "C" void MapParamsDialog_HarnessOpen() {
    GameUpdateLoop* gw = GameUpdateLoop::_singleton;
    if (!gw) { printf("[OpenMap] gameWorld null\n"); return; }
    UI::UserInterface* ui = gw->m_userInterface;
    printf("[OpenMap] harness open: ui=%p\n", (void*)ui);
    StandaloneOpenMapParams_UI(ui);
}

// ============================================================
// Construction, destruction, reflection
// ============================================================
// Retail ctor 0x14009BA30 (from MenuDLT): DialogInstance::DialogInstance with
// (this, the loop's UserInterface, the loop's dialog resource, nullParent, 0, 0, 0),
// then the two MainMenuDialog fields:
//   game     = the running game (0 at the menu; set by the in-game pause path)
//   menuLoop = the OWNING LOOP (param_1 == the MenuDLT) — `*(this+0xc0) = param_1`.
// This wiring is load-bearing: StartNewGame reads menuLoop to decide its
// transition path. If it is 0, StartNewGame falls into the
// SetEngineNextLoop(game) branch and dereferences a NULL game. With menuLoop set
// it correctly takes SetDialogLoaderNextLoop(loop) and writes loop+0xb68.

MainMenuDialog::MainMenuDialog() : game(nullptr), menuLoop(nullptr) {}

MainMenuDialog::MainMenuDialog(UI::UserInterface* ui, const UI::Dialog* dialog,
                               Object::InternalPtr<UI::DialogInstance> parent,
                               GameUpdateLoop* gameLoop, MenuDialogLoopT* loop)
    : UI::DialogInstance(ui, dialog, parent, nullptr, nullptr, 0),
      game(gameLoop), menuLoop(loop) {}

// create 0x140020d10: MemAlloc(0xc8, 8), then the ctor.
::Object::Base* MainMenuDialog::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(MainMenuDialog), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(::new (p) MainMenuDialog());
}
// createInPlace 0x140020d60 (the tree's in-place build is kept, B8-13).
::Object::Base* MainMenuDialog::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(::new (storage) MainMenuDialog());
}


// Public entry called from MenuDLT::CreateDialog (standalone/menudialogloopt_stubs.cpp).
// The loop's dialog resource is always loaded by then; a loop without one
// keeps the tree's bare-bones fallback (a default-constructed dialog, no element
// tree) instead of the Runtime ctor faulting on a null Dialog — a standalone-only
// guard retail does not have.
// B4 FORWARD: 1 outside caller (Game/menudialogloopt_stubs.cpp), which
// holds the loop as its `this` and the return slot as void*.
extern "C" void* MainMenuDialog_FromMenuDLT(MenuDialogLoopT* loop, void* retInternalBasePtr) {
    if (!System::MemoryAllocator::_singleton) {   // B3R: the g_pMemAllocSingleton snapshot, read live
        if (retInternalBasePtr) {
            ::new (retInternalBasePtr) Object::InternalPtr<UI::DialogInstance>();
        }
        return retInternalBasePtr;
    }
    void* mem = System::MemoryAllocator::MemAlloc(sizeof(MainMenuDialog), 8);
    MenuDialogLoopT* menuLoop = loop;
    const UI::Dialog* rsc = menuLoop ? menuLoop->_dialogExternal : nullptr;
    MainMenuDialog* dlg;
    if (rsc) {
        dlg = ::new (mem) MainMenuDialog(&menuLoop->_ui, rsc,
                                         Object::InternalPtr<UI::DialogInstance>(),
                                         nullptr, menuLoop);
    } else {
        printf("[MainMenuDialog_FromLoop] bare-bones init (loop=%p rsc=%p) — "
               "DialogInstance ctor skipped\n", loop, (const void*)rsc);
        dlg = ::new (mem) MainMenuDialog();
        dlg->menuLoop = menuLoop;
    }
    ::new (retInternalBasePtr) Object::InternalPtr<UI::DialogInstance>(dlg);
    return retInternalBasePtr;
}

DEFINE_EXE_TYPE(MainMenuDialog, "UI::DialogInstance")
