// src/game/Tutorial/tutorial_next_scenario.cpp
// Port of exe 0x1400E0D70 -- FinishTutorialInstance_helper_0d70, the
// "tutorial finished -> boot the next scenario" transition.
//
// This is what the tutorial panel's final button does. Until now
// TutWave_StepExecute ended the last step with a DarkOnce stub, so after
// "step 45/45 complete" the button did nothing at all -- the observable being
// a stream of `[TutorialUI] Slot9 event type=0x12 id=1400` with no response.
//
// The stub's comment claimed four unported exe functions made it out of scope.
// Three of them are one-liners and the fourth is a flat field-init:
//   0x140013A60  Scenario::GetDisplayName -> String(scenario + 0x58)
//   0x140013AA0  Scenario::GetSaveName    -> String(scenario + 0x88)
//   0x140099130  GetLoadingSoundString    -> already inline in engine_funcs.h
//   0x140098060  GameUpdateLoop ctor      -> the real class (Game/GameUpdateLoop.h)
// (See reference_stub_comment_outlived_the_port_that_replaced_it: grep the
// retail ADDRESS, not the stub's prose.)
//
// Retail body, verified against the decompile of 0x1400E0D70:
//   scenIdx = *(int*)(tutorialDef + 0x38); if (scenIdx < 0) return;
//   scenario = list[scenIdx] filtered by IsDerivedFrom(Scenario)
//   saveName = String(scenario + 0x88)                      // 0x140013AA0
//   if saveName is empty:
//       GameUpdateLoop(0x160) ctor'd with the scenario's worldgen params
//   else:
//       SaveLoad::SetOperation(kOpLoadFromFile,0); SetSaveName(saveName, 2);
//       swap Persistence+0x90 to the DATA directory, pump SaveLoad::Update
//       until GetOperation()==kOpNone, restore Persistence+0x90;
//       GameUpdateLoop ctor'd from SaveLoad::GetSaveState()
//   then GameLoadDialogLoopT(0xb60) -> LoaderLoop(0x48) -> publish on the
//   GAME WORLD singleton (cursor + fade on its UI at +0x98, next loop at +0xC8).
//
// The tutorials ship a .sav inside WinData/*.pkg, so the SAVED branch is the one
// that runs here -- the same path ScenarioDialog::LoadSelected takes, including
// the Persistence directory swap that makes SaveLoad read out of the data dir.
//
// NOTE on the publish target: retail 0x1400E0D70 always uses
// g_pGameWorldSingleton, with no controller and no +0xC0/+0xB8 test. That is why
// this cannot simply call WorldSetup_LoadFromSave (0x1400b49f0), whose publish
// step branches on a controller's +0xC0. SetEngineNextLoop() is byte-for-byte
// 0D70's tail.

#include "Game/exe_bodies.h"
#include "Game/game.h"   // B3R STORAGE: g_gameContainer
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "system/Filename.h"
#include "system/String.h"
#include "Tutorial/Tutorial.h"
#include "Tutorial/TutorialInstance.h"
#include "ui/UserInterface.h"
#include "Game/GameUpdateLoop.h"       // ENDGAME B.3.9 EXA: the real loop classes
#include "Game/GameLoadDialogLoopT.h"
#include "Game/GameLoaderLoop.h"
#include "Game/Scenario.h"
#include "io/Persistence.h"   // ENDGAME B.3.4 caller batch P
#include "io/FileSystem.h"    // ENDGAME B.3.4 caller batch P
#include "save/SaveLoad.h"    // ENDGAME B.3.7 callers (K2)
#include <new>
#include <cstdint>
#include <cstdio>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "object/TypeInfo.h"
#include "system/MemoryAllocator.h"
#include "system/Value.h"      // RtSystem_Time_Ctor

namespace {

// Steps 2 and 3 of the chain, shared by both branches:
//   GameLoadDialogLoopT (0xb60) wrapping the GameUpdateLoop, then the outer
//   GameLoaderLoop (0x48). Identical to retail's tail and to the builders in
//   worldsetup_reload.cpp / newgamedialog.cpp / saveloaddialog.cpp.
// ENDGAME B.3.9 EXA: the real loop classes (retail inlines the splash's
// subclass ctor here; the class's ctor is that body).
GameLoaderLoop* BuildLoaderChain(Engine::UpdateLoop* pGameLoop) {
    Engine::UpdateLoop* pDlLoop = nullptr;
    void* dlLoop = System::MemoryAllocator::MemAlloc(0xb60, 4);
    if (dlLoop) {
        System::Time delayTime{};
        RtSystem_Time_Ctor(&delayTime, 1.0f);

        System::String loadingName;
        GetLoadingSoundString(&loadingName);   // 0x140099130

        pDlLoop = ::new (dlLoop) GameLoadDialogLoopT(loadingName, pGameLoop, delayTime);
    }

    GameLoaderLoop* pOuterLoop = nullptr;
    void* outerLoop = System::MemoryAllocator::MemAlloc(0x48, 4);
    if (outerLoop) {
        System::Time zeroTime{};
        RtSystem_Time_Ctor(&zeroTime, 0.0f);
        pOuterLoop = ::new (outerLoop) GameLoaderLoop(pDlLoop, zeroTime);
    }
    return pOuterLoop;
}

// One-line report of anything unresolved, instead of an EXEC-of-0.
const char* MissingDependency(bool needSaveLoad) {
    if (g_gameContainer == nullptr) return "KeyMap/scenario-list singleton";
    if (GameUpdateLoop::_singleton == nullptr) return "GameWorld singleton";
    if (!needSaveLoad) return nullptr;
    if (!::Save::SaveLoad::_singleton) return "SaveLoad singleton";
    if (!::IO::Persistence::_singleton) return "Persistence singleton";
    if (!::IO::FileSystem::_singleton) return "FileSystem singleton";
    return nullptr;
}

// GAME-b: Scenario's two Strings are System::String members
const System::String& ScenarioString(const System::String& field) {
    return field;
}

}  // namespace

// ---------------------------------------------------------------------------
// TutorialInstance::LoadNextScenario (exe 0x1400E0D70)
// ---------------------------------------------------------------------------
// tutorialDef (+0x30) is the Tutorial definition and its currentStep (+0x38)
// is the index of the scenario to load next.
void TutorialInstance::LoadNextScenario() {
    Tutorial* tutorial = tutorialDef;
    if (!tutorial) {
        fprintf(stderr, "[TutNext] tutInst+0x30 (Tutorial def) is NULL — no next scenario\n");
        fflush(stderr);
        return;
    }

    // Retail's only bounds test. A negative index means "no scenario follows
    // this tutorial", which is how the last tutorial in the chain ends.
    int scenIdx = tutorial->currentStep;
    if (scenIdx < 0) {
        fprintf(stderr, "[TutNext] scenarioIndex=%d (<0) — this tutorial ends the chain\n", scenIdx);
        fflush(stderr);
        return;
    }

    if (const char* missing = MissingDependency(/*needSaveLoad=*/false)) {
        fprintf(stderr, "[TutNext] ABORTED — %s is NULL\n", missing);
        fflush(stderr);
        return;
    }

    // The scenario list is the SAME one ScenarioDialog enumerates:
    // GameContainer +0x08, its resources at +0x20.
    Object::ExternalList* scenarioList = g_gameContainer->scenarioList.Get();
    // NOT RETAIL (review F-113): retail reads _resources[scenIdx] with no upper bound
    // (0x1400e0dbb: MOVSXD RDX,[RDX+0x38]; MOV RCX,[RAX+0x20]; MOV RBX,[RCX+RDX*8]);
    // an index at or past the list's count, from a tutorial resource, ends the chain here.
    if (!ScenarioIndexValid(scenIdx, scenarioList->_count)) {
        fprintf(stderr, "[TutNext] scenarioIndex=%d is past the %d-entry scenario list\n",
                scenIdx, scenarioList->_count);
        fflush(stderr);
        return;
    }
    Object::External* resource = scenarioList->_resources[scenIdx];

    Scenario* scenario = Object::IsDerivedFromNullSafe(resource, Scenario::TypeId())
        ? static_cast<Scenario*>(resource) : nullptr;

    // 0x140013AA0 -- Scenario::GetSaveName, String(scenario + 0x88). Retail runs
    // this on a possibly-null `scenario` (the IsDerivedFrom filter can yield 0);
    // String(0x88) off null would fault, so a null scenario is an upstream bug we
    // want to see named rather than silently skipped.
    if (!scenario) {
        fprintf(stderr, "[TutNext] scenario[%d]=%p is not a Scenario (typeId filter %u) "
                        "— cannot start the next tutorial\n",
                scenIdx, static_cast<void*>(resource), Scenario::TypeId());
        fflush(stderr);
        return;
    }

    System::String saveNameStr("");
    // Scenario::GetSaveName (+0x88)
    saveNameStr = ScenarioString(scenario->_field88);

    bool hasSaveState = (saveNameStr._data != nullptr && saveNameStr.GetLength() > 0);

    fprintf(stderr, "[TutNext] next scenario index=%d resource=%p hasSaveState=%d\n",
            scenIdx, static_cast<void*>(scenario), (int)hasSaveState);
    fflush(stderr);

    // The null InternalBasePtr<AchievementInstance> the ctor's p3 receives.
    const Object::InternalPtr<Achievement::AchievementInstance> ibpCopy;

    Engine::UpdateLoop* pGameLoop = nullptr;

    if (!hasSaveState) {
        // ---- NEW-SCENARIO branch -------------------------------------------
        // 0x140013A60 -- Scenario::GetDisplayName, String(scenario + 0x58).
        System::String displayNameStr("");
        displayNameStr = ScenarioString(scenario->scenarioName);

        // Worldgen params. The outer two of the generator triple are stored in
        // the order mapGen(+0x70) / terrainGen(+0x78) / climateGen(+0x80) --
        // positionally p9/p10/p11 -> loop +0x40/+0x48/+0x50, which
        // GameUpdateLoop::Load reads back in that order. Same mapping
        // ScenarioDialog::LoadSelected documents.
        uint32_t seedVal        = static_cast<uint32_t>(scenario->_field60);
        uint32_t terrainSizeVal = static_cast<uint32_t>(scenario->_field64);
        bool     disasterOff    = scenario->_field68;

        void* gameLoop = System::MemoryAllocator::MemAlloc(0x160, 8);
        if (gameLoop) {
            // ENDGAME B.3.9 EXA: the real class; ctor 0x140098060 copy-constructs
            // the name (retail's by-value String arrives by hidden pointer), and
            // our local releases its own reference as retail's ~String does.
            pGameLoop = ::new (gameLoop) GameUpdateLoop(
                nullptr, ibpCopy, scenIdx, displayNameStr,
                static_cast<int32_t>(seedVal), static_cast<int32_t>(terrainSizeVal),
                /*p8=*/disasterOff, scenario->mapGenerator,
                scenario->terrainGenerator,
                scenario->climateGenerator);
        }
    } else {
        // ---- SAVED-SCENARIO branch -----------------------------------------
        if (const char* missing = MissingDependency(/*needSaveLoad=*/true)) {
            fprintf(stderr, "[TutNext] SAVED branch ABORTED — %s is NULL\n", missing);
            fflush(stderr);
            return;
        }

        Save::SaveLoad* saveLoad = ::Save::SaveLoad::_singleton;
        saveLoad->SetOperation(Save::SaveLoad::kOpLoadFromFile, 0);
        saveLoad->SetSaveName(saveNameStr, 2);

        // Tutorial saves live inside WinData/*.pkg, so Persistence+0x90 must
        // point at the DATA directory for the load, then be restored. Identical
        // to ScenarioDialog::LoadSelected -- including that GetSavePath returns a
        // System::String (destroyed with String::~String at the very end) while
        // the GetDataDirectory temp is a Filename.
        IO::Persistence* persistence = ::IO::Persistence::_singleton;
        System::String savePathBuf(persistence->GetSavePath());
        IO::FileSystem* fileSystem = ::IO::FileSystem::_singleton;
        System::Filename dataDirBuf(fileSystem->GetDataDirectory());
        ::IO::Persistence::_singleton->_savePath = dataDirBuf.AsString();

        Save::SaveLoad::Operation operation = saveLoad->GetOperation();
        int loopCount = 0;
        while (operation != Save::SaveLoad::kOpNone) {
            System::Time zeroTime{};
            RtSystem_Time_Ctor(&zeroTime, 0.0f);
            saveLoad->Update(zeroTime);
            operation = saveLoad->GetOperation();
            ++loopCount;
        }
        fprintf(stderr, "[TutNext] SaveLoad pump finished after %d iterations\n", loopCount);
        fflush(stderr);

        ::IO::Persistence::_singleton->_savePath = savePathBuf;

        Save::SavedState* savedState = saveLoad->GetSaveState();

        void* gameLoop = System::MemoryAllocator::MemAlloc(0x160, 8);
        if (gameLoop) {
            // Retail passes an EMPTY String here (local_res8 = 0). A null COW
            // pointer IS the empty string -- no allocation, nothing to destruct.
            const System::String emptyName;
            pGameLoop = ::new (gameLoop) GameUpdateLoop(
                savedState, ibpCopy, scenIdx, emptyName,
                0, 0, /*p8=*/false, nullptr, nullptr, nullptr);
        }

    }

    GameLoaderLoop* pOuterLoop = BuildLoaderChain(pGameLoop);

    // Publish on the GAME WORLD singleton (0x140097fb0, inlined as the tree's
    // SetEngineNextLoop): cursor + fullscreen fade on its UI (+0x98), next loop
    // at +0xC8. Retail has no controller test here.
    {
        GameUpdateLoop* engine = GameUpdateLoop::_singleton;
        UI::UserInterface* ui = engine->m_userInterface;
        ui->SetCursor(g_gameContainer->cursorHourglass);
        ui->SetFullscreenFade(true, System::kColorBlack, false);
        engine->m_nextLoop = pOuterLoop;
    }

    fprintf(stderr, "[TutNext] next-scenario loop chain queued (gameLoop=%p loader=%p)\n",
            static_cast<void*>(pGameLoop), static_cast<void*>(pOuterLoop));
    fflush(stderr);
}
