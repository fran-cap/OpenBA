// src/game/Game/menudialogloopt.cpp
// Phase A reimplementation of the MenuDLT (Engine::DialogLoopT subclass) 3D
// scene background — Load + Draw paths. Decompiled from:
//   - MenuDLT::Load (exe 0x14009A930)  — scene allocation + AddGlobalObject
//   - MenuDLT::Draw (exe 0x14009B440)  — camera/light setup + Renderer::Draw
//
// Phase A scope: render the menu scene mesh with sun lighting + fog as the
// background to the main menu UI. Skips:
//   - TradeWork DLC ship (gameSettings[0x16] branch)
//   - Exe-side vtable overrides (PTR_FUN_14015e290, _vslot10_14015e240)
//
// Skipping the vtable overrides means we use the default Runtime MeshInstance
// + SunLight vtables. This may render slightly differently from the original
// but is correct for an MVP scene background.
//
// B.4 GAME-b: these are the phase pieces of retail's Load / Update / Draw /
// Startup / Shutdown (B5: split bodies stay split). They read the loop, the
// MenuScene and the runtime classes by name. LEFT (each marked at its site):
// the entry points the STANDALONE block's callers hand a `void*` loop / time /
// context (chunked_scene_loader.cpp, menudialogloopt_stubs.cpp); the loop's
// three {data, count|cap} arrays and its Audio::Scene, raw storage whose ctor /
// dtor live in menudialogloopt_stubs.cpp (H5); PlayerController's camera (H5);
// the Renderer / Scene ctor doors (R-GFX); Video::Context +0x288 (inside its
// `_pixelConstantStaging` bytes, R-VIDEO's header).

#include "Game/menudialogloopt_funcs.h"
#include "video/Device.h"   // ENDGAME B.3.9 SS2: Video::Configuration::_singleton
#include "engine/MainLoop.h"   // ENDGAME B.3.9 SS2: Engine::MainLoop::_singleton
#include "engine/Engine.h"
// ENDGAME B.3.5 Video/Graphics caller batch: Graphics::{Camera,Scene,Geometry,
// MeshInstance,GraphicsMesh,SunLight,ParticleSystemInstance} and Video::Context
// are real classes. These go AHEAD of "resolve.h" — resolve.h pulls
// <windows.h>, whose <wingdi.h> declares a global `Rectangle` FUNCTION that
// hides `Volume::Rectangle`'s class name inside Volume.h's own static_asserts
// (Camera.h and Geometry.h include Volume.h).
#include "graphics/Camera.h"
#include "graphics/Scene.h"
#include "graphics/Mesh.h"                // MeshInstance
#include "graphics/GraphicsMesh.h"
#include "graphics/SunLight.h"
#include "graphics/ParticleInstance.h"    // ParticleSystemInstance
#include "graphics/Particle.h"
#include "volume/Volume.h"
#include "Game/MenuDialogLoopT.h"         // _loadBit (LoadSceneA1)
#include "Game/MenuScene.h"
#include "Game/PlayerController.h"
#include "Game/WorldGeometry.h"
#include "video/Context.h"
#include "audio/Device.h"
#include "options.h"
#include <new>                            // B.3.9 GFX: placement new
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
#include <cmath>
#include <cstdlib>
#include "system/Value.h"      // ENDGAME B.3.4b System caller batch (Random/Time/Color)
#include "audio/Audio.h"       // ENDGAME B.3.7 callers (K4)
#include "audio/SoundEffect.h" // Audio::SoundEffect : Object::External (the menu music)
#include "ext/ambient_occlusion/ambient_occlusion.h"   // EXT SEAM (NOT RETAIL, docs/extensions/handins/AO-MENU.md): ext.ambient_occlusion on the menu backdrop


// ============================================================
// Constants from the original exe (.rdata)
// ============================================================
// FUN_140098660 (per-frame camera update):
//   fovX  = DAT_1401721dc = 40.0f  (0x42200000)  passed as XMM1 = scalar param
//   refAspect = DAT_140172170 = 1.7777778f       (16:9 reference)
//   near  = DAT_140172174 = 2.0f
//   far   = DAT_140172240 = 512.0f
//   colorDiv = DAT_14017222c = 255.0f
// (0x140098800's 1 / 100 / 90 / 90 are PlayerController::PlayerController's, menuscene.cpp.)
namespace MenuSceneConst {
    static constexpr float kHelper8660_FovX        = 40.0f;   // DAT_1401721dc (UpdateCamera's fovY)
    static constexpr float kHelper8660_NearZ       = 2.0f;    // DAT_140172174
    static constexpr float kHelper8660_FarZ        = 512.0f;  // DAT_140172240

    static constexpr float kColorScale255          = 255.0f;  // DAT_14017222c
}

// The byte `shift` of an 0xAARRGGBB colour, as a 0..1 float.
static inline float ColorChannel(const System::Color& c, int shift) {
    return static_cast<float>((c._value >> shift) & 0xFFu) / MenuSceneConst::kColorScale255;
}

// ============================================================
// Local helpers — reimplementations of small exe-internal helpers we depend on
// ============================================================

// ============================================================
// Phase B: append-only dynamic array helpers
// ============================================================
// Retail MenuDLT::Load (0x14009a930, re-decompiled 2026-09-13) inlines the
// THREE-argument `Array<T>::SetSize(count+1, elem)` six times — once per array
// per loop: `n = count+1; if (n != count) {...}`, `_size = n`, `if (cap < n) {
// cap = n; if (count != 0) cap = n*3; MemAlloc(cap << 3 | << 2, 8 | 4) }`, copy
// the prefix on a move else start at count, `dst[i] = i < count ? old[i] :
// elem`, skip a null computed address, `if (old != data) MemFree(old)`; the
// trait-derived row (Geometry* / uint32_t: byteCopy, no init, no destroy, align 8 /
// 4). The loop's +0xbd0 / +0xbe0 / +0xbf0 arrays are real Collection::Array
// members (MenuDialogLoopT.h).
template <typename T>
static void AppendToDynArray(Collection::Array<T>& arr, const T& value) {
    arr.SetSize(arr.GetSize() + 1, value);
}

// ============================================================
// Phase B: static-mesh iteration (props/buildings/foliage)
// ============================================================
// Mirrors the original MenuDLT::Load static-mesh loop (exe 0x14009A930). For
// each element of MenuScene::meshes (stride 0x20):
//   1. Alloc 0x120-byte MeshInstance, default-construct.
//   2. Append the new instance ptr to MenuDLT+0xbe0 (shared with particles).
//   3. Per-element setup (mirrors MenuScene_helper_3b60 at exe 0x1400a3b60):
//      - SetMesh(meshResources[meshIndex]).
//      - Geometry::SetTransform(FromYawPitchOffset(yaw, 0, position)).
//      - MeshInstance::_color = {color[0..2], g_fUVScale (1.0f, DAT_14017214c)}.
//      - Iterate GraphicsMesh::GetMaterialList(skinIndex) and call
//        MeshInstance::SetMaterialInstance for each entry.
//      - Vtable+0x08 (Update) called with Time(0).
//   4. Scene::AddObject(scene, instance) → uint32 id, append to MenuDLT+0xbf0.
//
// Implementation is split into two pieces so the loop can be driven
// in chunks across the Loading splash's Update ticks (eliminating the
// ~542ms synchronous freeze that would otherwise happen during MenuDLT::
// Startup). A synchronous all-in-one wrapper is provided below.

// Per-element processing — body of the original loop's `for (i = 0; i < meshCount; ++i)`.
// Returns the number of material slots wired (for stat reporting). Sets
// *outSkippedBadIdx=true when the meshIndex was out of range (the slot still got
// an empty inst added so id arrays stay in sync).
static int32_t MenuDialogLoopT_PhaseB_ProcessElement(
    MenuDialogLoopT* self, Graphics::Scene* scene,
    const MenuSceneMesh& elem, Graphics::GraphicsMesh* const* meshArr, int32_t meshArrCount,
    bool* outSkippedBadIdx) {
    *outSkippedBadIdx = false;

    // 1. Allocate + construct MeshInstance.
    void* mem = System::MemoryAllocator::MemAlloc(0x120, 8);
    if (!mem) return 0;
    Graphics::MeshInstance* inst = ::new (mem) Graphics::MeshInstance();   // B.3.9 EXA: the real class (exe table 0x14015e240 is its own)

    // 2. Append to MenuDLT+0xbe0 (shared instance list).
    AppendToDynArray<Graphics::Geometry*>(self->_instanceArray, inst);

    // 3. Per-element setup — mirror MenuScene_helper_3b60.
    int16_t meshIdx = elem.meshIndex;
    int16_t matIdx  = elem.skinIndex;

    // Defensive bound check on meshIdx — keeps id arrays in sync if a future
    // serializer regression ever produces an out-of-range index.
    if (meshIdx < 0 || meshIdx >= meshArrCount) {
        uint32_t id0 = scene->AddObject(inst);
        AppendToDynArray<uint32_t>(self->_sceneIdArray, id0);
        *outSkippedBadIdx = true;
        return 0;
    }

    // SetMesh(meshArray[meshIdx]) — value passed is the 8-byte ExternalPtr.
    Graphics::GraphicsMesh* mesh = meshArr[meshIdx];
    inst->SetMesh(Object::ExternalPtr<Graphics::GraphicsMesh>(mesh));

    // Geometry::SetTransform(FromYawPitchOffset(yaw, pitch=0, offset=elem.position)).
    Math::Transform transformBuf = Math::Transform::FromYawPitchOffset(
        elem.yaw, 0.f, elem.position);
    inst->SetTransform(transformBuf);

    // The element's three colour floats + g_fUVScale (exe DAT_14017214c, 1.0f).
    inst->_color[0] = elem.color[0];
    inst->_color[1] = elem.color[1];
    inst->_color[2] = elem.color[2];
    inst->_color[3] = 1.0f;

    // Iterate GetMaterialList(matIdx) and SetMaterialInstance per slot.
    int32_t matSlots = 0;
    if (mesh) {
        // `?GetMaterialList@GraphicsMesh@Graphics@@…H@Z` @0x1800126c0, the ONE-ARG
        // overload (the arg-less 0x1800126b0 twin returns element 0).
        const Collection::Array<Object::ExternalPtr<Graphics::MaterialInstance> >& mats =
            mesh->GetMaterialList(matIdx);
        // The `mats.GetData()` term is the old `&& matData`, kept because
        // nothing here proves GetSize() is 0 whenever _data is null.
        for (int32_t m = 0; m < mats.GetSize() && mats.GetData(); ++m) {
            inst->SetMaterialInstance(m, Object::ExternalPtr<Graphics::MaterialInstance>(mats[m].Get()));
        }
        matSlots = mats.GetSize();
    }

    // Vtable+0x08 (Update) with Time(0): slot 1 of Graphics::Geometry.
    System::Time timeZero;
    RtSystem_Time_Ctor(&timeZero, 0.0f);
    inst->Update(timeZero);

    // 4. Scene::AddObject + append id.
    uint32_t id = scene->AddObject(inst);
    AppendToDynArray<uint32_t>(self->_sceneIdArray, id);

    return matSlots;
}

// Initialize a chunked-loop state for PhaseB. Captures the inputs and
// validates fn pointers + array bounds. Sets state->totalCount=0 (so
// PhaseB_TickChunk early-exits) on any failure.
void MenuDialogLoopT_PhaseBState::InitState(MenuDialogLoopT* self, MenuScene* menuScene, Graphics::Scene* scene) {
    this->self           = self;
    this->menuScene      = menuScene;
    this->scene          = scene;
    this->elemArr        = nullptr;
    this->meshArr        = nullptr;
    this->meshArrCount   = 0;
    totalCount     = 0;
    index          = 0;
    added          = 0;
    skippedBadIdx  = 0;
    matSlotsTotal  = 0;

    if (!self || !menuScene || !scene) return;
    // B.3.5: SetMesh / Scene::AddObject / Geometry::SetTransform are members now,
    // so their readiness terms and their `%p`s are gone; B.3.9 EXA took the
    // MeshInstance ctor door's (the class is constructed). MemoryAllocator stays;
    // the Time ctor is called by name (B3R).
    if (!System::MemoryAllocator::_singleton) {
        printf("[MenuDLT::PhaseB] missing required fn ptrs — skipping\n");
        return;
    }

    // Count is read from MenuScene+0x38 just like the original (FUN_14009a930
    // line: `if (0 < *(int *)(*(longlong *)(param_1 + 0xb90) + 0x38))`).
    // Menu scenes have ~12-15K props (foliage, rocks, decals); the variation
    // between runs is because MenuDLT::Load picks Scene[0..9].rsc randomly.
    MenuScene* ms = this->menuScene;
    int32_t meshCount    = ms->meshes.GetSize();
    int32_t meshArrCount = ms->meshResources.GetSize();  // bound for meshIndex
    printf("[MenuDLT::PhaseB] MenuScene+0x38=%d (arr30 elems) +0x48=%d "
           "(arr40 GraphicsMesh count)\n", meshCount, meshArrCount);
    if (meshCount <= 0) {
        printf("[MenuDLT::PhaseB] MenuScene+0x38 count=%d — no static meshes\n", meshCount);
        return;
    }

    MenuSceneMesh* elemArr = ms->meshes.GetData();
    Graphics::GraphicsMesh** meshArr = ms->meshResources.GetData();
    if (!elemArr || !meshArr) {
        printf("[MenuDLT::PhaseB] elemArr=%p meshArr=%p (count=%d) — skipping\n",
               (void*)elemArr, (void*)meshArr, meshCount);
        return;
    }

    this->elemArr      = elemArr;
    this->meshArr      = meshArr;
    this->meshArrCount = meshArrCount;
    totalCount   = meshCount;
}

// Process up to maxElements elements of the PhaseB iteration. Returns the
// number of elements processed this call. Returns 0 when the loop has fully
// completed (state->index == state->totalCount). Caller drives this across
// multiple Update ticks.
int32_t MenuDialogLoopT_PhaseBState::TickChunk(int32_t maxElements) {
    if (totalCount <= 0) return 0;
    if (index >= totalCount) return 0;
    if (maxElements <= 0) maxElements = 1;

    int32_t end = index + maxElements;
    if (end > totalCount) end = totalCount;

    int32_t processed = 0;
    for (int32_t i = index; i < end; ++i) {
        const MenuSceneMesh& elem = elemArr[i];
        bool skippedBadIdx = false;
        int32_t matSlots = MenuDialogLoopT_PhaseB_ProcessElement(
            self, scene, elem,
            meshArr, meshArrCount, &skippedBadIdx);
        if (skippedBadIdx) ++this->skippedBadIdx;
        else               ++added;
        matSlotsTotal += matSlots;
        ++processed;
    }
    index = end;

    if (index >= totalCount) {
        printf("[MenuDLT::PhaseB] static meshes: added=%d/%d wired=%d skipped(badIdx)=%d "
               "(%d material slots)\n",
               added + this->skippedBadIdx, totalCount,
               added, this->skippedBadIdx, matSlotsTotal);
        fflush(stdout);
    }
    return processed;
}

// Synchronous wrapper — preserves the original DLL-mode signature. Loops
// PhaseB_TickChunk until done. Standalone uses the chunked state directly
// from ChunkedSceneLoader to avoid the freeze.
void MenuDialogLoopT::LoadStaticMeshes_PhaseB(MenuScene* menuScene, Graphics::Scene* scene) {
    MenuDialogLoopT_PhaseBState state;
    state.InitState(this, menuScene, scene);
    if (state.totalCount <= 0) return;
    // Pass totalCount as the chunk size — entire iteration runs in one call.
    state.TickChunk(state.totalCount);
}

// ============================================================
// Phase C: particle iteration (chimney smoke, fountains, etc)
// ============================================================
// Mirror of the particle loop in MenuDLT::Load (exe 0x14009A930). Steps for
// each element of MenuScene::particles:
//   1. Allocate ParticleSystemInstance (0xe0 bytes, align 8) + ctor.
//   2. Append to MenuDLT+0xbe0 (all-instance array, shared with meshes).
//   3. Configure via MenuScene_helper_3ca0 (exe 0x1400a3ca0):
//      - SetParticle(particleResources[particleIndex]) (slot 9)
//      - SetTransform(FromYawPitchOffset(yaw, 0 pitch, position))
//      - Update(Time(0)) — initial tick.
//   4. Append to MenuDLT+0xbd0 (particle-only array — drives per-frame Update).
//   5. Scene::AddObject(scene, particle) → uint32 id, append to MenuDLT+0xbf0.
void MenuDialogLoopT::LoadParticles_PhaseC(MenuScene* menuScene, Graphics::Scene* scene) {
    if (!menuScene || !scene) return;
    // B.3.5: Scene::AddObject and Geometry::SetTransform are members now; the
    // Time ctor door stays. ENDGAME B.3.9 GFX (B9-13): the
    // ParticleSystemInstance CTOR door is gone -- the class's real ctor below.
    if (!System::MemoryAllocator::_singleton) {
        printf("[MenuDLT::PhaseC] missing required fn ptrs — skipping\n");
        return;
    }

    int32_t particleCount = menuScene->particles.GetSize();
    if (particleCount <= 0) {
        printf("[MenuDLT::PhaseC] MenuScene+0x58 count=%d — no particles\n", particleCount);
        return;
    }

    MenuSceneParticle* elemArr = menuScene->particles.GetData();
    Graphics::Particle** resArr = menuScene->particleResources.GetData();
    if (!elemArr || !resArr) {
        printf("[MenuDLT::PhaseC] elemArr=%p resArr=%p (count=%d) — skipping\n",
               (void*)elemArr, (void*)resArr, particleCount);
        return;
    }

    int32_t added = 0;
    for (int32_t i = 0; i < particleCount; ++i) {
        // 1. Allocate + construct ParticleSystemInstance.
        void* mem = System::MemoryAllocator::MemAlloc(0xe0, 8);
        if (!mem) continue;
        Graphics::ParticleSystemInstance* inst =
            ::new (mem) Graphics::ParticleSystemInstance();   // B.3.9 GFX: the real ctor

        // 2. Append to all-instance array (+0xbe0).
        AppendToDynArray<Graphics::Geometry*>(_instanceArray, inst);

        // 3. helper_3ca0 inline.
        const MenuSceneParticle& elem = elemArr[i];
        Graphics::Particle* particleResource = resArr[elem.particleIndex];

        // `?SetParticle@ParticleInstance@Graphics@@` @0x180104a00 (slot 9).
        inst->SetParticle(Object::ExternalPtr<Graphics::Particle>(particleResource));

        // SetTransform(FromYawPitchOffset(yaw, pitch=0, offset=elem.position)).
        Math::Transform transformBuf = Math::Transform::FromYawPitchOffset(
            elem.yaw, 0.f, elem.position);
        inst->SetTransform(transformBuf);

        // Initial Update(Time(0)) — slot 1 of Graphics::Geometry.
        System::Time timeZero;
        RtSystem_Time_Ctor(&timeZero, 0.0f);
        inst->Update(timeZero);

        // 4. Append to particle-only array (+0xbd0) — drives per-frame Update.
        AppendToDynArray<Graphics::Geometry*>(_particleArray, inst);

        // 5. Scene::AddObject + append id.
        uint32_t id = scene->AddObject(inst);
        AppendToDynArray<uint32_t>(_sceneIdArray, id);
        ++added;
    }

    printf("[MenuDLT::PhaseC] particles: added=%d/%d\n", added, particleCount);
    fflush(stdout);
}

// ============================================================
// Per-frame particle Update — iterate MenuDLT+0xbd0 and dispatch vtable+0x08.
// ============================================================
// Mirrors the inner loop of MenuDLT::Update (exe 0x14009b6e0):
//   for (i = 0; i < self+0xbd8; ++i) {
//     particle = ((Geometry**)self+0xbd0)[i];
//     particle->vtable[+0x08](particle, time);
//   }
// Without this, particles are added to the scene but never simulate — emitters
// don't spawn sprites, so chimneys don't smoke.
void MenuDialogLoopT::TickParticles(const System::Time* time) {
    const System::Time& now = *time;   // the slot's Time& (RDX of 0x14009b6e0), never tested

    // Retail reads the +0xbd0 array AS `Geometry**` (exe 0x14009b6e0) and re-reads its
    // size (+0xbd8) each turn; neither the array nor an element is tested.
    Collection::Array<Graphics::Geometry*>& particles =
        _particleArray;
    for (int32_t i = 0; i < particles.GetSize(); ++i)
        particles.GetData()[i]->Update(now);   // slot 1 of Graphics::Geometry

    // Match retail MenuDLT::Update ordering (exe 0x14009b6e0):
    //   per-instance Update loop  →  _frameIndex++  →  Scene::Clean
    // Order matters: incrementing _frameIndex BEFORE the Update loop would
    // make every shared-emit-state instance early-return on its first update;
    // and skipping Scene::Clean leaves Geometry dirty bits set, causing the
    // Renderer to read stale bounds/transforms each frame (visible flicker).
    Graphics::ParticleInstance::IncrementInstanceIndex();   // a class static
    _scene->Clean();   // 0x14009b8ca: inside Update's `_scene` test, no second one
}

// ============================================================
// Phase A1: pre-PhaseB allocation + MenuScene load
// ============================================================
// Mirror of the pre-PhaseB block in MenuDLT::Load (exe 0x14009A930):
// loads Game/MenuScene/SceneN.rsc, allocates the 5 scene-graph objects
// (Renderer/Scene/SunLight/PlayerController/MeshInstance), wires the main
// mesh, adds the 3 global objects (sceneGeom/sunLight/mainMesh), and copies
// the deserialized camera controller into the live one.
//
// Cheap (~10ms): doesn't touch the 12K-15K element static-mesh array.
// Standalone calls this from the Loading splash's Stub_Startup so the
// MenuScene External + scene-graph fields are populated before the heavy
// PhaseB chunked iteration starts.
bool MenuDialogLoopT::LoadSceneA1() {
    // Required Runtime functions
    // B.3.5: SetMesh and Scene::AddGlobalObject are members now; B3R: the
    // Renderer / Scene / Time ctors are called by name.
    if (!System::MemoryAllocator::_singleton) {
        printf("[MenuDLT::LoadScene] missing required fn pointers — bailing out\n");
        return false;
    }

    // Required factory
    if (!::Object::ExternalFactory::_singleton) {
        printf("[MenuDLT::LoadScene] missing factory/filename helpers — bailing out\n");
        return false;
    }

    ::Object::ExternalFactory* efactory = ::Object::ExternalFactory::_singleton;

    // Engine::Random lives at self+0x168 (UpdateLoop base + DialogLoopT field area).
    uint32_t variant = _random.Rand(10);

    // Diagnostic lever: BANISHED_MENU_SCENE pins the menu scene index (0..9) so
    // the otherwise-random MenuScene/SceneN.rsc choice becomes deterministic.
    // Lets us reproduce the intermittent menu heap-overflow per-scene instead
    // of waiting for the RNG to land on the bad one. Unset = normal random pick.
    if (Opt().menuScene >= 0) {
        unsigned forced = static_cast<unsigned>(Opt().menuScene) % 10u;
        printf("[MenuDLT::LoadScene] BANISHED_MENU_SCENE pinned scene %u "
               "(rng would have picked %u)\n", forced, variant);
        variant = forced;
    }

    // Format the scene path (Game/MenuScene/Scene{0..9}.rsc).
    System::String stringBuf =
        System::String::Format("Game/MenuScene/Scene%i.rsc", variant);

    // Filename(const String&) is what retail builds here: Format gave us a String.
    System::Filename fnBuf(stringBuf);

    // THE MENU'S RESOURCES CARRY THE MENU'S BIT. Retail loads the scene inside
    // MenuDialogLoopT::Load on the loader thread, where Loader::Run has just
    // published THIS loop's _loadBit as the factory's _cleanBitIndex, so every
    // External the scene pulls in (the scene, its meshes, particles, geometry
    // materials) is marked with the menu's usage bit. The standalone runs this
    // load from the splash (ChunkedSceneLoader, main thread) while the index
    // still holds the Loading splash's bit — so the scene belonged to the splash,
    // and the splash's retirement (~UpdateLoop -> ExternalFactory::Clean) evicted
    // it under the live menu: MenuScene freed, its WorldGeometry freed (D9 #12),
    // the menu's Graphics::Scene still holding it. Publish the menu's bit for
    // the load, as Loader::Run would, and hand the index back afterwards (the
    // loader thread is idle here: the splash waits for EngineLoader::IsBusy()).
    struct PublishLoadBit {
        ::Object::ExternalFactory* f;
        int32_t saved;
        PublishLoadBit(::Object::ExternalFactory* factory, int32_t bit)
            : f(factory), saved(factory->_cleanBitIndex) { f->_cleanBitIndex = bit; }
        ~PublishLoadBit() { f->_cleanBitIndex = saved; }
    } publish(efactory, _loadBit);
    ::Object::External* obj = efactory->FindOrLoadAs<::Object::External>(fnBuf);

    // Original validates via IsDerivedFrom against TypeInfo_MenuScene at exe 0x1401c59f0.
    // In standalone the Runtime factory tree doesn't have every base type registered,
    // so IsDerivedFrom NULL-derefs on the lookup. Trust FindOrLoad's return — if it
    // gave us a non-NULL External and the path resolved, it's the right type.
    MenuScene* menuScene = static_cast<MenuScene*>(obj);

    _menuScene = menuScene;

    if (!menuScene) {
        printf("[MenuDLT::LoadScene] failed to load Game/MenuScene/Scene%u.rsc\n", variant);
        return false;
    }
    printf("[MenuDLT::LoadScene] loaded MenuScene variant %u at %p\n", variant, (void*)menuScene);

    // --- Allocate Renderer (0x50 bytes, alignment 4) ---
    Graphics::Renderer* renderer = static_cast<Graphics::Renderer*>(System::MemoryAllocator::MemAlloc(0x50, 4));
    if (renderer) ::new (renderer) Graphics::Renderer();   // Renderer() 0x180105180
    _renderer = renderer;

    // --- Allocate Graphics::Scene (0x78 bytes, alignment 8) ---
    Graphics::Scene* gScene = static_cast<Graphics::Scene*>(System::MemoryAllocator::MemAlloc(0x78, 8));
    if (gScene) ::new (gScene) Graphics::Scene();   // Scene() 0x180102730
    _scene = gScene;

    // --- Allocate SunLight (0x108 bytes, alignment 8) ---
    // Retail: the imported ??0SunLight, then the exe's own copy of the table
    // (0x14015e290, slot 0 = the exe's scalar deleting dtor 0x140019e40) --
    // i.e. `new Graphics::SunLight()` compiled in the exe. ENDGAME B.3.9 pilot:
    // the real class's constructor does both (SunLight.h, B38 D1 / B39 D1); the
    // ??0SunLight door and its engine_funcs global are gone.
    void* sunMem = System::MemoryAllocator::MemAlloc(0x108, 8);
    Graphics::SunLight* sunLight = sunMem ? ::new (sunMem) Graphics::SunLight() : nullptr;
    _sunLight = sunLight;

    // --- Allocate the camera controller (0x10C bytes, alignment 4) ---
    void* controllerMem = System::MemoryAllocator::MemAlloc(0x10C, 4);
    PlayerController* transform = controllerMem ? ::new (controllerMem) PlayerController() : nullptr;   // 0x140098800
    _transform = transform;

    // --- Allocate MeshInstance (0x120 bytes, alignment 8) ---
    // Retail: `??0MeshInstance` then the exe's copy 0x14015e240 of the class's
    // OWN table (Ghidra: slot 0 the exe scalar deleting dtor 0x140019e80, 1-8
    // thunks onto MeshInstance / Geometry members) = `new MeshInstance()`.
    void* meshMem = System::MemoryAllocator::MemAlloc(0x120, 8);
    Graphics::MeshInstance* mainMesh = meshMem ? ::new (meshMem) Graphics::MeshInstance() : nullptr;   // B.3.9 EXA
    _mainMesh = mainMesh;

    // --- SetMesh from MenuScene::terrainMesh (the GraphicsMesh ExternalPtr) ---
    Graphics::GraphicsMesh* meshExt = menuScene->terrainMesh;
    if (mainMesh && meshExt) {
        mainMesh->SetMesh(Object::ExternalPtr<Graphics::GraphicsMesh>(meshExt));
    }

    // --- Time(0.0f) construction + Update vtable[+0x08] dispatch on the MeshInstance ---
    if (mainMesh) {
        System::Time timeZero;
        RtSystem_Time_Ctor(&timeZero, 0.0f);
        // Slot 1 of Graphics::Geometry is `Update(const Time&)`.
        mainMesh->Update(timeZero);
    }

    // --- Capture scene Geometry pointer (MenuScene::geometry) ---
    _sceneGeometry = menuScene->geometry;

    // --- Set Geometry::_detailId (+0x28) on each Geometry. Original:
    //   mainMesh->[+0x28] = 0   (default category)
    //   sunLight->[+0x28] = 0
    //   sceneGeom->[+0x28] = 1  (terrain category)
    if (mainMesh)  mainMesh->_detailId = 0;
    if (sunLight)  sunLight->_detailId = 0;
    Graphics::Geometry* sceneGeom = _sceneGeometry;
    if (sceneGeom) sceneGeom->_detailId = 1;

    // --- Add to global scene list (terrain Geometry, SunLight, MeshInstance) ---
    if (gScene) {
        if (sceneGeom) gScene->AddGlobalObject(sceneGeom);
        if (sunLight)  gScene->AddGlobalObject(sunLight);
        if (mainMesh)  gScene->AddGlobalObject(mainMesh);
    }

    // --- Copy the camera controller from MenuScene+0x88 into the live one (+0xbb8) ---
    // MenuScene_helper_a3a0 (0x14001a3a0): 13 dwords + Camera::operator=; the
    // tree copies all 0x10C bytes (Camera's three pad bytes included).
    if (transform) {
        *transform = menuScene->transform;
        // Cache yaw / pitch (+0x28 / +0x2C) at MenuDLT+0xb7c/+0xb80.
        _scrollX = transform->yaw;
        _scrollY = transform->pitch;
    }

    // Skip TradeWork (gameSettings[0x16]) — DLC ship, out of scope.
    // Static-mesh PhaseB + particle PhaseC happen in their own public funcs;
    // the synchronous all-in-one wrapper below calls them here.
    printf("[MenuDLT::LoadSceneA1] OK: renderer=%p gScene=%p sunLight=%p transform=%p "
           "mainMesh=%p sceneGeom=%p staticMeshCount=%d\n",
           (void*)renderer, (void*)gScene, (void*)sunLight, (void*)transform, (void*)mainMesh,
           (void*)sceneGeom, menuScene->meshes.GetSize());
    return true;
}

// ============================================================
// Phase A3: post-PhaseB particle iteration (small, single-call)
// ============================================================
// Thin wrapper around PhaseC for symmetry with A1. Standalone calls this
// from ChunkedSceneLoader after the chunked PhaseB completes; DLL-mode
// calls it from MenuDialogLoopT_LoadScene_PhaseA below.
void MenuDialogLoopT::LoadSceneA3() {
    MenuScene* menuScene = _menuScene;
    Graphics::Scene* gScene = _scene;
    if (!menuScene || !gScene) {
        printf("[MenuDLT::LoadSceneA3] WARN: menuScene=%p gScene=%p — skipping\n",
               (void*)menuScene, (void*)gScene);
        return;
    }
    LoadParticles_PhaseC(menuScene, gScene);
}

// ============================================================
// Phase A: full synchronous Load — A1 + PhaseB + A3 (DLL-mode signature)
// ============================================================
// Preserves the original public function so any synchronous caller (e.g.
// menudialogloopt_stubs.cpp's fallback path) keeps working unchanged.
// Standalone splits the work via LoadSceneA1 + PhaseB_TickChunk + LoadSceneA3
// driven from the Loading splash so the 542ms PhaseB freeze becomes invisible.
bool MenuDialogLoopT::LoadScene_PhaseA() {
    if (!LoadSceneA1()) return false;
    LoadStaticMeshes_PhaseB(_menuScene, _scene);
    LoadSceneA3();
    return true;
}

// ============================================================
// Phase A: Draw — render menu scene with sun lighting + fog
// ============================================================
void MenuDialogLoopT::DrawScene_PhaseA(float alpha, Video::Context* context) {
    MenuScene* menuScene = _menuScene;
    if (!menuScene || !context) return;

    // B.3.5: SunLight::Set, Context::SetFog and — since batch K1 —
    // `Graphics::Renderer::Draw` are all members.
    PlayerController* transform = _transform;
    Graphics::Renderer* renderer = _renderer;
    Graphics::SunLight* sunLight = _sunLight;
    Graphics::Scene* gScene = _scene;
    if (!transform || !renderer || !sunLight || !gScene) return;

    // Per-frame camera update: PlayerController::UpdateCamera 0x140098660 on +0xbb8
    // (0x14009b474..0x14009b4a0: XMM1 = [0x1401721dc] fovY, XMM2 = [0x140172174] near,
    // XMM3 = [0x140172240] far) -- the same body the game loop calls.
    transform->UpdateCamera(MenuSceneConst::kHelper8660_FovX,
                            MenuSceneConst::kHelper8660_NearZ,
                            MenuSceneConst::kHelper8660_FarZ);

    // SunLight::Set(self, dir*, sunVec4*, ambientVec4*).
    // Colors are 0xAARRGGBB; the Vector4 is (R, G, B, 1.0). This mirrors the
    // original disasm (exe 0x14009b440), which feeds e.g. (ms[0x196], ms[0x195],
    // ms[0x194]) for ambient and (ms[0x19A], ms[0x199], ms[0x198]) for sun.
    //
    // SunLight direction is yaw + pitch derived from MenuScene+0x1AC..+0x1B3.
    // Verified via disassembly of MenuDLT::Draw (exe 0x14009b440):
    //   yaw   = *(float*)(menuScene + 0x1AC)
    //   pitch = -*(float*)(menuScene + 0x1B0)   (XOR'd with sign-flip mask 0x80000000)
    // The struct labels these as sunIntensity/ambientIntensity but the disasm
    // reads them as a sun-direction yaw/pitch pair; defaults of 120°/65° fit
    // a directional-light orientation, not an intensity scalar.
    float sunYaw   = menuScene->sunIntensity;
    float sunPitch = -menuScene->ambientIntensity;

    // ?FromYawPitch@Vector3@Math@@SA?AV12@AEBVScalar@2@0@Z  retail 0x1800220b0
    Math::Vector3 dirVec = Math::Vector3::FromYawPitch(sunYaw, sunPitch);

    const Math::Vector4 sunColorVec(ColorChannel(menuScene->sunColor, 16),
                                    ColorChannel(menuScene->sunColor, 8),
                                    ColorChannel(menuScene->sunColor, 0), 1.0f);
    const Math::Vector4 ambientVec(ColorChannel(menuScene->ambientColor, 16),
                                   ColorChannel(menuScene->ambientColor, 8),
                                   ColorChannel(menuScene->ambientColor, 0), 1.0f);

    // Original arg order: SunLight::Set(self, dir, sunVec4, ambientVec4) — R8 is
    // sun, R9 is ambient.
    sunLight->Set(dirVec, sunColorVec, ambientVec);

    // Push the +0x1B4 Vector4 into Video::Context+0x3b8/+0x288 and OR fog-enable bits.
    // Original disasm:
    //   ctx[+0x3B8..+0x3C4] = MenuScene[+0x1B4..+0x1C0]  (4 dwords)
    //   ctx[+0x288..+0x294] = same 4 dwords             (mirrored)
    //   ctx[+0x10] |= 0x10800                            (fog/sky enable bits)
    context->_userPosition = menuScene->weatherTerms;
    // Context +0x288: Context::PixelConstantStaging::pixel[4] (Video/Context.h).
    std::memcpy(&context->_pixelConstantStaging.pixel[4], &menuScene->weatherTerms, 16);
    context->_dirty |= RtVideo::kDirtyGlobalConstants | RtVideo::kDirtyPixelConstants;

    // Fog color Vector4 — the skyColor's RGB; the 4th component is fog density.
    const Math::Vector4 fogColor(ColorChannel(menuScene->skyColor, 16),
                                 ColorChannel(menuScene->skyColor, 8),
                                 ColorChannel(menuScene->skyColor, 0),
                                 menuScene->fogDensity);

    context->SetFog(menuScene->fogStart, menuScene->fogEnd, fogColor);

    // The Renderer's shader detail from the video configuration (+0x30), as
    // the original's per-frame write into Renderer+0x3C.
    if (::Video::Configuration::_singleton) {
        renderer->_shaderDetail = ::Video::Configuration::_singleton->shaderDetail;
    }

    const Graphics::Camera& camera = transform->camera;

    // EXT SEAM (NOT RETAIL, docs/extensions/handins/AO-MENU.md): ext.ambient_occlusion
    // arms Graphics::Renderer::_extAo for this one Draw, as GameUpdateLoop::Draw step 7
    // does for the game's scene. The menu's own PlayerController carries the same
    // camera-to-target `distance` the radius scales with (CalcTransform: eye = target -
    // dir * distance), so Arm takes it unchanged. Row off: Enabled() is one load, the
    // seam stays null, retail's Draw.
    const bool aoArmed = Ext::AmbientOcclusion::Enabled() && Ext::AmbientOcclusion::Arm(*transform);
    renderer->Draw(*context, alpha, *gScene, camera);
    if (aoArmed) Ext::AmbientOcclusion::Disarm();
}

// ============================================================
// Per-frame camera derivation (from MainMenuDialog_impl_b6e0 + helper_83f0)
// ============================================================
// Mirror of the camera-animation block in MenuDLT::Update (exe 0x14009b6e0):
//
//   1. Animate yaw at MenuDLT+0xb78:
//        animYaw[+0xb78] += dt * 11.25f  (DAT_1401721b4 = spin scale)
//        controller.yaw = cos(animYaw) * 2.0f + cachedYaw[+0xb7c]
//        (DAT_140172174 = 2.0f orbit radius — the same const used as helper-8660 nearZ)
//
//   2. PlayerController::CalcTransform (0x1400983f0): eye = target -
//      FromYawPitch(yaw, pitch) * distance; transform = FromLookAt(eye, target).
//
//   3. Convert to Transform3 and apply to the embedded Camera:
//        Graphics::Camera::SetCameraToWorld(controller.camera, transform.AsTransform3())
// Update runs this inside its `_scene` (+0xba0) test, as retail's block does; the
// controller (+0xbb8) is read with no test of its own (0x14009b812).
void MenuDialogLoopT::UpdateCamera_PhaseA(float deltaSeconds) {
    PlayerController* transform = _transform;

    // 1. Animate yaw at MenuDLT+0xb78 (live) using cached scrollX at +0xb7c.
    //    Constants from the original exe (verified via Ghidra read_memory):
    //      DAT_1401721b4 = 11.25  (yaw spin rate, *degrees* per AsSeconds unit)
    //      DAT_140172174 = 2.0    (orbit radius — also doubles as helper-8660 nearZ)
    //
    // Banished's Math::Scalar uses **degrees**, so animYaw accumulates in
    // degrees and the cosine must be evaluated as cos(deg2rad(animYaw)).
    static constexpr float kYawSpinRate    = 11.25f;     // deg/sec
    static constexpr float kYawOrbitRadius = 2.0f;       // deg
    static constexpr float kDegToRad       = 3.14159265358979323846f / 180.0f;
    float animYaw = _animYaw + deltaSeconds * kYawSpinRate;
    _animYaw = animYaw;

    float cosAnim = std::cos(animYaw * kDegToRad);
    float cachedYaw = _scrollX;

    // The yaw input to FromYawPitch, with the orbit offset.
    transform->yaw = cosAnim * kYawOrbitRadius + cachedYaw;

    // 2. The look-at transform (writes +0x00..+0x1B; the target and yaw /
    //    pitch / distance are preserved).
    Math::Transform lookAt = transform->CalcTransform();   // 0x1400983f0

    // 3. Convert to Transform3 (48B = 3x4 matrix) and apply to the Camera at +0x34.
    Math::Transform3 t3Buf = lookAt.AsTransform3();
    transform->camera.SetCameraToWorld(t3Buf);

    // Diagnostic: log the first few frames so we can verify the camera transform
    // is non-identity (the underlying bug we're fixing).
    static int logCount = 0;
    if (logCount++ < 5) {
        [[maybe_unused]] const Math::Vector3& pos = transform->transform.translation;
        printf("[MenuDLT::UpdateCamera] frame=%d animYaw=%.3f yaw=%.3f pitch=%.3f "
               "dist=%.3f target=(%.2f,%.2f,%.2f) "
               "camPos=(%.2f,%.2f,%.2f)\n",
               logCount, animYaw, transform->yaw, transform->pitch, transform->distance,
               transform->target.x, transform->target.y, transform->target.z,
               pos.x, pos.y, pos.z);
    }
}

// ============================================================
// MenuDialogLoopT_TickAudio — per-frame Audio::Device::Update
// ============================================================
// Pull Audio::Device live from MainLoop::_engine->GetAudioDevice()
// (Engine::Engine::GetAudioDevice at Runtime 0x18000c970: +0x6d0).
void MenuDialogLoopT_TickAudio(const System::Time* time) {
    if (!time) return;
    if (!::Engine::MainLoop::_singleton) {
        static bool warned = false;
        if (!warned) {
            warned = true;
            printf("[MenuDLT::TickAudio] WARN: MainLoop singleton not populated\n");
        }
        return;
    }
    ::Engine::MainLoop* mainLoop = ::Engine::MainLoop::_singleton;
    ::Engine::Engine* engine = mainLoop->_engine;  // MainLoop::_engine (+0x78)
    if (!engine) {
        static bool warned = false;
        if (!warned) {
            warned = true;
            printf("[MenuDLT::TickAudio] WARN: MainLoop->m_engine is NULL\n");
        }
        return;
    }
    Audio::Device* audioDevice = engine->GetAudioDevice();  // Engine::_audioDevice (+0x6d0)
    if (!audioDevice) {
        static bool warned = false;
        if (!warned) {
            warned = true;
            printf("[MenuDLT::TickAudio] WARN: Engine[+0x6d0] (Audio::Device) is NULL\n");
        }
        return;
    }

    // Trace first few ticks so we can confirm the call is firing.
    static int tickCount = 0;
    if (tickCount < 3) {
        printf("[MenuDLT::TickAudio] tick=%d audioDevice=%p time=%p -> Audio::Device::Update\n",
               tickCount, (void*)audioDevice, (const void*)time);
        ++tickCount;
    }

    audioDevice->Update(*time);
}

// ============================================================
// MenuDialogLoopT_LoadMusic — load Audio/Music/Menu.rsc into self+0xb88
// ============================================================
// Mirror of the first block of MenuDLT::Load (exe 0x14009a930), which runs
// right after Engine::DialogLoopT::Load:
//   Filename f("Audio/Music/Menu.rsc");
//   External* e = ExternalFactory::FindOrLoad(_singleton, f);
//   if (e && IsDerivedFrom(e, SoundEffect typeinfo)) keep; else null;
//   *(External**)(self+0xb88) = e;
// Standalone trusts FindOrLoad's return and skips the IsDerivedFrom check (the
// Runtime factory tree is incomplete → IsDerivedFrom NULL-derefs; same
// convention as LoadSceneA1's MenuScene load). Playback is started later in
// StartMusic (the original splits Load from the Startup-time PlayEffect).
bool MenuDialogLoopT::LoadMusic() {
    if (!::Object::ExternalFactory::_singleton) {
        printf("[MenuDLT::LoadMusic] missing factory/filename helpers — skipping\n");
        return false;
    }

    System::Filename fnBuf("Audio/Music/Menu.rsc");  // System::Filename
    // The SoundEffect is the loaded External (B10 downcast, no IsDerivedFrom: see above).
    Audio::SoundEffect* music =
        ::Object::ExternalFactory::_singleton->FindOrLoadAs<Audio::SoundEffect>(fnBuf);

    _music = music;
    printf("[MenuDLT::LoadMusic] Audio/Music/Menu.rsc -> %p (self+0xb88)\n", (void*)music);
    return music != nullptr;
}

// ============================================================
// MenuDialogLoopT_StartMusic — register audio scene + play menu music
// ============================================================
// Mirror of the audio portion of MenuDLT::Startup (exe 0x14009b930), run AFTER
// Engine::DialogLoopT::Startup:
//   *(Scene**)(self+0xd0) = (Scene*)(self+0xb30);   // dialog base audio scene
//   Audio::Scene::PlayEffect(self+0xb30, *(self+0xb88), 2, &Time(0));
// soundType/channel 2 is the music channel (same channel Pause toggles in
// MenuDLT::Update).
void MenuDialogLoopT::StartMusic() {

    // 1. Register the loop's embedded Audio::Scene (+0xb30) with the
    //    DialogInstance base at +0xd0 (where UI sound effects route). The
    //    original writes this even before PlayEffect.
    Audio::Scene* audioScene = &_audioScene;   // the embedded Audio::Scene (+0xb30)
    _ui._audioScene = audioScene;              // +0xd0 = UI (+0x40) + 0x90

    // 2. Play the looping menu music.
    Audio::SoundEffect* music = _music;
    if (!music) {
        printf("[MenuDLT::StartMusic] no music External at +0xb88 — silent menu\n");
        return;
    }
    System::Time timeZero;  // System::Time(0) — 8-byte zero ticks, passed by const ref.
    timeZero._ticks = 0;
    [[maybe_unused]] uint32_t handle = audioScene->PlayEffect(music, static_cast<Audio::Scene::SoundType>(2), timeZero);
    printf("[MenuDLT::StartMusic] PlayEffect(scene=%p music=%p chan=2) -> handle=%u\n",
           (void*)audioScene, (void*)music, handle);
    fflush(stdout);
}

// ============================================================
// MenuDialogLoopT_StopMusic — the audio tail of MenuDLT::Shutdown
// ============================================================
// Mirror of the audio statement at the tail of MenuDLT::Shutdown (exe
// 0x14009b280):
//   Engine::DialogLoopT::Shutdown(self);
//   Audio::Scene::StopAll(self+0xb30);          // 0x14009b413/1a  <- this
//   UI::UserInterface::EnableCursor(self+0x40, false);
//
// LOAD-BEARING, not cosmetic. The menu music is a STREAMED sound: its
// Audio::Stream thread (Runtime 0x1800a0f60) keeps a packed IO::FileInfo open
// and calls FileSystem::ReadFile (0x1800ace00) once per buffer slot, and
// ReadFile indexes `_packfiles[handle>>24]` with NO lock (0x1800ace31/39). The
// next thing the main thread does after this Shutdown is LoaderLoop::Startup ->
// Engine::Loader::Load (0x1800dc050) -> Modification::SetupFileSystem
// (0x1800aebb0), which on a load-a-save is dirty (SaveLoad::UpdateLoad
// 0x1800c2690 marks the mod list dirty unconditionally) and so runs
// ClosePackages (frees `_packfiles` and nulls it) + LoadPackages. Retail takes
// no lock anywhere on that path either. What keeps the stream thread out of
// ReadFile during the remount is THIS call: StopAll -> Device::StopPlatform
// (0x1800dea20) raises the stream's stop flag and signals its wake event, so
// the thread abandons its pass and closes the file before the remount starts.
// Without it the stream kept reading through the remount and faulted on the
// freed/null array (STATUS open defect (c), ~1 in 60 resave runs).
void MenuDialogLoopT::StopMusic() {
    _audioScene.StopAll();   // the embedded Audio::Scene (+0xb30)
}

// ============================================================
// MenuDialogLoopT_TickAudioScene — per-frame Audio::Scene::Update
// ============================================================
// Mirror of the Audio::Scene::Update call in MenuDLT::Update (exe 0x14009b6e0):
//   if (!Application::IsPaused()) {
//       Audio::Scene::Update(self+0xb30, *(Time*)time, Time(0),
//                            &Random(self+0x168), &listenerVec3{0,0,0}, 0.0f);
//   }
// Update makes the IsPaused / Pause(2, paused) handshake and calls this only when
// not paused, as retail. This is what advances the music stream each frame.
void MenuDialogLoopT::TickAudioScene(const System::Time* time) {
    Audio::Scene* audioScene = &_audioScene;   // the embedded Audio::Scene (+0xb30)

    System::Time elapsed = *time;
    System::Time timeZero; timeZero._ticks = 0;            // Time(0), passed by value
    const Math::Vector3 listener(0.0f, 0.0f, 0.0f);         // origin listener (2D menu music)

    audioScene->Update(elapsed, timeZero, _random, listener, 0.0f);

    static int n = 0;
    if (n < 3) {
        ++n;
        printf("[MenuDLT::TickAudioScene] Audio::Scene::Update(scene=%p) tick=%d\n",
               (void*)audioScene, n);
    }
}
