#pragma once
// src/game/Game/menudialogloopt_funcs.h
// Function declarations for the MenuDialogLoopT (Engine::DialogLoopT subclass
// that drives the main menu) deep reimplementation.
//
// Drives the standalone menu Load path (Phase A).

#include <cstdint>

class MenuDialogLoopT;
class MenuScene;
struct MenuSceneMesh;
namespace Graphics { class Scene; class GraphicsMesh; }
namespace System { class Time; }
namespace Video { class Context; }

// B.4 GAME-b: the loop, the frame time and the context are typed (the slots in
// src/game/Game/menudialogloopt_stubs.cpp call the members on `this`, and
// chunked_scene_loader.cpp calls LoadSceneA1 / A3 and InitState on a MenuDialogLoopT*).

// MenuDialogLoopT::LoadScene_PhaseA — mirror the 3D-scene block of the original
// MenuDLT::Load (exe 0x14009A930), up to and including the three Scene::AddGlobalObject
// calls and the helper_a3a0/+0xb7c/+0xb80 transform copy. Skips:
//   - Audio/Music/Menu.rsc load (audio music ext stored at self+0xb88).
//   - TradeWork (gameSettings[0x16] gated DLC).
//
// SYNCHRONOUS all-in-one entry point used by MenuDialogLoopT::Startup's fallback path.
// It internally calls LoadSceneA1 +
// LoadStaticMeshes_PhaseB + LoadSceneA3 in sequence (~542ms blocking).
// Standalone's Loading splash drives those three pieces independently to
// chunk PhaseB across multiple Update ticks (see ChunkedSceneLoader).
//
// Caller must already have invoked Engine::DialogLoopT::Load (which populates
// self+0x30 with the dialog External). On entry self+0xb90 is expected to be
// nullptr; on success it holds the loaded MenuScene External.
//
// Returns true on success (scene allocated and added). Returns false if the
// MenuScene resource fails to load (no scene background — UI-only menu).

// MenuDialogLoopT::LoadSceneA1 — pre-PhaseB block of MenuDLT::Load. Loads the
// random Game/MenuScene/SceneN.rsc, allocates the 5 scene-graph objects
// (Renderer/Scene/SunLight/Transform/MeshInstance, ~10ms), wires the main
// mesh, adds the 3 global objects, copies the deserialized transform.
//
// Cheap (~10ms) — does NOT iterate the 12K-15K-element static-mesh array.
// Called from ChunkedSceneLoader::Init, which the Loading splash's
// GameLoadDialogLoopT::Update runs (RunArmedInit), so the MenuScene External +
// scene-graph fields are populated before chunked PhaseB starts.
//
// Returns true on success, false if the MenuScene resource fails to load.

// MenuDialogLoopT::LoadSceneA3 — post-PhaseB particle iteration wrapper.
// Reads menuScene/scene from MenuDLT struct fields and calls
// LoadParticles_PhaseC. Thin wrapper for symmetry with LoadSceneA1.

// MenuDialogLoopT::LoadStaticMeshes_PhaseB — mirror the static-mesh loop of
// MenuDLT::Load (exe 0x14009A930). For each element of MenuScene+0x30 (count =
// MenuScene+0x38), allocate a Graphics::MeshInstance, set its mesh + transform
// + material list, and add it to the global scene via Scene::AddObject. Result
// is the props/buildings/foliage of the menu background.
//
// SYNCHRONOUS all-in-one wrapper. Internally calls
// InitState + TickChunk(totalCount) which processes every
// element in a single call (~542ms blocking on the main thread).
//
// menuScene is the loaded MenuScene External (self+0xb90).
// scene     is the Graphics::Scene the meshes get added to (self+0xba0).
// No-op if either pointer is null or the allocator singleton is missing.

// PhaseB chunked-iteration state. Captured by InitState; advanced by
// TickChunk one chunk at a time. Standalone uses this to spread the 542ms
// loop across multiple Loading splash Update ticks so the splash animation
// keeps running smoothly instead of freezing for a single long block.
struct MenuDialogLoopT_PhaseBState {
    MenuDialogLoopT* self;    // receives appended array data
    MenuScene* menuScene;     // MenuScene External (loaded by LoadSceneA1)
    Graphics::Scene* scene;   // the meshes get added to it
    MenuSceneMesh* elemArr;   // MenuScene::meshes data
    Graphics::GraphicsMesh** meshArr;   // MenuScene::meshResources data
    int32_t  meshArrCount;    // MenuScene+0x48 — bound for meshIdx
    int32_t  totalCount;      // MenuScene+0x38 — total elements (snapshot)
    int32_t  index;           // current iteration index (0..totalCount)
    // Stats — accumulated across chunks, printed on completion.
    int32_t  added;
    int32_t  skippedBadIdx;
    int32_t  matSlotsTotal;
    void InitState(MenuDialogLoopT* self, MenuScene* menuScene, Graphics::Scene* scene);   // port-only: the chunked PhaseB of Load 0x14009a930 (Game/menudialogloopt.cpp)
    int32_t TickChunk(int32_t maxElements);   // port-only: the chunked PhaseB of Load 0x14009a930 (Game/menudialogloopt.cpp)
};

// Initialize a chunked-loop state. Validates the allocator singleton + array bounds.
// Sets state->totalCount=0 on any failure so TickChunk early-exits.
// Caller checks (state->totalCount > 0) to confirm work needs doing.

// Process up to maxElements elements of the PhaseB iteration. Returns the
// number of elements processed this call. Returns 0 when the loop has fully
// completed (state->index == state->totalCount). Caller drives this across
// multiple Update ticks.

// MenuDialogLoopT::LoadParticles_PhaseC — mirror the particle loop of
// MenuDLT::Load (exe 0x14009A930). For each element of MenuScene+0x50 (count =
// MenuScene+0x58), allocate a Graphics::ParticleSystemInstance, configure it
// via the equivalent of MenuScene_helper_3ca0 (vtable+0x48 SetParticle,
// SetTransform from element yaw+offset, Update with t=0), and add it to both
// the all-instance array (+0xbe0) and the particle-only array (+0xbd0). Per-
// frame Update iterates +0xbd0 — that's what makes smokestack chimneys emit
// smoke.
//
// menuScene is the loaded MenuScene External (self+0xb90).
// scene     is the Graphics::Scene the particles get added to (self+0xba0).
// No-op if either pointer is null or the allocator singleton is missing.

// MenuDialogLoopT::TickParticles — per-frame Update on each entry of the
// particle-only array at MenuDLT+0xbd0. Calls vtable+0x08 (Update(Time*)) on
// each particle so emitters spawn/age sprites. Caller is MenuDialogLoopT::Update,
// after the camera derivation step.

// MenuDialogLoopT::DrawScene_PhaseA — mirror the 3D `if (self+0xb90)` block of
// MenuDLT::Draw (exe 0x14009b440). Caller invokes this BEFORE UI::Draw.
//
// alpha is the frame-blend value passed to Renderer::Draw.
// context is the live Video::Context (engine_singleton+0x10).
//
// No-op if self+0xb90 (the MenuScene External) is null.

// MenuDialogLoopT_TickAudio — per-frame Audio::Device::Update tick.
//
// Original MainLoop::Run (Runtime 0x1800dd3c0) calls Audio::Device::Update
// EXACTLY ONCE before entering the main loop, never again per-frame. The
// Update fn (Runtime 0x1800de520) only iterates the 64 platform channels
// and calls FreeChannelPlatform on those marked done — i.e. it cleans up
// finished sounds so new sounds can be queued. With it firing only once,
// any sound that finishes after startup leaves its channel marked done
// forever, eventually starving the audio system.
//
// Calling per-frame (idempotent — does nothing if no channels are flagged
// done) keeps the channel pool fresh and matches the cadence of the per-
// scene Audio::Scene::Update calls the original game makes from each
// loop's Update slot.
//
// Fetches Audio::Device live from the MainLoop singleton (m_engine+0x6d0)
// and null-guards the time pointer, the singleton, its engine and the device. No-op if any
// of the chain is unavailable yet (e.g., during early loader-loop ticks
// before the engine subsystems have been fully wired).
//
// Caller invokes this each tick from the loop's Update slot, passing the
// current Time pointer (System::Time, 8-byte engine-tick value, same as
// what's passed to MenuDLT::Update).
void MenuDialogLoopT_TickAudio(const System::Time* time);  // B8: free -- no retail symbol (the audio tick MenuDLT::Update inlines); first parameter the frame's System::Time, no object

// MenuDialogLoopT::LoadMusic — mirror the first block of MenuDLT::Load (exe
// 0x14009a930): load Audio/Music/Menu.rsc via ExternalFactory::FindOrLoad and
// store the External at self+0xb88. The original type-checks the result against
// the SoundEffect type info; standalone trusts FindOrLoad's return (the Runtime
// factory tree is incomplete so IsDerivedFrom NULL-derefs — same convention as
// LoadSceneA1's MenuScene load).
//
// Loading the music does NOT start playback — that happens in StartMusic below
// (the original splits load (Load slot) from play (Startup slot)). Safe to call
// off the main thread: it is a plain refcounted resource load, like
// Engine::DialogLoopT::Load. (ENDGAME B.3.4a closing, comment only: this said
// "on the AsyncLoader worker thread". Our standalone AsyncLoader was deleted in
// 96c17f47 -- `Engine::Loader` is the only dispatcher now -- so the thread it
// named no longer exists. The property being asserted is unchanged.)
//
// Returns true if the music External loaded (self+0xb88 non-null).

// MenuDialogLoopT::StartMusic — mirror the audio block of MenuDLT::Startup (exe
// 0x14009b930), run AFTER Engine::DialogLoopT::Startup:
//   1. self+0xd0 = &Audio::Scene(self+0xb30)   — register the loop's audio
//      scene with the DialogInstance base (where UI sound effects route).
//   2. Audio::Scene::PlayEffect(scene@+0xb30, music@+0xb88, soundType=2, &Time(0))
//      — start the looping menu music on the music channel (2).
//
// Returns after step 1 if the music External (self+0xb88) is null. Call on the main
// thread (Startup), after LoadMusic populated +0xb88.

// MenuDialogLoopT::StopMusic — mirror the `Audio::Scene::StopAll(self+0xb30)` at
// the tail of MenuDLT::Shutdown (exe 0x14009b280, call at 0x14009b41a). Stops the
// streamed menu music BEFORE the next LoaderLoop's Loader::Load remounts the
// packages; the body's banner says why that ordering is load-bearing.


// MenuDialogLoopT::TickAudioScene — mirror the per-frame Audio::Scene::Update of
// MenuDLT::Update (exe 0x14009b6e0): advance the embedded Audio::Scene (+0xb30)
// each frame so the music (and any ambient/3D channels) keeps streaming.
//
// The original gates this behind Application::IsPaused and toggles
// Audio::Scene::Pause(channel 2) on pause-state transitions. MenuDialogLoopT::Update
// makes that handshake and calls this only when not paused.
//
// Calls Audio::Scene::Update(scene@+0xb30, elapsedTime, Time(0),
//   &Random@+0x168, &listenerVec3{0,0,0}, 0.0f) as a member call. Caller passes
// the frame's System::Time pointer.

// MenuDialogLoopT::UpdateCamera_PhaseA — mirror the camera-animation block of
// MenuDLT::Update (exe 0x14009b6e0). Animates the orbit yaw, derives the
// camera eye from the deserialized lookAtTarget/yaw/pitch/distance fields,
// rebuilds the Math::Transform via FromLookAt, converts to Transform3 and
// applies to the embedded Camera via SetCameraToWorld.
//
// Without this, the Camera's view matrix stays at identity (set by the
// Camera ctor) regardless of what was deserialized — so the renderer projects
// terrain through an identity camera at world origin and the menu is black.
//
// Caller invokes this each tick from MenuDialogLoopT::Update, inside its `_scene`
// (+0xba0) test, passing the frame's deltaSeconds (time-as-seconds — see
// System::Time::AsSeconds).
//
// The controller (self+0xbb8) is dereferenced with no test.
