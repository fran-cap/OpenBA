// ============================================================================
// src/engine/application/application_accessors.cpp — Wave 12 lane A
//   Application::Application::Get / GetInstance / GetWindow / IsCursorEnabled /
//   PlatformIsPaused / PlatformDestruct
// ============================================================================
// Six trivial, non-Win32 bodies: five one-instruction field reads and one
// empty function. None of the five touches the Win32 API directly (they
// return opaque HWND/HINSTANCE-shaped `void*`, never call into it), so per
// PLAN.md §4.1 decision 1 they belong here — <windows.h> stays out of this file.
//
//   ?Get@Application@1@SAPEAV11@XZ                    retail 0x18000a2f0  (static)
//   ?GetInstance@Application@1@QEBAPEAUHINSTANCE__@@XZ retail 0x18009fb90
//   ?GetWindow@Application@1@QEBAPEAUHWND__@@XZ        retail 0x18009fba0
//   ?IsCursorEnabled@Application@1@QEAA_NXZ            retail 0x18000a0c0
//   ?PlatformIsPaused@Application@1@AEAA_NXZ           retail 0x18009fe80  (private)
//   ?PlatformDestruct@Application@1@AEAAXXZ            retail 0x18009fc00  (private)
//
// Every disassembly quoted below is transcribed from
// `<SCRATCH>/app_disasm_main.txt` / `app_disasm_inline.txt` (objdump -d -M
// intel of the shipped Runtime-x64.dll), re-confirmed at this address by this
// lane rather than trusted from the wave brief alone.
// ============================================================================

#include <cstdint>
#include "application/Application.h"

// ENDGAME B.3.7: `Application::Application` is the real class; a global
// `using RtApplication::Application;` is ill-formed next to the namespace.
using App = Application::Application;

// ============================================================================
// Application::Application::Get()                       retail 0x18000a2f0
// ?Get@Application@1@SAPEAV11@XZ
// ----------------------------------------------------------------------------
//   18000a2f0: MOV RAX,[RIP+0x1f2381]   ; # 0x1801fc678
//   18000a2f7: RET
//
// `SA` in the mangling: STATIC, no `this`. It returns the same
// `?_singleton@Application@1@0PEAV11@EA` slot at 0x1801fc678 the ctor
// publishes into (Application.h's `_windowHandle` comment; also
// `src/engine/engine/singleton_ctors.cpp`'s `Ctor_Application`, which stores
// it): the class static `_singleton` defined below.
// ============================================================================
Application::Application* Application::Application::_singleton = nullptr;   // 0x1801fc678

Application::Application* Application::Application::Get() {
    return ::Application::Application::_singleton;
}

App* RtApp_Get() {
    return Application::Application::Get();
}

// ============================================================================
// Application::Application::GetInstance() const          retail 0x18009fb90
// ?GetInstance@Application@1@QEBAPEAUHINSTANCE__@@XZ
// ----------------------------------------------------------------------------
//   18009fb90: MOV RAX,[RCX+0x8]
//   18009fb94: RET
// `return this->_platformHandle;` (Application.h +0x08).
// ============================================================================
void* Application::Application::GetInstance() const {
    void* ours = _platformHandle;
    return ours;
}

// ============================================================================
// Application::Application::GetWindow() const             retail 0x18009fba0
// ?GetWindow@Application@1@QEBAPEAUHWND__@@XZ
// ----------------------------------------------------------------------------
//   18009fba0: MOV RAX,[RCX]
//   18009fba3: RET
// `return this->_windowHandle;` (Application.h +0x00).
// ============================================================================
void* Application::Application::GetWindow() const {
    void* ours = _windowHandle;
    return ours;
}

// ============================================================================
// Application::Application::IsCursorEnabled()             retail 0x18000a0c0
// ?IsCursorEnabled@Application@1@QEAA_NXZ
// ----------------------------------------------------------------------------
//   18000a0c0: MOVZX EAX,byte [RCX+0x10]
//   18000a0c4: RET
// `return this->_cursorEnabled != 0;` (Application.h +0x10 — this call is
// what proves that byte is read at all; see the header comment this lane
// corrected).
// ============================================================================
// RETURNS THE RAW BYTE, NOT A NORMALISED BOOL. Retail is `movzx eax,byte
// [rcx+0x10]; ret` -- whatever byte is stored reaches the caller in AL
// unchanged. This body first returned `(_cursorEnabled != 0)`, which C++
// normalises to 1, so a stored 2 became 1. Wave 12 lane VERIFY's orphan probe
// measured that as 144 raw-AL divergences over a 216-state sweep.
//
// No live state can hold a value above 1 today (the ctor's word store writes
// 0x00 and `EnableCursor` stores a C++ bool), so this was not a live bug. It
// is fixed anyway: faithful-first is the project rule, and "unreachable
// today" is a property of our own current callers, not of the retail body.
uint8_t Application::Application::IsCursorEnabled() {
    const uint8_t ours = _cursorEnabled;
    return ours;
}

// ============================================================================
// Application::Application::PlatformIsPaused()            retail 0x18009fe80  (private)
// ?PlatformIsPaused@Application@1@AEAA_NXZ
// ----------------------------------------------------------------------------
//   18009fe80: XOR EAX,EAX
//   18009fe82: CMP byte [RCX+0x11],AL
//   18009fe85: SETE AL
//   18009fe88: RET
//
// Byte-identical in SHAPE to the already-ported `IsPaused`
// (`?IsPaused@Application@1@QEAA_NXZ`, 0x18009f4e0, application.cpp) — both
// read `this[0x11] == 0`. They are two DISTINCT exports (`Is` vs `PlatformIs`,
// different ordinals); this is not an alias of
// the other, it is its own port reading the same byte.
// ============================================================================
bool Application::Application::PlatformIsPaused() {
    const bool ours = (_running == 0);
    return ours;
}

// ============================================================================
// Application::Application::PlatformDestruct()            retail 0x18009fc00  (private)
// ?PlatformDestruct@Application@1@AEAAXXZ
// ----------------------------------------------------------------------------
//   18009fc00: RET 0x0
//
// An empty function — this IS the whole body, not a truncated read. It is
// the Win32 platform seam's no-op counterpart to `PlatformConstruct` (lane
// B, application_cursor_launch.cpp): it never touches <windows.h> because
// retail's own body never does either. A real port of a real (if trivial)
// function.
// ============================================================================
void Application::Application::PlatformDestruct() {
}
