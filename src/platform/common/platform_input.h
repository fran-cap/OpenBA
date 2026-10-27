#pragma once
// ============================================================================
// src/platform/common/platform_input.h -- Platform::Input, the keyboard / mouse seam
// ============================================================================
// Phase D.4 (D4_RULES C1, SDL plan §A.4, step I1). Free functions, no OS header, no SDL
// header. Input::Device (src/engine/input/) keeps retail's two input paths and calls
// these instead of DirectInput 8 / user32:
//
//  * the DirectInput path (Input::Configuration::_legacy == 0, the default): a keyboard
//    read as 256 scan-code bytes (the DIK_* codes) and a mouse read as a DIMOUSESTATE2
//    record (relative x / y / wheel, 8 buttons);
//  * the legacy path (_legacy != 0, persisted `Input/Legacy`): per-key async state by
//    virtual-key code, the cursor position and the window-has-the-foreground test.
//
// Implementation: src/platform/sdl/input_sdl.cpp (SDL3; steps I2 keyboard, I3 mouse, I4
// device lifetime). Step I1 put one DirectInput 8 / user32 call behind each primitive
// (src/platform/win/input_dinput_win.cpp; deleted by SDL-5 with its layout asserts).
//
// Device handles are opaque (`void*`), kept by Input::Device at retail's offsets (+0x11D8
// / +0x11E0 / +0x11E8): DirectInput's interfaces under the I1 backend, null under SDL,
// which owns its devices. Results are DirectInput's HRESULT values (kOk, kInputLost,
// kNotAcquired): the pollers' re-acquire loop tests them, as retail's does.
// ============================================================================

#include <cstdint>

#include "platform_window.h"   // Platform::Point

namespace Platform {
namespace Input {

constexpr int32_t kOk          = 0;                                  // DI_OK
constexpr int32_t kInputLost   = static_cast<int32_t>(0x8007001Eu);  // DIERR_INPUTLOST
constexpr int32_t kNotAcquired = static_cast<int32_t>(0x8007000Cu);  // DIERR_NOTACQUIRED

// DIMOUSESTATE2: relative x / y / wheel since the last read, then 8 buttons, each down
// when bit 7 is set. 20 bytes, DIMOUSESTATE2's layout (c_dfDIMouse2), which retail's
// poller was written against; the size is asserted below.
struct MouseState {
    int32_t x;
    int32_t y;
    int32_t z;
    uint8_t buttons[8];
};
static_assert(sizeof(MouseState) == 20, "DIMOUSESTATE2: lX, lY, lZ, rgbButtons[8]");

// ---------------------------------------------------------------------------
// Device lifetime.
// ---------------------------------------------------------------------------
// Create the backend's factory, keyboard and mouse for `window`, in retail's order
// (Input::Device::PlatformStartup 0x1800e1880): the factory, the keyboard with its data
// format and cooperative level, the mouse likewise. Returns nullptr on success, else the
// retail Error::SetText text of the step that failed; whatever was created before it is
// left in the out-parameters and NOT released (retail leaks it; Engine::Startup ends the
// process on that path).
const char* CreateDevices(void* window, void** factory, void** keyboard, void** mouse);
// IDirectInputDevice8::Acquire / Unacquire.
int32_t Acquire(void* device);
int32_t Unacquire(void* device);
// IUnknown::Release of a device or of the factory.
void Release(void* object);

// ---------------------------------------------------------------------------
// The poll (SDL step I2). Input::Device::Update calls it first: DirectInput and
// GetAsyncKeyState are live when retail's pollers run, SDL's state is as fresh as its
// last event pump (src/platform/sdl/input_sdl.cpp).
// ---------------------------------------------------------------------------
void Poll();

// ---------------------------------------------------------------------------
// The DirectInput path's two reads (IDirectInputDevice8::GetDeviceState).
// ---------------------------------------------------------------------------
// All 256 scan-code bytes (DIK_* index, bit 7 = down). On failure `dik` is not written:
// kNotAcquired while the game window is not the foreground window (DISCL_FOREGROUND).
int32_t ReadKeyboard(void* device, uint8_t dik[256]);
// The mouse record. On failure `out` is not written.
int32_t ReadMouse(void* device, MouseState* out);

// ---------------------------------------------------------------------------
// The legacy path, and the cursor position both paths read.
// ---------------------------------------------------------------------------
// GetForegroundWindow() == window.
bool IsForeground(void* window);
// GetAsyncKeyState(vk) for a keyboard key (bit 15 = down now).
int16_t AsyncKeyState(int32_t vk);
// GetAsyncKeyState(vk) for a mouse button (VK_LBUTTON 1, VK_RBUTTON 2, VK_MBUTTON 4).
int16_t AsyncMouseButtonState(int32_t vk);
// GetCursorPos: the cursor in screen coordinates.
void GetCursorPos(Point* out);
// ScreenToClient, in place.
void ScreenToClient(void* window, Point* inOut);
// GetSystemMetrics(SM_SWAPBUTTON) != 0: the pollers swap buttons 0 and 1 when true.
bool ButtonsSwapped();

// ---------------------------------------------------------------------------
// The launcher's Shift test (Engine::PlatformConfigure 0x1800a1670; Phase D.4 SDL-5).
// ---------------------------------------------------------------------------
// Either Shift key held down RIGHT NOW: GetAsyncKeyState(VK_LSHIFT) & 0x8000 ||
// GetAsyncKeyState(VK_RSHIFT) & 0x8000 (0x1800a16f7 / 0x1800a170e; the "down now" bit, not
// bit 0). It runs inside Engine::Startup, after the window exists but before it is shown
// or has ever had the keyboard focus. That is why this is NOT a Poll / AsyncKeyState
// reader over SDL: SDL's keyboard state comes only from key events delivered to its
// focused window and is never seeded from the physical keys (on Windows SDL only CLEARS a
// stuck Shift with GetKeyState), so SDL answers "not held" for every launch.
// Bodies: src/platform/win/input_win.cpp (Win32, retail's two calls) and
// src/platform/posix/input_posix.cpp (SDL_GetModState after a pump: false at launch,
// known_port_issues row 1278).
bool ShiftHeld();

// ---------------------------------------------------------------------------
// NOT RETAIL (P11, docs/android/P11_PHONE_BUGS.md §3): which device moved the pointer last.
// ---------------------------------------------------------------------------
// True when the last mouse motion or button event the pump read was synthesised from a touch
// (SDL's SDL_TOUCH_MOUSEID: a finger on a touch screen), false for a real mouse, a pen or before
// any pointer event. The mouse the game reads (Input::Device) does not tell them apart.
// Body: src/platform/sdl/pump_sdl.cpp (every platform pumps through SDL). Main thread.
bool PointerIsTouch();

}  // namespace Input
}  // namespace Platform
