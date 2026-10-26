// src/game/Game/loader_loop_stubs.cpp
// Standalone-only stub vtables for the exe's two LoaderLoop subclasses
// (vtables @ 0x140158870 inner and @ 0x1401588d0 outer, used in this nesting
// in the original GameMain).
//
// LoaderLoop has 12 virtuals (UpdateLoop's full set, no CreateDialog).
//
// Inner vtable @ 0x140158870 (PTR_FUN_140158870) — wraps the splash chain:
//   +0x00  Destroy(self, flags)            — exe 0x140001150 (LoaderLoop::~ + free)
//   +0x08  Startup                         — thunk 0x14003A8E2 -> Runtime LoaderLoop::Startup
//   +0x10  Shutdown                        — thunk 0x14003A8DC
//   +0x18  Update(self, time)              — thunk 0x14003A8D6 -> Runtime LoaderLoop::Update
//   +0x20  Draw(self, alpha)               — thunk 0x14003A8D0 -> Runtime LoaderLoop::Draw
//   +0x28  DrawFailed                      — thunk 0x14003A90C
//   +0x30  Load(self)                      — exe 0x140001120 (PlacementTool ctor + reset loadBit)
//   +0x38  SaveRestore                     — thunk 0x14003A906
//   +0x40  OnVideoError                    — thunk 0x14003A8C4
//   +0x48  OnVideoRestart                  — thunk 0x14003A900
//   +0x50  GetResourceFilename             — exe 0x140001110 (returns "Game/Global")
//   +0x58  AllowsModChanges                — thunk 0x14003A8F4
//
// Outer vtable @ 0x1401588d0 (g_GameLoaderLoop_Vtable) — wraps the inner LoaderLoop:
//   +0x00  Destroy(self, flags)            — exe 0x140001190 (identical to inner Destroy)
//   +0x08-0x58                             — all thunks to Runtime LoaderLoop methods
//
// ENDGAME B.3.8 block LOOP: the two hand-built tables are gone. This file now
// holds the MEMBER bodies of the two real classes, `InnerLoaderLoop`
// (src/game/Game/InnerLoaderLoop.h) and `GameLoaderLoop` (GameLoaderLoop.h),
// whose compiler vtables the two retail addresses serve (B38_RULES D5).


#include "stall_debug.h"
#include "options.h"
#include "system/Thread.h"   // ENDGAME B.3.4b System callers: System::Thread
#include "engine/UpdateLoop.h"   // -Iruntime/types: ENDGAME B.3.7 Engine loop classes
#include "engine/MainLoop.h"     // -Iruntime/types: ENDGAME B.3.7 Engine::MainLoop
#include "Game/InnerLoaderLoop.h"   // ENDGAME B.3.8 LOOP: the two LoaderLoop classes
#include "Game/GameLoaderLoop.h"
#include "Game/exe_bodies.h"        // GameContainer_Init (0x140099db0)
#include "Game/game.h"              // GameContainer, g_gameContainer
#include "Game/inputbindings.h"     // Game::RegisterDefaultInputBindings (0x1400992f0)
#include "achievement/AchievementManager.h"
#include "audio/SoundEffect.h"
#include "ui/Cursor.h"
#include "ui/SpriteSheet.h"
#include "ui/StringTable.h"
#include "system/Filename.h"
#include "system/String.h"
#include <cstdio>
#include <cstdint>
#include <cstdlib>

namespace {

// The engine's Engine::Loader::Load(loop) spawns a background Loader thread
// (ThreadFunc -> Loader::Run -> UpdateLoop::LoadResources) that loads the loop's
// "<GetResourceFilename>Resources.rsc" and recursively resolves its ENTIRE
// dependency tree (FindOrLoad -> External::Load -> ExternalList::Serialize ->
// zlib). For the inner loop that's "Game/GlobalResources.rsc", and in the
// standalone's incomplete resource/FileSystem environment that recursive load
// NEVER terminates — the thread pegs a full CPU core forever (the root cause of
// the "menu 100% CPU"; verified non-terminating, ~1.5 cores for 600+ CPU-sec).
//
// BUT we can't simply skip it: boot DEPENDS on the Game/Global resources it
// loads (skipping it crashes boot 6/6). So this stays ENABLED by default; the
// real fix is to make the Game/Global load terminate (the standalone's resource
// path-resolution/caching, tied to the disabled Modification::SetupFileSystem,
// makes FindOrLoad never dedup -> endless re-load). Until then, the env var is
// an opt-OUT purely for investigation (expect a boot crash with it set):
//   BANISHED_SKIP_ENGINE_LOADER=1  -> skip Engine::Loader::Load
bool SkipEngineLoader() {
    return Opt().skipEngineLoader;
}

// No wait follows the kick (issue #13). Retail's LoaderLoop::Startup is the
// three-instruction tail call to Loader::Load and returns at once; the main
// loop then runs LoaderLoop::Update (Sleep 14 + advance) and Draw (a black
// clear) + Present every frame while the loader thread works, and the
// transition waits on the loader's run flag (LoaderLoopAdvance, loops.cpp).
// The standalone used to park the main thread in Thread::WaitForFinish here
// (BANISHED_SYNC_ENGINE_LOADER), for two reasons that are both gone:
//   - heap safety: the raw member Allocate/Free were once reachable without the
//     allocator CS. Today every door into the free list is locked
//     (src/engine/system/allocator.cpp hands out the composed stubs of
//     src/diag/alloc_cutover_arm.cpp for ?Allocate@/?Free@, and MemAlloc/MemFree
//     lock themselves), which is retail's arrangement: its Allocate call sites
//     are an inlined Enter(_singleton+0x640) / Allocate / Leave (operator new
//     0x18000107d/94/9f, String::SetLength 0x1800c827f/9d/a8, MemAlloc
//     0x1800c7a50);
//   - loader exclusivity (docs/history/endgame_b/LOADER_READ_SPIN.md): the
//     second loader thread that raced the Engine::Loader for the one package
//     handle was deleted in 09a74987b. The loader is the only reader again, and
//     nothing the loader loops' frames run reads a package.

// Slot 2 Shutdown is INHERITED (ENDGAME B.3.8 LOOP): Engine::LoaderLoop::Shutdown
// (Runtime 0x1800dc280) is retail's unconditional join of the loader thread
// (WaitForSingleObject on _loader's hFinished) ahead of teardown -- the body the
// old `LoaderLoopShutdownWait` stubs re-implemented.

} // namespace

// ============================================================
// InnerLoaderLoop (vtable @ 0x140158870) — src/game/Game/InnerLoaderLoop.h
// ============================================================
// ENDGAME B.3.8 block LOOP: the real class. Retail overrides [0], [6], [10];
// every other slot is Engine::LoaderLoop's / Engine::UpdateLoop's (inherited).
//
// [1] Startup is overridden anyway — a STANDALONE DIVERGENCE, not a port: its
// body IS retail's LoaderLoop::Startup (Loader::Load(_nextLoop) on the one
// static loader, returning at once) plus the standalone investigation option
// BANISHED_SKIP_ENGINE_LOADER, which skips the kick. The transition itself is
// the inherited LoaderLoop::Update: next loop once the loader is idle and the
// delay passed; until then every frame is LoaderLoop::Draw's black clear.

// Retail 0x140001150 (slot 0): CALL ??1LoaderLoop@Engine@@ (import
// 0x140152ba8), then MemFree on &1. The compiler's chain runs ~LoaderLoop ->
// ~UpdateLoop (the _loadBit hand-back + ExternalFactory::Clean + the list
// cleanup); the free is UpdateLoop's operator delete.
InnerLoaderLoop::~InnerLoaderLoop() {
    printf("[InnerLL::Destroy] self=%p\n", (void*)this);
}

void InnerLoaderLoop::Startup() {
    StallDebug::Phase phase("InnerLL::Startup");
    printf("[InnerLL::Startup] self=%p next=%p loader=%p\n",
           (void*)this, (void*)_nextLoop, (void*)&Engine::LoaderLoop::_loader);
    if (SkipEngineLoader()) {
        printf("[InnerLL::Startup] BANISHED_SKIP_ENGINE_LOADER set — skipping "
               "Engine::Loader::Load (background loader pegs a core, but boot "
               "depends on Game/Global; expect a crash).\n");
    } else {
        // ENDGAME B.3.9 ENG W2: `_loader` is the class static (the minted
        // DLL-data slot and its engine_funcs address globals are gone), so the
        // old "g_pLoader null" branch cannot happen and went with them.
        Engine::LoaderLoop::_loader.Load(_nextLoop);
        printf("[InnerLL::Startup] Loader::Load returned\n");
    }
}

// [6] 0x140001120 (read 2026-10-11, decompile + disassembly): RDX = [RCX+0x8] (this
// loop's resource list, Game/GlobalResources.rsc, which LoadResources has just
// filled on the loader thread); RCX = [0x1401c3de8]; CALL 0x140099db0; then
// `_loadBit = -1` ([RBX+0x20]), so ~UpdateLoop never cleans this loop's bit and
// everything the container now points at stays resident for the process.
void InnerLoaderLoop::Load() {
    GameContainer_Init(g_gameContainer, _resourceList);
    _loadBit = -1;
}

// ============================================================
// GameContainer_Init -- 0x140099db0 (read 2026-10-11, decompile + disassembly)
// ============================================================
// Every slot is `ExternalList::Find(list, path)` (0x1800bf700), kept only when the
// result is non-null and IsDerivedFrom the slot's type, else null -- in retail's
// order: the five lists, SetAchievements(list [0]) on the AchievementManager, the
// three StringTables, the nine UI SoundEffects, the six cursors, the sprite sheet,
// the "White" and "SaveLoadEmpty" sprites built over it, and last the default input
// bindings (0x1400992f0). Nothing is tested beyond the per-slot type gate: the
// manager, list [0] and the sheet are handed on as found.
namespace {
template <class T>
Object::ExternalPtr<T> FindInList(const Object::ExternalList* list, const char* path) {
    const System::Filename name(path);
    Object::External* found = list->Find(name, T::TypeId()).Get();
    if (found != nullptr && found->IsDerivedFrom(T::TypeId()))
        return Object::ExternalPtr<T>(static_cast<T*>(found));
    return Object::ExternalPtr<T>(nullptr);
}
}  // namespace

void GameContainer_Init(GameContainer* self, const Object::ExternalList* list) {
    using Object::ExternalList;
    self->achievementList = FindInList<ExternalList>(list, "Game/Achievement/AchievementList.rsc");
    self->scenarioList    = FindInList<ExternalList>(list, "Game/Scenario/ScenarioList.rsc");
    self->climateList     = FindInList<ExternalList>(list, "Game/Climate/ClimateList.rsc");
    self->startConditions = FindInList<ExternalList>(list, "Game/StartCondition/StartConditions.rsc");
    self->terrainList     = FindInList<ExternalList>(list, "Game/Terrain/TerrainList.rsc");
    Achievement::AchievementManager::_singleton->SetAchievements(self->achievementList);

    self->gameKeys    = FindInList<UI::StringTable>(list, "Dialog/StringTable.rsc:gameKeys");
    self->keyNames    = FindInList<UI::StringTable>(list, "Dialog/StringTable.rsc:keyNames");
    self->globalNames = FindInList<UI::StringTable>(list, "Dialog/StringTable.rsc:globalNames");

    self->alertEffect          = FindInList<Audio::SoundEffect>(list, "Audio/UI/AlertEffect.rsc");
    self->buttonDownEffect     = FindInList<Audio::SoundEffect>(list, "Audio/UI/ButtonDownEffect.rsc");
    self->buttonUpEffect       = FindInList<Audio::SoundEffect>(list, "Audio/UI/ButtonUpEffect.rsc");
    self->beginDragEffect      = FindInList<Audio::SoundEffect>(list, "Audio/UI/BeginDragEffect.rsc");
    self->endDragEffect        = FindInList<Audio::SoundEffect>(list, "Audio/UI/EndDragEffect.rsc");
    self->placementMoveEffect  = FindInList<Audio::SoundEffect>(list, "Audio/UI/PlacementMoveEffect.rsc");
    self->placementThunkEffect = FindInList<Audio::SoundEffect>(list, "Audio/UI/PlacementThunkEffect.rsc");
    self->placementErrorEffect = FindInList<Audio::SoundEffect>(list, "Audio/UI/PlacementErrorEffect.rsc");
    self->warningEffect        = FindInList<Audio::SoundEffect>(list, "Audio/UI/WarningEffect.rsc");

    self->cursorArrow     = FindInList<UI::Cursor>(list, "Cursor/Arrow.rsc:cursor");
    self->cursorHand      = FindInList<UI::Cursor>(list, "Cursor/Hand.rsc:cursor");
    self->cursorHourglass = FindInList<UI::Cursor>(list, "Cursor/Hourglass.rsc:cursor");
    self->cursorMove      = FindInList<UI::Cursor>(list, "Cursor/Move.rsc:cursor");
    self->cursorQuery     = FindInList<UI::Cursor>(list, "Cursor/Query.rsc:cursor");
    self->cursorPlace     = FindInList<UI::Cursor>(list, "Cursor/Place.rsc:cursor");

    self->spriteSheet = FindInList<UI::SpriteSheet>(list, "Dialog/SpriteSheet.rsc");
    self->spriteWhite         = UI::Sprite(self->spriteSheet, System::String("White"));
    self->spriteSaveLoadEmpty = UI::Sprite(self->spriteSheet, System::String("SaveLoadEmpty"));

    Game::RegisterDefaultInputBindings();   // 0x1400992f0
}

const char* InnerLoaderLoop::GetResourceFilename() {
    // Original (FUN_140001110) returns LEA-loaded "Game/Global".
    return "Game/Global";
}

// ============================================================
// GameLoaderLoop (vtable @ 0x1401588d0) — src/game/Game/GameLoaderLoop.h
// ============================================================
// Retail overrides only [0]; every other slot is the base's (inherited).
// [1] Startup: the same STANDALONE DIVERGENCE as InnerLoaderLoop's.

// Retail 0x140001190 (slot 0): the same shape as the inner loop's.
GameLoaderLoop::~GameLoaderLoop() {
    printf("[OuterLL::Destroy] self=%p\n", (void*)this);
}

void GameLoaderLoop::Startup() {
    StallDebug::Phase phase("OuterLL::Startup");
    printf("[OuterLL::Startup] self=%p next=%p loader=%p\n",
           (void*)this, (void*)_nextLoop, (void*)&Engine::LoaderLoop::_loader);
    if (SkipEngineLoader()) {
        printf("[OuterLL::Startup] BANISHED_SKIP_ENGINE_LOADER set — skipping "
               "Engine::Loader::Load (see InnerLL note; expect a crash).\n");
    } else {
        // ENDGAME B.3.9 ENG W2: `_loader` is the class static (the minted
        // DLL-data slot and its engine_funcs address globals are gone), so the
        // old "g_pLoader null" branch cannot happen and went with them.
        Engine::LoaderLoop::_loader.Load(_nextLoop);
        printf("[OuterLL::Startup] Loader::Load returned\n");
    }
}

// Neither retail vtable address is registered any more: InnerLoaderLoop's 0x140158870 row went at the B.3.8
// close, GameLoaderLoop's 0x1401588d0 at B.3.9 EXA (every builder constructs
// the real class).
