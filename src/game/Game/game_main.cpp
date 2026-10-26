// src/game/Game/game_main.cpp
// Clean reimpl of the original exe's GameMain (0x140041ef0).
//
// Original sequence (decompiled from Application-x64.exe):
//   1. Engine::Engine ctor on stack (1712 bytes)
//   2. Engine::Engine::Startup(cmdLine, 0x200, 0, false) -> bool
//   3. Allocate MainLoop (0xa0), Engine::MainLoop::MainLoop(ml, engine)
//   4. Allocate GameWorld (0xe0), FUN_140001050 ctor
//   5. Allocate game settings (0x34), Game_Register
//   6. FUN_140042320 -> MenuScene factory
//   7. DialogLoaderLoopT for "Dialog\Loading.rsc" (next=MenuScene)
//   8. DialogLoaderLoopT for "Dialog\ShiningRock.rsc" (next=Loading)
//   9. LoaderLoop wrapping Splash
//  10. LoaderLoop wrapping above (the game's loader)
//  11. Engine::MainLoop::Run(ml, gameLoader)
//  12. Cleanup: ExternalFactory::SetLoadBit/-1 + Clean, free settings/world,
//      MainLoop::~MainLoop + free, Engine::Engine::Shutdown, Engine::~Engine
//
// Initial stub only invokes Engine ctor/dtor so we can probe the standalone
// startup path one slot at a time. Stages will be enabled as dispatch slots
// fill in.

#include "game_main.h"
#include "platform_gfx.h"   // Platform::ReleaseLibrary (FreeLibrary; Phase D.4 LINUX-1 / SDL-5)
#include "system/Value.h"
#include "Game/game.h"   // B3R STORAGE: g_gameContainer
#include "system/Filename.h"
#include "system/String.h"
#include "io/Modification.h"   // ENDGAME B.3.4 caller batch P
#include "io/Persistence.h"    // ENDGAME B.3.4 caller batch P
#include "io/FileSystem.h"     // ENDGAME B.3.4a closing: FileSystem::ClosePackages
#include "achievement/AchievementManager.h"   // ENDGAME B.3.7 callers (K2)
#include "application/Application.h"          // ENDGAME B.3.7 callers (K2)
#include "engine/Engine.h"                    // ENDGAME B.3.7 callers (K2)
#include "engine/MainLoop.h"                  // ENDGAME B.3.7 callers (K2)
#include "engine/UpdateLoop.h"                // ENDGAME B.3.7 callers (K2)
#include "audio/Audio.h"                      // ENDGAME B.3.7 callers (K4)
#include "input/Input.h"                      // ENDGAME B.3.7 callers (K3)
#include "system/Error.h"                  // ENDGAME B.3.9 SS2: System::Error::_errorString
#include "system/MemoryAllocator.h"        // B3R W0-SM M1: System::MemoryAllocator::_singleton
// ENDGAME B.3.5 Video/Graphics caller batch: Video::Device and
// Video::Configuration are real classes.
#include "video/Device.h"
#include "platform_window.h"   // Phase D.4 SDL W1: the shutdown window log
#include "Game/game.h"
#include "Game/exe_bodies.h"   // B3R STORAGE: g_gameSettings
#include "Game/InnerLoaderLoop.h"   // ENDGAME B.3.8 LOOP
#include "Game/GameLoaderLoop.h"    // ENDGAME B.3.8 LOOP
#include "Game/GameLoadDialogLoopT.h"   // ENDGAME B.3.8 LOOP
#include "Game/MenuDialogLoopT.h"       // ENDGAME B.3.8 LOOP
#include "stall_debug.h"
#include "boot_state_watchdog.h"
#include "Object/factory_registration.h"   // Object::VerifyExeTypeInfos (ENDGAME B.3.9 RF)
#include "object/TypeInfo.h"               // Object::BaseTypeInfo::_firstTypeInfo (the dump)
#include <cstdio>
#include <string>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <new>

// The exe cell 0x1401c3de0 (B3R STORAGE): GameMain's settings block
// (Game/exe_bodies.h).
GameSettings* g_gameSettings = nullptr;


// Defined in src/game/Game/filesystem_setup.cpp. Relativizes the engine
// FileSystem's resource directories so resource paths resolve relative to the
// working dir (install dir) instead of long absolute "C:\GOG Games\..." strings.
// Must run AFTER Engine::Startup (FileSystem allocated + SetupDirectories done)
// and BEFORE the first resource load. See that file's header for rationale.

// Defined in src/app/main.cpp. Walks _firstTypeInfo for each registered
// type, invokes its factory, validates vtable + ctor-set field defaults.
extern "C" void RunStartupSelfTests();

// (ENDGAME B.3.9 TR: the post-Startup Fill* passes over the exe type-id storage cells
// that were declared here are gone -- every reader reads X::TypeId().)

// src/diag/win/boot_state_watchdog.cpp -- "MainLoop::Run returned, teardown has
// begun". Teardown renders no frames, so the hang detector budgets it from
// this call rather than from the last frame.
extern "C" void BootState_MarkQuitting();

// Standalone-only chunked driver for MenuDLT::LoadScene. Init runs
// MenuDialogLoopT_LoadSceneA1 (~10ms — small allocs + one .rsc load) inline
// and captures the chunked PhaseB iteration state. We invoke this during
// boot so the heavy iteration can be ticked across the Loading splash's
// Update frames (driven from splash_loop_stubs.cpp Stub_Update). Boot is
// non-rendering, so the synchronous A1 cost is invisible.
#include "chunked_scene_loader.h"
#include "ext/registry.h"   // EXT SEAM (NOT RETAIL, docs/extensions/handins/EXTSCAFFOLD.md): Ext::InitFromSettings
#include "ext/load_progress/load_progress.h"   // EXT SEAM (NOT RETAIL, docs/extensions/handins/LOADPROGRESS.md): BootBegin
#include "load_time.h"   // [LoadTime] stamps (NOT RETAIL, print-only; block LOADSTAMP)

// ============================================================
// Resolved Runtime DLL functions
// ============================================================
//
// Ghidra-confirmed addresses from Runtime-x64.dll:
//   0x1800dbf00 = Engine::Engine::Engine(this, void*)        ctor (174 B)
//   0x1800dbec0 = Engine::Engine::~Engine(this)              dtor
//   0x1800dd6b0 = Engine::MainLoop::MainLoop(this, Engine*)  ctor
//   0x1800dd640 = Engine::MainLoop::~MainLoop(this)          dtor
//   0x1800dd3c0 = Engine::MainLoop::Run(this, UpdateLoop*)
//
// ENDGAME B.3.7: Engine::Startup and MainLoop::Run are called as members. B3R:
// the ctor/dtor doors (B37_RULES 2.4) are called by name -- (the Engine's are its
// real ctor / dtor since B7), RtEngine_MainLoop_Ctor / _Dtor (engine/MainLoop.h); their
// caches, EnsureResolved, the "[GameMain] Engine ctor/dtor = %p" and "MainLoop
// ctor/dtor = %p" lines and the "not resolved" FATAL arm went with them.

// ============================================================
// Partial reimpl of Engine::Engine::Shutdown (Runtime 0x1800db4f0).
//
// Mirrors steps 1–13 of the stock Shutdown body — write back configuration,
// platform-shutdown each subsystem, FreeLibrary the renderer plugins, save
// settings (settings.cfg; retail: the registry), close .pkg files. We deliberately stop short of
// the dtor+free chain (Achievement/Input/Audio/Video/Application dtors,
// MemoryAllocator::Shutdown, CoUninitialize) because those crash silently
// in standalone — most likely tripping over leaked dialog/UI objects that
// MenuDLT's destroy stub doesn't release.
//
// The OS reclaims memory and uninitialises COM on process exit, so skipping
// the dtor chain only leaks until ExitProcess. Once standalone has a real
// MenuDLT::Destroy that walks DialogInstance/UserInterface/Audio::Scene,
// the leaks should shrink to nothing and we can call the stock function.
// ============================================================
namespace {

static void ShutdownEngineStaged(::Engine::Engine* engine) {

    printf("[Shutdown] partial Engine::Shutdown (steps 1-13)\n");
    fflush(stdout);

    // Video::Configuration::Save (step 8) persists whatever
    // Video::Device::AdjustDisplay last read out of GetClientRect. If anything
    // reshaped the window during the run, that is a degenerate client rect
    // (120x0 here) rather than the real size. Log the window state so a
    // poisoned save is attributable. Root cause of the 120x0 was an external
    // window manager (PowerToys FancyZones), not a minimize — see the guard in
    // src/render/dx11/devicedata.cpp, which now prevents the latch at the source.
    Video::Device* videoDev = engine->GetVideoDevice();   // +0x6c0
    // Device::windowHandle is the window held as void*; Platform::Window takes it
    // as is (Phase D.4 SDL W1: the three Win32 calls below moved behind it).
    void* gameWnd = videoDev
        ? videoDev->windowHandle   // +0x90
        : nullptr;
    auto logWindow = [&](const char* when) {
        if (!gameWnd) return;
        Platform::Rect cr{};
        Platform::Window::GetClientRect(gameWnd, &cr);
        // Called outside the printf, as GetWindowPlacement was: the log-stripped
        // builds compile the printf's arguments away, not this call.
        [[maybe_unused]] const uint32_t showCmd = Platform::Window::GetShowCmd(gameWnd);
        printf("[Shutdown] window %s: iconic=%d showCmd=%u client=%dx%d\n",
               when, Platform::Window::IsIconic(gameWnd) ? 1 : 0, showCmd,
               cr.right - cr.left, cr.bottom - cr.top);
        fflush(stdout);
    };
    logWindow("on entry");

    // 1. This is Engine::PlatformConfigure(false, ...) inlined: VideoSettings =
    //    (Engine::Startup never reached its end). Startup sets engine+0x6f0 = 1
    //    only on full success, so a good run stores false and the next boot
    //    skips the launcher dialog; a run that died during video init leaves it
    //    true, and the dialog comes back so the user can fix the settings.
    if (Video::Configuration* vc = engine->GetVideoConfiguration()) {   // +0x6b8
        const bool startupIncomplete = engine->_startupComplete == 0;   // +0x6f0
        vc->settingsValid = startupIncomplete;                          // byte +0x00
        printf("[Shutdown] VideoSettings <- %s (engine+0x6f0=%u)\n",
               startupIncomplete ? "true (show launcher dialog next boot)"
                                 : "false (clean run)",
               engine->_startupComplete);
    }

    // 2. AchievementManager::RemoveAll. ENDGAME B.3.7: the ResolveFn readiness
    // probe goes with the door — RemoveAll is a real member.
    if (Achievement::AchievementManager* am = engine->GetAchievementManager()) {   // +0x6e8
        am->RemoveAll();
    }

    // 3. Input::Device::PlatformShutdown. ENDGAME B.3.7: the ResolveFn readiness
    // probe goes with the door — PlatformShutdown is a real member.
    if (Input::Device* d = engine->GetInputDevice()) {   // +0x6e0
        d->PlatformShutdown();
    }

    // 4. Input::Configuration::Save (skip the FUN_180053440 inner-buffer free)
    if (Input::Configuration* cfg = engine->GetInputConfiguration()) {   // +0x6d8
        cfg->Save();
    }

    // 5. Audio sub-object teardown — vtable[+0x12] / [+0x2] of two children
    // at audio_device+0xa90 / +0xa88, then null those slots.
    if (Audio::Device* ad = engine->GetAudioDevice()) {   // +0x6d0
        // The two calls are IXAudio2MasteringVoice::DestroyVoice (vtable +0x90)
        // and IXAudio2::Release (+0x10), each followed by a null store: that is
        // Audio::Device::PlatformShutdown (0x1800de690) inlined. Phase D.4 A1
        // calls it (the COM calls are platform_audio.h's, audio_xaudio2_win.cpp).
        ad->PlatformShutdown();
    }

    // 6. Audio::Configuration::Save. ENDGAME B.3.7: the ResolveFn readiness
    // probe goes with the door — Save is a real member.
    if (Audio::Configuration* cfg = engine->GetAudioConfiguration()) {   // +0x6c8
        cfg->Save();
    }

    // 7. Video::Device::PlatformShutdown + FreeLibrary on the three plugin
    // HMODULE slots at video_device+0xb0/+0xc8/+0xe0.
    if (Video::Device* vdev = engine->GetVideoDevice()) {   // +0x6c0
        // ?PlatformShutdown@Device@Video@@AEAAXXZ. `AEAA` is retail's PRIVATE, but
        // Device.h declares the member public (this DLL exports its private
        // members — reference_aeaa_is_private_not_unexported).
        vdev->PlatformShutdown();
        for (Video::Device::InterfaceSlot& slot : vdev->dllSlots) {   // +0xb0, stride 24
            // The plugin module is the HMODULE held as void* (FreeLibrary).
            if (slot.module) Platform::ReleaseLibrary(slot.module);
            slot.module = nullptr;
            slot.getDeviceName = nullptr;
            slot.createDeviceData = nullptr;
        }
    }

    // 8. Video::Configuration::Save — this is the snapshot that reaches the
    //    registry, so log the window block it is about to persist.
    logWindow("before Video::Configuration::Save");
    if (Video::Configuration* cfg = engine->GetVideoConfiguration()) {   // +0x6b8
        printf("[Shutdown] Video::Configuration to persist: window %dx%d at %d,%d "
               "fullscreen=%u mode %dx%d\n",
               cfg->windowWidth, cfg->windowHeight,   // +0x1c / +0x20
               cfg->windowX, cfg->windowY,            // +0x14 / +0x18
               cfg->fullscreen,                       // +0x40
               cfg->width, cfg->height);              // +0x0c / +0x10
        // Do not persist a collapsed window. AdjustDisplay copies GetClientRect
        // into Configuration+0x1c/+0x20 on every call with no floor, so if
        // anything reshaped the window at any point, what is sitting here is a
        // degenerate 120x0 rect. This is the last line of defence — the guard
        // in src/render/dx11/devicedata.cpp stops it being latched at all. Saving that
        // makes the next boot fail in CreateColorBuffer and the game becomes
        // unlaunchable until the registry is edited by hand. Fall back to the
        // -1 "no override" sentinel from Video::Configuration's constructor.
        constexpr int kMinSaneWindowDim = 64;
        int& winW = cfg->windowWidth;
        int& winH = cfg->windowHeight;
        if ((winW >= 0 && winW < kMinSaneWindowDim) ||
            (winH >= 0 && winH < kMinSaneWindowDim)) {
            fprintf(stderr, "[Shutdown] refusing to persist collapsed window "
                            "%dx%d — storing -1x-1 instead\n", winW, winH);
            winW = -1;
            winH = -1;
        }
        // ?Save@Configuration@Video@@QEAAXXZ.
        cfg->Save();
    }

    // 9. Application::PlatformShutdown — DestroyWindow + UnregisterClass.
    // ENDGAME B.3.7: the ResolveFn readiness probe goes with the door.
    if (::Application::Application* app = engine->GetApplication()) {   // +0x690
        app->PlatformShutdown();
    }

    // 10. FreeLibrary on Object::Factory's compiler-plugin module (Runtime
    // 0x1801fc6d0, the class static Object::Factory::_compilerModule).
    if (::Object::Factory::_compilerModule)
        Platform::ReleaseLibrary(::Object::Factory::_compilerModule);   // FreeLibrary
    ::Object::Factory::_compilerModule = nullptr;

    // 11. Modification::SaveList
    if (IO::Modification* mod = engine->GetModification()) {   // +0x680
        // ENDGAME B.3.4a closing: the `ResolveFn` readiness probe is gone with
        // the door — `SaveList` is a real IO::Modification member.
        mod->SaveList();
    }

    // 12. Persistence::WriteSettings (gated on engine[0x6f1] == 0)
    //
    // WriteSettings is a CONST member — the export is QEBA, not QEAA. Asking
    // for QEAA resolved to null and this step silently did nothing, so no
    // setting ever reached the registry. That is what made ResetVideoRegistry
    // look necessary. The failure is logged now rather than swallowed.
    if (const IO::Persistence* per = engine->GetPersistence()) {   // +0x678
        if (engine->_noConfigure == 0) {   // +0x6f1
            // ENDGAME B.3.4a closing: the ResolveFn probe and its "did not
            // resolve — settings NOT persisted" else arm are gone with the
            // door. That arm reported a null mangled-name lookup; WriteSettings
            // is a real const member of IO::Persistence now, so the state it
            // described cannot occur.
            per->WriteSettings();
            printf("[Shutdown] Persistence::WriteSettings done\n");
        } else {
            printf("[Shutdown] WriteSettings skipped (engine+0x6f1 set)\n");
        }
    }

    // 13. FileSystem::ClosePackages
    // ENDGAME B.3.4a closing: onto IO::FileSystem::ClosePackages, and the
    // ResolveFn readiness guard retires here with its door.
    if (IO::FileSystem* fs = engine->GetFileSystem()) {   // +0x670
        fs->ClosePackages();
    }

    // (14+ deliberately omitted — see header comment.)
    printf("[Shutdown] done\n"); fflush(stdout);
}

} // anonymous namespace

namespace Game {

int RunGameMain(void* platformHandle, const char* cmdLine) {
    unsigned ok = 0;
    // ENDGAME B7 (CTOR, D5): the Engine is a typed local, as in retail GameMain
    // 0x140041ef0 (by value at [RSP+0x60]). Its real ctor ??0Engine@0@QEAA@PEAX@Z
    // (0x1800dbf00) builds it on untouched stack: no memset first (retail has
    // none; the ctor writes every field but +0x6f2..+0x6f7, which nothing reads).
    // The block ends where the dtor ran before (after "Destroying Engine").
    {
    printf("[GameMain] Constructing Engine...\n");
    ::Engine::Engine engine(platformHandle);
    ::Engine::Engine* const eng = &engine;
    printf("[GameMain] Engine constructed (at %p).\n", (void*)eng);

    StallDebug::Mark("Init() -> RunGameMain entry + Engine ctor");

    // ------------------------------------------------------------------
    // Arena size. arg2 of Engine::Startup is MemoryAllocator::minRangeSize in
    // MEGABYTES (0x1800db8d9: SHL RAX,0x14; MOV [RCX+0x10],RAX) and arg3 is
    // maxTotalSize, 0 meaning unlimited. Retail's 0x200 = a single 512 MB
    // reservation, which is more than any town we can build ever fills — so
    // the allocator's arena-GROWTH path (a second MemoryRange, the range-table
    // realloc-and-copy, every multi-range scan) never executes, and 12.75M
    // shadow-verified operations said nothing about it (WAVE9_LANE_E2.md 5.1).
    const unsigned arenaMB = 0x200;
    printf("[GameMain] Engine::Startup(cmdLine=%p, %u, 0, false)...\n", (void*)cmdLine, arenaMB);
    ok = eng->Startup(cmdLine, arenaMB, 0, false);
    // Engine::Startup somehow disconnects stdout/stderr from the log files
    // that WinMain freopen()'d (cause TBD — Engine::Startup itself doesn't call
    // AllocConsole/FreeConsole/SetStdHandle/freopen per Ghidra). Re-redirect
    // them so subsequent printf reaches the log.
    freopen("standalone_debug.log", "a", stdout);
    freopen("standalone_error.log", "a", stderr);
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    printf("[GameMain] Engine::Startup returned %u\n", ok);
    // Boot got past the point where the launcher dialog would have blocked it.
    BootState::MarkStartupComplete();

    // EXT SEAM (NOT RETAIL, docs/extensions/handins/EXTSCAFFOLD.md): the fixes and
    // extensions toggles, read ONCE from settings.cfg (Persistence::ReadSettings
    // ran inside Startup) and the command line's /profile:<name>.
    if (ok != 0) Ext::InitFromSettings();
    StallDebug::Mark("Engine::Startup");

    // On Engine::Startup failure, dump System::Error::_errorString
    // (Runtime 0x1801fc720, 8KB) — Engine::Startup's failure paths each call
    // System::Error::SetText with a unique tag (e.g., "VideoDevice: ..."),
    // so the string identifies exactly which step bombed.
    if (ok == 0) {
        const char* err = ::System::Error::_errorString;
        if (err[0]) {
            printf("[GameMain] System::Error string after Startup failure:\n");
            for (int i = 0; i < 1024 && err[i]; i++) {
                if (i == 0) printf("    | ");
                fputc(err[i], stdout);
                if (err[i] == '\n' && err[i+1]) printf("    | ");
            }
            fputc('\n', stdout);
        } else {
            printf("[GameMain] System::Error string is empty after Startup failure\n");
        }
    }

    // Probe: dump Factory tree state after Engine::Startup.
    // Engine::Engine::Startup -> Object::Factory::Startup walks _firstTypeInfo
    // and assigns CRC32-derived typeIds (overwriting the 0xFFFFFFFF ctor default).
    // If the walk succeeded, every entry now has a non-FFFFFFFF typeId.
    {
        // ENDGAME B.3.9 RF: the chain head is the class static
        // Object::BaseTypeInfo::_firstTypeInfo (was a by-address read of 0x1801fc6e0).
        const ::Object::BaseTypeInfo* const head = ::Object::BaseTypeInfo::_firstTypeInfo;
        int n = 0, withId = 0;
        for (const ::Object::BaseTypeInfo* p = head; p && n < 1000; p = p->next, n++) {
            if (p->typeId != 0xFFFFFFFF) withId++;
        }
        printf("[GameMain] Post-Startup _firstTypeInfo: %d entries, %d have non-FF typeId\n",
               n, withId);
        // ENDGAME B.3.9 RF: the exe records are CRT-init statics now. Prove every
        // DEFINE_EXE_TYPE record is in the chain with its CRC id, and that no two
        // records share an id -- FATAL otherwise, in every build. Prints the
        // `[FactoryReg] Registered N` line. Only after a Startup that got past
        // Factory::Startup: a failed Startup never assigned the ids.
        if (ok != 0) ::Object::VerifyExeTypeInfos();
        // The whole table, one line per record: id, name, (namespace-stripped)
        // parent name or "-". ORDER-FREE by construction -- the chain order is
        // static-init order and carries no meaning (b39_session/ORDER_MEASUREMENT.md);
        // the gate SORTS these lines (B39_RULES D6/D9). Replaces the old
        // "first ten entries" dump, whose content was the chain order.
        // ONE write for the whole table. The loader thread is live here (it runs
        // InnerLoaderLoop::Load -> GameContainer_Init, as retail does) and its own
        // TRACE lines spliced into these per-line printfs (2026-10-11, SLOTS2B
        // gate: "parent=Descri[tiongeDamiMain]"), which the gate read as a
        // typetable change. stdout is unbuffered, so each printf was its own
        // write; a single fwrite of the assembled text cannot be interleaved.
        {
            std::string table;
            char line[256];
            for (const ::Object::BaseTypeInfo* p = head; p; p = p->next) {
                snprintf(line, sizeof line, "[GameMain]   typeId=%08X name=%s parent=%s\n",
                         p->typeId, p->name ? p->name : "?",
                         p->parentName ? p->parentName : "-");
                table += line;
            }
            fwrite(table.data(), 1, table.size(), stdout);
        }
    }

    StallDebug::Mark("post-Startup typeinfo dump + typeId globals");

    if (ok != 0) {

        // Stage 3: allocate + construct + destroy a MainLoop. (No Run yet —
        // that needs an UpdateLoop chain.)
        {
            // The MemoryAllocator singleton, set by the Engine ctor. (B3R STORAGE:
            // every reader reads the class static live; the engine_funcs
            // snapshot and the self-test's refresh of it are gone.)
            void* memAllocSingleton = System::MemoryAllocator::_singleton;
            printf("[GameMain] MemAllocSingleton (fresh) = %p\n", memAllocSingleton);
            (void)memAllocSingleton;   // B3R: its last reader was the MemAlloc door; the perf build strips the printf

            // Validate per-class factory registrations end-to-end now that
            // Engine::Engine::Engine has populated the MemoryAllocator
            // singleton. See src/app/main.cpp for the implementation.
            RunStartupSelfTests();
            StallDebug::Mark("RunStartupSelfTests + video probes");

            // MainLoop is 0xa0 bytes, 8-byte aligned (matches original GameMain).
            ::Engine::MainLoop* ml = static_cast<::Engine::MainLoop*>(
                System::MemoryAllocator::MemAlloc(0xa0, 8));
            printf("[GameMain] MainLoop alloc = %p\n", (void*)ml);
            if (ml) {
                RtEngine_MainLoop_Ctor(ml, eng);
                printf("[GameMain] MainLoop constructed.\n");

                // Stage 5a: allocate the Game/KeyMap container (0xe0 bytes, 4-byte
                // aligned) and run its ctor. Original GameMain follows this with
                // Game_Register on a settings struct; we skip Game_Register for now
                // and just zero-init settings to the same hardcoded defaults.
                GameContainer* gameObj = static_cast<GameContainer*>(
                    System::MemoryAllocator::MemAlloc(0xe0, 4));
                printf("[GameMain] Game alloc      = %p\n", gameObj);
                if (gameObj) {
                    Game_Ctor(gameObj);
                    StallDebug::Mark("MainLoop ctor + Game ctor");
                    printf("[GameMain] Game_Ctor done; KeyMap singleton -> %p\n",
                           g_gameContainer);
                }

                // Engine::Startup constructed Input::Configuration at
                // engine[0x6d8] (== Input::Configuration::_singleton); its first
                // field is the KeyMap pointer. The KeyMap readers read
                // Input::Configuration::_singleton->_keyMap (B3R STORAGE: the
                // hand-built exref chain this used to fill is gone).
                Input::Configuration* inputCfg = eng->GetInputConfiguration();   // +0x6d8
                Input::KeyMap* keyMap = inputCfg ? inputCfg->_keyMap : nullptr;
                printf("[GameMain] InputConfig=%p  -> KeyMap=%p\n",
                       (void*)inputCfg, (void*)keyMap);
                (void)keyMap;   // the perf build strips the printf

                // The KeyMap's default bindings, the input StringTables and the
                // sprite sheet / "White" / "SaveLoadEmpty" sprites are NOT loaded here:
                // retail's GameContainer_Init 0x140099db0 fills them from
                // Game/GlobalResources.rsc when the outer loader runs
                // InnerLoaderLoop::Load (loader_loop_stubs.cpp), before the outer loop
                // hands over to the splash chain (D.4 RESIDUE; the boot-time copies
                // RegisterDefaultInputBindings / LoadInputNameTables /
                // StandaloneEnsureSpriteSheetLoaded are gone).

                // Settings struct (0x34 bytes). Defaults from original GameMain.
                GameSettings* settings = static_cast<GameSettings*>(
                    System::MemoryAllocator::MemAlloc(0x34, 4));
                printf("[GameMain] Settings alloc  = %p\n", (void*)settings);
                if (settings) {
                    settings->uiScale           = 100;
                    settings->autosaveInterval  = 1;
                    settings->statusIconOpacity = 100;
                    // Retail writes ONE byte at +0x0C (0x140042001) and at +0x18 (0x140042013);
                    // +0x0D..+0x0F and +0x19..+0x1B keep MemAlloc's bytes (B6 row 22: the tree used
                    // to zero them).
                    settings->statusIconEnable  = true;
                    settings->statusIconScale   = 100;
                    // retail: one dword 0x01010001 at +0x14 (0x14004200c)
                    settings->edgeScroll        = true;
                    settings->showCelsius       = false;
                    settings->showWeather       = true;
                    settings->autoSlow          = true;
                    settings->autoPause         = true;
                    settings->lastOption        = 0;   // retail: one qword 0 at +0x1C
                    settings->lastTerrain       = 0;
                    settings->lastMapSize       = 0;
                    settings->lastClimate       = 1;
                    settings->lastStart         = 1;   // retail: one qword 1 at +0x2C
                    settings->lastDisaster      = 0;
                    g_gameSettings = settings;
                    // Retail GameMain calls this immediately after the defaults
                    // (0x14004203d -> 0x140098b60): every field above is only a
                    // fallback for a key IO::Persistence does not have yet. Without
                    // it the game always came up on defaults, which is why the New
                    // Game combo picks never survived a restart.
                    settings->Load();
                }

                // Stage 5b: build a MenuDialogLoopT (exe-internal subclass of
                // Engine::DialogLoopT, drives Dialog\MainMenu.rsc).
                //   1. MemAlloc(0xc00, 8)
                //   2. Construct String "Dialog\\MainMenu.rsc"
                //   3. MenuDialogLoopT's ctor (DialogLoopT base, the stamp,
                //      Random + Audio::Scene + zero-init extra fields)
                //   4. Destroy String
                //
                // Stage 5c: wrap MenuDLT in the Loading.rsc DialogLoaderLoopT
                // (vtable @ 0x140158800). Mirrors the inner branch of the
                // original GameMain (FUN_140041ef0):
                //   - MemAlloc(0xb60, 4)
                //   - Time(0.5f), String "Dialog\\Loading.rsc"
                //   - Engine::DialogLoaderLoopT::DialogLoaderLoopT(self, &str, menuLoop, &time)
                //   - the 0x140158800 stamp (GameLoadDialogLoopT's ctor)
                //   - init colors at +0x188/+0x18c (0xff000000), zero +0x190,
                //     System::Random at +0x194, zero +0xb58 (random text timer),
                //     zero _autoAdvance at +0x40
                // ENDGAME B.3.8 LOOP: the five loops are real classes
                // (src/game/Game/*.h); each is built with its real ctor.
                {
                    void* menuMem = System::MemoryAllocator::MemAlloc(0xc00, 8);
                    printf("[GameMain] MenuDLT alloc = %p\n", menuMem);

                    if (menuMem) {
                        // Construct the dialog filename string.
                        System::String menuStr("Dialog\\MainMenu.rsc");

                        StallDebug::Mark("settings alloc + MenuDLT alloc");
                        // The real class (ctor 0x14009bb00).
                        MenuDialogLoopT* menuLoop = ::new (menuMem) MenuDialogLoopT(menuStr);
                        StallDebug::Mark("MenuDialogLoopT_Ctor");
                        printf("[GameMain] MenuDLT ctor done, vtable=%p\n",
                               *reinterpret_cast<void**>(menuLoop));   // CAST-OK: crash_walk

                        // Helper: build a splash GameLoadDialogLoopT (0xb60
                        // bytes, vtable 0x140158800) wrapping `next`. Mirrors
                        // the identical splash setup blocks in FUN_140041ef0.
                        auto buildSplashDLT = [&](const char* filename, float delaySec,
                                                  Engine::UpdateLoop* next, const char* tag)
                            -> GameLoadDialogLoopT* {
                            void* mem = System::MemoryAllocator::MemAlloc(0xb60, 4);
                            printf("[GameMain] %s alloc = %p\n", tag, mem);
                            if (!mem) return nullptr;

                            System::Time delayTime;
                            delayTime._ticks = 0;
                            RtSystem_Time_Ctor(&delayTime, delaySec);

                            System::String fileStr(filename);

                            // Its ctor is the one retail inlines here:
                            // DialogLoaderLoopT(file, next, delay), the stamp (the
                            // compiler's), the two fade colours, Random at +0x194,
                            // the tip timer, _autoAdvance = 0.
                            GameLoadDialogLoopT* dlt = ::new (mem) GameLoadDialogLoopT(
                                fileStr, next, delayTime);

                            printf("[GameMain] %s ctor done, vtable=%p, next=%p, "
                                   "delay=%.2fs\n",
                                   tag, *reinterpret_cast<void**>(dlt), (void*)next, delaySec);   // CAST-OK: crash_walk
                            return dlt;
                        };

                        // Stage 5c: Loading.rsc splash wrapping menuLoop.
                        GameLoadDialogLoopT* loadingLoop = buildSplashDLT(
                            "Dialog\\Loading.rsc", 0.5f, menuLoop, "LoadingDLT");

                        // Stage 5d: ShiningRock.rsc splash wrapping LoadingDLT.
                        // Original GameMain uses DAT_140172174 = 2.0f for delay.
                        GameLoadDialogLoopT* shiningRockLoop = loadingLoop
                            ? buildSplashDLT("Dialog\\ShiningRock.rsc", 2.0f,
                                             loadingLoop, "ShiningRockDLT")
                            : nullptr;

                        // Pre-load each splash's own .rsc on the main thread BEFORE
                        // MainLoop::Run starts. Boot is non-rendering (no swap chain
                        // Present yet from gameplay code), so blocking ~56ms here is
                        // invisible — whereas blocking those same ms inside
                        // GameLoadDialogLoopT::Startup happens after Engine::Startup
                        // has already presented at least one frame, so the user sees
                        // a brief flash of the DX11 initial backbuffer color before
                        // the splash appears. Loading the .rsc up front populates
                        // _dialogExternal so Startup's fallback synchronous load is
                        // skipped.
                        StallDebug::Mark("splash DLT ctors");
                        if (shiningRockLoop) {
                            printf("[GameMain] pre-loading ShiningRock.rsc\n");
                            // QUALIFIED: the base body (slot 6 is inherited, but
                            // say which body is meant).
                            shiningRockLoop->Engine::DialogLoaderLoopT::Load();
                        }
                        if (loadingLoop) {
                            printf("[GameMain] pre-loading Loading.rsc\n");
                            loadingLoop->Engine::DialogLoaderLoopT::Load();
                        }

                        StallDebug::Mark("splash .rsc preloads (ShiningRock + Loading)");

                        // EXT SEAM (NOT RETAIL, docs/extensions/handins/LOADPROGRESS.md): ext.load_progress
                        // shows the Loading screen on the outer loader loops' frames while mods load.
                        if (loadingLoop)
                            Ext::LoadProgress::BootBegin(loadingLoop->_dialogExternal,
                                                         g_gameSettings->uiScale);

                        // ARM MenuDLT's scene load; do NOT run it here.
                        //
                        // This used to call ChunkedSceneLoader::Init directly,
                        // on the premise that "boot is non-rendering, so the
                        // ~10ms A1 cost is invisible". Both halves were wrong.
                        // The engine window is already VISIBLE by this point —
                        // VideoDX11 CreateDevice ShowWindow's it during
                        // Engine::Startup, well before we get here — and A1
                        // measures ~590ms, not ~10ms. The result was ~590ms of
                        // an on-screen, never-painted window at every boot.
                        //
                        // Arm() records the loop; the first splash runs it from
                        // its Update once it has actually revealed, so the cost
                        // lands behind a drawn loading screen the way retail
                        // does it. After Init runs, MenuDLT struct fields
                        // (+0xb90 menuScene, +0xb98 renderer, etc.) are
                        // populated and the splash Tick(500)s the chunked PhaseB
                        // iteration each frame, keeping the splash animating
                        // through the heavy 12K-15K static-mesh loop.
                        printf("[GameMain] arming ChunkedSceneLoader for MenuDLT "
                               "(runs behind the splash, not during boot)\n");
                        ChunkedSceneLoader::Arm(menuLoop);

                        // Stage 5e: wrap the splash chain in two LoaderLoop layers.
                        // Inner LoaderLoop wraps shiningRockLoop, outer LoaderLoop
                        // wraps inner, both with delay 0.0f (matches
                        // FUN_140041ef0): the two real classes
                        // (src/game/Game/{Inner,Game}LoaderLoop.h), whose ctors
                        // run `??0LoaderLoop@Engine@@`'s body while the compiler
                        // stamps 0x140158870's / 0x1401588d0's table.
                        auto buildLoaderLoop = [&](Engine::UpdateLoop* next, bool inner,
                                                   const char* tag) -> Engine::UpdateLoop* {
                            if (!next) return nullptr;
                            void* mem = System::MemoryAllocator::MemAlloc(0x48, 4);
                            printf("[GameMain] %s alloc = %p\n", tag, mem);
                            if (!mem) return nullptr;

                            System::Time zeroDelay;
                            zeroDelay._ticks = 0;
                            RtSystem_Time_Ctor(&zeroDelay, 0.0f);
                            const System::Time& delay = zeroDelay;
                            Engine::LoaderLoop* ll = inner
                                ? static_cast<Engine::LoaderLoop*>(::new (mem) InnerLoaderLoop(next, delay))
                                : static_cast<Engine::LoaderLoop*>(::new (mem) GameLoaderLoop(next, delay));
                            printf("[GameMain] %s ctor done, vtable=%p, next=%p\n",
                                   tag, *reinterpret_cast<void**>(ll), (void*)next);   // CAST-OK: crash_walk
                            return ll;
                        };

                        Engine::UpdateLoop* innerLoader = shiningRockLoop
                            ? buildLoaderLoop(shiningRockLoop, true, "InnerLL")
                            : nullptr;
                        Engine::UpdateLoop* outerLoader = innerLoader
                            ? buildLoaderLoop(innerLoader, false, "OuterLL")
                            : nullptr;

                        Engine::UpdateLoop* topLoop =
                              outerLoader ? outerLoader
                            : innerLoader ? innerLoader
                            : shiningRockLoop ? static_cast<Engine::UpdateLoop*>(shiningRockLoop)
                            : loadingLoop ? static_cast<Engine::UpdateLoop*>(loadingLoop)
                            : static_cast<Engine::UpdateLoop*>(menuLoop);
                        const char* topTag = outerLoader ? "OuterLL"
                                           : innerLoader ? "InnerLL"
                                           : shiningRockLoop ? "ShiningRockDLT"
                                           : loadingLoop ? "LoadingDLT"
                                           : "MenuDLT (fallback)";
                        if (topLoop) {
                            printf("[GameMain] Calling MainLoop::Run with %s top "
                                   "loop = %p...\n", topTag, (void*)topLoop);
                            LoadTime::Stamp("gamemain.run");   // NOT RETAIL: print-only (MainLoop::Run takes over)
                            ml->Run(topLoop);
                            // Run has returned: no more frames will ever be
                            // rendered, so the boot-state watchdog's hang
                            // detector must switch to its teardown budget here
                            // or every clean quit would read as HUNG.
                            // (src/diag/win/boot_state_watchdog.cpp)
                            BootState_MarkQuitting();
                            printf("[GameMain] MainLoop::Run returned "
                                   "(top loop destroyed by Run)\n");
                        }
                    }
                }

                // Retail GameMain pushes the whole struct back into
                // IO::Persistence here (0x1400422a3 -> 0x140098970), before
                // the frees and before Engine::Shutdown's WriteSettings
                // flushes Persistence to disk. NewGameDialog's OK only writes
                // the settings STRUCT, so this call is what makes the player's
                // terrain / size / climate / start / disaster picks outlive
                // the process. The call comes BEFORE retail's test
                // (0x1400422a0 MOV RCX,RBX; CALL; 0x1400422a8 TEST RBX,RBX), which
                // guards only the free (B9 RES, row 1209).
                settings->Save();
                if (settings) {
                    g_gameSettings = nullptr;
                    System::MemoryAllocator::MemFree(settings);
                    printf("[GameMain] Settings freed.\n");
                }
                if (gameObj) {
                    g_gameContainer = nullptr;
                    System::MemoryAllocator::MemFree(gameObj);
                    printf("[GameMain] Game freed.\n");
                }

                RtEngine_MainLoop_Dtor(ml);
                System::MemoryAllocator::MemFree(ml);
                printf("[GameMain] MainLoop destroyed + freed.\n");
            }
        }

        // BISECT: stepped reimpl of Engine::Engine::Shutdown (Runtime 0x1800db4f0).
        // The stock function silently terminates the process somewhere inside —
        // not even our SetUnhandledExceptionFilter sees a fault. We mirror its
        // body in stages with fflush between each so the last logged step is
        // the one that killed us.
        ShutdownEngineStaged(eng);
    } else {
        // Retail GameMain's Startup-failure arm (Application-x64 0x140041f55):
        //   140041f55 TEST EAX,EAX ; JNZ 0x140041f72
        //   140041f59 CALL [0x140152b58]   ; System::Error::Display()
        //   140041f5f LEA RCX,[RSP+0x60]
        //   140041f64 CALL [0x140152c18]   ; Engine::Engine::Shutdown(&engine)
        //   140041f6a OR EDI,0xffffffff    ; return value -1
        //   140041f6d JMP 0x1400422e4      ; -> ~Engine, return
        // Display shows the "A fatal error has occurred..." box with
        // _errorString (the SDL box since Phase D.4 SDL W6) and blocks until it
        // is dismissed; an unattended run reads it as BLOCKED_DIALOG. The
        // Shutdown is the staged one the success arm also runs as this tree's
        // Engine::Shutdown (see ShutdownEngineStaged's banner): steps 1-13, so
        // the failed boot still stores VideoSettings = true and the launcher
        // dialog comes back next boot, as retail's does.
        fprintf(stderr, "[GameMain] Engine::Startup FAILED\n");
        ::System::Error::Display();
        ShutdownEngineStaged(eng);
    }

    printf("[GameMain] Destroying Engine...\n"); fflush(stdout);
    }   // ~Engine 0x1800dbec0
    printf("[GameMain] Engine destroyed.\n"); fflush(stdout);

    // Retail returns -1 (EDI) on the Startup-failure arm: process exit code
    // 0xFFFFFFFF, read as 255 by a POSIX shell.
    return ok != 0 ? 0 : -1;
}

} // namespace Game
