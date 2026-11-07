// src/game/Game/chunked_scene_loader.cpp — see chunked_scene_loader.h for design.


#include "chunked_scene_loader.h"
#include "stall_debug.h"
#include "Game/menudialogloopt_funcs.h"
#include "Game/MenuDialogLoopT.h"
#include <cstdio>
#include <cstdint>
#include "load_time.h"   // [LoadTime] stamps (NOT RETAIL, print-only; block LOADSTAMP)

namespace ChunkedSceneLoader {

namespace {

MenuDialogLoopT* g_menuDltSelf = nullptr;
MenuDialogLoopT* g_armedSelf = nullptr;
MenuDialogLoopT_PhaseBState g_state{};

// The loader's PhaseB state (NOT RETAIL; port-only chunking of MenuDLT::Load PhaseA).
// "Armed" (Arm() called, Init not yet run) is not a value here: it is
// g_armedSelf != nullptr, and RunArmedInit clears it before Init can make the
// loader Busy. bool-backed so the storage and code stay those of the old flag.
enum class LoaderState : bool {
    Idle = false,   // never started, or the last load finished / bailed out
    Busy = true,    // Init found PhaseB work; Tick has not finished it yet
};

LoaderState g_loaderState = LoaderState::Idle;
int    g_tickCount = 0;
double g_initStartMs = 0.0;
double g_chunkSumMs = 0.0;
double g_chunkMaxMs = 0.0;

}  // namespace

void Init(MenuDialogLoopT* menuDltSelf) {
    if (!menuDltSelf) {
        printf("[ChunkedSceneLoader] Init: null menuDltSelf — skipping\n");
        std::fflush(stdout);
        return;
    }
    if (g_loaderState != LoaderState::Idle && g_menuDltSelf == menuDltSelf) {
        // Idempotent — same loop already initialized.
        return;
    }
    if (g_loaderState != LoaderState::Idle) {
        printf("[ChunkedSceneLoader] Init: WARN: prior load still busy "
                    "(self=%p, new=%p) — overwriting\n",
                    (void*)g_menuDltSelf, (void*)menuDltSelf);
    }

    g_menuDltSelf  = menuDltSelf;
    g_loaderState  = LoaderState::Idle;
    g_tickCount    = 0;
    g_chunkSumMs   = 0.0;
    g_chunkMaxMs   = 0.0;
    g_initStartMs  = StallDebug::NowMs();

    printf("[ChunkedSceneLoader] Init: self=%p — running LoadSceneA1 (small "
                "allocs + MenuScene.rsc load)\n", (void*)menuDltSelf);
    std::fflush(stdout);

    LoadTime::Stamp("menu.scene.a1");   // NOT RETAIL: print-only (MenuScene.rsc load)
    bool a1Ok;
    {
        StallDebug::Phase a1Phase("ChunkedSceneLoader::Init LoadSceneA1");
        a1Ok = menuDltSelf->LoadSceneA1();
    }
    if (!a1Ok) {
        printf("[ChunkedSceneLoader] Init: LoadSceneA1 returned false — no "
                    "scene background; skipping PhaseB\n");
        std::fflush(stdout);
        g_loaderState = LoaderState::Idle;
        return;
    }

    // Re-read menuScene/scene from MenuDLT struct fields (set by LoadSceneA1).
    MenuScene* menuScene = menuDltSelf->_menuScene;   // +0xb90
    Graphics::Scene* scene = menuDltSelf->_scene;     // +0xba0

    g_state.InitState(menuDltSelf, menuScene, scene);
    if (g_state.totalCount <= 0) {
        printf("[ChunkedSceneLoader] Init: PhaseB has no work "
                    "(totalCount=%d) — running A3 immediately\n",
                    g_state.totalCount);
        std::fflush(stdout);
        menuDltSelf->LoadSceneA3();
        g_loaderState = LoaderState::Idle;
        return;
    }

    LoadTime::Stamp("menu.scene.b");   // NOT RETAIL: print-only (chunked PhaseB starts)
    g_loaderState = LoaderState::Busy;
    printf("[ChunkedSceneLoader] Init: ready — PhaseB has %d elements to "
                "process (chunked across Update ticks)\n",
                g_state.totalCount);
    std::fflush(stdout);
}

void Arm(MenuDialogLoopT* menuDltSelf) {
    g_armedSelf = menuDltSelf;
    printf("[ChunkedSceneLoader] Arm: self=%p — LoadSceneA1 deferred until the "
                "splash is on screen\n", (void*)menuDltSelf);
    std::fflush(stdout);
}

bool IsArmed() {
    return g_armedSelf != nullptr;
}

void RunArmedInit() {
    if (!g_armedSelf) return;
    MenuDialogLoopT* self = g_armedSelf;
    g_armedSelf = nullptr;   // clear FIRST so a failing Init can't re-enter
    Init(self);
}

int Tick(int maxElements) {
    if (g_loaderState == LoaderState::Idle) return 0;

    double startMs = StallDebug::NowMs();
    int processed = g_state.TickChunk(maxElements);
    double endMs = StallDebug::NowMs();
    double durMs = endMs - startMs;
    g_chunkSumMs += durMs;
    if (durMs > g_chunkMaxMs) g_chunkMaxMs = durMs;
    ++g_tickCount;

    bool done = (g_state.index >= g_state.totalCount);
    // Log first + last tick always; every 5th tick in between.
    if (g_tickCount == 1 || done || (g_tickCount % 5) == 0) {
        printf("[ChunkedSceneLoader] Tick #%d: processed=%d cumProgress=%d/%d "
                    "(%.1f%%) chunkDur=%.2fms\n",
                    g_tickCount, processed, g_state.index, g_state.totalCount,
                    g_state.totalCount > 0
                        ? 100.0 * (double)g_state.index / (double)g_state.totalCount
                        : 0.0,
                    durMs);
        std::fflush(stdout);
    }

    if (done) {
        // PhaseC particles — small, runs in one shot (~5ms).
        {
            LoadTime::Stamp("menu.scene.a3");   // NOT RETAIL: print-only
            StallDebug::Phase a3Phase("ChunkedSceneLoader::Tick LoadSceneA3");
            g_menuDltSelf->LoadSceneA3();
        }
        LoadTime::Stamp("menu.scene.done");   // NOT RETAIL: print-only
        double totalMs = endMs - g_initStartMs;
        printf("[ChunkedSceneLoader] DONE: ticks=%d sumChunkMs=%.2f "
                    "maxChunkMs=%.2f initToDoneMs=%.2f (PhaseB %d elements)\n",
                    g_tickCount, g_chunkSumMs, g_chunkMaxMs, totalMs,
                    g_state.totalCount);
        std::fflush(stdout);
        g_loaderState = LoaderState::Idle;
    }

    return processed;
}

bool IsBusy() {
    return g_loaderState != LoaderState::Idle;
}

}  // namespace ChunkedSceneLoader

