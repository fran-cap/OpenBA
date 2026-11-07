// src/game/Game/worldsetup_reload.cpp
// Port of exe 0x1400b49f0 — the "reload the world from a SavedState" loop chain.
//
// WHY IT LIVES HERE (and not in a Dialog/ file): 0x1400b49f0 is ONE retail
// function with TWO call sites in two different dialogs, and engine_funcs.h gives
// it two different typedef names for the same address —
//
//   engine_funcs.h  fn_Game_ReloadWithSave  (self, savedState, param, nullRef)
//                   called unguarded at moddialog.cpp (Apply → in-game reload)
//   engine_funcs.h  fn_LoadScenarioFromSave (worldSetup, savedState,
//                                            scenarioIdx, retvalPtr)
//                   called unguarded at scenariodialog.cpp
//
// Both resolve `ResolveAddr(Module::Exe, 0x1400b49f0)` and both were NULL →
// EXEC-of-0. The disassembly settles that they are the same function with the same
// ABI: retail does `puVar8 = (ulonglong)param_3 & 0xffffffff` then uses
// `(int)param_3`, i.e. arg3 is an INT widened to a pointer — ModDialog passes
// (void*)-1 ("no scenario"), ScenarioDialog passes the scenario resource index. So
// this must be registered EXACTLY ONCE, and neither dialog owns it; it is a
// WorldSetup/loop-chain function. Registering it here fixes BOTH call sites with no
// edit to scenariodialog.cpp.
//
// Retail body (0x1400b49f0), verified against the decompile:
//   1. MemAlloc(0x160) -> GameUpdateLoop, constructed by FUN_140098060 with
//        (savedState, &(*retvalPtr), (int)scenarioIdx, emptyString, 0,0,'\0',0,0,0)
//   2. MemAlloc(0xb60) -> GameLoadDialogLoopT: Time(1.0f), FUN_140099130 name
//        ("Dialog/Loading.rsc:gameLoad%i", Rand(12)), DialogLoaderLoopT base ctor,
//        vtable -> 0x140158800, then the subclass field init.
//   3. MemAlloc(0x48)  -> outer LoaderLoop, Time(0.0f), vtable -> 0x1401588d0.
//   4. Publish: `if (self+0xC0 == 0)` use the GAME WORLD at self+0xB8 (SetCursor +
//        fade on its UI at +0x98, next loop at +0xC8); else use the dialog loader
//        at self+0xC0 (fade on +0x40, next loop at +0xb68).
//      ⚠ Note the test is on +0xC0 FIRST here, the INVERSE of 0x1400b4790
//      (MainMenuDialog_ReturnToMenu), which tests +0xB8 first. Kept faithful.
//
// FUN_140098060 arg->field map (decompiled, 0x140098060):
//   p2 savedState -> +0x58 | p3 &IBP -> +0x150 (= *p3) | p4 int -> +0x15C
//   p5 String     -> +0x28 (copy-ctor)  | p6 -> +0x30 | p7 -> +0x34
//   p8 char       -> +0xD0 = (p8 ? 0 : 0xF0)   <- p8 is '\0' here, so +0xD0 = 0xF0
//   p9 -> +0x40 | p10 -> +0x48 | p11 -> +0x50 | everything else zeroed
//   (saveloaddialog.cpp's inline copy of this ctor passes p8=1 and therefore writes
//    +0xD0 = 0; that is a DIFFERENT call site, not a discrepancy with this one.)

#include "system/Value.h"
#include "system/MemoryAllocator.h"   // System::MemoryAllocator::MemAlloc / MemFree
#include "system/String.h"
#include "Game/GameLoaderLoop.h"   // ENDGAME B.3.8 LOOP
#include "Game/GameLoadDialogLoopT.h"
#include "Game/GameUpdateLoop.h"
#include <cstdint>
#include <cstdio>
#include <new>
#include "Game/exe_bodies.h"
#include "Game/MenuDialogLoopT.h"   // SetDialogLoaderNextLoop
#include "Dialog/MainMenuDialog.h"    // the receiver's game / menuLoop
#include "Game/game.h"             // g_gameContainer (the hourglass cursor)
#include "ui/UserInterface.h"

// 0x140097fb0: hourglass cursor (GameContainer +0x38, "Cursor/Hourglass.rsc:cursor"),
// fade to opaque black (fadeIn=true, 0xFF000000, reverse=false), queue the loop.
void GameUpdateLoop::SetEngineNextLoop(Engine::UpdateLoop* loaderLoop) {
    UI::UserInterface* ui = m_userInterface;   // +0x98
    ui->SetCursor(::Object::ExternalPtr<UI::Cursor>(g_gameContainer->cursorHourglass.Get()));
    ui->SetFullscreenFade(true, System::kColorBlack, false);
    m_nextLoop = loaderLoop;                    // +0xC8
}

// 0x14009a8f0: fade the menu loop's UI (+0x40) to opaque black, queue the loop.
void MenuDialogLoopT::SetLoaderNextLoop(Engine::UpdateLoop* loaderLoop) {
    GetUserInterface().SetFullscreenFade(true, System::kColorBlack, false);
    _pendingLoop = loaderLoop;              // +0xb68
}

void MainMenuDialog::LoadFromSave(Save::SavedState* savedState, int32_t scenarioIdx,
                                  const Object::InternalBasePtr* retvalIbpPtr) {
    // Caller batch CB6: the three loops are built by their real ctors (below),
    // which stamp the compiler tables and chain the Runtime base ctors
    // directly, so the by-address table globals and the base-ctor doors this
    // readiness test named are no longer read here.
    if (!System::MemoryAllocator::_singleton) {
        printf("[WorldSetup::LoadFromSave] loop-chain allocator unresolved - aborting "
                    "(alloc=%p)\n",
                    (void*)System::MemoryAllocator::_singleton);
        return;
    }

    // The IBP the caller handed us is copied to a local BEFORE the ctor runs, and
    // the ctor stores that copy at gameLoop+0x150 (retail: local_40 = *param_4).
    Object::InternalPtr<Achievement::AchievementInstance> ibpCopy;   // null unless the caller hands one
    Object::InternalBasePtr& ibpCopyBase = ibpCopy;
    if (retvalIbpPtr) ibpCopyBase = *retvalIbpPtr;

    // The scenario index: retail keeps R8's low dword.
    const int32_t scenario = scenarioIdx;

    // --- Step 1: inner GameUpdateLoop (0x160) --------------------------------
    GameUpdateLoop* pGameLoop = nullptr;
    void* gameLoop = System::MemoryAllocator::MemAlloc(0x160, 8);
    if (gameLoop) {
        // ENDGAME B.3.8 LOOP: the real class; its ctor is 0x140098060's body.
        // +0x28 is a copy of retail's `local_res8 = 0` empty System::String (a
        // null COW pointer IS the empty string: the copy is a plain 0). p8 ==
        // '\0' at this call site, so retail writes 0xF0 at +0xD0 (NOT 0).
        const System::String emptyName;
        pGameLoop = ::new (gameLoop) GameUpdateLoop(
            savedState, ibpCopy,
            scenario,
            emptyName, 0, 0, /*disasters (p8)=*/false, nullptr, nullptr, nullptr);
    }

    // --- Step 2: GameLoadDialogLoopT (0xb60) ---------------------------------
    GameLoadDialogLoopT* pDlLoop = nullptr;
    void* dlLoop = System::MemoryAllocator::MemAlloc(0xb60, 4);
    if (dlLoop) {
        System::Time delayTime;
        RtSystem_Time_Ctor(&delayTime, 1.0f);

        System::String loadingName;
        GetLoadingSoundString(&loadingName);   // FUN_140099130, inline in engine_funcs.h

        // ENDGAME B.3.8 LOOP: the real class; its ctor is the subclass field
        // init retail inlines here (colours, +0x190, Random, +0xB58, +0x40).
        pDlLoop = ::new (dlLoop) GameLoadDialogLoopT(
            loadingName, pGameLoop,
            delayTime);
    }

    // --- Step 3: outer LoaderLoop (0x48) -------------------------------------
    GameLoaderLoop* pOuterLoop = nullptr;
    void* outerLoop = System::MemoryAllocator::MemAlloc(0x48, 4);
    if (outerLoop) {
        System::Time zeroTime;
        RtSystem_Time_Ctor(&zeroTime, 0.0f);
        // ENDGAME B.3.8 LOOP: the real class (its ctor = the LoaderLoop door's
        // body + the compiler's 0x1401588d0 stamp).
        pOuterLoop = ::new (outerLoop) GameLoaderLoop(
            pDlLoop,
            zeroTime);
    }

    // --- Step 4: publish the next loop ---------------------------------------
    // Retail tests +0xC0 first (inverse of 0x1400b4790 - see the header comment).
    MenuDialogLoopT* dialogLoader = menuLoop;   // +0xC0
    if (dialogLoader == nullptr) {
        GameUpdateLoop* gameWorld = game;   // +0xB8
        gameWorld->SetEngineNextLoop(pOuterLoop);   // UI at +0x98, next loop at +0xC8
    } else {
        dialogLoader->SetLoaderNextLoop(pOuterLoop);
    }
    printf("[WorldSetup::LoadFromSave] chain built: game=%p dlt=%p loader=%p "
                "scenarioIdx=%d via %s\n",
                static_cast<void*>(pGameLoop), static_cast<void*>(pDlLoop), static_cast<void*>(pOuterLoop),
                (int)scenario,
                dialogLoader ? "dialogLoader" : "gameWorld");
}
