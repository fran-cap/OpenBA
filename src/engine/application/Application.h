#pragma once
// ============================================================================
// src/engine/application/Application.h — Application::Application layout
//                                            (Wave 8, lane E)
// ============================================================================
//
// Every offset below was read out of the SHIPPED Runtime-x64.dll (image base
// 0x180000000) in Ghidra, 2026-09-07. This lane does NOT own or port the
// constructor — it is not in the wave's function list — so the layout here is
// only as complete as this lane's five member functions need, and says so
// rather than inventing the rest.
//
// NOT A POLYMORPHIC CLASS. The ctor (`Application`, 0x18009f590, found via the
// xrefs to the `_singleton` data export rather than by name) is:
//
// CORRECTED (ENDGAME B.3.7, B37_RULES sec. 6.1 / B7-11): "it has no mangled
// export of its own" was WRONG. The ctor IS exported, as
// `??0Application@0@QEAA@PEAX@Z`, and the class has 27 named exports, every one
// spelled with MSVC's back-reference (`@Application@1@`, `@Application@0@`)
// because the class shares its namespace's name. A grep for `@Application@@`
// finds none of them.
//
//   MOV qword ptr [RSP+0x8],RCX
//   ...
//   LEA RAX,[RCX+0x18]
//   XOR R8D,R8D
//   MOV qword ptr [RAX],R8          ; +0x18 = 0
//   MOV dword ptr [RAX+0x8],R8D     ; +0x20 = 0
//   MOV byte ptr [RCX+0x28],R8B     ; +0x28 = 0
//   MOV qword ptr [0x1801fc678],RCX ; publish _singleton
//   MOV qword ptr [RCX],R8          ; +0x00 = 0
//   MOV qword ptr [RCX+0x8],RDX     ; +0x08 = ctor's 2nd argument
//   MOV word ptr [RCX+0x10],0x100   ; +0x10 = 0x00, +0x11 = 0x01
//
// The FIRST store is a zeroed qword at +0x00, not a `LEA` of a `??_7…6B@`
// vtable symbol — the exact test WAVE8.md §1 requires of every lane. Combined
// with the DLL exporting no `??_7Application@Application@@6B@` (147 `??_7`
// exports total, none for this class — pre-wave measurement in WAVE8.md §2),
// PLAN §4.3 risk 3 (vtable ownership) does not apply to this class.
//
// `?_singleton@Application@1@0PEAV11@EA` is at 0x1801fc678, confirmed by the
// `MOV qword ptr [0x1801fc678],RCX` store above and cross-checked against
// pefile's export directory. `engine/funcs/engine_funcs.cpp`/`.h` carry a
// STALE comment claiming this address is `0x1801feb80` — that number is not
// used by any resolve call (the real resolution goes through
// `standalone/address_registry.cpp`'s dispatch-slot registration of
// `ResolveAddr(Module::Runtime, 0x1801fc678)`, which is correct) — fixed at
// its source by this lane (PLAN §6 step 8); see WAVE8_LANE_E.md §5.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include <type_traits>
#include "collection/set_avl.h"   // B.4 R-ENG: rtsetavl::Set (_messageCallbacks), a POD header

// ############################################################################
// Application::Application -- THE REAL CLASS (ENDGAME B.3 step 7, block B1)
// ############################################################################
//
// Retail's class is `Application` inside namespace `Application` (the mangled
// `?M@Application@1@`). NON-POLYMORPHIC (no `??_7`, the ctor's stores below),
// 0x30 bytes, heap-only: `Engine::Startup` allocates it (`MemAlloc(0x30)`
// 0x1800dbbb3) and runs the ctor INLINED, which this tree ports as the door
// `Ctor_Application` (src/engine/engine/singleton_ctors.cpp) -- that door
// STAYS (B37_RULES sec. 2.4, B7-10), as do the dtor, copy-ctor and the
// unexported scalar deleting dtor (closure_batch2b.cpp). The Wave 8 POD that
// used to live here was field-identical, so the POD BECOMES the class (B7-6)
// and `RtApplication::Application` is an alias of it.
//
// `?_singleton@Application@1@0PEAV11@EA` (0x1801fc678) STAYS a data slot read
// through RT_DATA_SLOT until the end of B.3 (carry-over 5).
//
// ORDER (B7-10): Engine::Startup builds the Application BEFORE Video, and
// `PlatformStartup` (the window) runs before any `RegisterMessageCallback`
// (Input registers three callbacks, the Video device two). Nothing here moves
// that order.
//
// PORTABILITY: no <windows.h> (PLAN.md sec. 4.1 decision 1). HWND / HINSTANCE /
// WPARAM / LPARAM are spelled `void*` / `uint64_t` / `int64_t`; the _win.cpp
// files cast at the Win32 boundary.
// ############################################################################

namespace Application {

// ============================================================================
// WAVE 12 LANE B — THE CLASS EXTENT IS SETTLED. IT IS 0x30 BYTES.
// ----------------------------------------------------------------------------
// The comment above (and application.cpp's `RtApp_Quit` banner, corrected at
// its source per PLAN §6 step 8) said the object was only known to be "AT LEAST
// 0x29 bytes", because Wave 8 lane E did not own the constructor and so never
// saw an allocation. It is 0x30, from retail's own allocation site:
//
//   Engine::Startup, 0x1800dbbb3:  MemAlloc(0x30, 4)  -> the Application
//                                  then ??0Application@0@QEAA@PEAX@Z on it
//
// (transcribed in src/engine/engine/engine_lifecycle.cpp at the "0x1800dbbb3:
// Engine::Application" comment). An MSVC `new` passes sizeof, so 0x30 IS the
// size. Two further facts agree rather than merely fail to contradict:
//   * the last byte any of the eight ported members touches is +0x28, and the
//     class contains pointers, so alignof is 8 and 0x29 rounds to exactly 0x30;
//   * `??0Application@0@QEAA@AEBV00@@Z` (the copy constructor, 0x18000a0d0) and
//     `??4Application@0@` (operator=, 0x18000a160) both stop copying at +0x28.
// So the `static_assert(sizeof(...) == 0x30)` below is a real check, not a
// restatement of the field list.
//
// +0x10 AND +0x11 ARE TWO SEPARATE ONE-BYTE MEMBERS, also new from this lane.
// The constructor writes them with ONE 16-bit store (`MOV WORD PTR [RCX+0x10],
// 0x100`), which on its own is equally consistent with a single `uint16_t`
// field. The copy constructor settles it: 0x18000a0ff and 0x18000a106 copy them
// as two independent MOVZX/MOV BYTE pairs. A `uint16_t` member would have been
// copied as one word.
//
// STILL UNPROVEN, and deliberately left unnamed: +0x12..+0x17 and +0x24. No
// ported body reads or writes either range — and note that the copy constructor
// and operator= do not copy them EITHER, so those six bytes of a copied
// Application keep whatever the destination held. PLAN §6 step 8: say what is
// not known rather than guess.
// ============================================================================
class Application {
public:
    // A registered Win32 message callback:
    // `int (*)(HWND, unsigned, WPARAM, LPARAM)`; non-zero means "handled".
    using MessageCallback = int (*)(void* hwnd, uint32_t message,
                                    uint64_t wParam, int64_t lParam);

    // The 16-byte element of `_messageCallbacks` (node +0x00..+0x0f). The node
    // alloc 0x18000a5b0 copies it as two qwords; RegisterMessageCallback writes
    // `message` and `callback` only, so `pad04` carries a dword of the
    // registering caller's stack frame (message_callbacks.cpp): never compared.
    struct MessageCallbackEntry {
        uint32_t        message;   // +0x00  the key (unsigned compare)
        uint32_t        pad04;     // +0x04  never written, never read
        MessageCallback callback;  // +0x08
    };

    // ---- the 27 exports (B37_SLOTS.md sec. 6), ALL PORTED -----------------
    static Application* Get();                                      // 0x18000a2f0  application_accessors.cpp
    // ?_singleton@Application@1@0PEAV11@EA (0x1801fc678) — the class static (ENDGAME B.3.9 ENG, B39_RULES D7).
    // The namespace's *PortInit redirects the minted storage row onto it, so
    // the name door, the address door and this member are the same bytes.
    // PUBLIC while foreign readers still reach the row through the doors.
    static Application* _singleton;
    bool Startup();                                                 // 0x18009f510  application_thunks.cpp
    void Shutdown();                                                // 0x18009f500  application_thunks.cpp
    bool Update();                                                  // 0x18009f4f0  application_thunks.cpp
    void Quit();                                                    // 0x18009f4d0  application.cpp
    bool IsPaused();                                                // 0x18009f4e0  application.cpp
    bool LaunchProcess(const char* command, bool wait);             // 0x18009f5e0  application_cursor_launch.cpp
    void EnableCursor(bool enabled);                                // 0x18009f800  application_cursor_launch.cpp
    // Retail's return is `_N` but the body is `MOVZX EAX,byte [RCX+0x10]; RET`:
    // the RAW byte reaches AL. Declared `uint8_t` (the same AL return) so C++
    // does not normalise it (application_accessors.cpp's banner, Wave 12).
    uint8_t IsCursorEnabled();                                      // 0x18000a0c0  application_accessors.cpp
    void SleepThread(int32_t milliseconds);                         // 0x18009f850  application.cpp
    void YieldToThreads();                                          // 0x18009f920  application.cpp
    void RegisterMessageCallback(uint32_t message, MessageCallback callback); // 0x18009fa60  message_callbacks.cpp
    void UnregisterMessageCallback(uint32_t message);               // 0x18009fa20  message_callbacks.cpp
    void* GetInstance() const;                                      // 0x18009fb90  HINSTANCE
    void* GetWindow() const;                                        // 0x18009fba0  HWND
    Application& operator=(const Application& other);               // 0x18000a160  application_lifecycle.cpp

    // `CA` (private static) and `AEAA` (private) in retail. PUBLIC here: each
    // registered door is a free function that has to call its member, and
    // access control is not ABI (reference_aeaa_is_private_not_unexported.md).
    static int64_t WindowProc(void* hwnd, uint32_t message,
                              uint64_t wParam, int64_t lParam);     // 0x18009f930  window_proc.cpp
    void PlatformConstruct(void* platformHandle);                   // 0x18009fbe0  application_cursor_launch.cpp
    void PlatformDestruct();                                        // 0x18009fc00  application_accessors.cpp
    bool PlatformStartup();                                         // 0x18009fc10  application_platform.cpp
    void PlatformShutdown();                                        // 0x18009fd70  platform_startup_win.cpp
    bool PlatformUpdate();                                          // 0x18009fdf0  application_win.cpp
    bool PlatformIsPaused();                                        // 0x18009fe80  application_accessors.cpp

    // ---- lifetime: DOORS, declared, deliberately undefined -----------------
    // B6-10 / B7-10: the ctor is INLINED by retail's Engine::Startup and ported
    // as the door `Ctor_Application`; the dtor and copy-ctor doors are
    // `RtApp_Dtor` / `RtApp_CopyCtor` (application_lifecycle.cpp). Declaring
    // them keeps C++ from generating a member-wise copy or destructor for a
    // class that owns an AVL tree.
    explicit Application(void* platformHandle);                     // 0x18009f590  door Ctor_Application
    Application(const Application& other);                          // 0x18000a0d0  door RtApp_CopyCtor
    ~Application();                                                 // 0x18009f520  door RtApp_Dtor

    // ---- the 0x30 bytes, at retail's offsets ------------------------------
    // PUBLIC: the unconverted callers (input/input_platform_shutdown.cpp, the
    // exe's Application readers) reach them through a cast.

    // +0x00  THE MAIN WINDOW HANDLE. Named by Wave 9 lane VID2:
    // `?PlatformStartup@Device@Video@@AEAA_NXZ` opens with
    //     MOV RAX,[0x1801fc678] ; MOV RDX,[RAX] ; MOV [RCX+0x90],RDX
    // i.e. it copies `*(void**)Application::_singleton` into
    // `Video::Device::windowHandle` (+0x90), which src/engine/video/Device.h
    // already documented from the other side as "HWND, copied from
    // Application::_singleton[0] by Startup". Two independent bodies, same
    // conclusion. The ctor zeroes it; something later in bring-up fills it.
    void*   _windowHandle;     // +0x00
    // +0x08  THE PLATFORM HANDLE (HINSTANCE). The ctor's 2nd argument, stored
    // by `MOV qword ptr [RCX+0x8],RDX` above. Named by Wave 9 lane PLAT:
    // `?PlatformStartup@Application@1@AEAA_NXZ` (0x18009fc10) loads it TWICE —
    //     18009fc59: MOV RAX,[RDI+0x8]  -> WNDCLASSW::hInstance
    //     18009fcb0: MOV RAX,[RDI+0x8]  -> CreateWindowExW's hInstance argument
    // and both uses are HINSTANCE-shaped. `Engine::Startup` passes
    // `this->_platformHandle` into the ctor (engine_lifecycle.cpp:635), which
    // is the module handle WinMain received, so the two sides agree.
    //
    // Declared `void*` rather than `HINSTANCE`: PLAN §4.1 decision 1 keeps
    // <windows.h> out of every header under runtime/.
    void*   _platformHandle;   // +0x08
    // +0x10  THE CURSOR-ENABLED FLAG. Was `_stateLow`, commented "low byte of
    // the ctor's word store (0x00). Not read." The "not read" half was wrong,
    // and Wave 12 disproved it twice independently:
    //   * lane A from `?IsCursorEnabled@Application@1@QEAA_NXZ` (0x18000a0c0),
    //     which is exactly `movzx eax,byte [rcx+0x10]; ret`;
    //   * lane C from `?EnableCursor@Application@1@QEAAX_N@Z` (0x18009f800),
    //     which reads this byte before writing it (`CMP BYTE PTR [RCX+0x10],DL`)
    //     and early-outs when the value is unchanged.
    // The other half of the old comment was RIGHT about the write: the ctor's
    // word store (`MOV word ptr [RCX+0x10],0x100`) writes this byte 0x00 and
    // _running (+0x11) 0x01 in one instruction, so cursor-enabled starts FALSE.
    //
    // That single word store is also why this looked like the low half of a
    // uint16_t. It is not: lane B proved it a member in its own right from the
    // COPY CONSTRUCTOR, which copies +0x10 and +0x11 as two separate byte moves
    // (0x18000a0ff, 0x18000a106). Three lanes, three independent routes, one
    // field. Corrected at its source per PLAN.md §6 step 8, not only in a doc.
    uint8_t _cursorEnabled;    // +0x10
    uint8_t _running;          // +0x11  IsPaused: this[0x11] == 0 means PAUSED. Ctor sets 1.
    uint8_t _pad12[0x06];  // +0x12..+0x17  unproven.

    // +0x18  THE MESSAGE-CALLBACK SET. Named by Wave 9 lane VID2 from
    // `?RegisterMessageCallback@Application@1@…` (0x18009fa60), which does
    // `LEA RDI,[RCX+0x18]` and then treats RDI as a `Collection::Set` — root at
    // +0x00, count at +0x08 (`INC dword ptr [RDI+8]`, and the erase decrements
    // the same dword at 0x18000abae). `?UnregisterMessageCallback@` (0x18009fa20)
    // and `?PlatformShutdown@Device@Video@@` (0x1800f83f0) descend the same tree
    // from the same offset. That is exactly the "+0x18 (qword) and +0x20 (dword)
    // proven zeroed by the ctor" range this comment used to leave unnamed — the
    // ctor is zero-initialising an empty Set.
    //
    // The element is {unsigned message; int (*)(HWND, unsigned, WPARAM, LPARAM)}.
    // B.4 (R-ENG): typed as the `rtsetavl::Set` every reader already walks
    // (message_callbacks.cpp, application_win.cpp, platform_startup_win.cpp,
    // video/device_startup.cpp); its +0x0c pad is the old `_unknown24`.
    rtsetavl::Set _messageCallbacks;  // +0x18 root, +0x20 count, +0x24 pad
    uint8_t _quitRequested;        // +0x28  Quit() sets 1; PlatformUpdate() reads it.
};
static_assert(!std::is_polymorphic<Application>::value,
              "Application::Application has no vtable: the ctor's first stores are zeros");
static_assert(alignof(Application) == 8, "qword-aligned");
static_assert(sizeof(Application::MessageCallbackEntry) == 0x10, "message-callback element is 0x10 (two qwords)");
static_assert(offsetof(Application::MessageCallbackEntry, pad04) == 0x4, "MessageCallbackEntry::pad04");
static_assert(offsetof(Application::MessageCallbackEntry, message)  == 0x00, "element::message @ +0x00");
static_assert(offsetof(Application::MessageCallbackEntry, callback) == 0x08, "element::callback @ +0x08");
static_assert(offsetof(Application, _windowHandle)         == 0x00, "Application._windowHandle @ 0x00");
static_assert(offsetof(Application, _platformHandle)       == 0x08, "Application._platformHandle @ 0x08");
static_assert(offsetof(Application, _cursorEnabled)        == 0x10, "Application._cursorEnabled @ 0x10");
static_assert(offsetof(Application, _running)              == 0x11, "Application._running @ 0x11");
static_assert(offsetof(Application, _messageCallbacks)     == 0x18, "Application._messageCallbacks @ 0x18");
static_assert(offsetof(Application, _messageCallbacks) + offsetof(rtsetavl::Set, count) == 0x20, "Application Set count @ 0x20");
static_assert(offsetof(Application, _quitRequested)        == 0x28, "Application._quitRequested @ 0x28");
// Wave 12 lane B. Pinned by retail's own allocation at Engine::Startup
// 0x1800dbbb3 (`MemAlloc(0x30, 4)`), not by this struct's field list.
static_assert(sizeof(Application) == 0x30,
              "Application is 0x30 bytes -- Engine::Startup 0x1800dbbb3 allocates 0x30");
static_assert(offsetof(Application, _pad12) == 0x12, "Application::_pad12");

}  // namespace Application

// B7-6: one definition per layout. `RtApplication::Application` keeps compiling
// for the files that still spell it (src/engine/input/
// input_platform_shutdown.cpp, block B5's) until they rebase. NOTE: a
// GLOBAL `using RtApplication::Application;` is ill-formed now that
// `namespace Application` exists at global scope -- spell the class
// `Application::Application` instead.
namespace RtApplication { using Application = ::Application::Application; }

// ==== B3R W0-P0c: Runtime bodies reached by retail address ====
// ENDGAME B.3R (B3R_RULES R5): each body below is registered at the retail
// address in its trailing comment and read through the registry today. It is
// declared here, copied from its definition (linkage, namespace, types), so the
// reader can call it by name. Nothing calls it through this declaration yet.
namespace rtsetavl { struct Node; }   // src/engine/collection/set_avl.h

// ---- application/application_lifecycle.cpp
extern "C" void RtApp_SetDestroyTree(void* self, rtsetavl::Node* n);  // 0x18000a4f0
// B3R RT-Core: the ~Application body (??1Application@0@QEAA@XZ).
extern "C" void RtApp_Dtor(::Application::Application* self);  // 0x18009f520
// ==== end B3R W0-P0c ====
