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
// `Run` is THE main loop; `src/game/Game/game_main.cpp` calls it as a member.
//
// Both names were verified against the shipped export directory with pefile.
//
// `Update` (0x1800dd070) and `Frame` (0x1800dd100) are both exported — ordinals
// 4496 and 2377 respectively, read out of the export directory.
//
// ---------------------------------------------------------------------------
// 1. THE VTABLE SLOTS ARE READ OUT OF THE BINARY, NOT INFERRED
// ---------------------------------------------------------------------------
// Both bodies dispatch through the UpdateLoop vtable, and getting a slot wrong
// would call `Draw` where `Update` belongs. The slots were confirmed by dumping
// ??_7DialogLoopT@Engine@@6B@ (0x1801579a0) — the derived table, because the
// base table's own slots are all the same `_purecall` thunk and prove nothing
// (a uniform population cannot tell a wrong reading from a right one):
//
//     slot 0  +0x00  0x18004d8b0  (deleting dtor)
//     slot 1  +0x08  ?Startup@DialogLoopT@Engine@@UEAAXXZ
//     slot 2  +0x10  ?Shutdown@DialogLoopT@Engine@@UEAAXXZ
//     slot 3  +0x18  ?Update@DialogLoopT@Engine@@UEAA_NAEBVTime@System@@@Z
//     slot 4  +0x20  ?Draw@DialogLoopT@Engine@@UEAAXM@Z
//
// ---------------------------------------------------------------------------
// 2. THE CLOCK: THE TIMER CLASS STATICS, READ THROUGH ONE SHARED HELPER
// ---------------------------------------------------------------------------
// Retail inlines System::Timer::CurrentTimeTicks three times in `Run`:
//
//     1800dd3da  CALL [0x18014b1a8]              ; QueryPerformanceCounter
//     1800dd3e8  SUB  RAX,[0x1801fe738]          ; _systemInitialTick
//     1800dd3ef  IMUL RAX,[0x1801fe720]          ; _ticksPerSecond  (1000000)
//     1800dd3f9  IDIV qword ptr [0x1801fe730]    ; _systemTicksPerSecond (QPF)
//
// `src/engine/system/timer.cpp` ports that expression as
// `System::Timer::CurrentTimeTicks()`, from the same Timer class statics. This
// file reads them through `rtmainloop::CurrentTimeTicksViaGlobals()`
// (mainloop_internal.h, defined in mainloop_frame.cpp), the one helper `Frame`
// also uses, so `_startTicks`, `_frameStartTicks` and everything Frame computes
// from them share one epoch.
//
// `QueryPerformanceCounter` is called DIRECTLY rather than through the DLL's
// import slot at 0x18014b1a8 — that slot is Runtime-x64.dll's own IAT and does
// not exist once the DLL is gone.
//
// ---------------------------------------------------------------------------
// 3. RUN'S FIVE INTERNAL CALLEES
// ---------------------------------------------------------------------------
//   0x1800de520  Audio::Device::Update        audio_device.cpp
//   0x18009fdf0  Application::PlatformUpdate  application_platform.cpp
//   0x1800e1490  Input::Device::Update        input_device_update.cpp
//   0x1800dd070  MainLoop::Update             this file
//   0x1800dd100  MainLoop::Frame              mainloop_frame.cpp
//                                                and it is 684 bytes, not 683 —
//                                                .pdata's entry fragment reads 123.
//
// All five are direct member calls.
//
// ---------------------------------------------------------------------------
// 4. VERIFICATION — WHAT IS AND IS NOT ESTABLISHED
// ---------------------------------------------------------------------------
// `Run` CANNOT have a diff oracle. It is called once per process, it never
// returns until the game quits, and it mutates the entire world through the
// loop objects. There is nothing to replay and nothing to compare against. This
// is a genuine "no oracle", not a missing one — unlike the four Serialize
// cases on the write path, which replay into two separate sinks.
//
// `Update` has no check of its own: `clearedOk` in the transition is computed
// and discarded. The real test for both is a BOOT: game_main.cpp calls `Run`,
// which drives the whole session, and `Update` is called from `Run` and from
// `Frame` (mainloop_frame.cpp).
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
// here rather than including a src/diag/ header so src/engine/ keeps no
// dependency on the harness's include path.
extern "C" void BootState_MarkFrame();
// src/app/boot_window.cpp -- the deferred window reveal (NOT RETAIL, boot_window.h).
extern "C" void BootWindow_MarkFrame();

using RtEngine::MainLoop;

namespace {

// UpdateLoop vtable slots — see §1.
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

    // `clearedOk` is computed and discarded: nothing reads it. Retail clears
    // `_nextLoop` BEFORE calling `Startup` (0x1800dd0c8 precedes 0x1800dd0d7)
    // precisely so that `Startup` is free to queue the following loop, and the
    // menu -> game chain DOES exactly that.
    const bool clearedOk = (_nextLoop == nullptr) && (_currentLoop == next);
    (void)clearedOk;

    LoadTime::StampType("loop.startup", typeid(*next));   // NOT RETAIL: print-only
    next->Startup();                                                // slot 1
    LoadTime::StampType("loop.started", typeid(*next));   // NOT RETAIL: print-only

    _currentLoop->Update(time);                                     // slot 3, fresh read
    _contextDirty = 1;                                   // 0x1800dd0e7

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

}
