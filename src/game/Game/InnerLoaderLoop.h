#pragma once
// ============================================================================
// src/game/Game/InnerLoaderLoop.h — ENDGAME B.3.8 block LOOP (B38_RULES D8)
//
// The exe's first `Engine::LoaderLoop` subclass: GameMain's INNER loader loop,
// the one that wraps the splash chain and loads "Game/GlobalResources.rsc".
// Unreflected (no type name); retail builds exactly one, in GameMain.
//
// RETAIL (Application-x64.exe, B38_SLOTS.md sec. 6, B37_RULES sec. 3.1):
//   table 0x140158870, 12 slots, sizeof 0x48 (= Engine::LoaderLoop, adds nothing)
//   [0]  0x140001150  own scalar deleting dtor (~LoaderLoop + MemFree on &1)
//   [1]  IMPORT ?Startup@LoaderLoop@Engine@@          [2]  IMPORT ?Shutdown@LoaderLoop@
//   [3]  IMPORT ?Update@LoaderLoop@                   [4]  IMPORT ?Draw@LoaderLoop@
//   [5]  IMPORT ?DrawFailed@UpdateLoop@               [6]  0x140001120 own Load:
//        PlacementTool_Ctor(KeyMap, _resourceList); _loadBit = -1
//   [7]  IMPORT ?SaveRestore@UpdateLoop@              [8]  IMPORT ?OnVideoError@LoaderLoop@
//   [9]  IMPORT ?OnVideoRestart@UpdateLoop@           [10] 0x140001110 own: "Game/Global"
//   [11] IMPORT ?AllowsModChanges@UpdateLoop@
//   stamped by GameMain 0x140041ef0 (at 0x140042222), after the imported
//   `??0LoaderLoop@Engine@@` — i.e. `new InnerLoaderLoop(next, Time(0.0f))`.
//
// Every other slot is INHERITED from Engine::LoaderLoop / Engine::UpdateLoop,
// as retail's table holds the base imports.
// ============================================================================

#include <cstdint>
#include "engine/UpdateLoop.h"   // -Isrc/engine: Engine::LoaderLoop

class InnerLoaderLoop final : public Engine::LoaderLoop {
public:
    // `??0LoaderLoop@Engine@@` then the 0x140158870 stamp (the compiler's).
    InnerLoaderLoop(Engine::UpdateLoop* nextLoop, const System::Time& delay)
        : Engine::LoaderLoop(nextLoop, delay) {}

    ~InnerLoaderLoop() override;                                 // [0] 0x140001150
    // [1] retail: LoaderLoop::Startup (Loader::Load(_nextLoop)). Overridden
    // for the standalone's skip-loader option only — STANDALONE DIVERGENCE,
    // see loader_loop_stubs.cpp.
    void Startup() override;
    void Load() override;                                        // [6] 0x140001120
    const char* GetResourceFilename() override;                  // [10] 0x140001110 "Game/Global"
};

static_assert(sizeof(InnerLoaderLoop) == 0x48, "InnerLoaderLoop adds nothing to Engine::LoaderLoop (GameMain MemAlloc(0x48))");
static_assert(alignof(InnerLoaderLoop) == 8, "qword-aligned (the vptr)");
