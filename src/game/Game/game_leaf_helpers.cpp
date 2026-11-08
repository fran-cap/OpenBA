// src/game/Game/game_leaf_helpers.cpp
// Closure-safe leaf reimplementations for the Game module (Wave: Game gap).
//
// These are small, fully closure-safe exe helpers referenced (via guarded
// ResolveAddr(Module::Exe, ...)) by src/game/Game/{mapgenerator,menuscene,
// gameupdateloop}.cpp. In standalone the exe is replaced, so each address must
// resolve to a registered reimpl or the call silently no-ops (leaving arrays
// unfreed / objects un-destroyed). Each body below is a faithful 1:1 port of the
// original (decompiled from Application-x64.exe) and depends only on already-
// resolvable primitives: System::String::~String (Runtime export, fn_String_Dtor),
// the static MemFree (fn_MemFreeStatic), already-registered tree-free 0x14000e320,
// and Scene::RemoveGlobalObject (Runtime export).
//
// ALL of these run on the shutdown / dtor / teardown path, which the autostart
// A/B path never reaches — so registering them is purely additive (no world-build
// regression), it just removes a latent silent-skip.
//
// Registered by standalone/reg_game_leaf.cpp.

// ENDGAME B.3.5 Video/Graphics caller batch: Graphics::Scene is a real class.
// Ahead of "resolve.h" on purpose — resolve.h pulls <windows.h>, and
// <wingdi.h>'s global `Rectangle` function hides `Volume::Rectangle`'s class
// name inside Volume.h's own static_asserts (Geometry.h includes Volume.h).
#include "graphics/Scene.h"
#include "object/TypeInfo.h"
#include "graphics/Camera.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "system/MemoryAllocator.h"   // System::MemoryAllocator::MemAlloc / MemFree
#include "graphics/Material.h"   // ENDGAME B.3.9 TI-REST: Graphics::MaterialInstance::TypeId()
#include "Game/GameUpdateLoop.h"   // caller batch CB6: the world's named fields
#include "system/Filename.h"
#include "system/String.h"
#include "collection/Collection.h"
#include "object/External.h"   // ENDGAME B.3.3 step 6A
#include "video/ImageBuffer.h"   // B.3.x W0: Game_ImageCaptureDtor deletes by name
#include <cstdint>
#include <initializer_list>
#include <new>
#include "Game/exe_bodies.h"
#include "Game/MapGenerator.h"        // ExternalRef / InitialStructure, the element rows (B4 P2 GAME-1)
#include "Game/Minimap.h"             // Game_ImageCaptureDtor's class (B.4 GAME-a)
#include "Game/MenuScene.h"           // MenuSceneTreeMap (0x14001b860)
#include "Game/WeatherSimulation.h"   // Game_WeatherDtor's class (B.4 GAME-a)
#include "Work/exe_bodies.h"   // WorkBase_TreeNodeFree_e320

// ------------------------------------------------------------------
// String-array destructors 0x14001b030 (0x18 ExternalRef) / 0x14001b0a0 (0x30
// InitialStructure): B4 X7-GAME-A made MapGenerator's two String pools typed
// `Collection::Array`s over the rows in mapgenerator.cpp (MapGeneratorRows), so
// those bodies are the pools' own `~Array`; Game_DestroyStringArray30 and the
// duplicate rows here had no caller left and are deleted. (0x14000a8a0, the
// 0x28 event-log entry, is EventLog_EventArray_Destroy in eventlog.cpp.)
// ------------------------------------------------------------------

// FUN_14001b030 for BuildingGoal's entry array (~BuildingGoal, 0x140025e80): B4 Y
// GAMEA typed BuildingGoal::_buildingTypes as NamedExternalRefArray (Goal/goal_exe_helpers.h),
// so the body is that member's own `~Array`; Game_DestroyStringArray18 is deleted.

// FUN_14000a8a0 (gameupdateloop.cpp, EventNotifier_Cleanup) — String at element+8,
// stride 0x28: EventLog_EventArray_Destroy (eventlog.cpp), called by name; the
// one-line forwarder Game_EventNotifierCleanup was deleted in B.4 wave X.

// ------------------------------------------------------------------
// FUN_140018220 — CharacterGrid::~CharacterGrid (Component/CharacterGrid.h): the five
// `Collection::Array` members' ~Array, +0x50 down to +0x10 (reverse declaration order;
// MemFree(data); data = 0; one qword over size+capacity = 0). The defaulted dtor is that;
// GameUpdateLoop::Shutdown runs it by name (Game_MusicPlayerDtor, its C body, is gone).
// ------------------------------------------------------------------

// ------------------------------------------------------------------
// FUN_1400a4fc0 (Minimap::~Minimap, run by GameUpdateLoop::Shutdown) — virtual scalar-dtor
// of the two sub-objects at +8/+0x10, then frees the two buffers at +0x28/+0x18.
// ------------------------------------------------------------------
Minimap::~Minimap() {
    // Retail is (**(code **)*puVar1)(puVar1, 1): deref the object to get its
    // vtable, deref THAT to get slot 0, then call. This port had only one deref
    // and so called the vtable's own address as if it were code — EXEC of
    // g_NoiseUI_ImageBufferVtable_Standalone when GameUpdateLoop::Shutdown
    // finally reached the minimap ImageBuffers.
    for (Video::ImageBuffer* obj : {terrainImage, overlayImage}) {   // +0x08, +0x10
        if (!obj) continue;
        delete obj;   // slot 0, free
    }
    // Then the members' two trivial-element `~Array`s, +0x28 and +0x18 in that order
    // (reverse declaration order: MemFree, data = 0, one qword over size+capacity).
}

// ------------------------------------------------------------------
// 0x14001b860 — MenuSceneTreeMap::~MenuSceneTreeMap (Game/MenuScene.h; MenuScene
// holds two, at +0x1C8 and +0x1E8, destroyed by ~MenuScene's member
// destruction). Free the tree at +0x10 (the root's two children through the
// shared recursive free 0x14000e320, then the root), zero root and count; the
// compiler then runs ~Array on the backing array at +0 (MemFree(*p); p[0] = 0;
// p[1] = 0), as retail does after the body.
// ------------------------------------------------------------------
MenuSceneTreeMap::~MenuSceneTreeMap() {
    MenuSceneTreeNode* node = root;  // *(self + 0x10)
    if (node) {
        WorkBase_TreeNodeFree(&root, node->left);   // 0x14000e320
        WorkBase_TreeNodeFree(&root, node->right);
        System::MemoryAllocator::MemFree(node);
    }
    root = nullptr;
    count = 0;
}

// ------------------------------------------------------------------
// FUN_140093eb0 (gameupdateloop.cpp, Weather_Dtor) — remove each geometry in the
// array at +0x50 (count +0x58) from the global scene and virtual-dtor it, then
// free the two arrays at +0x60 / +0x50. Scene = (*g_pGameWorldSingleton)+0x60.
// ------------------------------------------------------------------
void WeatherSimulation::Teardown() {
    int count = effects.GetSize();           // +0x58
    Graphics::Geometry** arr = effects._data;   // +0x50
    GameUpdateLoop* gw = GameUpdateLoop::_singleton;
    Graphics::Scene* scene = gw ? gw->m_graphicsScene : nullptr;
    if (arr && count > 0) {
        for (int i = 0; i < count; ++i) {
            Graphics::Geometry* geom = arr[i];
            if (geom) {
                if (scene)
                    scene->RemoveGlobalObject(geom);
                // Retail: (**(code **)*puVar2)(puVar2, 1) — object -> vtable ->
                // slot 0. Same one-deref-short bug as Game_ImageCaptureDtor above.
                delete geom;   // slot 0, free
            }
        }
    }
    // ENDGAME B.3.2 step 2: the loop above is Weather's own per-element work
    // (retail 0x140093eb0 runs it inline, before the array frees); the two
    // frees that follow are trivial-element `~Array`s at +0x60 then +0x50
    // (re-decompiled 2026-09-13: MemFree, data = 0, one qword over
    // size+capacity). +0x50 holds Geometry* — `Array<void*>`, trait-derived:
    // byteCopy, no init, no destroy, align 8. +0x60's element is not pinned.
    // No C++ dtor runs (Shutdown MemFrees the block): the members' ~Array are
    // called here, in retail's order (0x140093f24 then 0x140093f36).
    emitters.~Array();
    effects.~Array();
}

// ------------------------------------------------------------------
// FUN_14001fd10 (terraingenerator.cpp, TerrainGenerator_Slot5_sub_fd10) —
// cached resolve of a {String filename @+0, int generation @+8, External*
// resolved @+0x10} reference triple to a Graphics::MaterialInstance. Re-resolves
// only when the field's cached generation differs from the ExternalFactory's
// (factory+8). Validates via IsDerivedFrom against the MaterialInstance type
// (.rdata slot 0x140151350 = g_ppTypeInfo_MaterialInstance). Runs only on the
// legacy version-0 terrain deserialize path -> dormant on autostart.
//
// Disasm-verified offsets (14001fd10): cmp [RCX+8],factoryGen ; [RBX+0x10]=result
// ; [RBX+8]=factoryGen. Filename built via the String& ctor (slot 0x140151d68 =
// fn_Filename_CtorString = ??0Filename@System@@QEAA@AEBVString@1@@Z), Find via
// ::Object::ExternalFactory::Find. Cascade-safe: if the type slot is unresolved, load
// without IsDerivedFrom validation rather than dereferencing a null BaseTypeInfo.
void Game_TerrainResolveMaterial(MapGenerator::ExternalRef* ref) {
    if (!ref) return;
    if (!::Object::ExternalFactory::_singleton) return;
    ::Object::ExternalFactory* efactory = ::Object::ExternalFactory::_singleton;
    // The record is TerrainGenerator's material ref (+0x38 / +0x50 / +0x68), a
    // MapGenerator::ExternalRef {String, generation, External*}.
    int factoryGen = efactory->_generation;   // factory+8
    if (ref->generation == factoryGen) return;

    System::Filename fnameBuf(ref->name);  // String @ +0
    ::Object::External* obj = efactory->FindAs<::Object::External>(fnameBuf);

    ::Object::External* result = nullptr;
    if (obj) {
        // Retail: IsDerivedFrom against [0x140151350] (?_typeInfo@MaterialInstance@
        // Graphics@@); ENDGAME B.3.9 TI-REST: the class accessor.
        const uint32_t tid = Graphics::MaterialInstance::TypeId();
        if (::Object::IsDerivedFromNullSafe(obj, tid))
            result = obj;
    }
    ref->resolved = result;
    ref->generation = factoryGen;
}
