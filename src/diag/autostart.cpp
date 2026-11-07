// src/diag/autostart.cpp
// See autostart.h for the rationale. Env-var-gated state machine that drives
// the menu -> New Game -> Start sequence by injecting synthetic dialog
// slot-8 events, reusing the already-reimplemented click handlers.


#include "autostart.h"
#include "mod_click_guard.h"   // ModDialogClickRowInRange (review F-294)
#include "options.h"
#include "clamped_atoi.h"     // review F-087: checked parse of BANISHED_* text
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include "save/SaveLoad.h"   // -Iruntime/types: ENDGAME B.3.7 callers (K5)

// Captured by MainMenuDialog::OpenNewGame (src/game/Dialog/mainmenudialog.cpp).
class NewGameDialog;    // Dialog/NewGameDialog.h
extern "C" {
    NewGameDialog* g_StandaloneNewGameDialog = nullptr;
}
// Captured by MainMenuDialog_OpenLoad (src/game/Dialog/mainmenudialog.cpp; its local
// declaration still spells the pointer void*: the same symbol, the stored value is the
// dialog it creates).
class SaveLoadDialog;   // Dialog/SaveLoadDialog.h
SaveLoadDialog* g_StandaloneSaveLoadDialog = nullptr;
// Captured by MainMenuDialog_OpenTutorial (same file, same void* local declaration).
// ScenarioDialog and TutorialDialog are ONE class -- the Tutorial button is the only
// way to reach it.
class ScenarioDialog;   // Dialog/ScenarioDialog.h
ScenarioDialog* g_StandaloneScenarioDialog = nullptr;

#include "ui/UserInterface.h"
#include "ui/DialogInstance.h"
#include "object/Internal.h"
#include "Game/MenuDialogLoopT.h"     // B4: TickMenu's typed menu loop
#include "Dialog/ModDialog.h"         // B4: ModDialog::entries
#include "Dialog/SaveLoadDialog.h"    // B4: SaveLoadDialog list fields
#include "Dialog/ScenarioDialog.h"    // B4: ScenarioDialog's entries / selectedIndex
#include "Dialog/NewGameDialog.h"     // B4: NewGameDialog::buttonOk
#include "Dialog/ModDetailsDialog.h"  // B4: ModDetailsDialog is-a UI::DialogInstance (InjectClick)

// src/game/Dialog/mod_dialog_vtables.cpp -- targets for BANISHED_AUTOMENU_CLICKS
class ModDetailsDialog;
extern "C" ModDialog*        ModDialog_LastOpened();
extern "C" ModDetailsDialog* ModDetailsDialog_LastOpened();

namespace AutoStart {

namespace {

// Dialog command/event protocol (matches the UI dispatcher).
constexpr int kButtonId_New    = 0;   // MainMenuDialog "buttonNew"
constexpr int kButtonId_Load   = 2;   // MainMenuDialog "buttonLoad" (kButton_Load)
constexpr int kButtonId_Quit   = 6;   // MainMenuDialog "buttonQuit"
constexpr int kEventId_Ok      = 2;   // NewGameDialog "buttonOk" (Start)
constexpr int kSLD_LoadClick   = 2;   // SaveLoadDialog OnEvent 0x12/2 = buttonLoad
constexpr int kButtonId_Tutorial = 12;    // MainMenuDialog "buttonTutorial" (kButton_Tutorial)
constexpr int kSD_LoadClick      = 0x65;  // ScenarioDialog Slot8 0x12/101 = LoadSelected

// State of the driver.
enum class Phase {
    Disabled,
    WaitMenuSettle,   // menu up; counting down before firing New Game
    WaitNewGame,      // New Game opened; waiting for its Slot4 (combos/OK)
    WaitQuit,         // menu up; counting down before firing Quit
    WaitLoadMenu,     // menu up; counting down before firing Load (AUTOLOAD)
    WaitLoadDialog,   // SaveLoadDialog open; waiting for the file list, then click Load
    WaitTutorialMenu, // menu up; counting down before firing Tutorial (AUTOTUTORIAL)
    WaitTutorialDialog, // ScenarioDialog open; waiting for its entry list, then click Load
    WaitMenuButton,   // menu up; counting down before firing an arbitrary button id
    MenuClicks,       // AUTOMENU_BUTTON fired; feeding BANISHED_AUTOMENU_CLICKS tokens
    Done,
};

// BANISHED_AUTOMENU_CLICKS=<tok>[,<tok>...]: follow-up clicks delivered one per
// settle interval after the AUTOMENU_BUTTON click. Token forms:
//   <id>   click element id <id> on the most recently opened ModDialog
//   d<id>  click element id <id> on the most recently opened ModDetailsDialog
//   m<id>  click element id <id> on the MainMenuDialog (e.g. m7 re-opens Mods)
// Only the mod dialogs are addressable for now; add a *_LastOpened() per
// dialog type to extend it.
char  s_menuClicks[256] = {0};
char* s_menuClickCursor = nullptr;

// AUTOLOAD: name of the save to load (defaults to the autosave, "Auto").
char s_loadName[64] = "Auto";

// AUTOTUTORIAL: which tutorial entry to load (index into the ScenarioDialog list).
int s_tutorialIndex = 0;

// AUTOMENU_BUTTON: an arbitrary MainMenuDialog element id to click at the front end,
// then leave the result on screen. The in-game half of every menu button already has a
// driver (BANISHED_PAUSEMENU_BUTTON, src/diag/pausemenu_harness.cpp); this is the
// front-end half, so a menu-only dialog (Achievements id 8, Goals id 10, ...) can be
// brought up and screenshotted with nobody at the mouse.
int s_menuButtonId = -1;

bool   s_initialized   = false;
bool   s_enabled       = false;
Phase  s_phase         = Phase::Disabled;
int    s_settleFrames  = 60;     // frames to let the menu render before firing
int    s_phaseFrame    = 0;      // frames spent in the current phase
int    s_newGameFrame  = 0;      // frames since New Game opened (Slot4 grace)

// BANISHED_AUTOSTART_GAMES=<n>: start n games in one process, not one. After the
// Start click the driver rearms instead of finishing, so a quit-to-main-menu (see
// src/diag/pausemenu_harness.cpp, BANISHED_PAUSEMENU_BUTTON=5) is followed by a
// second New Game. That sequence is what catches teardown bugs a single-game run
// cannot see -- anything the world teardown frees while a process-lifetime cache
// still points at it faults on game 2, not game 1.
int    s_gamesLeft     = 1;      // New Games still to start
void*  s_lastMenu      = nullptr;// the MainMenuDialog we last clicked New Game on


// Safety timeout: if the NewGameDialog never finishes setup, give up rather
// than firing Start against half-built combos (which would crash).
constexpr int kNewGameTimeoutFrames = 600;   // ~10s at 60fps

// Invoke a dialog's UI::DialogInstance::OnElementEvent (retail slot 8, +0x40)
// as a synthetic button click: (element=null, msg, id). Every dialog driven
// here (MainMenu / NewGame / SaveLoad / ModDetails / Mod) derives from
// UI::DialogInstance. The three trailing ints and userData were never set by
// the old raw 4-argument call; the by-name call passes 0 / nullptr. The four
// trailing arguments are the harness's choice: retail has no such caller.
void InjectClick(UI::DialogInstance* dialog, UI::EventId msg, int id) {
    dialog->OnElementEvent(
        nullptr, msg, id, 0, 0, 0, nullptr);
}

}  // namespace

void Init() {
    if (s_initialized) return;
    s_initialized = true;

    // BANISHED_AUTOQUIT drives menu -> Quit instead of menu -> New Game. It
    // exercises the only retail exit path (MainMenuDialog buttonQuit ->
    // Application::Quit -> MainLoop::Run returns -> Engine::Shutdown), which is
    // what runs Video::Configuration::Save + Persistence::WriteSettings. Use it
    // to verify settings actually round-trip to settings.cfg.
    if (Opt().autoquit) {
        if (Opt().autoquitFrames > 1) s_settleFrames = Opt().autoquitFrames;
        s_enabled = true;
        s_phase   = Phase::WaitQuit;
        fprintf(stderr, "[AutoStart] AUTOQUIT ENABLED — will "
               "click Quit after %d menu frames\n", s_settleFrames);
        return;
    }

    // BANISHED_AUTOLOAD drives menu -> Load -> select save -> Load click, the
    // full retail load-from-save path (SaveLoadDialog list scan, DoLoad,
    // SetOperation(2 kOpLoad), loader-loop chain, GameUpdateLoop::Load restore branch).
    // Value: a save NAME to load ("Auto", "Save0", ...); "1" defaults to the
    // autosave "Auto". (Save/load wave 2026-08-27.)
    if (Opt().autoload) {
        if (Opt().autoloadName) {
            std::snprintf(s_loadName, sizeof(s_loadName), "%s", Opt().autoloadName);
        }
        s_enabled = true;
        s_phase   = Phase::WaitLoadMenu;
        fprintf(stderr, "[AutoStart] AUTOLOAD ENABLED — will click Load after %d "
               "menu frames, then load save \"%s\"\n", s_settleFrames, s_loadName);
        return;
    }

    // BANISHED_AUTOTUTORIAL drives menu -> Tutorial -> select entry -> Load click.
    // That is the ONLY path to a retail-authored save: the tutorial .sav files live
    // inside WinData/*.pkg, and ScenarioDialog::LoadSelected swaps persistence+0x90
    // to the data directory so SaveLoad reads them. Value: the entry index to load
    // ("1" or empty means index 0).
    if (Opt().autotutorial) {
        s_tutorialIndex = Opt().autotutorialIndex;
        s_enabled = true;
        s_phase   = Phase::WaitTutorialMenu;
        // This block RETURNS before BANISHED_AUTOSTART_GAMES is parsed below, so
        // read it here and default to ZERO on the tutorial path. Defaulting to
        // s_gamesLeft's initial 1 would silently make every existing AUTOTUTORIAL
        // run start a New Game after a quit-to-menu, which is not what those runs
        // have always meant.
        s_gamesLeft = 0;
        if (Opt().autostartGames > 0) {
            s_gamesLeft = Opt().autostartGames;
            fprintf(stderr, "[AutoStart] AUTOTUTORIAL + %d New Game(s) after a "
                   "quit-to-menu (BANISHED_AUTOSTART_GAMES)\n", s_gamesLeft);
        }
        fprintf(stderr, "[AutoStart] AUTOTUTORIAL ENABLED — will click Tutorial after "
               "%d menu frames, then load entry #%d\n",
               s_settleFrames, s_tutorialIndex);
        return;
    }

    // BANISHED_AUTOMENU_BUTTON=<id> clicks one front-end menu button and stops there,
    // leaving whatever it opened up for a screenshot (tools/capture_window.ps1).
    if (Opt().automenuButton != 0) {
        s_menuButtonId = Opt().automenuButton;
        s_enabled = true;
        s_phase   = Phase::WaitMenuButton;
        if (const char* c = Opt().automenuClicks) {
            std::strncpy(s_menuClicks, c, sizeof(s_menuClicks) - 1);
            s_menuClickCursor = s_menuClicks[0] ? s_menuClicks : nullptr;
            if (s_menuClickCursor)
                fprintf(stderr, "[AutoStart] AUTOMENU_CLICKS queued: %s\n", s_menuClicks);
        }
        fprintf(stderr, "[AutoStart] AUTOMENU_BUTTON ENABLED - will click menu id=%d "
               "after %d menu frames and stop there\n", s_menuButtonId, s_settleFrames);
        return;
    }
    if (!Opt().autostart) {
        s_enabled = false;
        s_phase   = Phase::Disabled;
        fprintf(stderr, "[AutoStart] disabled (BANISHED_AUTOSTART unset)\n");
        return;
    }

    // A numeric value overrides the menu settle-frame count.
    if (Opt().autostartFrames > 1) s_settleFrames = Opt().autostartFrames;

    if (Opt().autostartGames > 1) {
        s_gamesLeft = Opt().autostartGames;
        fprintf(stderr, "[AutoStart] will start %d games this run "
               "(BANISHED_AUTOSTART_GAMES)\n", s_gamesLeft);
    }

    s_enabled = true;
    s_phase   = Phase::WaitMenuSettle;
    fprintf(stderr, "[AutoStart] ENABLED — will auto-open "
           "New Game after %d menu frames, then auto-Start\n",
           s_settleFrames);
}

bool Enabled() { return s_enabled; }

void TickMenu(MenuDialogLoopT* menuDLT) {
    if (!s_enabled || s_phase == Phase::Disabled || s_phase == Phase::Done)
        return;
    if (!menuDLT) return;

    switch (s_phase) {
    case Phase::WaitMenuSettle: {
        // The MainMenuDialog is created + set up (Slot4) by the menu loop's
        // Startup path; it lands at MenuDLT+0x38. Wait for it, then let the
        // menu render for s_settleFrames before injecting the New Game click.
        UI::DialogInstance* mainMenu = menuDLT->_dialogInstance;
        if (!mainMenu) {
            s_phaseFrame = 0;   // not ready yet — keep waiting
            return;
        }
        // On a rearm (BANISHED_AUTOSTART_GAMES > 1) the old menu keeps ticking for
        // a few frames after the Start click, before the loop transition takes
        // hold. Refuse to fire on that same dialog — wait for the fresh one a
        // quit-to-menu builds, or we would re-enter New Game on a dying menu.
        if (mainMenu == s_lastMenu) {
            s_phaseFrame = 0;
            return;
        }
        if (++s_phaseFrame < s_settleFrames) return;

        fprintf(stderr, "[AutoStart] menu settled (mainMenu=%p) — injecting New Game "
               "click\n", (void*)mainMenu);
        s_lastMenu = mainMenu;
        InjectClick(mainMenu, UI::EventId::Activated, kButtonId_New);
        s_phase        = Phase::WaitNewGame;
        s_newGameFrame = 0;
        break;
    }

    case Phase::WaitNewGame: {
        ++s_newGameFrame;
        NewGameDialog* newGame = g_StandaloneNewGameDialog;
        // NewGameDialog::Slot4 (Startup) finds buttonOk and populates the
        // combos. buttonOk != null is our proxy that setup finished, so
        // StartGame can safely read the combo selections.
        bool setupDone = newGame && newGame->buttonOk != nullptr;   // +0xF0

        if (setupDone) {
            fprintf(stderr, "[AutoStart] NewGameDialog ready (self=%p) — injecting "
                   "Start click (buttonOk)\n", newGame);
            InjectClick(newGame, UI::EventId::Activated, kEventId_Ok);
            if (--s_gamesLeft > 0) {
                // Rearm for the next game. WaitMenuSettle refuses to fire on the
                // menu we just clicked (s_lastMenu), so the few frames the old menu
                // still ticks before the transition cannot re-trigger New Game; it
                // waits for the fresh MainMenuDialog a quit-to-menu rebuilds.
                s_phase        = Phase::WaitMenuSettle;
                s_phaseFrame   = 0;
                s_newGameFrame = 0;
                fprintf(stderr, "[AutoStart] sequence fired — new-game transition queued; "
                       "%d more game(s) to start, rearmed\n", s_gamesLeft);
                return;
            }
            s_phase = Phase::Done;
            fprintf(stderr, "[AutoStart] sequence fired — new-game transition queued; "
                   "driver done\n");
            return;
        }

        if (s_newGameFrame > kNewGameTimeoutFrames) {
            fprintf(stderr, "[AutoStart] WARN: NewGameDialog setup did not complete in "
                   "%d frames (self=%p) — aborting auto-Start to avoid a crash "
                   "on half-built combos\n",
                   kNewGameTimeoutFrames, newGame);
            s_phase = Phase::Done;
        }
        break;
    }

    case Phase::WaitMenuButton: {
        UI::DialogInstance* mainMenu = menuDLT->_dialogInstance;
        if (!mainMenu) {
            s_phaseFrame = 0;
            return;
        }
        if (++s_phaseFrame < s_settleFrames) return;

        fprintf(stderr, "[AutoStart] menu settled (mainMenu=%p) - injecting click id=%d\n",
                (void*)mainMenu, s_menuButtonId);
        InjectClick(mainMenu, UI::EventId::Activated, s_menuButtonId);
        s_phase      = s_menuClickCursor ? Phase::MenuClicks : Phase::Done;
        s_phaseFrame = 0;
        break;
    }

    case Phase::MenuClicks: {
        if (++s_phaseFrame < s_settleFrames) return;
        s_phaseFrame = 0;
        if (!s_menuClickCursor || !*s_menuClickCursor) { s_phase = Phase::Done; break; }
        char* tok = s_menuClickCursor;
        char* comma = std::strchr(tok, ',');
        if (comma) { *comma = '\0'; s_menuClickCursor = comma + 1; }
        else       { s_menuClickCursor = nullptr; }

        if (tok[0] == 'm') {
            UI::DialogInstance* mainMenu = menuDLT->_dialogInstance;
            int id = HarnessAtoi(tok + 1);
            fprintf(stderr, "[AutoStart] AUTOMENU_CLICKS: MainMenuDialog=%p id=%d\n", (void*)mainMenu, id);
            if (mainMenu) InjectClick(mainMenu, UI::EventId::Activated, id);
        } else if (tok[0] == 'd') {
            ModDetailsDialog* dlg = ModDetailsDialog_LastOpened();
            int id = HarnessAtoi(tok + 1);
            fprintf(stderr, "[AutoStart] AUTOMENU_CLICKS: ModDetailsDialog=%p id=%d\n", dlg, id);
            if (dlg) InjectClick(dlg, UI::EventId::Activated, id);
        } else {
            ModDialog* dlg = ModDialog_LastOpened();
            int id = HarnessAtoi(tok);
            fprintf(stderr, "[AutoStart] AUTOMENU_CLICKS: ModDialog=%p id=%d\n", static_cast<void*>(dlg), id);
            // A fabricated click on a per-row id (1000..5999: row = id % 1000)
            // must name a row the dialog actually built. The real UI cannot
            // deliver such a click without the element, but this driver can,
            // and with no .pkm installed (WinData/_mods_off holds them) the
            // entry array count at +0xC0 is 0 and the row handler faults reading
            // its entry. Mirror WaitLoadDialog's saveCount gate: skip loudly. Move up
            // on row 0 and move down on the last row name a neighbour row that does
            // not exist either (review F-294; mod_click_guard.h).
            ModDialog* modDlg = dlg;
            if (dlg && id >= 1000 && id < 6000) {
                const int rows = modDlg->entries.GetSize();   // +0xC0, entries' count
                const int row  = id % 1000;
                if (!ModDialogClickRowInRange(id, rows)) {
                    fprintf(stderr, "[AutoStart] WARN: AUTOMENU_CLICKS token %d names row %d "
                                    "(or its neighbour) but the ModDialog has %d row(s) -- skipped "
                                    "(are any .pkm files installed in WinData?)\n", id, row, rows);
                    dlg = nullptr;
                }
            }
            if (dlg) {
                // ModDialog::OnElementEvent reads *element as the element
                // InternalPtr, so hand it a real (null) qword. The four trailing
                // arguments (0 / 0 / 0 / nullptr; the old raw 4-argument call
                // left them unset) are the harness's choice: retail has no such
                // caller.
                Object::InternalPtr<UI::Element> nullElem;
                modDlg->OnElementEvent(&nullElem, UI::EventId::Activated, id, 0, 0, 0, nullptr);
            }
        }
        if (!s_menuClickCursor) s_phase = Phase::Done;
        break;
    }

    case Phase::WaitLoadMenu: {
        UI::DialogInstance* mainMenu = menuDLT->_dialogInstance;
        if (!mainMenu) {
            s_phaseFrame = 0;
            return;
        }
        if (++s_phaseFrame < s_settleFrames) return;

        fprintf(stderr, "[AutoStart] menu settled (mainMenu=%p) — injecting Load click\n",
                (void*)mainMenu);
        InjectClick(mainMenu, UI::EventId::Activated, kButtonId_Load);
        s_phase        = Phase::WaitLoadDialog;
        s_newGameFrame = 0;
        break;
    }

    case Phase::WaitLoadDialog: {
        ++s_newGameFrame;
        SaveLoadDialog* dlg = g_StandaloneSaveLoadDialog;
        if (!dlg) {
            if (s_newGameFrame > kNewGameTimeoutFrames) {
                fprintf(stderr, "[AutoStart] WARN: SaveLoadDialog never appeared — "
                       "aborting AUTOLOAD\n");
                s_phase = Phase::Done;
            }
            return;
        }
        SaveLoadDialog* d = dlg;
        // Wait for the list scan: refreshFlag (+0xBB) cleared AND saveCount
        // (+0xC4) populated by SaveLoadDialog_Load's refresh pass.
        int saveCount = d->saveCount;
        if (d->refreshFlag != 0 || saveCount <= 0) {
            if (s_newGameFrame > kNewGameTimeoutFrames) {
                fprintf(stderr, "[AutoStart] WARN: save list never populated "
                       "(refreshFlag=%d count=%d) — aborting AUTOLOAD\n",
                       d->refreshFlag, saveCount);
                s_phase = Phase::Done;
            }
            return;
        }

        // Pick the save whose SaveInfo name (+0x38 String) matches s_loadName;
        // fall back to index 0.
        int pick = 0;
        if (::Save::SaveLoad::_singleton) {
            for (int i = 0; i < saveCount; i++) {
                const ::Save::SaveLoad::SaveInfo* info = &::Save::SaveLoad::_singleton->GetSaveInfo(i);
                const char* nm = info ? info->name._data : nullptr;   // +0x38
                if (nm && std::strcmp(nm, s_loadName) == 0) { pick = i; break; }
            }
        }
        d->selectedIndex = pick;   // +0xC0 (SelectEntry sans sound)
        fprintf(stderr, "[AutoStart] save list ready (%d entries) — selecting #%d "
               "(\"%s\") and injecting Load click\n", saveCount, pick, s_loadName);
        InjectClick(dlg, UI::EventId::Activated, kSLD_LoadClick);
        // DoLoad runs inside the click, and on an unattended run every modal on its
        // path (confirmLoad, the mods / `ext:` notices) is answered there and then
        // (SaveLoad_ModalShowDialog). So loadFlag (+0xBA) is already 1 if the load
        // was queued and still 0 if an answer declined it (BANISHED_MODAL_ANSWER
        // other than 2). A declined load leaves the menu up with nothing left to
        // drive, and the run would sit there until something killed it: quit
        // through the menu's own Quit button instead (retail's exit path, as
        // AUTOQUIT), after the usual settle.
        if (d->loadFlag == 0) {
            fprintf(stderr, "[AutoStart] AUTOLOAD: the load was declined (a modal "
                   "answered other than 2) - quitting from the menu after %d frames\n",
                   s_settleFrames);
            s_phase      = Phase::WaitQuit;
            s_phaseFrame = 0;
            break;
        }
        s_phase = Phase::Done;
        fprintf(stderr, "[AutoStart] AUTOLOAD sequence fired — load operation queued; "
               "driver done\n");
        break;
    }

    case Phase::WaitTutorialMenu: {
        UI::DialogInstance* mainMenu = menuDLT->_dialogInstance;
        if (!mainMenu) {
            s_phaseFrame = 0;
            return;
        }
        if (++s_phaseFrame < s_settleFrames) return;

        fprintf(stderr, "[AutoStart] menu settled (mainMenu=%p) — injecting Tutorial "
                "click\n", (void*)mainMenu);
        InjectClick(mainMenu, UI::EventId::Activated, kButtonId_Tutorial);
        s_phase        = Phase::WaitTutorialDialog;
        s_newGameFrame = 0;
        break;
    }

    case Phase::WaitTutorialDialog: {
        ++s_newGameFrame;
        ScenarioDialog* dlg = g_StandaloneScenarioDialog;
        if (!dlg) {
            if (s_newGameFrame > kNewGameTimeoutFrames) {
                fprintf(stderr, "[AutoStart] WARN: ScenarioDialog never appeared — "
                       "aborting AUTOTUTORIAL\n");
                s_phase = Phase::Done;
            }
            return;
        }
        ScenarioDialog* d = dlg;
        // Slot4 fills the entry array (+0xD8) and its count (+0xE0) from the
        // scenario resource list. Count > 0 is our proxy that setup finished.
        int count = d->entries.GetSize();   // +0xE0
        if (count <= 0) {
            if (s_newGameFrame > kNewGameTimeoutFrames) {
                fprintf(stderr, "[AutoStart] WARN: tutorial list never populated "
                       "(count=%d) — aborting AUTOTUTORIAL\n", count);
                s_phase = Phase::Done;
            }
            return;
        }

        int pick = s_tutorialIndex;
        if (pick < 0 || pick >= count) {
            fprintf(stderr, "[AutoStart] AUTOTUTORIAL index %d out of range (count=%d) "
                    "— clamping to 0\n", pick, count);
            pick = 0;
        }
        // The ctor seeds selectedIndex = -1; LoadSelected indexes the entry array
        // with it unguarded, so it MUST be set before the click.
        d->selectedIndex = pick;   // +0xC8

        fprintf(stderr, "[AutoStart] tutorial list ready (%d entries) — selecting #%d "
                "and injecting Load click\n", count, pick);
        InjectClick(dlg, UI::EventId::Activated, kSD_LoadClick);
        // BANISHED_AUTOSTART_GAMES=<n> alongside AUTOTUTORIAL rearms the driver so a
        // quit-to-main-menu (PAUSEMENU_BUTTON=5) is followed by n NEW GAMES. Added
        // 2026-09-07 for a user-reported repro that the pre-existing games-loop could
        // not express: new -> quit -> new is CLEAN, but TUTORIAL -> quit -> new builds
        // an empty world. The distinction matters because a tutorial is a loaded
        // retail save and a new game is a generated world, so they tear down through
        // different paths -- and only the tutorial's leaves the next world broken.
        if (s_gamesLeft > 0) {
            s_phase        = Phase::WaitMenuSettle;
            s_phaseFrame   = 0;
            s_newGameFrame = 0;
            s_lastMenu     = nullptr;   // the tutorial never clicked a MainMenuDialog
            fprintf(stderr, "[AutoStart] AUTOTUTORIAL sequence fired — rearmed for %d "
                   "New Game(s) after a quit-to-menu\n", s_gamesLeft);
            break;
        }
        s_phase = Phase::Done;
        fprintf(stderr, "[AutoStart] AUTOTUTORIAL sequence fired — driver done\n");
        break;
    }

    case Phase::WaitQuit: {
        UI::DialogInstance* mainMenu = menuDLT->_dialogInstance;
        if (!mainMenu) {
            s_phaseFrame = 0;
            return;
        }
        if (++s_phaseFrame < s_settleFrames) return;

        fprintf(stderr, "[AutoStart] menu settled (mainMenu=%p) — injecting Quit click\n",
               (void*)mainMenu);
        InjectClick(mainMenu, UI::EventId::Activated, kButtonId_Quit);
        s_phase = Phase::Done;
        break;
    }

    default:
        break;
    }
}

}  // namespace AutoStart

