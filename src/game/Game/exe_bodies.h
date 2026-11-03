#pragma once
// src/game/Game/exe_bodies.h -- declarations of the exe bodies DEFINED in
// src/game/Game/ (ENDGAME B.3R, block W0-P0c, B3R_RULES R5).
//
// Every body the registry reaches by retail address, declared once, copied
// from its DEFINITION (linkage included), grouped by defining file; the
// retail address(es) it is registered under are the trailing comment. The
// defining .cpp includes this header so the compiler checks each pair.
// Readers call these by name (W1); nothing here changes what runs. Making
// them members is B.4's work.
//
// Also here: the declarations and inline bodies of this module that lived in
// engine/funcs/engine_funcs.h, moved verbatim with their comments (that
// header is gone since B3R W2-EZ: its includers include this one directly).
//
// Declared in another header already (not redeclared here):
//   Game::RegisterDefaultInputBindings     0x1400992f0  (src/game/Game/inputbindings.h)
//
// Internal linkage in the defining file (static / anonymous namespace):
// not declared -- that would change linkage, which this step does not do.
//   (ResizeStride0x10 0x140017640 is CitizenIntAssocArray::SetSize since B6 row 15)

#include <cstdint>
// The inline bodies' own needs (they reached these through engine/funcs/engine_funcs.h
// until B3R W2-EZ deleted it):
#include "options.h"            // Opt() (GetLoadingSoundString)
#include "system/String.h"      // System::String::Format
#include "system/Value.h"       // System::Random
#include "ui/UserInterface.h"   // UI::UserInterface::SetCursor / SetFullscreenFade
#include "object/External.h"    // Object::ExternalPtr<UI::Cursor>
#include "Game/game.h"   // B3R STORAGE: g_gameContainer
#include "Game/GameSettings.h"   // g_gameSettings's struct
class Minimap;             // Game/Minimap.h (Minimap_PaintTerrain)
class WeatherSimulation;   // Game/WeatherSimulation.h (Game_WeatherDtor)
#include "system/MemoryAllocator.h"   // System::MemoryAllocator::MemAlloc / MemFree (B3R Leftovers)

// ---- citizen_names.cpp
class NameDatabase;
extern "C" System::String* NameDB_GenerateName(System::String* retStr, const NameDatabase* db, System::Random* rng, int minLen, int maxLen);  // 0x1400a4b30  // B8: stays free -- free in retail: RCX is the String return slot (0x1400a4c25 MOV RCX,R12 before the String ctor), RDX the NameDatabase; an MSVC member's RCX is this (and the port's db null test has no this to sit on)
extern "C" System::String* NameDB_PickAndGenerate(System::String* retStr, System::Random* rng, char female);  // 0x1400a4c60  // B8: stays free -- free in retail: no NameDatabase argument (RCX the String return slot, RDX the Random, R8B the flag; it loads Female / Male.ndb itself, 0x1400a4c86)

// ---- citizenlist_serialize.cpp

// ---- eventlog.cpp
struct EventLogEntry;   // Game/EventLog.h

class EventLog; struct EventLogEntry; class GameUpdateLoop;
namespace Object { class InternalBasePtr; }
// The shared leaves the crop-infestation writer (diseasecomponent.cpp) uses.

extern "C" int  EventLog_TileIndex_Reg(void* unused, const Object::InternalBasePtr* entityIBP);  // 0x140095030 (RCX dead)  // B8: free in retail -- RCX dead: at 0x1400484b0 it is what the import call [0x140151ee8] before it left

// ---- game.cpp
// --- Game_TriggerDisasterByType (0x140060110) — REIMPLEMENTED inline ---
// Iterates ExternalFactory for entities with ComponentDescription (DAT_1401c7318),
// finds SelectDescription sub-component (DAT_1401c48a8), checks type match,
// removes matching entity via Game_RemoveEntity.
void Game_TriggerDisasterByType_Impl(int disasterType);  // B8: free in retail -- first parameter the disaster type (an int), no object

// --- Game_SpawnResources (0x140091660) — REIMPLEMENTED inline ---
// Spawns 'count' resources into citizen storage buildings.
// Phase 1: builds array of entity pointers whose NaturalResourceComponent flags match resourceFlag.
// Phase 2: round-robin distributes items across citizens' StorageComponents.
// param_1 = Game object (longlong*), param_2 = resourceFlag bitmask, param_3 = total count to spawn.
//
// Structure offsets on param_1 (Game object):
//   +0x110 (param_1[0x22]) = entity array ptr (raw pointers)
//   +0x118 (param_1[0x23]) = entity count
//   +0x000 (param_1[0])    = citizen InternalBasePtr array
//   +0x008 (param_1[1])    = citizen count
//
// Entities (phase 1) are raw pointers with components at +0x28 (array) / +0x30 (count).
// Citizens (phase 2) are InternalBasePtr entries with components at +0x30 (IBP array) / +0x38 (count).
//
// Calls original exe functions StorageComponent_impl_cd80 (0x14012cd80) for CanAdd
// and FUN_14012d000 (0x14012d000) for AddItem, via resolved function pointers.
class CitizenBehavior;

// ---- game_leaf_helpers.cpp
class CharacterGrid;   // Component/CharacterGrid.h

// ---- game_settings_persist.cpp

// ---- game_main.cpp
// The exe cell 0x1401c3de0 (B3R STORAGE): the 0x34-byte settings block GameMain
// allocates and stores there (0x14004202e), a `GameSettings` (Game/GameSettings.h).
// A namespace-scope pointer; was the file-static g_storage_GameSettingsPtr of
// standalone/address_registry.cpp, read through g_pGameSettingsPtr /
// g_pNewGameSettings. Null outside RunGameMain's settings lifetime.
extern GameSettings* g_gameSettings;

// ---- gameupdateloop_load.cpp

// ---- gameupdateloop_serialize.cpp
#include "Entity/EntityList.h"   // UpdateBucket (an alias of Collection::Array since B.4 X4-COMP)
namespace IO { class Serializer; }

// ---- gameupdateloop_teardown.cpp
struct EntityList; class CitizenBehavior; class PlayerToolManager; class WorkScheduler;

// ---- geometry_serialize.cpp
class WorldGeometry;
namespace IO { class Stream; }

// ---- goals_serialize.cpp
class WorkScheduler;
namespace IO { class Stream; }

// ---- keystone_a_gamedtor.cpp
class Map;

// ---- mapquery.cpp

// ---- minimap_paint.cpp
class WorldGeometry;
namespace Video { class Context; }
namespace Graphics { class SunLight; }

// ---- professionlist.cpp
class ProfessionList;

// ---- serialize_arrays.cpp
class PlayerController;
extern "C" void Reimpl_Array_Resize_Stride08(void* arrayHeader, int newCount);  // 0x140001bb0  // B9: stays free -- erased core over holders that are not one Collection::Array: WorldGeometry::_batchMaterials (WorldGeometryArray), the +0x1E0 outlines and roadtool's +0x218 refs are RawArray-shaped headers, AmbientEmitterDescription::_emitters a raw {data, count, capacity} triple; MenuScene +0x40 / +0x60, Tutorial steps and TutorialStep's arrays are Collection::Array<T*> on the trait row, whose init is null (tail unwritten) where 0x140001bb0 zeroes it (`TEST RDX,RDX / MOV [RDX],R10`); moving needs those holders typed on a zero-tail row (kSpec_Ptr8Zero: plain qword copy as 0x140001c31, zero tail) -- header work outside RT's files; the port-only null test went in B9 RT
// 0x14000c520 Reimpl_Array_Resize_Stride10_Spawn: declared in Component/SpawnDescription.h (it takes the typed entry array).
struct MenuSceneMesh;
namespace IO { class Serializer; }

// ---- serialize_heightmap_mesh.cpp

// ---- serialize_resize_helpers.cpp

// ---- serialize_struct60b.cpp: GeometryBatch's members and GeometryBatch_Resize
// (0x14001f250 / 0x14001f5e0) are declared in Game/WorldGeometryElements.h.
template <class T> struct RawArray;   // Action/PathSearch.h

// ---- town_storage_seed.cpp
class Component; class MapGenerator;

// ---- town_workplace_registry.cpp
class CitizenBehavior; class ComponentEntity; class Profession;
class WorkPlaceComponent;
extern "C" void TownRegistry_RegisterWorkplace(CitizenBehavior* townMgr, const WorkPlaceComponent* workplaceComp);  // B9: harness / port-only -- read: no retail body; retail registers a workplace from WorkPlaceComponent's OnCreate (0x14012f520 -> CitizenBehavior::RegisterWorkplace 0x1400895a0, the +0x10 entity copied to a stack local); this is the standalone's spawn-time wiring of that call (starttown.cpp)

// ---- unreg_lane5_musicplayer.cpp
class MusicPlayer;   // Game/MusicPlayer.h

// ---- w2_takescreenshot.cpp

// ---- weather_particles.cpp
class WeatherParticleGeometry;
class Weather;
// 0x1400198a0 Reimpl_WeatherArray_Resize: declared in Component/EnvironmentDescription.h (it takes the typed array).

// ---- weather_sim.cpp
class WeatherSimulation; namespace System { class Time; }

// ---- worldgen_f390_shims.cpp

// ---- worldsetup_reload.cpp
class MainMenuDialog; namespace Save { class SavedState; }

// ---- inline bodies (moved from engine/funcs/engine_funcs.h; no .cpp)

// SetEngineNextLoop — FUN_140097fb0(game, loaderLoop): the running game's UI
// (GameUpdateLoop::m_userInterface +0x98) gets the hourglass cursor and a fade to
// black, then the loop is queued in GameUpdateLoop::m_nextLoop (+0xC8).
// SetDialogLoaderNextLoop — FUN_14009a8f0(menuLoop, loaderLoop): the menu loop's UI
// (DialogLoopT::GetUserInterface, +0x40) fades to black, then the loop is queued in
// MenuDialogLoopT::_pendingLoop (+0xb68).
// Both are defined in Game/worldsetup_reload.cpp (they were inline here, reached
// through uintptr_t offsets).
class GameUpdateLoop;
class MenuDialogLoopT;
namespace Engine { class UpdateLoop; }

// GetLoadingSoundString — FUN_140099130 — NOW REIMPLEMENTED INLINE
// Creates a Random, generates rand 0-11, formats "Dialog/Loading.rsc:gameLoad%i".
// Old typedef kept for reference:
// typedef void* (*fn_GetLoadingSoundString_t)(uint64_t unused, void* retStr,
//                                              uint64_t unused2, uint64_t unused3);
// extern fn_GetLoadingSoundString_t fn_GetLoadingSoundString;
inline System::String* GetLoadingSoundString(System::String* retStr) {
    // 0x140099151: the 0x9c4-byte Random at [RSP+0x30] of a 0xa00 frame, built by
    // ??0Random@System@@QEAA@XZ 0x1800c7f20 (CALL [0x140152b50]); no dtor.
    System::Random random;
    // the uint overload ?Rand@Random@System@@QEAAII@Z (exe slot
    // 0x1401512e8, Runtime 0x1800c8040) — `12u` picks it over Rand() and
    // Rand(int,int).
    uint32_t variant = random.Rand(12u);
    // BANISHED_MENU_SCENE pins every random front-end pick, this one included:
    // the loading image it selects stays resident and is numbered into the
    // save's reference table, so an unpinned pick shifts every reference id
    // after "Dialog\Loading.rsc:gameLoadN" (tools/resave_gate/).
    if (Opt().menuScene >= 0)
        variant = static_cast<unsigned>(Opt().menuScene) % 12u;
    // ?Format@String@System@@SA?AV12@PEBDZZ  retail 0x1800c9730
    *retStr = System::String::Format("Dialog/Loading.rsc:gameLoad%i", variant);
    return retStr;
}

// ---- citizenlist_serialize.cpp (B3R G-Game: external linkage given; read by citizenbehavior.cpp)

// ---- B3R G-Game: bodies of this module called directly (formerly through local extern
// prototypes in geometry_serialize.cpp / worldgen.cpp); not registered.
// serialize_struct60b_serialize.cpp
// serialize_heightmap_mesh.cpp
// serialize_image_upload.cpp

// ---- 0x14009e8c0 the Map's region system build, ported in two halves (block LUT, 2026-10-11).
// Retail calls it from Map::Serialize's load tail (0x14009f2a6, gamegrid_serialize.cpp) and from
// the worldgen map create 0x14009ef20. Both halves read only the Map; dim = Map::size (*param_1).
//   0x14009e8c0-0x14009eb16 pathfinder thread (+0x68): alloc, ctor 0x1400a67f0, search flags,
//                           the eleven step costs, the per-cell flag bytes   (pathfinder_seed.cpp)
//   0x14009eb18-0x14009ebd9 region graph (+0x70): alloc, Init 0x1400a8670, Flush 0x1400a8a90;
//                           region-pair cache (+0x78)                           (region_seed.cpp)
extern "C" void Pathfinder_Seed(Map* game, int dim);   // 0x14009e8c0, first half
extern "C" void Region_Seed(Map* game, int dim);       // 0x14009e8c0, second half
// ---- loader_loop_stubs.cpp (Phase D.4 gap hunt, SLOTS2B)
// 0x140099db0: fill the 0xE0 game container from the inner LoaderLoop's resource list
// (Game/GlobalResources.rsc), then the default input bindings. Called by
// InnerLoaderLoop::Load (0x140001120) on the loader thread.
namespace Object { class ExternalList; }
void GameContainer_Init(GameContainer* self, const Object::ExternalList* list);   // 0x140099db0
