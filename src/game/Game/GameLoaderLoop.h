#pragma once
// ============================================================================
// src/game/Game/GameLoaderLoop.h — ENDGAME B.3.8 block LOOP (B38_RULES D8)
//
// The exe's second `Engine::LoaderLoop` subclass: the OUTER loader loop that
// every loop chain is wrapped in (GameMain, NewGame, SaveLoad, MainMenu,
// the tutorial paths). Unreflected. Retail overrides only its deleting dtor;
// the port also overrides Startup (below).
//
// RETAIL (Application-x64.exe, B38_SLOTS.md sec. 6, B37_RULES sec. 3.1):
//   table 0x1401588d0, 12 slots, sizeof 0x48 (= Engine::LoaderLoop)
//   [0]  0x140001190  own scalar deleting dtor (~LoaderLoop + MemFree on &1)
//   [1]-[11] IMPORTS: LoaderLoop's Startup/Shutdown/Update/Draw/Load/OnVideoError,
//        UpdateLoop's DrawFailed/SaveRestore/OnVideoRestart/GetResourceFilename/
//        AllowsModChanges
//   stamped by 7 sites (GameMain, NewGameDialog, SaveLoadDialog_OnCreate,
//   MainMenuDialog, the tutorial helpers) after `??0LoaderLoop@Engine@@`.
//
// Every other slot is INHERITED from Engine::LoaderLoop / Engine::UpdateLoop,
// as retail's table holds the base imports.
// ============================================================================

#include <cstdint>
#include "engine/UpdateLoop.h"   // -Isrc/engine: Engine::LoaderLoop

class GameLoaderLoop final : public Engine::LoaderLoop {
public:
    // `??0LoaderLoop@Engine@@` then the 0x1401588d0 stamp (the compiler's).
    GameLoaderLoop(Engine::UpdateLoop* nextLoop, const System::Time& delay)
        : Engine::LoaderLoop(nextLoop, delay) {}

    ~GameLoaderLoop() override;                                  // [0] 0x140001190
    // [1] retail: LoaderLoop::Startup (Loader::Load(_nextLoop)). Overridden
    // for the standalone's skip-loader option only — STANDALONE DIVERGENCE,
    // see loader_loop_stubs.cpp.
    void Startup() override;
};

static_assert(sizeof(GameLoaderLoop) == 0x48, "GameLoaderLoop adds nothing to Engine::LoaderLoop (MemAlloc(0x48) at every site)");
static_assert(alignof(GameLoaderLoop) == 8, "qword-aligned (the vptr)");
