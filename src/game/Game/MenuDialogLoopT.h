#pragma once
// ============================================================================
// src/game/Game/MenuDialogLoopT.h — ENDGAME B.3.8 block LOOP (B38_RULES D8)
//
// The exe's `Engine::DialogLoopT` subclass that drives the main menu
// (Dialog\MainMenu.rsc) and its animated 3D background scene. Unreflected.
//
// RETAIL (Application-x64.exe, B38_SLOTS.md sec. 6, B37_RULES sec. 3.1):
//   table 0x14015ed00, 13 slots, sizeof 0xc00 (MemAlloc(0xc00, 8))
//   [0]  0x14001a2e0  own scalar deleting dtor
//   [1]  0x14009b930  own Startup      [2]  0x14009b280  own Shutdown
//   [3]  0x14009b6e0  own Update       [4]  0x14009b440  own Draw
//   [5]  IMPORT ?DrawFailed@UpdateLoop@     [6]  0x14009a930  own Load
//   [7]  IMPORT ?SaveRestore@UpdateLoop@    [8]  0x14009a8d0  own OnVideoError
//   [9]  0x14009a8b0  own OnVideoRestart    [10] 0x14001a2d0  own "Game/Menu"
//   [11] 0x14001a2c0  own AllowsModChanges `MOV AL,1`
//   [12] 0x14009ba30  own CreateDialog: the MainMenuDialog
//   ctor 0x14009bb00: ??0DialogLoopT@Engine@@(filename); stamp 0x14015ed00;
//     ??0Random@System@@ on +0x168; ??0Scene@Audio@@ on +0xb30; zero
//     +0xb68..+0xbf8.
//
// Slots the tree still overrides although retail inherits the base body are
// marked `(base in retail)`: the old hand-built table's stubs, moved here
// unchanged; the titled behaviour commit after this one makes them inherited.
// The helper bodies in menudialogloopt.cpp still address the fields by raw
// offset (MenuDLTOff); the named members below pin the same bytes.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "engine/UpdateLoop.h"   // -Iruntime/types: Engine::DialogLoopT
#include "system/Value.h"        // -Iruntime/types: System::Random (by value at +0x168)
#include "audio/Audio.h"         // -Iruntime/types: Audio::Scene (by value at +0xb30)
#include "collection/Collection.h"

namespace Audio { class SoundEffect; }
namespace Graphics { class Renderer; class Scene; class Geometry; class MeshInstance; class SunLight; }
namespace Save { class SaveLoad; }
namespace Engine { class UpdateLoop; }
namespace Video { class Context; }
class MenuScene;
class PlayerController;
class WorldGeometry;      // Game/WorldGeometry.h (MenuScene::geometry, +0xbc0)

class MenuDialogLoopT final : public Engine::DialogLoopT {
public:
    // 0x14009bb00 (header banner).
    explicit MenuDialogLoopT(const System::String& filename);

    // [0] 0x14001a2e0: ~MenuDialogLoopT (0x14001a310), MemFree on &1.
    ~MenuDialogLoopT() override;   // 0x14001a310 (menudialogloopt_stubs.cpp)
    void Startup() override;                                     // [1] 0x14009b930
    void Shutdown() override;                                    // [2] 0x14009b280
    bool Update(const System::Time& time) override;              // [3] 0x14009b6e0
    void Draw(float interpolation) override;                     // [4] 0x14009b440
    void Load() override;                                        // [6] 0x14009a930
    void OnVideoError() override;                                // [8] 0x14009a8d0
    void OnVideoRestart() override;                              // [9] 0x14009a8b0
    const char* GetResourceFilename() override;                  // [10] 0x14001a2d0 "Game/Menu"
    bool AllowsModChanges() override;                            // [11] 0x14001a2c0 true
    Object::InternalPtr<UI::DialogInstance>* CreateLoopDialog(   // [12] 0x14009ba30
        Object::InternalPtr<UI::DialogInstance>* ret) override;


public:
    System::Random _random;              // +0x168  ??0Random@System@@
    uint8_t        _pad0b2c[4];          // +0xb2c
    // +0xb30 and +0xbd0..+0xbff are real members (behaviour: B4 wave Y): retail's
    // ctor 0x14009bb00 runs ??0Scene@Audio@@ on +0xb30 and zeroes the three
    // array headers; its dtor 0x14001a310 has an empty body, frees +0xbf0,
    // +0xbe0, +0xbd0 (MemFree, then the data and size|capacity qwords zeroed)
    // and runs ??1Scene@Audio@@ on +0xb30: reverse declaration order.
    Audio::Scene   _audioScene;          // +0xb30  Audio::Scene (by value)
    Engine::UpdateLoop* _pendingLoop;    // +0xb68  queued loader chain (SetDialogLoaderNextLoop)
    Save::SaveLoad* _saveLoad;           // +0xb70  the menu's own (B4 STANDALONE: H4)
    float          _animYaw;             // +0xb78  live yaw of the orbit
    float          _scrollX;             // +0xb7c  transform[+0x28] cached at load
    float          _scrollY;             // +0xb80  transform[+0x2c] cached at load
    uint8_t        _pad0b84[4];          // +0xb84
    Audio::SoundEffect* _music;          // +0xb88  ExternalPtr<SoundEffect> Audio/Music/Menu.rsc
    MenuScene*     _menuScene;           // +0xb90  ExternalPtr<MenuScene> Game/MenuScene/SceneN.rsc
    Graphics::Renderer* _renderer;       // +0xb98
    Graphics::Scene* _scene;             // +0xba0
    Graphics::MeshInstance* _mainMesh;   // +0xba8  (B4 STANDALONE: H4)
    Graphics::SunLight* _sunLight;       // +0xbb0  (B4 STANDALONE: H4)
    PlayerController* _transform;        // +0xbb8  the camera controller (MemAlloc(0x10c, 4) + 0x140098800)
    WorldGeometry* _sceneGeometry;       // +0xbc0  MenuScene::geometry (not owned; the menu pair 0x14009a8d0 / 0x14009a8b0 calls WorldGeometry members on it)
    Graphics::Geometry* _dlcShip;        // +0xbc8  the DLC ship: MemAlloc(0xf0) + the exe Geometry subclass ctor 0x1400d3f10 (0x14009ac9a), added as a scene global
    Collection::Array<Graphics::Geometry*> _particleArray;  // +0xbd0  the ParticleSystemInstances (Update reads it as Geometry**, 0x14009b6e0)
    Collection::Array<Graphics::Geometry*> _instanceArray;  // +0xbe0  every scene instance (MeshInstance / ParticleSystemInstance)
    Collection::Array<uint32_t>            _sceneIdArray;   // +0xbf0  scene ids
    void SetLoaderNextLoop(Engine::UpdateLoop* loaderLoop);   // 0x14009a8f0 (Game/worldsetup_reload.cpp)
    bool LoadScene_PhaseA();   // part of Load 0x14009a930 (Game/menudialogloopt.cpp)
    bool LoadSceneA1();   // part of Load 0x14009a930 (Game/menudialogloopt.cpp)
    void LoadSceneA3();   // part of Load 0x14009a930 (Game/menudialogloopt.cpp)
    void LoadStaticMeshes_PhaseB(MenuScene* menuScene, Graphics::Scene* scene);   // part of Load 0x14009a930 (Game/menudialogloopt.cpp)
    void LoadParticles_PhaseC(MenuScene* menuScene, Graphics::Scene* scene);   // part of Load 0x14009a930 (Game/menudialogloopt.cpp)
    void DrawScene_PhaseA(float alpha, Video::Context* context);   // part of Draw 0x14009b440 (Game/menudialogloopt.cpp)
    void UpdateCamera_PhaseA(float deltaSeconds);   // part of Update 0x14009b6e0 (Game/menudialogloopt.cpp)
    void TickParticles(const System::Time* time);   // part of Update 0x14009b6e0 (Game/menudialogloopt.cpp)
    bool LoadMusic();   // part of Load 0x14009a930 (Game/menudialogloopt.cpp)
    void StartMusic();   // part of Startup 0x14009b930 (Game/menudialogloopt.cpp)
    void StopMusic();   // part of Shutdown 0x14009b280 (call at 0x14009b41a) (Game/menudialogloopt.cpp)
    void TickAudioScene(const System::Time* time);   // part of Update 0x14009b6e0 (Game/menudialogloopt.cpp)
};

static_assert(sizeof(MenuDialogLoopT) == 0xc00, "MenuDialogLoopT is 0xc00 (MemAlloc(0xc00, 8))");
static_assert(offsetof(MenuDialogLoopT, _pad0b2c) == 0xB2C, "MenuDialogLoopT::_pad0b2c");
static_assert(offsetof(MenuDialogLoopT, _pad0b84) == 0xB84, "MenuDialogLoopT::_pad0b84");
static_assert(alignof(MenuDialogLoopT) == 8, "qword-aligned (the vptr)");
static_assert(offsetof(MenuDialogLoopT, _random)        == 0x168, "+0x168 System::Random");
static_assert(offsetof(MenuDialogLoopT, _audioScene)    == 0xb30, "+0xb30 Audio::Scene");
static_assert(offsetof(MenuDialogLoopT, _pendingLoop)   == 0xb68, "+0xb68");
static_assert(offsetof(MenuDialogLoopT, _saveLoad)      == 0xb70, "+0xb70");
static_assert(offsetof(MenuDialogLoopT, _animYaw)       == 0xb78, "+0xb78");
static_assert(offsetof(MenuDialogLoopT, _scrollX)       == 0xb7c, "+0xb7c");
static_assert(offsetof(MenuDialogLoopT, _scrollY)       == 0xb80, "+0xb80");
static_assert(offsetof(MenuDialogLoopT, _music)         == 0xb88, "+0xb88");
static_assert(offsetof(MenuDialogLoopT, _menuScene)     == 0xb90, "+0xb90");
static_assert(offsetof(MenuDialogLoopT, _renderer)      == 0xb98, "+0xb98");
static_assert(offsetof(MenuDialogLoopT, _scene)         == 0xba0, "+0xba0");
static_assert(offsetof(MenuDialogLoopT, _mainMesh)      == 0xba8, "+0xba8");
static_assert(offsetof(MenuDialogLoopT, _sunLight)      == 0xbb0, "+0xbb0");
static_assert(offsetof(MenuDialogLoopT, _transform)     == 0xbb8, "+0xbb8");
static_assert(offsetof(MenuDialogLoopT, _sceneGeometry) == 0xbc0, "+0xbc0");
static_assert(offsetof(MenuDialogLoopT, _dlcShip)       == 0xbc8, "+0xbc8");
static_assert(offsetof(MenuDialogLoopT, _particleArray) == 0xbd0, "+0xbd0");
static_assert(offsetof(MenuDialogLoopT, _instanceArray) == 0xbe0, "+0xbe0");
static_assert(offsetof(MenuDialogLoopT, _sceneIdArray)  == 0xbf0, "+0xbf0");
