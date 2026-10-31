// Standalone entry point for Banished reimplementation.
// Replaces Application-x64.exe. Since ENDGAME.md A.1 it also replaces the
// engine: neither Runtime-x64.dll nor VideoDX11-x64.dll is ever loaded, and
// the game loop runs entirely on our reimplemented code.
//
// Build with: make standalone
// Deploy to: C:/GOG Games/Banished/Application-x64.exe (rename original first!)
//


// ENDGAME B.3.5: Video::Context, ahead of <windows.h> — <wingdi.h> declares a
// FUNCTION named Rectangle, which breaks
// Volume.h's Volume::Rectangle (B35 caller brief, the <windows.h> trap).
#include "video/Context.h"
#include "video/Device.h"   // ENDGAME B.3.9 SS2: Video::Device / Configuration::_singleton
#include "options.h"
#include "io/settings_file.h"     // Phase D.4 SETTINGS E.4: the harness guard
#include "io/settings_store.h"    //   and DumpVideoSettings read settings.cfg
#include "runtime_init.h"
#include "render_backend.h"   // Render::DumpSwapChainState / BootedExclusiveFullscreen (the two swap-chain probes)
#include "video_interface.h"
#include "renderdoc_integration.h"
#include "stall_debug.h"
#include "boot_state_watchdog.h"
#include "autostart.h"
#include "stress_harness.h"
#include "sampler.h"
#include "load_time.h"   // [LoadTime] stamps (NOT RETAIL, print-only; block LOADSTAMP)
#include "hw_watch.h"
#include "redzone_alloc.h"
#include "crash_handler.h"               // src/diag/win/crash_handler_win.cpp
#include "alloc_cutover_arm.h"            // RtAllocSwapArmCutover
#include "system/MemoryAllocator.h"   // RtSystemAllocatorSetCutoverArmer
#include "Game/game_main.h"
#include "platform/common/cpu/topology.h"     // block CPULIMIT: Cpu::Boot (sys.cpu_limit, before any thread)
#include "Object/factory_registration.h"
#include "Goal/StatGoal.h"             // StatGoal::GetTypeInfo() for the self-test
#include "object/TypeInfo.h"           // ENDGAME B.3.9 RF: BaseTypeInfo::_firstTypeInfo for the self-test
#include "platform_window.h"   // Phase D.4 SDL W6: the pump observer
#include "platform_app.h"      // Platform::App: the lifecycle hooks around SDL_Init / SDL_Quit
#include "platform_settings.h" // Platform::SetUserDirOverride (BANISHED_USER_DIR, tools/game_dirs.sh)
#include "engine/MainLoop.h"   // ENDGAME B.3.9 SS2: Engine::MainLoop::_singleton
// WAVE 11 follow-up 5 (lane TINFO): the BaseTypeInfo::factoryFn dispatch census.

// Wave 9 lane N's work-scheduler probe tally (src/game/Action/action_base_complete.cpp).
// Declared here rather than in a header: it has exactly one caller, the crash handler.
#include <cstdio>
#include <cstdlib>   // atoi (the harness guard)
#include <cstdint>
#include <cstring>
#include <exception>
#if defined(_WIN32)
#include <io.h>      // _get_osfhandle, _fileno
#include <windows.h>
#include <dbghelp.h>
#else
// Phase D.4 LINUX-1: dladdr for the active vtable's module (DumpVideoState). The swap-chain
// probes stay Windows-only (plans/RENDER.md C.5), so no Windows or DXGI header is read here.
#include <dlfcn.h>
#include <string>
#endif
// Phase D.4 SDL B0.2. SDL_MAIN_HANDLED: our WinMain stays the entry point. With it defined,
// SDL_main.h neither renames main nor compiles SDL's own WinMain (SDL_main_impl.h); it is
// included only because it is the one header that declares SDL_SetMainReady.
#define SDL_MAIN_HANDLED
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_messagebox.h>
#include "rt_crt.h"   // rtcrt::StrICmp, retail's _stricmp (D.4 Q9)

// WAVE 14 LANE SAVEMETA -- src/engine/save/saveload_meta.cpp.
// ============================================================
// GPU preference exports (match original exe)
// ============================================================
#if defined(_WIN32)
extern "C" {
    __declspec(dllexport) unsigned long NvOptimusEnablement = 1;
    __declspec(dllexport) unsigned long AmdPowerXpressRequestHighPerformance = 1;
}
#endif  // the two exports are read by the NVIDIA / AMD Windows drivers only


// ============================================================
// Reset Video* settings to known-safe values BEFORE Engine::Startup.
//
// Retail stored all settings under HKCU\Software\Shining Rock Software LLC
// \Banished; this exe stores them in <Documents>\Banished\settings.cfg
// (Phase D.4 SETTINGS). The standalone's incomplete shutdown path writes garbage back —
// VideoWindowWidth/Height end up as 0, VideoSettings flips to "true" — which
// makes ValidateConfiguration override the swap-chain dims to 120x0 on the
// next boot. DX11 then fails to create a B8G8R8A8 color buffer of that size,
// Engine::Startup returns 0, and we never reach the loader thread.
//
// Resetting WindowWidth/Height/X/Y to -1 ("skip override") and VideoSettings
// to "false" each boot breaks the cycle without touching Persistence::Save.
// Once standalone has a real shutdown path, this can go away.
// ============================================================
// Log the show-state the process was launched with. Video::Device::PlatformStartup
// (VideoDX11 slot 2) calls ShowWindow(hwnd, SW_SHOWDEFAULT), and SW_SHOWDEFAULT
// means "use STARTUPINFO.wShowWindow from whoever called CreateProcess". So the
// window's initial show state is a property of the launcher, not of the game.
// Launching with wShowWindow = SW_SHOWMINIMIZED (e.g. Start-Process
// -WindowStyle Minimized) really does dead-end in CreateColorBuffer, so this
// stays worth logging. It is NOT, however, the cause of the 136x39 collapse —
// that is an external window manager; see "Who collapses the game window?".
#if defined(_WIN32)
static void DumpStartupInfo() {
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    GetStartupInfoW(&si);
    const bool usesShow = (si.dwFlags & STARTF_USESHOWWINDOW) != 0;
    const char* name = "?";
    switch (si.wShowWindow) {
    case SW_HIDE:            name = "SW_HIDE";            break;
    case SW_SHOWNORMAL:      name = "SW_SHOWNORMAL";      break;
    case SW_SHOWMINIMIZED:   name = "SW_SHOWMINIMIZED";   break;
    case SW_SHOWMAXIMIZED:   name = "SW_SHOWMAXIMIZED";   break;
    case SW_SHOWNOACTIVATE:  name = "SW_SHOWNOACTIVATE";  break;
    case SW_SHOW:            name = "SW_SHOW";            break;
    case SW_MINIMIZE:        name = "SW_MINIMIZE";        break;
    case SW_SHOWMINNOACTIVE: name = "SW_SHOWMINNOACTIVE"; break;
    case SW_SHOWNA:          name = "SW_SHOWNA";          break;
    case SW_RESTORE:         name = "SW_RESTORE";         break;
    case SW_SHOWDEFAULT:     name = "SW_SHOWDEFAULT";     break;
    }
    const bool minimizing = usesShow &&
        (si.wShowWindow == SW_SHOWMINIMIZED || si.wShowWindow == SW_MINIMIZE ||
         si.wShowWindow == SW_SHOWMINNOACTIVE);
    printf("[StartupInfo] dwFlags=0x%08lX USESHOWWINDOW=%d wShowWindow=%u (%s)%s\n",
           si.dwFlags, usesShow ? 1 : 0, si.wShowWindow, name,
           minimizing ? "  <-- SW_SHOWDEFAULT WILL MINIMIZE THE GAME WINDOW" : "");
    fflush(stdout);
}
#endif  // STARTUPINFO is the Win32 launcher's; a POSIX process has no show state

// The settings store as main.cpp sees it, BEFORE Engine::Startup: the file
// (or, when it is absent, the one-time read-only registry import the engine's
// ReadSettings will make too). IO::SettingsStore allocates nothing from
// System::MemoryAllocator, which does not exist yet. (Phase D.4 SETTINGS E.4.)
static const IO::SettingsFile::Entry* FindSetting(const IO::SettingsFile::Entries& e,
                                                  const char* key) {
    for (const IO::SettingsFile::Entry& x : e)
        if (x.key == key) return &x;
    return nullptr;
}

[[maybe_unused]] static const char* SettingsSourceName(IO::SettingsStore::Source s) {
    switch (s) {
    case IO::SettingsStore::Source::File:   return "settings.cfg";
    case IO::SettingsStore::Source::Legacy: return "registry import (settings.cfg absent)";
    case IO::SettingsStore::Source::None:   return "none";
    }
    return "?";
}

// Read back the Video* keys exactly as they sit on disk, before anything
// overwrites them. This is what Video::Configuration::Startup will parse.
static void DumpVideoSettings(const char* tag) {
    std::string path;
    IO::SettingsFile::Entries entries;
    const IO::SettingsStore::Source src =
        IO::SettingsStore::DefaultPath(path) ? IO::SettingsStore::Load(path, entries)
                                             : IO::SettingsStore::Source::None;
    if (src == IO::SettingsStore::Source::None) {
        printf("[VideoReg %s] settings absent (first run)\n", tag);
        return;
    }
    printf("[VideoReg %s] source: %s (%s)\n", tag, SettingsSourceName(src), path.c_str());
    static const char* keys[] = {
        "VideoSettings", "VideoInterface", "VideoFullscreen",
        "VideoWidth", "VideoHeight",
        "VideoWindowX", "VideoWindowY",
        "VideoWindowWidth", "VideoWindowHeight",
    };
    for (const char* k : keys) {
        // [[maybe_unused]]: the perf build compiles printf away (log_strip.h).
        [[maybe_unused]] const IO::SettingsFile::Entry* e = FindSetting(entries, k);
        if (e != nullptr)
            printf("[VideoReg %s]   %-18s = \"%s\"\n", tag, k, e->value.c_str());
        else
            printf("[VideoReg %s]   %-18s = <absent>\n", tag, k);
    }
    fflush(stdout);
}

// ============================================================
// Unattended-run guard (harness only — NOT a settings override)
//
// Retail's launcher dialog is gated on one bit: Engine::Shutdown stores
// VideoSettings = "Engine::Startup never reached its end", and
// Engine::PlatformConfigure shows the dialog whenever that bit is true. So a
// clean run leaves it false and boots straight into the game; a run that died
// during video init leaves it true and the dialog comes back so the user can
// repair their settings. That is the whole mechanism, and the standalone now
// participates in it unmodified.
//
// Phase D.4 SETTINGS E.5: this exe has NO launcher dialog. Half B (the bit
// set) validates the configuration and proceeds as Play; the dialog's two
// check boxes are the settings.cfg entries LauncherDisableMods /
// LauncherResetSettings. Nothing can block on a modal any more, so the
// clearing below now only keeps an unattended run on Half A, retail's
// ordinary boot path, rather than Half B's ValidateConfiguration.
//
// The one case it does not survive is ours: during development we kill the
// process constantly, which looks exactly like a crash and leaves the bit set.
// An automated run (make sa-run / sa-run-menu) would then block forever on a
// modal Win32 dialog nobody is there to click.
//
// So when — and only when — a harness env var says this run is unattended, we
// assert what we already know ("the previous run was ended deliberately") by
// clearing that one bit in the same settings store the engine reads. No byte
// patching, no forced resolutions, no forced renderer.
//
// The second half repairs a non-positive VideoWindowWidth/Height. Those are
// not a user preference — they are the residue of an external window manager
// collapsing the window (PowerToys FancyZones; see the "Who collapses the game
// window?" note below). Video::Device::AdjustDisplay re-reads GetClientRect on
// every call and writes the result back into the configuration, so whatever
// size anything else forced on the window latches into the settings.
// Persisting that makes the next boot fail in
// CreateColorBuffer ("[0x0 4xAA]") and every boot after it. Retail recovers via
// the launcher dialog; an unattended run has no one to drive it, so we drop
// back to the -1 "no override" sentinel the Video::Configuration constructor
// itself uses and let the engine pick the size.
static void HarnessGuardUnattendedBoot() {
    const bool unattended = Opt().unattendedBoot;
    if (!unattended) {
        printf("[Standalone] Attended run — launcher half left to retail rules "
               "(Half B iff the last run did not finish Engine::Startup; no dialog, "
               "settings.cfg's Launcher* entries replace it)\n");
    }

    // Phase D.4 SETTINGS E.4: the store is <Documents>\Banished\settings.cfg
    // (IO::SettingsStore, the same file the engine's ReadSettings reads). When
    // the file is absent this loads the one-time read-only registry import;
    // a repair then WRITES the file (import first, then write), and the
    // engine's ReadSettings reads that file instead of importing again.
    std::string path;
    IO::SettingsFile::Entries entries;
    if (!IO::SettingsStore::DefaultPath(path) ||
        IO::SettingsStore::Load(path, entries) == IO::SettingsStore::Source::None) {
        printf("[Standalone] HarnessGuard: settings absent — nothing to guard\n");
        return;
    }
    bool changed = false;

    // Every Banished setting is a string, parsed with strtol / strcmp by
    // Persistence::Get.
    auto setStr = [&](const char* name, const char* val) {
        changed = true;
        for (IO::SettingsFile::Entry& e : entries)
            if (e.key == name) { e.value = val; return; }
        entries.push_back(IO::SettingsFile::Entry{name, val});
    };
    auto getInt = [&](const char* name, int* out) {
        const IO::SettingsFile::Entry* e = FindSetting(entries, name);
        if (e == nullptr)
            return false;
        *out = atoi(e->value.c_str());
        return true;
    };

    if (unattended) {
        const IO::SettingsFile::Entry* settings = FindSetting(entries, "VideoSettings");
        if (settings != nullptr && settings->value == "true") {
            setStr("VideoSettings", "false");
            printf("[Standalone] HarnessGuard: VideoSettings true -> false "
                   "(unattended run; suppresses the launcher dialog the previous "
                   "abnormal exit armed)\n");
        }
    }

    // Degenerate window size repair — runs on EVERY launch, attended or not.
    //
    // This is the one deliberate deviation from retail in the settings path,
    // and it is a rejection of impossible values rather than an override of
    // real ones. A collapsed window (the 120x0 client of a minimized frame)
    // gets latched into Video::Configuration by AdjustDisplay and persisted by
    // Engine::Shutdown; on the next boot the windowed override feeds it back
    // into the swap chain and CreateColorBuffer fails with "[0x0 4xAA]", so
    // Engine::Startup returns 0 and the game never boots again. No user ever
    // chose a 120x0 window, so anything below a plausible minimum goes back to
    // the -1 "no override" sentinel that Video::Configuration's own
    // constructor uses, and the engine picks the size.
    constexpr int kMinSaneWindowDim = 64;
    int w = -1, h = -1;
    const bool haveW = getInt("VideoWindowWidth",  &w);
    const bool haveH = getInt("VideoWindowHeight", &h);
    const bool degenerate = (haveW && w >= 0 && w < kMinSaneWindowDim) ||
                            (haveH && h >= 0 && h < kMinSaneWindowDim);
    if (degenerate) {
        setStr("VideoWindowWidth",  "-1");
        setStr("VideoWindowHeight", "-1");
        printf("[Standalone] Repaired collapsed window size %dx%d -> -1x-1 "
               "(a previous run persisted a minimized frame's client rect; see "
               "Video::Device::AdjustDisplay)\n", w, h);
    }

    // Store merges over the file (absent after an import: the whole imported
    // set plus the repairs is written).
    if (changed && !IO::SettingsStore::Store(path, entries))
        printf("[Standalone] HarnessGuard: FAILED to write %s\n", path.c_str());
}

// Forward decl — defined alongside the focus-event message hook, used from
// RunStartupSelfTests and CallWndProcHook.
static void DumpSwapChainState(const char* tag);
static void CaptureBootFullscreenMode();

// ============================================================
// Factory self-test: walk _firstTypeInfo to find a registered type by name,
// invoke its factoryFn (the same pointer External::Load would dispatch to),
// and validate the resulting object's vtable + ctor-set field defaults.
//
// This proves the per-class factory path end-to-end without requiring the
// loader thread to reach the type. As more types get real factories, add
// one of these per type so we can validate locally.
// ============================================================
struct FieldCheck {
    uintptr_t offset;
    uint32_t  expected;
    int       width;          // 1, 2, or 4 bytes
    const char* name;
};

// `expected` is the class's own record (`X::GetTypeInfo()`): the test asks "did the
// factory build THIS class", and the built object's slot 2 (`Type()`) answers with its
// class's type id. Factory::Startup has written the ids by now; a record still holding
// the 0xFFFFFFFF sentinel fails the test instead of matching every unstarted class. (It
// compared the vptr with the class's compiler vtable until 2026-10-26, and before
// ENDGAME B.3.9 EXA the retail table read back through the address registry.)
static bool RunFactorySelfTest(const char* typeName,
                               const ::Object::BaseTypeInfo& expected,
                               size_t expectedSize,
                               const FieldCheck* fields, size_t fieldCount) {
    printf("\n[SelfTest %s] start\n", typeName);

    // ENDGAME B.3.9 RF: the chain head is the class static, not a by-address
    // read of ?_firstTypeInfo@ (0x1801fc6e0).
    const ::Object::BaseTypeInfo* match = nullptr;
    for (const ::Object::BaseTypeInfo* p = ::Object::BaseTypeInfo::_firstTypeInfo; p; p = p->next) {
        if (p->name && std::strcmp(p->name, typeName) == 0) { match = p; break; }
    }
    if (!match) {
        printf("[SelfTest %s] FAIL: not found in _firstTypeInfo (registration didn't run)\n",
               typeName);
        return false;
    }
    printf("[SelfTest %s]   found entry @ %p (factoryFn=%p, parent=%s)\n",
           typeName, (const void*)match,
           (void*)match->factoryFn,
           match->parentName ? match->parentName : "(null)");

    if (!match->factoryFn) {
        printf("[SelfTest %s] FAIL: factoryFn is NULL\n", typeName);
        return false;
    }

    ::Object::Base* const obj = match->factoryFn();
    if (!obj) {
        printf("[SelfTest %s] FAIL: factory returned NULL\n", typeName);
        return false;
    }
    printf("[SelfTest %s]   factory returned %p\n", typeName, (void*)obj);

    // Validate the class
    const uint32_t actualType = obj->Type();
    if (expected.typeId == 0xFFFFFFFFu || actualType != expected.typeId) {
        printf("[SelfTest %s] FAIL: type id 0x%08x != expected 0x%08x (the class's record)\n",
               typeName, actualType, expected.typeId);
        return false;
    }
    printf("[SelfTest %s]   type id OK (0x%08x)\n", typeName, actualType);

    // Validate field defaults
    bool allOk = true;
    for (size_t i = 0; i < fieldCount; i++) {
        const FieldCheck& f = fields[i];
        uint32_t actual = 0;
        switch (f.width) {
        case 1: actual = *(uint8_t*) ((uintptr_t)obj + f.offset); break;   // CAST-OK: crash_walk
        case 2: actual = *(uint16_t*)((uintptr_t)obj + f.offset); break;   // CAST-OK: crash_walk
        case 4: actual = *(uint32_t*)((uintptr_t)obj + f.offset); break;   // CAST-OK: crash_walk
        default:
            printf("[SelfTest %s]   FAIL: bad field width %d for %s\n",
                   typeName, f.width, f.name);
            allOk = false; continue;
        }
        if (actual != f.expected) {
            printf("[SelfTest %s]   FAIL: %s @+0x%llx = 0x%X, expected 0x%X\n",
                   typeName, f.name, (unsigned long long)f.offset, actual, f.expected);
            allOk = false;
        }
    }
    if (!allOk) return false;

    printf("[SelfTest %s] PASS (sizeof=%zu, %zu fields verified)\n",
           typeName, expectedSize, fieldCount);
    // Object intentionally leaked — one-shot startup test, OS reclaims at exit.
    // Calling the dtor would require the full Goal::~Goal/String chain to be
    // valid, which is out of scope for the factory test.
    return true;
}

// ============================================================
// Probe the Video subsystem state after Engine::Startup. Tells us:
//   - Configuration::_singleton[+4]: requested interface index (0=DX9, 1=DX11, 2=GL)
//   - Device::_singleton[+0xf8]:     active interface index (CreateVideoInterface picked this)
//   - Device::_singleton[+0x100]:    active DeviceData pointer (read by ImageBuffer::PlatformConstruct)
//   - Device plugin slots (HMODULEs) at +0xb0, +0xc8, +0xe0
//
// The active DeviceData's vtable will reveal the actual renderer in use:
// pointers in VideoDX11-x64.dll vs. VideoDX9-x64.dll. This pins down the
// "DX9 or DX11" question that drives the +0x267E crash.
// ============================================================
static void DumpVideoState() {
    ::Video::Device* device = ::Video::Device::_singleton;
    ::Video::Configuration* config = ::Video::Configuration::_singleton;
    printf("\n[VideoProbe] Configuration::_singleton = %p\n", (void*)config);
    if (config) {
        int interfaceIdx = config->interfaceIndex;   // +0x04
        int adapterIdx   = config->adapterIndex;     // +0x08
        int width        = config->width;            // +0x0c
        int height       = config->height;           // +0x10
        const char* iname =
            interfaceIdx == 0 ? "VideoDX9" :
            interfaceIdx == 1 ? "VideoDX11" :
            interfaceIdx == 2 ? "VideoGL" : "?";
        printf("[VideoProbe]   +0x04 interface = %d (%s)\n", interfaceIdx, iname);
        printf("[VideoProbe]   +0x08 adapter   = %d\n", adapterIdx);
        printf("[VideoProbe]   +0x0c width     = %d\n", width);
        printf("[VideoProbe]   +0x10 height    = %d\n", height);

        // The window block. Video::Device::AdjustDisplay's windowed branch
        // rewrites +0x1c/+0x20 from GetClientRect on every call, so any size an
        // external window manager forces on the window ends up here. Whatever
        // sits here now is what Video::Configuration::Save writes to the
        // registry at shutdown, so a 0 in either field means the next boot is
        // poisoned.
        const int cfgWinX = config->windowX;        // +0x14
        const int cfgWinY = config->windowY;        // +0x18
        const int cfgWinW = config->windowWidth;    // +0x1c
        const int cfgWinH = config->windowHeight;   // +0x20
        printf("[VideoProbe]   +0x00 settings  = %d  (true => launcher dialog next boot)\n",
               config->settingsValid);
        printf("[VideoProbe]   +0x14 windowX   = %d\n", cfgWinX);
        printf("[VideoProbe]   +0x18 windowY   = %d\n", cfgWinY);
        printf("[VideoProbe]   +0x1c windowW   = %d%s\n", cfgWinW,
               cfgWinW <= 0 ? "   <-- POISONED (AdjustDisplay latched an externally-forced size)" : "");
        printf("[VideoProbe]   +0x20 windowH   = %d%s\n", cfgWinH,
               cfgWinH <= 0 ? "   <-- POISONED (AdjustDisplay latched an externally-forced size)" : "");
        printf("[VideoProbe]   +0x40 fullscreen= %d  +0x41 vsync=%d  +0x42 skipShow=%d\n",
               config->fullscreen, config->vsync, config->field42);
    }

    printf("[VideoProbe] Device::_singleton        = %p\n", (void*)device);
    if (device) {
        int activeIdx = device->activeInterface;                   // +0xf8
        ::Video::DeviceData* activeDD = device->deviceData;       // +0x100
        int interfaceCount = device->interfaceCount;               // +0x48
        const ::Video::Device::InterfaceSlot& dx9  = device->dllSlots[0];   // +0xb0
        const ::Video::Device::InterfaceSlot& dx11 = device->dllSlots[1];   // +0xc8
        const ::Video::Device::InterfaceSlot& gl   = device->dllSlots[2];   // +0xe0
        printf("[VideoProbe]   +0x48  interface count = %d\n", interfaceCount);
        printf("[VideoProbe]   +0xf8  active idx  = %d\n", activeIdx);
        printf("[VideoProbe]   +0x100 active DeviceData = %p\n", (void*)activeDD);
        printf("[VideoProbe]   slot0 DX9:  HMOD=%p GetName=%p Create=%p\n", dx9.module,
               (void*)dx9.getDeviceName, (void*)dx9.createDeviceData);
        printf("[VideoProbe]   slot1 DX11: HMOD=%p GetName=%p Create=%p\n", dx11.module,
               (void*)dx11.getDeviceName, (void*)dx11.createDeviceData);
        printf("[VideoProbe]   slot2 GL:   HMOD=%p\n", gl.module);
        if (activeDD) {
            void* vtbl = *reinterpret_cast<void**>(activeDD);   // the vtable word: logged, then its module looked up  // CAST-OK: crash_walk
            printf("[VideoProbe]   active DeviceData vtable = %p\n", vtbl);
#if defined(_WIN32)
            HMODULE owner = nullptr;
            if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                   GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                   (LPCSTR)vtbl, &owner) && owner) {  // CAST-OK: crash_walk
                char path[MAX_PATH] = {0};
                GetModuleFileNameA(owner, path, MAX_PATH);
                const char* nm = strrchr(path, '\\');
                nm = nm ? nm + 1 : path;
                printf("[VideoProbe]   active vtable lives in: %s\n", nm);

                // The active vtable must be one of ours, i.e. in this exe.
                //
                // The failure this catches: if Video::Device cannot bring up the
                // configured interface it falls back to interface 0 and loads
                // VideoDX9-x64.dll, and the game then renders correctly on a
                // completely original DX9 plugin. Verified 2026-07-24; the only
                // sign in a 10k-line log was one "seam not installed" line,
                // which is not enough to stop someone A/B-ing our port against
                // a run that never used it.
                const char* expected = "OpenBA.exe";
                if (rtcrt::StrICmp(nm, expected) != 0) {
                    fprintf(stderr,
                            "[VideoProbe] *** THE DX11 PORT IS NOT RUNNING ***\n"
                            "[VideoProbe] The active renderer's vtable lives in %s; "
                            "it should be %s.\n"
                            "[VideoProbe] Video::Device fell back to another plugin, so "
                            "every draw is ORIGINAL code.\n"
                            "[VideoProbe] Anything measured from this run is meaningless.\n",
                            nm, expected);
                    fflush(stderr);
                }
            } else {
                printf("[VideoProbe]   active vtable in unknown module\n");
            }
#else
            // dladdr names the object holding the vtable: the exe, or a shared library.
            Dl_info owner;
            if (dladdr(vtbl, &owner) != 0 && owner.dli_fname != nullptr) {
                const char* nm = strrchr(owner.dli_fname, '/');
                nm = nm ? nm + 1 : owner.dli_fname;
                printf("[VideoProbe]   active vtable lives in: %s\n", nm);
                const char* expected = "OpenBA";
                if (std::strcmp(nm, expected) != 0) {
                    fprintf(stderr,
                            "[VideoProbe] *** THE DX11 PORT IS NOT RUNNING ***\n"
                            "[VideoProbe] The active renderer's vtable lives in %s; "
                            "it should be %s.\n", nm, expected);
                    fflush(stderr);
                }
            } else {
                printf("[VideoProbe]   active vtable in unknown module\n");
            }
#endif
        }
    }
    fflush(stdout);
}

// Called from game_main.cpp after Engine::Startup completes (so that the
// MemoryAllocator singleton is populated). Linked via the extern decl below.
extern "C" void RunStartupSelfTests() {
    printf("\n[Standalone] === Running factory self-tests ===\n");

    static const FieldCheck statGoalFields[] = {
        { 0x30, 5,    4, "statTypeA" },
        { 0x34, 5,    4, "statTypeB" },
        { 0x38, 10,   4, "comparisonType" },
        { 0x3c, 1,    4, "fixedValue" },
        { 0x40, 0,    1, "requireOtherGoalsMet" },
        { 0x50, 0xFFFFFFFFu, 4, "statAGeneration" },
        { 0x68, 0xFFFFFFFFu, 4, "statBGeneration" },
        { 0x78, 0x20, 4, "statFlags" },
        { 0x7c, 1,    4, "statSubType" },
    };
    RunFactorySelfTest("StatGoal", StatGoal::GetTypeInfo(), 0x80,
                       statGoalFields, sizeof(statGoalFields)/sizeof(*statGoalFields));

    DumpVideoState();
    DumpSwapChainState("AFTER_ENGINE_STARTUP");
    CaptureBootFullscreenMode();
}

// ============================================================
// Swap-chain state probe — reads IDXGISwapChain at
// Device::_singleton[+0x100][+0x18] (the active DeviceData's swap chain
// pointer, populated by VideoDX11!FUN_180004e10 during Engine::Startup),
// then calls GetFullscreenState (vtable+0x58) and GetDesc (vtable+0x60).
//
// The fullscreen-on-focus-loss bug shows up here: standalone enters DXGI
// exclusive fullscreen, focus-loss makes DXGI silently drop FSE, the
// window snaps to VideoWindow{Width,Height} (1280x720) — about 2/3 of
// a 1080p desktop — and Present succeeds but renders to a stale surface.
// Logging the swap chain's Windowed flag + DXGI_MODE_DESC across focus
// events tells us whether the engine's recovery (FUN_180004340) is
// firing AND whether SetFullscreenState(TRUE) succeeds on refocus.
// ============================================================

// The probe bodies live with the backend that owns the swap chain
// (src/render/dx11/swap_probe_dx11.cpp; the Vulkan and Metal backends print one
// "not probed" line): src/render/common/render_backend.h.
static void DumpSwapChainState(const char* tag) {
    Render::DumpSwapChainState(tag);
}

// ============================================================
// Window-message observer — log focus / activation / size messages so we can see
// what the engine's WindowProc receives when the user clicks outside the window.
//
// Phase D.4 SDL W6: this was a WH_CALLWNDPROC hook (SetWindowsHookExW on the main
// thread). The window procedure is SDL's now, and the messages WindowProc receives
// are the tuples Platform::Pump translates from SDL events, so the observer is
// called by the pump itself, before WindowProc, on the main thread
// (Platform::Window::SetMessageObserver). It sees exactly what WindowProc sees:
// WM_SIZE, WM_ACTIVATE, WM_SETFOCUS / WM_KILLFOCUS, WM_SETCURSOR, WM_MOUSEWHEEL,
// WM_EXITSIZEMOVE, WM_CLOSE / WM_DESTROY, and no longer the messages only USER32
// saw (WM_NCACTIVATE, WM_ACTIVATEAPP, WM_WINDOWPOSCHANGING, ...). Not
// SDL_AddEventWatch: its filter "may run in a different thread", and
// ForceVideoContextReset touches the render context.
// ============================================================

static const char* WMNameForFocus(uint32_t m) {
    switch (m) {
    case 0x0006: return "WM_ACTIVATE";
    case 0x001C: return "WM_ACTIVATEAPP";
    case 0x0007: return "WM_SETFOCUS";
    case 0x0008: return "WM_KILLFOCUS";
    case 0x0086: return "WM_NCACTIVATE";
    case 0x0024: return "WM_GETMINMAXINFO";
    case 0x0046: return "WM_WINDOWPOSCHANGING";
    case 0x0047: return "WM_WINDOWPOSCHANGED";
    case 0x001A: return "WM_SETTINGCHANGE";
    default:     return nullptr;
    }
}

// Captured at boot: was the swap chain created with Windowed=0 (DXGI
// exclusive fullscreen)? Drives whether ForceVideoContextReset fires on
// refocus — borderless/windowed swap chains never lose their back buffer
// across focus events, so the recovery is unnecessary (and the Reset call
// would visibly flicker the menu for no reason).
static bool g_bootedExclusiveFullscreen = false;

static void CaptureBootFullscreenMode() {
    // DX11 reads its swap chain's Windowed flag; the other backends answer false
    // (no DXGI exclusive fullscreen under SDL's Vulkan / Metal surfaces).
    g_bootedExclusiveFullscreen = Render::BootedExclusiveFullscreen();
    if (::Video::Device::_singleton && ::Video::Device::_singleton->deviceData) {
        printf("[Standalone] Boot mode: %s\n",
               g_bootedExclusiveFullscreen ? "DXGI EXCLUSIVE FULLSCREEN"
                                           : "borderless/windowed");
    }
}

// Recovery for the DXGI-exclusive-fullscreen focus-loss bug: on refocus
// the engine's WM_ACTIVATE handler (VideoDX11!FUN_1800042d0 → FUN_180004340)
// rebuilds the swap chain + DeviceData RTV/DSV, but the per-frame
// Video::Context cache at Context+0x2160 still holds stale references to
// the old back-buffer's RTV/DSV. Calling Video::Context::Reset (the same
// function the engine itself invokes when BeginFrame fails) flushes that
// cache so the next Draw rebinds the rebuilt resources. Without this,
// Present succeeds (returns S_OK) but pixels stay black.
//
// No-op when booted in borderless/windowed since DXGI doesn't drop the
// back buffer across focus events in that mode.
static void ForceVideoContextReset(const char* tag) {
    if (!g_bootedExclusiveFullscreen) return;
    if (!::Engine::MainLoop::_singleton) return;
    ::Engine::MainLoop* ml = ::Engine::MainLoop::_singleton;
    ::Video::Context* ctx = ml->_context;   // +0x10
    if (!ctx) return;
    ml->_contextDirty = 0;  // +0x18: engine clears m_videoContextDirty before Reset
    ctx->Reset();
    printf("[CtxReset %s] Video::Context::Reset(ctx=%p) — FSE refocus recovery\n",
           tag, ctx);
    fflush(stdout);
}

// Phase D.4 SDL W6: the black-brush swap (EnsureBlackWindowBrush, IsEngineWindow) is
// gone. It replaced the "Banished" class's COLOR_WINDOW + 1 background with
// BLACK_BRUSH so the window was black, not white, between ShowWindow and the first
// Present. The class is registered through SDL now (no background brush) and SDL's
// WndProc paints the client black on its first WM_ERASEBKGND
// (SDL_HINT_WINDOWS_ERASE_BACKGROUND_MODE's default), which is the colour the swap
// forced.

// [[maybe_unused]]: the perf build compiles printf's arguments away.
static void WndMsgObserver([[maybe_unused]] void* hwnd, uint32_t message, uint64_t wParam,
                           [[maybe_unused]] int64_t lParam) {
    // WM_SIZE carries the dimensions that end up in Video::Device+0x04/+0x08 and
    // then in VideoWindowWidth/Height. Decode them so a collapse to a minimum-size
    // window is attributable to the message that caused it. (The WM_WINDOWPOSCHANGING
    // decode went with the WH_CALLWNDPROC hook: no SDL event carries it.)
    if (message == 0x0005 /*WM_SIZE*/) {
        printf("[WndMsg] WM_SIZE               hwnd=%p type=%llu %ux%u\n",
               hwnd, (unsigned long long)wParam,
               (unsigned)(lParam & 0xFFFF),
               (unsigned)((lParam >> 16) & 0xFFFF));
        fflush(stdout);
    }
    if (const char* name = WMNameForFocus(message)) {
        printf("[WndMsg] %-20s hwnd=%p w=%llx l=%llx\n",
               name, hwnd,
               (unsigned long long)wParam,
               (unsigned long long)lParam);
        // Refocus recovery: WM_ACTIVATE w!=0 is dispatched before WM_SETFOCUS (the
        // pump keeps Win32's order) and this observer runs BEFORE WindowProc, so
        // the engine's own WM_ACTIVATE handler (DeviceData_OnActivate) runs right
        // after it on the same message. Calling Reset here lands on the next Draw
        // frame rather than waiting until WM_SETFOCUS. Our Reset clears
        // Video::Context's cached state, which the engine does not touch, so by
        // the next frame both sides are fresh.
        if (message == 0x0006 /*WM_ACTIVATE*/ && (wParam & 0xFFFF) != 0) {
            ForceVideoContextReset("WM_ACTIVATE(refocus)");
        }
        fflush(stdout);
    }
}

// ============================================================
// Who collapses the game window?  ANSWERED 2026-07-24: PowerToys FancyZones.
//
// A SetWindowPos to 136x39 at an off-screen origin lands on the engine window
// about 1.6s in, carrying SWP_ASYNCWINDOWPOS. Video::Device::AdjustDisplay then
// latches the resulting 120x0 client rect into Video::Configuration, and every
// later call re-applies it (240 times in the captured run).
//
// The caller is an EXTERNAL PROCESS: FancyZones, with no app exclusions and
// openWindowOnActiveMonitor set, snapping the newly created window into a zone
// on another monitor. Adding OpenBA.exe to fancyzones_excluded_apps takes
// the collapse from 240 occurrences to 0 with no code change. Nothing in this
// exe ever asked for that size.
//
// Two readings that wasted time here, recorded so they are not repeated:
//   * 136x39 is SM_CXMIN x SM_CYMIN -- the OS *minimum tracking size*, i.e. the
//     clamp on a degenerate request. It is NOT SM_CXMINIMIZED x SM_CYMINIMIZED
//     (160x28). WM_SIZE carries type=0 (SIZE_RESTORED) and IsIconic() is 0, so
//     the window is never minimized at any point.
//   * SWP_ASYNCWINDOWPOS is passed by the CALLER, not added by the OS. It is
//     the tell for a cross-process caller.
//
// The in-process user32 IAT interposers that chased this lived here; they hooked
// Runtime-x64.dll's imports and died with the DLL (ENDGAME.md A.1). They could
// never have seen the culprit anyway. The cheap discriminator is in
// src/render/dx11/devicedata.cpp: if DeviceData_Resize already sees a degenerate client
// rect on entry, the caller is external.
// ============================================================

static void InstallWndMsgHook() {
    Platform::Window::SetMessageObserver(&WndMsgObserver);
    printf("[Standalone] Window-message observer installed (focus diag)\n");
}

// ============================================================
// Initialization sequence
// ============================================================
static bool Init() {
    // 1. Install crash handler — only catches second-chance exceptions
    //    so first-chance AVs absorbed by Banished's SEH chains stay quiet.
    //    (src/diag/win/crash_handler_win.cpp: SetUnhandledExceptionFilter +
    //    AddVectoredExceptionHandler(1, FirstChanceFailFast).)
    CrashHandler_Install();
    HwWatch_Init();
    Sampler_Init();   // BANISHED_SAMPLE=<hz>: sim-thread sampling profiler

    // 1a. Open the dedicated crash log NOW (before any pure-virtual /
    //     terminate handler can be hit). Re-opens are no-ops because
    //     CREATE_ALWAYS truncates and CrashHandler_OpenLog is also called below
    //     for normal flow — explicit early call here so the pure-virtual
    //     override has somewhere to write if it fires during init.
    CrashHandler_OpenLog();

    // 1b. Install the std::terminate handler. libstdc++'s __cxa_pure_virtual
    //     and uncaught C++ exceptions land here; abort() bypasses SEH so
    //     without this, a pure-virtual call leaves zero log evidence.
    CrashHandler_InstallTerminate();

    // 1b. Log the Video* keys exactly as they sit on disk, then apply the
    //     unattended-run guard (no-op for a normal launch). Everything the
    //     engine reads comes from settings.cfg via Persistence::ReadSettings and
    //     Video::Configuration::Startup.
    DumpVideoSettings("AT-BOOT");
    HarnessGuardUnattendedBoot();
    // Start publishing boot state BEFORE Engine::Startup: a modal dialog inside Startup
    // (an error box; the launcher dialog itself is gone since SETTINGS E.5) keeps the
    // process alive pumping messages while the log goes silent -- which reads exactly
    // like a slow load. See boot_state_watchdog.cpp.
    BootState::Start();
    StallDebug::Mark("settings probe + harness guard");

    // 2. No engine DLL may be mapped (ENDGAME.md A.1).
    StallDebug::Mark("(pre RuntimeDllNotMapped)");
    if (!RuntimeDllNotMapped()) {
        fprintf(stderr, "[Standalone] FATAL: Runtime-x64.dll is "
                        "mapped in this process and must not be (ENDGAME A.1).\n");
        return false;
    }

    StallDebug::Mark("RuntimeDllNotMapped");

    // 3. The allocator's cutover armer (src/diag/alloc_cutover_arm.cpp), handed
    //    in BEFORE RuntimeInit() arms the allocator: without it
    //    RtSystemAllocatorArm() refuses.
    RtSystemAllocatorSetCutoverArmer(&RtAllocSwapArmCutover);

    // 4. The ported Runtime's one-time initialisation (src/app/runtime_init.cpp):
    //    libm seam, allocator, [ExtLoad] exit hooks, run identity, [SLOTS29].
    RuntimeInit();
    StallDebug::Mark("RuntimeInit");

    // 4b. The renderer. B5 (the dx11 seam, 2026-10-05): there is nothing to
    //     install. The twelve VideoDX11::*Data classes are real subclasses of the
    //     runtime's Video::*Data bases with the compiler's vtables; the DX11
    //     interface slot (Video::Device::dllSlots[1]) is filled by
    //     VideoInterfaceFillDx11Slot, the first statement of our ported
    //     Video::Device::InitializeInterfaces. VideoDX11-x64.dll is never loaded.
    StallDebug::Mark("dx11 plugin");

    // 4a. Optional heap-overflow detector (BANISHED_REDZONE env var). Wraps
    //     fn_MemAlloc/fn_MemFreeStatic with tail-canary redzones so an OOB
    //     write in the menu/geometry deserialize is caught at the source via
    //     RedzoneAlloc_ValidateAll() checkpoints, instead of crashing later in
    //     the allocator free-list. No-op unless the env var is set.
    RedzoneAlloc_Install();

    // 4c. The BANISHED_FNDTOR_CHECK use-after-free interposer used to install
    //     here. It worked by swapping the global `fn_Filename_Dtor` function
    //     pointer for a checking wrapper; ENDGAME B.3.1 deleted that pointer
    //     along with the rest of the C seam (System::Filename is a real class
    //     with a real destructor now), so the diagnostic has no seam to sit in
    //     and the flag is gone from src/app/options.h with it.

    // 4d. THE MENU CRASH FIX (session 5) used to install here as a pair of DLL
    //     byte patches. It now lives in the ported bodies themselves: the
    //     recursive CS is src/app/res_load_lock.cpp, taken by
    //     ExternalFactory::FindOrLoad and External::Load directly. See the
    //     banner in src/app/res_load_lock.h for why an install site was the
    //     wrong home for it.
    StallDebug::Mark("redzone/alloc-lock/diagnostic installers");

    // 5./6. (were the two DumpFirstTypeInfo probes around InvokeAllRegistrations)
    //    The exe type records are real Object::BaseTypeInfo statics linked into
    //    _firstTypeInfo at CRT init (ENDGAME B.3.9 RF, src/game/Object/exe_type.h);
    //    there is nothing to drain. Engine::Engine::Startup ->
    //    Object::Factory::Startup walks the chain and populates the typeId tree;
    //    RunGameMain dumps the whole table, order-free, right after it.

    // 7. (was SkipPlatformConfigure) The launcher dialog is no longer patched
    //    out. Engine::PlatformConfigure skips it on its own whenever
    //    VideoSettings is false, which a clean Engine::Shutdown now stores —
    //    see HarnessGuardUnattendedBoot for the whole mechanism.

    // 8. Install thread-local CALLWNDPROC hook so we can see WM_ACTIVATE /
    //    WM_KILLFOCUS / WM_SETFOCUS messages reaching the engine WindowProc.
    //    Diagnoses fullscreen focus-loss black-screen issue.
    InstallWndMsgHook();

    // 9. Latch the BANISHED_AUTOSTART env var. When set, the menu update loop
    //    auto-drives New Game -> Start (see src/diag/autostart.cpp).
    AutoStart::Init();
    StallDebug::Mark("WndMsgHook + AutoStart::Init");

    // RunStartupSelfTests() runs from game_main.cpp after Engine::Startup,
    // when the MemoryAllocator singleton is finally populated.

    printf("[Standalone] Init complete.\n");
    return true;
}

// ============================================================
// Entry point
// ============================================================
// Wave 9 lane ENG (src/engine/engine/engine_lifecycle.cpp). A linkage
// specification is not allowed at block scope, so it is declared here.
extern "C" void RtEngineLifecycleSummary();
// Wave 23 lane ENGINE. PlatformConfigure is the cutover boot wall and
// LoadResources runs on the loader thread; both are boot/load-once bodies whose
// only coverage question is "did it run, and which branch".
extern "C" void RtEnginePlatformSummary();
extern "C" void RtEngineLoadResourcesSummary();

// The attendance / counter summaries printed after GameMain returns, on the process
// exit path (each one's comment says why it is unconditional). One function so the
// Windows and POSIX entry points print the same set.
static void PrintExitSummaries() {
    // Wave 9 lane FLUSH: UI::SpriteContext::Flush's per-branch counts. A
    // `divergences=0` with no idea WHICH branches ran is the shape lane UIF's
    // own section 11.3 mis-read, so the branch split is printed next to it.
    { extern void RtUiSpriteContextFlushReport(); RtUiSpriteContextFlushReport(); }
    // Wave 9 lane ENG: did Engine::Engine's ctor / Startup / dtor / Shutdown
    // actually RUN? Each executes at most once per process, so the diff
    // harness's divergences= column says nothing on its own; this is the
    // effectiveness control. Always on -- it prints only if one of them ran.
    RtEngineLifecycleSummary();
    RtEnginePlatformSummary();
    RtEngineLoadResourcesSummary();
    // Wave 23 lane GFXBODY: attendance for the four FrameCopyImageBuffer bodies
    // and AnimationManager::Update. Always on and unconditional, because the
    // number this lane needs is the ZERO — a run that never entered a ported
    // body must SAY so rather than stay silent
    // (reference_uncalled_port_is_not_verified.md). `Generate` reports its BLIT
    // count separately from its entry count: its whole body sits behind one
    // equality test, so an entry count cannot tell you the copy happened.
    { extern void RtGraphicsWave23GfxbodySummary(); RtGraphicsWave23GfxbodySummary(); }
    // Wave 27 lane GFXPART: attendance for the six particle vtable slot bodies.
    // Unconditional for the same reason as the line above -- the number this lane
    // needs most is the ZERO, and four of its six rows dispatch only through a
    // vtable that is NOT promoted today, so a zero here is the EXPECTED reading
    // and must be printed rather than inferred from silence.
    { extern void RtGraphicsWave27GfxpartSummary(); RtGraphicsWave27GfxpartSummary(); }
    // Wave 28 lane PARTSYS. UNCONDITIONAL, so the expected zeros are PRINTED
    // rather than inferred from silence (reference_uncalled_port_is_not_verified.md).
    { extern void RtGraphicsWave28PartsysSummary(); RtGraphicsWave28PartsysSummary(); }
    // WAVE 29 LANE VERIFY — defect #7 from the Wave 28 ledger, FIXED HERE.
    //
    // These three summaries were WRITTEN, each with the standard "a run that
    // entered none of these must SAY so rather than stay silent" comment above
    // it, and NOTHING CALLED THEM. Three waves of Graphics attendance numbers
    // (Wave 24 GFXMAT 6 rows, Wave 27 GFXBIG 4 rows, Wave 28 GFXBODY 8 rows —
    // 18 registered rows, all DEFAULT-ON kill-switch waves) were therefore
    // never printed by any run, and the silence was read as attendance 0.
    //
    // A zero from an unreached printf and a zero from an unentered body are the
    // same character in a log. That is the defect: the instrument could not
    // express the difference (reference_diagnostic_field_cannot_express_the_answer.md).
    // Reachability of every *Summary/*Report/*Census in the tree is now a screen
    // of its own, recorded in docs/history/runtime_port/WAVE29_LANE_VERIFY.md §2.
    { extern void RtGraphicsWave24GfxmatSummary();  RtGraphicsWave24GfxmatSummary(); }
    { extern void RtGraphicsWave27GfxbigSummary();  RtGraphicsWave27GfxbigSummary(); }
    { extern void RtGraphicsWave28GfxbodySummary(); RtGraphicsWave28GfxbodySummary(); }
    // Wave 29 lane GFXBODY. UNCONDITIONAL, and WIRED ON PURPOSE: Wave 28 defect
    // #7 is that every lane's *Summary() is written and nothing calls it, which
    // makes a zero mean "never printed" rather than "never called" and made a
    // lane unable to read its own BRANCH counters at all. This lane's doc
    // reports that these lines were SEEN TO PRINT before any zero in them was
    // believed (reference_uncalled_port_is_not_verified.md).
    { extern void RtGraphicsWave29GfxbodySummary(); RtGraphicsWave29GfxbodySummary(); }
    // Wave 11 follow-up 3 lane AVL: ExternalFactory::SerializeReferenceTable's
    // attendance counters and its MUTATION A post-condition. Printed here, from
    // the process exit path rather than a static destructor, because an
    // instrument outside its subject's live window prints a plausible ZERO
    // rather than an error.
    { extern void RtObjectExternalRefTableSummary();      RtObjectExternalRefTableSummary(); }
}

#if defined(_WIN32)
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR lpCmdLine, int) {
    // Redirect stdout/stderr to log files.
    //
    // No AllocConsole. Retail is a GUI-subsystem process with no console, and
    // so are we (-mwindows) — a console here was purely a leftover: the two
    // freopen calls below rebind stdout/stderr to files immediately, so the
    // console never received a single byte. It also had a real cost: an extra
    // top-level window shifts the CW_USEDEFAULT cascade that places the engine
    // window at creation time, and it sat on top of the game window during the
    // pre-Present part of boot.
    // The working directory the logs below are opened in (platform_app.h: Android moves to its
    // data folder; every other platform runs where it was launched).
    Platform::App::EnterRunDirectory();
    freopen("standalone_debug.log", "w", stdout);
    freopen("standalone_error.log", "w", stderr);
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    LoadTime::Stamp("main");   // NOT RETAIL: print-only (the logs are open)

    // ENDGAME C.3: the ONE read of the environment. Every BANISHED_* flag lands
    // in the Options struct here and nothing else in standalone/ calls getenv.
    // It also expands BANISHED_STRESS=1 into its bundle (autostart, medium map,
    // uncapped, pinned camera, demo buildings, in-game quit), which is why it
    // must come before every other reader — HarnessGuardUnattendedBoot below
    // keys off the autostart flag that bundle sets. Before Cpu::Boot, because
    // the user-dir override below must be in place when Boot reads settings.cfg.
    OptionsInit();
    // BANISHED_USER_DIR (harness, tools/game_dirs.sh): the whole "<Documents>\Banished"
    // folder. The platform layer never reads Options, so the app hands it over once.
    Platform::SetUserDirOverride(Opt().userDir);

    // Block CPULIMIT (NOT RETAIL; src/platform/common/cpu/topology.h): settings.cfg's `sys.cpu_limit`
    // -> the process affinity mask, then the core topology every thread-count rule reads
    // (src/platform/common/cpu/roster.h). FIRST, before any thread of ours exists -- the crash
    // handler's, the sampler, the boot-state watchdog, SDL's, the loader, audio, the
    // pathfinder, the path workers -- and after the freopen so its [Cpu] line is logged.
    Cpu::Boot();

    // Open dedicated crash log (FILE_FLAG_WRITE_THROUGH) — survives the OS
    // tearing the crashing thread down before stderr buffers commit.
    CrashHandler_OpenLog();

    // The stress town's own state (home-loss watch, the [Stress] banner).
    StressHarness_Init();

    printf("[Standalone] Banished Reimplementation - Standalone EXE\n");
    printf("[Standalone] Build: %s %s\n", __DATE__, __TIME__);

    // Must be logged before anything creates a window — see DumpStartupInfo.
    DumpStartupInfo();

    // Boot the stall detector so subsequent [Stall] log lines are tagged with
    // milliseconds since boot. Used to hunt remaining splash-flash sources.
    StallDebug::Initialize();
    // 16ms threshold catches anything that would drop a frame at 60Hz.
    StallDebug::SetStallThresholdMs(16.0);

    // Probe for RenderDoc. If the exe was launched via RenderDoc's UI,
    // renderdoc.dll is already loaded into the process and we can drive
    // captures programmatically. No-op when not attached.
    RenderDocIntegration::Init();

    // Phase D.4 SDL B0.2: SDL3 is linked (static, third_party/SDL). Our WinMain stays the
    // entry point, so SDL is told main is ready. SDL_Init would otherwise call
    // timeBeginPeriod(1) from SDL_InitTicks (SDL_TIMER_RESOLUTION's default, "let's have
    // good sleep precision"): a process-wide clock change that neither retail nor this
    // tree makes (frame_limiter.cpp uses a high-resolution waitable timer on purpose). The
    // hint at 0 keeps the system timer as it was.
    //
    // Phase D.4 SDL W3: the video subsystem (and with it the event queue) is up from here,
    // so the game window (Application::PlatformStartup, src/platform/sdl/window_sdl.cpp)
    // and the pump (Platform::Pump) are SDL's. Everything below keeps a retail fact SDL
    // would otherwise change:
    //  * SDL_RegisterApp: the window class keeps retail's NAME "Banished", STYLE 0 and this
    //    module (Application::PlatformStartup's RegisterClassW, 0x18009fc10); SDL's own
    //    default is "SDL_app" with CS_BYTEALIGNCLIENT | CS_OWNDC. Unregistered after
    //    SDL_Quit below.
    //  * "SDL_WINDOWS_DPI_AWARENESS" = "unaware": the retail exe's manifest carries only
    //    trustInfo (asInvoker), no dpiAware element, and nothing in retail declares an
    //    awareness, so retail is DPI-UNAWARE (bitmap-stretched by Windows on a scaled
    //    display, every coordinate in 96-dpi units). SDL's default is per-monitor-v2. The
    //    hint has no SDL_HINT_ macro in 3.4.18; WIN_InitDPIAwareness reads this string.
    //  * No COM init here (Phase D.4 SDL-5 D2). Retail's main thread was MTA from
    //    Engine::Startup's CoInitializeEx(NULL, 0) at 0x1800dbc12, for its COM users
    //    (XAudio2, DirectInput 8, ShellExecuteW). None is left in this exe, so the
    //    MTA request SDL-B made here before SDL's video init is gone with Engine::Startup's.
    //    SDL's video init now makes the main thread an STA (WIN_CoInitialize asks for
    //    APARTMENTTHREADED first) and its OleInitialize succeeds; SDL balances both in
    //    SDL_Quit. SDL's own COM users initialise what they need: SDL_OpenURL around its
    //    ShellExecuteW, WASAPI on its management thread. known_port_issues row 1279: with
    //    OLE up SDL registers the window as an OLE drop target, where it used
    //    DragAcceptFiles before; retail's window accepts no drop at all.
    //  * SDL_QUIT_ON_LAST_WINDOW_CLOSE = 0: a close request reaches WindowProc as
    //    WM_CLOSE -> WM_DESTROY, whose PostQuitMessage is the one quit, as retail's.
    //  * SDL_NO_SIGNAL_HANDLERS = 1: retail installs no SIGINT / SIGTERM handler.
    //  * SDL_VIDEO_ALLOW_SCREENSAVER = 1: retail does not hold the screensaver off; SDL's
    //    video init otherwise calls SDL_DisableScreenSaver.
    //  * SDL_MOUSE_AUTO_CAPTURE = 0: retail never captures the mouse during a drag.
    SDL_SetMainReady();
    SDL_SetHintWithPriority(SDL_HINT_TIMER_RESOLUTION, "0", SDL_HINT_OVERRIDE);
    SDL_SetHintWithPriority("SDL_WINDOWS_DPI_AWARENESS", "unaware", SDL_HINT_OVERRIDE);
    SDL_SetHintWithPriority(SDL_HINT_QUIT_ON_LAST_WINDOW_CLOSE, "0", SDL_HINT_OVERRIDE);
    SDL_SetHintWithPriority(SDL_HINT_NO_SIGNAL_HANDLERS, "1", SDL_HINT_OVERRIDE);
    SDL_SetHintWithPriority(SDL_HINT_VIDEO_ALLOW_SCREENSAVER, "1", SDL_HINT_OVERRIDE);
    SDL_SetHintWithPriority(SDL_HINT_MOUSE_AUTO_CAPTURE, "0", SDL_HINT_OVERRIDE);
    if (!SDL_RegisterApp("Banished", 0, hInstance)) {
        fprintf(stderr, "[SDL] SDL_RegisterApp failed: %s\n", SDL_GetError());
    }
    // A failure is logged and boot continues: Application::PlatformStartup then reports
    // retail's "failed to register a window class" (Platform::Window::VideoAvailable).
    Platform::App::PreSdlInitHints();
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "[SDL] SDL_Init(SDL_INIT_VIDEO) failed: %s\n", SDL_GetError());
    }
    // known_port_issues row 1279: the game window is no drop target. Retail's window class /
    // CreateWindowExW (Application::PlatformStartup 0x18009fc10) carries no WS_EX_ACCEPTFILES
    // and retail never calls DragAcceptFiles or RegisterDragDrop, so a file dragged over it
    // shows the no-drop cursor. SDL enables SDL_EVENT_DROP_FILE / DROP_TEXT by default, and
    // SDL_CreateWindow's PrepareDragAndDropSupport asks the driver to accept drops whenever
    // either is enabled (SDL_video.c IsAcceptingDragAndDrop; on Windows WIN_AcceptDragAndDrop
    // then calls RegisterDragDrop, or DragAcceptFiles without OLE). Disabled here, after the
    // event queue exists and before Application::PlatformStartup creates the window, SDL
    // calls neither (a disable also revokes it from any window that already exists).
    SDL_SetEventEnabled(SDL_EVENT_DROP_FILE, false);
    SDL_SetEventEnabled(SDL_EVENT_DROP_TEXT, false);
    Platform::App::PostSdlInit();

    if (!Init()) {
        // Phase D.4 SDL W6: was MessageBoxA(nullptr, ..., MB_OK | MB_ICONERROR).
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Banished Standalone - Init Failed",
            "Failed to initialize. Check standalone_debug.log for details.", nullptr);
        Platform::App::Shutdown();
        SDL_Quit();
        SDL_UnregisterApp();
        return 1;
    }

    printf("\n[Standalone] === INIT SUCCESS ===\n");

    printf("\n[Standalone] === ENTERING GameMain ===\n");
    int rc = Game::RunGameMain(hInstance, lpCmdLine ? lpCmdLine : "");
    printf("[Standalone] === GameMain returned %d ===\n", rc);
    PrintExitSummaries();

    // Phase D.4 SDL: the matching shutdown of the SDL_Init above, then the class
    // registration (SDL_RegisterApp) it outlived.
    Platform::App::Shutdown();
    SDL_Quit();
    SDL_UnregisterApp();
    return rc;
}

#else
// ============================================================
// Entry point, POSIX (Phase D.4 LINUX-1): WinMain's sequence for a process that has no
// HINSTANCE, no command line string and no console to lose. What it keeps, in WinMain's
// order: the two log files in the working directory (unbuffered), the crash log opened
// first, the ONE read of the environment (OptionsInit), the stress / stall / RenderDoc
// set-up, SDL_SetMainReady + SDL's video subsystem with the same hints, Init(), GameMain,
// the exit summaries, SDL_Quit.
//
// What it leaves out, each Windows-only: DumpStartupInfo (STARTUPINFO's show state),
// SDL_RegisterApp / SDL_UnregisterApp (the Win32 window class "Banished"; SDL3 declares
// them on Windows only), the SDL_WINDOWS_DPI_AWARENESS and SDL_TIMER_RESOLUTION hints
// (both Windows meanings: timeBeginPeriod and the manifest's DPI awareness). (WinMain's
// COM init went with SDL-5 D2, so there is no COM step to leave out.)
//
// The command line: retail's GameMain receives WinMain's lpCmdLine, the arguments after
// the program name as one string; argv[1..] joined by single spaces is the same string for
// any argument without embedded spaces. The platform handle is null: Engine::Engine stores
// it for Application::PlatformStartup, whose window is SDL's (Phase D.4 SDL W3).
//
// Android (P12): libmain.so is built with hidden visibility (CMakeLists.txt), so `main`, the one
// symbol SDL's activity looks up by name (SDL_android.c nativeRunMain: dlsym), is exported here.
// ============================================================
#if defined(__ANDROID__)
__attribute__((visibility("default")))
#endif
int main(int argc, char** argv) {
    // The working directory the logs below are opened in (platform_app.h: Android moves to its
    // data folder; every other platform runs where it was launched).
    Platform::App::EnterRunDirectory();
    freopen("standalone_debug.log", "w", stdout);
    freopen("standalone_error.log", "w", stderr);
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    LoadTime::Stamp("main");   // NOT RETAIL: print-only (the logs are open)

    // The one environment read, then the user-dir override, before Cpu::Boot reads
    // settings.cfg: as in WinMain.
    OptionsInit();
    Platform::SetUserDirOverride(Opt().userDir);

    // Block CPULIMIT, as in WinMain. On Linux the order is load-bearing: sched_setaffinity
    // binds only the calling thread and the threads created after it.
    Cpu::Boot();

    CrashHandler_OpenLog();

    StressHarness_Init();

    printf("[Standalone] Banished Reimplementation - Standalone EXE (POSIX)\n");
    printf("[Standalone] Build: %s %s\n", __DATE__, __TIME__);

    StallDebug::Initialize();
    StallDebug::SetStallThresholdMs(16.0);

    RenderDocIntegration::Init();

    // WinMain's hints that mean the same thing here (its comment block says why each one).
    SDL_SetMainReady();
    SDL_SetHintWithPriority(SDL_HINT_QUIT_ON_LAST_WINDOW_CLOSE, "0", SDL_HINT_OVERRIDE);
    SDL_SetHintWithPriority(SDL_HINT_NO_SIGNAL_HANDLERS, "1", SDL_HINT_OVERRIDE);
    SDL_SetHintWithPriority(SDL_HINT_VIDEO_ALLOW_SCREENSAVER, "1", SDL_HINT_OVERRIDE);
    SDL_SetHintWithPriority(SDL_HINT_MOUSE_AUTO_CAPTURE, "0", SDL_HINT_OVERRIDE);
    Platform::App::PreSdlInitHints();
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "[SDL] SDL_Init(SDL_INIT_VIDEO) failed: %s\n", SDL_GetError());
    }
    // Row 1279, as in WinMain: no drop target (SDL's X11 driver then sets no XdndAware
    // property on the window; its Wayland driver marks the window as refusing drag offers).
    SDL_SetEventEnabled(SDL_EVENT_DROP_FILE, false);
    SDL_SetEventEnabled(SDL_EVENT_DROP_TEXT, false);
    Platform::App::PostSdlInit();

    if (!Init()) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Banished Standalone - Init Failed",
            "Failed to initialize. Check standalone_debug.log for details.", nullptr);
        Platform::App::Shutdown();
        SDL_Quit();
        return 1;
    }

    printf("\n[Standalone] === INIT SUCCESS ===\n");

    std::string cmdLine;
    for (int i = 1; i < argc; ++i) {
        if (i > 1) cmdLine += ' ';
        cmdLine += argv[i];
    }

    printf("\n[Standalone] === ENTERING GameMain ===\n");
    int rc = Game::RunGameMain(nullptr, cmdLine.c_str());
    printf("[Standalone] === GameMain returned %d ===\n", rc);
    PrintExitSummaries();

    Platform::App::Shutdown();
    SDL_Quit();
    return rc;
}
#endif  // _WIN32
