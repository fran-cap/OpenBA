// ============================================================================
// src/engine/engine/mainloop_run.cpp — MainLoop::Run + MainLoop::Update
//                                       Wave 9 lane ADDR
// ============================================================================
//
//   | Export                                          | Retail      | Ord  |
//   |-------------------------------------------------|-------------|------|
//   | ?Run@MainLoop@Engine@@QEAAXPEAVUpdateLoop@2@@Z   | 0x1800dd3c0 | 3601 |
//   | ?Update@MainLoop@Engine@@QEAA_NAEBVTime@System@@@Z| 0x1800dd070 | 4496 |
//
// `Run` is the single most consequential address door in the wave: it is THE
// main loop, and `src/game/Game/game_main.cpp:176` reaches it BY ADDRESS.
// An unregistered address door prints no [RtUnresolved] line and no
// [RtResolve] MISS — at cutover it is simply a call through null with no
// diagnosis, which is why this one survived nineteen boot attempts while the
// name-door residue got enumerated.
//
// Both names were verified against the shipped export directory with pefile.
//
// A CORRECTION TO src/engine/engine/MainLoop.h, WHICH THIS FILE RELIES ON:
// that header files `Update` (0x1800dd070) and `Frame` (0x1800dd100) under
// "Internal, NOT exported". Both ARE exported — ordinals 4496 and 2377
// respectively, read out of the export directory. The layout facts in that
// header are excellent and are what made this port cheap; only the
// exported/internal classification of those two lines is wrong. It matters
// because it decides whether the residue this port leaves behind is a NAMED
// door the scanners can see or a SILENT one they cannot.
//
// ---------------------------------------------------------------------------
// 1. THE VTABLE SLOTS ARE READ OUT OF THE BINARY, NOT INFERRED
// ---------------------------------------------------------------------------
// Both bodies dispatch through the UpdateLoop vtable, and getting a slot wrong
// would call `Draw` where `Update` belongs. The slots were confirmed by dumping
// ??_7DialogLoopT@Engine@@6B@ (0x1801579a0) — the derived table, because the
// base table's own slots are all the same `_purecall` thunk and prove nothing
// (reference_uniform_population_is_not_a_verification.md is the same trap one
// level over):
//
//     slot 0  +0x00  0x18004d8b0  (deleting dtor)
//     slot 1  +0x08  ?Startup@DialogLoopT@Engine@@UEAAXXZ
//     slot 2  +0x10  ?Shutdown@DialogLoopT@Engine@@UEAAXXZ
//     slot 3  +0x18  ?Update@DialogLoopT@Engine@@UEAA_NAEBVTime@System@@@Z
//     slot 4  +0x20  ?Draw@DialogLoopT@Engine@@UEAAXM@Z
//
// ---------------------------------------------------------------------------
// 2. THE CLOCK: WHY THIS READS THE GLOBALS AND DOES **NOT** CALL OUR OWN TIMER
// ---------------------------------------------------------------------------
// Retail inlines System::Timer::CurrentTimeTicks three times in `Run`:
//
//     1800dd3da  CALL [0x18014b1a8]              ; QueryPerformanceCounter
//     1800dd3e8  SUB  RAX,[0x1801fe738]          ; _systemInitialTick
//     1800dd3ef  IMUL RAX,[0x1801fe720]          ; _ticksPerSecond  (1000000)
//     1800dd3f9  IDIV qword ptr [0x1801fe730]    ; _systemTicksPerSecond (QPF)
//
// `src/engine/system/timer.cpp` already ports that expression as
// `RtSystem_Timer_CurrentTimeTicks()`, and calling it here would be the
// obvious move. IT WOULD ALSO BE A BUG, and timer.cpp's own banner is the one
// that names the hazard: our Timer keeps its OWN `_systemInitialTick`, sampled
// by OUR PlatformStartup, while the DLL's `MainLoop::Frame` and `MainLoop::Draw`
// — which this port still calls — read the DLL's globals directly. The two
// epochs differ by the interval between the two PlatformStartup calls. Mixing
// them would put `_startTicks` and `_frameStartTicks` on one epoch and
// everything Frame computes from them on another.
//
// timer.cpp says that hazard is bounded because "no file names CurrentTimeTicks
// at all". THIS FILE IS THE FIRST FUNCTION THAT WOULD HAVE MADE IT OBSERVABLE.
// So the globals are read through `ResolveAddr(Module::Runtime, ...)`, exactly
// as retail reads them and exactly as `engine/funcs/engine_funcs.cpp:1545-1548`
// already does: with the DLL present that is the DLL's storage (one epoch,
// shared with Frame/Draw); with the mint armed at cutover it is the mint's
// storage, which our own PlatformStartup writes. Either way there is one epoch.
//
// `QueryPerformanceCounter` is called DIRECTLY rather than through the DLL's
// import slot at 0x18014b1a8 — that slot is Runtime-x64.dll's own IAT and does
// not exist once the DLL is gone.
//
// ---------------------------------------------------------------------------
// 3. DOOR ARITHMETIC — THIS PORT MAKES THE COUNTED SURFACE GO **UP**
// ---------------------------------------------------------------------------
// Stated plainly because a lane that closes 3 doors and opens 4 has gone
// backwards, and this one does open more than it closes. `Run`'s five internal
// callees:
//
//   0x1800de520  Audio::Device::Update        PORTED  audio_device.cpp:283-284
//   0x18009fdf0  Application::PlatformUpdate  PORTED  rt_application_port.cpp:36-37
//   0x1800e1490  Input::Device::Update        PORTED input_device_update.cpp (member call below, K5)
//   0x1800dd070  MainLoop::Update             PORTED HERE
//   0x1800dd100  MainLoop::Frame              PORTED (W25 BODY) mainloop_frame.cpp
//                                                and it is 684 bytes, not 683 —
//                                                .pdata's entry fragment reads 123.
//
// So: closes 1 ADDRESS door (0x1800dd3c0) and opens 2 NAME doors
// (Input::Device::Update ord 4482, MainLoop::Frame ord 2377). That exchange is
// still worth making, and this is the reason: an unregistered ADDRESS door
// fails SILENTLY at cutover, while a name door prints `[RtUnresolved]` and is
// counted by `unsatisfied_export_scan.py`. The port converts one undiagnosable
// null call into two diagnosable ones. `called` is not a denominator counting
// down to zero (reference_porting_can_grow_the_cutover_surface.md).
//
// The two ALREADY-PORTED callees are reached by ADDRESS, not by name, and that
// is deliberate. `ResolveAddr(Module::Runtime, addr)` consults the PORT
// REGISTRY FIRST, so it finds our port; `ResolveExport` FALLS THROUGH to the
// shipped implementation for anything not registered under that exact name,
// which would silently bypass the port we already have
// (reference_bypassed_port_needs_a_source_checker.md,
// reference_resolveaddr_consults_port_registry_first.md). Both happen to be
// registered under both doors today, but relying on that is relying on another
// lane's registration style.
//
// ---------------------------------------------------------------------------
// 4. VERIFICATION — WHAT IS AND IS NOT ESTABLISHED
// ---------------------------------------------------------------------------
// `Run` CANNOT have a diff oracle. It is called once per process, it never
// returns until the game quits, and it mutates the entire world through the
// loop objects. There is nothing to replay and nothing to compare against. This
// is a genuine "no oracle", not a missing one — unlike the four Serialize
// cases reference_write_path_serialize_is_replayable.md corrected.
//
// `Update` gets a SELF-CONSISTENCY shadow, and it is labelled as such rather
// than dressed up as an oracle: it checks the post-conditions the body itself
// establishes (after a transition `_nextLoop` is null and `_currentLoop` is
// what `_nextLoop` was). That is NOT a comparison against retail. It would
// catch a botched transition; it would not catch a wrong-but-consistent one.
//
// The real test for both is a BOOT: with the DLL present, `game_main.cpp:176`
// resolves 0x1800dd3c0 through the registry, finds this body, and it drives the
// actual game. MEASURED, 2026-09-08, DLL present, the diff oracle armed,
// BANISHED_AUTOSTART=1 through menu -> New Game -> Start -> in-game -> quit:
//
//     Engine::MainLoop::Run     calls=0  worked=1  divergences=0
//     Engine::MainLoop::Update  calls=1  worked=1  divergences=0
//
// READ `calls=0` ON `Run` CORRECTLY — it is not a dead port
// (reference_uncalled_port_is_not_verified.md is about the opposite case).
// `calls` counts COMPARISONS, and `Run` makes none because it has no oracle.
// `worked=1` is the evidence that matters: this body executed, once, and drove
// the whole session — boot, menu, the new-game transition, gameplay and a clean
// shutdown. A `worked=0` here would have meant the registry never handed
// game_main.cpp:176 this function.
//
// `Update`'s `worked=1` is ONE transition, and that is the honest number: our
// `Update` is reached only from our `Run`, before the frame loop. Inside the
// loop it is the DLL's `Frame` that ticks, and `Frame` calls the DLL's `Update`
// by a direct internal call the registry cannot redirect. So this port is
// exercised on exactly the real menu -> game transition and nowhere else, until
// `Frame` is ported too. Thin, but real, and not to be reported as broad
// coverage.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include <cstdio>

#include "engine/MainLoop.h"
#include "engine/UpdateLoop.h"         // ENDGAME B.3.7: the loop classes
#include "engine/Engine.h"             // ENDGAME B.3.7: Engine::Engine
#include "application/Application.h"   // ENDGAME B.3.7: PlatformUpdate
#include "audio/Device.h"              // ENDGAME B.3.7 callers (K4): Audio::Device
#include "input/Device.h"              // ENDGAME B.3.7 callers (K5): Input::Device
#include "mainloop_internal.h"
#include "load_time.h"   // [LoadTime] stamps (NOT RETAIL, print-only; block LOADSTAMP)
#include <atomic>


// src/diag/win/boot_state_watchdog.cpp -- the frozen-exe heartbeat. Declared
// here rather than including a standalone/ header so runtime/ keeps no
// dependency on the harness's include path.
extern "C" void BootState_MarkFrame();
// src/app/boot_window.cpp -- the deferred window reveal (NOT RETAIL, boot_window.h).
extern "C" void BootWindow_MarkFrame();

using RtEngine::MainLoop;

namespace {

// UpdateLoop vtable slots — see §1. The object is opaque here on purpose: this
// file needs the vtable pointer at +0x00 and nothing else about it, and
// RtEngine::UpdateLoop is a different lane's type.
// ENDGAME B.3.7: the hand-inlined `LoopVtable(loop)[N]` slot calls this file
// used to carry (slots 0 DeletingDtor, 1 Startup, 2 Shutdown, 3 Update) are
// ordinary virtual calls on `Engine::UpdateLoop` now; its slot order is
// retail's (src/engine/engine/UpdateLoop.h), so each compiles to the same
// `CALL [vptr+N*8]`.

// Retail's inlined CurrentTimeTicks now has ONE definition, in
// mainloop_frame.cpp, reached through mainloop_internal.h. It used to live here
// in this anonymous namespace, which was right while it had one caller. Wave 25
// lane BODY added `MainLoop::Frame`, which inlines the SAME four instructions
// against the SAME three globals, and two copies of a clock that must agree on
// an EPOCH is how the two-epoch bug §2 describes gets reintroduced by a later
// edit to one copy. The reasoning in §2 is unchanged and still lives here.
inline int64_t CurrentTimeTicksViaGlobals() {
    return rtmainloop::CurrentTimeTicksViaGlobals();
}

}  // namespace

// ?Update@MainLoop@Engine@@QEAA_NAEBVTime@System@@@Z   retail 0x1800dd070  ord 4496
//
// Tick the current loop; if it says it is finished, or a next loop has been
// queued by SetNextLoop, tear the current one down and start the next.
//
// Retail's control flow, preserved exactly — note that BOTH branches of the
// first test fall into the same transition block, and that the two `_nextLoop`
// tests return DIFFERENT values (false when the loop finished and there is
// nothing queued, true when it is still running):
//
//   1800dd087  CALL [vtbl+0x18]           ; currentLoop->Update(t)
//   1800dd08c  TEST AL,AL / JNE 1800dd0a0
//   1800dd08e  CMP  [RBX+0x28],0 / JNE 1800dd0a7   ; !running: queued? -> switch
//   1800dd095  return false                        ; !running, nothing queued
//   1800dd0a0  CMP  [RBX+0x28],0 / JE  1800dd0eb   ; running: nothing queued -> true
bool Engine::MainLoop::Update(const System::Time& time) {

    const bool running = _currentLoop->Update(time);                // slot 3

    if (!running && _nextLoop == nullptr) {
        return false;                                    // 0x1800dd095
    }
    if (running && _nextLoop == nullptr) {
        return true;                                     // 0x1800dd0eb
    }

    // --- the transition, 0x1800dd0a7..0x1800dd0e7 ---------------------------
    // EXT SEAM (NOT RETAIL, docs/android/P13_OVERLAP.md): ext.render_pipe joins its
    // render thread before the loop it may still be drawing is shut down and deleted.
    if (_extFrame != nullptr)
        _extFrame(*this, kExtBeforeTransition, 0.0f, 0.0f);
    _currentLoop->Shutdown();                                       // slot 2

    // Retail re-reads _currentLoop and null-tests it before the deleting dtor
    // even though it just called through it. Reproduced: the Shutdown above is
    // a virtual call into game code that is free to have nulled it.
    if (UpdateLoop* toDelete = _currentLoop)
        delete toDelete;                                  // slot 0, delete

    UpdateLoop* next = _nextLoop;
    _nextLoop = nullptr;                                 // 0x1800dd0c8
    _currentLoop = next;

    // THE SHADOW'S POST-CONDITION IS SAMPLED **HERE**, NOT AT THE END, AND THE
    // FIRST VERSION OF THIS GOT IT WRONG. It asserted `_nextLoop == nullptr`
    // after the whole body and fired on the very first real transition
    // (menu -> new game) with `next=0000...84dc0`. The PORT was right and the
    // PREDICATE was wrong: retail clears `_nextLoop` BEFORE calling `Startup`
    // (0x1800dd0c8 precedes 0x1800dd0d7) precisely so that `Startup` is free to
    // queue the following loop, and the menu -> game chain DOES exactly that.
    // Asserting the field is still null at the end of the body outlaws the
    // engine's normal loop chaining.
    //
    // Sampling here checks what the transition step itself actually guarantees.
    // Recorded rather than quietly relaxed, because a shadow that is loosened
    // until it stops complaining verifies nothing.
    const bool clearedOk = (_nextLoop == nullptr) && (_currentLoop == next);
    (void)clearedOk;

    LoadTime::StampType("loop.startup", typeid(*next));   // NOT RETAIL: print-only
    next->Startup();                                                // slot 1
    LoadTime::StampType("loop.started", typeid(*next));   // NOT RETAIL: print-only

    _currentLoop->Update(time);                                     // slot 3, fresh read
    _contextDirty = 1;                                   // 0x1800dd0e7

    // SELF-CONSISTENCY SHADOW, not an oracle (§4). These are post-conditions
    // this body itself establishes; a green row here means the transition was
    // internally coherent, NOT that it matched retail.
    return true;
}

// ?Run@MainLoop@Engine@@QEAAXPEAVUpdateLoop@2@@Z   retail 0x1800dd3c0  ord 3601
void Engine::MainLoop::Run(UpdateLoop* loop) {

    // The three subsystem pointers hang off Engine, which Run reaches through
    // _engine (+0x78). The offsets are Run's own: +0x6e0 Input::Device,
    // +0x6d0 Audio::Device, +0x690 Application.
    //   1800dd416  MOV RCX,[RBX+0x6e0]
    //   1800dd422  MOV RCX,[RBX+0x6d0]
    //   1800dd432  MOV RCX,[RBX+0x690]

    _currentLoop = loop;                                                 // +0x20
    _startTicks  = CurrentTimeTicksViaGlobals();                         // +0x30

    LoadTime::StampType("loop.startup", typeid(*loop));   // NOT RETAIL: print-only (MainLoop::Run's first loop)
    loop->Startup();                                                     // slot 1
    LoadTime::StampType("loop.started", typeid(*loop));   // NOT RETAIL: print-only

    ::Engine::Engine* engine = _engine;
    const System::Time& deltaTime = _deltaTime;             // the `Time&` retail passes

    // Input::Device::Update — ENDGAME B.3.7 callers (K5): a member of the real
    // class (was the name door).
    engine->_inputDevice->Update(deltaTime);                             // +0x6e0

    // Audio::Device::Update (0x1800de520). ENDGAME B.3.7: a member call on the
    // real class (was BY ADDRESS through a namespace-scope atomic slot).
    engine->GetAudioDevice()->Update(deltaTime);                         // +0x6d0
    // Application::PlatformUpdate (0x18009fdf0, AEAA): was reached BY ADDRESS
    // (`ResolveAddr(Module::Runtime, 0x18009fdf0)` in a function-local static);
    // ENDGAME B.3.7: a member call on the real class. Result discarded, as retail's.
    engine->GetApplication()->PlatformUpdate();                          // +0x690

    Update(_deltaTime);                                                  // LEA RDX,[RBX+0x48]

    // MainLoop::Frame — **PORTED, Wave 25 lane BODY** (mainloop_frame.cpp).
    // ENDGAME B.3.7: a direct member call, as retail's rel32 CALL is. (The
    // name door this replaced was kept for a census column; B.3 converts every
    // runtime-internal door onto the class it belongs to.)
    //
    // The loop condition is tested BEFORE the first iteration (0x1800dd44a
    // JNE past the whole body) and again at the bottom (0x1800dd4be JE back to
    // the top), so this is a `while`, not a `do/while` — a `do/while` would run
    // one frame after a loop that asked to quit during its Startup.
    while (_quit == 0) {
        _frameStartTicks = CurrentTimeTicksViaGlobals();                 // +0x60
        Frame(false);
        _frameEndTicks = CurrentTimeTicksViaGlobals();                   // +0x70
        _frameTime = _frameEndTicks - _frameStartTicks;                  // +0x58

        // NOT RETAIL. The frozen-exe heartbeat: one interlocked increment, and
        // this is THE place for it -- every loop type (loader, splash, menu
        // DialogLoopT, GameUpdateLoop) is ticked by the Frame call above, so a
        // counter that stops moving here means the main thread is wedged.
        // src/diag/win/boot_state_watchdog.cpp turns that into STATE=HUNG and an
        // all-thread stack dump instead of a ten-minute caller timeout.
        BootState_MarkFrame();

        // NOT RETAIL. The deferred window reveal (src/app/boot_window.h): the
        // window CreateDevice left hidden is shown once this first Frame has
        // presented, whichever loop drew it. A null test after the reveal.
        BootWindow_MarkFrame();
    }

    // Teardown, 0x1800dd4c0..0x1800dd4dd. Both reads of _currentLoop are fresh,
    // as retail's are.
    _currentLoop->Shutdown();                                            // slot 2

    if (UpdateLoop* toDelete = _currentLoop)
        delete toDelete;                                       // slot 0

    // No oracle is possible here (§4); the site is registered so that its call
    // count is still visible in the diff report.
}
