// src/game/Game/menudialogloopt_stubs.cpp
// Standalone-only stub vtable for MenuDialogLoopT (exe vtable @ 0x14015ed00).
//
// MenuDialogLoopT is the exe-internal subclass of Engine::DialogLoopT that drives
// the main menu (Dialog\MainMenu.rsc). Its 13-slot vtable lives at 0x14015ed00 in
// the original exe; in standalone mode we provide our own vtable here.
//
// Slot layout (13 entries — UpdateLoop's 12 + DialogLoopT's CreateDialog at +0x60):
//   +0x00  Destroy(self, flags)           — exe 0x14001A2E0 (scalar deleting dtor)
//   +0x08  Startup(self, remainder)       — exe 0x14009B930
//   +0x10  Shutdown(self)                 — exe 0x14009B280
//   +0x18  Update(self, time)             — exe 0x14009B6E0
//   +0x20  Draw(self, alpha)              — exe 0x14009B440
//   +0x28  DrawFailed(self)               — exe 0x14003A90C (thunk -> Runtime DialogLoopT::DrawFailed)
//   +0x30  Load(self)                     — exe 0x14009A930
//   +0x38  SaveRestore(self, save, state) — exe 0x14003A906 (thunk)
//   +0x40  OnVideoError(self)             — exe 0x14009A8D0
//   +0x48  OnVideoRestart(self)           — exe 0x14009A8B0
//   +0x50  GetResourceFilename(self)      — exe 0x14001A2D0  -> "Game/Menu"
//   +0x58  AllowsModChanges(self)         — exe 0x14001A2C0  -> true
//   +0x60  CreateDialog(self, retptr)     — exe 0x14009BA30
//
// ENDGAME B.3.8 block LOOP: the hand-built table is gone. This file now holds
// the MEMBER bodies of the real class `MenuDialogLoopT`
// (src/game/Game/MenuDialogLoopT.h), whose compiler vtable 0x14015ed00 serves
// (B38_RULES D5), plus the standalone resource-loading helpers they call.


#include "system/String.h"
#include "ui/DialogInstance.h"
#include "object/Internal.h"
#include "Game/game.h"   // B3R STORAGE: g_gameContainer
#include "renderdoc_integration.h"
#include "stall_debug.h"
#include "redzone_alloc.h"
#include "frame_limiter.h"
#include "modal_driver.h"
#include "mouse_inject.h"
#include "global_types.h"
#include "Object/factory_registration.h"
#include "system/Timer.h"   // ENDGAME B.3.9 SS2: System::Timer::_ticksPerSecond
#include "Game/menudialogloopt_funcs.h"
#include "Game/uifade.h"
#include "Game/MenuDialogLoopT.h"   // ENDGAME B.3.8 LOOP
#include "Game/WorldGeometry.h"     // _sceneGeometry's class (the device-lost pair)
#include "Object/exe_type.h"      // DEFINE_EXE_VTABLE
#include "autostart.h"
#include <cstdio>
#include <cstdint>
#include "system/Filename.h"   // -Iruntime/types
#include "system/Value.h"      // ENDGAME B.3.4b System callers: System::Time
#include "object/External.h"   // ENDGAME B.3.3 step 6A
#include "ui/Sprite.h"          // ENDGAME B.3.6 UI callers (K4)
#include "ui/UserInterface.h"   // ENDGAME B.3.6 UI callers (K4)
#include "engine/UpdateLoop.h"   // -Iruntime/types: ENDGAME B.3.7 Engine loop classes
#include "engine/MainLoop.h"     // -Iruntime/types: ENDGAME B.3.7 Engine::MainLoop
#include "graphics/Scene.h"     // ENDGAME B.3.7: MenuDLT::Shutdown (carry-over 11)
#include "save/SaveLoad.h"      // -Iruntime/types: ENDGAME B.3.7 callers (K5)
#include "audio/Audio.h"        // ENDGAME B.3.8 LOOP: ~MenuDialogLoopT runs ~Audio::Scene
#include "application/Application.h"   // Application::IsPaused (Update 0x14009b72f)
#include "graphics/Camera.h"    // Graphics::Renderer (~Renderer)
#include "system/MemoryAllocator.h"   // B3R: System::MemoryAllocator::MemAlloc / MemFree
#include "ui/Cursor.h"          // B4: GameContainer cursor slots
#include "ui/SpriteSheet.h"     // B4: GameContainer::spriteSheet
#include "audio/SoundEffect.h"  // B4: GameContainer UI sound slots
#include "graphics/SunLight.h"  // B4: _sunLight (Geometry)
#include "graphics/Mesh.h"      // B4: _mainMesh (Geometry)
#include <new>
#include <cstring>
#include <initializer_list>   // ENDGAME B.3.7: MenuDLT::Shutdown field list
#include "ext/registry.h"            // EXT SEAM (NOT RETAIL, docs/extensions/handins/SHARPUI.md): Ext::Ext().sharp_ui
#include "ext/sharp_ui/sharp_ui.h"   // EXT SEAM (NOT RETAIL, docs/extensions/handins/SHARPUI.md): ext.sharp_ui, Draw
#include "load_time.h"   // [LoadTime] stamps (NOT RETAIL, print-only; block LOADSTAMP)

// ============================================================================
// MenuDialogLoopT (vtable @ 0x14015ed00) — src/game/Game/MenuDialogLoopT.h
// ============================================================================
// ENDGAME B.3.8 block LOOP: the old hand-built `g_menuDialogLoopVtable[13]`'s
// stub bodies, moved into the real class's members unchanged.

// 0x14009bb00 — the body the inline `MenuDialogLoopT_Ctor` (engine_funcs.h,
// deleted at B.3.9 EXA) duplicated, with the vtable store the compiler's.
MenuDialogLoopT::MenuDialogLoopT(const System::String& filename)
    : Engine::DialogLoopT(filename) {
    // The member initialisers have run System::Random::Random() on +0x168
    // (??0Random@System@@ 0x1800c7f20, retail's CALL at 0x14009bb2e), then
    // Audio::Scene::Scene() on +0xb30 (??0Scene@Audio@@ 0x1800e0160, 0x14009bb3b):
    // retail's order, which is declaration order. They also zeroed the three
    // array headers +0xbd0..+0xbf8 (retail's last six qword stores).
    // Zero-init the plain fields +0xb68..+0xbcf: every field from _pendingLoop up
    // to the first array member, pad included (retail's qword stores; the
    // memset stops before _particleArray, which its own ctor zeroed).
    std::memset(&_pendingLoop, 0,
                offsetof(MenuDialogLoopT, _particleArray) - offsetof(MenuDialogLoopT, _pendingLoop));
}

// 0x14001a310 (read from the disassembly): MemFree the data of the three
// arrays +0xbf0, +0xbe0, +0xbd0 (in that order) and zero each {data, size|cap},
// then ??1Scene@Audio@@ on +0xb30 (import 0x140151fb0), then the tail JMP to
// ??1DialogLoopT@Engine@@ (import 0x140152378) — the compiler's base chain
// (~UserInterface, ~String, ~UpdateLoop). The elements the arrays point at were
// deleted by Shutdown (0x14009b280), as in retail.
MenuDialogLoopT::~MenuDialogLoopT() {
    // Retail 0x14001a2e0 (slot 0) calls this, then MemFree on &1 (UpdateLoop's
    // operator delete); the log line the old slot-0 body carried.
    printf("[MenuDLT::Destroy] self=%p\n", (void*)this);
    // After this body the compiler frees _sceneIdArray (+0xbf0), _instanceArray
    // (+0xbe0), _particleArray (+0xbd0) and runs ~Audio::Scene on +0xb30, reverse
    // declaration order, as retail's 0x14001a310 does.
    //
    // No `_loadBit` store, as retail 0x14001a310 has none: ~UpdateLoop hands the
    // menu's bit back, publishes it as the factory's _cleanBitIndex and runs
    // ExternalFactory::Clean for it (known_port_issues rows 1295, 1333). That index is
    // still published when the next loop's Loader::Load remounts the mods, so what the
    // remount's in-place reloads newly load carries the freed menu bit, which the next
    // MenuDialogLoopT takes again (the lowest free bit). The store this replaced kept
    // bit 0 for the process: the stale index was then an earlier loop's bit that the
    // GameUpdateLoop had taken, and quitting to the menu freed those loads under a
    // resident model (row 1333). The splash dialogs game_main.cpp preloads under bit 0
    // are unused by then: both splash loops are deleted before the menu is.
}

void MenuDialogLoopT::Startup() {
    StallDebug::Phase startupPhase("MenuDLT::Stub_Startup");

    // Menu-side Save::SaveLoad (+0xb70) — retail MenuDLT keeps its own SaveLoad
    // and ticks it every frame (MenuDLT::Update 0x14009b71f calls the
    // ?Update@SaveLoad dispatch slot on *(self+0xb70)); the menu Load button's
    // list-scan/load operations can only progress through that tick. The ctor
    // also publishes Save::SaveLoad::_singleton, which SaveLoadDialog and the
    // autosave path read. (Save/load wave 2026-08-27.)
    {
        if (_saveLoad == nullptr) {
            Save::SaveLoad* mem = static_cast<Save::SaveLoad*>(System::MemoryAllocator::MemAlloc(0x90, 4));
            if (mem) {
                RtSave_SaveLoad_Ctor(mem);   // ??0SaveLoad@Save@@ 0x1800c3d30
                _saveLoad = mem;
                printf("[MenuDLT::Startup] menu SaveLoad created at +0xb70 = %p\n", (void*)mem);
            }
        }
    }

    const void* dialogExt = _dialogExternal;
    printf("[MenuDLT::Startup] self=%p m_dialogExternal=%p\n",
           (void*)this, dialogExt);
    // Original MenuDLT::Startup (exe 0x14009B930) chains Engine::DialogLoopT::Startup
    // (Runtime 0x1800db2f0) which:
    //   1. Calls slot 12 CreateLoopDialog → MainMenuDialog_FromMenuDLT.
    //   2. Stores the InternalPtr at self+0x38 (_dialogInstance).
    //   3. Invokes the dialog's Slot4 (OnOpened/Setup).
    //   4. Pushes the IBP into the UI's pending-open dialog list.
    // M2 wiring (SplashDLT → Loader::Load → MenuDLT::Load → DialogLoopT::Load)
    // populates _dialogExternal before this fires, so DialogInstance::DialogInstance
    // and the UI list iteration both have valid data to read.
    if (!dialogExt) {
        printf("[MenuDLT::Startup] WARN: m_dialogExternal is NULL, skipping Startup "
               "(would crash in DialogInstance ctor)\n");
        return;
    }

    // Phase A: by the time MenuDLT activates, the Loading splash's
    // ChunkedSceneLoader has already run LoadSceneA1 + chunked PhaseB +
    // LoadSceneA3 (driven from GameLoadDialogLoopT::Startup / ::Update
    // across the splash's display period). MenuDLT::Startup
    // just confirms the work was done and falls back to synchronous
    // LoadScene_PhaseA recovery if some boot path skipped the splash chain
    // (e.g. direct MenuDLT entry from a debug harness).
    void* menuSceneExt = _menuScene;
    if (!menuSceneExt) {
        printf("[MenuDLT::Startup] WARN: scene fields empty (Loading splash "
               "didn't run ChunkedSceneLoader) — falling back to synchronous "
               "LoadScene_PhaseA\n");
        StallDebug::Phase phaseA("MenuDLT LoadScene_PhaseA fallback");
        bool sceneOk = LoadScene_PhaseA();
        printf("[MenuDLT::Startup] fallback LoadScene_PhaseA returned %s\n",
               sceneOk ? "true" : "false");
    } else {
        printf("[MenuDLT::Startup] scene already populated by Loading "
               "splash ChunkedSceneLoader (menuScene=%p) — skipping "
               "LoadScene_PhaseA\n", menuSceneExt);
    }

    {
        StallDebug::Phase dialogPhase("MenuDLT DialogLoopT::Startup");
        // QUALIFIED (slot 1 is VIRTUAL on the base; this is the class's own Startup).
        Engine::DialogLoopT::Startup();
    }
    printf("[MenuDLT::Startup] DialogLoopT::Startup returned, m_dialogInstance=%p\n",
           _dialogInstance);

    // The container's cursors, sprite sheet and UI sounds were filled at boot by
    // GameContainer_Init 0x140099db0 (InnerLoaderLoop::Load), as in retail; the tree's
    // on-demand copies (EnsureCursorsLoaded / StandaloneEnsureSpriteSheetLoaded /
    // EnsureUISoundsLoaded) are gone (D.4 RESIDUE).

    // 0x14009b96b: CALL 0x140099260 = GameContainer::ApplyKeyMapToUI(container, &ui) -- the five UI
    // sound slots, the fullscreen fade sprite (+0xa8) and the UI scale (+0xdc), RCX = [0x1401c3de8]
    // untested. (Until 2026-10-10 a partial second port, BindUISounds, copied the five sounds only,
    // behind a g_gameContainer test retail lacks -- B8 audit F1, known_port_issues row 1206.)
    g_gameContainer->ApplyKeyMapToUI(&_ui);

    UI::UserInterface* ui = &_ui;
    GameContainer* keyMap = (g_gameContainer);
    UI::Cursor* arrowCursor = keyMap
        ? keyMap->cursorArrow.Get()
        : nullptr;
    if (arrowCursor) {
        _ui.SetCursor(Object::ExternalPtr<UI::Cursor>(arrowCursor));
        printf("[MenuDLT::Startup] UI::SetCursor(ui=%p, arrow=%p)\n", ui, (void*)arrowCursor);
    } else {
        printf("[MenuDLT::Startup] WARN: skipping SetCursor (arrow=%p)\n", (void*)arrowCursor);
    }
    _ui.EnableCursor(true);
    printf("[MenuDLT::Startup] UI::EnableCursor(true) called on ui=%p\n", ui);

    // Trigger Startup-reveal fade — instant black -> animated reveal over ~0.25s.
    // Without this, the splash->menu transition hard-cuts. The original calls
    // SetFullscreenFade twice from OptionsDialog_impl_8fa0 in the same shape.
    // Chunked PhaseB completed during the Loading splash, so the scene is
    // fully populated by now — we can fade in immediately.
    UIFade::EnsureFadeSprite(ui);
    UIFade::TriggerReveal(ui);

    // Start the menu music. Mirrors the Audio::Scene::PlayEffect block near the
    // end of the original MenuDLT::Startup (exe 0x14009b930): registers the
    // loop's Audio::Scene (self+0xb30) at the DialogInstance base (self+0xd0) and
    // plays Audio/Music/Menu.rsc (loaded into self+0xb88 by Load) on the
    // music channel. Per-frame Audio::Scene::Update (Update) advances it.
    StartMusic();
}

void MenuDialogLoopT::Shutdown() {
    printf("[MenuDLT::Shutdown] self=%p\n", (void*)this);
    // ENDGAME B.3.7 CARRY-OVER 11: retail MenuDialogLoopT::Shutdown
    // (Application-x64.exe 0x14009b280), transcribed from its DISASSEMBLY, whole.
    // This stub used to run only step 11 (the StopAll) and skip the rest -- in
    // particular the call to `DialogLoopT::Shutdown`, which retail makes
    // DIRECTLY through import 0x1401523a0 (`?Shutdown@DialogLoopT@Engine@@`),
    // and which closes the menu dialog. Now that the base member is callable
    // it is called QUALIFIED (slot 2 is VIRTUAL on the base; this IS the
    // class's slot 2). Import names read from the exe's IAT with pefile:
    //   0x140151eb8 ?RemoveGlobalObject@Scene@Graphics@@   0x140151360 ?RemoveObject@Scene@Graphics@@
    //   0x140152b68 ?MemFree@MemoryAllocator@System@@       0x140151fa8 ??1Scene@Graphics@@
    //   0x1401521f0 ??1Renderer@Graphics@@                 0x140151f88 ??1SaveLoad@Save@@
    //   0x1401523a0 ?Shutdown@DialogLoopT@Engine@@          0x140151f98 ?StopAll@Scene@Audio@@
    //   0x140151bf8 ?EnableCursor@UserInterface@UI@@ (tail JMP)
    // Retail re-reads every field from `this` at each use and NEVER nulls one
    // after freeing it; both are reproduced. Unguarded where retail is.
    // Caller batch CB6: the fields are the class's named members.
    auto scene = [this]() {                          // MOV RCX,[RBX+0xba0], re-read
        return static_cast<Graphics::Scene*>(_scene);
    };
    // Every object deleted here (SunLight, the MeshInstances, the DLC ship, the
    // scene-grid instances) is a Graphics::Geometry, which is its OWN hierarchy
    // root -- not an Object::Base. Slot 0 through the object's own root.
    auto deleteGeometry = [](Graphics::Geometry* o) {  // MOV RAX,[RCX]; MOV EDX,1; CALL [RAX]
        delete o;
    };
    // 1. +0xbc0: the menu scene's first Geometry -- REMOVED, not owned (no delete).
    if (_sceneGeometry)
        scene()->RemoveGlobalObject(static_cast<Graphics::Geometry*>(_sceneGeometry));
    // 2-4. +0xbb0 (SunLight), +0xba8 (main mesh), +0xbc8 (DLC ship): removed
    //      from the scene's global list, then deleted through slot 0.
    if (_sunLight) {
        scene()->RemoveGlobalObject(_sunLight);
        if (Graphics::Geometry* o = _sunLight) deleteGeometry(o);
    }
    if (_mainMesh) {
        scene()->RemoveGlobalObject(_mainMesh);
        if (Graphics::Geometry* o = _mainMesh) deleteGeometry(o);
    }
    // +0xbc8: retail builds it with MemAlloc(0xf0) + the exe Geometry subclass ctor
    // 0x1400d3f10 (0x14009ac9a: the import Geometry ctor, then its own vtable) and adds it
    // as a scene global.
    if (_dlcShip) {
        Graphics::Geometry* ship = _dlcShip;
        scene()->RemoveGlobalObject(ship);
        if (ship) deleteGeometry(ship);
    }
    // 5. every scene-grid instance: RemoveObject(id[i]) then delete object[i].
    //    Count from +0xbe8, ids from +0xbf0 (uint32), objects from +0xbe0: the
    //    Arrays _instanceArray (+0xbe0) and _sceneIdArray (+0xbf0).
    for (int32_t i = 0; i < _instanceArray.GetSize(); ++i) {
        const uint32_t id = _sceneIdArray[i];
        scene()->RemoveObject(id);
        if (Graphics::Geometry* o = _instanceArray[i]) deleteGeometry(o);
    }
    // 6. +0xbb8 (the Transform block): freed.
    if (void* t = _transform) System::MemoryAllocator::MemFree(t);
    // 7. +0xba0 Graphics::Scene: ~Scene (??1Scene@Graphics@@ 0x1801026c0) then freed.
    if (Graphics::Scene* g = _scene) {
        g->~Scene();
        System::MemoryAllocator::MemFree(g);
    }
    // 8. +0xb98 Graphics::Renderer: ~Renderer (??1Renderer@Graphics@@ 0x1800180a0) then freed.
    if (Graphics::Renderer* r = _renderer) {
        r->~Renderer();
        System::MemoryAllocator::MemFree(r);
    }
    // 9. +0xb70 the menu's Save::SaveLoad: ~SaveLoad (??1SaveLoad@Save@@ 0x1800c3c90) then freed.
    if (Save::SaveLoad* sl = _saveLoad) {
        RtSave_SaveLoad_Dtor(sl);
        System::MemoryAllocator::MemFree(sl);
    }
    // 10. `CALL [0x1401523a0]` -- Engine::DialogLoopT::Shutdown, QUALIFIED.
    Engine::DialogLoopT::Shutdown();
    // 11. Audio::Scene::StopAll(this+0xb30). NOT skippable: it parks the
    //     streamed menu music's thread before the next LoaderLoop's
    //     Loader::Load remounts the packages under it (open defect (c)).
    StopMusic();
    // 12. The tail `JMP UI::UserInterface::EnableCursor(this+0x40, false)`.
    _ui.EnableCursor(false);
}

bool MenuDialogLoopT::Update(const System::Time& timeRef) {
    StallDebug::Tick("MenuDLT::Update");
    // NOT RETAIL (block LOADSTAMP): one [LoadTime] line on the first Update of each menu loop.
    {
        static const MenuDialogLoopT* s_stamped = nullptr;
        if (s_stamped != this) { s_stamped = this; LoadTime::Stamp("menu.update.first"); }
    }
    // Retail's slot takes MainLoop's Time by const& and never writes it: the first
    // Update after the synchronous Startup gets the whole Startup time as its dt, as
    // in retail (whose Startup is synchronous too). B6 ruling: no first-tick clamp.
    const System::Time* time = &timeRef;
    // Per-frame heap-integrity check (no-op unless BANISHED_REDZONE set). Catches
    // free-list poisoning that surfaces during the steady-state menu loop, where
    // our alloc/free are rare so the alloc/free auto-scan rarely fires.
    RedzoneAlloc_HeapCheck("menu Update (per-frame)");
    // Retail MenuDialogLoopT::Update 0x14009b6e0 (read 2026-10-11, decompile +
    // disassembly), in its order:
    //   SetCursor(container->cursorArrow) every frame (0x14009b70a, RDX = [[0x1401c3de8]+0x28]);
    //   if (+0xb70) SaveLoad::Update(time);
    //   paused = Application::IsPaused(); if (paused != _audioScene.paused[2] (+0xb62))
    //     _audioScene.Pause(2, paused); if (!paused) _audioScene.Update(...);
    //   if (_scene (+0xba0)) { the camera; if (+0xbc8) [+0xbc8]->slot1(time);
    //     the +0xbd0 Update loop; ++ParticleInstance frame index; Scene::Clean };
    //   the pending-loop poll; tail DialogLoopT::Update.
    static int tick = 0;
    ++tick;
    if (tick <= 4 || (tick % 300) == 0) {
        printf("[MenuDLT::Update] self=%p time=%p tick=%d -> UpdateCamera + DialogLoopT::Update\n",
               (void*)this, (void*)time, tick);
    }

    _ui.SetCursor(g_gameContainer->cursorArrow);   // 0x14009b6f0-0x14009b70a, container untested

    // 0x14009b710-0x14009b71f: the menu-side Save::SaveLoad (+0xb70, created in Startup).
    {
        Save::SaveLoad* menuSaveLoad = _saveLoad;
        if (menuSaveLoad)
            menuSaveLoad->Update(*time);
    }

    // Harness hooks (env-gated, no-ops on a plain run): the autostart click driver and the
    // modal driver's arming. Not retail; they sit where they always sat.
    AutoStart::TickMenu(this);
    if (ModalDriver::Enabled())
        ModalDriver::Poll();

    // 0x14009b725-0x14009b7a0: the pause handshake, then the audio scene tick.
    const bool paused = ::Application::Application::_singleton->IsPaused();
    if (paused != _audioScene.paused[2])
        _audioScene.Pause(2, paused);
    if (!paused)
        TickAudioScene(time);

    // STANDALONE DIFFERENCE (row 1242, kept): a per-frame Audio::Device::Update. Retail's
    // Update has none; the tree's SDL voice path raises a finished channel's bit 2 and only
    // Device::Update frees it, so the menu's one-shot UI sounds would exhaust the 64
    // channels without it. Whether retail reaps elsewhere is the audio owner's question
    // (SLOTS2B hand-in).
    MenuDialogLoopT_TickAudio(time);

    // 0x14009b7a6-0x14009b8ca: everything scene-side runs under one `_scene` test.
    if (_scene) {
        UpdateCamera_PhaseA(time->AsSeconds());
        if (_dlcShip)
            _dlcShip->Update(*time);   // [+0xbc8] slot 1 (0x14009b878)
        TickParticles(time);           // the +0xbd0 loop, the frame index, Scene::Clean
    }

    // Stage C entry: poll the pending-next-loop written by NewGameDialog::
    // StartGame's SetDialogLoaderNextLoop (MenuDLT+0xb68). Once the fullscreen
    // fade-to-black settles (UI fade RATE at self+0x130 == UI(+0x40)+0xF0 == 0.0),
    // advance the MainLoop to the queued loader chain (outer LoaderLoop ->
    // GameLoadDialogLoopT -> Engine::Loader thread -> GameUpdateLoop::Load).
    // Mirrors retail
    // MenuDialogLoopT::Update (exe 0x14009b6e0) and our own GameUpdateLoop::Update
    // transition (gameupdateloop.cpp:886). Without this the menu loop never
    // advances and we never reach the in-game world build. Done BEFORE the
    // DialogLoopT::Update tail-call, matching retail ordering (reads the fade
    // state the prior tick's UI::Update settled).
    {
        Engine::UpdateLoop* pendingLoop = _pendingLoop;
        float fadeRate    = _ui._fadeVelocity;   // +0x130 = UI (+0x40) + 0xF0
        if (pendingLoop && fadeRate == 0.0f) {
            Engine::MainLoop* mainLoop = ::Engine::MainLoop::_singleton;
            LoadTime::Stamp("menu.stageC");   // NOT RETAIL: print-only (the fade settled; the loader chain is queued)
            fprintf(stderr, "[MenuDLT::Update] STAGE C entry: pendingLoop=%p "
                    "fade settled (rate=0) -> MainLoop::SetNextLoop\n", (void*)pendingLoop);
            fflush(stderr);
            if (mainLoop) {
                mainLoop->SetNextLoop(pendingLoop);
            } else {
                fprintf(stderr, "[MenuDLT::Update] WARN: cannot advance "
                        "(mainLoop=%p)\n", (void*)mainLoop);
                fflush(stderr);
            }
        }
    }

    // Wave 10D lane MOUSE: the menu half of the cursor route. The menu UI is
    // MenuDLT+0x38 (DialogInstance) -> +0x10 (UserInterface), the same walk
    // ModalDriver::RunSelfTest uses above. No-op unless BANISHED_MOUSE_INJECT.
    UI::UserInterface* menuUI = nullptr;
    if (MouseInject::Enabled()) {
        UI::DialogInstance* menuDlg = _dialogInstance;
        if (menuDlg)
            menuUI = menuDlg->_ui;
        MouseInject::PreUpdate(menuUI);
    }
    // QUALIFIED (slot 3 is VIRTUAL on the base; this is the class's own Update).
    bool result = Engine::DialogLoopT::Update(*time);
    if (menuUI) MouseInject::PostUpdate(menuUI);
    return result;
}

void MenuDialogLoopT::Draw(float alpha) {
    StallDebug::Tick("MenuDLT::Draw");
    // Frame-rate ceiling. The Runtime main loop (MainLoop::Frame) is uncapped —
    // it only SwitchToThread()s, so the menu otherwise spins one core to 100%.
    // Tick() at frame start paces us to (display refresh + headroom) FPS; it's a
    // no-op whenever an external limiter (vsync/G-Sync/RTSS) already holds the
    // rate at or below refresh. This is the per-frame chokepoint we own, since
    // the outer Frame loop lives in the Runtime DLL.
    FrameLimiter::Tick();
    // Mirror original MenuDLT::Draw (exe 0x14009B440):
    //   ctx = engine_singleton->m_context   (engine+0x10)
    //   if (self+0xb90 != null) {  // MenuScene loaded
    //       <camera setup, sun light, fog, Renderer::Draw>
    //   }
    //   UI::UserInterface::Draw(self+0x40, ctx)
    //
    // self+0x40 is the embedded UI::UserInterface (DialogLoopT layout).
    static int draws = 0;
    ++draws;
    bool trace = (draws <= 3) || (draws % 200) == 0;

    // Use MainLoop singleton's m_context — populated by MainLoop ctor and
    // unchanged for the run.
    ::Engine::MainLoop* mainLoop = ::Engine::MainLoop::_singleton;
    Video::Context* context = mainLoop
        ? mainLoop->_context
        : nullptr;
    UI::UserInterface* ui = &_ui;

    if (trace) {
        void* menuScene = _menuScene;
        printf("[MenuDLT::Draw] self=%p frameAlpha=%f mainLoop=%p ctx=%p ui=%p menuScene=%p\n",
               (void*)this, alpha, (void*)mainLoop, (void*)context, ui, menuScene);
        printf("[MenuDLT::Draw] fps=%.1f (cap=%.1f)\n",
               FrameLimiter::GetFps(), FrameLimiter::GetCapFps());
    }

    // Per-frame fade alpha trace — log first 5 + every 30th draw so the curve
    // is visible in standalone_debug.log without spamming. UI is at self+0x40
    // for MenuDLT (DialogLoopT subclass).
    {
        static int draws = 0;
        ++draws;
        if (draws <= 5 || (draws % 30) == 0) {
            UIFade::LogState("MenuDLT::Draw", ui);
        }
    }

    // Phase A: render the 3D menu scene before the UI overlay.
    if (context) {
        DrawScene_PhaseA(alpha, context);
    }

    if (context) {
        // EXT SEAM (NOT RETAIL, docs/extensions/handins/SHARPUI.md): ext.sharp_ui, as GameUpdateLoop::Draw step 12.
        if (Ext::Ext().sharp_ui)
            Ext::SharpUI::Draw(_ui, *context);
        else
            _ui.Draw(*context);
    } else if (trace) {
        printf("[MenuDLT::Draw] WARN: skipping UI::Draw (ctx=%p)\n", (void*)context);
    }

    // Auto-trigger a RenderDoc capture on draw 10 if RenderDoc is attached.
    // By this point the camera is stable, terrain is uploaded, and the menu
    // UI is drawn. Capturing one such frame is enough to inspect the GPU
    // state (which RTV is bound, what gets drawn, depth/cull, etc.).
    if (RenderDocIntegration::IsAttached()) {
        if (draws == 10) RenderDocIntegration::TriggerCapture();
        RenderDocIntegration::OnFrame();
    }
}

void MenuDialogLoopT::Load() {
    // Original MenuDLT::Load (exe 0x14009A930) tail-calls Engine::DialogLoopT::Load
    // first to populate self+0x30 (_dialogExternal) from _dialogFilename
    // ("Dialog\MainMenu.rsc"), then loads companion resources (Audio/Music/Menu.rsc,
    // Game/MenuScene/SceneN.rsc) for the 3D menu background.
    //
    // This runs on the single Runtime Engine::Loader thread (kicked by
    // the Loading splash's Startup, as in retail). Only thread-safe work
    // goes here:
    // DialogLoopT::Load is safe (it's a single-call resource load with internal
    // refcounting). MenuDialogLoopT_LoadScene_PhaseA is NOT — it iterates 15K+
    // calls to Graphics_Scene_AddGlobalObject which mutates global scene state in
    // a way that produced corrupted vtable reads on first Update tick when run on
    // a worker thread. The PhaseA scene allocation has been moved to Startup
    // (main thread, runs while screen is fully faded to black so the freeze is
    // invisible to the user — see Startup above).
    const void* dialogExtBefore = _dialogExternal;
    printf("[MenuDLT::Load] self=%p m_dialogExternal(before)=%p\n", (void*)this, dialogExtBefore);
    // QUALIFIED (slot 6 is VIRTUAL on the base; this is the class's own Load,
    // dispatched by Loader::Run on the loader thread).
    Engine::DialogLoopT::Load();
    const void* dialogExtAfter = _dialogExternal;
    printf("[MenuDLT::Load] m_dialogExternal(after)=%p (PhaseA deferred to Startup)\n",
           dialogExtAfter);

    // Load the menu music (Audio/Music/Menu.rsc -> self+0xb88). Mirrors the
    // music-load block at the top of the original MenuDLT::Load (exe 0x14009a930).
    // Like DialogLoopT::Load this is a plain refcounted ExternalFactory load,
    // safe to run on the Engine::Loader thread (no scene-graph mutation).
    // Playback is started later from Startup (main thread).
    LoadMusic();
}

// [8] 0x14009a8d0: MOV RCX,[RCX+0xbc0]; TEST RCX,RCX; JNZ 0x1400b0be0; RET --
// the menu terrain's device-lost release, the same WorldGeometry member
// GameUpdateLoop::OnVideoError calls. The null test is retail's. The trace
// line is the tree's (DEVLOST_FINDING greps for it).
void MenuDialogLoopT::OnVideoError() {
    printf("[MenuDLT::OnVideoError] self=%p\n", (void*)this);
    if (_sceneGeometry)
        _sceneGeometry->OnVideoError();      // 0x1400b0be0
}

// [9] 0x14009a8b0: the same shape, JNZ 0x1400b0c50 -- the menu terrain rebuilt.
void MenuDialogLoopT::OnVideoRestart() {
    printf("[MenuDLT::OnVideoRestart] self=%p\n", (void*)this);
    if (_sceneGeometry)
        _sceneGeometry->OnVideoRestart();    // 0x1400b0c50
}

const char* MenuDialogLoopT::GetResourceFilename() {
    // Original exe slot 10 returns the literal "Game/Menu" via LEA RAX,[rip+...].
    return "Game/Menu";
}

bool MenuDialogLoopT::AllowsModChanges() {
    // Original exe slot 11 = MOV AL,1; RET — i.e. always true.
    return true;
}

extern "C" void* MainMenuDialog_FromMenuDLT(MenuDialogLoopT* loop, void* retInternalBasePtr);

Object::InternalPtr<UI::DialogInstance>* MenuDialogLoopT::CreateLoopDialog(
        Object::InternalPtr<UI::DialogInstance>* retPtr) {
    printf("[MenuDLT::CreateDialog] self=%p retPtr=%p rsc=%p -> MainMenuDialog_FromMenuDLT\n",
           (void*)this, (void*)retPtr, _dialogExternal);
    // M2 wired Loader::Load → MenuDLT::Load → DialogLoopT::Load, so self+0x30
    // (_dialogExternal) is populated with Dialog/MainMenu.rsc by the time
    // DialogLoopT::Startup invokes this slot. Delegate to the standalone
    // MainMenuDialog factory which mirrors the exe ctor at 0x14009BA30:
    // builds the DialogInstance + patches our 20-slot vtable + writes the
    // resulting IBP into retPtr.
    // The body (mainmenudialog.cpp, B4 FORWARD) returns its second argument on
    // every path; the slot returns that same pointer.
    MainMenuDialog_FromMenuDLT(this, retPtr);
    return retPtr;
}

// B38_RULES D5: `Vtable()` is the COMPILER's table. ENDGAME B.3.9 EXA: the
// 0x14015ed00 registration went with the inline door-then-stamp builder.
// (the DEFINE_EXE_VTABLE alias itself is in src/game/Object/exe_vtables.cpp, the one TU built without LTO)
