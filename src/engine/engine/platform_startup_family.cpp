// ============================================================================
// src/engine/engine/platform_startup_family.cpp — Wave 9 lane PLAT
//   (was src/platform/win/platform_startup_win.cpp; Phase D.4 SDL-5 put its last OS call,
//   Engine::PlatformStartup's CoInitializeEx, on Platform::ComInit and moved it here)
//   The `PlatformStartup` family, and the window procedure that only exists
//   because one of them installs it.
// ============================================================================
//
// `AEAA` is PRIVATE, not unexported — Runtime-x64.dll exports its private members:
// Application::PlatformStartup is ordinal 3402 at 0x18009fc10.
//
// THE FAMILY
// ----------
// Nine exports share the name (Wave 9's survey of them:
// docs/history/runtime_port/WAVE9_LANE_PLAT.md):
//
// | Ord  | Export                                            | Retail      | Body |
// |------|---------------------------------------------------|-------------|------|
// | 3401 | ?PlatformStartup@AchievementManager@Achievement@@  | 0x18009eb70 | here (member since the B.3.7 fragment) |
// | 3402 | ?PlatformStartup@Application@1@                    | 0x18009fc10 | src/engine/application/application_platform.cpp |
// | 3403 | ?PlatformStartup@Device@Audio@@                    | 0x1800de600 | src/engine/audio/audio_platform_startup.cpp |
// | 3404 | ?PlatformStartup@Device@Input@@                    | 0x1800e1880 | src/engine/input/input_platform_startup.cpp |
// | 3405 | ?PlatformStartup@Device@Video@@                    | 0x1800f8260 | src/engine/video/device_startup.cpp |
// | 3406 | ?PlatformStartup@Engine@1@                         | 0x1800a2100 | here |
// | 3407 | ?PlatformStartup@ExternalFactory@Object@@          | 0x1800bf6c0 | here |
// | 3408 | ?PlatformStartup@MemoryAllocator@System@@          | 0x1800c7c40 | src/platform/win/allocator_win.cpp and its posix / darwin twins |
// | 3409 | ?PlatformStartup@Timer@System@@                    | 0x1800ca860 | src/engine/system/timer.cpp |
//
// Only four of the nine are on the boot path, and the path is our own
// `Engine::Startup` port (src/engine/engine/engine_lifecycle.cpp): Application
// at `g_startupFailAt == 6`, Video at 11, Audio at 12, Input at 13. Achievement,
// ExternalFactory and Engine are NOT called by anything — Ghidra's xrefs for all
// three are the export address table and nothing else, and
// Application-x64.exe's import table names none of them (checked with pefile).
// They are ported anyway because each is two or seven instructions.
//
// THE CALLING CONVENTIONS ARE READ OFF THE MANGLED NAMES, NOT GUESSED
// -------------------------------------------------------------------
//   AEAA  private instance  -> `this` in RCX          (Application, Achievement,
//                                                      ExternalFactory, Audio, Input)
//   QEAA  public  instance  -> `this` in RCX          (Engine)
//   CA    static            -> NO `this`              (Timer, and WindowProc)
//
// WIN32
// -----
// No OS call is left in this file. The IAT slots retail's bodies used, mapped
// with pefile:
//   0x18014b4a0 DefWindowProcW   0x18014b4a8 UpdateWindow   0x18014b4b0 SetCursor
//   0x18014b4b8 PostQuitMessage  0x18014b4c0 CreateWindowExW
//   0x18014b4c8 RegisterClassW   0x18014b618 CoInitializeEx
// ============================================================================

#include <cstdint>


#include "application/Application.h"
#include "engine/Engine.h"   // ENDGAME B.3.7: Engine::Engine::PlatformStartup
#include "achievement/AchievementManager.h"   // ENDGAME B.3.7 fragment: AchievementManager::PlatformStartup

// ENDGAME B.3.7: `Application::Application` is the real class; a global
// `using RtApplication::Application;` is ill-formed next to the namespace.
using App = Application::Application;

// ============================================================================
// Phase D.4 SDL W3: Application::PlatformStartup / PlatformShutdown, the WNDCLASSW
// registration and the RtApp_WindowProc thunk RegisterClassW stored are gone from here.
// The window is SDL's (src/platform/sdl/window_sdl.cpp), its class is registered with
// SDL by src/app/main.cpp (SDL_RegisterApp: retail's name "Banished" and style 0), and
// the two members are src/engine/application/application_platform.cpp, with retail's
// listings and the two Error::SetText literals. What remains below is the rest of the
// PlatformStartup family, which never touched the window.
// ============================================================================

// ============================================================================
// The three that are two instructions, and the one that is seven
// ============================================================================
// All four are quoted in full because they are short enough that a summary
// would be longer than the transcription.

// ?PlatformStartup@AchievementManager@Achievement@@AEAA_NXZ   0x18009eb70 ord 3401
//     18009eb70: MOV AL,0x1
//     18009eb72: RET
// `this` is never touched. There is no achievement backend to bring up in this
// build; the class's platform hook is a stub in the shipped DLL.
// ENDGAME B.3.7 fragment: the body is the member's; nothing calls it.
bool Achievement::AchievementManager::PlatformStartup() {
    return true;
}

// ?PlatformStartup@ExternalFactory@Object@@AEAA_NXZ           0x1800bf6c0 ord 3407
//     1800bf6c0: MOV AL,0x1
//     1800bf6c2: RET
// Consistent with `Engine::Startup`, which constructs an ExternalFactory and
// then calls NO Startup on it at all (engine_lifecycle.cpp, the 0x1800dbb47
// block).
extern "C" bool RtObject_ExternalFactory_PlatformStartup(void* /*self*/) {
    return true;
}

// ?PlatformStartup@Engine@1@QEAA_NXZ                          0x1800a2100 ord 3406
//     1800a2100: SUB RSP,0x28
//     1800a2104: XOR EDX,EDX                 ; pvReserved = NULL
//     1800a2106: XOR ECX,ECX                 ; dwCoInit   = COINIT_MULTITHREADED (0)
//     1800a2108: CALL [0x18014b618]          ; ole32!CoInitializeEx
//     1800a210e: MOV AL,0x1                  ; the HRESULT is DISCARDED
//     ...
// NOTE THE ARGUMENT ORDER. RCX is the FIRST argument (`pvReserved`) and RDX the
// second (`dwCoInit`); both are zero, so the two XORs are indistinguishable at
// the call site and the naming above is by position, not by value.
// COINIT_MULTITHREADED is 0, so this is `CoInitializeEx(NULL, COINIT_MULTITHREADED)`.
//
// `Engine::Startup` does NOT call this — it INLINES the same two-argument
// CoInitializeEx at 0x1800dbc12. Neither copy is made in the port: the
// CoInitializeEx is gone from Engine::Startup (engine_lifecycle.cpp says why), and
// what remains of this body is its `MOV AL,0x1`. Nothing calls it.
bool Engine::Engine::PlatformStartup() {
    return true;
}
