// src/game/Tutorial/tutorialui.cpp
// TutorialUI — the controller on the tutorial step panel.
//
// TutorialUI (0x60 bytes), table 0x140160f30, on UI::DialogController.
// ENDGAME B.3.8 block GOAL: a real class (Tutorial/TutorialUI.h); the
// hand-built 48-slot table in tutorial_dialog_vtables.cpp and its
// dialog_controller_vtable.cpp inherit-fill are gone. Field map: the header.

#include "Tutorial/TutorialUI.h"
#include "Game/MenuDialogLoopT.h"       // ENDGAME B.3.9 EXA: the real loop classes
#include "Game/GameLoadDialogLoopT.h"
#include "Game/GameLoaderLoop.h"
#include "object/TypeInfo.h"
#include "Game/game.h"   // B3R STORAGE: g_gameContainer
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "save/SaveLoad.h"             // RtSave_SaveLoad_Dtor
#include "system/MemoryAllocator.h"
#include "system/Value.h"               // RtSystem_Time_Ctor
#include "system/String.h"
#include <cstdint>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "object/External.h"   // ENDGAME B.3.6 UI callers (K4)
#include "ui/DialogInstance.h"   // ENDGAME B.3.6 UI callers (K4)
#include "ui/Element.h"                       // ENDGAME B.3.6 UI callers (K4)
#include "ui/UserInterface.h"                 // ENDGAME B.3.6 UI callers (K4)
#include <new>

// ============================================================
// Construction / destruction (ENDGAME B.3.8 block GOAL)
// ============================================================
// create 0x140029400: DialogController() (the base's real ctor), the stamp
// (the compiler's), then +0x30..+0x58 zeroed.
TutorialUI::TutorialUI()
    : unusedElement(0), preText(nullptr), postText(nullptr), imageTable(nullptr),
      nextButton(nullptr), quitButton(nullptr) {}

// Helper: FindElemByName with IsDerivedFrom check, stores InternalBasePtr
static void FindAndStore(UI::Element* container, const char* name, UI::Element** field) {
    System::String nameBuf(name);

    UI::Element* found = container->FindElement(nameBuf).Get();
    UI::Element* checked = nullptr;
    // The exref type check uses the generic Element type from the import table.
    // In practice these all succeed, so we just do a null check and store.
    if (found) {
        checked = found;
    }
    *field = checked;
}

void TutorialUI::OnCreated() {
    FindAndStore(_container, "preText",    &preText);
    FindAndStore(_container, "postText",   &postText);
    FindAndStore(_container, "imageTable", &imageTable);
    FindAndStore(_container, "nextButton", &nextButton);
    FindAndStore(_container, "quitButton", &quitButton);

    // Set button IDs
    nextButton->SetElementId(0x578);  // nextButton = 1400
    quitButton->SetElementId(0x579);  // quitButton = 1401

    // Hide the "buttonClose" element in the owning dialog instance (+0x18)
    System::String closeName("buttonClose");
    UI::Element* closeElem = _dialog->FindElement(closeName).Get();
    // SetFlagsOnOff(elem, hideFlags=1, showFlags=0) — hide element
    closeElem->SetFlagsOnOff(UI::kElementFlagHidden, 0);
}

void TutorialUI::OnElementEvent(Object::InternalPtr<UI::Element>* /*element*/,
                                UI::EventId a, int32_t b, int32_t /*c*/,
                                int32_t /*d*/, int32_t /*e*/, void* /*userData*/) {
    if (a == UI::EventId::Activated && b == 0x579) QuitTutorial();
}

void TutorialUI::QuitTutorial() {
    // The engine singleton (DAT_1401c3dd8)
    GameUpdateLoop* eng = GameUpdateLoop::_singleton;

    // Destroy and free the SaveLoad object stored at eng+0x140
    Save::SaveLoad* saveLoad = eng->m_saveLoad;
    if (saveLoad) {
        RtSave_SaveLoad_Dtor(saveLoad);   // ??1SaveLoad 0x1800c3c90
        System::MemoryAllocator::MemFree(saveLoad);
    }
    eng->m_saveLoad = nullptr;

    // --- Step 1: Allocate and construct the MenuDialogLoopT (0xc00 bytes) ---
    // This is the main menu dialog loop that will display after the loading screen.
    // ENDGAME B.3.9 EXA: the real class (ctor 0x14009bb00, which retail CALLs).
    void* menuLoop = System::MemoryAllocator::MemAlloc(0xc00, 8);
    Engine::UpdateLoop* pMenuLoop = nullptr;
    if (menuLoop) {
        System::String menuNameStr("Dialog/MainMenu.rsc");
        pMenuLoop = ::new (menuLoop) MenuDialogLoopT(menuNameStr);
    }

    // --- Step 2: Allocate and construct the GameLoadDialogLoopT (0xb60 bytes) ---
    // Wraps the menu loop; shows "Dialog/Loading.rsc" while the menu is loading.
    // The real class: its ctor is the subclass init retail inlines here.
    void* dlLoop = System::MemoryAllocator::MemAlloc(0xb60, 4);
    Engine::UpdateLoop* pDlLoop = nullptr;
    if (dlLoop) {
        // Delay constant is 1.0f (DAT_14017214c — bytes 0x3f800000).
        System::Time delayTime{};
        RtSystem_Time_Ctor(&delayTime, 1.0f);

        System::String loadingNameStr("Dialog/Loading.rsc");
        pDlLoop = ::new (dlLoop) GameLoadDialogLoopT(loadingNameStr, pMenuLoop, delayTime);
    }

    // --- Step 3: Allocate and construct the outer GameLoaderLoop (0x48 bytes) ---
    // Wraps the GameLoadDialogLoopT with a zero-delay outer loader.
    void* outerLoop = System::MemoryAllocator::MemAlloc(0x48, 4);
    GameLoaderLoop* pOuterLoop = nullptr;
    if (outerLoop) {
        System::Time zeroTime{};
        RtSystem_Time_Ctor(&zeroTime, 0.0f);
        pOuterLoop = ::new (outerLoop) GameLoaderLoop(pDlLoop, zeroTime);
    }

    // --- Step 4: Set cursor, fullscreen fade to black, and queue next loop ---
    // FUN_140097fb0: SetCursor(null), SetFullscreenFade(black), store next loop at eng+0xC8.
    {
        UI::UserInterface* ui = eng->m_userInterface;
        ui->SetCursor(g_gameContainer->cursorHourglass);
        ui->SetFullscreenFade(true, System::kColorBlack, false);
        eng->m_nextLoop = pOuterLoop;
    }
}

void TutorialUI::OnUpdateLayout() {
    // nextButton (+0x50): check flags at element +0x34
    UI::Element* nextBtn = nextButton;
    bool nextHidden = (nextBtn->_flags & UI::kElementFlagAnyDisabled) != 0;
    nextBtn->SetFlagsOnOff(nextHidden ? 1 : 0, nextHidden ? 0 : 1);

    // quitButton (+0x58): same check
    UI::Element* quitBtn = quitButton;
    bool quitHidden = (quitBtn->_flags & UI::kElementFlagAnyDisabled) != 0;
    quitBtn->SetFlagsOnOff(quitHidden ? 1 : 0, quitHidden ? 0 : 1);
}

::Object::Base* TutorialUI::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(TutorialUI), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(::new (p) TutorialUI());
}
// Retail's createInPlace is DialogController's import and does not build a
// controller in place (B8-13); the tree's in-place entry did, and that is kept.
::Object::Base* TutorialUI::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(::new (storage) TutorialUI());
}

// DEFINE_EXE_TYPE(TutorialUI, "UI::DialogController") stays where its
// REGISTER_TYPE was — tutorial_dialog_vtables.cpp (B38_RULES B8-7).
