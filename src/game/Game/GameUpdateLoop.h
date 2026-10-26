#pragma once
// ============================================================================
// src/game/Game/GameUpdateLoop.h — ENDGAME B.3.8 block LOOP (B38_RULES D8)
//
// The exe's in-game simulation loop, a real `Engine::UpdateLoop` subclass.
// Unreflected. It owns every game subsystem: graphics scene, audio, renderer,
// UI, entities, citizens, pathfinder, weather, tutorial, save/load, camera
// and the scenario/achievement instance. g_pGameWorldSingleton
// (DAT_1401c3dd8) points at the live instance; the tree's many
// `gameWorld + 0x…` reads are fields of this class.
//
// RETAIL (Application-x64.exe, B38_SLOTS.md sec. 6, B37_RULES sec. 3.1):
//   table 0x14015e2f0, 12 slots, sizeof 0x160 (MemAlloc(0x160, 8))
//   [0]  0x140019dd0  scalar deleting dtor: restamp, g_pGameWorldSingleton = 0,
//                     ~String(+0x28), ~UpdateLoop, MemFree on &1
//   [1]  0x140097c00  Startup     [2]  0x1400977c0  Shutdown
//   [3]  0x140096380  Update      [4]  0x140096010  Draw
//   [5]  0x140096000  DrawFailed: a bare `RET 0`
//   [6]  0x140096b00  Load (the world build; LOADER THREAD)
//   [7]  0x1400959c0  SaveRestore(save, SavedState*): reads RCX, DL, R8 (row 1247)
//   [8]  0x140095970  OnVideoError     [9]  0x140095930  OnVideoRestart
//   [10] 0x140019d40  GetResourceFilename: "Game/Game"
//   [11] IMPORT ?AllowsModChanges@UpdateLoop@Engine@@ (inherited: false)
//   ctor 0x140098060(this, savedState, &ibp, scenarioIdx, name, p6, p7, p8,
//   p9, p10, p11) — see the ctor below.
//
// Fields keep their exe-side `m_` names (the bodies in gameupdateloop*.cpp
// read them); the UpdateLoop base's are Engine::UpdateLoop's.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "engine/UpdateLoop.h"   // -Iruntime/types: Engine::UpdateLoop
#include "system/String.h"       // -Iruntime/types: System::String (by value at +0x28)
#include "object/Internal.h"     // -Iruntime/types: Object::InternalPtr (the ctor's scenario reference)

// B.4 W0-H2 / GAME-a (H4): every pointer member is typed; forward declarations
// only (layout-neutral, proven by the offset asserts below). The member names
// m_tutorial (a WorkScheduler) and m_imageCapture (the Minimap) are the tree's.
namespace Graphics    { class Material; class SunLight; class MeshInstance;
                        class Scene; class AnimationManager; class Renderer; }
namespace Picking     { class Scene; }
namespace Audio       { class Scene; }
namespace UI          { class UserInterface; }
namespace System      { class Random; }
namespace Save        { class SaveLoad; }
namespace Object      { class ExternalList; }
namespace Achievement { class AchievementInstance; }
class ProfessionList;     // Game/ProfessionList.h (block W0-H1)
class WorldGeometry;      // Game/WorldGeometry.h
class PlayerController;   // Game/PlayerController.h
class PlayerToolManager;  // Game/PlayerToolManager.h
class Map;                // Game/Map.h (B4 GAME-a)
class EntityList;         // Entity/EntityList.h (block WORK-a)
class CitizenBehavior;    // Entity/CitizenBehavior.h (block WORK-a)
class CharacterGrid;      // Component/CharacterGrid.h (block COMP-a)
class Calendar;           // Game/Calendar.h (B4 GAME-a)
class WeatherSimulation;  // Game/WeatherSimulation.h (B4 GAME-a)
class WorkScheduler;      // Game/WorkScheduler.h (B4 GAME-a)
class EventLog;           // Game/EventLog.h (B4 GAME-a)
class MusicPlayer;        // Game/MusicPlayer.h (B4 GAME-a)
class Minimap;            // Game/Minimap.h (B4 GAME-a)
class MapGenerator;       // Game/MapGenerator.h
class TerrainGenerator;   // Game/TerrainGenerator.h
class ClimateGenerator;   // Game/ClimateGenerator.h
namespace Save        { class SavedState; }
namespace IO          { class Stream; }

class GameUpdateLoop final : public Engine::UpdateLoop {
public:
    // 0x140098060. `scenarioIbp` arrives by reference (R8, 0x140098085 `MOV RDI,R8`);
    // retail copies `*param_3` into +0x150. `disasters` is retail's char p8:
    // +0xD0 = p8 ? 0 : 0xF0.
    GameUpdateLoop(Save::SavedState* savedState,
                   const Object::InternalPtr<Achievement::AchievementInstance>& scenarioIbp,
                   int32_t scenarioIndex,
                   const System::String& saveName, int32_t seed,
                   int32_t mapSize, bool disasters, MapGenerator* mapConfig,
                   TerrainGenerator* terrainConfig, ClimateGenerator* climateConfig);

    // [0] 0x140019dd0's body: g_pGameWorldSingleton = 0; then ~String(+0x28) and
    // ~UpdateLoop are the compiler's member/base chain, in retail's order.
    ~GameUpdateLoop() override;                                  // [0] 0x140019dd0
    void Startup() override;                                     // [1] 0x140097c00
    void Shutdown() override;                                    // [2] 0x1400977c0
    bool Update(const System::Time& time) override;              // [3] 0x140096380
    void Draw(float interpolation) override;                     // [4] 0x140096010
    void DrawFailed() override {}                                // [5] 0x140096000 `RET 0`
    void Load() override;                                        // [6] 0x140096b00
    // [7] 0x1400959c0 reads RCX, DL and R8 only: the out pointer retail's Load
    // passes in R9 (and the dead R9 of Save::SaveLoad::UpdateSave) is never
    // read (row 1247). Body in gameupdateloop_serialize.cpp.
    void SaveRestore(bool saving, Save::SavedState* state) override;
    void OnVideoError() override;                                // [8] 0x140095970
    void OnVideoRestart() override;                              // [9] 0x140095930
    const char* GetResourceFilename() override;                  // [10] 0x140019d40 "Game/Game"
    // [11] AllowsModChanges: inherited (Engine::UpdateLoop's, false).

    void OnScenarioComplete();                                   // 0x140096830

    // 0x140095a90: the master serializer SaveRestore (0x1400959c0) calls with
    // the stream (B4 GAME-a: a member; retail's is non-virtual and unnamed; it
    // reads RCX and RDX only, row 1247). Body in gameupdateloop_serialize.cpp.
    void SerializeAll(IO::Stream& stream);
    // NOT RETAIL as functions: SerializeAll's two halves (Game/serialize_scope.h), cut after
    // PlayerController::Serialize for ext.background_save (docs/extensions/handins/HITCH_SAVESNAP.md).
    struct SerializeScope;
    void SerializeAllHead(IO::Stream& stream, SerializeScope& scope);
    void SerializeAllBody(IO::Stream& stream, SerializeScope& scope);

    // The exe cell 0x1401c3dd8 (B3R STORAGE): the live loop. Load stores `this`
    // (0x140096b35); both destructors clear it (0x140098030, 0x140019df7).
    // Null outside a game. Defined in gameupdateloop.cpp.
    static GameUpdateLoop* _singleton;

public:
    // Construction parameters (stored by the ctor, used by Load)
    System::String m_saveName;      // +0x28  save file name
    int32_t  m_seed;                // +0x30  map seed (seeds the Random at +0xD8; newgamedialog writes it)
    int32_t  m_mapSize;             // +0x34  map size index (the terrain size-table row)
    int64_t  m_gameTime;            // +0x38  accumulated game time (Time)
    MapGenerator*     m_mapConfig;     // +0x40  ExternalPtr<MapGenerator> (ctor p9; serializer 0x140019ec0)
    TerrainGenerator* m_terrainConfig; // +0x48  ExternalPtr<TerrainGenerator> (ctor p10; serializer 0x14001a0c0)
    ClimateGenerator* m_climateConfig; // +0x50  ExternalPtr<ClimateGenerator> (ctor p11; serializer 0x1400194a0)
    Save::SavedState* m_savedState;    // +0x58  non-null = loading from save (ctor p2)

    // Rendering subsystems (created in Load, destroyed in Shutdown)
    // B.4 GAME-a (H4): typed. Load / Shutdown still build and destroy the
    // Graphics three and Audio::Scene through the Runtime ctor / dtor doors
    // (RtGraphics_* on the POD mirrors, RtAudio_Scene_*): the real classes'
    // ctors / dtors are declared and undefined (src/engine/graphics/Scene.h,
    // Camera.h, AnimationManager.h), so the order of calls is main's.
    Graphics::Scene*            m_graphicsScene;    // +0x60  MemAlloc(0x78) in Load
    Graphics::AnimationManager* m_animationManager; // +0x68  MemAlloc(0x9d8) in Load
    Picking::Scene*             m_pickingScene;     // +0x70  MemAlloc(0x60) in Load
    Audio::Scene*               m_audioScene;       // +0x78  MemAlloc(0x38) in Load
    // +0x80: MusicPlayer* — MemAlloc(0x18,4) {+0x00 int32 currentTrackId = -1,
    //   +0x08 MusicTrackList* = Find("Game/MusicTracks.rsc"), +0x10 int64 0};
    //   ticked by 0x1400a40e0 (MusicPlayer::Update).
    MusicPlayer* m_musicPlayer;     // +0x80
    // +0x88: loose Morton (Z-order) quadtree over characters, MemAlloc(0x60,8),
    //   ctor 0x1400182b0 (8 levels, 0x5555 cells). NOT a MusicPlayer and NOT a
    //   Transform (the +0x80/+0x88 names were swapped until 2026-08-30).
    CharacterGrid* m_characterGrid; // +0x88
    Graphics::Renderer* m_renderer; // +0x90  MemAlloc(0x50) in Load
    UI::UserInterface* m_userInterface; // +0x98  MemAlloc(0x128) in Load

    // Resource references (looked up in Load from the ExternalList). Retail's
    // type is ExternalPtr<T> (one pointer); declared as T* (B.4 H5: the tree's
    // ctor zeroes them by hand, as retail's does). T is the type id Load's
    // ExternalList::Find is gated on (0x140096b00).
    Object::ExternalList* m_helpTopics;     // +0xA0  "Game/Help/HelpTopics.rsc" (ExternalList type id)
    Object::ExternalList* m_diseaseList;    // +0xA8  "Game/Disease/Diseases.rsc" (ExternalList type id)
    ProfessionList*       m_professionList; // +0xB0  "Game/Profession/Profession.rsc" (TypeInfo_ProfessionList)
    Graphics::Material*   m_uiMaterial;     // +0xB8  "Material/UI/UIMaterialNoBlend.rsc" (Material type id)

    // Input state
    uint8_t  m_quickSavePressed;    // +0xC0  KeyMap::Pressed(0x21)
    uint8_t  m_quickLoadPressed;    // +0xC1  KeyMap::Pressed(0x22)
    uint8_t  _padC2[6];             // +0xC2

    Engine::UpdateLoop* m_nextLoop; // +0xC8  pending loop transition (ctor 0)

    uint32_t m_gameFlags;           // +0xD0  0xF0 or 0x00 (ctor's p8)
    uint32_t _padD4;                // +0xD4

    // Game systems (created in Load, destroyed in Shutdown)
    System::Random* m_random;       // +0xD8  MemAlloc(0x9c4) + Random(seed +0x30) in Load
    PlayerController* m_playerController; // +0xE0  camera controller, MemAlloc(0x10c) + ctor 0x1400985b0
    EntityList* m_entityList;       // +0xE8  MemAlloc(0xe8) + ctor 0x1400c3f90 in Load
    CitizenBehavior* m_citizenList; // +0xF0  town manager, MemAlloc(0x1c0) + ctor 0x140090a30 in Load
    Map*     m_game;                // +0xF8  the world map (tile grid), MemAlloc(0xf8) + ctor 0x14009ee70

    // Scene geometry objects (added to Graphics::Scene as globals)
    Graphics::SunLight*     m_sunLight;      // +0x100 MemAlloc(0x108) in Load (exe table 0x14015e290)
    Graphics::MeshInstance* m_skyMesh;       // +0x108 sky, MemAlloc(0x120) in Load (exe table 0x14015e240)
    WorldGeometry*          m_worldGeometry; // +0x110 terrain, MemAlloc(0x230) + ctor 0x1400b00e0

    // Game managers
    // +0x118: the work scheduler (0x138 bytes), NOT a Tutorial: the member keeps
    //   the tree's name until the coordinator renames it (other blocks read it).
    WorkScheduler* m_tutorial;      // +0x118
    // +0x120: NOT an EntityUI (that is a 0x48-byte UI::DialogController). Load
    //   builds it with MemAlloc(0xb0, 4) + ctor 0x1401025c0 (no vtable: an
    //   InternalBasePtr at +0x00, the CURRENT TOOL InternalBasePtr at +0x28 that
    //   Update's per-frame call 0x1401026a0 checks against EditTool and drives
    //   through the tool's slots, +0x30 the paused byte, +0x34 the game speed
    //   (ctor 1), a 5-entry Array at +0x70). Renamed from m_entityUI (B.4 W0-H2).
    PlayerToolManager* m_toolManager; // +0x120 player tool manager (0xB0 bytes)
    Calendar* m_dayCounter;         // +0x128 the year clock (0x18 bytes: {year, yearTime, yearLength})
    WeatherSimulation* m_weather;   // +0x130 the weather simulation (0x78 bytes; not the Weather resource)
    EventLog* m_eventNotifier;      // +0x138 the event log (0x28 bytes)
    Save::SaveLoad* m_saveLoad;     // +0x140 MemAlloc(0x90) in Load
    // +0x148: the minimap image (0x38 bytes), not an image capture: the member
    //   keeps the tree's name until the coordinator renames it.
    Minimap* m_imageCapture;        // +0x148

    // Scenario/achievement
    // +0x150 (ctor 0x140098060: the member's null store 0x1400981bc, then *p3
    // copied in 0x1400981c3 / 0x1400981c6; no destructor: Shutdown DeletePtrs it)
    Object::InternalPtr<Achievement::AchievementInstance> m_scenarioInstance;
    bool     m_scenarioCompleted;   // +0x158 set to 1 when scenario met/failed (Serialize bool, 0x140095a90)
    uint8_t  _pad159[3];            // +0x159
    int32_t  m_scenarioIndex;       // +0x15C index into scenario list (-1 = none)

    void TakeScreenshot();          // 0x140095da0 (body: w2_takescreenshot.cpp, B4 GAME-c)
    void SetEngineNextLoop(Engine::UpdateLoop* loaderLoop);   // 0x140097fb0 (Game/worldsetup_reload.cpp)
};

static_assert(sizeof(GameUpdateLoop) == 0x160, "GameUpdateLoop is 0x160 (MemAlloc(0x160, 8))");
static_assert(alignof(GameUpdateLoop) == 8, "qword-aligned (the vptr)");
static_assert(offsetof(GameUpdateLoop, m_saveName) == 0x28, "m_saveName at +0x28");
static_assert(offsetof(GameUpdateLoop, m_seed) == 0x30, "m_seed at +0x30");
static_assert(offsetof(GameUpdateLoop, m_mapSize) == 0x34, "m_mapSize at +0x34");
static_assert(offsetof(GameUpdateLoop, m_gameTime) == 0x38, "m_gameTime at +0x38");
static_assert(offsetof(GameUpdateLoop, m_mapConfig) == 0x40, "m_mapConfig at +0x40");
static_assert(offsetof(GameUpdateLoop, m_terrainConfig) == 0x48, "m_terrainConfig at +0x48");
static_assert(offsetof(GameUpdateLoop, m_climateConfig) == 0x50, "m_climateConfig at +0x50");
static_assert(offsetof(GameUpdateLoop, m_savedState) == 0x58, "m_savedState at +0x58");
static_assert(offsetof(GameUpdateLoop, m_graphicsScene) == 0x60, "m_graphicsScene at +0x60");
static_assert(offsetof(GameUpdateLoop, m_animationManager) == 0x68, "m_animationManager at +0x68");
static_assert(offsetof(GameUpdateLoop, m_pickingScene) == 0x70, "m_pickingScene at +0x70");
static_assert(offsetof(GameUpdateLoop, m_audioScene) == 0x78, "m_audioScene at +0x78");
static_assert(offsetof(GameUpdateLoop, m_musicPlayer) == 0x80, "m_musicPlayer at +0x80");
static_assert(offsetof(GameUpdateLoop, m_characterGrid) == 0x88, "m_characterGrid at +0x88");
static_assert(offsetof(GameUpdateLoop, m_renderer) == 0x90, "m_renderer at +0x90");
static_assert(offsetof(GameUpdateLoop, m_userInterface) == 0x98, "m_userInterface at +0x98");
static_assert(offsetof(GameUpdateLoop, m_helpTopics) == 0xA0, "m_helpTopics at +0xA0");
static_assert(offsetof(GameUpdateLoop, m_diseaseList) == 0xA8, "m_diseaseList at +0xA8");
static_assert(offsetof(GameUpdateLoop, m_professionList) == 0xB0, "m_professionList at +0xB0");
static_assert(offsetof(GameUpdateLoop, m_uiMaterial) == 0xB8, "m_uiMaterial at +0xB8");
static_assert(offsetof(GameUpdateLoop, m_quickSavePressed) == 0xC0, "m_quickSavePressed at +0xC0");
static_assert(offsetof(GameUpdateLoop, m_quickLoadPressed) == 0xC1, "m_quickLoadPressed at +0xC1");
static_assert(offsetof(GameUpdateLoop, _padC2) == 0xC2, "_padC2 at +0xC2");
static_assert(offsetof(GameUpdateLoop, m_nextLoop) == 0xC8, "m_nextLoop at +0xC8");
static_assert(offsetof(GameUpdateLoop, m_gameFlags) == 0xD0, "m_gameFlags at +0xD0");
static_assert(offsetof(GameUpdateLoop, _padD4) == 0xD4, "_padD4 at +0xD4");
static_assert(offsetof(GameUpdateLoop, m_random) == 0xD8, "m_random at +0xD8");
static_assert(offsetof(GameUpdateLoop, m_playerController) == 0xE0, "m_playerController at +0xE0");
static_assert(offsetof(GameUpdateLoop, m_entityList) == 0xE8, "m_entityList at +0xE8");
static_assert(offsetof(GameUpdateLoop, m_citizenList) == 0xF0, "m_citizenList at +0xF0");
static_assert(offsetof(GameUpdateLoop, m_game) == 0xF8, "m_game at +0xF8");
static_assert(offsetof(GameUpdateLoop, m_sunLight) == 0x100, "m_sunLight at +0x100");
static_assert(offsetof(GameUpdateLoop, m_skyMesh) == 0x108, "m_skyMesh at +0x108");
static_assert(offsetof(GameUpdateLoop, m_worldGeometry) == 0x110, "m_worldGeometry at +0x110");
static_assert(offsetof(GameUpdateLoop, m_tutorial) == 0x118, "m_tutorial at +0x118");
static_assert(offsetof(GameUpdateLoop, m_toolManager) == 0x120, "m_toolManager at +0x120");
static_assert(offsetof(GameUpdateLoop, m_dayCounter) == 0x128, "m_dayCounter at +0x128");
static_assert(offsetof(GameUpdateLoop, m_weather) == 0x130, "m_weather at +0x130");
static_assert(offsetof(GameUpdateLoop, m_eventNotifier) == 0x138, "m_eventNotifier at +0x138");
static_assert(offsetof(GameUpdateLoop, m_saveLoad) == 0x140, "m_saveLoad at +0x140");
static_assert(offsetof(GameUpdateLoop, m_imageCapture) == 0x148, "m_imageCapture at +0x148");
static_assert(offsetof(GameUpdateLoop, m_scenarioInstance) == 0x150, "m_scenarioInstance at +0x150");
static_assert(offsetof(GameUpdateLoop, m_scenarioCompleted) == 0x158, "m_scenarioCompleted at +0x158");
static_assert(offsetof(GameUpdateLoop, _pad159) == 0x159, "_pad159 at +0x159");
static_assert(offsetof(GameUpdateLoop, m_scenarioIndex) == 0x15C, "m_scenarioIndex at +0x15C");

// The per-render-frame token (gameupdateloop.cpp s_frameNum), read-only. Read by
// ParticleComponent's per-frame un-stamp and the stress harness's /part row.
extern "C" int GameUpdateLoop_CurrentFrame();
