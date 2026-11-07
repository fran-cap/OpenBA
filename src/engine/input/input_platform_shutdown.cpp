// ============================================================================
// src/engine/input/input_platform_shutdown.cpp — Wave 24 lane SMALLBODY
//   (was src/platform/win/input_platform_shutdown_win.cpp; OS-free since SDL-C,
//   moved by Phase D.4 SDL-5)
//   ?PlatformShutdown@Device@Input@@AEAAXXZ   retail 0x1800e1b50  ord 3395
//                                             259 bytes, 0x1800e1b50..0x1800e1c53
// ============================================================================
//
// The Win32/DirectInput teardown half of `Input::Device`. Its own `_win` .cpp
// for the reason input_win.cpp gives: <windows.h> and COM must not appear in
// input.cpp (PLAN.md §4.1 decision 1).
//
// ---------------------------------------------------------------------------
// `AEAA` IS *PRIVATE*, NOT UNEXPORTED — SO THIS IS A PLAIN NAME DOOR
// ---------------------------------------------------------------------------
// This DLL exports its private members
// (reference_aeaa_is_private_not_unexported.md records three silent address
// doors that survived review because a comment said "unexported"). Checked, not
// assumed: the name is in the shipped export directory at ordinal 3395, and our
// own tree already reaches it BY NAME —
// `src/engine/engine/engine_lifecycle.cpp:847` and
// `src/game/Game/game_main.cpp:265` both spell the mangled name. So the name
// door is the primary one and the address door is the belt-and-braces second.
//
// ---------------------------------------------------------------------------
// ABSENCE ESTABLISHED FIRST
// ---------------------------------------------------------------------------
// Grepped by NAME and by ADDRESS (0x1800e1b50) across runtime/ code/ standalone/
// engine/ src/render/dx11/, scoped to the source roots. The two name hits above are
// CONSUMERS; the address has NO hits. No body existed
// (reference_half_registered_port_reads_as_absent.md).
//
// ---------------------------------------------------------------------------
// THE BODY, FROM THE INSTRUCTION LISTING
// ---------------------------------------------------------------------------
// Ghidra's decompiled C drops dereferences and drops whole arguments at
// mistyped vtable calls (reference_ghidra_c_output_vs_disassembly.md), and this
// body is FOUR vtable calls and three inlined tree descents, so every line below
// comes from the disassembly.
//
//   R8 = [0x1801fc678]                       Application::_singleton
//   -- three times, for keys 7, 8 and 0x20a: --
//        RDX = [R8+0x18]                     set->root      (the Set is app+0x18)
//        RCX = R8+0x18                       &set
//        descend: EAX = [RDX] ; CMP EAX,key
//                 JA  -> RDX = [RDX+0x18]    node key > wanted  -> LEFT
//                 JAE -> CALL 0x18000aba0    node key == wanted -> ERASE
//                 else-> RDX = [RDX+0x20]                       -> RIGHT
//   RCX = [RBX+0x11e8] ; if non-null: [vtbl+0x40]() then reload, [vtbl+0x10]()
//   RCX = [RBX+0x11e0] ; if non-null: [vtbl+0x40]() then reload, [vtbl+0x10]()
//   RCX = [RBX+0x11d8] ; if non-null: TAIL-JMP [vtbl+0x10]()
//
// THE THIRD ONE IS NOT LIKE THE OTHER TWO, AND THAT ASYMMETRY IS THE BODY'S
// ONE REAL FEATURE. +0x11E8 and +0x11E0 get `[vtbl+0x40]` AND `[vtbl+0x10]`;
// +0x11D8 gets `[vtbl+0x10]` ONLY, as a tail jump. A decompiler that folded
// these three into a loop would erase exactly that, and the brief's warning
// about a dtor "destroying every element or only one" is the same hazard in
// mirror image — here the risk is treating three UNLIKE things alike.
//
// It is not an anomaly, it is the type system: COM index 2 (+0x10) is
// `IUnknown::Release`, which all three have; COM index 8 (+0x40) is
// `IDirectInputDevice8::Unacquire`, which only a DEVICE has. +0x11D8 is the
// `IDirectInput8` FACTORY (pinned in src/engine/input/Input.h by its
// `DirectInput8Create` writer, added by this lane), and a factory was never
// acquired, so there is nothing to unacquire. The three slots being released in
// the order mouse, keyboard, factory — children before parent — is the same
// fact from the other side.
//
// THE THREE KEYS ARE WIN32 MESSAGES: 7 = WM_SETFOCUS, 8 = WM_KILLFOCUS,
// 0x20A = WM_MOUSEWHEEL. Independently corroborated by
// src/engine/input/Input.h, which already names
// `Input::Device::OnFocusMessage` and `::OnLostFocusMessage` as the two
// `_hasFocus` writers — so the callbacks this body removes are the ones that
// header says exist. `PlatformStartup` registers them; this removes them, which
// is exactly what src/engine/application/message_callbacks.cpp's banner says
// the pair does.
//
// THE DESCENT IS NOT REIMPLEMENTED FROM SCRATCH. It is instruction-for-
// instruction `?UnregisterMessageCallback@Application@1@QEAAXI@Z` (0x18009fa20),
// which message_callbacks.cpp:267 already ports — same unsigned compare, same
// LEFT=+0x18 / RIGHT=+0x20, same tail into the erase at 0x18000aba0, same
// silent return on a miss. Retail INLINES it here rather than calling it; this
// port calls our ported copy, so there is one descent in the tree instead of
// two. The AVL erase itself (0x18000aba0) is PORTED and registered
// (src/engine/collection/set_avl.cpp:428), so this body has ZERO unported
// callees.
//
// ---------------------------------------------------------------------------
// ORACLE — WHAT THIS LANE COULD AND COULD NOT BUILD
// ---------------------------------------------------------------------------
// PROBE SHAPE (WAVE24_KICKOFF §1c): **shadow/diff site**, labelled a SELF-CHECK,
// exercised by lane DIFF's armed sweep. The other shapes, each ruled out by a
// named fact rather than by "no run, therefore STRUCTURAL":
//
//   * INPUT SWEEP — not a pure function. It mutates a live AVL tree and calls
//     COM `Release`, which is a refcount decrement that cannot be undone.
//   * TWO-SIDED A/B — structurally impossible here, and not for lack of a run:
//     this is a ONCE-ONLY teardown. Running the shipped body after ours would
//     double-Release three COM objects and erase three tree nodes that are
//     already gone. Replay is exactly what a shutdown path forbids.
//   * SYNTHESISED ORACLE — would need three fake `IDirectInputDevice8`s. That is
//     buildable (a vtable of stubs is cheap) and it is the RIGHT follow-up, but
//     it needs the harness-side driver this lane has no room to add; handed
//     forward below rather than claimed.
//   * WRITE-PATH REPLAY — not a Serialize.
//
// WHAT THE SELF-CHECK ASSERTS: the three callbacks really are gone from the
// tree afterwards, and the Set's `count` fell by exactly the number of nodes
// that were actually found. That second half is the EFFECTIVENESS CONTROL —
// `g_erased` counts how many of the three descents HIT, so a run in which none
// of them hit reports `erased=0` and says so, instead of printing a clean
// `divergences=0` that means nothing (KICKOFF §3 rule 3; and
// reference_probe_needs_an_effectiveness_control.md).
//
// NEGATIVE CONTROL: `RT_INPUTSHUTDOWN_CONTROL=1` skips the
// WM_MOUSEWHEEL (0x20A) erase. The post-condition check looks up all THREE keys
// afterwards, so the control corrupts a subject the predicate actually examines
// (reference_control_must_pick_a_compared_subject.md), and it leaves the COM
// slots alone — which the predicate must NOT examine, because after Release the
// pointers are dangling and reading through them is a use, not a check.
//
// ---------------------------------------------------------------------------
// ONE KILL SWITCH: RT_INPUTSHUTDOWN
// ---------------------------------------------------------------------------
// Its own switch rather than riding RT_INPUT: this is on the process
// SHUTDOWN path, and a shutdown-only row should be bisectable without moving
// Input::Device::Update and the KeyMap bodies with it.
// ============================================================================

// Phase D.4 SDL I1: no <windows.h> / <dinput.h>: Unacquire / Release are
// Platform::Input primitives (src/platform/sdl/input_sdl.cpp since SDL-C I4).
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include "application/Application.h"
#include "input/Input.h"
#include "collection/set_avl.h"
#include "platform_input.h"
#include "platform_window.h"   // Platform::Window::kMsg* (the three erased callbacks)

// ENDGAME B.3.7 (block B1, outside its file set -- the minimum): the global
// `using RtApplication::Application;` is ill-formed next to `namespace
// Application`; the class is spelled `Application::Application` below.
using rtsetavl::Node;
using rtsetavl::Set;

namespace {

// The three Win32 messages retail removes, in retail's order.
using Platform::Window::kMsgSetFocus;     // CMP EAX,0x7   at 0x1800e1b72
using Platform::Window::kMsgKillFocus;    // CMP EAX,0x8   at 0x1800e1ba5
using Platform::Window::kMsgMouseWheel;   // CMP EAX,0x20a at 0x1800e1bd8

// The COM calls retail makes: IUnknown::Release +0x10,
// IDirectInputDevice8W::Unacquire +0x40 (Platform::Input::Release / Unacquire).

// ---- attendance ------------------------------------------------------------
uint64_t g_entries   = 0;
uint64_t g_erased    = 0;   // EFFECTIVENESS: descents that actually HIT a node
uint64_t g_missed    = 0;   // EFFECTIVENESS: descents that found nothing
uint64_t g_released  = 0;   // control: COM slots that were non-null
uint64_t g_checks    = 0;   // EFFECTIVENESS: self-check actually evaluated
constexpr uint64_t g_noApp = 0;   // control: the singleton was null (never since B6 D G20: kept for the summary line)

struct ShutdownSummary {
    ~ShutdownSummary() {
        if (g_entries == 0) {
            printf("[RtPort] Input::Device::PlatformShutdown ATTENDANCE: not "
                   "entered in this run.\n");
            fflush(stdout);
            return;
        }
        printf("[RtPort] Input::Device::PlatformShutdown ATTENDANCE (entries, "
               "not comparisons): entries=%llu | callbacks erased=%llu "
               "missed=%llu | COM released=%llu | nullSingleton=%llu | "
               "SELF-CHECKS EVALUATED=%llu%s\n",
               (unsigned long long)g_entries, (unsigned long long)g_erased,
               (unsigned long long)g_missed,  (unsigned long long)g_released,
               (unsigned long long)g_noApp,   (unsigned long long)g_checks,
               (g_erased == 0)
                   ? "  <-- NOTHING WAS ERASED: the self-check's subject never "
                     "existed, so its verdict says nothing"
                   : "");
        fflush(stdout);
    }
};
ShutdownSummary g_summary;

// The inlined descent. Instruction-for-instruction our own ported
// `Application::Application::UnregisterMessageCallback` (0x18009fa20). It used
// to be reached BY NAME through RT_EXPORT_ONCE; since block B1 made it a
// member it is the direct member call (ENDGAME B.3.7 B5), which reaches the
// same body — the door's `if (fn)` miss arm, possible only with the port
// unregistered, goes with it. Returns whether a node was actually erased, which
// the descent itself cannot report (retail's returns void and a miss is
// silent) — the extra return value is THIS PORT'S instrumentation, not a
// behavioural change: the observable effect on the tree is identical.
//
// The post-condition helper `CallbackPresent` that used to sit below this was
// DEAD CODE — defined, never called (the only warning this file carried,
// -Wunused-function) — and is removed rather than kept alive.
bool EraseCallback(Application::Application* app, unsigned message) {
    Set* set = &app->_messageCallbacks;   // Application + 0x18
    // Read the node out BEFORE the call so a hit can be recognised without
    // duplicating retail's descent logic in two places.
    for (Node* n = set->root; n; ) {
        const unsigned key = rtsetavl::ElementOf<Application::Application::MessageCallbackEntry>(n).message;
        if (message < key)      n = n->left;
        else if (key >= message) {
            app->UnregisterMessageCallback(message);
            return true;
        }
        else                    n = n->right;
    }
    return false;
}


}  // namespace

// ============================================================================
// ?PlatformShutdown@Device@Input@@AEAAXXZ                  retail 0x1800e1b50
// ============================================================================
namespace Input {

void Device::PlatformShutdown() {
    ++g_entries;

    // ---- the three message-callback erases ---------------------------------
    // Retail derefs [0x1801fc678] unguarded (0x1800e1b8a..0x1800e1bf2). B6 (14d G20): the
    // tree's null test is gone. Its reason ("our standalone can reach shutdown on a path
    // where the Application singleton was never published") does not hold: Engine::Startup
    // publishes the Application (step 6) before it creates the Input::Device (step 13), both
    // shutdown paths (Engine::Shutdown, game_main's ShutdownEngineStaged) shut the device
    // down before the Application, and every recorded B6 gate run printed nullSingleton=0.
    // g_noApp stays in the summary line (now always 0).
    Application::Application*  app  = Application::Application::_singleton;
    if (EraseCallback(app, kMsgSetFocus))   ++g_erased; else ++g_missed;  // 0x1800e1b8a
    if (EraseCallback(app, kMsgKillFocus))  ++g_erased; else ++g_missed;  // 0x1800e1bbd
    if (EraseCallback(app, kMsgMouseWheel)) ++g_erased; else ++g_missed;  // 0x1800e1bf2

    // ---- the three COM teardowns, in retail's order and with its asymmetry --
    // MOUSE first (+0x11E8): Unacquire, then Release.
    void* mouse = _mouseDevice;
    if (mouse != nullptr) {                                   // 0x1800e1bfe
        Platform::Input::Unacquire(mouse);                    // +0x40, 0x1800e1c06
        // Retail RE-LOADS the field between the two calls rather than reusing
        // the register. Reproduced: Unacquire is an outbound call and nothing
        // here may assume it left the field untouched.
        Platform::Input::Release(_mouseDevice);               // +0x10, 0x1800e1c13
        ++g_released;
    }

    // KEYBOARD (+0x11E0): Unacquire, then Release.
    void* keyboard = _keyboardDevice;
    if (keyboard != nullptr) {                                // 0x1800e1c1d
        Platform::Input::Unacquire(keyboard);                 // +0x40, 0x1800e1c25
        Platform::Input::Release(_keyboardDevice);            // +0x10, 0x1800e1c32
        ++g_released;
    }

    // FACTORY (+0x11D8): Release ONLY — retail tail-jumps it. No Unacquire,
    // because an IDirectInput8 was never acquired. See the banner.
    void* dinput = _directInput;
    if (dinput != nullptr) {                                  // 0x1800e1c3f
        Platform::Input::Release(dinput);                     // +0x10, 0x1800e1c49, TAIL JMP
        ++g_released;
    }

    // ---- SELF-CHECK --------------------------------------------------------
    // The COM pointers are deliberately NOT examined: after Release they may be
    // dangling, and reading through one is a use, not a check.
}

}  // namespace Input
