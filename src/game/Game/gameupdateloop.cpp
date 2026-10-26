// src/game/Game/gameupdateloop.cpp
// Clean reimplementation of GameUpdateLoop vtable methods from Application-x64.exe.
//
// GameUpdateLoop (0x160 bytes), vtable at 0x14015e2f0.
// Extends UpdateLoop (0x28 bytes base).
//
// This is the main gameplay loop. It owns all game subsystems and orchestrates
// the simulation tick (Update), render frame (Draw), and lifecycle (Startup/Shutdown).
//
// Methods reimplemented:
//   - GameUpdateLoop::Update            (0x140096380) -- main simulation tick
//   - GameUpdateLoop::Draw              (0x140096010) -- render interpolated frame
//   - GameUpdateLoop::Startup           (0x140097C00) -- init UI dialogs, cursor, game systems
//   - GameUpdateLoop::Shutdown          (0x1400977C0) -- teardown all subsystems
//   - GameUpdateLoop::OnScenarioComplete(0x140096830) -- scenario win/lose notification
//
// HEAVILY INSTRUMENTED with printf() for tracing a hang during first Update tick
// after loading a tutorial scenario.

// ENDGAME B.3.5 Video/Graphics caller batch: Graphics::Scene and Video::Context
// are real classes. FIRST in the block on purpose — "resolve.h" (and several of
// the standalone headers below) pull <windows.h>, whose <wingdi.h> declares a
// global `Rectangle` FUNCTION that hides `Volume::Rectangle`'s class name inside
// Volume.h's own static_asserts (Scene.h -> Geometry.h -> Volume.h).
#include "graphics/Scene.h"
#include "Component/still_terrain.h"   // STILLTERRAIN: StillTerrain::Bump (the terrain epoch)
#include "graphics/Camera.h"          // ENDGAME B.3.5 batch K1 — Graphics::{Camera,Renderer}
#include "graphics/AnimationManager.h"
#include "video/Context.h"
#include "video/Device.h"
#include "video/ImageBuffer.h"
#include "graphics/SunLight.h"      // B.3.x W0: Shutdown deletes m_sunLight by name
#include "graphics/Mesh.h"          // B.3.x W0: Shutdown deletes m_skyMesh by name
#include "sampler.h"
#include "options.h"
#include "Game/GameUpdateLoop.h"   // ENDGAME B.3.8 LOOP: the real class
#include "Game/WorldGeometry.h"    // caller batch CB6: m_worldGeometry's class
#include "Player/Tool.h"            // caller batch CB6: the active tool's Draw
#include "Object/internal_factory_audit.h"
#include "platform_time.h"   // Platform::PerfCounter / PerfFrequency (the SIMPROF probes)
#include "ui/DialogInstance.h"
#include "object/TypeInfo.h"
#include "Game/game.h"   // B3R STORAGE: g_gameContainer
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "Game/Minimap.h"   // m_imageCapture's class (B.4 GAME-a)
#include "Component/CharacterGrid.h"   // m_characterGrid's class (Shutdown runs ~CharacterGrid)
#include "Game/EventLog.h"   // m_eventNotifier's class (B.4 GAME-a)
#include "Game/WorkScheduler.h"   // m_tutorial's class (B.4 GAME-a)
#include "Game/Calendar.h"   // m_dayCounter's class (B.4 GAME-a)
#include "Game/WeatherSimulation.h"   // m_weather's class (B.4 GAME-a)
#include "Game/Map.h"        // m_game's class (B.4 GAME-a)
#include "Game/PlayerToolManager.h"   // m_toolManager's class (B.4 GAME-a)
#include "Game/PlayerController.h"    // m_playerController's class (B.4 GAME-a)
#include "Game/ProfessionList.h"      // m_professionList's class
#include "Entity/EntityList.h"        // m_entityList's class
#include "Component/CitizenComponent.h"
#include "Component/CitizenDescription.h"
#include "system/MemoryAllocator.h"   // System::MemoryAllocator::MemAlloc / MemFree
#include "math/Math.h"
#include "system/String.h"
#include "system/Filename.h"
#include "showroom.h"         // BANISHED_SHOWROOM shot sequence (src/diag/)
#include "stress_harness.h"   // BANISHED_STRESS Update/Draw brackets (standalone/)
#include "frame_limiter.h"    // NOT RETAIL: the in-game frame cap (src/app/)
#include "ext/render_culling/render_culling.h" // EXT SEAM (NOT RETAIL, docs/extensions/handins/PERFXEXT.md): ext.render_culling / ext.anim_batch (PerfX)
#include "ext/walk_batch/walk_batch.h" // EXT SEAM (NOT RETAIL, docs/extensions/handins/WALKBATCH.md, WALKGRID.md): ext.walk_batch (A) / ext.walk_batch_grid (B)
#include "ext/spread_warmth/spread_warmth.h"   // EXT SEAM (NOT RETAIL, docs/extensions/handins/HITCH_PFBURST.md): ext.spread_warmth (class B), Ext::SpreadWarmth::BeginFrame
// EXT SEAM (NOT RETAIL, docs/extensions/handins/RESTRICTEDFOODS.md):
// ext.restricted_foods (class C), Ext::RestrictedFoods::BeginFrame
#include "ext/restricted_foods/restricted_foods.h"
// EXT SEAM (NOT RETAIL, docs/extensions/handins/ITEMPRIO.md):
// ext.item_priority (class C), Ext::ItemPriority::BeginFrame
#include "ext/item_priority/item_priority.h"
#include "ext/drain_split/drain_split.h"       // EXT SEAM (NOT RETAIL, docs/extensions/handins/HITCH_DRAINSPLIT.md): ext.drain_split (class B), Ext::DrainSplit::BeginFrame
#include "Game/terrain_sidecar.h" // the terrain-type mirror's teardown (faithful perf work)
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <cmath>
#include <cstdlib>
#include <atomic>
#include <new>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "system/Value.h"      // ENDGAME B.3.4b System caller batch (Random/Time/Color)
#include "ui/UserInterface.h"  // ENDGAME B.3.6 UI callers (K4)
#include "ui/Dialog.h"         // ENDGAME B.3.9 CB: UI::Dialog::TypeId()
#include "ui/String.h"         // ENDGAME B.3.6 UI callers (K4)
#include "ui/StringTable.h"    // ENDGAME B.3.6 UI callers (K4)
#include "achievement/AchievementManager.h"   // ENDGAME B.3.7 callers (K2)
#include "save/SaveLoad.h"                    // ENDGAME B.3.7 callers (K2)
#include "engine/MainLoop.h"                  // ENDGAME B.3.7 callers (K2)
#include "audio/Audio.h"                      // ENDGAME B.3.7 callers (K4)
#include "input/Input.h"                      // ENDGAME B.3.7 callers (K3)
#include "graphics/Mesh.h"               // ENDGAME B.3.9 SS2: MeshInstance::_rootOnlyUpdates
#include "graphics/ParticleInstance.h"   // ENDGAME B.3.9 SS2: ParticleInstance::_frameIndex
#include "io/FileSystem.h"               // ENDGAME B.3.9 SS2: IO::FileSystem::_singleton
#include "Component/AttendanceComponent.h"   // B4 S4 RT2: InternalPtr<AttendanceComponent>::Get() needs the class complete

// (EventNotifyDialog_Ctor_Standalone / AutoSaveDialog_Ctor_Standalone: Dialog/exe_bodies.h.)
extern "C" void BuildingGoal_Probe(const char* templateName);  // src/game/Goal/buildinggoal.cpp
// NOTE: the tree's second port of FUN_14008c100 (EventLog_CheckResourceShortages)
// is gone (B4 GAME-a: it had no caller) — see the retirement note at its former
// call site below. The scan runs once per tick from CitizenBehavior::CheckThresholds.

// Minimap terrain painter (src/game/Game/minimap_paint.cpp) — standalone fallback for
// the exe FUN_1400a5140 (ImageCapture::DrawCheck), wired into fn_ImageCapture_DrawCheck.
// In-app RenderDoc capture driver — ticked once per in-game rendered frame so a
// delayed capture armed by OptionsDialog::ApplyVideoSettings (after an MSAA/res
// apply) actually fires. No-op unless the exe was launched via RenderDoc's UI.
#include "renderdoc_integration.h"
#include "scene_watch.h"
#include "citizen_vitals.h"
#include "mouse_inject.h"                 // BANISHED_MOUSE_INJECT hover/drag route (standalone/)
#include "object/External.h"   // ENDGAME B.3.3 step 6A — must precede the
#include "Game/exe_bodies.h"
#include "Game/exe_constants.h"          // kRdata_* (B3R)
#include "Action/exe_bodies.h"           // Pf_RegionPairCacheTick_5d10
#include "Component/exe_bodies.h"        // CitizenComp_AssignHousing
#include "Dialog/exe_bodies.h"           // ApplyKeyMapToUI_Impl, Toolbar_ShowGroup_Impl
#include "Dialog/ToolbarDialog.h"        // ToolbarDialog
#include "Dialog/AutoSaveDialog.h"       // AutoSaveDialog (a UI::DialogInstance)
#include "platform/common/platform_app.h"   // Platform::App::TakeSaveRequest (P7, Android lifecycle)
#include "Dialog/EventNotifyDialog.h"    // EventNotifyDialog (a UI::DialogInstance)
#include "Entity/CitizenBehavior.h"      // m_citizenList's class: CitizenBehavior::Update
#include "graphics/Animation.h"          // RtGraphics_AnimationManager_Dtor
#include "picking/Scene.h"
#include "Game/MusicPlayer.h"   // MusicPlayer::Update (0x1400a40e0)
                                // `namespace Object { ... }` forward decl below
                                // (NAMESPACE HAZARD: spell ::Object::X everywhere
                                // in this file, even at global scope)
#include "ext/registry.h"   // EXT SEAM (NOT RETAIL, docs/extensions/handins/EXTSCAFFOLD.md): Ext::UnlockForGame
#include "ext/background_save/background_save.h"   // EXT SEAM (NOT RETAIL, docs/extensions/handins/HITCH_SAVESNAP.md): ext.background_save, the frozen Update / Draw alpha
#include "ext/eye_camera/eye_camera.h"   // EXT SEAM (NOT RETAIL, docs/extensions/handins/EYECAM.md): ext.eye_camera, Update step 6 / Draw steps 4 and 7
#include "ext/sharp_ui/sharp_ui.h"       // EXT SEAM (NOT RETAIL, docs/extensions/handins/SHARPUI.md): ext.sharp_ui, Draw step 12
#include "ext/ambient_occlusion/ambient_occlusion.h"     // EXT SEAM (NOT RETAIL, docs/extensions/AO.md): ext.ambient_occlusion, Draw step 7
#include "load_time.h"   // [LoadTime] stamps (NOT RETAIL, print-only; block LOADSTAMP)
#include "Game/cell_stamp.h"   // AMBMEMO: CellStamp::Invalidate at teardown
#include "Component/AmbientEnvironmentComponent.h"   // AMBMEMO: ReportMemo at teardown


// ============================================================
// Exe globals (resolved at init time from .rdata/.data addresses)
// ============================================================

// Graphics::MeshInstance::_rootOnlyUpdates (retail: import slot 0x140151f30) and
// Graphics::ParticleInstance::_frameIndex (import slot 0x140151f38) are read as
// their class statics (ENDGAME B.3.9 SS2).

// ============================================================
// Exe function pointers (not yet in engine_funcs.h)
// ============================================================

// RegionPairCache::Tick (0x1400a5d10) - per-sim-tick staleness sweep of the
// pathfinder REGION-PAIR DISTANCE CACHE at Game+0x78. Named "UpdateTimedEvents"
// here historically; there is no timed-event queue. Ported + registered
// 2026-08-30 (reg_pf_region.cpp) - see src/game/Action/region_paircache.cpp.
// RCX=regionPairCache (game+0x78), RDX=time

// Graphics::AnimationManager::Update(this, Time const&, bool rootOnly)
// Runtime DLL export
// ENDGAME B.3.5 batch K1: the `fn_AnimationManager_Update` cache (read out of exe
// dispatch slot 0x140152050) went with its one caller, which calls
// `Graphics::AnimationManager::Update` directly. The member is DEFINED — the door in
// src/engine/graphics/wave23_lane_gfxbody.cpp:521 delegates to it.

// EntityList::Update (FUN_1400c4090) — tick all entity components
// RCX=entityList, RDX=time

// CitizenBehavior::Update / Map::Update (FUN_14008c2b0) — population tick
// RCX=citizenList, RDX=time

// EntityList::PostUpdate (FUN_1400c43d0) — post-update cleanup pass
// RCX=entityList

// AmbientSound::Update (FUN_1400a40e0) — ambient sound state update
// RCX=ambientSoundData

// EntityUI::Update / PlayerToolUpdate (FUN_1401026a0)
// RCX=entityUI, RDX=time(int64), R8=inputFlag(bool)
// Returns int (cursor index)

// PlayerController::CalcTransform (FUN_1400983f0)
// RCX=playerController, RDX=outTransform -> returns Transform*

// Graphics::Scene::Clean(this) — moved to engine_funcs.{h,cpp}; reuse the
// project-wide extern so MenuDLT can also call it.

// ENDGAME B.3.5: the `?RemoveGlobalObject@Scene@Graphics@@QEAAXPEAVGeometry@2@@Z`
// cache (typedef + static + its resolver) is GONE — all five callers in this file
// now call `Graphics::Scene::RemoveGlobalObject` directly.

// Graphics::Renderer::Draw(this, Context*, float alpha, Scene*, Camera*)
// (NB: a global with the same name is declared extern in engine_funcs.h. The two
// resolve to the same Runtime export when both succeed; we keep the local static
// here for backward-compat with the existing in-flight reimpl. Local static name
// renamed to avoid the extern/static clash.)
// ENDGAME B.3.5 batch K1: the `fn_Graphics_Renderer_Draw_local` alias (binding spelling
// 5 — a plain `s_fn_X = fn_Y` with a corrected-mangled-name fallback) went with its one
// caller, which calls `Graphics::Renderer::Draw` directly. The member is DEFINED — the
// door in src/engine/graphics/wave27_lane_gfxbig.cpp:567 delegates to it. The [FIX-M5]
// history the fallback recorded is kept at the resolver site.

// Graphics::Renderer::~Renderer(this)

// ENDGAME B.3.5: the `?SetTime@Context@Video@@QEAAXM@Z` and
// `?SetCamera@Context@Video@@QEAAXAEBVMatrix@Math@@0@Z` caches (typedefs,
// statics, resolvers and CHECK_FN lines) are GONE — both are members of block
// D1's real `Video::Context` now.

// Math::Transform3::AsMatrix(this) -> Matrix (non-trivial return: 64 bytes)
// MSVC x64 member function ABI: RCX=this, RDX=hidden retval ptr, returns RAX=retval ptr

// Math::Vector3::Normalize(this)
// REMOVED 2026-07-31: `fn_Vector3_Normalize` lived here with a ONE-ARG typedef
// `void(*)(void* self)` bound to `?Normalize@Vector3@Math@@QEBA?AV12@XZ`. That export
// returns Vector3 BY VALUE, so the ABI passes a hidden return-buffer pointer in RDX and
// the callee WRITES 8+ bytes through it — a one-arg call leaves RDX stale and the store
// lands wherever the last call left it. It was resolved and CHECK_FN'd but never called
// from this file, so it corrupted nothing; it was a loaded gun for the next caller.
// See reference_byvalue_return_hidden_rdx — this same export already caused one
// deterministic crash (the ±0.7071 garbage signature).
//
// Deleted rather than corrected: the three real callers (action_base_move.cpp:58,
// moveto.cpp:101, and the action_child_ctors/animalcomponent/weather_particles group)
// each declare their own correct 2-arg `(void* self, void* retbuf)` static. An unused,
// wrongly-typed resolver is pure liability.
//
// NOTE the mirror-image trap: `AEAV` is a REFERENCE return, NOT by-value.
// `?Normalize@Vector2@Math@@QEAAAEAV12@XZ` returns Vector2& in RAX and a one-arg typedef
// is CORRECT for it (worldgen_f390_coastline.cpp:94 looks wrong and is right). Same
// method name, different ABI between Vector2 and Vector3.

// Engine::MainLoop::SetNextLoop — declared in engine_funcs.h, resolved by EngineFuncsInit()

// fn_ImageBuffer_CopyToTexture — available from engine_funcs.h

// FUN_140095da0 — QuickSave handler (complex, just call via pointer)

// Startup sub-functions
// FUN_140099260 — SetupUIReferences(keyMapManager, UI)

// FUN_1400a90a0 — ProfessionList::Startup(professionList)

// fn_ToolbarDialog_Ctor — available from engine_funcs.h (last param is void* toolbarData)

// FUN_140068ab0 — EventNotifyDialog::Ctor(mem)

// FUN_14005e230 — AutoSaveDialog::Ctor(mem, ui, dialog, parentPtr)

// FUN_140101ee0 — EntityUI::Startup(entityUI)

// Shutdown sub-functions
// FUN_140102b60 — EntityUI::SetTool(entityUI, tool)

// CitizenComponent::AssignHousing — now shared from engine_funcs.h

// EntityList::Shutdown (FUN_1400c3d30)

// EntityList::Dtor (FUN_1400c3e60)

// CitizenBehavior::Dtor (FUN_1400907b0)

// Game::Dtor (FUN_14009ed60)

// Tutorial::Dtor (FUN_1400f8ff0)

// EntityUI::Dtor (FUN_1401024d0)

// Weather::Dtor (FUN_140093eb0)

// EventNotifier string cleanup (FUN_14000a8a0)

// CharacterGrid::Dtor (FUN_140018220) - frees the five arrays at +0x10/+0x20/
// +0x30/+0x40/+0x50 of the 0x60-byte Morton quadtree at gameWorld+0x88.
// NOT a music dtor: the MusicPlayer at +0x80 is a POD retail simply MemFrees.

// ImageCapture::Dtor (FUN_1400a4fc0)

// Runtime destructors (resolved by mangled name or ordinal)




// Draw sub-functions
// FUN_1400a5140 — ImageCapture::DrawCheck (image capture terrain update)

// FUN_140098660 — PlayerController::UpdateCamera(playerCtrl, float, float, float)

// 0x1400938d0 — WeatherSimulation::DrawUpdate(ctx) (src/game/Game/weather_sun.cpp)

// 0x1401026a0 — PlayerToolManager::Update (src/game/Dialog/entityui.cpp): the
// tool, its hotkeys and the camera (Game/camera_input.cpp).

// OnScenarioComplete sub-functions
// FUN_140095580 — EventNotifier::FireNotification(notifier, eventType, priority, entityPtr)

// FUN_140095750 — EventNotifier::FireNotificationWithName(notifier, type, priority, entityPtr, nameStr)

// fn_UIString_Ctor — available from engine_funcs.h (retval, stringTable, stringName)

// fn_UIString_ToSystemString — available from engine_funcs.h (self, retval)

// FUN_1401027c0 — CalcWindFactor (for audio scene update)
// FUN_1401027c0 — despite the name, a pure DISTANCE: |cameraPos - playerCtrl anchor|.
// Returns a FLOAT in XMM0 (it tail-JMPs into the sqrtf at 0x140132010); this typedef
// used to say uint32_t, which would have read garbage out of EAX. Its Time* param is
// dead in retail — spilled at entry and never read.
static float CalcWindFactor(const System::Time* time);   // defined below

// System::Time::Time(this, float seconds) -> Time* (constructor from seconds)

// Object::ExternalList::Find(this, retval, Filename const&, Object::Type) (Runtime 0x1800bf700)
// `retval` is the HIDDEN RETURN SLOT for the ExternalPtr<External> this returns
// by value; ExternalPtr is ONE RAW POINTER and holds NO reference.
//
// *** THE 4th ARGUMENT IS NEVER READ. *** Reversed 2026-09-06 (runtime port
// Wave 3 lane C): R9 is dead through the whole body — the list is searched by
// PATH only and the first path match is returned whatever its type. Do not
// expect it to filter. (It is `Object::Type`, not a `typeInfo*`; the old
// parameter name here implied both a pointer and an effect, and neither holds.)
// The match is a linear scan and is case-SENSITIVE, unlike ExternalFactory::Find
// over the same kind of path, which is case-insensitive. A path with no
// `:identifier` suffix is searched as `path + ":resource"`.

// System::Filename::Filename(this, const char*) (Runtime)

// fn_Filename_Dtor — available from engine_funcs.h

// fn_InternalBasePtr_Ctor — available from engine_funcs.h

// ENDGAME B.3.9 CB: the toolbar lookup's type id is UI::Dialog's. Retail reads it
// through the import slot ?_typeInfo@Dialog@UI (0x140151728) at 0x140097c83 /
// 0x140097cae (FUN_140097c00). The tree used to resolve exe cell 0x1401c4138 instead --
// that is CreatePathDescription's record (CreatePathDescription_RegisterType
// 0x14014ab25; its only reader is EntityUI_impl_1ee0), unregistered, so the read
// always failed and a by-name "Dialog" fallback supplied the value.

// ============================================================
// Float constants from exe .rdata
// ============================================================
// DAT_14017214c = 1.0f (UV scale / general purpose)
// DAT_14017219c = some audio time scale factor
// DAT_1401721dc, DAT_140172174, DAT_140172240 = camera update constants
// B3R: read as kRdata_14017214c / _14017219c / _1401721dc / _140172174 / _140172240.



// ============================================================
// Initialization — must be called from EngineFuncsInit or similar
// ============================================================
static int s_frameNum = 0;

// Per-render-frame token (incremented once per GameUpdateLoop::Update). Exposed so the
// tree-driven ParticleComponent tick can un-stamp each pooled emit-state at most once
// per frame (matching retail's once-per-render-frame _frameIndex advance in
// Renderer::Render), independent of how many houses share the emit or how many sim
// sub-iterations the speed multiplier runs.
extern "C" int GameUpdateLoop_CurrentFrame() { return s_frameNum; }

// NOT RETAIL, a platform fit (docs/vulkan/PLAN.md §4.6; Platform::App::TakeSaveRequest, which only
// Android answers): a save the platform asked for (SDL_EVENT_LOW_MEMORY / TERMINATING) runs the
// game's own autosave body, AutoSaveDialog::Autosave (0x14005dec0), with its own refusals (the UI
// pauses the game, the world is blocked, an operation runs). A refused request is retried every 120
// frames, up to ten times. A request is for the game being played: one made while no game ran (the
// main menu, a load) is dropped when the next game starts (ResetPlatformSaveRequest), so it never
// rotates that game's Auto.sav unasked.
static int s_savePending = 0;   // tries left
static int s_saveWait = 0;      // frames to the next try

static void ResetPlatformSaveRequest() {
    if (Platform::App::TakeSaveRequest()) {
        fprintf(stderr, "[Lifecycle] a save request made while no game ran is dropped\n");
        fflush(stderr);
    }
    s_savePending = 0;
    s_saveWait = 0;
}

static void HonourPlatformSaveRequest(UI::UserInterface* ui) {
    int& s_pending = s_savePending;
    int& s_wait = s_saveWait;
    if (Platform::App::TakeSaveRequest()) {
        s_pending = 10;
        s_wait = 0;
    }
    if (s_pending == 0) return;
    if (s_wait > 0) {
        --s_wait;
        return;
    }
    const Save::SaveLoad* saveLoad = ::Save::SaveLoad::_singleton;
    AutoSaveDialog* autoSave = nullptr;
    const uint32_t autoSaveTid = AutoSaveDialog::TypeId();
    for (auto it = ui->_dialogs.GetFirst(); it; ++it) {
        UI::DialogInstance* dialog = it.node->element;
        if (dialog && ::Object::IsDerivedFromNullSafe(dialog, autoSaveTid)) {
            autoSave = static_cast<AutoSaveDialog*>(dialog);   // the dialog IS the AutoSaveDialog
            break;
        }
    }
    --s_pending;
    if (saveLoad != nullptr && saveLoad->GetOperation() == Save::SaveLoad::kOpSave) {
        fprintf(stderr, "[Lifecycle] the platform's save request found a save already running (frame %d)\n",
                s_frameNum);
        fflush(stderr);
        s_pending = 0;
        return;
    }
    if (autoSave != nullptr && saveLoad != nullptr) {
        autoSave->Autosave();
        if (saveLoad->GetOperation() == Save::SaveLoad::kOpSave) {
            fprintf(stderr, "[Lifecycle] the platform's save request started the autosave (frame %d)\n", s_frameNum);
            fflush(stderr);
            s_pending = 0;
            return;
        }
    }
    fprintf(stderr, "[Lifecycle] the platform's save request was refused (%s); %d tries left\n",
            autoSave == nullptr ? "no AutoSaveDialog" : "see [AutoSave]", s_pending);
    fflush(stderr);
    s_wait = 120;
}
// Debug trace — disabled for performance, enable when debugging
#define TRACE(...) do { } while(0)

// B3R: EnsureInit and every cache it filled are gone. Each callee is called by
// name (Game/, Entity/, Action/, Dialog/ exe_bodies.h and the Runtime headers);
// the .rdata floats are kRdata_* (Game/exe_constants.h); the unread
// g_pDebugSingleton cell (0x1401c3de0 is g_pGameSettingsPtr's cell) went with it.
// The 0x140152160 dispatch-slot fallback for
// ExternalList::Find went with its cache (the member is defined).


// ============================================================
// Forward declarations
// ============================================================

// In-game tool-injection harness (src/diag/tool_harness.cpp), env-gated by
// BANISHED_TOOL_HARNESS. No-op unless that env var is set.
extern "C" void ToolHarness_Tick(PlayerToolManager* entityUI, int frameNum);
#include "pausemenu_harness.h"   // PauseMenuHarness_Tick (src/diag)
#include "object/class_vptr.h"   // Object::ClassVptr (the ctor records the vptr hint)
extern "C" void FoodPanel_Tick(PlayerToolManager* entityUI, int frameNum);  // src/diag/food_panel_harness.cpp
extern "C" void FoodPlace_Tick(int frameNum);                  // src/diag/food_placement_harness.cpp
extern "C" void StartTown_DeferredSpawn_Tick(int frameNum);    // src/game/Game/starttown.cpp
extern "C" void StartTown_ApplyPendingOccupiedVisuals(int frameNum);   // src/game/Game/starttown.cpp
extern "C" void FoodStockCensus_Tick(int frame);                 // src/diag/food_stock_census.cpp
extern "C" void CitizenDeathHarness_Tick(int frameNum);   // src/diag/death_harness.cpp
extern "C" void SaveLoadHarness_Tick(GameUpdateLoop* gul, int frameNum); // src/diag/saveload_harness.cpp
extern "C" void DisasterHarness_Tick(int frameNum);       // src/diag/disaster_harness.cpp
extern "C" void PastureFillHarness_Tick(int frameNum);    // src/diag/pasture_harness.cpp
extern "C" void MarketOverstock_Tick(int frameNum);   // src/diag/market_overstock_harness.cpp
extern "C" void PanelStaffingDriver_Tick(PlayerToolManager* entityUI, int frameNum);
extern "C" void PanelHoldHarness_Tick(PlayerToolManager* entityUI, int frameNum);  // src/diag/panel_staffing_driver.cpp
extern "C" void BuilderSeat_Tick(int frameNum);        // src/diag/builder_seat_harness.cpp
extern "C" float AchClock_TimeScale();                 // src/diag/ach_clock.cpp
extern "C" void  AchClock_Tick(int frameNum);          // src/diag/ach_clock.cpp
extern "C" void TradeSellHarness_Tick(PlayerToolManager* entityUI, int frameNum);  // src/diag/trade_buy_harness.cpp
extern "C" void FinishBuildsHarness_Tick(int frameNum);   // src/diag/finish_builds_harness.cpp
extern "C" void OrchardDiag_Tick(int frameNum);           // src/diag/orchard_diag.cpp
extern "C" void FoodDiag_Tick(int frameNum);              // src/diag/food_production_diag.cpp
extern "C" void CropFieldForceStart_Tick(int frameNum);   // src/diag/cropfield_force_start.cpp
extern "C" void OrchardForceStart_Tick(PlayerToolManager* entityUI, int frameNum);  // src/diag/orchard_force_start.cpp
extern "C" void OrchardWorkersHarness_Tick(int frameNum);  // src/diag/orchard_workers_harness.cpp
extern "C" void FoodWorkers_Tick(int frameNum);            // src/diag/food_workers_harness.cpp
extern "C" void OrchardGrowthHarness_Tick(int frameNum);   // src/diag/orchard_growth_harness.cpp
extern "C" void OrchardCutHarness_Tick(int frameNum);      // src/diag/orchard_cut_harness.cpp
// TownHallUI gate diagnostic (src/game/Dialog/townhallui.cpp) — one-shot report
// of the fn-pointer / type-id / town-data gates behind the town-hall panel.
extern "C" void TownHallSA_Diag(int frameNum);

// fn_MemFreeStatic is already available from engine_funcs.h

// ============================================================
// GameUpdateLoop::Update (0x140096380) — Main simulation tick
// ============================================================
// This is the core game loop tick. Called once per frame by MainLoop::Frame.
// Runs the simulation N times (speed multiplier), then updates UI, audio, etc.
//
// HEAVILY INSTRUMENTED for debugging tutorial hang.


// ============================================================
// [SIMPROF] Per-phase frame profiler (env BANISHED_SIMPROF)
// ============================================================
// Diagnostic only: wall-clock cost of each sim phase, so we can tell whether a
// speed-multiplier slowdown is a linear "N sim iterations" cost or something
// superlinear. Reports every BANISHED_SIMPROF frames (default 120) and resets.
// Zero cost when off (one predictable branch per probe).
// QueryPerformanceCounter/Frequency are Platform::PerfCounter / PerfFrequency
// (platform_time.h); the wrappers below keep the call sites on plain int64.

namespace {

enum SimPhase {
    kPhPairCache = 0, kPhAnimation, kPhEntityList, kPhWeather,
    kPhCitizenList, kPhPostSim, kPhWholeUpdate, kPhCount
};
const char* const kPhaseName[kPhCount] = {
    "PairCache", "Animation", "EntityList", "Weather",
    "CitizenList", "PostSim(UI/cam/audio)", "TOTAL Update"
};

int      g_simProfOn   = -1;      // -1 = unresolved, 0 = off, else report interval
long long g_qpcFreq    = 0;
double   g_phSec[kPhCount] = {0};
int      g_phCalls[kPhCount] = {0};
// Second accumulator with its OWN reset, drained by the BANISHED_STRESS harness
// (GameUpdateLoop_SimProfTake) so its CSV rows are not tied to the stderr report interval.
double   g_phSecStress[kPhCount] = {0};
int      g_profFrames  = 0;
long long g_profSimIters = 0;

inline bool SimProfOn() {
    if (g_simProfOn < 0) {
        const char* e = Opt().simprof;
        int n = (e && e[0] && e[0] != '0') ? std::atoi(e) : 0;
        if (e && e[0] && e[0] != '0' && n <= 1) n = 120;   // "1" => default interval
        g_simProfOn = n;
        if (n) { int64_t f; Platform::PerfFrequency(&f); g_qpcFreq = f; }
    }
    return g_simProfOn > 0;
}
inline long long QpcNow() { int64_t t; Platform::PerfCounter(&t); return t; }

// Milliseconds between two QpcNow() samples. Self-contained: g_qpcFreq is only
// filled when the sim profiler is armed, so query the frequency here instead.
inline double QpcMs(long long ticks) {
    static long long freq = 0;
    if (!freq) { int64_t f; Platform::PerfFrequency(&f); freq = f; }
    return freq ? 1000.0 * (double)ticks / (double)freq : 0.0;
}

struct PhaseTimer {
    int ph; long long t0; bool on;
    explicit PhaseTimer(int p) : ph(p), t0(0), on(SimProfOn()) {
        g_samplerTag = 2 + p;   // [Sampler] phase tag (src/diag/sampler.h)
        if (on) t0 = QpcNow();
    }
    ~PhaseTimer() {
        g_samplerTag = 1;
        if (!on || !g_qpcFreq) return;
        const double s = (double)(QpcNow() - t0) / (double)g_qpcFreq;
        g_phSec[ph] += s;
        g_phSecStress[ph] += s;
        g_phCalls[ph]++;
    }
};

// Emit the accumulated report and reset the window.
void SimProfReport(int speedMultiplier, int32_t entCount) {
    if (++g_profFrames < g_simProfOn) return;
    double total = g_phSec[kPhWholeUpdate];
    double msPerFrame = total * 1000.0 / g_profFrames;
    std::fprintf(stderr,
        "[SIMPROF] speed=%d frames=%d  %.2f ms/frame (%.1f fps equivalent)  "
        "simIters/frame=%.2f entities~=%d\n",
        speedMultiplier, g_profFrames, msPerFrame,
        msPerFrame > 0 ? 1000.0 / msPerFrame : 0.0,
        (double)g_profSimIters / (double)g_profFrames, entCount);
    for (int i = 0; i < kPhCount; i++) {
        if (!g_phCalls[i]) continue;
        double ms = g_phSec[i] * 1000.0 / g_profFrames;
        std::fprintf(stderr,
            "[SIMPROF]   %-22s %8.3f ms/frame  %6.2f%%  calls/frame=%.2f  %.4f ms/call\n",
            kPhaseName[i], ms, total > 0 ? g_phSec[i] * 100.0 / total : 0.0,
            (double)g_phCalls[i] / (double)g_profFrames,
            g_phSec[i] * 1000.0 / (double)g_phCalls[i]);
    }
    std::fflush(stderr);
    for (int i = 0; i < kPhCount; i++) { g_phSec[i] = 0; g_phCalls[i] = 0; }
    g_profFrames = 0;
    g_profSimIters = 0;
}

// [FORCE SPEED] env BANISHED_FORCE_SPEED=N — pin the EntityUI speed multiplier
// (+0x34) and clear the paused flag (+0x30) every frame, so an A/B of 1x vs 2x
// vs 3x is deterministic under autostart with no clicking. Diagnostic only.
int g_forceSpeed = -1;
inline void ApplyForceSpeed(PlayerToolManager* toolManager) {
    if (g_forceSpeed < 0) {
        const char* e = Opt().forceSpeed;
        g_forceSpeed = (e && e[0]) ? std::atoi(e) : 0;
    }
    if (g_forceSpeed > 0 && toolManager) {
        toolManager->paused = false;               // not paused
        toolManager->field_0x34 = g_forceSpeed;        // speed multiplier
    }
}

}  // namespace

// [STRESS] drain the harness copy of the phase accumulators: outMs[i] = total ms of
// phase i since the previous Take (i in SimPhase order; kPhWholeUpdate is the last).
extern "C" int GameUpdateLoop_SimProfTake(double* outMs, int maxN) {
    const int n = maxN < kPhCount ? maxN : kPhCount;
    for (int i = 0; i < n; i++) { outMs[i] = g_phSecStress[i] * 1000.0; }
    for (int i = 0; i < kPhCount; i++) g_phSecStress[i] = 0;
    return n;
}
extern "C" const char* GameUpdateLoop_SimProfName(int phase) {
    return (phase >= 0 && phase < kPhCount) ? kPhaseName[phase] : "?";
}

bool GameUpdateLoop::Update(const System::Time& timeRef) {
    // NOT RETAIL (block LOADSTAMP): one [LoadTime] line on the first Update of each loop.
    {
        static const GameUpdateLoop* s_stampedUpdate = nullptr;
        if (s_stampedUpdate != this) { s_stampedUpdate = this; LoadTime::Stamp("game.update.first"); }
    }
    // Retail's slot takes the Time by const&; the tick bodies below take it as a
    // const Time* (CitizenBehavior::Update still as a mutable int64_t*, below).
    const System::Time* time = &timeRef;
    // Stage C entry guard (TEMPORARY): GameUpdateLoop::Load (0x140096b00, the
    // in-game world build) is not yet implemented, so on the new-game path
    // m_game (+0xF8) is still null when the loader chain transitions in. Every
    // step below dereferences the unbuilt world, AND EnsureInit() below resolves
    // dispatch slots (e.g. 0x140152058 = AchievementInstance::Update) that are
    // unregistered in standalone and null-deref. So this guard MUST run before
    // EnsureInit. Hold with a no-op tick until Load runs — remove once Load
    // constructs the world (project_newgame_path_roadmap Stage C). The STAGE C
    // marker has already fired by this point.
    if (m_game == nullptr) {
        static bool logged = false;
        if (!logged) {
            logged = true;
            fprintf(stderr, "[GameUpdateLoop::Update] world not built "
                    "(m_game==null) — Stage C cliff, holding (no-op tick)\n");
            fflush(stderr);
        }
        return true;
    }

    // EXT SEAM (NOT RETAIL, docs/extensions/handins/HITCH_SAVESNAP.md): ext.background_save -- while the save is serialised on its worker
    // the sim is frozen and this Update is the camera and the save poll only
    // (src/ext/background_save, docs/extensions/handins/HITCH_SAVESNAP.md).
    if (Ext::BackgroundSave::Busy()) {
        Ext::BackgroundSave::FrozenUpdate(*this, *time);
        return true;
    }

    s_frameNum++;

    // [SIMPROF] whole-Update stamp + deterministic speed override (both diagnostic-only)
    const bool simProf = SimProfOn();
    const long long frameT0 = simProf ? QpcNow() : 0;
    g_samplerTag = 1;   // [Sampler] inside Update
    struct SamplerTagReset { ~SamplerTagReset() { g_samplerTag = 0; } } samplerTagReset;
    ApplyForceSpeed(m_toolManager);
    // [SHOWROOM] hold the world paused while the showroom frames and shoots its buildings,
    // so every shot sees the same season, weather and sun.
    if (Showroom_WantsPause()) {
        m_toolManager->paused = true;
    }
    StressHarness_UpdateBegin(s_frameNum);   // [STRESS] no-op unless BANISHED_STRESS
    Ext::RenderCulling::BeginFrame(m_renderer);      // EXT SEAM (NOT RETAIL, docs/extensions/handins/PERFXEXT.md): ext.render_culling / ext.anim_batch read once per frame (frustum, pause flush)
    Ext::WalkBatch::BeginFrame();            // EXT SEAM (NOT RETAIL, docs/extensions/handins/WALKBATCH.md, WALKGRID.md): ext.walk_batch (A) / ext.walk_batch_grid (B) read once per frame
    Ext::SpreadWarmth::BeginFrame();         // EXT SEAM (NOT RETAIL, docs/extensions/handins/HITCH_PFBURST.md): ext.spread_warmth (B) read once per frame, its budget reset
    // EXT SEAM (NOT RETAIL, docs/extensions/handins/RESTRICTEDFOODS.md): ext.restricted_foods (C) read once per frame
    Ext::RestrictedFoods::BeginFrame();
    // EXT SEAM (NOT RETAIL, docs/extensions/handins/ITEMPRIO.md): ext.item_priority (C) read once per frame
    Ext::ItemPriority::BeginFrame();
    Ext::DrainSplit::BeginFrame();           // EXT SEAM (NOT RETAIL, docs/extensions/handins/HITCH_DRAINSPLIT.md): ext.drain_split (B) read once per frame, the owed half's frame count

    // [EVENTS] RETIRED: the BANISHED_LIVE_EVENTS resource-shortage scanner used to
    // run here (a second port of FUN_14008c100 via EventLog_CheckResourceShortages).
    // It is superseded by the registered tick path: CitizenBehavior::CheckThresholds
    // (citizenbehavior.cpp) scans the SAME townMgr+0x170/+0x180 shortage tables every
    // tick and, now that 0x140095580 (EventLog_AddEvent) is registered as
    // fn_NotificationEvent, fires the same 0x11-0x18 sev-3 events. Keeping this block
    // would double-decrement those shared cooldown timers and double-log each
    // shortage, so it is removed — following the "retire stale duplicate" precedent.
    // (B4 GAME-a: the uncalled duplicate port is deleted.)
    // The scenario/achievement popup (0x1a-0x1d) source op_6830 (0x140096830) and the
    // ribbon / EventPopupDialog paths are unaffected and stay live for any real event.


    // Drive a Player tool (env-gated, one-shot) so otherwise-dormant tool code
    // paths fire for debugger validation. No-op unless BANISHED_TOOL_HARNESS set.
    ToolHarness_Tick(m_toolManager, s_frameNum);

    // Open the IN-GAME pause menu (the toolbar menu button's action, retail
    // ToolbarDialog_OnRemove case 3) and click one of its buttons -- by default
    // Quit Game. The front-end Quit is already drivable via BANISHED_AUTOQUIT;
    // this is the only unattended way to reach the in-game one, which needs a
    // live world underneath it. No-op unless BANISHED_PAUSEMENU is set
    // (src/diag/pausemenu_harness.cpp).
    PauseMenuHarness_Tick(m_toolManager, s_frameNum);

    // Dialog harness for the six food-production panels: opens each building's info
    // panel and reports the controllers on it plus the component's CanCommand verdicts
    // (what greys or enables each button). No-op unless BANISHED_FOOD_PANEL is set
    // (src/diag/food_panel_harness.cpp).
    FoodPanel_Tick(m_toolManager, s_frameNum);

    // Placement + SLOT-COVERAGE harness: where each food building landed, and which of
    // its 22 component vtable slots are actually live versus still holding the shared
    // inert stub. No-op unless BANISHED_FOOD_PLACE is set
    // (src/diag/food_placement_harness.cpp).
    FoodPlace_Tick(s_frameNum);

    // Deferred demo spawns that CANNOT be sited at world-build time because their
    // siting input does not exist yet. Currently the hunter lodge, which is scored by
    // live animal density out of the character spatial-hash manager at gw+0x88 — an
    // index that is empty during StartTown because characters register during their own
    // Startup. Must run BEFORE FoodWorkers_Tick's first pass (frame 400) so the lodge is
    // staffable on that pass; it re-checks every 120 frames regardless. No-op unless
    // BANISHED_BUILD_DEMO_HUNTERLODGE is set (src/game/Game/starttown.cpp).
    StartTown_DeferredSpawn_Tick(s_frameNum);

    // Kill N citizens on a chosen frame so the removal teardown (HandleLifeEvent ->
    // MarkForRemoval -> the +0x38 broadcast -> CitizenComponent::OnRemove) can be
    // exercised without waiting decades of game time for a natural death. No-op
    // unless BANISHED_KILL_CITIZENS is set.
    CitizenDeathHarness_Tick(s_frameNum);

    // Trigger the retail autosave path on demand and log the operation's
    // progress + the resulting .sav. No-op unless BANISHED_SAVE_TEST is set
    // (src/diag/saveload_harness.cpp).
    SaveLoadHarness_Tick(this, s_frameNum);
    HonourPlatformSaveRequest(m_userInterface);   // NOT RETAIL (P7): the Android lifecycle's save

    // Fire / citizen-disease / crop-infestation / tornado on demand. Every disaster
    // rolls its own dice inside its own component tick, and those chances are
    // denominated in YEARS (Common.rsc _fireChance = 20-40 years per 100 population),
    // so the organic arm is unreachable in a session-length run. Half the building
    // templates carry _years=0 and can never ignite at all, which is why the fire arm
    // targets a HOUSE at the centre of the residential cluster rather than rolling a
    // random structure. No-op unless one of BANISHED_FORCE_{FIRE,TORNADO,SICK,INFEST}
    // is set (src/diag/disaster_harness.cpp).
    DisasterHarness_Tick(s_frameNum);

    // One-shot pasture stocking for the animal-wave test rig. No-op unless
    // BANISHED_PASTURE_FILL is set (src/diag/pasture_harness.cpp).
    PastureFillHarness_Tick(s_frameNum);

    // Force-feed the market one resource past 2x its target share so MarketWork
    // slot 4 stage 3 can take its SURPLUS SHED arm (0x1400f09eb) — the only path in
    // that function haulers never reach on their own, because they stock the market
    // TO its target and stop. No-op unless BANISHED_MARKET_OVERSTOCK is set
    // (src/diag/market_overstock_harness.cpp).
    MarketOverstock_Tick(s_frameNum);

    // THE VIRTUAL PLAYER: clicks workerCountUp (the retail first-seat path) on every
    // under-capacity workplace. Automatic for unattended runs only; replaces the old
    // BANISHED_PASTURE_HERDERS / BANISHED_BUILDER_AUTOPOOL cascade-bypass stand-ins
    // (src/diag/panel_staffing_driver.cpp).
    PanelStaffingDriver_Tick(m_toolManager, s_frameNum);
    // Hold one building panel open so its layout can be captured (BANISHED_PANEL_HOLD).
    PanelHoldHarness_Tick(m_toolManager, s_frameNum);

    // Player stand-in for the town-hall Builder spinner. No-op unless BANISHED_BUILDERS.
    BuilderSeat_Tick(s_frameNum);
    // Non-retail clock knobs for the time-gated achievements: advance the year
    // counter stat 26 reads. No-op unless BANISHED_ACH_YEAR_BOOST is set
    // (src/diag/ach_clock.cpp).
    AchClock_Tick(s_frameNum);

    // THE VIRTUAL MERCHANT CUSTOMER: with a merchant docked, fill the sell rows and
    // press Trade through the ported checkout. No-op unless BANISHED_TRADE_SELL is
    // set (src/diag/trade_buy_harness.cpp).
    TradeSellHarness_Tick(m_toolManager, s_frameNum);

    // Finish every structure still under construction, the way the stress placer
    // finishes its own. No-op unless BANISHED_FINISH_BUILDS is set
    // (src/diag/finish_builds_harness.cpp).
    FinishBuildsHarness_Tick(s_frameNum);

    // Periodic OrchardComponent state dump + PlantOrchard/HarvestOrchard work
    // counters. No-op unless BANISHED_ORCHARD_DIAG is set (src/diag/orchard_diag.cpp).
    OrchardDiag_Tick(s_frameNum);

    // Periodic census of ALL SIX food-production classes (state, staffing, fishing
    // rate, hunt progress) plus town-storage totals by resource type — the number
    // that actually answers "does this building feed the town". No-op unless
    // BANISHED_FOOD_DIAG is set (src/diag/food_production_diag.cpp).
    FoodDiag_Tick(s_frameNum);
    FoodStockCensus_Tick(s_frameNum);

    // Switch a demo crop field on (and optionally fire its plant command) so states
    // 9/10/11 are reachable without the drag tool. No-op unless
    // BANISHED_CROPFIELD_FORCE_START is set (src/diag/cropfield_force_start.cpp).
    CropFieldForceStart_Tick(s_frameNum);

    // One-shot: force an untyped demo orchard's master switch on + assign a real
    // tree type via the live combo-select path, so the state-2->state-9 transition
    // has something to prove. No-op unless BANISHED_ORCHARD_FORCE_START is set
    // (src/diag/orchard_force_start.cpp).
    OrchardForceStart_Tick(m_toolManager, s_frameNum);

    // Seat the first worker at each orchard — the one player click the employment
    // allocator structurally cannot make (it gates on roster != 0). No-op unless
    // BANISHED_ORCHARD_WORKERS is set (src/diag/orchard_workers_harness.cpp).
    OrchardWorkersHarness_Tick(s_frameNum);

    // Seat the first worker at each Fishing Dock / Hunter Lodge / Gatherer's Hut /
    // Crop Field — the one player click those buildings have no tool for yet. The
    // employment allocator gates on a NON-ZERO roster, so it can rebalance but never
    // seat worker #1. No-op unless BANISHED_FOOD_WORKERS is set
    // (src/diag/food_workers_harness.cpp).
    FoodWorkers_Tick(s_frameNum);

    // Fast-forward tree maturity, then fruit ripeness, so the harvest half of the orchard
    // state machine is reachable in one run rather than a multi-hour soak. No-op unless
    // BANISHED_ORCHARD_GROW is set (src/diag/orchard_growth_harness.cpp). It advances two
    // simulation values only — every gate downstream of them still has to pass.
    OrchardGrowthHarness_Tick(s_frameNum);

    // Click the panel's CUT button (command id 3) on the player's behalf, so state 20 can
    // be exercised without a human. No-op unless BANISHED_ORCHARD_CUT is set
    // (src/diag/orchard_cut_harness.cpp). Destructive to the orchard it fires on.
    OrchardCutHarness_Tick(s_frameNum);

    // One-shot at frame 120. Despite the name this is NOT purely a diagnostic: it
    // also publishes nine shared element type-id globals that other dialogs read
    // (toolbardialog / optionsdialog / saveloaddialog / spritecaptureui), so it must
    // stay ungated — see the note above TownHallSA_Diag in townhallui.cpp. The
    // reporting half is gated on BANISHED_TOWNHALL_DIAG.
    TownHallSA_Diag(s_frameNum);


    // Apply the deferred founding-house "occupied" visuals now that the scene is live
    // (the Scene mesh-visual can't run during the build-time founding pass — it
    // deadlocks; see g_wpcRosterOnly). One-shot, drains its queue on an early frame.
    StartTown_ApplyPendingOccupiedVisuals(s_frameNum);

    // (StatusIcon "no workers" warnings are now raised by the genuine event
    // WorkPlaceComponent_SetAccepting at workplace activation and cleared by
    // WorkplaceComponent_Attach — the per-frame occupancy-poll drain was removed.
    // See src/game/Component/statusiconcomponent.cpp.)

    // (StorageDisplayComponent pile rendering is now driven natively by EntityList::Update's
    // 5-bucket +0x48 tick — the explicit per-frame drain was removed once the spurious
    // bit-0x400 cull on start-town buildings was fixed. See storagedisplaycomponent.cpp.)

    TRACE("[GameUpdateLoop::Update] ENTER frame=%d (self=%p, time=%p, gameTime=%lld)\n",
          s_frameNum, (void*)this, (void*)time, (long long)m_gameTime);

    // Step 1: Check if UI pauses game
    TRACE("[GameUpdateLoop::Update] checking PausesGame...\n");
    bool paused = m_userInterface->PausesGame();
    TRACE("[GameUpdateLoop::Update] PausesGame = %d\n", (int)paused);

    if (paused) {
        TRACE("[GameUpdateLoop::Update] game paused by UI, skipping simulation loop\n");
        goto post_simulation;
    }

    {
        int speedMultiplier = 0;
        int iteration = 0;

        // Step 2: Read speed multiplier from the tool manager
        // +0x30 = paused flag, +0x34 = speed multiplier
        PlayerToolManager* toolManager = m_toolManager;
        bool entityUIPaused = toolManager->paused;
        if (!entityUIPaused) {
            speedMultiplier = toolManager->field_0x34;
        }
        TRACE("[GameUpdateLoop::Update] entityUIPaused=%d, speedMultiplier=%d\n",
               (int)entityUIPaused, speedMultiplier);

        if (speedMultiplier > 0) {
            // Accumulate game time
            m_gameTime += time->_ticks;
            TRACE("[GameUpdateLoop::Update] gameTime updated to %lld\n", (long long)m_gameTime);
        }

        // Step 3: Inner simulation loop
        if (speedMultiplier > 0) {
            do {
                TRACE("[GameUpdateLoop::Update] === sim iteration %d/%d ===\n", iteration + 1, speedMultiplier);

                // 3a. Region-pair distance cache staleness sweep
                RegionPairCache* regionPairCache = m_game->regionPairCache;
                TRACE("[GameUpdateLoop::Update] calling RegionPairCache::Tick (cache=%p)...\n", (void*)regionPairCache);
                { PhaseTimer pt(kPhPairCache);
                regionPairCache->Expire(&time->_ticks);   // 0x1400a5d10
                }
                TRACE("[GameUpdateLoop::Update] RegionPairCache::Tick done\n");

                // 3b. AnimationManager::Update
                bool rootOnly = (iteration < speedMultiplier - 1);
                Ext::RenderCulling::SetIteration(iteration, speedMultiplier);   // [PerfX] ANIM_BATCH needs the iteration index
                ::Graphics::MeshInstance::_rootOnlyUpdates = rootOnly;
                TRACE("[GameUpdateLoop::Update] calling AnimationManager::Update (rootOnly=%d)...\n", (int)rootOnly);
                { PhaseTimer pt(kPhAnimation);
                m_animationManager->Update(*time, rootOnly);
                }
                TRACE("[GameUpdateLoop::Update] AnimationManager::Update done\n");

                // 3c. EntityList::Update (component ticks)
                // Uses custom reimpl — original fn_EntityList_Update (0x1400c4090)
                // hangs due to a spinlock issue in the hooked environment.
                TRACE("[GameUpdateLoop::Update] calling EntityList::Update (entityList=%p)...\n", m_entityList);
                { PhaseTimer pt(kPhEntityList);
                // EXT SEAM (NOT RETAIL, docs/extensions/handins/WALKBATCH.md, WALKGRID.md): ext.walk_batch (class A) /
                // ext.walk_batch_grid (class B) -- an intermediate iteration while either row is on walks with the
                // batched character tick (src/ext/walk_batch/walk_batch.h); otherwise retail's
                // EntityList::Update.
                const int walk = Ext::WalkBatch::Select(rootOnly);
                if (walk == Ext::WalkBatch::kWalkNoPlace) m_entityList->UpdateWalkBatchGrid(time);
                else if (walk == Ext::WalkBatch::kWalkPlace) m_entityList->UpdateWalkBatch(time);
                else m_entityList->Update(time);
                }
                TRACE("[GameUpdateLoop::Update] EntityList::Update done\n");

                // 3d. Day counter increment
                {
                    Calendar* dayCounter = m_dayCounter;
                    dayCounter->yearTime._ticks += time->_ticks;
                    if (dayCounter->yearTime._ticks >= dayCounter->yearLength._ticks) {
                        dayCounter->year = dayCounter->year + 1;
                        dayCounter->yearTime._ticks -= dayCounter->yearLength._ticks;
                        TRACE("[GameUpdateLoop::Update] day counter incremented to %d\n", dayCounter->year);
                    }
                }

                // 3e. Weather::ResetTime
                TRACE("[GameUpdateLoop::Update] calling Weather_ResetTime...\n");
                { PhaseTimer pt(kPhWeather);
                m_weather->Update(time);   // 0x1400944e0
                }
                TRACE("[GameUpdateLoop::Update] Weather_ResetTime done\n");

                // 3f. CitizenBehavior::Update / Map::Update
                TRACE("[GameUpdateLoop::Update] calling CitizenList::Update (citizenList=%p)...\n", m_citizenList);
                { PhaseTimer pt(kPhCitizenList);
                m_citizenList->Update(time);   // 0x14008c2b0 (the slot's const Time*)
                }
                TRACE("[GameUpdateLoop::Update] CitizenList::Update done\n");

                // 3g. Tutorial frame/time increment
                {
                    WorkScheduler* scheduler = m_tutorial;
                    scheduler->stepCount = scheduler->stepCount + 1;
                    scheduler->time._ticks = scheduler->time._ticks + time->_ticks;
                    TRACE("[GameUpdateLoop::Update] tutorial frame=%d, tutorialTime=%lld\n",
                           (int)scheduler->stepCount, (long long)scheduler->time._ticks);
                }

                // 3h. Global frame index increment
                ::Graphics::ParticleInstance::_frameIndex =
                    ::Graphics::ParticleInstance::_frameIndex + 1;

                iteration++;
                if (simProf) g_profSimIters++;   // [SIMPROF]
            } while (iteration < speedMultiplier);
        }

        // Step 4: Achievement check
        TRACE("[GameUpdateLoop::Update] checking achievements (scenarioInstance=%p, completed=%d)...\n",
               m_scenarioInstance.Get(), (int)m_scenarioCompleted);
        if (m_scenarioInstance.Get() != nullptr && m_scenarioCompleted == 0) {
            TRACE("[GameUpdateLoop::Update] calling AchievementInstance::Update...\n");
            m_scenarioInstance->Update(*time);
            TRACE("[GameUpdateLoop::Update] AchievementInstance::Update done\n");

            bool isMet = m_scenarioInstance->IsMet();
            TRACE("[GameUpdateLoop::Update] AchievementInstance::IsMet = %d\n", (int)isMet);

            if (!isMet) {
                bool hasFailed = m_scenarioInstance->HasFailed();
                TRACE("[GameUpdateLoop::Update] AchievementInstance::HasFailed = %d\n", (int)hasFailed);
                if (hasFailed) {
                    TRACE("[GameUpdateLoop::Update] calling OnScenarioComplete (failed)...\n");
                    OnScenarioComplete();
                    TRACE("[GameUpdateLoop::Update] OnScenarioComplete done\n");
                }
            } else {
                TRACE("[GameUpdateLoop::Update] calling OnScenarioComplete (met)...\n");
                OnScenarioComplete();
                TRACE("[GameUpdateLoop::Update] OnScenarioComplete done\n");
            }
        }

        // Step 4b: AchievementManager::Update with scaled time
        TRACE("[GameUpdateLoop::Update] calling AchievementManager::Update...\n");
        {
            float seconds = time->AsSeconds();
            System::Time scaledTime;
            // AchClock_TimeScale() is 1.0 unless BANISHED_ACH_TIME_SCALE is set, so
            // this is retail's `seconds * speedMultiplier` unchanged by default. This
            // one Time is what every TimeGoalInstance accumulates, so scaling it here
            // advances all of them at once (src/diag/ach_clock.cpp).
            RtSystem_Time_Ctor(&scaledTime,
                               seconds * (float)speedMultiplier * AchClock_TimeScale());
            // Achievement::AchievementManager::_singleton (retail: **import slot
            // 0x1401514f0). The null check on the manager is the M5 first-slice guard.
            if (::Achievement::AchievementManager::_singleton) {
                ::Achievement::AchievementManager* achievementMgr = ::Achievement::AchievementManager::_singleton;

                // BANISHED_ACH_IGNORE_MODS=1 -- NON-RETAIL TEST FIXTURE.
                //
                // AchievementManager::Update's first act (Runtime 0x18009e6c6) is
                //     if (*(int*)(IO::FileSystem::_singleton + 0x38) > 0) return;
                // i.e. AreUserPackFilesEnabled(). Retail disables achievements outright
                // whenever any .pkm is mounted, silently. That gate lives inside the
                // Runtime DLL, so it cannot be flagged off at its source -- instead we
                // zero the mod count for the duration of THIS ONE CALL and put it back
                // immediately, so no other reader ever observes the lie.
                //
                // What this buys: the goal wiring can be exercised in a modded session
                // (e.g. with DebugMenu.pkm mounted). What it does NOT buy: evidence that
                // a retail player could earn the achievement -- retail would have
                // refused to evaluate at all. An award obtained under this flag is a
                // test result, not a legitimate unlock, and it still writes a real
                // HKCU/.../Ach<N> value. Belongs with the other declared deviations in
                // docs/history/waves/achievements_wave.md section 7.
                int32_t* modGate = nullptr;
                int32_t  savedGate = 0;
                static int s_ignoreMods = -1;
                if (s_ignoreMods < 0) {
                    const char* e = Opt().achIgnoreMods;
                    s_ignoreMods = (e && e[0] && e[0] != '0') ? 1 : 0;
                }
                if (s_ignoreMods == 1) {
                    if (::IO::FileSystem::_singleton) {
                        modGate = &::IO::FileSystem::_singleton->_userPackFilenames.size;   // +0x38
                        savedGate = *modGate;
                        if (savedGate > 0) {
                            *modGate = 0;
                            static bool s_said = false;
                            if (!s_said) {
                                s_said = true;
                                fprintf(stderr, "[Ach] BANISHED_ACH_IGNORE_MODS: masking the "
                                        "user-pack gate (FileSystem+0x38 was %d) for the "
                                        "AchievementManager::Update call ONLY. Retail would "
                                        "evaluate NOTHING here -- any award this run is a test "
                                        "result, not a legitimate unlock.\n", savedGate);
                                fflush(stderr);
                            }
                        }
                    }
                }

                // With the gate masked, Update runs on to its instance list at mgr+0x18
                // and derefs it unguarded -- faithful, because retail always has one by
                // then (Reset() builds it at new-game, Persist restores it on load).
                // Measured 2026-09-05: a LOADED town could reach here with mgr+0x18
                // null, and Update then faulted READ of 0 at Runtime+0x9E6F4.
                // ROOT-CAUSED 2026-09-06 (Wave 4 lane H) and FIXED AT THE CAUSE: the
                // Load path never ran the container init, so
                // AchievementManager::SetAchievements never ran for a loaded game.
                // Retail does it once at app init (GameContainer_Init 0x140099db0),
                // which the tree runs from InnerLoaderLoop::Load since the D.4 gap hunt
                // (the per-dialog stand-ins it replaced are deleted, row 1312).
                // The old diagnosis on this line -- "Reset()/Persist has not populated
                // it, a load-path defect" -- named the wrong MECHANISM: it was a
                // missing init, not a Persist defect.
                // The report below is now a FIXTURE THAT SHOULD NEVER FIRE. If it does,
                // some OTHER path into a game is missing that same call.
                const void* instList = achievementMgr->_instances._data;   // mgr+0x18
                if (modGate && !instList) {
                    static bool s_warned = false;
                    if (!s_warned) {
                        s_warned = true;
                        fprintf(stderr, "[Ach] BANISHED_ACH_IGNORE_MODS: manager has NO "
                                "INSTANCE LIST (mgr+0x18 null) -- nothing can be evaluated "
                                "or awarded this run. GameContainer_Init runs at boot "
                                "(InnerLoaderLoop::Load) since 2026-10-11, so this "
                                "should be unreachable -- some OTHER path into a game "
                                "is missing that call. Skipping the Update call.\n");
                        fflush(stderr);
                    }
                } else {
                    achievementMgr->Update(scaledTime);
                }

                if (modGate) *modGate = savedGate;   // restore before anything else reads it
            }
        }
        TRACE("[GameUpdateLoop::Update] AchievementManager::Update done\n");

        // Step 5: EntityList post-update cleanup
        TRACE("[GameUpdateLoop::Update] calling EntityList::PostUpdate...\n");
        m_entityList->PostUpdate();
        TRACE("[GameUpdateLoop::Update] EntityList::PostUpdate done\n");

        // Step 6: Camera transform update
        TRACE("[GameUpdateLoop::Update] updating camera transform...\n");
        {
            // Math::Transform3 is 0x30 = 48 bytes (static_assert in
            // src/engine/math/Math.h:118), and retail's AsTransform3
            // (0x18001fcd0) writes twelve MOVSS stores covering ret+0x00..0x2f.
            // This buffer was declared [44] until Wave 9, so every frame wrote
            // FOUR BYTES PAST THE END of a stack buffer. Line 2281 of this same
            // file already had it right at [48]; this was a plain typo, not a
            // layout disagreement. Found by lane P while diagnosing an
            // AsTransform3 divergence that turned out to be an oracle defect --
            // the overflow is older and unrelated to that port.
            PlayerController* playerCtrl = m_playerController;
            // EXT SEAM (NOT RETAIL, docs/extensions/handins/EYECAM.md): while
            // ext.eye_camera follows a citizen, the eye transform replaces the
            // camera write (retail's CalcTransform still runs inside Apply).
            if (Ext::Ext().eye_camera && Ext::EyeCamera::Active()) {
                Ext::EyeCamera::Apply(*playerCtrl);
            } else {
                Math::Transform transform = playerCtrl->CalcTransform();   // 0x1400983f0
                // AsTransform3 on the result, then SetCameraToWorld
                Math::Transform3 t3 = transform.AsTransform3();
                // `playerCtrl` is `m_playerController` (+0xE0); its +0x34 is the
                // Graphics::Camera member. There was no readiness guard here.
                playerCtrl->camera.SetCameraToWorld(t3);
            }
        }
        TRACE("[GameUpdateLoop::Update] camera transform done\n");
    }

post_simulation:
    // [SIMPROF] everything from here to EXIT is the once-per-render-frame work
    // (ambient/terrain/UI/tools/camera/audio) — NOT multiplied by game speed.
    { PhaseTimer ptPost(kPhPostSim);

    // Step 7: MusicPlayer::Update - exe 0x1400a40e0, not yet ported. [GUARD-M5]
    // NOT an ambient-sound update despite the fn_ name: it picks the next music
    // track by month + town mood, so while this stays null background music
    // never plays. See engine/types/GameUpdateLoop.h +0x80.
    TRACE("[GameUpdateLoop::Update] calling MusicPlayer::Update...\n");
    m_musicPlayer->Update();   // 0x1400a40e0
    TRACE("[GameUpdateLoop::Update] MusicPlayer::Update done\n");

    // Step 8: World geometry vtable call (terrain update)
    TRACE("[GameUpdateLoop::Update] calling WorldGeometry::Update...\n");
    {
        // Caller batch CB6: slot 1 (vtable +0x08) of the real class. The receiver is
        // PROVED: Load's step 23 builds it with WorldGeometry's ctor (and Load is
        // m_worldGeometry's only writer). A virtual call, as retail's is.
        m_worldGeometry->Update(*time);
    }
    TRACE("[GameUpdateLoop::Update] WorldGeometry::Update done\n");

    // Step 9: UI::UserInterface::Update
    // fn_UI_Update returns the cursor id; engine_funcs.h now declares that directly
    // (the export is ...QEAAH...). The local cast below is now redundant.
    TRACE("[GameUpdateLoop::Update] calling UI::UserInterface::Update...\n");
    // Wave 10D lane MOUSE: the ONLY seam at which a synthetic cursor can be
    // placed -- after this frame's input poll, before the UI decodes it. Both
    // calls are a cached-getenv no-op unless BANISHED_MOUSE_INJECT is set.
    MouseInject::PreUpdate(m_userInterface);
    int uiCursor = m_userInterface->Update(*time);
    MouseInject::PostUpdate(m_userInterface);
    TRACE("[GameUpdateLoop::Update] UI::UserInterface::Update done (cursor=%d)\n", uiCursor);

    // Step 10: the tool manager's Update (retail 0x1400965a0-0x1400965ed): the
    // default cursor is 1; while the UI does not pause the game the manager's
    // Update gives it (its input flag: no dialog holds the mouse); a UI that
    // holds the mouse or pauses the game gives the UI's cursor instead.
    int cursorIndex = 1;
    if (!m_userInterface->PausesGame())
        cursorIndex = m_toolManager->Update(*time, m_userInterface->_mouseCaptured == 0);   // 0x1401026a0
    if (m_userInterface->_mouseCaptured != 0 || m_userInterface->PausesGame())
        cursorIndex = uiCursor;

    // Step 11: Set cursor
    TRACE("[GameUpdateLoop::Update] setting cursor (index=%d)...\n", cursorIndex);
    {
        GameContainer* keyMapMgr = g_gameContainer;  // [GUARD-M5]
        if (keyMapMgr) {
            // The six cursors +0x28..+0x50 are indexed as one array (retail:
            // container + 0x28 + index * 8).
            const Object::ExternalPtr<UI::Cursor>* cursors = &keyMapMgr->cursorArrow;
            m_userInterface->SetCursor(cursors[cursorIndex]);
        }
    }
    TRACE("[GameUpdateLoop::Update] cursor set\n");

    // Step 12: Audio::Scene::Update
    TRACE("[GameUpdateLoop::Update] calling Audio::Scene::Update...\n");
    {
        PlayerController* playerCtrl = m_playerController;
        // The listener (retail 0x1400965c0..0x1400965e4): x, y = the controller's
        // orbit target (+0x1c / +0x20), z = the camera-to-world TRANSLATION's z
        // (the Transform3 at +0x98 copied to a local, its +0x2c read back) -- the
        // camera's height over the point it looks at. Until Phase D.4's close this
        // read the Transform3's first ROW (+0x98..+0xa0, the X axis, a unit vector):
        // a listener that never moved and sat at ground level, so no 3D sound faded
        // with the camera and the lifted wind emitter was faint (row 1286).
        const Graphics::Camera* camera = &playerCtrl->camera;
        Math::Vector3 listenerPos(playerCtrl->target.x, playerCtrl->target.y,
                                  camera->_cameraToWorld.translation.z);

        // Get speed multiplier for audio time scaling
        int speedMul = 0;
        PlayerToolManager* toolManager = m_toolManager;
        if (!toolManager->paused) {
            speedMul = toolManager->field_0x34;
        }

        {
            float seconds = time->AsSeconds();
            System::Time scaledTime;
            RtSystem_Time_Ctor(&scaledTime,
                               seconds * (float)speedMul * kRdata_14017219c);
            float windFactor = CalcWindFactor(&scaledTime);   // 0x1401027c0
            // Sanity-check the camera-position sub-offset (playerCtrl+0x34+0x88) once:
            // it was derived from stack arithmetic in the retail disasm, not from a
            // struct definition. CLAUDE.md documents the default camera dist as 40, so
            // a plausible reading is tens of units — not 0, not 1e38.
            {
                static int s_once = 0;
                if (s_once < 3) {
                    s_once++;
                    std::fprintf(stderr, "[AudioScene] windFactor(camera dist)=%.3f\n", windFactor);
                    std::fflush(stderr);
                }
            }
            // windFactor is the FLOAT the mangled name's trailing `M` says; an
            // earlier uint32_t cache typedef converted it (40.5f -> 40).
            System::Time audioNow;    audioNow._ticks    = time->_ticks;
            System::Time audioScaled; audioScaled._ticks = scaledTime._ticks;
            m_audioScene->Update(
                audioNow, audioScaled, *m_random,
                listenerPos, windFactor);
        }
    }
    TRACE("[GameUpdateLoop::Update] Audio::Scene::Update done\n");

    // Step 13: Quicksave/quickload key checks
    // Original asm at 0x1400966c1: triple deref from slot, dispatch call at 0x140151758
    // Match proven working pattern from dragtool/pathtool — no null guards (original has none).
    {
        const Input::KeyMap* keyMap = Input::Configuration::_singleton->_keyMap;

        if (keyMap->Pressed(0, 0x21)) {
            m_quickSavePressed = 1;
        }

        // HARNESS, not retail: BANISHED_SCREENSHOT_AT=<frame> raises the same
        // request bit the 0x21 keypress raises, once, at that frame. Retail's
        // GameUpdateLoop::TakeScreenshot (0x140095da0, ported in
        // src/game/Game/w2_takescreenshot.cpp) then runs from Draw and writes a
        // .bmp, exactly as a real keypress would.
        //
        // WHY THIS EXISTS: the diff oracle, [LodCensus] and [DrawCensus] between
        // them cover the scene walk and the draw submission, and NONE of them can
        // answer "is it actually on the screen" -- they all stop short of the GPU.
        // Wave 6 needed that answer for a report of missing terrain resources and
        // had no way to get it unattended. Combined with BANISHED_SEED and
        // BANISHED_CAMERA_PIN (which make framing reproducible run to run) this
        // turns a visual regression into a file two runs can diff.
        {
            static const char* const ssEnv = Opt().screenshotAt;
            static const int32_t ssAt = ssEnv ? std::atol(ssEnv) : -1;
            static int32_t ssFrame = 0;
            static bool ssFired = false;
            ++ssFrame;
            if (ssAt >= 0 && !ssFired && ssFrame >= ssAt) {
                ssFired = true;
                m_quickSavePressed = 1;
                std::fprintf(stderr, "[Screenshot] BANISHED_SCREENSHOT_AT=%d reached at frame %d"
                                     " -- raising the TakeScreenshot request bit\n", ssAt, ssFrame);
            }
            // BANISHED_SHOWROOM (src/diag/showroom.h): the same request bit, once per
            // building, after the camera pin has framed it.
            if (Showroom_Tick()) {
                m_quickSavePressed = 1;
            }
        }

        if (keyMap->Pressed(0, 0x22)) {
            m_quickLoadPressed = 1;
        }
        TRACE("[GameUpdateLoop::Update] quicksave/quickload checked (save=%d, load=%d)\n",
              m_quickSavePressed, m_quickLoadPressed);
    }

    // Step 14: SaveLoad::Update if active
    if (m_saveLoad != nullptr) {  // [GUARD-M5]
        TRACE("[GameUpdateLoop::Update] calling SaveLoad::Update...\n");
        m_saveLoad->Update(*time);
        TRACE("[GameUpdateLoop::Update] SaveLoad::Update done\n");
    }

    // Step 14b: Calculate sun direction (normalized)
    // Original at 0x140096717-0x14009676f computes direction from PlayerController
    // offsets (+0x98..+0xac), normalizes via Math::Vector3::Normalize, but the
    // normalized result goes to a stack temporary and is never stored to SunLight
    // or passed to Scene::Clean. Verified in disassembly: no MOV between the
    // Normalize CALL and the Scene::Clean CALL. This appears to be dead code in
    // the original binary — possibly a removed/vestigial lighting update.
    {
        const Math::Transform3& view = m_playerController->camera._cameraToWorld;
        float dirX = view.axisY.x + view.axisX.x + 0.0f;   // +0xa4 + +0x98
        float dirY = view.axisY.y + view.axisX.y + 0.0f;   // +0xa8 + +0x9c
        float dirZ = (view.axisY.z + view.axisX.z) - kRdata_14017214c;   // +0xac + +0xa0
        float dir[3] = { dirX, dirY, dirZ };
        float len = sqrtf(dir[0]*dir[0] + dir[1]*dir[1] + dir[2]*dir[2]);
        if (len > 0.0f) { dir[0] /= len; dir[1] /= len; dir[2] /= len; }
        // Result intentionally unused — matches original binary behavior.
        (void)dir;
    }

    // [diag] BANISHED_SCENE_WATCH — validate the node array BEFORE Clean walks it,
    // so a recycled node is reported the frame it appears rather than whenever
    // Clean's vtable+0x40 dispatch happens to reach it ("EXEC of <heap address>").
    SceneWatch_Check(m_graphicsScene);
    // [diag] BANISHED_SCENE_CENSUS — membership + position churn (the "models keep
    // popping in and out" report). Same place as the integrity check: before Clean,
    // after every component has had its per-frame say.
    SceneWatch_Census(m_graphicsScene);
    if (GameUpdateLoop::_singleton) CitizenVitals_Tick(GameUpdateLoop::_singleton);

    // Step 15: Graphics::Scene::Clean
    TRACE("[GameUpdateLoop::Update] calling Graphics::Scene::Clean...\n");
    m_graphicsScene->Clean();
    TRACE("[GameUpdateLoop::Update] Graphics::Scene::Clean done\n");

    // Step 16: Image capture check
    {
        Minimap* imageCapture = m_imageCapture;
        if (imageCapture->terrainImage != nullptr && imageCapture->overlayDirty != 0) {
            TRACE("[GameUpdateLoop::Update] processing image capture...\n");
            ::Video::Device::_singleton
                ->Lock();
            imageCapture->overlayImage
                ->CopyToTexture(imageCapture->overlayPixels._data,
                                imageCapture->size,
                                imageCapture->size);
            ::Video::Device::_singleton
                ->Unlock();
            imageCapture->overlayDirty = 0;
            TRACE("[GameUpdateLoop::Update] image capture done\n");
        }
    }

    // Step 17: Next loop transition check
    if (m_nextLoop != nullptr) {
        // Check if fullscreen fade is done (UI+0xf0 == 0.0f)
        float fadeProgress = m_userInterface->_fadeVelocity;
        if (fadeProgress == 0.0f) {
            TRACE("[GameUpdateLoop::Update] transitioning to next loop %p\n", m_nextLoop);
            ::Engine::MainLoop::_singleton->SetNextLoop(
                m_nextLoop);
        }
    }

    }  // [SIMPROF] end kPhPostSim

    // [SIMPROF] close the whole-frame timer before reporting so TOTAL includes
    // this frame, then emit/reset on the reporting interval.
    if (simProf && g_qpcFreq) {
        g_phSec[kPhWholeUpdate] += (double)(QpcNow() - frameT0) / (double)g_qpcFreq;
        g_phCalls[kPhWholeUpdate]++;
        int32_t entCount = -1;
        if (m_entityList)
            entCount = m_entityList->buckets[1]._size;   // +0x30
        int spd = 0;
        if (m_toolManager && !m_toolManager->paused)
            spd = m_toolManager->field_0x34;
        SimProfReport(spd, entCount);
    }
    // [STRESS] close the Update bracket; the harness's own periodic work (CSV row,
    // staffing pass) runs inside this call, AFTER its timer has stopped.
    StressHarness_UpdateEnd(this, s_frameNum);

    TRACE("[GameUpdateLoop::Update] EXIT\n");
    return true;
}


// ============================================================
// GameUpdateLoop::Draw (0x140096010) — Render one interpolated frame
// ============================================================

void GameUpdateLoop::Draw(float alpha) {
    // NOT RETAIL (block LOADSTAMP): [LoadTime] lines around the first Draw of each loop.
    static const GameUpdateLoop* s_stampedDraw = nullptr;
    const bool stampDraw = (s_stampedDraw != this);
    if (stampDraw) { s_stampedDraw = this; LoadTime::Stamp("game.draw.first"); }
    // Stage C entry guard (TEMPORARY): see GameUpdateLoop_Update — must run
    // before EnsureInit (which null-derefs an unregistered dispatch slot in
    // standalone). m_game is null until GameUpdateLoop::Load builds the world;
    // the draw path dereferences the unbuilt world (UI, renderer, scenes).
    if (m_game == nullptr) {
        static bool logged = false;
        if (!logged) {
            logged = true;
            fprintf(stderr, "[GameUpdateLoop::Draw] world not built "
                    "(m_game==null) — Stage C cliff, skipping draw\n");
            fflush(stderr);
        }
        return;
    }

    // NOT RETAIL: the in-game frame cap (src/app/frame_limiter.h). A no-op unless the settings cap
    // (ext.fps_cap, the phone's first run sets it to 60) or BANISHED_FPS_CAP asks for one; it sleeps
    // before the frame's draw work, and MainLoop::Frame's accumulator still runs every sim step owed.
    FrameLimiter::TickInGame();

    StressHarness_DrawBegin();   // [STRESS] render-frame period + Draw bracket

    // Tick the RenderDoc capture state machine once per rendered frame. The
    // engine's Present happens after this Draw returns (MainLoop::Draw EndFrame),
    // so a StartFrameCapture here brackets a full frame of GPU work.
    RenderDocIntegration::OnFrame();
    // One-shot in-game capture: BANISHED_CAPTURE_INGAME=N arms a RenderDoc
    // frame capture N in-game frames after the first drawn frame (default 120,
    // ~enough for the camera to settle on the start town). Requires the exe to
    // be launched under RenderDoc (renderdoc.dll injected). Used to inspect the
    // terrain draw's PS bindings + shadow cbuffer with shadows ON.
    {
        static int s_capArm = -2;      // -2=unread, -1=disabled, >=0 = countdown to arm
        if (s_capArm == -2) {
            const char* e = Opt().captureIngame;
            s_capArm = (e && e[0]) ? (atoi(e) > 0 ? atoi(e) : 120) : -1;
        }
        if (s_capArm >= 0) {
            if (s_capArm == 0) {
                RenderDocIntegration::RequestCaptureInFrames(0);
            }
            --s_capArm;
        }
    }

    TRACE("[GameUpdateLoop::Draw] ENTER (alpha=%.3f)\n", alpha);

    // Step 1: Check pause — if paused, force alpha to 1.0 (no interpolation)
    bool paused = m_userInterface->PausesGame();
    bool entityUIPaused = m_toolManager->paused;
    // EXT SEAM (NOT RETAIL, docs/extensions/handins/HITCH_SAVESNAP.md): ext.background_save's frozen sim holds the last tick's pose too.
    if (paused || entityUIPaused || Ext::BackgroundSave::Busy()) {
        alpha = kRdata_14017214c; // 1.0f
    }

    // Step 2: Set time on video context
    TRACE("[GameUpdateLoop::Draw] setting video context time...\n");
    {
        ::Engine::MainLoop* engine = ::Engine::MainLoop::_singleton;
        Video::Context* videoCtx = engine->_context;
        System::Time gameTime;        gameTime._ticks = m_gameTime;
        System::Time engineFrameTime; engineFrameTime._ticks = engine->_frameUpdateTime;

        float gameSeconds = gameTime.AsSeconds();
        float frameSeconds = engineFrameTime.AsSeconds();
        videoCtx->SetTime(gameSeconds + frameSeconds * alpha);
    }
    TRACE("[GameUpdateLoop::Draw] video context time set\n");

    // Step 3: Image capture terrain update check
    TRACE("[GameUpdateLoop::Draw] checking image capture terrain...\n");
    {
        Minimap* imageCapture = m_imageCapture;
        Video::Context* videoCtx = ::Engine::MainLoop::_singleton->_context;
        // ImageCapture::DrawCheck (exe FUN_1400a5140) is our clean port
        // Minimap::PaintTerrain. It fires
        // once — when the minimap's terrain GPU buffer (imageCapture+0x08) is still
        // null — to render the terrain top-down into the minimap pixel arrays.
        if (imageCapture->terrainImage == nullptr) {
            // Remove and re-add global objects for terrain update
            GameUpdateLoop* gameWorld = GameUpdateLoop::_singleton;
            Graphics::Scene* gs = gameWorld->m_graphicsScene;
            gs->RemoveGlobalObject(m_sunLight);
            gs->RemoveGlobalObject(m_worldGeometry);

            imageCapture->PaintTerrain(*videoCtx, m_worldGeometry, m_sunLight);   // 0x1400a5140

            gs->AddGlobalObject(m_sunLight);
            gs->AddGlobalObject(m_worldGeometry);
        }
    }
    TRACE("[GameUpdateLoop::Draw] image capture terrain done\n");

    // Step 4: Camera update
    TRACE("[GameUpdateLoop::Draw] updating player camera...\n");
    // EXT SEAM (NOT RETAIL, docs/extensions/handins/EYECAM.md): ext.eye_camera
    // builds the eye at the renderer's alpha and the near plane 0.1 per frame.
    if (Ext::Ext().eye_camera && Ext::EyeCamera::Active())
        Ext::EyeCamera::ApplyFrame(*m_playerController, alpha, kRdata_1401721dc, kRdata_140172240);
    else
        m_playerController->UpdateCamera(kRdata_1401721dc, kRdata_140172174, kRdata_140172240);   // 0x140098660
    TRACE("[GameUpdateLoop::Draw] player camera done\n");

    // Step 5: Weather draw update
    TRACE("[GameUpdateLoop::Draw] calling Weather::DrawUpdate...\n");
    {
        Video::Context* videoCtx = ::Engine::MainLoop::_singleton->_context;
        // 0x140096158: lighting, fog, sun and the four terrain weather floats.
        m_weather->DrawUpdate(*videoCtx);   // 0x1400938d0

        // [diag] BANISHED_FORCE_SNOW=<float> overrides the snow coverage retail
        // just pushed (the first of the four floats, both copies). Not retail.
        static int s_forceSnow = -1;
        static float s_snowVal = 0.0f;
        if (s_forceSnow < 0) {
            const char* e = Opt().forceSnow;
            s_forceSnow = (e && e[0]) ? 1 : 0;
            if (s_forceSnow) s_snowVal = (float)atof(e);
        }
        if (s_forceSnow) {
            videoCtx->_unknown03b8.x = s_snowVal;
            videoCtx->_pixelConstantStaging.pixel[4].x = s_snowVal;
        }
    }
    TRACE("[GameUpdateLoop::Draw] Weather::DrawUpdate done\n");

    // Step 6: Copy renderer shadow mode from engine.
    // renderer+0x3c is the shadow mode: Renderer::AddScene (Runtime 0x180105700) stores it
    // as the main draw group's +0xe8, and Renderer::Draw (0x180104bb0) reads it back into
    // renderer+0x3c before dispatching each global object's vtable+0x48 (DrawShadowMap) /
    // vtable+0x50 (Draw). SunLight::DrawShadowMap (0x180106bc0) is gated by
    // `if (renderer+0x3c != 0)`; SunLight::Draw (0x1801067b0, main pass) is NOT — it always
    // runs Context::SetLight + Renderer::DrawItemList to light and render the terrain.
    {
        uint32_t shadowMode = 0;
        // The shadow-mode source field. The ORIGINAL GameUpdateLoop::Draw (exe 0x140096010)
        // sets renderer+0x3c from *(*g_pVideoConfigSingleton + 0x30) — the shadow-QUALITY
        // setting (index, 0=off, >0=on), NOT engine+0x30 (g_pMainLoopSingleton ==
        // MainLoop, so engine+0x30 is garbage). Shadows are DEFAULT-ON: mirror the
        // original and read VideoConfig+0x30 so SunLight::DrawShadowMap engages (verified
        // crash-free; terrain + buildings cast/receive). The day cycle is driven by
        // WeatherSimulation::DrawUpdate above.
        if (::Video::Configuration::_singleton) {
            shadowMode = ::Video::Configuration::_singleton->shaderDetail;   // +0x30
        }
        m_renderer->_shaderDetail = shadowMode;
    }

    // [diag] BANISHED_LOD_CENSUS — replicate MeshInstance's distance-based detail
    // pick with the very camera this frame is about to draw with, and count the
    // flips. The scene census cleared every CULL input; the LOD pick reads a
    // different set (the union with the PREVIOUS world box, and the mesh's own
    // distance table), so it needs its own measurement.
    SceneWatch_LodCensus(m_graphicsScene,
                         &m_playerController->camera,   // the Graphics::Camera at +0x34
                         // The MAIN scene pass's variant index, measured not guessed:
                         // Renderer::Draw hands BeginDrawGroup the scene entry's +0xec,
                         // which is AddScene's param_6 and is a literal 0 there. (The
                         // obvious guess, renderer+0x3c, is the shader-DETAIL level that
                         // rides to Context+0xc; the shadow cascades use 1 and 2.)
                         0);

    // Step 7: Graphics::Renderer::Draw
    TRACE("[GameUpdateLoop::Draw] calling Renderer::Draw...\n");
    {
        Video::Context* videoCtx = ::Engine::MainLoop::_singleton->_context;
        // EXT SEAM (NOT RETAIL, docs/extensions/handins/AOCORE.md): ext.ambient_occlusion
        // arms Graphics::Renderer::_extAo for this one Draw (the game's main scene
        // only: the menu backdrop and the minimap never come through here).
        const bool aoArmed = Ext::AmbientOcclusion::Enabled() && Ext::AmbientOcclusion::Arm(*m_playerController);
        // EXT SEAM (NOT RETAIL, docs/extensions/handins/EYECAM.md §9): while
        // ext.eye_camera follows a citizen, the same Renderer::Draw with the
        // sun's shadow camera fitted to a view over the citizen (retail's fit
        // to the eye-level view misses it).
        if (Ext::Ext().eye_camera && Ext::EyeCamera::Active())
            Ext::EyeCamera::DrawScene(*m_renderer, *videoCtx, alpha, *m_graphicsScene,
                                 *m_playerController);
        else
            m_renderer->Draw(
                *videoCtx, alpha,
                *m_graphicsScene,
                m_playerController->camera);
        if (aoArmed) Ext::AmbientOcclusion::Disarm();
    }
    TRACE("[GameUpdateLoop::Draw] Renderer::Draw done\n");

    // [diag] BANISHED_LOD_CENSUS — count what the frame actually DREW. The lists
    // survive until the next Renderer::Reset, so read them here, right after Draw.
    SceneWatch_DrawItemCensus(m_renderer);

    // Step 8: SaveLoad::Draw if active
    if (m_saveLoad != nullptr) {
        TRACE("[GameUpdateLoop::Draw] calling SaveLoad::Draw...\n");
        Video::Context* videoCtx = ::Engine::MainLoop::_singleton->_context;
        // Save::SaveLoad::Draw — the ExternalPtr<Material> is a bare register value (B7-5).
        m_saveLoad->Draw(
            *videoCtx,
            Object::ExternalPtr<Graphics::Material>(m_uiMaterial));
        TRACE("[GameUpdateLoop::Draw] SaveLoad::Draw done\n");
    }

    // Step 9: Quicksave screenshot
    if (m_quickSavePressed != 0) {
        TRACE("[GameUpdateLoop::Draw] processing quicksave screenshot...\n");
        TakeScreenshot();   // 0x140095da0
        TRACE("[GameUpdateLoop::Draw] quicksave screenshot done\n");
    }

    // Step 10: Set camera matrices on video context for UI
    TRACE("[GameUpdateLoop::Draw] setting camera matrices...\n");
    {
        Video::Context* videoCtx = ::Engine::MainLoop::_singleton->_context;
        const Graphics::Camera* camera = &m_playerController->camera;

        // Original copies Transform3 (12 floats at +0x98..+0xC4 = 48 bytes) and projection
        // matrix (16 floats at +0xC8) from PlayerController onto the stack, then calls
        // AsMatrix → SetCamera. Math::Transform3 is a 48-byte struct (3x4). Copying only
        // 7 floats (the earlier bug) left AsMatrix reading 20 bytes of stack garbage —
        // including the camera translation — producing a corrupt view matrix that threw
        // the 3D placement overlay behind the camera (RenderDoc VS-out w<0). Terrain was
        // unaffected (drawn earlier inside Renderer::Draw with its own camera).
        // AsMatrix returns Matrix by value (64 bytes) — requires hidden return pointer ABI.
        const Math::Transform3 transform3 = camera->_cameraToWorld;   // +0x98, 48 bytes

        Math::Matrix viewMatrix = transform3.AsMatrix();

        // Projection matrix at +0xC8 (16 floats = 64 bytes)
        const Math::Matrix projMatrix = camera->_projection;

        videoCtx->SetCamera(viewMatrix, projMatrix);
    }
    TRACE("[GameUpdateLoop::Draw] camera matrices set\n");

    // Step 11: EntityUI toolbar draw (via vtable call on EntityUI's current tool)
    TRACE("[GameUpdateLoop::Draw] calling EntityUI toolbar draw...\n");
    {
        PlayerToolManager* toolManager = m_toolManager;
        Video::Context* videoCtx = ::Engine::MainLoop::_singleton->_context;

        // Check if UI has input focus or pauses game -> pass as flag
        bool hasInput = (m_userInterface->_mouseCaptured != 0);
        bool pauseCheck = m_userInterface->PausesGame();
        uint64_t drawFlag = (hasInput || pauseCheck) ? 1 : 0;

        // +0x28 is the InternalPtr to the current tool
        Tool* tool = toolManager->currentTool.Get();
        if (tool) {
            // Caller batch CB6: slot 8 (vtable +0x40) `Tool::Draw(context, active)`.
            // The receiver is PROVED by its only writer, Toolbar_ShowGroup_Impl
            // (toolbardialog.cpp): it stores the Factory::CreateObject result only
            // after IsDerivedFrom(DWORD_1401c6eb8), and that cell is TypeId(Tool).
            // The old typedef passed the flag as a qword; the slot reads its low byte.
            tool->Draw(videoCtx, static_cast<char>(drawFlag));
        }
    }
    TRACE("[GameUpdateLoop::Draw] EntityUI toolbar draw done\n");

    // Step 12: UI::UserInterface::Draw
    TRACE("[GameUpdateLoop::Draw] calling UI::UserInterface::Draw...\n");
    {
        Video::Context* videoCtx = ::Engine::MainLoop::_singleton->_context;
        // EXT SEAM (NOT RETAIL, docs/extensions/handins/SHARPUI.md): ext.sharp_ui, retail's Draw inside Ext::SharpUI::Draw
        // (font variants, pixel snap); off = retail's call.
        if (Ext::Ext().sharp_ui)
            Ext::SharpUI::Draw(*m_userInterface, *videoCtx);
        else
            m_userInterface->Draw(*videoCtx);
    }
    TRACE("[GameUpdateLoop::Draw] UI::UserInterface::Draw done\n");

    // Step 13: Quickload screenshot (after UI draw)
    if (m_quickLoadPressed != 0) {
        TRACE("[GameUpdateLoop::Draw] processing quickload screenshot...\n");
        TakeScreenshot();   // 0x140095da0
        TRACE("[GameUpdateLoop::Draw] quickload screenshot done\n");
    }

    StressHarness_DrawEnd();   // [STRESS]
    if (stampDraw) LoadTime::Stamp("game.draw.first.done");   // NOT RETAIL: print-only
    TRACE("[GameUpdateLoop::Draw] EXIT\n");
}



// ============================================================

void GameUpdateLoop::Startup() {
    ResetPlatformSaveRequest();   // NOT RETAIL (P7): a game starts; earlier save requests are not its
    // [HUD-DIAG] Unconditional one-shot: confirm Startup is invoked at all, and
    // whether the world is already built (m_game set) when it runs. If Startup
    // fires before Load (m_game==null), it early-returns forever and the HUD
    // would never be created — an ordering bug we must catch here.
    {
        static bool s_called = false;
        if (!s_called) {
            s_called = true;
            fprintf(stderr, "[HUD-DIAG] Startup CALLED self=%p m_game=%p "
                    "m_userInterface=%p m_resourceList(+0x08)=%p\n",
                    (void*)this,
                    (void*)m_game,
                    (void*)m_userInterface,
                    (void*)_resourceList);
            fflush(stderr);
        }
    }

    // Stage C entry guard (TEMPORARY): see GameUpdateLoop_Update — must run
    // before EnsureInit (which null-derefs an unregistered dispatch slot in
    // standalone). On the new-game path m_game (+0xF8) is null until
    // GameUpdateLoop::Load builds the world; Startup dereferences m_userInterface
    // (+0x98, also null) below. Skip cleanly until Load is implemented.
    if (m_game == nullptr) {
        static bool logged = false;
        if (!logged) {
            logged = true;
            fprintf(stderr, "[GameUpdateLoop::Startup] world not built "
                    "(m_game==null) — Stage C cliff, skipping startup\n");
            fflush(stderr);
        }
        return;
    }


    TRACE("[GameUpdateLoop::Startup] ENTER (self=%p)\n", (void*)this);

    // Set audio scene on UI
    TRACE("[GameUpdateLoop::Startup] setting audio scene on UI...\n");
    m_userInterface->_audioScene = m_audioScene;

    // Setup UI references (cursors, lists, etc.)
    TRACE("[GameUpdateLoop::Startup] calling SetupUIRefs...\n");
    g_gameContainer->ApplyKeyMapToUI(m_userInterface);   // 0x140099260
    // Report the OUTCOME. "SetupUIRefs done" printed identically for the whole time
    // 0x140099260 was unregistered and the call was a no-op; the fields it writes are
    // the only evidence it ran. uiScale 0 == never applied (edittool.cpp:674 reads it).
    {
        const UI::UserInterface* ui = m_userInterface;
        std::fprintf(stderr, "[Startup] SetupUIRefs: uiScale=%.3f sndButtonDown=%p sndAlert=%p\n",
                     ui ? ui->_uiScale : -1.0f,       // +0xDC
                     ui ? ui->_sounds[4] : nullptr,   // +0xB8
                     ui ? ui->_sounds[6] : nullptr);  // +0xC8
        std::fflush(stderr);
    }

    // Set default cursor
    TRACE("[GameUpdateLoop::Startup] setting default cursor...\n");
    {
        GameContainer* keyMapMgr = g_gameContainer;  // [GUARD-M5]
        if (keyMapMgr) {
            m_userInterface->SetCursor(keyMapMgr->cursorHand);
        }
    }

    // Enable cursor
    TRACE("[GameUpdateLoop::Startup] enabling cursor...\n");
    m_userInterface->EnableCursor(true);

    // ProfessionList startup — builds the sorted profession ARRAY at profList+0x20/+0x28
    // (FUN_1400a90a0) for the profession UI panel. m_professionList (gw+0xb0) is normally
    // DEFERRED (null) in the standalone slice; it goes live under BANISHED_LIVE_PROFESSIONLIST.
    // ProfessionList_Startup_Impl is the port of 0x1400a90a0 (B3R: called by name; its
    // readiness guard went with the door).
    TRACE("[GameUpdateLoop::Startup] calling ProfessionList::Startup...\n");
    if (m_professionList)
        m_professionList->Startup();   // 0x1400a90a0
    TRACE("[GameUpdateLoop::Startup] ProfessionList::Startup done\n");

    // Game HUD dialogs (Toolbar/EventNotify/AutoSave) are DEFERRED in the M5 first
    // slice: they require the loop's resource list (m_resourceList, +0x08 — the
    // standalone loader does not populate it yet, so ExternalList::Find faults) AND
    // their exe dialog ctors (not reimplemented). Disable the whole section until
    // both are wired. [GUARD-M5]
    // [HUD-DIAG] One-shot probe: did the preload populate the loop's resource
    // list (+0x08)? And does ExternalList::Find resolve the toolbar Dialog
    // resource out of it? This is the gate for enabling HUD creation below.
    {
        static bool s_diag = false;
        if (!s_diag) {
            s_diag = true;
            const ::Object::ExternalList* rl = _resourceList;
            fprintf(stderr, "[HUD-DIAG] probe: m_resourceList=%p DialogTypeId=%u\n",
                    rl, UI::Dialog::TypeId());
            fflush(stderr);
            if (rl) {
                System::Filename fnbuf("Dialog/Toolbar.rsc");
                void* foundBase = nullptr;
                uint32_t typeInfo = UI::Dialog::TypeId();
                foundBase = rl
                                ->Find(fnbuf, typeInfo)._ptr;
                fprintf(stderr, "[HUD-DIAG] ExternalList::Find(\"Dialog/Toolbar.rsc\") -> foundBase=%p\n",
                        foundBase);
                fflush(stderr);
            } else {
                fprintf(stderr, "[HUD-DIAG] Find probe skipped (resourceList null)\n");
                fflush(stderr);
            }
        }
    }

    // HUD bring-up — the in-game Toolbar (Keystone F). Gated behind the env var
    // BANISHED_LIVE_TOOLBAR (default OFF → the default build stays byte-identical).
    // The resource is resolved via ExternalFactory::FindOrLoad (m_resourceList is
    // empty in standalone — the preload that populates it is off by default).
    // The Toolbar ctor (0x1400b9910 -> ToolbarDialog_Ctor_Impl) is already registered.
    //
    // NOTE: the toolbar dialog constructs + ticks crash-free with BANISHED_LIVE_TOOLBAR
    // alone, but its BUTTONS come from the "Toolbar" item externals, which are loaded by
    // the resource preload. So a fully POPULATED, visibly-rendered toolbar (10 buttons)
    // needs BANISHED_LIVE_TOOLBAR=1 *and* BANISHED_GAME_PRELOAD=1 — verified rendering
    // the real bottom-right toolbar bar on screen, crash-free. With LIVE_TOOLBAR alone
    // the dialog is present but empty (itemCount=0).
    const bool kCreateToolbar = true;

    // Captured toolbar dialog — the EventNotify ribbon parents to it (positions the
    // ribbon just below the toolbar; see EventNotifyDialog_Update). Null if the
    // toolbar was not created (LIVE_TOOLBAR off), in which case the ribbon Update
    // early-returns on the null parent, so we skip creating the ribbon too.
    ToolbarDialog* g_ribbonParentToolbar = nullptr;

    // Create and add Toolbar dialog
    TRACE("[GameUpdateLoop::Startup] creating Toolbar dialog...\n");
    if (kCreateToolbar) {
        // (ENDGAME B.3.9 TR: ToolbarStandalone_Init and its file are gone -- the
        // toolbar reads Toolbar / ToolbarDescription / UI element TypeId() directly.)
        System::Filename filename("Dialog/Toolbar.rsc");

        // Find the Dialog resource. First try the loop's resource list (populated
        // when preload is on); if that is empty or misses, fall back to
        // ExternalFactory::FindOrLoad (the path OpenToolDialog/ToggleAll use — loads
        // the .rsc directly, robust in standalone where m_resourceList is empty).
        ::Object::External* foundBase = nullptr;
        uint32_t typeInfo = UI::Dialog::TypeId();   // retail: [?_typeInfo@Dialog@UI] (0x140097c83)
        if (_resourceList)
            foundBase = _resourceList
                            ->Find(filename, typeInfo)._ptr;
        if (foundBase == nullptr &&
            ::Object::ExternalFactory::_singleton) {
            // ENDGAME B.3.3 step 6A: member call on the real Object::ExternalFactory.
            foundBase = ::Object::ExternalFactory::_singleton
                    ->FindOrLoadAs<::Object::External>(filename);
        }

        const UI::Dialog* dialogRes = nullptr;
        if (foundBase != nullptr) {
            if (::Object::IsDerivedFromNullSafe(foundBase, UI::Dialog::TypeId())) {   // 0x140097cba
                dialogRes = static_cast<const UI::Dialog*>(foundBase);
            }
        }

        fprintf(stderr, "[HUD] Toolbar: foundBase=%p dialogRes=%p UI=%p\n",
                foundBase, dialogRes, (void*)m_userInterface);
        fflush(stderr);

        // Allocate and construct ToolbarDialog (0x108 bytes). [GUARD-M5]
        void* mem = System::MemoryAllocator::MemAlloc(0x108, 8);
        ToolbarDialog* toolbar = nullptr;
        // Retail (0x140097cf1) constructs whenever the allocation succeeded; the
        // `dialogRes != nullptr` skip that sat here coped with a failed type-id
        // resolution, which cannot happen now (ENDGAME B.3.9 CB).
        if (mem != nullptr) {
            ::Object::InternalPtr<UI::DialogInstance> nullParent;   // the InternalBasePtr ctor: one store
            toolbar = ::new (mem) ToolbarDialog(m_userInterface, dialogRes, nullParent, nullptr);   // 0x1400b9910
        }

        // Add to UI (only if the dialog was actually constructed).
        if (toolbar) {
            g_ribbonParentToolbar = toolbar;  // parent for the EventNotify ribbon
            ::Object::InternalPtr<UI::DialogInstance> dialogPtr(toolbar);
            m_userInterface->AddDialog(&dialogPtr);
            fprintf(stderr, "[HUD] Toolbar CONSTRUCTED + added to UI: toolbar=%p itemCount(+0xD8)=%d "
                    "buttonCount(+0xC8)=%d speedCount(+0x100)=%d\n", static_cast<void*>(toolbar),
                    toolbar->toolbarItems.GetSize(), toolbar->buttonElements.GetSize(),
                    toolbar->speedValues.GetSize());
        } else {
            fprintf(stderr, "[HUD] Toolbar NOT constructed (mem=%p) — freeing\n", mem);
            if (mem) System::MemoryAllocator::MemFree(mem);
        }
        fflush(stderr);
    }
    TRACE("[GameUpdateLoop::Startup] Toolbar dialog added\n");

    // Create and add EventNotify ribbon (BANISHED_LIVE_EVENTS). Shows recent game
    // events as a strip below the toolbar. Uses the FindOrLoad fallback (empty
    // m_resourceList in standalone) like the toolbar, and parents to the toolbar
    // dialog so Update can position the ribbon. Skipped if the toolbar is absent
    // (the ribbon Update early-returns on a null parent anyway).
    TRACE("[GameUpdateLoop::Startup] creating EventNotify dialog...\n");
    if (g_ribbonParentToolbar != nullptr) {
        System::Filename filename("Dialog/EventNotify.rsc");
        ::Object::External* foundBase = nullptr;
        uint32_t typeInfo = UI::Dialog::TypeId();   // retail: [?_typeInfo@Dialog@UI]
        if (_resourceList)
            foundBase = _resourceList
                            ->Find(filename, typeInfo)._ptr;
        if (foundBase == nullptr &&
            ::Object::ExternalFactory::_singleton) {
            // ENDGAME B.3.3 step 6A: member call on the real Object::ExternalFactory.
            foundBase = ::Object::ExternalFactory::_singleton
                    ->FindOrLoadAs<::Object::External>(filename);
        }
        const UI::Dialog* dialogRes = nullptr;
        if (foundBase != nullptr && ::Object::IsDerivedFromNullSafe(foundBase, UI::Dialog::TypeId()))
            dialogRes = static_cast<const UI::Dialog*>(foundBase);

        fprintf(stderr, "[HUD] EventNotify: foundBase=%p dialogRes=%p parentToolbar=%p\n",
                foundBase, dialogRes, g_ribbonParentToolbar);
        fflush(stderr);

        void* mem = System::MemoryAllocator::MemAlloc(0xF0, 8);
        EventNotifyDialog* eventNotify = nullptr;
        if (mem != nullptr && dialogRes != nullptr) {
            ::Object::InternalPtr<UI::DialogInstance> parentIBP;
            new (&parentIBP) ::Object::InternalPtr<UI::DialogInstance>(g_ribbonParentToolbar);
            eventNotify = ::new (mem) EventNotifyDialog(m_userInterface, dialogRes, parentIBP);   // 0x140068ab0
        }

        if (eventNotify) {
            ::Object::InternalPtr<UI::DialogInstance> dialogPtr(eventNotify);
            m_userInterface->AddDialog(&dialogPtr);
            fprintf(stderr, "[HUD] EventNotify ribbon CONSTRUCTED + added to UI: %p\n", eventNotify);
            fflush(stderr);
        } else if (mem) {
            System::MemoryAllocator::MemFree(mem);
        }
    }
    TRACE("[GameUpdateLoop::Startup] EventNotify dialog added\n");

    // Create and add AutoSave dialog
    TRACE("[GameUpdateLoop::Startup] creating AutoSave dialog...\n");
    // The whole autosave FEATURE hangs off this dialog:
    // its timer (started by AutoSaveDialog::Slot4 from the Options interval) is the
    // only clock, and its timer-event handler (Slot8 -> Dialog_AutoSaveHandler) is the
    // only caller of SaveLoad::SetOperation(SAVE). While this block was hardcoded off
    // the game never autosaved at all - the Options 'Autosave' setting was inert.
    // Uses the ExternalFactory::FindOrLoad fallback like the toolbar/EventNotify,
    // since m_resourceList is empty in standalone. Parented to nothing (retail
    // passes a null parent IBP here too).
    {
        System::Filename filename("Dialog/AutoSave.rsc");
        ::Object::External* foundBase = nullptr;
        uint32_t typeInfo = UI::Dialog::TypeId();   // retail: [?_typeInfo@Dialog@UI]
        if (_resourceList)
            foundBase = _resourceList
                            ->Find(filename, typeInfo)._ptr;
        if (foundBase == nullptr &&
            ::Object::ExternalFactory::_singleton) {
            // ENDGAME B.3.3 step 6A: member call on the real Object::ExternalFactory.
            foundBase = ::Object::ExternalFactory::_singleton
                    ->FindOrLoadAs<::Object::External>(filename);
        }
        const UI::Dialog* dialogRes = nullptr;
        if (foundBase != nullptr && ::Object::IsDerivedFromNullSafe(foundBase, UI::Dialog::TypeId()))
            dialogRes = static_cast<const UI::Dialog*>(foundBase);

        void* mem = System::MemoryAllocator::MemAlloc(0xD0, 8);
        AutoSaveDialog* autoSave = nullptr;
        if (mem != nullptr && dialogRes != nullptr) {
            ::Object::InternalPtr<UI::DialogInstance> nullParent;   // the InternalBasePtr ctor: one store
            autoSave = ::new (mem) AutoSaveDialog(m_userInterface, dialogRes, nullParent);   // 0x14005e230
        }

        if (autoSave) {
            ::Object::InternalPtr<UI::DialogInstance> dialogPtr(autoSave);
            m_userInterface->AddDialog(&dialogPtr);
        } else if (mem) {
            System::MemoryAllocator::MemFree(mem);
        }
        fprintf(stderr, "[AutoSave] dialog res=%p instance=%p (null instance = no autosave)\n",
                dialogRes, autoSave);
        fflush(stderr);
    }
    TRACE("[GameUpdateLoop::Startup] AutoSave dialog added\n");

    // Set fullscreen fade
    TRACE("[GameUpdateLoop::Startup] setting fullscreen fade...\n");
    m_userInterface->SetFullscreenFade(true, 0xFF000000, true);
    m_userInterface->SetFullscreenFade(false, 0xFF000000, false);
    TRACE("[GameUpdateLoop::Startup] fullscreen fade set\n");

    TRACE("[GameUpdateLoop::Startup] calling EntityUI::Startup...\n");
    // The tool registry, the pinned dialogs and entity panels a save recorded,
    // then the EditTool.
    m_toolManager->Startup();   // 0x140101ee0
    TRACE("[GameUpdateLoop::Startup] EntityUI::Startup done\n");

    TRACE("[GameUpdateLoop::Startup] EXIT\n");
}


// ============================================================
// GameUpdateLoop::Shutdown (0x1400977C0) — Teardown all subsystems
// ============================================================

extern "C" void RedzoneAlloc_ArmTeardownCheck();   // src/diag/win/redzone_alloc.cpp
void GameUpdateLoop::Shutdown() {
    // EXT SEAM (NOT RETAIL, docs/extensions/handins/HITCH_SAVESNAP.md): ext.background_save -- Engine::MainLoop runs Shutdown before the
    // destructor, and the teardown below frees the world a save worker may still be reading (a
    // window close pumped during the frozen frames). Wait for it and finish that save first.
    Ext::BackgroundSave::Join(true);
    Ext::RenderCulling::Reset();   // [PerfX] drop cached model pointers before the world is freed
    TerrainSidecar::Report();       // [terrain sidecar] the COMPPROF=4 shadow's totals
    AmbientEnvironmentComponent::ReportMemo();   // [AMBMEMO] hit / miss / shadow totals
    TerrainSidecar::Invalidate();   // [terrain sidecar] the grid is about to be freed
    CellStamp::Invalidate();        // [AMBMEMO] the grid is about to be freed
    StillTerrain::Bump();           // [STILLTERRAIN] the map and the heightmap are about to be freed
    RedzoneAlloc_ArmTeardownCheck();   // no-op unless BANISHED_REDZONE=watch + BANISHED_TEARDOWN_HEAPCHECK=1

    TRACE("[GameUpdateLoop::Shutdown] ENTER (self=%p)\n", (void*)this);

    // Stage C entry guard (TEMPORARY): on the new-game path the world was never
    // built (GameUpdateLoop::Load is a stub), so m_game (+0xF8) is null and the
    // teardown below dereferences unbuilt subsystems (m_toolManager, m_userInterface,
    // m_audioScene, m_citizenList). Nothing was constructed, so there is nothing
    // to tear down — return. (Destroy still runs afterward to free the loop.)
    if (m_game == nullptr) {
        fprintf(stderr, "[GameUpdateLoop::Shutdown] world not built (m_game==null) "
                "— Stage C cliff, nothing to tear down\n");
        fflush(stderr);
        return;
    }

    // Clear achievement event handler
    TRACE("[GameUpdateLoop::Shutdown] clearing achievement event handler...\n");
    if (::Achievement::AchievementManager::_singleton) {
        ::Achievement::AchievementManager::_singleton->SetEventHandler(nullptr);
    }

    // Clear game flags (remove 0x100 bit)
    TRACE("[GameUpdateLoop::Shutdown] clearing game flags...\n");
    {
        GameUpdateLoop* gameWorld = GameUpdateLoop::_singleton;
        uint32_t* flags = &gameWorld->m_gameFlags;
        *flags = *flags & 0xFFFFFEFF;
    }

    // EntityUI::SetTool(nullptr)
    TRACE("[GameUpdateLoop::Shutdown] calling EntityUI::SetTool(0)...\n");
    m_toolManager->SelectTool(nullptr);   // 0x140102b60

    // Disable cursor
    TRACE("[GameUpdateLoop::Shutdown] disabling cursor...\n");
    m_userInterface->EnableCursor(false);

    // Stop all audio
    TRACE("[GameUpdateLoop::Shutdown] stopping all audio...\n");
    m_audioScene->StopAll();

    // Citizen housing reassignment loop
    TRACE("[GameUpdateLoop::Shutdown] reassigning citizen housing...\n");
    {
        CitizenBehavior* citizenList = m_citizenList;
        int32_t citizenCount = citizenList->citizens._size;       // +0xa8
        Object::InternalPtr<CitizenComponent>* citizenArray = citizenList->citizens._data;   // +0xa0

        for (int i = 0; i < citizenCount; i++) {
            CitizenComponent* citizen = citizenArray[i].Get();

            // An adult (age +0x3c >= the description's adultAge +0x44) with no
            // school (+0x208) goes back to the list's default profession and
            // loses its workplace.
            float age = citizen->age;
            float maxAge = citizen->description->adultAge;
            AttendanceComponent* school = citizen->school.Get();

            if (age >= maxAge && school == nullptr) {
                // The profession list's +0x30 profession
                Profession* defaultProfession = m_professionList->field_0x30;

                // Current profession (+0x1f0) and workplace (+0x248)
                Profession* citProfession = citizen->profession;
                ComponentEntity* citWorkplace = citizen->workplace.Get();

                ComponentEntity* nullRef = nullptr;   // the null InternalPtr<ComponentEntity> retail passes

                if (citProfession != defaultProfession || citWorkplace != nullptr) {
                    citizen->SetWorkplace(nullRef);   // 0x140111ee0
                    citizen->profession = defaultProfession;
                }
            }
        }
    }
    TRACE("[GameUpdateLoop::Shutdown] citizen housing done\n");

    // [OBJAUDIT] Wave 9 lane N, Finding 3. The periodic scan above only runs from
    // Update, so the handle table has NEVER been checked during teardown — which is
    // the only place the intermittent allocator fault fires. Mode (A) in
    // Object/internal_factory_audit.h ("register without unregister": the next
    // ~Internal swap-removes a freed object and writes `*(int*)(freedPtr+0x08) = idx`,
    // a 4-byte store into a reused block) has exactly the signature of that fault —
    // a small integer landing on half of something live. Bracketing EntityList's
    // shutdown separates "the table was already corrupt when teardown started" from
    // "teardown corrupted it", which no run so far can tell apart.
    // EntityList shutdown
    TRACE("[GameUpdateLoop::Shutdown] calling EntityList::Shutdown...\n");
    m_entityList->Shutdown();   // 0x1400c3d30
    TRACE("[GameUpdateLoop::Shutdown] EntityList::Shutdown done\n");

    // Remove global scene objects
    TRACE("[GameUpdateLoop::Shutdown] removing global scene objects...\n");
    {
        GameUpdateLoop* gameWorld = GameUpdateLoop::_singleton;
        Graphics::Scene* gs = gameWorld->m_graphicsScene;
        gs->RemoveGlobalObject(m_worldGeometry);
        gs->RemoveGlobalObject(m_sunLight);
        gs->RemoveGlobalObject(m_skyMesh);
    }

    // Delete scenario instance
    if (m_scenarioInstance.Get() != nullptr) {
        TRACE("[GameUpdateLoop::Shutdown] deleting scenario instance...\n");
        m_scenarioInstance.DeletePtr();
    }

    // Free/destroy all subsystems
    TRACE("[GameUpdateLoop::Shutdown] destroying subsystems...\n");

    // Random
    if (m_random != nullptr) {
        TRACE("[GameUpdateLoop::Shutdown]   freeing Random...\n");
        System::MemoryAllocator::MemFree(m_random);
    }

    // PlayerController
    if (m_playerController != nullptr) {
        TRACE("[GameUpdateLoop::Shutdown]   freeing PlayerController...\n");
        System::MemoryAllocator::MemFree(m_playerController);
    }

    // Renderer
    if (m_renderer != nullptr) {
        TRACE("[GameUpdateLoop::Shutdown]   destroying Renderer...\n");
        m_renderer->~Renderer();   // ~Renderer 0x1800180a0
        System::MemoryAllocator::MemFree(m_renderer);
    }

    // EntityList
    if (m_entityList != nullptr) {
        TRACE("[GameUpdateLoop::Shutdown]   destroying EntityList...\n");
        m_entityList->~EntityList();   // 0x1400c3e60
        System::MemoryAllocator::MemFree(m_entityList);
    }

    // UserInterface
    if (m_userInterface != nullptr) {
        TRACE("[GameUpdateLoop::Shutdown]   destroying UserInterface...\n");
        // Call Runtime destructor
        RtUi_UserInterface_Dtor(m_userInterface);
        System::MemoryAllocator::MemFree(m_userInterface);
    }

    // CitizenBehavior
    if (m_citizenList != nullptr) {
        TRACE("[GameUpdateLoop::Shutdown]   destroying CitizenBehavior...\n");
        m_citizenList->~CitizenBehavior();   // 0x1400907b0
        System::MemoryAllocator::MemFree(m_citizenList);
    }

    // Game
    if (m_game != nullptr) {
        TRACE("[GameUpdateLoop::Shutdown]   destroying Game...\n");
        m_game->~Map();   // 0x14009ed60 (keystone_a_gamedtor.cpp)
        System::MemoryAllocator::MemFree(m_game);
    }

    // Tutorial
    if (m_tutorial != nullptr) {
        TRACE("[GameUpdateLoop::Shutdown]   destroying Tutorial...\n");
        m_tutorial->Teardown();   // 0x1400f8ff0
        System::MemoryAllocator::MemFree(m_tutorial);
    }

    // EntityUI
    if (m_toolManager != nullptr) {
        TRACE("[GameUpdateLoop::Shutdown]   destroying EntityUI...\n");
        m_toolManager->Teardown();   // 0x1401024d0
        System::MemoryAllocator::MemFree(m_toolManager);
    }

    // DayCounter
    if (m_dayCounter != nullptr) {
        TRACE("[GameUpdateLoop::Shutdown]   freeing DayCounter...\n");
        System::MemoryAllocator::MemFree(m_dayCounter);
    }

    // Weather
    if (m_weather != nullptr) {
        TRACE("[GameUpdateLoop::Shutdown]   destroying Weather...\n");
        m_weather->Teardown();   // 0x140093eb0
        System::MemoryAllocator::MemFree(m_weather);
    }

    // EventNotifier
    if (m_eventNotifier != nullptr) {
        TRACE("[GameUpdateLoop::Shutdown]   destroying EventNotifier...\n");
        m_eventNotifier->~EventLog();   // ~Array 0x14000a8a0 on +0x18 (0x140097ab0)
        System::MemoryAllocator::MemFree(m_eventNotifier);
    }

    // Graphics::Scene
    if (m_graphicsScene != nullptr) {
        TRACE("[GameUpdateLoop::Shutdown]   destroying Graphics::Scene...\n");
        m_graphicsScene->~Scene();   // ~Scene 0x1801026c0
        System::MemoryAllocator::MemFree(m_graphicsScene);
    }

    // Audio::Scene
    if (m_audioScene != nullptr) {
        TRACE("[GameUpdateLoop::Shutdown]   destroying Audio::Scene...\n");
        m_audioScene->~Scene();   // ??1Scene@Audio@@ 0x1800e0120
        System::MemoryAllocator::MemFree(m_audioScene);
    }

    // Picking::Scene
    if (m_pickingScene != nullptr) {
        TRACE("[GameUpdateLoop::Shutdown]   destroying Picking::Scene...\n");
        m_pickingScene->::Picking::Scene::~Scene();
        System::MemoryAllocator::MemFree(m_pickingScene);
    }

    // WorldGeometry, SunLight, SkyMesh — via scalar deleting dtor (vtable[0] with flags=1)
    if (m_worldGeometry != nullptr) {
        TRACE("[GameUpdateLoop::Shutdown]   destroying WorldGeometry...\n");
        // Caller batch CB6: slot 0 of the real class (proved as in Update step 8).
        delete m_worldGeometry;
    }
    if (m_sunLight != nullptr) {
        TRACE("[GameUpdateLoop::Shutdown]   destroying SunLight...\n");
        delete m_sunLight;
    }
    if (m_skyMesh != nullptr) {
        TRACE("[GameUpdateLoop::Shutdown]   destroying SkyMesh...\n");
        delete m_skyMesh;
    }

    // AnimationManager
    if (m_animationManager != nullptr) {
        TRACE("[GameUpdateLoop::Shutdown]   destroying AnimationManager...\n");
        RtGraphics_AnimationManager_Dtor(m_animationManager);   // ~AnimationManager 0x1800fc400
        System::MemoryAllocator::MemFree(m_animationManager);
    }

    // CharacterGrid (Morton quadtree at +0x88)
    if (m_characterGrid != nullptr) {
        TRACE("[GameUpdateLoop::Shutdown]   destroying CharacterGrid...\n");
        m_characterGrid->~CharacterGrid();   // 0x140018220
        System::MemoryAllocator::MemFree(m_characterGrid);
    }

    // SaveLoad
    if (m_saveLoad != nullptr) {
        TRACE("[GameUpdateLoop::Shutdown]   destroying SaveLoad...\n");
        RtSave_SaveLoad_Dtor(m_saveLoad);
        System::MemoryAllocator::MemFree(m_saveLoad);
    }

    // ImageCapture
    if (m_imageCapture != nullptr) {
        TRACE("[GameUpdateLoop::Shutdown]   destroying ImageCapture...\n");
        m_imageCapture->~Minimap();   // 0x1400a4fc0
        System::MemoryAllocator::MemFree(m_imageCapture);
    }

    // MusicPlayer (0x18-byte POD; retail frees it with no dtor)
    if (m_musicPlayer != nullptr) {
        TRACE("[GameUpdateLoop::Shutdown]   freeing MusicPlayer...\n");
        System::MemoryAllocator::MemFree(m_musicPlayer);
    }

    TRACE("[GameUpdateLoop::Shutdown] EXIT\n");
}


// ============================================================
// CalcWindFactor (0x1401027c0) — really "camera distance to focus"
// ============================================================
// Feeds Audio::Scene::Update's last parameter. Named fn_CalcWindFactor here, but
// the disassembly is a plain distance:
//
//   RBX  = [g_pGameWorldSingleton + 0xE0]        playerController
//   copy Camera (playerCtrl+0x34) to a stack temp via slot 0x140152940
//   diff = camera.position (copy+0x88..0x90) - [RBX+0x1c/0x20/0x24]   focus/anchor
//   Vector3::Dot(diff, diff) -> XMM0 ; JMP 0x140132010 (sqrtf)
//
// So: |cameraPos - focusPos|, returned as a FLOAT in XMM0. The Time* argument is
// spilled at entry and never read — keep it unused; making it drive a real
// oscillation would invent behaviour retail never had.
//
// DEVIATION: retail copy-constructs the whole Camera onto the stack and reads the
// copy's +0x88. We read playerCtrl+0x34+0x88 directly. The position is a copied
// field, not one the copy ctor could synthesise, so the values are identical — and
// this avoids taking a dependency on dispatch slot 0x140152940, which nothing else
// in the tree registers.
static float CalcWindFactor(const System::Time* /*time — dead in retail*/) {
    GameUpdateLoop* gw = GameUpdateLoop::_singleton;
    if (!gw) return 0.0f;
    const PlayerController* playerCtrl = gw->m_playerController;
    if (!playerCtrl) return 0.0f;

    // The camera's position: its cameraToWorld translation (+0x34 + 0x88).
    const Math::Vector3& camPos = playerCtrl->camera._cameraToWorld.translation;
    float dx = camPos.x - playerCtrl->target.x;
    float dy = camPos.y - playerCtrl->target.y;
    float dz = camPos.z - playerCtrl->target.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

// ============================================================
// GameUpdateLoop::OnScenarioComplete (0x140096830)
// ============================================================
// Called when the scenario achievement is met or has failed.
// Sets m_scenarioCompleted flag and fires an event notification.

void GameUpdateLoop::OnScenarioComplete() {

    TRACE("[GameUpdateLoop::OnScenarioComplete] ENTER (self=%p)\n", (void*)this);

    // Set completed flag
    m_scenarioCompleted = 1;

    // Check if the achievement has a parent Achievement object
    Achievement::Achievement* achievement = m_scenarioInstance->_achievement.Get();   // +0x20

    if (achievement == nullptr) {
        // No parent achievement — simple notification
        // Event type: 0x1d (win) or 0x1c (fail) depending on IsMet
        TRACE("[GameUpdateLoop::OnScenarioComplete] no parent achievement, firing simple notification...\n");
        ::Object::InternalBasePtr nullEntity(nullptr);

        bool isMet = m_scenarioInstance->IsMet();

        // 0x1c = scenario won (28), 0x1d = scenario failed (29). IsMet=1 -> 0x1c.
        // (The IDs were documented inverted here; the live string table settles it —
        // the named sibling 0x1b reads "You've failed to meet a goal in the scenario".)
        int eventType = 0x1d - (int)isMet;
        if (m_eventNotifier) m_eventNotifier->AddEvent(eventType, 4, &nullEntity);   // 0x140095580
    } else {
        // Has parent achievement — notification with achievement title
        TRACE("[GameUpdateLoop::OnScenarioComplete] has parent achievement, firing named notification...\n");

        // Get achievement title (non-trivial return: this, retval)
        System::String titleStr;
        ::new (&titleStr) System::String(achievement->GetTitle());

        // Build UI::String from achievement name (B.3.6: the old `uint8_t uiStr[24]`
        // scratch, whose only uses were this ctor and the conversion below).
        UI::String uiStr(achievement->_stringTable, titleStr);   // Achievement+0x20

        // Retail destructs the title String here — it reuses that same stack slot
        // for the UI::String -> System::String conversion below.
        titleStr = System::String();

        // Convert to System::String (member, non-trivial return: this, retval)
        System::String sysStr(uiStr.operator System::String());

        ::Object::InternalBasePtr nullEntity(nullptr);

        bool isMet = m_scenarioInstance->IsMet();

        // 0x1a = scenario won with name (26), 0x1b = scenario failed with name (27)
        int eventType = 0x1b - (int)isMet;
        if (m_eventNotifier) m_eventNotifier->AddEventWithName(eventType, 4, &nullEntity, &sysStr);   // 0x140095750
    }

    TRACE("[GameUpdateLoop::OnScenarioComplete] EXIT\n");
}


// ============================================================
// GameUpdateLoop::Destroy (0x140019DD0) — Scalar deleting destructor
// ============================================================
// Ghidra decompile:
//   1. Set vtable to GameUpdateLoop vtable
//   2. Clear g_pGameWorldSingleton to NULL
//   3. Destroy System::String at +0x28 (m_saveName)
//   4. Call UpdateLoop::~UpdateLoop (base dtor in Runtime)
//   5. If flags & 1, free memory

// ENDGAME B.3.8 LOOP: step 2 is the destructor's body; steps 3 and 4 (~String
// on m_saveName, ~UpdateLoop) are the compiler's member and base chain, in that
// order (step 1, the restamp, is the compiler's too). The old slot reached the
// base through the `??1UpdateLoop@Engine@@` door, which shares its body with
// the real ~UpdateLoop (RB). Step 5, the free, is UpdateLoop's operator delete
// (the virtual destructor's D0); nothing may touch the object after it, so the
// old EXIT trace is gone.
GameUpdateLoop::~GameUpdateLoop() {

    TRACE("[GameUpdateLoop::Destroy] ENTER (self=%p)\n", (void*)this);

    // EXT SEAM (NOT RETAIL, docs/extensions/handins/HITCH_SAVESNAP.md): ext.background_save -- never free the world under its worker
    // (Shutdown has joined already when it ran; this covers a loop deleted without it).
    Ext::BackgroundSave::Join(false);

    // Step 2: Clear game world singleton
    _singleton = nullptr;

    // EXT SEAM (NOT RETAIL, docs/extensions/handins/EXTSCAFFOLD.md): release this loop's class B / C lock (ignored
    // when a reload's new loop already holds it). Releasing it also empties the class C state.
    Ext::UnlockForGame(this);
}

// The exe cell 0x1401c3dd8 (B3R STORAGE; was the file-static
// g_storage_GameWorldSingleton of standalone/address_registry.cpp, read through
// g_pGameWorldSingleton / g_pGameSingleton).
GameUpdateLoop* GameUpdateLoop::_singleton = nullptr;


// ============================================================
// GameUpdateLoop::GameUpdateLoop (0x140098060)
// ============================================================
// Retail's ctor: `??0UpdateLoop@Engine@@` (the init list), the 0x14015e2f0
// stamp (the compiler's), then every field below — the same stores the three
// door-then-stamp construction sites outside this block write by hand
// (newgamedialog.cpp, saveloaddialog.cpp, tutorial_next_scenario.cpp).
GameUpdateLoop::GameUpdateLoop(Save::SavedState* savedState,
                               const Object::InternalPtr<Achievement::AchievementInstance>& scenarioIbp,
                               int32_t scenarioIndex,
                               const System::String& saveName, int32_t seed,
                               int32_t mapSize, bool disasters, MapGenerator* mapConfig,
                               TerrainGenerator* terrainConfig, ClimateGenerator* climateConfig)
    : Engine::UpdateLoop(), m_saveName(saveName) {
    Object::ClassVptr<GameUpdateLoop>::Record(this);   // NOT RETAIL: the vptr hint (object/class_vptr.h)
    m_seed          = seed;
    m_mapSize       = mapSize;
    m_gameTime      = 0;
    m_mapConfig     = mapConfig;
    m_terrainConfig = terrainConfig;
    m_climateConfig = climateConfig;
    m_savedState    = savedState;
    // +0x60..+0xB8, in field order (retail's qword stores).
    m_audioScene = nullptr;    // the chained store's order: +0x78, +0x70, +0x68, +0x60
    m_pickingScene = nullptr;
    m_animationManager = nullptr;
    m_graphicsScene = nullptr;
    m_renderer = nullptr;      // the chained store's order: +0x90, +0x88, +0x80
    m_characterGrid = nullptr;
    m_musicPlayer = nullptr;
    m_userInterface  = nullptr;
    m_helpTopics     = nullptr;
    m_diseaseList    = nullptr;
    m_professionList = nullptr;
    m_uiMaterial     = nullptr;
    m_quickSavePressed = 0;
    m_quickLoadPressed = 0;
    m_nextLoop      = nullptr;
    m_gameFlags     = disasters ? 0u : 0xF0u;   // retail: p8 ? 0 : 0xF0
    // +0xD8..+0x148, in field order.
    m_random = nullptr;
    m_playerController = nullptr;
    m_game = nullptr;          // the chained store's order: +0xF8, +0xF0, +0xE8
    m_citizenList = nullptr;
    m_entityList = nullptr;
    m_sunLight      = nullptr;
    m_skyMesh       = nullptr;
    m_worldGeometry = nullptr;
    m_tutorial    = nullptr;
    m_toolManager = nullptr;
    m_weather = nullptr;       // the chained store's order: +0x130, +0x128
    m_dayCounter = nullptr;
    m_eventNotifier = nullptr;
    m_saveLoad      = nullptr;
    m_imageCapture  = nullptr;
    m_scenarioInstance  = scenarioIbp;   // *param_3 (0x1400981c3)
    m_scenarioCompleted = 0;
    m_scenarioIndex     = scenarioIndex;
}

// ============================================================
// The small slots (were standalone/gameupdateloop_stubs.cpp)
// ============================================================

// [7] 0x1400959c0 — serializes loop state to/from a Save::SavedState. Body in
// gameupdateloop_serialize.cpp.

// [8] 0x140095970 — the device-lost release of the loop's own GPU objects.
// Engine::MainLoop::OnVideoError calls it after destroying the Externals and
// before destroying the Video::Context. Retail releases exactly two owners:
// the terrain (WorldGeometry, +0x110: its four buffer companions and its height
// image, 0x1400b0be0) and the minimap (+0x148: the two images' companions,
// inlined as ImageBuffer::Release on +0x10, then +0x08). Both null tests are
// retail's (TEST RCX,RCX / TEST RBX,RBX); the images themselves are not tested.
void GameUpdateLoop::OnVideoError() {
    if (m_worldGeometry)
        m_worldGeometry->OnVideoError();             // 0x1400b0be0
    if (Minimap* minimap = m_imageCapture) {
        minimap->overlayImage->Release();            // +0x10 (0x14009599a)
        minimap->terrainImage->Release();            // +0x08 (tail JMP 0x1400959a9)
    }
}

// [9] 0x140095930 — the device-restored half, called by
// Engine::MainLoop::OnVideoRestart on the new Context: the terrain's buffers,
// mesh and height image rebuilt (0x1400b0c50), then the minimap's two images
// recreated and re-uploaded (tail JMP 0x1400a59a0). Retail's null tests.
void GameUpdateLoop::OnVideoRestart() {
    if (m_worldGeometry)
        m_worldGeometry->OnVideoRestart();           // 0x1400b0c50
    if (m_imageCapture)
        m_imageCapture->OnVideoRestart();            // 0x1400a59a0
}

// [10] 0x140019d40 — LOAD-BEARING: returns the static string "Game/Game".
// The engine's UpdateLoop::LoadResources (Runtime 0x1800dd7d0, LOADER THREAD)
// calls this slot and, if it returns non-null, FindOrLoads "<ret>Resources.rsc"
// + its whole recursive dependency tree (the HUD/dialog/toolbar resources).
// The preload graduated to always-on 2026-09-13. The once-only log flag is a
// namespace-scope atomic (B6-5: this runs on the loader thread).
namespace { std::atomic<bool> s_gulResourceLogged{false}; }
const char* GameUpdateLoop::GetResourceFilename() {
    if (!s_gulResourceLogged.exchange(true)) {
        printf("[GUL::GetResourceFilename] preload ON -> returning \"Game/Game\" "
               "(engine will recursively preload the HUD/dialog/toolbar resource tree)\n");
        fflush(stdout);
    }
    return "Game/Game";
}

// The ctor records the vptr (Object::ClassVptr<GameUpdateLoop>): the splash's "is the
// next loop a GameUpdateLoop" identity test reads it. ENDGAME B.3.9 EXA: the
// 0x14015e2f0 registration went with the last door-then-stamp builder.
