// ============================================================================
// src/engine/input/input_platform_update.cpp — Wave 9 lane IOA
//   (was src/platform/win/input_win.cpp; OS-free since SDL-C, moved by Phase D.4 SDL-5)
//   Input::Device::EnableMouseClipping  +  Input::Device::PlatformUpdate
// ============================================================================
//
//   ?EnableMouseClipping@Device@Input@@QEAAX_N@Z   retail 0x1800E1780  ord 2296
//   (its only callee)  Device::PlatformUpdate      retail 0x1800E2130  ord 3411 (PRIVATE, AEAA -- exported)
//
// The platform half of Input::Device, behind PLAN.md §4.1 decision 1's platform
// seam: it reaches the OS only through Platform::Window (platform_window.h).
//
// ---------------------------------------------------------------------------
// THE STATED BLOCKER FOR THIS ROW WAS WRONG, AND THIS IS THE CORRECTION
// ---------------------------------------------------------------------------
// WAVE8_LANE_D.md §"?EnableMouseClipping@Device@Input@@QEAAX_N@Z" holds it back
// like this:
//
//     "One field write (this[0x11D0] = param) followed by a call to the same
//      unexported PlatformUpdate Update depends on. Porting the field write
//      alone would silently drop the Win32 clip-rect side effect the DLL's
//      call actually produces; porting the whole thing needs the same
//      unresolved internal helper Update does. Held back for the same reason,
//      not a separate one."
//
// The first two sentences are exactly right. The third is false, and it is
// false because `PlatformUpdate` was never disassembled — it was assumed to
// share `Update`'s problem because `Update` also ends in a tail-jump to it.
// Its whole body is 0xB8 bytes (0x1800E2130..0x1800E21E8) and it contains:
//
//   * NO DirectInput8 anything. No COM pointer, no `IDirectInputDevice8`
//     vtable call, nothing past +0x11F0.
//   * NO unexported Runtime callee. Not one `CALL <rva>`.
//   * FOUR calls, all of them USER32 imports, resolved from the DLL's own
//     import table with pefile: GetClientRect (0x18014B558), ClientToScreen
//     (0x18014B530, twice) and ClipCursor (0x18014B528).
//   * ONE Runtime data read: `Application::Application::_singleton`
//     (0x1801FC678), whose +0x00 our tree documents as the HWND
//     (`Application::_windowHandle`, Application.h) — src/engine/video/Device.h:619
//     says "+0x90 HWND, copied from Application::_singleton[0] by Startup".
//
// So the helper `Update` and `EnableMouseClipping` share is satisfied on our
// side; `Update`'s other three helpers (PlatformGetKeyboardState 0x1800E1C60,
// UpdateState 0x1800E0FC0, PlatformGetMouseState 0x1800E1E10) are ported in
// input_device_update.cpp.
//
// ============================================================================

// Phase D.4 SDL W1: GetClientRect / ClientToScreen / ClipCursor moved behind
// Platform::Window, arguments unchanged; this TU no longer includes <windows.h>.
// Since SDL-C (I3) the clip is SDL_SetWindowMouseRect (src/platform/sdl/input_sdl.cpp)
// and the rect reads are SDL's (src/platform/sdl/display_sdl.cpp).
#include <cstdint>
#include "application/Application.h"   // ENDGAME B.3.9 ENG: the class static
#include "input/Input.h"
#include "platform_window.h"

// ENDGAME B.3.7 B5: `Device::Update` (input_device_update.cpp) ends in this
// body exactly as retail's `JMP 0x1800E2130` does, through the MEMBER call
// `PlatformUpdate()`. The extern "C" forwarder `RtInput_Device_PlatformUpdate`
// that used to carry that call is gone (its only caller was Update).

namespace Input {

// ---------------------------------------------------------------------------
// Input::Device::PlatformUpdate — retail 0x1800E2130, EXPORTED as the private
// `?PlatformUpdate@Device@Input@@AEAAXXZ` (ordinal 3411). Its callers are
// EnableMouseClipping, Update, PlatformStartup and OnFocusMessage / OnLostFocusMessage.
//
// Retail, instruction for instruction:
//
//   1800e2134  CMP byte ptr [RCX+0x11d0],0   ; _mouseClipping
//   1800e213b  JZ  -> 1800e21dc
//   1800e2141  CMP byte ptr [RCX+0x11f0],0   ; _hasFocus
//   1800e2148  JZ  -> 1800e21dc
//   1800e214e  MOV RAX,[0x1801fc678]         ; Application::_singleton
//   1800e215f  MOV RBX,[RAX]                 ; app->hwnd   (+0x00)
//   1800e2165  CALL [GetClientRect]  (hwnd, RSP+0x20)
//   1800e216b  copy the 4 dwords RSP+0x20 -> RSP+0x40
//   1800e2193  CALL [ClientToScreen] (hwnd, RSP+0x40)   ; the (left,top) corner
//   1800e21a1  CALL [ClientToScreen] (hwnd, RSP+0x48)   ; the (right,bottom) corner
//   1800e21a7  copy back: [0x20]=[0x40] [0x28]=[0x48] [0x24]=[0x44] [0x2c]=[0x4c]
//   1800e21cc  CALL [ClipCursor] (RSP+0x20)
//   1800e21dc  XOR ECX,ECX ; JMP [ClipCursor]           ; ClipCursor(NULL)
//
// The interleaved copy at 0x1800E21A7 is an IDENTITY copy — left<-left,
// right<-right, top<-top, bottom<-bottom — the compiler's own scheduling of
// two POINTs back into one RECT, not a swap. It is written out below in the
// obvious form rather than reproduced as four dword moves, because the two
// are the same bytes in the RECT either way and the shuffled spelling reads as
// a bug it is not.
//
// The `*(HWND*)app` deref is UNGUARDED here because it is unguarded in retail.
// CLAUDE.md's rule applies: retail relies on `Application::_singleton` being
// published (its ctor is what publishes it) and on +0x00 holding the window by
// the time any focus message or mouse-clipping toggle can arrive. A null check
// would not restore the behaviour, it would skip it. If this ever faults, the
// upstream fact to fix is that our Application was never given its HWND.
void Device::PlatformUpdate() {

    const bool clipping = _mouseClipping != 0;
    const bool focused  = _hasFocus != 0;

    if (!clipping || !focused) {
        Platform::Window::ClipCursor(nullptr);
        return;
    }

    void* const hwnd = ::Application::Application::_singleton->_windowHandle;   // Application +0x00

    // Platform::Rect is a RECT and Platform::Point a POINT (window_hook_win.cpp asserts both),
    // so each corner of the rect is the POINT retail hands ClientToScreen.
    Platform::Rect rc;
    Platform::Window::GetClientRect(hwnd, &rc);
    Platform::Window::ClientToScreen(hwnd, reinterpret_cast<Platform::Point*>(&rc.left));    // (left, top)  CAST-OK: win32
    Platform::Window::ClientToScreen(hwnd, reinterpret_cast<Platform::Point*>(&rc.right));   // (right, bottom)  CAST-OK: win32
    Platform::Window::ClipCursor(&rc);
}

// ---------------------------------------------------------------------------
// Input::Device::EnableMouseClipping — retail 0x1800E1780, ord 2296. Nine
// bytes:
//
//   1800e1780  MOV byte ptr [RCX+0x11d0],DL
//   1800e1786  JMP 0x1800e2130                 ; tail call PlatformUpdate
//
// The parameter is taken as `uint8_t`, not `bool`, on purpose. Retail stores
// DL RAW — whatever byte the caller left in the register — and does not
// normalise it to 0/1. The two spellings are ABI-identical in the Microsoft
// x64 convention (a `_N` argument is one byte in DL either way), so a caller
// holding a `void(*)(void*, bool)` typedef reaches this unchanged, and the
// raw-byte store stays faithful.
// ---------------------------------------------------------------------------
void Device::EnableMouseClipping(uint8_t enable) {
    _mouseClipping = enable;
    PlatformUpdate();                            // retail: JMP 0x1800e2130
}

}  // namespace Input
