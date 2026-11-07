// src/diag/boot_state_watchdog.h
// Publishes a one-line, machine-readable boot state to standalone_boot_state.txt so an
// automated run can distinguish "blocked on a modal dialog" from "still loading" from
// "frozen". See boot_state_watchdog.cpp for the full rationale, and tools/boot_state.sh
// for the reader an agent should call.

#pragma once

#include <cstdint>

namespace BootState {

// The states a run publishes (STATE= in standalone_boot_state.txt, the [BootState] stderr lines).
// Not retail: the numbers are this file's own. The SPELLINGS are the external contract that
// tools/boot_state.sh, tools/android_boot_state.sh and agents' greps parse, so BootStateName must
// keep them byte for byte. Shared by the four platform twins (src/diag/<platform>/).
enum class BootStateId : uint8_t {
    None          = 0,   // no state published yet (the watchdog loop's first lastState)
    Booting       = 1,   // Start() until MarkStartupComplete()
    Running       = 2,   // Engine::Startup returned (MarkStartupComplete)
    Quitting      = 3,   // MainLoop::Run returned (BootState_MarkQuitting): the teardown budget
    Hung          = 4,   // the frame counter stalled past the phase's budget
    BlockedDialog = 5,   // Windows only: a modal Win32 dialog (#32770) is open
    Exited        = 6,   // Stop() on a clean shutdown
    NoGameData    = 7,   // src/app/main.cpp: the game-data check stopped the boot
};

// The published spelling of a state. A constant table lookup: async-signal-safe for the posix /
// android hang handlers.
constexpr const char* BootStateName(BootStateId id) {
    switch (id) {
    case BootStateId::Booting:
        return "BOOTING";
    case BootStateId::Running:
        return "RUNNING";
    case BootStateId::Quitting:
        return "QUITTING";
    case BootStateId::Hung:
        return "HUNG";
    case BootStateId::BlockedDialog:
        return "BLOCKED_DIALOG";
    case BootStateId::Exited:
        return "EXITED";
    case BootStateId::NoGameData:
        return "NO_GAME_DATA";
    case BootStateId::None:
        break;
    }
    return "";
}

// Start the watchdog thread. Call as early in WinMain as possible -- the dialog we most
// need to catch opens inside Engine::Startup, so this must already be running by then.
// Also records the calling thread as THE MAIN THREAD for the hang dump.
void Start();

// Flip BOOTING -> RUNNING. Call right after Engine::Startup returns.
void MarkStartupComplete();

// Stop the thread and stamp the file EXITED. Only reached on a clean shutdown; a killed
// process leaves its last line behind, which the reader script reports as STALE.
void Stop();

// The state and detail Stop() writes in place of EXITED / clean-shutdown, also published at once.
// Safe before Start():
// src/app/main.cpp names NO_GAME_DATA here when the game-data check stops the boot
// (src/app/game_data_check.h), and the platform's Shutdown then writes it. tools/boot_state.sh
// reports that state with its own exit code.
void SetExitState(const char* state, const char* detail);

}  // namespace BootState

// ---------------------------------------------------------------------------
// The heartbeat, as extern "C" so the runtime port and code/Game can call it
// without taking a dependency on standalone/ headers.
// ---------------------------------------------------------------------------

// Bump the frame counter. Called once per main-loop iteration from
// RtEngine_MainLoop_Run (src/engine/engine/mainloop_run.cpp) -- the single
// point every loop type (loader, splash, menu DialogLoopT, GameUpdateLoop)
// passes through. A counter that stops advancing is what "frozen" means here.
extern "C" void BootState_MarkFrame();

// MainLoop::Run has returned and teardown has begun. Teardown legitimately
// renders no frames, so it gets its own (longer) budget measured from this
// moment. Called from src/game/Game/game_main.cpp right after Run returns,
// before Engine shutdown.
extern "C" void BootState_MarkQuitting();

// ---------------------------------------------------------------------------
// The crash handler's stack walker, defined in src/diag/win/crash_handler_win.cpp next
// to the VEH crash handler that uses it: it walks the calling thread's own CONTEXT.
// Another thread's stack (the hang dump) goes through ThreadStack::Capture
// (src/diag/win/thread_stack.h), which walks it without a lock and resumes it before the
// module lookups.
//
// `origCtx` is not modified (the walker copies it).
// ---------------------------------------------------------------------------
struct _CONTEXT;
typedef void (*StackLineSink)(void* user, const char* line);
extern "C" void StandaloneWalkStack(struct _CONTEXT* ctx, StackLineSink sink, void* user);
