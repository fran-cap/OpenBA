// src/game/Game/gameupdateloop_load.cpp
// GameUpdateLoop::Load (exe 0x140096b00) — the in-game WORLD BUILD (M5).
//
// This is the Stage-C cliff from project_newgame_path_roadmap: the loader chain
// reaches GameUpdateLoop::Load on "New Game → OK", and this function constructs
// the ~28 in-game subsystems that GameUpdateLoop::{Startup,Update,Draw} operate on.
// Until it ran, m_game (+0xF8) was null and those methods held behind a guard.
//
// FIRST SLICE (milestone 5a): build every subsystem object so m_game is non-null
// and the loop runs. DEFERRED to later slices:
//   - the Object::ExternalList::Find resource loads (Material/Environment/MusicList/
//     SkyMesh/StringTables) — left null; consumers guarded. (exe-typed ones are
//     factory-cascade landmines; Runtime-typed ones are cheap to re-add once the
//     skeleton is proven.) See project_gameupdateloop_load_map.
//     NO LONGER deferred: Diseases (step 1, +0xA8), Profession (step 2, +0xB0),
//     HelpTopics (step 4, +0xA0) — each behind its own env gate.
//   - the SkyMesh MeshInstance (+0x108, step 28) — needs vtable 0x14015e240 registered.
//   - the deep worldgen (step 31): FUN_1400b0230 / CreateNaturalResource_impl_ef20 /
//     FUN_1400a3300 — real terrain + citizens; milestone 5b.
//
// All subsystem ctor bodies below were verified against Ghidra disassembly
// (m5-load-ctor-drafts workflow, 2026-05-30) — exact field offsets in comments.

// ENDGAME B.3.5 Video/Graphics caller batch: Graphics::{Camera,Scene,Geometry,
// MeshInstance,SunLight} are real classes. These go AHEAD of "resolve.h" and the
// explicit <windows.h> below — <wingdi.h> declares a global `Rectangle` FUNCTION
// that hides `Volume::Rectangle`'s class name inside Volume.h's own
// static_asserts (Camera.h and Geometry.h include Volume.h).
#include "graphics/Camera.h"
#include "Component/still_terrain.h"   // STILLTERRAIN: StillTerrain::Bump (the terrain epoch)
#include "graphics/Scene.h"
#include "graphics/Mesh.h"        // MeshInstance
#include "graphics/SunLight.h"
#include "graphics/Material.h"   // Graphics::Material::TypeId() (ENDGAME B.3.9 TR)
#include "GameUpdateLoop.h"
#include "WorldGeometry.h"      // caller batch CB6: m_worldGeometry's class
#include "ProfessionList.h"     // m_professionList's class (B.4 W0-H2)
#include "PlayerController.h"   // m_playerController's class (B.4 W0-H2)
#include "PlayerToolManager.h"  // m_toolManager's class (B.4 W0-H2)
#include "Map.h"                // m_game's class (B.4 GAME-a)
#include "Entity/EntityList.h"       // m_entityList's class (B.4 GAME-a)
#include "Entity/CitizenBehavior.h"  // m_citizenList's class (B.4 GAME-a)
#include "Component/CharacterGrid.h"  // m_characterGrid's class (B.4 GAME-a)
#include "Calendar.h"                // m_dayCounter's class (B.4 GAME-a)
#include "WeatherSimulation.h"       // m_weather's class (B.4 GAME-a)
#include "Component/EnvironmentDescription.h"   // WeatherSimulation::Bind
#include "Game/ClimateGenerator.h"
#include "Game/Disease.h"            // the disease list dump
#include "WorkScheduler.h"           // m_tutorial's class (B.4 GAME-a)
#include "EventLog.h"                // m_eventNotifier's class (B.4 GAME-a)
#include "MusicPlayer.h"             // m_musicPlayer's class (B.4 GAME-a)
#include "Minimap.h"                 // m_imageCapture's class (B.4 GAME-a)
#include "MapGenerator.h"            // m_mapConfig's class (B.4 GAME-a)
#include "Game/TerrainGenerator.h"     // m_terrainConfig->_field80
#include "Game/TerrainHeightfield.h"  // TerrainSizeRow
#include <type_traits>
#include <new>
#include "Game/game.h"   // B3R STORAGE: g_gameContainer
#include "system/MemoryAllocator.h"   // System::MemoryAllocator::MemAlloc / MemFree
#include "math/Math.h"
#include "system/Filename.h"
#include "system/String.h"
#include "collection/Collection.h"
#include "object/External.h"   // ENDGAME B.3.3 step 6A
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <atomic>
#include "platform_thread.h"  // Platform::CurrentThreadId (double-dispatch diagnostic)
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "object/TypeInfo.h"   // ENDGAME B.3.3 step 6C
#include "system/Value.h"      // ENDGAME B.3.4b System caller batch (Random/Time/Color)
#include "achievement/AchievementManager.h"   // ENDGAME B.3.7 callers (K2)
#include "audio/Audio.h"     // ENDGAME B.3.7 callers (K4)
#include "ui/StringTable.h"  // UI::StringTable::TypeId() (D5, ENDGAME B.3.9 CG3)
#include "video/Device.h"   // ENDGAME B.3.9 SS2: Video::Device::_singleton
#include "Game/exe_bodies.h"
#include "graphics/Camera.h"            // Graphics::Camera, Graphics::Renderer
#include "graphics/Animation.h"         // RtGraphics_AnimationManager_Ctor
#include "picking/Scene.h"              // RtPicking_Scene_Ctor
#include "ui/UserInterface.h"           // RtUi_UserInterface_Ctor
#include "save/SaveLoad.h"              // RtSave_SaveLoad_Ctor / RtSave_SavedState_Dtor
#include "utility/PerlinNoise.h"        // RtUtility_PerlinNoise_Ctor
#include "Entity/ComponentEntity.h"   // B4 S4 RT2: InternalPtr<ComponentEntity>::Get() needs the class complete
#include "ext/registry.h"   // EXT SEAM (NOT RETAIL, docs/extensions/handins/EXTSCAFFOLD.md): Ext::LockForGame
// EXT SEAM (NOT RETAIL, docs/extensions/handins/RESTRICTEDFOODS.md): Ext::SaveTail::ResetAllState, a
// class C extension's state
#include "ext/savetail.h"
#include "ext/show_grid/show_grid.h"   // EXT SEAM (NOT RETAIL, SHOWGRID.md): Ext::ShowGrid::OnGameStart
#include "ext/fast_paths/fast_paths.h"   // EXT SEAM (NOT RETAIL, docs/extensions/handins/FASTPATHS.md): Ext::FastPaths::SelectForGame
#include "ext/path_workers/path_workers.h"   // EXT SEAM (NOT RETAIL, docs/extensions/handins/PATHWORKERS.md): Ext::PathWorkers::SelectForGame
#include "load_time.h"   // [LoadTime] stamps (NOT RETAIL, print-only; block LOADSTAMP)

// ============================================================
// Externs from sibling reimpl TUs (already registered/defined)
// ============================================================
extern "C" void  WorldGen_BuildTerrain(WorldGeometry* wg, TerrainGenerator* gen, int sizeIdx, uint32_t seed); // worldgen.cpp (M5 5b)
extern "C" void  MapTileGrid_Build(Map* game, int dim);  // maptilegrid.cpp (M5 5c — tile grid Game+0x80)
extern "C" void  Pathfinder_Seed(Map* game, int dim);     // pathfinder_seed.cpp (task #34 — pathfinder @ game+0x68)
extern "C" void  Region_Seed(Map* game, int dim);         // region_seed.cpp (keystone A — region map @ game+0x70)
extern "C" void  CharManager_Seed();                      // charmanager_seed.cpp (move-tick tail — char spatial-hash mgr @ GW+0x88)
extern "C" void  StartTown_ComputeCamera(MapGenerator* startConfig, Map* game, float camPosOut[3]); // starttown.cpp (M5 5c — camera)
extern "C" void  StartTown_SpawnEntities(MapGenerator* startConfig, GameUpdateLoop* world);                  // starttown.cpp (M5 5c — ECS spawn)

// ============================================================
// Load-specific Runtime resolves not yet in engine_funcs.h
// ============================================================

// --- Step 32 (AchievementInstance) -----------------------------------------
// (The instance is the real class and SetAchievement a member — ENDGAME B.3.8 /
// B.3.7.) The Achievement type id is Achievement::Achievement::TypeId()
// (ENDGAME B.3.9 TR: was a by-name resolve of the Runtime record, cached in a
// namespace-scope atomic).

// B3R: the Runtime ctor/dtor doors Load used to resolve by name are called by name:
// System::Random(uint32_t) (system/Value.h), RtPicking_Scene_Ctor (picking/Scene.h),
// RtUi_UserInterface_Ctor (ui/UserInterface.h), RtGraphics_AnimationManager_Ctor
// (graphics/Animation.h), RtSave_SaveLoad_Ctor / RtSave_SavedState_Dtor
// (save/SaveLoad.h). The "[GUL::Load] resolves:" stderr line went with the cache.

// src/game/Game/gameupdateloop_serialize.cpp — the serializer roundtrip check;
// the LOAD-FROM-SAVE branch below calls SaveRestore (slot 7).
extern "C" void GameUpdateLoop_SaveRoundtripCheck(GameUpdateLoop* self, Save::SavedState* loadedState);

// src/game/Game/eventlog_achievement.cpp — the achievement award TOAST (exe
// 0x140096960). Installed on the AchievementManager at step 30 below.
extern "C" void EventLog_OnAchievementAwarded(int achievementId);

// ============================================================
// Step 2 — ProfessionList load (WaveH2; env BANISHED_LIVE_PROFESSIONLIST)
// ============================================================
// The original GameUpdateLoop::Load (step 2, @0x140096baa) loads the ProfessionList
// from "Game/Profession/Profession.rsc" via Object::ExternalList::Find and stores it
// at +0xB0 (param_1[0x16]). This single object is the CONVERGENT KEYSTONE for the
// whole citizen economy: it carries the DEFAULT PROFESSION at +0x30 (→ citizen+0x1f0
// at spawn, FUN_140117080) and profRef2 at +0x38 (the workplace-registration gate key
// in WorkPlaceComponent_impl_f670). Every economy path (housing/workplace finders,
// citizen spawn) derefs gw+0xb0; with it null in standalone they all bail/crash.
//
// The ProfessionList External (vtable 0x14015ef50, factory 0x14001e5c0) + its Serialize
// (loads the 4 ExternalPtr<Profession> refs) are already ported (professionlist.cpp);
// they were just never DRIVEN. We load it with ExternalFactory::FindOrLoad — the same
// primitive StartTown_ResolveTemplate / LoadDialogResource use successfully — rather
// than ExternalList::Find(param_1[1]); the global-factory load works in standalone and
// Profession resource resolution is already proven (CropField/Orchard/Pasture).
// The ProfessionList keystone loads crash-free (the real blocker was a missing
// fn-pointer null-check in GameUpdateLoop_Startup, not the object — see
// professionlist.cpp). gw+0xb0 carries the default profession (+0x30) + profRef2
// (+0x38) the citizen-economy finders key on.

static void LoadProfessionList_Step2(GameUpdateLoop* self) {

    if (!::Object::ExternalFactory::_singleton) {
        self->m_professionList = nullptr;
        fprintf(stderr, "[GUL::ProfList] SKIP — factory unavailable (factory=%p)\n",
                ::Object::ExternalFactory::_singleton);
        fflush(stderr);
        return;
    }

    // ENDGAME B.3.3 step 6A: member call on the real Object::ExternalFactory.
    ::Object::ExternalFactory* efactory = ::Object::ExternalFactory::_singleton;
    System::Filename fname("Game/Profession/Profession.rsc");
    ::Object::External* profList =
        efactory->FindOrLoadAs<::Object::External>(fname);
    self->m_professionList = static_cast<ProfessionList*>(profList);   // unchecked, as before

    ProfessionList* profs = self->m_professionList;
    Profession* defProf  = profs ? profs->field_0x30 : nullptr;
    Profession* profRef2 = profs ? profs->field_0x38 : nullptr;
    fprintf(stderr, "[GUL::ProfList] gw+0xb0=%p defaultProf(+0x30)=%p profRef2(+0x38)=%p\n",
            profList, defProf, profRef2);
    fflush(stderr);
}

// ============================================================
// Step 4 — HelpTopics load (help-dialog bring-up 2026-07-31)
// ============================================================
// Retail (0x140096c80..0x140096cdd) does
//   ExternalList::Find(param_1[1], &out, "Game/Help/HelpTopics.rsc", ExternalList typeId)
// and stores the result at +0xA0 — an Object::ExternalList whose resources are the
// per-topic UI::StringTables (text 0 = link key, 1 = combo title, 2 = page body).
// That store was deferred with the rest of the step 1-4 Finds, so gw+0xa0 stayed null
// and HelpUI::Slot4 populated an empty combo — the reason the in-game help dialog
// renders blank (verified live: `[HelpUI] Slot4 ... topics=0000000000000000`).
//
// Loaded through ExternalFactory::FindOrLoad rather than ExternalList::Find on the
// resource list at +0x08, for the same reason as ProfessionList above: the global
// factory load is the primitive that is proven to work in standalone. The
// IsDerivedFrom(ExternalList) check is retail's (it stores null on a type mismatch).
// ============================================================
// Step 3 — UI material (+0xB8): "Material/UI/UIMaterialNoBlend.rsc"
// ============================================================
// Retail step 3 FindOrLoads the no-blend UI material and stores the
// ExternalPtr<Material> at gw+0xB8. It was the last deferred step 1-4 Find.
// Sole consumer today is GUL::Draw step 8 — SaveLoad::Draw(ctx, material) —
// which only runs while a save/load operation is active; with the slot null,
// SaveLoad::Draw crashed in SpriteContext::Flush -> MaterialInstance::Setup
// (read of 0+0x28) the frame after an autosave started (2026-08-27).
static void LoadUIMaterial_Step3(GameUpdateLoop* self) {
    self->m_uiMaterial = nullptr;

    if (!::Object::ExternalFactory::_singleton)
        return;

    System::Filename fname("Material/UI/UIMaterialNoBlend.rsc");
    // ENDGAME B.3.3 step 6A: member call on the real Object::ExternalFactory.
    ::Object::External* mat = ::Object::ExternalFactory::_singleton
            ->FindOrLoadAs<::Object::External>(fname);

    // Retail's type gate: Graphics::Material's type id (ENDGAME B.3.9 TR: the class
    // accessor; was an RT_DATA_SLOT read of its ?_typeInfo@ record).
    bool isMat = false;
    if (mat)
        isMat = ::Object::IsDerivedFromNullSafe(mat, Graphics::Material::TypeId());
    if (isMat) self->m_uiMaterial = static_cast<Graphics::Material*>(mat);

    fprintf(stderr, "[GUL::UIMat] gw+0xb8=%p (loaded=%p isMaterial=%d)\n",
            self->m_uiMaterial, mat, (int)isMat);
    fflush(stderr);
}

static void LoadHelpTopics_Step4(GameUpdateLoop* self) {
    self->m_helpTopics = nullptr;

    if (!::Object::ExternalFactory::_singleton) {
        fprintf(stderr, "[GUL::Help] SKIP — factory unavailable (factory=%p)\n",
                ::Object::ExternalFactory::_singleton);
        fflush(stderr);
        return;
    }

    System::Filename fname("Game/Help/HelpTopics.rsc");
    // ENDGAME B.3.3 step 6A: member call on the real Object::ExternalFactory.
    ::Object::External* list = ::Object::ExternalFactory::_singleton
            ->FindOrLoadAs<::Object::External>(fname);

    // Retail's type gate: Object::ExternalList's type id (retail reads it through the
    // exe import slot 0x140151ef0; ENDGAME B.3.9 TR: the class accessor).
    bool isList = false;
    if (list)
        isList = ::Object::IsDerivedFromNullSafe(list, ::Object::ExternalList::TypeId());
    if (isList) self->m_helpTopics = static_cast<::Object::ExternalList*>(list);

    fprintf(stderr, "[GUL::Help] gw+0xa0=%p (loaded=%p isExternalList=%d topics=%d)\n",
            self->m_helpTopics, list, (int)isList,
            isList ? self->m_helpTopics->GetResourceCount() : -1);
    fflush(stderr);
}

// ============================================================
// Step 1 — Diseases load (citizen-plague bring-up 2026-08-22)
// ============================================================
// Retail's step 1 does ExternalList::Find(param_1[1], &out,
// "Game/Disease/Diseases.rsc", ExternalList typeId) and stores the result at +0xA8.
// The string is in the exe at 0x14015e428. That store was deferred with the rest of
// the step 1-4 Finds, and it is the SINGLE reason no citizen can ever fall ill:
// CitizenComponent::CatchDisease (0x140112620, citizen_updatehealth.cpp:378-379)
// reads gw+0xA8 and returns immediately when it is null. Everything downstream —
// CatchDisease -> Infect -> SpreadDisease -> FindCareBuilding -> RecoverOrDie — is
// already ported, registered and wired; it has simply never had a disease to draw.
// DebugUI's citizen-plague trigger (0x14005ff30) reads the same slot.
//
// Modelled on LoadHelpTopics_Step4 rather than LoadProfessionList_Step2: gw+0xA8 is
// an Object::ExternalList (CatchDisease walks `*(list+0x20)` by index and calls
// ExternalList::GetResourceCount on it), exactly like gw+0xA0 — whereas gw+0xB0 is a
// ProfessionList External. Same FindOrLoad primitive, same retail type gate.
//
// Retail always performs this Find — leaving gw+0xA8 null is
// a deviation, and it silently deletes every reader of the list: CatchDisease returns
// immediately, and DebugUI::Slot4's diseaseCombo loop is bounded by
// ExternalList::GetResourceCount(null-guarded to 0), so the debug panel's Disaster tab
// showed an EMPTY disease dropdown with no error anywhere.

static void LoadDiseaseList_Step1(GameUpdateLoop* self) {
    self->m_diseaseList = nullptr;

    if (!::Object::ExternalFactory::_singleton) {
        fprintf(stderr, "[GUL::DiseaseList] SKIP — factory unavailable (factory=%p)\n",
                ::Object::ExternalFactory::_singleton);
        fflush(stderr);
        return;
    }

    System::Filename fname("Game/Disease/Diseases.rsc");
    // ENDGAME B.3.3 step 6A: member call on the real Object::ExternalFactory.
    ::Object::External* list = ::Object::ExternalFactory::_singleton
            ->FindOrLoadAs<::Object::External>(fname);

    // Retail's type gate (same ExternalList type id as the HelpTopics list).
    bool isList = false;
    if (list)
        isList = ::Object::IsDerivedFromNullSafe(list, ::Object::ExternalList::TypeId());
    if (isList) self->m_diseaseList = static_cast<::Object::ExternalList*>(list);

    // B3R: the `fn_ExternalList_GetResourceCount` readiness test went with the door.
    int count = isList ? self->m_diseaseList->GetResourceCount() : -1;
    fprintf(stderr, "[GUL::DiseaseList] gw+0xa8=%p (loaded=%p isExternalList=%d diseases=%d)\n",
            self->m_diseaseList, list, (int)isList, count);

    // Per-entry tuning dump. Disease+0x40 is the RECIPROCAL mortality — RecoverOrDie
    // computes (1/x)/careMult, so a zero here is an infinite death chance (every sick
    // citizen dies the instant the illness runs out). The factory default IS 0
    // (disease.cpp:82), so an entry reading 0 means the .rsc never wrote the field.
    // Report it; do not clamp it — that would hide a real load failure.
    if (isList && count > 0) {
        ::Object::External** arr = self->m_diseaseList->_resources;   // list+0x20
        for (int i = 0; i < count && arr; i++) {
            // The list's entries are the Disease resources (Diseases.rsc).
            const Disease* d = static_cast<const Disease*>(arr[i]);
            if (!d) { fprintf(stderr, "[GUL::DiseaseList]   [%d] NULL entry\n", i); continue; }
            fprintf(stderr, "[GUL::DiseaseList]   [%d] %p spreadMonths(+0x30)=%.3f "
                    "contagion(+0x34)=%.3f radius(+0x38)=%.3f weight(+0x3c)=%.3f "
                    "recipMortality(+0x40)=%.3f%s\n",
                    i, (const void*)d, d->_field30, d->_field34,
                    d->_field38, d->_field3C, d->_field40,
                    (d->_field40 == 0.0f) ? "  <== ZERO: deathChance is INFINITE" : "");
        }
    }
    fflush(stderr);
}

// ============================================================
// Array-resize leaf helpers (verified, m5-load-ctor-drafts)
// Header layout: +0x00 ptr, +0x08 int count, +0x0C int capacity.
// NB: the stride-8 ZERO-FILL variant (FUN_1400032e0) is reached as
// the typed array's own SetSize (kSpec_PtrZero) — not redefined here.
// ============================================================

// ENDGAME B.3.2 step 2: the five resize bodies in this block (0x1400191f0,
// 0x14002e000, 0x140017640, 0x1400177c0, 0x14000f090) WERE hand-written copies
// of `rtcoll::SetSizeCore` (src/engine/collection/array.cpp). Each is now one
// `Collection::Array<>::SetSize` driven by an `ArraySpec` row spelling out the
// site's element ops. Compared arm for arm before the swap: exact-equal early
// return, `_size` stored before the capacity test,
// treble-on-single-append-from-non-empty, copy the preserved prefix only when
// the block moved, init the new tail (or leave it — 0x1400191f0), destroy the
// truncated tail in place / the whole old buffer on a realloc, then free. The
// one deliberate difference: the allocation now goes through the LOCKING
// static `MemAlloc` (0x1800c7a50) instead of the unlocked instance `Allocate`
// these bodies reached through `fn_MemAlloc` — same allocator, same heap, plus
// the critical section retail's own Collection code takes. The 0x14000f9e0
// deep copy is an `operator=` (rtcoll::ArrayAssignCore), not a grow, and is
// kept as the element-assign of the 0x140017640 row.
namespace {

// 0x1400191f0 element: one 8-byte pointer, align 8. Copy = 8-byte copy; the
// new tail is left UNINITIALISED (retail's empty else arm) — init is null on
// purpose; trivially destructible.
void AssignPtr8(uint8_t* d, const uint8_t* s) {
    rtcoll::AssignAs<uint64_t, &rtcoll::CopyWords<uint64_t, 1>>(d, s);
}

// 0x14002e000 element: camera view {Vector3 pos, float a, float b, float c,
// bool used}, stride 0x1c, align 4. Copy writes +0x00..+0x18 (the 4x-unrolled
// dword loop plus the bool) and leaves +0x19..+0x1b; init writes the same
// bytes with zero. Partial copy, so byteCopy=false; trivially destructible.
// The element is PlayerCameraView (Game/PlayerToolManager.h), reached through
// Collection.h's element adapters.
void CopyCamView(PlayerCameraView* d, const PlayerCameraView* s) {
    d->position.x = s->position.x;   // +0x00
    d->position.y = s->position.y;   // +0x04
    d->position.z = s->position.z;   // +0x08
    d->field_0x0C = s->field_0x0C;
    d->field_0x10 = s->field_0x10;
    d->field_0x14 = s->field_0x14;
    d->used = s->used;               // +0x18, the raw byte
}
void ZeroCamView(PlayerCameraView* d) {
    // retail: three qword zero stores over +0x00..+0x17 (H7: six named stores)
    d->position.x = 0.0f;
    d->position.y = 0.0f;
    d->position.z = 0.0f;
    d->field_0x0C = 0.0f;
    d->field_0x10 = 0.0f;
    d->field_0x14 = 0.0f;
    d->used = false;
}
void AssignCamView(uint8_t* d, const uint8_t* s) {
    rtcoll::AssignAs<PlayerCameraView, &CopyCamView>(d, s);
}
void InitCamView(uint8_t* d) {
    rtcoll::InitAs<PlayerCameraView, &ZeroCamView>(d);
}

}  // namespace

// WeatherSimulation::effects' row (WeatherSimulation.h).
const rtcoll::ArraySpec WeatherSimulationRows::kSpec_Ptr8NoInit = {   // exe 0x1400191f0
    "Array<void*> (no tail init)", 8, 8,
    &AssignPtr8, nullptr, nullptr, true,
    /*assign unwritten*/ 0, 0,
    /*init   unwritten*/ 0, 0,
    /*seed zero       */ 0, 0 };

// PlayerToolManager::cameraViews' row (PlayerToolManager.h).
const rtcoll::ArraySpec PlayerToolManagerRows::kSpec_CamView = {   // exe 0x14002e000
    "Array<CameraView>", 0x1c, 4,
    &AssignCamView, &InitCamView, nullptr, false,
    /*assign unwritten*/ 0x19, 0x1c,
    /*init   unwritten*/ 0x19, 0x1c,
    /*seed zero       */ 0,    0 };




// (B4 X5-COMP: the three CitizenBehavior element rows this file carried —
// 0x140017640 with 0x14000f9e0, 0x1400177c0, 0x14000f090 — and their Reimpl_
// resize bridges are the class's members' rows now: CitizenBehaviorRows,
// Entity/CitizenBehavior.h.)

// ============================================================
// Subsystem constructors (verified bodies)
// ============================================================

// 0x1400983f0 — the look-at transform builder. Returns the transform through
// retail's hidden return slot and stores it into +0x00.
Math::Transform PlayerController::CalcTransform() {
    // ?FromYawPitch@Vector3@Math@@SA?AV12@AEBVScalar@2@0@Z  retail 0x1800220b0
    Math::Vector3 dir = Math::Vector3::FromYawPitch(yaw, pitch);
    // The camera focus point: the three target floats at +0x1c.
    const Math::Vector3 pos(target.x, target.y, target.z);
    Math::Vector3 eye(pos.x - dir.x * distance,
                      pos.y - dir.y * distance,
                      pos.z - dir.z * distance);
    // ?FromLookAt@Transform@Math@@SA?AV12@AEBVVector3@2@0@Z  retail 0x18007dbb0
    Math::Transform lookAt = Math::Transform::FromLookAt(eye, pos);
    // +0x00 is the Math::Transform member (GAME-b).
    transform = lookAt;   // self[+0x00..+0x1b]
    return lookAt;
}

// 0x1400985b0 — the PlayerController ctor (+0xE0, 0x10c). Retail's sequence:
// Transform::Transform(+0x00) (0x1400985d6), qword 0 at +0x1c and dword 0 at
// +0x24 (H7: three named stores), yaw / pitch / distance (0x1400985f3..fd),
// CalcTransform 0x1400983f0, AsTransform3 (0x140098612), and LAST
// Camera::Camera(+0x34, frustum, t3, true) (0x140098625): the init list in
// member order, the camera its last entry.
PlayerController::PlayerController(const ::Frustum& frustum, float yaw_, float pitch_, float distance_)
    : transform(),
      target(0.0f, 0.0f, 0.0f),
      yaw(yaw_), pitch(pitch_), distance(distance_),
      camera(frustum, CalcTransform().AsTransform3(), true) {
}

// (0x1400c3f90, the EntityList ctor, is EntityList::EntityList since B.4
// X4-COMP, and 0x140090a30, the CitizenBehavior ctor, CitizenBehavior::
// CitizenBehavior since B.4 X5-COMP: Load's placement new runs them.) The classes below are trivially
// constructible: Load begins their lifetime with a placement new (no code) and
// runs their ctor body on it.
static_assert(std::is_trivially_default_constructible<Calendar>::value,
              "placement new of Calendar runs no code");
static_assert(std::is_trivially_default_constructible<MusicPlayer>::value,
              "placement new of MusicPlayer runs no code");

// 0x14009ee70 — the Map ctor (+0xF8, 0xf8). Retail's stores, in order: the
// Picking::Scene import on +8 (0x14009ee8a); qword 0 at +0x68 / +0x70 / +0x78;
// the cell Array header (+0x80, +0x88); byte 1 at +0xA2; the cell queue
// (0x14009eec5..eee3); the +0xE8 tree root / count; +0x90 / +0x98 / +0xA0 (the
// nine shorts). The members in declaration order give the first six; the body
// writes the last two. Retail writes no padding (+0x04, +0xA3..+0xA7, +0xF4).
Map::Map()
    : pickingScene(),
      pathfinder(nullptr),
      regionGraph(nullptr),
      regionPairCache(nullptr),
      cells(),
      field_0xA2(1),
      cellQueue() {
    field_0xE8.root = nullptr;                // +0xE8 qword 0
    field_0xE8.count = 0;                     // +0xF0 dword 0
    for (int i = 0; i < 9; ++i)               // +0x90 / +0x98 qwords, +0xA0 word
        field_0x90[i] = 0;
}

// 0x1400f9180 — the WorkScheduler ctor's body (+0x118, 0x138).
static_assert(std::is_trivially_default_constructible<WorkScheduler>::value,
              "placement new of WorkScheduler runs no code");
static void WorkScheduler_Construct(WorkScheduler* scheduler) {
    memset(scheduler, 0, sizeof(WorkScheduler));   // zeroes header + the 15x16 array @+0x30
    scheduler->stepCount = 1;
}

// 0x140093e30 — binds the environment and the climate into the weather sim.
// (env may be null on the deferred path.)
void WeatherSimulation::Bind(EnvironmentDescription* env, ClimateGenerator* climateGen) {
    environment = env;
    climate = climateGen;
    if (env) {
        int count = env->_weatherArray._size;    // env+0x30
        effects.SetSize(count);    // 0x1400191f0
        emitters.SetSize(count);   // 0x1400032e0
        int n0 = effects.GetSize();
        Graphics::Geometry** arr0 = effects._data;
        for (int i = 0; i < n0; ++i) arr0[i] = nullptr;
    }
    if (climateGen) {
        float v = climateGen->temperatureBase;   // climate+0x34
        field_0x2C = v;
        field_0x30 = v;
    }
}

// FUN_1400182b0 - CharacterGrid ctor (+0x88, 0x60). A loose Morton (Z-order)
// quadtree over characters, NOT a SkyMesh transform (that older name came from a
// wrong plate comment on retail Load; corrected 2026-08-30 - see
// engine/types/GameUpdateLoop.h +0x88).
// Retail zeroes +0x04..+0x5F with qword stores (the five Arrays' ctors and the centre
// pair), then stores worldSize, levels = 8 and the centre (param2 * 0.5).
CharacterGrid::CharacterGrid(float param2)
    : worldSize(param2), centerX(param2 * 0.5f), centerY(param2 * 0.5f), levels(8) {
    CharacterGrid* grid = this;
    grid->levelOffsets.SetSize(8);   // 0x1400021d0
    {
        int32_t* lod = grid->levelOffsets._data;
        if (lod) {
            lod[0] = 0x0; lod[1] = 0x1; lod[2] = 0x5; lod[3] = 0x15;
            lod[4] = 0x55; lod[5] = 0x155; lod[6] = 0x555; lod[7] = 0x1555;
        }
    }
    grid->cellHead.SetSize(0x5555);        // 0x1400021d0
    grid->cellOccupancy.SetSize(0x5555);   // 0x1400021d0
    {
        int32_t* aFF = grid->cellHead._data;
        int32_t* aZ = grid->cellOccupancy._data;
        if (aFF && aZ) {
            for (int idx = 0; idx < 0x5555; ++idx) { aFF[idx] = (int32_t)0xffffffff; aZ[idx] = 0; }
        }
    }
}

// 0x140098660 — PlayerController::UpdateCamera (builds the projection frustum on
// the camera at +0x34). Draw and Load step 33 call it.
void PlayerController::UpdateCamera(float fovY, float nearZ, float farZ) {
    int width = 1920, height = 1080;          // default 16:9 if viewport singleton unavailable
    {
        const ::Video::Device* vp = ::Video::Device::_singleton;
        if (vp) { width = vp->backBufferWidth; height = vp->backBufferHeight; }   // +0x04 / +0x08
    }
    const float kAspectThresh = 1.7777778f, kHalf = 0.5f, kDeg2Rad = 0.017453292f,
                kTwo = 2.0f, kRad2Deg = 57.29578f;
    float aspect = (height != 0) ? (float)width / (float)height : kAspectThresh;
    float fovX = aspect * fovY;
    if (aspect > kAspectThresh) {
        float t = tanf(fovY * kAspectThresh * kHalf * kDeg2Rad);
        float den = (height * kAspectThresh * kHalf) / t;
        float halfW = (float)(width / 2);
        fovX = atanf(halfW / den) * kTwo * kRad2Deg;
    }
    const Math::Scalar nearS(nearZ), farS(farZ), fovXS(fovX), fovYS(fovY);
    // ??0Frustum@Volume@@QEAA@AEBVScalar@Math@@000@Z 0x1800fa1e0 on a stack local
    // (its Vector4 planes are zeroed inside that ctor, as retail's are).
    const ::Frustum frustum(nearS, farS, fovXS, fovYS);
    camera.SetFrustum(frustum);
}

// 0x1401025c0 — the PlayerToolManager ctor (+0x120, 0xb0). The two InternalPtrs
// (+0x00, +0x28) are declared as plain pointers (H5); their null ctor is the
// null store.
PlayerToolManager::PlayerToolManager() {
    field_0x00 = nullptr;
    toolRegistry.root = nullptr;
    toolRegistry.count = 0;
    field_0x20 = 0;
    // +0x28 currentTool: the member's InternalPtr ctor stores the null (0x1401025fe).
    paused = false;
    field_0x34 = 1;
    field_0x38 = false;
    field_0x3C = 0.0f;                        // +0x3c / +0x40: one qword store (H7: two stores)
    field_0x40 = 0.0f;
    field_0x44 = 0.75f;
    field_0x48 = 0;
    field_0x50.root = nullptr;
    field_0x50.count = 0;
    field_0x60.root = nullptr;
    field_0x60.count = 0;
    // +0x70: the Array member's own ctor zeroes data / count / capacity.
    field_0x80 = false;
    field_0x88 = nullptr;
    field_0x90 = nullptr;
    field_0x98 = -1;                          // +0x98 / +0x9c: one qword store of -1
    field_0x9C = -1;
    field_0xA0 = -1;
    field_0xA4 = false;
    cameraViews.SetSize(5);   // 0x14002e000
}

// ============================================================
// Small allocation helper
// ============================================================
// The block is returned as the class the caller builds in it (`void` for the
// ones built through a POD-mirror ctor or a placement new).
template <class T>
static inline T* AllocSub(uint64_t size, int align) {
    if (!System::MemoryAllocator::_singleton) return nullptr;
    return static_cast<T*>(System::MemoryAllocator::MemAlloc(size, align));
}

// ============================================================
// Step 28 helper — SkyMesh MeshInstance (+0x108): the sky `_clear_` dome.
// ============================================================
//
// Faithful port of the exe Load's sky-build (0x140097356..0x140097420):
// MemAlloc a Graphics::MeshInstance (0x120 bytes) and construct the REAL
// Runtime class on it (ENDGAME B.3.8 LOOP, B38_RULES D8: retail runs the
// imported ctor, then stamps the exe's 9-slot COPY of the table, 0x14015e240 —
// slot 0 the exe scalar deleting dtor 0x140019e80, slots 1-8 IAT thunks into
// the Runtime bodies — an artefact of the inlined ctor, not an exe class; the
// class's own DeletingDtor is that exe body since block RB), FindOrLoad
// "Models/Sky/SkyMesh.rsc", SetMesh
// it, run Startup via vtable[+8](skyMesh, Time(0)), zero +0x28. The resource
// loads in standalone with no factory cascade (SkyMesh GraphicsMesh is
// Runtime-self-registered). GameUpdateLoop::Shutdown already tears m_skyMesh
// down via vtable[0].
//
// This is the fix for "dark/flat water": without the sky as a Scene global
// object, the engine's reflection pass has only terrain to mirror (reflection
// RT blue mean ~0.18 vs retail ~0.68), so the water — whose shader is
// sky-reflection-dominated on the tilted camera — renders dark navy. Adding the
// sky as a Scene global object (step-33 tail) feeds the reflection pass for
// free, exactly like retail. Opt out for A/B with BANISHED_NO_SKYMESH=1.
// LOADER THREAD: no function-local resolve cache (B6-5) — the ctor is the
// class's, and Time(float) is engine_funcs' boot-resolved fn_Time_Ctor_Float
// (the same export, `??0Time@System@@QEAA@M@Z`).
static void BuildSkyMesh_Step28(GameUpdateLoop* self) {
    void* mem = AllocSub<void>(0x120, 8);
    if (!mem) {
        self->m_skyMesh = nullptr;
        fprintf(stderr, "[GUL::SkyMesh] SKIP — mem=%p\n", mem);
        fflush(stderr);
        return;
    }
    // Retail stores the loop's +0x108 AFTER the MeshInstance ctor and the table
    // stamp (ctor 0x14009736b, stamp 0x140097378, store 0x140097380), before
    // the SkyMesh.rsc Find: the main thread never sees an unconstructed sky.
    Graphics::MeshInstance* sky = ::new (mem) Graphics::MeshInstance();
    self->m_skyMesh = sky;

    // Load the sky dome resource (the `_clear_`-material mesh) via the global factory.
    Graphics::GraphicsMesh* meshRes = nullptr;
    if (::Object::ExternalFactory::_singleton) {
        // ENDGAME B.3.3 step 6A: member call on the real Object::ExternalFactory.
        ::Object::ExternalFactory* efactory = ::Object::ExternalFactory::_singleton;
        System::Filename fname("Models/Sky/SkyMesh.rsc");
        meshRes = efactory->FindOrLoadAs<Graphics::GraphicsMesh>(fname);
    }

    if (meshRes)
        sky->SetMesh(Object::ExternalPtr<Graphics::GraphicsMesh>(meshRes));

    // Startup: vtable[+8](skyMesh, Time(0)) — matches the original Load's
    // `(*(code**)(*skyMesh + 8))(skyMesh, &Time0)`.
    {
        System::Time timeObj;
        timeObj._ticks = 0;
        RtSystem_Time_Ctor(&timeObj, 0.0f);
        // vtable+0x08 is slot 1 of `Graphics::Geometry`, i.e.
        // `Update(const System::Time&)`. The receiver is a PROVED MeshInstance:
        // 0x120 bytes from AllocSub, run through `??0MeshInstance@Graphics@@` above.
        Graphics::Geometry* skyGeometry = sky;
        skyGeometry->Update(timeObj);
    }
    sky->_detailId = 0;   // +0x28

    fprintf(stderr, "[GUL::SkyMesh] built skyMesh=%p meshRes=%p\n",
            mem, meshRes);
    fflush(stderr);
}

// ============================================================
// GameUpdateLoop::Load (0x140096b00) — orchestration
// ============================================================

// World-build completion signal. GameUpdateLoop::Load runs on the Runtime
// Engine::Loader thread spawned inside fn_DialogLoaderLoopT_Startup -- the
// only dispatcher there is (see the guard below). The loading-screen loop
// (splash_loop_stubs.cpp) polls GameUpdateLoop_IsWorldBuilt(next) to hold the
// loading screen up until the build genuinely finishes, so GameUpdateLoop::Draw
// never renders a half-built world (torn scene reads) and the screen shows a
// real loading screen + tips for the whole build, like retail. Distinct from the
// s_loadingLoop *ownership* guard below (which is set at build START).
static std::atomic<void*> s_worldBuiltLoop{nullptr};

extern "C" bool GameUpdateLoop_IsWorldBuilt(const Engine::UpdateLoop* loop) {
    return loop != nullptr &&
           s_worldBuiltLoop.load(std::memory_order_acquire) == loop;
}

// ENDGAME B.3.8 LOOP: slot 6 of the real class (was the hand-built table's
// GameUpdateLoop_Load). Caller batch CB6: the fields are the class's named
// members; `loopVoid` is only the build guard's / log's pointer value.
void GameUpdateLoop::Load() {
    void* const loopVoid = this;
    // Single-owner build guard — NOW AN ASSERTION, NOT A RACE FIX.
    //
    // ENDGAME B.3.4a closing, comment only. This block used to describe the
    // loop's Load (+0x30) being dispatched TWICE, concurrently, on the SAME
    // GameUpdateLoop (confirmed 2026-06-17, two distinct tids per game-load):
    //   (1) our standalone AsyncLoader worker, from Stub_Startup step 3; and
    //   (2) the Runtime Engine::Loader thread, spawned inside
    //       fn_DialogLoaderLoopT_Startup's Loader::Load(m_nextLoop).
    // Both ran identical idempotent build code, so the OUTCOME was correct
    // whichever won, but unguarded they double-spawned into the entity buckets
    // (null entries -> crash in EntityList_Update). The note that stopping
    // dispatcher (1) at its source was "blocked" is obsolete.
    //
    // DISPATCHER (1) IS GONE. `standalone/async_loader.{h,cpp}` were deleted in
    // 96c17f47 and `Engine::Loader` is the only dispatcher. So this guard no
    // longer resolves a race; it ASSERTS that there is only one. A
    // "[GUL::Load] redundant dispatch" line reappearing in the log means a
    // SECOND DISPATCHER IS BACK -- treat it as a regression to find, not as the
    // guard doing its job. It is kept for exactly that reason.
    static std::atomic<void*> s_loadingLoop{nullptr};
    const uint32_t _tid = Platform::CurrentThreadId();
    if (s_loadingLoop.exchange(loopVoid) == loopVoid) {
        fprintf(stderr, "[GUL::Load] redundant dispatch for loop=%p tid=%u — "
                "rejected (loop already owned/built by the first dispatcher)\n", loopVoid, _tid);
        fflush(stderr);
        return;
    }
    fprintf(stderr, "[GUL::Load] build owner = tid=%u for loop=%p\n", _tid, loopVoid);
    fflush(stderr);

    LoadTime::Stamp("gul.load", m_savedState ? "save" : "new");   // NOT RETAIL: print-only
    fprintf(stderr, "[GUL::Load] ENTER self=%p seed=%d mapSize=%d climateGen=%p terrainGen=%p saved=%p\n",
            loopVoid, m_seed, m_mapSize,
            m_climateConfig, m_terrainConfig, m_savedState);
    fflush(stderr);

    // GameUpdateLoop::_singleton = this loop (0x140096b35; read by AddGlobalObject /
    // Audio::Pause at the tail).
    _singleton = this;

    // EXT SEAM (NOT RETAIL, docs/extensions/handins/EXTSCAFFOLD.md): the class lock (DESIGN.md §2) -- a new game or a
    // load takes the class B / C lock for this loop's lifetime. A no-op on the
    // game while no B / C extension is registered.
    Ext::LockForGame(this);
    // EXT SEAM (NOT RETAIL, docs/extensions/handins/RESTRICTEDFOODS.md): a class C extension's per-town state
    // starts empty; a load's tail record decodes it
    Ext::SaveTail::ResetAllState();
    // EXT SEAM (NOT RETAIL, docs/extensions/handins/SHOWGRID.md): ext.show_grid (class A) starts every
    // game with its grid hidden.
    Ext::ShowGrid::OnGameStart();
    // ext.fast_paths (class B) reads the lock here; final for a new game, a
    // load's manifest replaces it in SaveRestore and the second call below
    // re-reads it (src/ext/fast_paths/fast_paths.h).
    Ext::FastPaths::SelectForGame();
    Ext::PathWorkers::SelectForGame();   // ext.path_workers (class B), the same rule

    // Steps 1-4: resource Finds (Diseases/Profession/Material/Help). Only step 3
    // (UI material) is still DEFERRED (left null; consumers guarded).
    //   step 1 Diseases    → +0xA8, opt-in BANISHED_LIVE_DISEASELIST (unblocks citizen illness)
    //   step 2 Professions  → +0xB0, the citizen-economy keystone (BANISHED_LIVE_PROFESSIONLIST)
    //   step 4 HelpTopics   → +0xA0, consumed only by the help dialog
    LoadTime::Stamp("gul.resources");   // NOT RETAIL: print-only (steps 1-4: the resource Finds)
    LoadDiseaseList_Step1(this);       // step 1 → +0xA8 (diseases); env-gated, else null
    LoadProfessionList_Step2(this);    // step 2 → +0xB0 (professions); env-gated, else null
    LoadUIMaterial_Step3(this);        // step 3 → +0xB8 (UI material — SaveLoad::Draw overlay)
    LoadHelpTopics_Step4(this);        // step 4 → +0xA0 (help topics)
    LoadTime::Stamp("gul.subsystems");   // NOT RETAIL: print-only (steps 5-27: the subsystem objects)

    // Step 5 — the gameplay Random (+0xD8, 0x9c4) seeded from gameLoop+0x30.
    //
    // ⚠ CORRECTED 2026-07-31: this said "seeded with mapSizeIndex (+0x30)". WRONG, and it
    // cost real time. `+0x30` is THE MAP SEED — `newgamedialog.cpp:718` writes the seed
    // there and `:719` writes terrainSize to `+0x34`. So the full chain is
    //   BANISHED_SEED -> seed UI element (newgamedialog.cpp:229-236)
    //                 -> gameLoop+0x30   (newgamedialog.cpp:718)
    //                 -> Random(seed) at gw+0xD8   (here)
    // The gameplay RNG IS seeded. Do NOT "fix" this by pinning gw+0xD8 — it is already
    // pinned, and an investigation was launched on the belief that it wasn't.
    //
    // Runs with an identical seed still diverge (gender splits 14/13, 15/12, 13/14), so
    // the divergence is in the ORDER or COUNT of RNG consumption, or a second entropy
    // source — NOT in seeding. Leading suspect: iteration over a container keyed by heap
    // POINTERS (the profession BST is confirmed pointer-keyed), which ASLR reorders.
    {
        System::Random* mem = AllocSub<System::Random>(0x9c4, 4);
        if (mem) ::new (mem) System::Random(static_cast<uint32_t>(m_seed));   // ??0Random@System@@QEAA@I@Z 0x1800c7ed0
        m_random = mem;
    }

    // Step 6 — PlayerController/Camera (+0xE0, 0x10c). Initial frustum (overwritten by step 33).
    {
        int width = 1920, height = 1080;
        {
            const ::Video::Device* vp = ::Video::Device::_singleton;
            if (vp) { width = vp->backBufferHeight; height = vp->backBufferWidth; }  // step6 reads +8/+4
        }
        float aspect = (height != 0) ? (float)width / (float)height : 1.7777778f;
        void* mem = AllocSub<void>(0x10c, 4);
        PlayerController* pc = nullptr;
        if (mem) {
            const Math::Scalar nearZ(2.0f), farZ(70.0f), fovX(60.0f), fovY(aspect * 60.0f);
            // ??0Frustum 0x1800fa1e0 on a stack local; no byte-zeroing of the
            // block first (retail has none: the ctor zeroes the planes itself).
            const ::Frustum frustum(nearZ, farZ, fovX, fovY);
            pc = ::new (mem) PlayerController(frustum, 0.0f, -40.0f, 25.0f);   // 0x1400985b0
        }
        m_playerController = pc;
    }

    // Step 7 — Renderer (+0x90, 0x50).
    {
        Graphics::Renderer* mem = AllocSub<Graphics::Renderer>(0x50, 4);
        if (mem) ::new (mem) Graphics::Renderer();   // Renderer() 0x180105180
        m_renderer = mem;
    }

    // Step 8 — EntityList (+0xE8, 0xe8).
    {
        void* mem = AllocSub<void>(0xe8, 4);
        EntityList* list = mem ? new (mem) EntityList : nullptr;   // the ctor 0x1400c3f90
        m_entityList = list;
    }

    // Step 9 — UserInterface (+0x98, 0x128).
    {
        UI::UserInterface* mem = AllocSub<UI::UserInterface>(0x128, 4);
        if (mem) RtUi_UserInterface_Ctor(mem);
        m_userInterface = mem;
    }

    // Step 10 — town/trade manager (+0xF0, 0x1c0) — exe ctor 0x140090a30.
    {
        void* mem = AllocSub<void>(0x1c0, 4);
        CitizenBehavior* cb = mem ? new (mem) CitizenBehavior : nullptr;   // 0x140090a30
        m_citizenList = cb;
    }

    // Step 11 — Game (+0xF8, 0xf8) — CRITICAL: clears the m_game==null Stage-C guards.
    {
        void* mem = AllocSub<void>(0xf8, 8);
        Map* map = mem ? new (mem) Map : nullptr;
        m_game = map;
        StillTerrain::Bump();   // [STILLTERRAIN] a new map (the terrain query's grid)
    }

    // Step 12 — Tutorial-ish (+0x118, 0x138).
    {
        void* mem = AllocSub<void>(0x138, 4);
        WorkScheduler* scheduler = mem ? new (mem) WorkScheduler : nullptr;
        if (scheduler) WorkScheduler_Construct(scheduler);
        m_tutorial = scheduler;
    }

    // Step 13 — EntityUI (+0x120, 0xb0).
    {
        void* mem = AllocSub<void>(0xb0, 4);
        PlayerToolManager* tm = mem ? ::new (mem) PlayerToolManager() : nullptr;   // 0x1401025c0
        m_toolManager = tm;
    }

    // Step 14 — DayCounter Time struct (+0x128, 0x18): flag=1, Time(+8,0.0), Time(+0x10,3600.0).
    {
        void* mem = AllocSub<void>(0x18, 8);
        Calendar* calendar = mem ? new (mem) Calendar : nullptr;
        if (calendar) {
            calendar->year = 1;
            RtSystem_Time_Ctor(&calendar->yearTime, 0.0f);
            RtSystem_Time_Ctor(&calendar->yearLength, 3600.0f);
        }
        m_dayCounter = calendar;
    }

    // Step 15 — Weather sim (+0x130, 0x78).
    {
        void* mem = AllocSub<void>(0x78, 8);
        WeatherSimulation* weather = mem ? ::new (mem) WeatherSimulation() : nullptr;   // 0x140093f60
        m_weather = weather;
    }

    // Step 16 — env config: bind (envRes, climateGen) into the weather sim.
    //
    // envRes = EnvironmentDescription loaded from Environment/Environment.rsc —
    // it holds the 12 lighting keyframes AND the rain/snow Weather descriptor
    // array (envRes+0x28) that the particle engine builds effects from. Retail's
    // GameUpdateLoop::Load FindOrLoads it and passes it to FUN_140093e30; we do
    // the same. A partial/failed load degrades gracefully to null (the weather
    // sim still runs its temperature/snow half).
    if (m_weather) {
        EnvironmentDescription* envRes = nullptr;
        if (::Object::ExternalFactory::_singleton) {
            System::Filename fname("Environment/Environment.rsc");
            // ENDGAME B.3.3 step 6A: member call on the real Object::ExternalFactory.
            // The file's root resource is the EnvironmentDescription.
            envRes = ::Object::ExternalFactory::_singleton
                    ->FindOrLoadAs<EnvironmentDescription>(fname);
            int wxCount = envRes ? static_cast<int>(envRes->_weatherArray._size) : 0;   // env+0x30
            fprintf(stderr, "[GUL::Weather] Environment.rsc envRes=%p weatherDescCount=%d\n",
                    (void*)envRes, wxCount);
            fflush(stderr);
        }
        m_weather->Bind(envRes, /*climateGen*/m_climateConfig);   // 0x140093e30
    }

    // Step 17 — the event log (+0x138, 0x28): retail constructs it inline
    // (0x14009700c..0x14009702c), EventLog::EventLog.
    {
        void* mem = AllocSub<void>(0x28, 4);
        m_eventNotifier = mem ? new (mem) EventLog : nullptr;
    }

    // Step 18 — Graphics::Scene (+0x60, 0x78).
    {
        Graphics::Scene* mem = AllocSub<Graphics::Scene>(0x78, 8);
        if (mem) ::new (mem) Graphics::Scene();   // Scene() 0x180102730
        m_graphicsScene = mem;
    }

    // Step 19 — Audio::Scene (+0x78, 0x38).
    {
        Audio::Scene* mem = AllocSub<Audio::Scene>(0x38, 8);
        if (mem) RtAudio_Scene_Ctor(mem);
        m_audioScene = mem;
    }

    // Step 20 — Picking::Scene (+0x70, 0x60).
    {
        void* mem = AllocSub<void>(0x60, 8);
        // Retail stores the ctor's return (this), as here (0x140096b00).
        m_pickingScene = mem ? RtPicking_Scene_Ctor(mem) : nullptr;
    }

    // Step 21 - CharacterGrid: loose Morton quadtree over characters (+0x88, 0x60).
    {
        void* mem = AllocSub<void>(0x60, 8);
        CharacterGrid* grid = mem ? new (mem) CharacterGrid(2048.0f) : nullptr;   // 0x1400182b0
        m_characterGrid = grid;
    }

    // Step 22 — AnimationManager (+0x68, 0x9d8).
    {
        Graphics::AnimationManager* mem = AllocSub<Graphics::AnimationManager>(0x9d8, 8);
        if (mem) RtGraphics_AnimationManager_Ctor(mem);   // AnimationManager() 0x1800fc430
        m_animationManager = mem;
    }

    // Step 23 — WorldGeometry (+0x110, 0x230): the real ctor (0x1400b00e0).
    // Caller batch CB6: was the Reimpl_Geometry_Init door (kept for its
    // address-registry row), which placement-news the same class.
    {
        void* mem = AllocSub<void>(0x230, 8);
        m_worldGeometry = mem ? ::new (mem) WorldGeometry() : nullptr;
        StillTerrain::Bump();   // [STILLTERRAIN] a new heightmap owner
    }

    // Step 24 — SunLight (+0x100, 0x108): ctor + Set(dir,col1,col2). No exe vtable override
    // (matches the proven menu path, which uses the Runtime default SunLight vtable).
    {
        void* mem = AllocSub<void>(0x108, 8);
        if (mem) {
            // ENDGAME B.3.8 LOOP (B38_RULES D8): the REAL Runtime class. Retail
            // runs the imported ctor and stamps the exe's 11-slot copy of its
            // table (0x14015e290, slot 0 the exe scalar deleting dtor
            // 0x140019e40); SunLight's own DeletingDtor is that body since RB.
            Graphics::SunLight* sun = ::new (mem) Graphics::SunLight();
            // Initial sun dir = normalize(0.5, 0.33, 1.33) then negate (faithful to
            // the original Load, exe .rdata DAT_140172114/_10c/_15c). This is just a
            // bootstrap: WeatherSimulation::DrawUpdate (0x1400938d0, GameUpdateLoop::Draw
            // step 5) sets the SunLight every frame from the environment's monthly
            // lighting, so this value only matters for the first pre-Draw frame.
            float dir[3] = { 0.5f, 0.33f, 1.33f };
            float len = sqrtf(dir[0]*dir[0] + dir[1]*dir[1] + dir[2]*dir[2]);
            if (len > 0.0f) { dir[0] = -dir[0]/len; dir[1] = -dir[1]/len; dir[2] = -dir[2]/len; }
            const Math::Vector3 sunDir(dir[0], dir[1], dir[2]);
            const Math::Vector4 col1(0.8f, 0.8f, 0.8f, 0.85f);
            const Math::Vector4 col2(0.37f, 0.37f, 0.4f, 1.0f);
            // ?Set@SunLight@Graphics@@QEAAXAEBVVector3@Math@@AEBVVector4@4@1@Z.
            sun->Set(sunDir, col1, col2);
            sun->_detailId = 0;   // +0x28
            m_sunLight = sun;
        } else {
            m_sunLight = nullptr;
        }
    }
    if (m_worldGeometry)   // worldGeom +0x28 = 1
        m_worldGeometry->_detailId = 1;

    // Step 25 — SaveLoad (+0x140, 0x90).
    {
        Save::SaveLoad* mem = AllocSub<Save::SaveLoad>(0x90, 4);
        if (mem) RtSave_SaveLoad_Ctor(mem);
        m_saveLoad = mem;
    }

    // Step 26 — KeyState/map-input struct (+0x148, 0x38) zero-init.
    {
        void* mem = AllocSub<void>(0x38, 4);
        // Retail's inlined ctor (0x140097276..0x140097291): Minimap::Minimap.
        Minimap* minimap = mem ? new (mem) Minimap : nullptr;
        m_imageCapture = minimap;
    }

    // Step 27 — MusicTracks struct (+0x80, 0x18): {idx=-1, res=null (Find deferred), 0}.
    {
        void* mem = AllocSub<void>(0x18, 4);
        MusicPlayer* player = mem ? new (mem) MusicPlayer : nullptr;
        if (player) {
            player->effect = 0xFFFFFFFFu;         // -1
            player->trackList = nullptr;          // MusicTrackList - DEFERRED.
            // BLOCKER for porting MusicPlayer::Update (0x1400a40e0): that
            // function reads *(int*)(musicPlayer[+0x08] + 0x28) as the track
            // count with no null check, so it MUST NOT be wired until this
            // Find("Game/MusicTracks.rsc") is done. See LANE1 report row 39.
            player->field_0x10 = 0;
        }
        m_musicPlayer = player;
    }

    // Step 28 — SkyMesh MeshInstance (+0x108): the sky `_clear_` dome (helper above).
    LoadTime::Stamp("gul.skymesh");   // NOT RETAIL: print-only (steps 28-30)
    BuildSkyMesh_Step28(this);

    // Step 29 — Seasons/Events StringTables. Fill the +0x138 event-log struct's
    // StringTable slots: +0x00 = events (message templates, indexed by eventType),
    // +0x08 = seasons. AddEvent (FUN_140095580) formats every event's text from
    // these tables, so leaving them null yields empty event text AND risks a
    // null-deref in the season/date formatter (FUN_140095230 dereferences +0x08).
    // Load via ExternalFactory::FindOrLoad (m_resourceList is empty in standalone),
    // mirroring the Environment/SkyMesh finds above. The ":events"/":seasons"
    // suffix selects the named sub-resource within Dialog/StringTable.rsc.
    {
        EventLog* evStruct = m_eventNotifier;
        if (evStruct && ::Object::ExternalFactory::_singleton) {
            // ENDGAME B.3.3 step 6A: member call on the real Object::ExternalFactory.
            ::Object::ExternalFactory* efactory = ::Object::ExternalFactory::_singleton;

            // StringTable type-id, read directly (D5): the IsDerivedFrom validation is
            // what makes FindOrLoad return the actual :events/:seasons sub-table
            // instead of the wrong level.
            uint32_t stTid = UI::StringTable::TypeId();

            // Load a StringTable sub-resource. AdjustDelimiters normalizes the
            // "rsc:subname" delimiter so FindOrLoad resolves the named sub-table.
            auto loadStringTable = [&](const char* name) -> UI::StringTable* {
                // Retail: String(name) then Filename::AdjustDelimiters on it —
                // Filename(const char*) is exactly Set + AdjustDelimiters.
                System::Filename fname(name);
                ::Object::External* ext =
                    efactory->FindOrLoadAs<::Object::External>(fname);
                if (!ext) return nullptr;
                if (stTid != 0 && !::Object::IsDerivedFromNullSafe(ext, stTid)) return nullptr;
                return static_cast<UI::StringTable*>(ext);   // checked above (B10)
            };

            UI::StringTable* evTable = loadStringTable("Dialog/StringTable.rsc:events");
            UI::StringTable* seTable = loadStringTable("Dialog/StringTable.rsc:seasons");
            evStruct->events = evTable;
            evStruct->seasons = seTable;

            fprintf(stderr, "[GUL::Load] step29 event StringTables: events=%p seasons=%p (tid=%u)\n",
                    evTable, seTable, stTid);
            fflush(stderr);
        }
    }

    // Step 30 — AchievementManager::SetEventHandler (retail 0x14009752e).
    //
    // The old note here said the singleton slot 0x1401514f0 was "not yet registered in
    // standalone, so g_ppAchievementManager is null → skip cleanly". That has not been
    // true since the slot was wired (engine_funcs.cpp) — the Achievements dialog reads
    // the same manager through it. The handler retail installs is exe 0x140096960, the
    // award TOAST (it formats the achievement title into an event-log entry, type 0x19).
    // PORTED 2026-09-06 (exe close-out lane C): src/game/Game/eventlog_achievement.cpp,
    // registered by standalone/reg_closeout_lane_c.cpp. Until then the handler was left
    // null, which cost only the on-screen announcement — Runtime's award bookkeeping and
    // the "Ach" registry value never depended on it.
    //
    // Retail: RAX = *0x1401514f0 (AchievementManager::_singleton storage);
    //         RCX = *RAX; RDX = 0x140096960; CALL SetEventHandler.
    // We call our port directly rather than through ResolveAddr — the address IS
    // registered for any retail-shaped caller, but this is the only call site.
    {
        Achievement::AchievementManager* achMgr = ::Achievement::AchievementManager::_singleton;
        if (achMgr) {
            achMgr->SetEventHandler(&EventLog_OnAchievementAwarded);
            fprintf(stderr, "[GUL::Load] step30 AchievementManager::SetEventHandler "
                            "(mgr=%p handler=EventLog_OnAchievementAwarded)\n", (void*)achMgr);
        } else {
            fprintf(stderr, "[GUL::Load] step30 SetEventHandler SKIPPED (mgr=%p)\n",
                    (void*)achMgr);
        }
        fflush(stderr);
    }

    // Retail reads +0x15C into a register HERE, before either branch, and writes it
    // back after step 32 (0x1400976d7). That ordering is load-bearing on the
    // restore-from-save path: GameUpdateLoop's own serializer round-trips +0x15C
    // (gameupdateloop_serialize.cpp), so the SaveRestore below overwrites the field
    // with whatever the .sav held — the id the SCENARIO DIALOG chose has to survive.
    const int achievementId = m_scenarioIndex;

    // Step 31 — worldgen (new-game branch, savedState==0). M5 5b first-light:
    // build the terrain mesh (base perlin island) + the gameplay tile-grid map
    // arrays + the TownManager coarse cell index (0x1400975c7). DEFERRED to later 5b slices:
    // CreateNaturalResource (trees/stone — spawns worker threads), and
    // FUN_1400a3300 start-town + initial-citizen spawn (which also sets the
    // camera position — until then the camera stays at the ctor default, framing
    // the map centre at the origin). See src/game/Game/worldgen.cpp.
    // Step 30b — AchievementManager::Reset (retail 0x14009757d), the ELSE arm of the
    // same `if (self+0x58)` branch worldgen sits in: a NEW GAME resets the manager, a
    // load-from-save restores it through AchievementManager::Persist instead
    // (gameupdateloop_serialize.cpp, already ported).
    //
    // Reset is not a "clear the awards" call — it is the INSTANCE BUILDER. Runtime
    // 0x18009e5c0 resizes the array at mgr+0x18 to the achievement count and fills it
    // with Achievement::CreateInstance for every entry. Nothing else ever writes that
    // array. Without it mgr+0x18 stayed NULL and mgr+0x20 zero, and AchievementManager::
    // Update -- which we do call every tick -- returned immediately on its `count <= 0`
    // guard. Result: every goal evaluation, every award and every "Ach" registry write
    // was unreachable, and all 36 achievements sat at 0% forever with nothing in the log
    // to say why. The dialog looked healthy the whole time because it reads the
    // ACHIEVEMENT list (mgr+0x00), not the instance list.
    if (m_savedState == nullptr /*new game*/) {
        Achievement::AchievementManager* mgr = ::Achievement::AchievementManager::_singleton;
        if (mgr) {
            mgr->Reset();
            fprintf(stderr, "[GUL::Load] step30b AchievementManager::Reset(mgr=%p) -> "
                            "instances=%d\n", (void*)mgr, mgr->_instances._size);   // mgr+0x20
        } else {
            fprintf(stderr, "[GUL::Load] step30b Reset SKIPPED (mgr=%p) — "
                            "no achievement can ever be awarded\n",
                    (void*)mgr);
        }
        fflush(stderr);
    }

    static const bool kEnableWorldgen = true;
    if (kEnableWorldgen && m_savedState == nullptr /*new game*/) {
        WorldGeometry*    worldGeom  = m_worldGeometry;
        TerrainGenerator* terrainGen = m_terrainConfig;
        int   sizeIdx    = m_mapSize;
        uint32_t seed    = static_cast<uint32_t>(m_seed);
        if (worldGeom && terrainGen) {
            LoadTime::Stamp("gul.worldgen");   // NOT RETAIL: print-only (BuildTerrain)
            WorldGen_BuildTerrain(worldGeom, terrainGen, sizeIdx, seed);

            // Map dimension from the terrain size-table row (sizeRow[0]).
            const TerrainSizeRow* sizeTable = terrainGen->sizeRows._data;   // +0x80, 0x44-byte rows
            uint32_t dim = sizeTable ? sizeTable[sizeIdx].mapSize : 0;

            // Retail call site 0x1400975c7, immediately after the map dimension is
            // read and BEFORE the +0x148 tile arrays: size the TownManager's coarse
            // cell index to (mapWidth >> 3)^2 entries.
            //
            //     rcx = *(GameWorld+0xF0) + 0x140 ; edx = (W>>3) * (W>>3)
            //     call 0x140017640                ; array-of-arrays resize
            //
            // The array is TownManager+0x140 (header {ptr, count, cap}; each of the
            // count entries is a {int* ids, int count} bucket, stride 0x10), indexed
            // by COARSE cell = (row>>3)*(W>>3) + (col>>3). It is the spatial index the
            // FireWork water search walks: FireWork_SpiralCell_8730 does
            // `*(townMgr+0x140) + coarseCell*0x10` with no null test, exactly as retail
            // does, because retail sized it here. Left unsized the base stayed null and
            // the first ring probe faulted reading 0x2510 (= coarse cell 593 * 0x10).
            // This was the "TradeComponent trade-map resize" deferred at Step 31.
            {
                CitizenBehavior* townMgr = m_citizenList;
                if (townMgr && dim > 0) {
                    const int coarse = (int)dim >> 3;
                    townMgr->field_0x140.SetSize(coarse * coarse);   // 0x140017640
                    fprintf(stderr, "[TownMgr] coarse cell index sized: dim=%u coarse=%d "
                            "buckets=%d base=%p (FireWork water search indexes this; a "
                            "null base faults at coarseCell*0x10)\n",
                            dim, coarse, coarse * coarse,
                            townMgr->field_0x140._data);
                    fflush(stderr);
                }
            }

            // Gameplay tile-grid arrays at the map struct (+0x148): store dim,
            // allocate two dim*dim arrays (+0x18/+0x28), zero the +0x28 one.
            Minimap* mapStruct = m_imageCapture;
            if (mapStruct && dim > 0) {
                mapStruct->size = (int32_t)dim;
                int n = (int)(dim * dim);
                mapStruct->overlayPixels.SetSize(n);   // 0x1400021d0
                mapStruct->terrainPixels.SetSize(n);   // 0x1400021d0
                uint32_t* arr = mapStruct->overlayPixels._data;
                if (arr) memset(arr, 0, (size_t)n * 4);
            }

            // M5 5c — build the gameplay TILE GRID at Game+0x80 (front-half of
            // CreateNaturalResource): classify every cell into a terrain type
            // (buildable / shore / water / mountain). Prerequisite for the
            // start-town spiral spawn-finder. See src/game/Game/maptilegrid.cpp.
            Map* game = m_game;
            LoadTime::Stamp("gul.tilegrid");   // NOT RETAIL: print-only (MapTileGrid_Build)
            if (game && dim > 0)
                MapTileGrid_Build(game, (int)dim);

            // The region system build 0x14009e8c0 runs INSIDE the map create
            // 0x14009ef20 (0x14009ef9c), which MapTileGrid_Build ports in full since
            // 827d0ef75 (FillZones, the region build, the shore loop); retail's Load
            // makes no call of its own after the create (rows 1292 / 1312).

            // move-tick tail — seed the CHARACTER SPATIAL-HASH MANAGER at GameWorld+0x88.
            // CharacterComponent::OnAdd allocates each citizen's char-index out of this; that
            // index (CharacterComponent+0x40 >= 0) is what un-gates the move-tick transform
            // tail. Reads GW/map itself, so no args. Runs before the citizen ECS spawn below,
            // so each citizen's Startup-time OnAdd finds a live manager. Same env gate.
            CharManager_Seed();

            // M5 5c — camera: spiral-search the buildable tile grid for the
            // start-town spawn tile and frame it. Writes the look-at target to
            // PlayerController+0x1c/+0x20/+0x24, exactly as Load does after
            // FUN_1400a3300 (call site 0x14009761d). The ECS spawn at the same
            // tile is deferred. See src/game/Game/starttown.cpp.
            {
                MapGenerator* startCfg = m_mapConfig;   // mapGen
                PlayerController* playerCtrl = m_playerController;
                if (startCfg && playerCtrl && game && dim > 0) {
                    float camPos[3] = { 0, 0, 0 };
                    LoadTime::Stamp("gul.starttown");   // NOT RETAIL: print-only (camera + entity spawn)
                    StartTown_ComputeCamera(startCfg, game, camPos);
                    playerCtrl->target.x = camPos[0];   // +0x1c
                    playerCtrl->target.y = camPos[1];   // +0x20
                    playerCtrl->target.z = camPos[2];   // +0x24
                    fprintf(stderr, "[startTown::DIAG]   camPos = (%.2f, %.2f, %.2f)\n",
                            camPos[0], camPos[1], camPos[2]);
                    fflush(stderr);

                    // M5 5c increment #3 — ECS spawn (FUN_1400a3300 steps 4-7).
                    // world base = the GameUpdateLoop (= g_pGameWorldSingleton):
                    // world+0xf8 = Game, world+0xe8 = entity list.
                    StartTown_SpawnEntities(startCfg, this);
                    LoadTime::Stamp("gul.starttown.done");   // NOT RETAIL: print-only
                    if (game && game->cells.GetData()) {   // NOT RETAIL: print-only world-identity proof (LOADPLAN §4)
                        const MapCell* wc = game->cells.GetData();
                        const int wn = game->cells.GetSize();
                        uint64_t th = LoadTime::kHashSeed;
                        int blocked = 0, occupied = 0;
                        for (int i = 0; i < wn; ++i) {   // the pointer-free bytes +0x18..+0x24, +0x26..+0x2D
                            unsigned char cb[sizeof(MapCell)];
                            std::memcpy(cb, &wc[i], sizeof(MapCell));
                            th = LoadTime::HashBytes(cb + 0x18, 0x0D, th);
                            th = LoadTime::HashBytes(cb + 0x26, 0x08, th);
                            const unsigned char bo = (unsigned char)((wc[i].blocker.Get() ? 1 : 0) | (wc[i].occupant.Get() ? 2 : 0));
                            blocked += bo & 1; occupied += (bo >> 1) & 1;
                            th = LoadTime::HashBytes(&bo, 1, th);
                        }
                        fprintf(stderr, "[WorldHash] tiles=%016llx cells=%d blocked=%d occupied=%d\n",
                                (unsigned long long)th, wn, blocked, occupied);
                    }
                }
            }

            // [M5 5c DIAG] Start-town (FUN_1400a3300) prerequisite probe. Dumps the
            // state of startConfig (mapGen = loop+0x40), the Game tile grid (Game+0x80),
            // and the camera target, WITHOUT calling FUN_1400a3300 yet. Remove once
            // start-town is implemented. See project_newgame_path_roadmap / task #6/#7.
            {
                MapGenerator* mapGen = m_mapConfig;   // startConfig (MapGenerator rsc)
                Map* game       = m_game;
                WorldGeometry* wgeom = m_worldGeometry;
                PlayerController* pctrl = m_playerController;
                fprintf(stderr, "[startTown::DIAG] mapGen(loop+0x40)=%p game=%p worldGeom=%p playerCtrl=%p\n",
                        mapGen, game, wgeom, pctrl);
                if (mapGen) {
                    fprintf(stderr, "[startTown::DIAG]   mapGen+0x3c(townSize)=%d\n",
                            mapGen->subdivisions);
                    fprintf(stderr, "[startTown::DIAG]   pools: a0=%p a8(cnt)=%d | b0=%p b8(cnt)=%d | "
                            "c0=%p c8(cnt)=%d | d0=%p d8(cnt)=%d | e0=%d e4=%d e8=%d\n",
                            mapGen->refArray4._data, mapGen->refArray4._size,
                            mapGen->refArray5._data, mapGen->refArray5._size,
                            mapGen->refArray6._data, mapGen->refArray6._size,
                            mapGen->refArray7._data, mapGen->refArray7._size,
                            mapGen->paramE0, mapGen->paramE4,
                            mapGen->paramE8);
                }
                if (game) {
                    int gdim = game->size;
                    MapCell* cells = game->cells.GetData();
                    fprintf(stderr, "[startTown::DIAG]   *game(dim)=%d  tileGrid(*(game+0x80))=%p  cnt=%d\n",
                            gdim, cells, game->cells.GetSize());
                    // Tile-type histogram (2=buildable 6/10=mountain 7=shore 8=deep water).
                    if (cells && gdim > 0) {
                        int hist[16] = {0};
                        int total = gdim * gdim;
                        for (int i = 0; i < total; ++i) {
                            uint8_t t = cells[i].terrainType;
                            hist[t < 16 ? t : 0]++;
                        }
                        fprintf(stderr, "[startTown::DIAG]   tiles: buildable(2)=%d shore(7)=%d "
                                "deepWater(8)=%d mtnLo(10)=%d mtnHi(6)=%d\n",
                                hist[2], hist[7], hist[8], hist[10], hist[6]);
                    }
                }
                if (wgeom)
                    fprintf(stderr, "[startTown::DIAG]   worldGeom+0xc0(dim)=%d\n", wgeom->_gridSize);
                // Keystone sanity: sample terrain height via Reimpl_MapGetTerrainPos
                // (FUN_14009f3e0) at map centre + a few offsets. Banished valley => centre
                // should be low/buildable, edges higher.
                {
                    const float pts[4][2] = {{0,0},{-100,0},{100,100},{-120,-120}};
                    for (int i = 0; i < 4; ++i) {
                        float in[3] = { pts[i][0], pts[i][1], 0.0f };
                        float out[3] = {0,0,0};
                        game->GetTerrainPos(out, in, 1, nullptr);
                        fprintf(stderr, "[startTown::DIAG]   height@(%.0f,%.0f) = %.3f\n",
                                pts[i][0], pts[i][1], out[2]);
                    }
                }
                fflush(stderr);
            }
        }
    }

    // Step 31-LOAD — restore-from-save branch (save/load wave 2026-08-27).
    // Retail Load's tail: `if (m_savedState) { this->vtbl[+0x38](this, 0,
    // savedState, &out); ~SavedState(); MemFree; m_savedState = 0; }` (vcall at
    // 0x14009754c) — the ENTIRE world state deserializes here instead of the
    // worldgen/StartTown branch above. We call the ported SaveRestore directly
    // (it IS our vtable +0x38 slot).
    if (m_savedState != nullptr) {
        Save::SavedState* savedState = m_savedState;
        fprintf(stderr, "[GUL::Load] LOAD-FROM-SAVE branch: SaveRestore(load) state=%p\n",
                savedState);
        fflush(stderr);
        // Char spatial-hash manager must exist BEFORE deserialize: citizen
        // CharacterComponents are persist-created during SaveRestore and their
        // OnAdd/enrolment allocates a char-index out of it (worldgen path seeds
        // it before the citizen spawn for the same reason).
        CharManager_Seed();
        // Retail's vcall also passes &out in R9; 0x1400959c0 never reads it.
        LoadTime::Stamp("gul.saverestore");   // NOT RETAIL: print-only
        SaveRestore(false, savedState);
        // BANISHED_SAVE_TEST=roundtrip: serialize the just-restored world again
        // and compare the raw bytes (gameupdateloop_serialize.cpp). No-op otherwise.
        LoadTime::Stamp("gul.saverestore.done");   // NOT RETAIL: print-only
        GameUpdateLoop_SaveRoundtripCheck(this, savedState);
        RtSave_SavedState_Dtor(savedState);
        System::MemoryAllocator::MemFree(savedState);
        m_savedState = nullptr;
        fprintf(stderr, "[GUL::Load] LOAD-FROM-SAVE done\n");
        fflush(stderr);

        // No region build here: retail's Load (0x140096b00) has none on this path.
        // Map::Serialize's load tail (0x14009f2a6) builds it (row 1292).
        // MapTileGrid_Build is a WORLDGEN-ONLY step and must not run here either:
        // it reallocates the cell array and would wipe the per-cell region ids the
        // save just restored (every citizen then loses its job a few hundred frames
        // after the load).
        {
            Map* game = m_game;
            int dim = game ? game->size : 0;
            if (game && dim > 0) {
                // Regression canary. The per-cell region id (tile+0x26) is the last
                // gate in FUN_1400a00f0; if a future rebuild step reintroduces the
                // wipe, these read 0 and every citizen silently loses its job a few
                // hundred frames later. One line per load.
                {
                    const MapCell* tiles = game->cells.GetData();
                    fprintf(stderr, "[GridProbe] tiles=%p regionIds:", (const void*)tiles);
                    if (tiles)
                        for (int c = 32000; c <= 35000; c += 500)
                            fprintf(stderr, " [%d]=%u/kind%u", c,
                                    tiles[c].zone,
                                    tiles[c].terrainType);
                    fprintf(stderr, "\n"); fflush(stderr);
                }
            }
        }
    }

    // EXT SEAM (NOT RETAIL, docs/extensions/handins/FASTPATHS.md, PATHWORKERS.md): the class B set is final here on every route (a
    // load's manifest was read in SaveRestore above); ext.fast_paths takes its
    // selection for the game before the sim issues its first path request.
    Ext::FastPaths::SelectForGame();
    Ext::PathWorkers::SelectForGame();   // ext.path_workers (class B), the same rule

    // Step 32 — AchievementInstance (retail 0x140097645-0x1400976dd).
    //
    // THIS IS WHAT STARTS A SCENARIO OR TUTORIAL. +0x15C is the index of the chosen
    // Scenario in the global resource ExternalList; ScenarioDialog::LoadSelected puts
    // it there (via WorldSetup_LoadFromSave) and passes a NULL instance IBP, so the
    // instance is built here and nowhere else. Scenario derives from
    // Achievement::Achievement, and SetAchievement -> Achievement::GoalList::Initialize
    // (Runtime 0x18009f0e0) walks the achievement's goal array, asks each goal for its
    // instance type id (Goal vtable +0x48) and constructs one through Object::Factory.
    // For a tutorial that goal is FinishTutorialGoal, whose FinishTutorialInstance
    // allocates the TutorialInstance in its own vtable +0x20 (exe 0x1400d7a90) and is
    // then stepped every tick through vtable +0x28 (-> 0x1400e1db0).
    //
    // Until this ran the port loaded the tutorial WORLD and played it as a sandbox:
    // no goal list, no popup, no steps. The old one-line `*(p+0x150) = nullptr` carried
    // the comment "SKIPPED on new game (achievementId == -1)" — true of a new game, but
    // it skipped unconditionally, so the scenario/tutorial path got no instance either.
    //
    // The block is inert on a new game (id == -1), which is why it is default-on;
    // BANISHED_LIVE_SCENARIO_GOALS=0 is the kill switch for an A/B run.
    m_scenarioInstance = nullptr;
    if (achievementId >= 0) {
        const uint32_t achTypeId = Achievement::Achievement::TypeId();
        if (!System::MemoryAllocator::_singleton || !g_gameContainer) {
            fprintf(stderr, "[GUL::Load] step32 SKIPPED - unresolved (reslist=%p)\n",
                    (void*)g_gameContainer);
            fflush(stderr);
        } else {
            Achievement::AchievementInstance* instance = nullptr;
            void* mem = System::MemoryAllocator::MemAlloc(0x28, 8);
            if (mem) {
                // ENDGAME B.3.8 LOOP (B38_RULES D8): retail runs the imported
                // `??0AchievementInstance@Achievement@@` and then stamps the EXE's
                // copy of the class's table (0x14015e350: slot 0 the exe scalar
                // deleting dtor 0x140019d90 — ~AchievementInstance + MemFree on &1 —
                // slots 1-3 import thunks into the Runtime bodies). That copy is
                // an MSVC artefact of the inlined ctor, not an exe class: the real
                // Runtime class (concrete since block RB, its DeletingDtor = that
                // exe body) is built here and wears its compiler table. The old
                // path ran the ctor door and read 0x14015e350 by address, which
                // was never registered, so the object kept the minted table.
                // `::new`: the class declares (and does not define) its own
                // placement operator new.
                instance = ::new (mem) Achievement::AchievementInstance();
            }
            // InternalBasePtr(instance): the member holds the bare pointer.
            m_scenarioInstance = instance;

            // scenario = resourceList->data[achievementId]
            //   MOV RAX,[0x1401c3de8] / MOV RCX,[RAX+8] / MOV RAX,[RCX+0x20] / [RAX+idx*8]
            ::Object::External* scenario = nullptr;
            GameContainer* container = g_gameContainer;
            ::Object::ExternalList* resList = container ? container->scenarioList.Get() : nullptr;   // +0x08
            ::Object::External** data = resList ? resList->_resources : nullptr;                       // +0x20
            if (data) scenario = data[achievementId];

            // IsDerivedFrom(scenario, Achievement::_typeInfo.typeId). Retail reads the id
            // through the .rdata slot at 0x140151f68, which points at the Runtime's
            // exported BaseTypeInfo for Achievement; its first dword IS the type id.
            ::Object::External* checked = scenario;
            if (scenario && achTypeId)
                checked = ::Object::IsDerivedFromNullSafe(scenario, achTypeId) ? scenario : nullptr;

            Achievement::AchievementInstance* inst = m_scenarioInstance.Get();
            fprintf(stderr, "[GUL::Load] step32 achievementId=%d instance=%p scenario=%p "
                            "derived=%p achTid=%u\n",
                    achievementId, (void*)inst, (void*)scenario, (void*)checked,
                    achTypeId);
            fflush(stderr);
            // ExternalPtr<Achievement> arrives as a bare register value (B7-5).
            // `checked` passed the IsDerivedFrom(Achievement) gate above.
            if (inst) inst->SetAchievement(
                Object::ExternalPtr<Achievement::Achievement>(static_cast<Achievement::Achievement*>(checked)));
            m_scenarioIndex = achievementId;
        }
    }

    LoadTime::Stamp("gul.finalize");   // NOT RETAIL: print-only (steps 33+)
    // Step 33 — finalize camera + add scene globals + unpause audio channels.
    if (m_playerController)
        m_playerController->UpdateCamera(40.0f, 2.0f, 512.0f);   // 0x140098660
    {
        Graphics::Scene* gScene = m_graphicsScene;   // g_pGameWorld+0x60
        // B.3.5: the `fn_Graphics_Scene_AddGlobalObject` readiness term went with
        // the door; `gScene` is an object test and stays.
        if (gScene) {
            Graphics::Scene* gs = gScene;
            if (m_worldGeometry)
                gs->AddGlobalObject(m_worldGeometry); // WorldGeom
            // M5 5b lighting: add the SunLight so the directional light + ambient
            // light the terrain in the main render pass (Renderer::Draw dispatches the
            // light's vtable+0x50 SunLight::Draw → Context::SetLight + DrawItemList).
            // The crashing shadow pass (SunLight::DrawShadowMap, vtable+0x48) is gated by
            // renderer+0x3c, which GameUpdateLoop::Draw step 6 forces to 0 in standalone
            // ("Shadows: Off") — the shadow draw never runs, so the terrain renders lit
            // without the unwired shadow shader/render-target. Verified against the
            // proven menu path (which adds its SunLight the same way) + the Runtime
            // decompiles (Renderer::Draw 0x180104bb0, SunLight::Draw 0x1801067b0,
            // DrawShadowMap 0x180106bc0, AddScene 0x180105700).
            static const bool kAddSunLight = true;
            if (kAddSunLight && m_sunLight)
                gs->AddGlobalObject(m_sunLight); // SunLight
            // SkyMesh: add as a Scene global object so the reflection + main passes
            // both draw the `_clear_` sky dome (the dark-water fix; see BuildSkyMesh_Step28).
            static const bool kAddSkyMesh = true;
            if (kAddSkyMesh && m_skyMesh)
                gs->AddGlobalObject(m_skyMesh); // SkyMesh
        }
        Audio::Scene* aScene = m_audioScene;   // g_pGameWorld+0x78
        if (aScene) {
            aScene->Pause(4, false);
            aScene->Pause(3, false);
            aScene->Pause(2, false);
        }
    }

    // Publish the completion signal LAST, after every subsystem + the world are
    // fully built, so GameUpdateLoop_IsWorldBuilt only returns true once the loop
    // is safe to transition into and draw. release-store pairs with the acquire
    // load in the poller.
    LoadTime::Stamp("gul.load.done");   // NOT RETAIL: print-only (the world-built signal follows)
    s_worldBuiltLoop.store(loopVoid, std::memory_order_release);

    fprintf(stderr, "[GUL::Load] EXIT — world built: game=%p ui=%p entityList=%p scene=%p sun=%p\n",
            m_game, m_userInterface, m_entityList,
            m_graphicsScene, m_sunLight);
    fflush(stderr);
}
