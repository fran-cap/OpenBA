#pragma once
// ============================================================================
// src/engine/input/Device.h — Input::Device, THE REAL CLASS
//                                (ENDGAME B.3 step 7, block B5)
// ============================================================================
//
// Retail Runtime-x64.dll, image base 0x180000000. NON-POLYMORPHIC: the ctor
// `??0Device@Input@@QEAA@XZ` (0x1800e13b0) opens with three State zeroing
// loops, not a `??_7` stamp, and the DLL exports no `??_7Device@Input@@6B@`
// (B37_RULES sec. 1, B37_SLOTS.md sec. 7). 0x1200 bytes, heap-only:
// `Engine::Startup` allocates it (`MemAlloc(0x1200)` at 0x1800dbe0b) and runs
// the ctor INLINED, which this tree ports as the door `Ctor_InputDevice`
// (src/engine/engine/singleton_ctors.cpp, Engine-owned). That door STAYS
// (B37_RULES sec. 2.4): the ctor below is declared and deliberately undefined.
//
// Until block B5 there was NO struct for this class anywhere in the tree: the
// ported bodies addressed it through the `RtInput::k*Offset` constants that
// src/engine/input/Input.h carried. Those offsets MOVE here under the retail
// name (RULE B6-8); every one of them is re-asserted below, and Input.h keeps
// the `RtInput::` spellings for the callers outside this block
// (src/diag/mouse_inject.cpp, singleton_ctors.cpp) as aliases of THIS class.
//
// `?_singleton@Device@Input@@0PEAV12@EA` (0x1801fe770) STAYS a data slot read
// through RT_DATA_SLOT until the end of B.3 (carry-over 5); it is not declared
// as a class static here, exactly as Application.h does for its singleton.
//
// PORTABILITY: no <windows.h> (PLAN.md sec. 4.1 decision 1). HWND / WPARAM /
// LPARAM are spelled `void*` / `uint64_t` / `int64_t`, the COM pointers `void*`;
// the _win.cpp files cast at the Win32 boundary.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include <type_traits>

namespace System { class Time; }

namespace Input {

class Device {
public:
    // ------------------------------------------------------------------------
    // Input::Device::InputDevice — nested in retail: the mangled parameter
    // type is `W4InputDevice@12@` (back-references 1 = Device, 2 = Input).
    // The three branches every ported accessor takes pin the values.
    // ------------------------------------------------------------------------
    enum class InputDevice : int32_t {
        Joystick = 0,
        Keyboard = 1,
        Mouse    = 2,
    };

    // ------------------------------------------------------------------------
    // Input::Device::State — nested in retail (`PEAUState@12@` in
    // `?UpdateState@Device@Input@@...`, a `struct`). 0x18 bytes. A layout-only
    // value type with no export of its own (B37_RULES sec. 1): trivial, no
    // user ctor — the Device ctor and `Device::Update`'s scratch loops write
    // only +0x00..+0x07 and +0x10..+0x17 of each record, and a C++ ctor would
    // add stores retail does not make.
    //
    //   +0x00 float normalized   1800e11f0  MOVSS [RCX+R10-0x4],XMM0
    //                            == (float)value / 32767.0f
    //   +0x04 int16 value        1800e1229  MOV   word [RCX+R10],AX
    //   +0x06 bool  pressed      1800e1208  MOV   byte [RCX+R10+0x2],AL
    //   +0x07 bool  released     1800e1221  MOV   byte [RCX+R10+0x3],AL
    //   +0x08 bool  repeat       1800e123f  MOV   byte [RCX+R10+0x4],SIL  and
    //                            1800e138b  MOV   byte [RCX+R10+0x4],1
    //   +0x10 int64 holdTicks    1800e136b  ADD   qword [RCX+R10+0xc],RAX
    //
    // +0x09..+0x0F are written by nothing read so far and stay padding.
    // ------------------------------------------------------------------------
    struct State {
        float    normalized;     // +0x00 — value / 32767.0f, written by UpdateState
        int16_t  value;          // +0x04 — axis/analog value (Device::GetValue)
        bool     pressed;        // +0x06 — Device::Pressed
        bool     released;       // +0x07 — Device::Released
        bool     repeat;         // +0x08 — key-repeat pulse for this frame
        uint8_t  _unknown09[7];  // +0x09..+0x0F — never written by any body read so far
        int64_t  holdTicks;      // +0x10 — accumulated hold time, in System::Time ticks
    };

    // The three State blocks, pinned by GetValue/Pressed/Released
    // (0x1800e1660/0x1800e16c0/0x1800e1720) AND independently by ResetState
    // (0x1800e0ef0)'s and the ctor's three loops, which agree on every base
    // and count.
    static constexpr int kJoystickCount      = 4;
    static constexpr int kJoystickInputCount = 0x12;   // 18, stride 0x1B0
    static constexpr int kKeyboardInputCount = 0x69;   // 105
    static constexpr int kMouseInputCount    = 0x0D;   // 13

    // ---- the 31 exports (B37_SLOTS.md sec. 6) -------------------------------
    // Access control is not ABI (reference_aeaa_is_private_not_unexported.md):
    // retail's `AEAA`/`CA` (private) members are PUBLIC here because each
    // registered door is a free function that has to call its member.

    // 0x1800e1660 input.cpp. Joystick index is the first int (retail
    // `MOVSXD`s EDX and multiplies by 0x1b0 on the device-0 branch only).
    int16_t GetValue(InputDevice device, int32_t joystick, int32_t input) const;
    bool Pressed(InputDevice device, int32_t joystick, int32_t input) const;    // 0x1800e16c0 input.cpp
    bool Released(InputDevice device, int32_t joystick, int32_t input) const;   // 0x1800e1720 input.cpp
    void ResetState();                                                          // 0x1800e0ef0 input.cpp
    void Update(const System::Time& time);                  // 0x1800e1490 input_device_update.cpp
    // 0x1800e0fc0 input_device_update.cpp. `this` is loaded by retail and
    // never read. Retail's `src` is a non-const `State*` (`PEAUState@12@0`);
    // the body only reads it.
    void UpdateState(State* dst, State* src, int32_t count, const System::Time& time);
    bool PlatformGetKeyboardState(State* out);              // 0x1800e1c60 input_device_update.cpp
    bool PlatformGetMouseState(State* out);                 // 0x1800e1e10 input_device_update.cpp
    void PlatformUpdate();                                  // 0x1800e2130 input_platform_update.cpp
    // Retail stores DL RAW (`MOV byte [RCX+0x11d0],DL`) without normalising it
    // to 0/1, so the parameter is the byte, not `bool` — ABI-identical for a
    // one-byte `_N` argument (the IsCursorEnabled reasoning in Application.h).
    void EnableMouseClipping(uint8_t enable);               // 0x1800e1780 input_platform_update.cpp
    bool PlatformStartup();                                 // 0x1800e1880 input_platform_startup.cpp
    void PlatformShutdown();                                // 0x1800e1b50 input_platform_shutdown.cpp
    // The three message callbacks (`CA`: private static, no `this`); each
    // reaches the device through `_singleton`. The shape is
    // `Application::Application::MessageCallback`.
    static int OnMouseWheelMessage(void* hwnd, uint32_t message,
                                   uint64_t wParam, int64_t lParam);   // 0x1800e17a0
    static int OnLostFocusMessage(void* hwnd, uint32_t message,
                                  uint64_t wParam, int64_t lParam);    // 0x1800e17d0
    static int OnFocusMessage(void* hwnd, uint32_t message,
                              uint64_t wParam, int64_t lParam);        // 0x1800e1800

    // ---- declared, NOT PORTED: nothing in the tree calls these (no name, no
    // address, no exe-slot binding). Left undefined on purpose. -------------
    static Device* Get();                                   // 0x180022c40  not ported
    // ?_singleton@Device@Input@@0PEAV12@EA (0x1801fe770) — the class static (ENDGAME B.3.9 ENG, B39_RULES D7).
    // The namespace's *PortInit redirects the minted storage row onto it, so
    // the name door, the address door and this member are the same bytes.
    // PUBLIC while foreign readers still reach the row through the doors.
    static Device* _singleton;
    // EXT SEAM (NOT RETAIL, DESIGN.md hook rule 5; docs/android/P11_TOUCH_POINTER.md): called at the
    // end of Update, after every UpdateState, when set (ext.touch_pointer's pointer writer). Null by
    // default and under the faithful profile.
    static void (*_extAfterUpdate)(Device& device);
    // By-value `System::Time` return: a hidden RETSLOT export (B37_SLOTS
    // sec. 4). A door would keep `(this, ret, ...)` and placement-new it.
    System::Time PressedTime(InputDevice device, int32_t joystick, int32_t input) const; // 0x1800e0e20  not ported
    const State* GetState(InputDevice device, int32_t joystick, int32_t& input) const;  // 0x1800e0ea0  not ported
    bool Startup();                                         // 0x1800e1460  not ported
    void Shutdown();                                        // 0x1800e1470  not ported
    bool PressedOrRepeated(InputDevice device, int32_t joystick, int32_t input) const;  // 0x1800e15a0  not ported
    float GetNormalizedValue(InputDevice device, int32_t joystick, int32_t input) const; // 0x1800e1600  not ported
    void PlatformConstruct();                               // 0x1800e1830  not ported
    void PlatformDestruct();                                // 0x1800e1870  not ported
    bool PlatformGetGamepadState(int32_t pad, State* out);  // 0x1800e2120  not ported
    Device& operator=(const Device& other);                 // 0x180022ba0  not ported
    // `?_lkeyMap@Device@Input@@0PAEA` (0x1801f9480, 0x69 bytes) and
    // `?_keyMap@Device@Input@@0PAEA` (0x1801f94f0, 0x100 bytes) are LIFTED as
    // file-local literal tables in input_device_update.cpp (verbatim from
    // .data), not declared as class statics: nothing outside that file reads
    // them, and a declared-undefined static would only invite a link error.

    // ---- lifetime: DOORS, declared, deliberately undefined -----------------
    // The ctor is INLINED by retail's Engine::Startup and ported as the door
    // `Ctor_InputDevice` (singleton_ctors.cpp). `??1Device@Input@@` (0x1800e1480)
    // is not ported. Declaring them keeps C++ from generating a constructor
    // that would zero bytes retail leaves alone (+0x08..+0x0F of every State).
    Device();                                               // 0x1800e13b0  door Ctor_InputDevice
    ~Device();                                              // 0x1800e1480  not ported

    // ---- the 0x1200 bytes, at retail's offsets -----------------------------
    // PUBLIC: callers outside this block (src/diag/mouse_inject.cpp,
    // singleton_ctors.cpp, the UI element bodies) read them by offset.
    State    _joystick[kJoystickCount][kJoystickInputCount];  // +0x0000  (InputDevice 0)
    State    _keyboard[kKeyboardInputCount];                  // +0x06C0  (InputDevice 1)
    State    _mouse[kMouseInputCount];                        // +0x1098  (InputDevice 2)
    // +0x11D0, right after the mouse block. Pinned by EnableMouseClipping
    // (0x1800e1780) and the ctor's `MOV byte [RCX+0x11d0],R8B`.
    uint8_t  _mouseClipping;                                  // +0x11D0
    uint8_t  _pad11d1[7];                                     // +0x11D1..+0x11D7 — the ctor's gap
    // +0x11D8 the IDirectInput8W FACTORY — `PlatformStartup` passes
    // `LEA R9,[RCX+0x11d8]` to DINPUT8!DirectInput8Create (0x1800e18a3), then
    // calls its CreateDevice (COM +0x18) twice to fill the next two slots.
    // `PlatformShutdown` Releases it WITHOUT an Unacquire: a factory is never
    // acquired. Phase D.4 SDL I1: the three are OPAQUE platform input handles
    // (src/platform/common/platform_input.h); the Win32 backend's are the
    // IDirectInput8W / IDirectInputDevice8W retail stored here.
    void*    _directInput;                                    // +0x11D8
    void*    _keyboardDevice;                                 // +0x11E0  (1800e1d15)
    void*    _mouseDevice;                                    // +0x11E8  (1800e1f81)
    // +0x11F0: WM_KILLFOCUS clears it (OnLostFocusMessage 0x1800e17db),
    // WM_SETFOCUS sets it (OnFocusMessage 0x1800e180b); the ctor sets 1. The
    // four immediate writers are the only ones in the image.
    uint8_t  _hasFocus;                                       // +0x11F0
    uint8_t  _pad11f1[3];                                     // +0x11F1..+0x11F3
    // The legacy (GetAsyncKeyState) mouse path's three fields; the ctor zeroes
    // +0x11F4..+0x11FB with ONE unaligned qword store.
    int32_t  _wheelAccum;                                     // +0x11F4  OnMouseWheelMessage writes +-1
    int32_t  _lastCursorX;                                    // +0x11F8  (1800e1e98)
    int32_t  _lastCursorY;                                    // +0x11FC  (1800e1e9e) — the ctor's last store
};

static_assert(!std::is_polymorphic<Device>::value,
              "Input::Device has no vtable: the ctor's first stores are State zeroes");
static_assert(std::is_standard_layout<Device>::value, "reached by offset from outside");
static_assert(sizeof(Device::State) == 0x18, "State is 0x18");
static_assert(offsetof(Device::State, _unknown09) == 0x9, "State::_unknown09");
static_assert(offsetof(Device::State, normalized) == 0x00, "State.normalized @ +0x00");
static_assert(offsetof(Device::State, value)      == 0x04, "State.value @ +0x04");
static_assert(offsetof(Device::State, pressed)    == 0x06, "State.pressed @ +0x06");
static_assert(offsetof(Device::State, released)   == 0x07, "State.released @ +0x07");
static_assert(offsetof(Device::State, repeat)     == 0x08, "State.repeat @ +0x08");
static_assert(offsetof(Device::State, holdTicks)  == 0x10, "State.holdTicks @ +0x10");
static_assert(std::is_trivially_default_constructible<Device::State>::value,
              "State has no ctor: retail's loops write only 16 of its 24 bytes");

static_assert(offsetof(Device, _joystick)       == 0x0000, "Device._joystick @ +0x0000");
static_assert(sizeof(Device::State) * Device::kJoystickInputCount == 0x1B0, "joystick stride 0x1b0");
static_assert(offsetof(Device, _keyboard)       == 0x06C0, "Device._keyboard @ +0x06C0");
static_assert(offsetof(Device, _mouse)          == 0x1098, "Device._mouse @ +0x1098");
static_assert(offsetof(Device, _mouseClipping)  == 0x11D0, "Device._mouseClipping @ +0x11D0");
static_assert(offsetof(Device, _directInput)    == 0x11D8, "Device._directInput @ +0x11D8");
static_assert(offsetof(Device, _keyboardDevice) == 0x11E0, "Device._keyboardDevice @ +0x11E0");
static_assert(offsetof(Device, _mouseDevice)    == 0x11E8, "Device._mouseDevice @ +0x11E8");
static_assert(offsetof(Device, _hasFocus)       == 0x11F0, "Device._hasFocus @ +0x11F0");
static_assert(offsetof(Device, _wheelAccum)     == 0x11F4, "Device._wheelAccum @ +0x11F4");
static_assert(offsetof(Device, _lastCursorX)    == 0x11F8, "Device._lastCursorX @ +0x11F8");
static_assert(offsetof(Device, _lastCursorY)    == 0x11FC, "Device._lastCursorY @ +0x11FC");
static_assert(alignof(Device) == 8, "qword-aligned (the COM pointers)");
// B37_RULES sec. 2.3: pinned by retail's allocation, `MemAlloc(0x1200)` at
// Engine::Startup 0x1800dbe0b, not by this field list.
static_assert(sizeof(Device) == 0x1200,
              "Input::Device is 0x1200 bytes -- Engine::Startup 0x1800dbe0b allocates 0x1200");
static_assert(offsetof(Device, _pad11d1) == 0x11D1, "Device::_pad11d1");
static_assert(offsetof(Device, _pad11f1) == 0x11F1, "Device::_pad11f1");

}  // namespace Input
