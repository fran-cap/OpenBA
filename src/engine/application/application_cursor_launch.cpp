// ============================================================================
// src/engine/application/application_cursor_launch.cpp — Wave 8 lane E, Wave 12 lane C
//   (was src/platform/win/application_win.cpp: OS-free since SDL W7, moved by Phase D.4 SDL-5)
//   Application::Application::EnableCursor / LaunchProcess
//   (PlatformUpdate moved to src/engine/application/application_platform.cpp, SDL W3)
// ============================================================================
// The members of this class that touch Win32 directly, so they live behind
// the PLAN.md §4.1 decision 1 platform seam: <windows.h> must never appear in
// application.cpp (IsPaused/Quit, and since Phase D.4 THREADS E.4 SleepThread /
// YieldToThreads, over the Platform:: time primitives).
//
//   ?EnableCursor@Application@1@QEAAX_N@Z     retail 0x18009f800   ord 2292
//   ?LaunchProcess@Application@1@QEAA_NPEBD_N@Z retail 0x18009f5e0 ord 3179
//
// NONE OF THE FIRST THREE HAS AN ORACLE, and WAVE8.md §5 says so explicitly
// before any lane started: "running both pumps two message loops or sleeps
// twice."
//   * PlatformUpdate drains the live Win32 message queue with PeekMessageW(…,
//     PM_REMOVE) — a second call would eat the NEXT message, not the same one.
//   * SleepThread and YieldToThreads block/yield the calling thread; calling
//     the DLL's copy afterward would sleep or yield a second time for real.
// All three are registered anyway (, matching the
// ~SavedState precedent in src/engine/save/saveload.cpp — a site that is
// counted by `calls=` but never compared) because each is unambiguous, short,
// and reproduces retail's own Win32 call sequence exactly rather than
// approximating it.
//
// EnableCursor and LaunchProcess are Wave 12 lane C's two, and each needed a
// DIFFERENT answer to "how do you oracle a function with a real side effect,
// without performing that side effect twice":
//   * EnableCursor's own body has NO direct Win32 call at all — see its own
//     banner below — and is written against the live sink (rt_sink.h).
//   * LaunchProcess spawns a real process or opens a real shell document; a
//     predicting oracle cannot help because the state it changes (the OS
//     process table, or whatever ShellExecuteW launches) is not a memory
//     region either side could snapshot or restore. It is the diff oracle'd
//     like the three above, for the same reason: calling the DLL's copy
//     afterward would launch (or open) a SECOND time for real.
// ============================================================================

// Phase D.4 SDL W7: no <windows.h>: LaunchProcess's CreateProcessW / ShellExecuteW are
// Platform::Process primitives (src/platform/common/platform_os.h); EnableCursor and
// PlatformConstruct never made an OS call. EnableCursor's OS effect is one hop downstream,
// in the WM_SETCURSOR callback it dispatches (src/render/dx11/devicedata.cpp
// DeviceData_OnSetCursor), which SDL-C W5 put on Platform::Cursor::Set / Show (SDL3); so
// Phase D.4 SDL-5 moved this file to src/engine/application unchanged.
#include <cstdint>
#include <cstring>
#include "rt_sink.h"   // RtSink, the write sink EnableCursorBody takes
#include "application/Application.h"
#include "collection/set_avl.h"
#include "system/widestring_internal.h"
#include "system/String.h"
#include "system/WideString.h"
#include "platform_os.h"
#include "platform_window.h"   // Platform::Window::kMsgSetCursor

// ENDGAME B.3.7: `Application::Application` is the real class; a global
// `using RtApplication::Application;` is ill-formed next to the namespace.
using App = Application::Application;


// ============================================================================
// Phase D.4 SDL W3: Application::Application::PlatformUpdate (retail 0x18009fdf0, the
// PeekMessageW / TranslateMessage / DispatchMessageW loop) moved to
// src/engine/application/application_platform.cpp over Platform::Pump.
// ============================================================================

// ============================================================================
// Application::Application::EnableCursor(bool)   retail 0x18009f800   ord 2292
// ?EnableCursor@Application@1@QEAAX_N@Z
// ----------------------------------------------------------------------------
//   if (this->_cursorEnabled == enabled) return;         // 0x18009f803 JE
//   this->_cursorEnabled = enabled;                       // 0x18009f809
//   walk the message-callback Set (this+0x18, the SAME tree WindowProc
//   descends in window_proc.cpp) for key == WM_SETCURSOR (0x20);
//   if found, TAIL-CALL it as (hwnd = this->_windowHandle, msg = 0x20,
//   wParam = 0, lParam = 0).                              // 0x18009f82b..838
//
// NOT A DIRECT WIN32 CALL. There is no ShowCursor/ClipCursor/SetCursor
// anywhere in this function's own body (confirmed both from the disassembly
// and from decompile_function — the only CALL-shaped instruction is the tail
// JMP through the found node's callback pointer). The Win32 calls the wave
// brief's note pointed at happen one hop downstream, in whatever is
// registered at WM_SETCURSOR: `src/render/dx11/devicedata.cpp:1105`
// (`DeviceData_OnSetCursor`) IS that registration —
// `SetCursor(dd->cursor); ShowCursor(TRUE);` — and it ignores hwnd/msg/
// wParam/lParam entirely, so the SAME two calls fire whether `enabled` is
// going true or false. That is retail's own behaviour (the dispatch never
// reads `enabled`, only the tree), not a defect this port introduces.
//
// THE TREE DESCENT is byte-identical in shape to WindowProc's — unsigned
// compare against the target key, `< key` goes to `+0x18` (left), `> key`
// goes to `+0x20` (right), equal dispatches — just keyed on the fixed
// constant WM_SETCURSOR instead of the live message id. Verified against
// `rtsetavl::Node`'s pinned layout (set_avl.h) rather than re-derived.
//
// The body is written against the live sink (rt_sink.h); every store below
// goes through it.
// ============================================================================
namespace {

using Platform::Window::kMsgSetCursor;   // WM_SETCURSOR

// Application's message-callback element: `{unsigned message; int(*)(HWND,
// unsigned, WPARAM, LPARAM) callback;}` at node+0x00/+0x08 (Application.h).
using CursorCallbackFn = Application::Application::MessageCallback;
using CursorCallbackElement = Application::Application::MessageCallbackEntry;

// The body, written against the live sink (rt_sink.h): every store goes
// through `out` and lands for real.
void EnableCursorBody(App* self, bool enabled, RtSink& out) {
    const uint8_t prev = out.ReadU8(&self->_cursorEnabled);
    if (prev == static_cast<uint8_t>(enabled)) return;          // 0x18009f803

    out.U8(&self->_cursorEnabled, static_cast<uint8_t>(enabled),
           "Application._cursorEnabled");                        // 0x18009f809

    using rtsetavl::Node;
    Node* n = self->_messageCallbacks.root;
    const Node* found = nullptr;
    while (n != nullptr) {
        const unsigned key = rtsetavl::ElementOf<CursorCallbackElement>(n).message;
        if (kMsgSetCursor < key)      n = n->left;                // 0x18009f820
        else if (key < kMsgSetCursor) n = n->right;                // 0x18009f81a
        else { found = n; break; }                                // 0x18009f826
    }
    if (found != nullptr) {
        CursorCallbackFn cb =
            rtsetavl::ElementOf<CursorCallbackElement>(found).callback;
        if (out.Call("Application message callback @ WM_SETCURSOR",
                     reinterpret_cast<const void*>(cb),  // CAST-OK: ptr_int
                     reinterpret_cast<int64_t>(self->_windowHandle),  // CAST-OK: win32
                     static_cast<int64_t>(kMsgSetCursor), 0, 0)) {
            cb(self->_windowHandle, kMsgSetCursor, 0, 0);          // 0x18009f838
        }
    }
}

}  // namespace

void Application::Application::EnableCursor(bool enabled) {
    RtSink live;
    EnableCursorBody(this, enabled, live);
}

// ============================================================================
// Application::Application::LaunchProcess(const char*, bool)
// ?LaunchProcess@Application@1@QEAA_NPEBD_N@Z          retail 0x18009f5e0
//                                                       ord 3179
// ----------------------------------------------------------------------------
// `this` IS NEVER READ — confirmed by decompile_function, exactly like
// SleepThread/YieldToThreads (application.cpp); kept as an unused parameter for
// signature fidelity.
//
// TWO ENTIRELY DIFFERENT WIN32 PATHS, selected by `wait`. Both resolved by
// mapping each `call [rip+…]` thunk to its import-table entry with pefile
// (never inferred from the argument shape):
//
//   wait == false                                      (0x18009f612..0x18009f6bd)
//     WideString file(cmd);
//     WideString verb("open");                          // literal @ 0x180154900
//     HINSTANCE r = ShellExecuteW(NULL, verb.data, file.data, NULL, NULL,
//                                 10 /* SW_SHOWDEFAULT */);
//                                     // import slot 0x18014b488 -> SHELL32!ShellExecuteW
//     <release both WideStrings>
//     return (INT_PTR)r > 32;         // ShellExecuteW's own success threshold
//
//   wait == true                                        (0x18009f6c2..0x18009f7d4)
//     STARTUPINFOW si = {}; si.cb = sizeof(si);           // 0x68, memset then
//                                                          // one store
//     PROCESS_INFORMATION pi = {};
//     size_t len = strlen(cmd) + 1;                       // NUL included
//     char buf[260];                                      // local_128[259] +
//                                                          // local_25, MAX_PATH
//     memcpy(buf, cmd, min(len, 260));
//     if (len > 260) buf[259] = 0;                         // force-terminate a
//                                                           // truncated command
//     WideString cmdLine(buf);                             // ANSI -> UTF-16,
//                                                           // from the
//                                                           // (possibly
//                                                           // truncated) COPY,
//                                                           // never from cmd
//                                                           // itself
//     BOOL ok = CreateProcessW(NULL, cmdLine.data, NULL, NULL, FALSE, 0, NULL,
//                              NULL, &si, &pi);
//                                     // import slot 0x18014b3c8 -> KERNEL32!CreateProcessW
//     <release cmdLine>
//     if (!ok) return false;
//     WaitForSingleObject(pi.hProcess, INFINITE);
//                                     // import slot 0x18014b3c0 -> KERNEL32!WaitForSingleObject
//     CloseHandle(pi.hProcess);
//                                     // import slot 0x18014b3b8 -> KERNEL32!CloseHandle
//     return true;
//
// RETAIL LEAKS `pi.hThread` ON THE WAIT PATH. Confirmed by decompile: hThread
// is stored by CreateProcessW and never closed or read again. Faithful, not
// "fixed" — the port doesn't get to repair a leak retail always had.
//
// THE 260-BYTE CLAMP is retail's own MAX_PATH-shaped stack buffer; the
// constant is read directly off `CMP EBX,R14D` / `MOV R14D,0x104` at
// 0x18009f703/0x18009f709, not assumed from the name MAX_PATH.
//
// BOTH WideString CONSTRUCTIONS (the literal "open", `cmd` itself on the
// no-wait path, and the truncated ANSI buffer on the wait path) go through
// the SAME already-ported call target, 0x1800cb110
// (`RtSystem_WideString_CtorFromChars`, Wave 9 lane PLAT) — confirmed by
// reading the same `CALL 0x1800cb110` at all three call sites
// (0x18009f617, 0x18009f62d, 0x18009f73d). Its dtor is
// `RtSystem_WideString_Dtor`, already used by platform_startup_win.cpp in
// this same directory.
//
// THE ORACLE: NONE, and the reason is structural, not awkward — running the
// DLL's own copy afterward would spawn a SECOND real child process (wait
// path) or open a SECOND real shell document (no-wait path). Neither side
// effect lands in any memory region either implementation could snapshot or
// restore.
// the diff oracle'd like PlatformUpdate above and SleepThread/YieldToThreads (application.cpp), for
// the identical reason.
// ============================================================================
// The verb literal @ 0x180154900 is "open"; Platform::Process::Open always opens
// (SDL_OpenURL's ShellExecuteW verb is "open").

bool Application::Application::LaunchProcess(const char* cmd, bool wait) {

    // Phase D.4 SDL W7: the two Win32 calls are Platform::Process primitives over SDL
    // (src/platform/sdl/process_sdl.cpp, which records what SDL does differently). The
    // retail steps that are not OS calls stay here: the 0x104 copy / truncation and the
    // WideString conversions (UTF-8 -> UTF-16, the same decode retail ran); the UTF-16 is
    // handed back as UTF-8 (WideString::AsUTF8), which is what SDL takes.
    bool ok;
    if (wait) {
        size_t len = std::strlen(cmd) + 1;                   // 0x18009f6e9..6ff
        const size_t copyLen = len > 0x104 ? 0x104 : len;     // 0x18009f709 CMOVG

        char buf[0x104];
        std::memcpy(buf, cmd, copyLen);                       // 0x18009f71a
        if (len > 0x104) buf[0x103] = 0;                       // 0x18009f72c, truncated

        const System::WideString cmdLine(buf);                 // 0x18009f73d
        const System::String utf8 = cmdLine.AsUTF8();
        // CreateProcessW 0x18009f773 + WaitForSingleObject(INFINITE) 0x18009f7c3.
        ok = Platform::Process::RunAndWait(utf8._data);
    } else {
        const System::WideString file(cmd);                    // 0x18009f617
        const System::String utf8 = file.AsUTF8();
        // ShellExecuteW(NULL, verb "open" (kLaunchVerbOpen), file, NULL, NULL,
        // SW_SHOWDEFAULT) > 32, 0x18009f64d / 0x18009f6b7. SDL_OpenURL's verb is "open".
        ok = Platform::Process::Open(utf8._data);
    }

    return ok;
}

// ==========================================================================
// Application::Application::PlatformConstruct(void* platformHandle)
// ?PlatformConstruct@Application@1@AEAAXPEAX@Z  retail 0x18009fbe0   ord 3331
// ============================================================================
// WAVE 12 LANE B. Four instructions, and the only body in this file that makes
// no Win32 call at all — it is here because it is the `Platform*` seam by
// retail's own architecture (a `_posix` twin would have a different one), not
// because it needs <windows.h>.
//
//   18009fbe0: MOV  QWORD PTR [RCX],0x0        ; _windowHandle   = null
//   18009fbe7: MOV  QWORD PTR [RCX+0x8],RDX    ; _platformHandle = arg
//   18009fbeb: MOV  WORD  PTR [RCX+0x10],0x100 ; ONE 16-bit store: +0x10 = 0x00,
//                                              ;                   +0x11 = 0x01
//   18009fbf1: RET
//
// IT IS THE CONSTRUCTOR'S TAIL, LIFTED. `??0Application@0@QEAA@PEAX@Z`
// (0x18009f590, ported in src/engine/engine/singleton_ctors.cpp) ends with
// exactly these three stores in exactly this order — MSVC inlined the call.
// That is why `calls=` for this site is zero on every run today: nothing in our
// tree calls it, because retail's own only caller inlined it. The probe in
// application_lifecycle_probe.cpp is what executes it, and unlike the other
// four rows of this lane it gets a FULL BYTE-DIFF oracle, because the body is
// three stores and no side effects.
//
// The 16-bit store is reproduced as a 16-bit store rather than as two byte
// assignments. Single-threaded the two are indistinguishable, but this object
// is read from the window procedure, which USER32 can re-enter, and "the two
// bytes become visible together" is a property of the instruction retail chose.
// `std::memcpy` rather than a cast-and-assign: `_cursorEnabled` and `_running` are
// two distinct `uint8_t` members and writing one `uint16_t` across them through
// a reinterpret_cast is undefined behaviour GCC is entitled to reorder.
// ============================================================================
void Application::Application::PlatformConstruct(void* platformHandle) {


    _windowHandle   = nullptr;                 // 0x18009fbe0
    _platformHandle = platformHandle;          // 0x18009fbe7
    const uint16_t stateWord = 0x0100;               // 0x18009fbeb
    std::memcpy(&_cursorEnabled, &stateWord, sizeof(stateWord));
}
