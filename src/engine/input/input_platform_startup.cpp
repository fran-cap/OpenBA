// ============================================================================
// src/engine/input/input_platform_startup.cpp — Wave 9 lane PLAT
//   (was src/platform/win/input_platform_startup_win.cpp; OS-free since SDL-C,
//   moved by Phase D.4 SDL-5)
//   ?PlatformStartup@Device@Input@@AEAA_NXZ   retail 0x1800e1880  ord 3404
//   and the three exported message callbacks it installs.
// ============================================================================
//
// The LAST of the four `PlatformStartup` members our ported `Engine::Startup`
// calls: engine_lifecycle.cpp:668, `g_startupFailAt == 13`. `AEAA` is PRIVATE,
// not unexported; every name in this file was read out of the shipped export
// directory with pefile before a body was written:
//
//   | Export                                                | Retail      | Ord  |
//   |-------------------------------------------------------|-------------|------|
//   | ?PlatformStartup@Device@Input@@AEAA_NXZ                | 0x1800e1880 | 3404 |
//   | ?OnFocusMessage@Device@Input@@CAHPEAUHWND__@@I_K_J@Z   | 0x1800e1800 | 3275 |
//   | ?OnLostFocusMessage@Device@Input@@CAHPEAUHWND__@@I_K_J@Z | 0x1800e17d0 | 3276 |
//   | ?OnMouseWheelMessage@Device@Input@@CAHPEAUHWND__@@I_K_J@Z | 0x1800e17a0 | 3277 |
//   | ?PlatformUpdate@Device@Input@@AEAAXXZ                  | 0x1800e2130 | 3411 |
//
// PHASE D.4 SDL I1: THE DIRECTINPUT CODE MOVED OUT
// ------------------------------------------------
// The DirectInput 8 block of PlatformStartup (DirectInput8Create, the two CreateDevice /
// SetDataFormat / SetCooperativeLevel triples and their seven Error::SetText texts), the
// tail's two Acquire calls and the cursor read are Platform::Input primitives now
// (src/platform/common/platform_input.h; Win32 bodies, the HINSTANCE substitution and the
// data-format notes were in src/platform/win/input_dinput_win.cpp, deleted by SDL-5
// with the DirectInput backend; since SDL-C I4 the primitives are SDL's,
// src/platform/sdl/input_sdl.cpp). This file has no OS
// header; the retail control flow, the three configuration reads and the callback
// registration are unchanged.
// ============================================================================

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "input/Device.h"   // ENDGAME B.3.9 ENG: the class static
#include "application/Application.h"
#include "input/Input.h"
#include "system/Error.h"
#include "platform_input.h"
#include "platform_window.h"

namespace {


// `?SetText@Error@System@@SAXPEBD_N@Z` 0x1800c5fa0, called by name (B3R).
void ErrorSetText(const char* text, bool flag) {
    System::Error::SetText(text, flag);
}

// ENDGAME B.3.7 B5: two by-NAME doors of this file became member calls.
// `?PlatformUpdate@Device@Input@@AEAAXXZ` (RT_EXPORT_ONCE, input_win.cpp's
// body) is now `Device::PlatformUpdate()`, and
// `?RegisterMessageCallback@Application@1@...` (RT_EXPORT_ONCE, block B1's
// message_callbacks.cpp body) is now
// `Application::Application::RegisterMessageCallback`. Both doors resolved to
// the same bodies the member calls reach; their `if (fn)` miss arms, which
// could only fire with the port unregistered, go with them.


// `Input::Configuration` +0x08 / +0x09 are the only two bytes this body reads
// of it: +0x09 (`_legacy`) is the "skip DirectInput entirely" flag
// (`CMP byte [RAX+9],0` at 0x1800e1899 jumps past ALL device creation) and
// +0x08 (`_mouseClipping`) seeds the device's mouse-clipping flag at
// 0x1800e1ac4. The device's +0x11D8 is `Device::_directInput`
// (`LEA R9,[RCX+0x11d8]` at 0x1800e18a3).

// Phase D.4 SDL I1: the cooperative level (DISCL_FOREGROUND | DISCL_NONEXCLUSIVE, the
// literal 6) and the COM slots retail calls were input_dinput_win.cpp's (deleted by
// SDL-5; the I1 bodies are in that file's history, cac6ce3b7).

}  // namespace

// ============================================================================
// The three static message callbacks, ordinals 3275-3277.
// ============================================================================
// All three are `CA` — static, no `this` — and all three reach the device
// through `?_singleton@Device@Input@@0PEAV12@EA` (0x1801fe770) rather than
// through a captured pointer. All three return 0, i.e. "not handled": the
// WindowProc's caller then falls through to its own switch and on to
// DefWindowProcW, which is why WM_SETFOCUS still reaches Application's own
// focus bookkeeping.

namespace Input {

// Phase D.4 SDL I4: these fire from Platform::Pump's WM_SETFOCUS / WM_KILLFOCUS tuples
// (on Windows the real messages, forwarded by src/platform/win/window_hook_win.cpp),
// in Win32's order after WM_ACTIVATE; PlatformUpdate's clip is SDL_SetWindowMouseRect.
//
// 0x1800e1800: `_singleton->_hasFocus = 1; PlatformUpdate(_singleton); return 0;`
// Note it re-loads the singleton for the second use (0x1800e1812) instead of
// keeping the first in a register. Kept.
int Device::OnFocusMessage(void*, uint32_t, uint64_t, int64_t) {
    Device* dev = Device::_singleton;
    dev->_hasFocus = 1;                            // 0x1800e180b, unguarded in retail
    Device* dev2 = Device::_singleton;             // 0x1800e1812, retail re-reads
    dev2->PlatformUpdate();
    return 0;
}

// 0x1800e17d0: the same, with 0.
int Device::OnLostFocusMessage(void*, uint32_t, uint64_t, int64_t) {
    Device* dev = Device::_singleton;
    dev->_hasFocus = 0;                            // 0x1800e17db
    Device* dev2 = Device::_singleton;
    dev2->PlatformUpdate();
    return 0;
}

// 0x1800e17a0: the wheel. Nine instructions and no branch:
//     OR EAX,-1 ; SHR R8,0x10 ; MOV ECX,1 ; TEST R8W,R8W ; CMOVS ECX,EAX
//     MOV [singleton+0x11f4],ECX ; XOR EAX,EAX ; RET
// So it stores the SIGN of the wheel delta as -1 or +1 — NOT the delta itself,
// and NOT an accumulation. `TEST R8W,R8W` is a 16-bit test of the wParam's high
// word after the shift, i.e. the signed WHEEL_DELTA multiple, and CMOVS picks
// -1 only when that 16-bit value is negative. A 32-bit test here would read the
// wParam's upper half as part of the sign and be wrong for any wParam with bit
// 31 set.
int Device::OnMouseWheelMessage(void*, uint32_t, uint64_t wParam, int64_t) {
    const int16_t delta = static_cast<int16_t>((wParam >> 16) & 0xFFFF);
    const int32_t sign = (delta < 0) ? -1 : 1;
    Device* dev = Device::_singleton;
    dev->_wheelAccum = sign;
    return 0;
}

}  // namespace Input

// ============================================================================
// ?PlatformStartup@Device@Input@@AEAA_NXZ              retail 0x1800e1880
// ============================================================================
// Retail's shape:
//
//   if (Configuration::_singleton->_legacy) goto tail;          // 0x1800e1899
//   DirectInput8Create(hinst, 0x800, IID_IDirectInput8W, &_di8, NULL);
//   ... seven HRESULT checks, each with its own Error::SetText and `return false`
//   ...
//   tail:
//   RegisterMessageCallback(app, WM_SETFOCUS,   OnFocusMessage);
//   RegisterMessageCallback(app, WM_KILLFOCUS,  OnLostFocusMessage);
//   RegisterMessageCallback(app, WM_MOUSEWHEEL, OnMouseWheelMessage);
//   _mouseClipping = Configuration::_singleton->_mouseClipping;
//   PlatformUpdate(this);
//   GetCursorPos(&p); ScreenToClient(app->_windowHandle, &p);
//   _lastCursorX = p.x; _lastCursorY = p.y;
//   if (!Configuration::_singleton->_legacy) { keyboard->Acquire(); mouse->Acquire(); }
//   return true;
//
// FOUR DETAILS WORTH THE INK
// --------------------------
//  1. EVERY HRESULT TEST IS `TEST EAX,EAX ; JZ`, an EQUALITY test against S_OK,
//     not `JS`. A DirectInput call that succeeded with a non-zero success code
//     would take the failure branch. Reproduced.
//  2. The `_legacy` flag is re-read from the singleton at the END
//     (0x1800e1b0a) rather than reused from the entry test, so a value that
//     changed in between changes which Acquire branch runs. Kept as two reads.
//  3. The tail runs even when DirectInput was skipped entirely — the message
//     callbacks, the clip flag and the cursor origin are installed either way.
//     That is what makes the skip path usable rather than a stub.
//  4. On EVERY failure path the interfaces created so far are LEAKED: retail
//     Releases nothing before returning false. Faithful; `Engine::Startup`
//     ends the process on that path anyway.
//
// ============================================================================
namespace Input {

bool Device::PlatformStartup() {

    Configuration* cfg = Configuration::_singleton;
    Application::Application* app = Application::Application::_singleton;

    // 0x1800e1899 — unguarded in retail, and a null here is an upstream fault
    // to find rather than one to paper over.
    const bool skipDirectInput = cfg->_legacy != 0;


    if (!skipDirectInput) {
        // 0x1800e18a3..0x1800e1a55: DirectInput8Create and the six device calls, each
        // with its own Error::SetText and `return false` (Platform::Input::CreateDevices
        // returns the text of the step that failed; what was created is leaked, as retail).
        // The window handle comes from Application::_singleton[0] (0x1800e196e MOV
        // RDX,[RAX], no test: B6 14d G18).
        const char* failed = Platform::Input::CreateDevices(app->_windowHandle, &_directInput,
                                                            &_keyboardDevice, &_mouseDevice);
        if (failed != nullptr) {
            ErrorSetText(failed, false);
            return false;
        }
    }

    // ---- the tail at 0x1800e1a75, reached both ways --------------------------
    app->RegisterMessageCallback(Platform::Window::kMsgSetFocus,   &Device::OnFocusMessage);
    app->RegisterMessageCallback(Platform::Window::kMsgKillFocus,  &Device::OnLostFocusMessage);
    app->RegisterMessageCallback(Platform::Window::kMsgMouseWheel, &Device::OnMouseWheelMessage);

    // 0x1800e1abd — retail RE-READS the configuration singleton here.
    Configuration* cfg2 = Configuration::_singleton;
    _mouseClipping = static_cast<uint8_t>(cfg2->_mouseClipping);

    PlatformUpdate();                                            // 0x1800e1ad1

    Platform::Point p;
    Platform::Input::GetCursorPos(&p);                           // 0x1800e1adb
    Platform::Input::ScreenToClient(app->_windowHandle, &p);     // 0x1800e1ae1 / 0x1800e1aed, no test
    _lastCursorX = p.x;
    _lastCursorY = p.y;

    // 0x1800e1b0a — the THIRD read of the configuration singleton, and it is a
    // separate read in retail rather than the entry value reused.
    Configuration* cfg3 = Configuration::_singleton;
    if (cfg3->_legacy == 0) {
        Platform::Input::Acquire(_keyboardDevice);               // +0x38, 0x1800e1b21
        Platform::Input::Acquire(_mouseDevice);                  // +0x38, 0x1800e1b2e
    }

    return true;                                                 // 0x1800e1b3b
}

}  // namespace Input
