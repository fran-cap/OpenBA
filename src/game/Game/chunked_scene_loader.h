// src/game/Game/chunked_scene_loader.h
// Standalone-only chunked driver for MenuDLT::Load's heavy PhaseB work.
//
// MenuDialogLoopT_LoadStaticMeshes_PhaseB iterates 12K-15K elements per scene
// (Scene[0..9].rsc) — ~542ms of synchronous work. Running it from MenuDLT::
// Stub_Startup blocks the main thread for that whole time. The PhaseB loop
// can NOT run on a worker thread because Scene::AddObject mutates global
// scene state in a non-thread-safe way. It must therefore stay on the main
// thread -- but it must NOT run while the Engine::Loader thread is loading
// (splash_loop_stubs.cpp gates it on EngineLoader::IsBusy).
//
// Solution: split LoadScene_PhaseA into three pieces (LoadSceneA1 + chunked
// PhaseB + LoadSceneA3) and drive PhaseB incrementally from MenuDLT::
// Stub_Update — each tick processes ~500 elements (~17ms of work).
//
// Usage from menudialogloopt_stubs.cpp:
//   ChunkedSceneLoader::Init(menuDltSelf);   // from Stub_Startup (main thread)
//   while (...) {
//     ChunkedSceneLoader::Tick(500);          // from Stub_Update (per-tick)
//     if (!ChunkedSceneLoader::IsBusy()) break;
//   }
//
// Single-job model — only one chunked load in flight at a time.
//
// All operations log [ChunkedSceneLoader] lines for greppability.

#pragma once


class MenuDialogLoopT;

namespace ChunkedSceneLoader {

// Run LoadSceneA1 (small allocs + MenuScene.rsc load + 3 AddGlobalObject
// calls — measured ~590ms, not the ~10ms originally assumed) and capture the
// chunked PhaseB state. After this returns, IsBusy() is true; Tick() drives
// the rest. Safe to call multiple times — early-exits if already initialized
// for the same loop.
void Init(MenuDialogLoopT* menuDltSelf);

// Record which loop Init should run for, WITHOUT paying its cost yet.
//
// Init used to be called straight from RunGameMain, before MainLoop::Run. That
// put its ~590ms inside the window between ShowWindow (which happens during
// Engine::Startup, in VideoDX11 CreateDevice) and the first splash Present —
// i.e. ~590ms of a visible, never-painted engine window at boot. Retail loads
// the menu scene behind an already-drawing splash, so it has no such gap.
//
// Arm() at boot, then RunArmedInit() from the splash's Update once the splash
// has actually revealed. Cost is unchanged; it just lands behind a drawn frame.
void Arm(MenuDialogLoopT* menuDltSelf);

// True between Arm() and RunArmedInit(). Callers that gate a loop transition
// on IsBusy() must ALSO check this — armed-but-not-started is still work
// outstanding, and IsBusy() is false until Init has actually run.
bool IsArmed();

// If armed, run Init for the armed loop and clear the armed flag. No-op
// otherwise. Main thread only.
void RunArmedInit();

// Process up to maxElements elements of the chunked PhaseB loop. When the
// chunk function reports done, runs LoadSceneA3 (particles, ~5ms) and clears
// the busy flag (back to LoaderState::Idle). Returns the number of elements processed this call.
int Tick(int maxElements);

// True while Init has run but Tick hasn't yet finished the iteration.
bool IsBusy();

} // namespace ChunkedSceneLoader

