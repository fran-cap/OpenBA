// ============================================================================
// src/ext/quick_screens/quick_screens.h -- ext.quick_screens (class A)
// ============================================================================
// NOT RETAIL. docs/extensions/handins/SPLASH.md; the user's rulings 1 and 3 in
// docs/extensions/handins/LOADPLAN.md section 1.
//
// Retail holds every splash / loading screen for a minimum display time and
// the save dialog's operations for a minimum operation time, even when the
// work they cover finished long before. With the row ON each floor counts as
// satisfied as soon as its work is done; the fades still play unchanged.
//
//   * GameLoadDialogLoopT::Update (src/game/Game/splash_loop_stubs.cpp): the
//     hold `_elapsedTicks >= _delayTicks` (2.0 s ShiningRock, 0.5 s Loading at
//     boot, 1.0 s on every other loading screen). The loader-idle and
//     world-built terms beside it are retail's and stay; ON adds the other
//     work the hold covered -- the menu-scene build (ChunkedSceneLoader armed
//     or busy) and the screen's own reveal fade -- via SplashWorkDone.
//   * SaveLoadDialog::OnOpened / DoSave / DoLoad (src/game/Dialog/
//     saveloaddialog.cpp): SetMinOperationTime 0.75 s / 1.5 s / 1.5 s. ON
//     passes kNoMinOperationTime instead: Save::SaveLoad::Update dispatches an
//     operation's step only while the persistence worker is idle, so the
//     operation still ends only once its file work is done.
//   * SaveLoadDialog::OnUpdate, while the save list is being read: retail's
//     Save::SaveLoad::UpdateList takes one step per frame and three steps per
//     save (verify the file, read its header block, build its row), so the
//     list costs three frames per save however fast the worker is. ON keeps
//     stepping inside the frame, for at most kListPumpBudgetMs, each step
//     taken as soon as the worker has finished the one before
//     (PumpSaveList, list_pump.cpp; the loop itself is PumpListSteps below).
//     Same steps in the same order, so the list and its Corrupt marks are
//     retail's. docs/extensions/handins/SPLASH.md section 2.3.
//     kOpList only: the load, save and delete operations are a few steps each.
//
// The toggle is read once per screen (GameLoadDialogLoopT::Startup) and once
// per save-dialog operation, never per frame. Standard C++ only (the unit
// tests include it: tests/unit/ext_quick_screens_test.cpp).
// ============================================================================
#pragma once

#include <cstdint>

namespace Ext {
namespace QuickScreens {

// The splash hold's remaining work under ext.quick_screens, asked only while
// the retail hold has not elapsed. Retail's own terms (loader thread idle,
// world built) are tested beside it at the call site.
//   `fadeOutStarted`: this screen already kicked its fade-out to black (the
//       hold ended on an earlier frame; the fade now runs toward 1, so the
//       reveal test below would otherwise re-close the hold mid-fade).
//   `sceneBuildPending`: the menu scene's chunked build is armed or running
//       (ChunkedSceneLoader::IsArmed() || IsBusy()).
//   `fadeAmount`: the screen's fullscreen fade (UI +0xF4; 1 = black, 0 = the
//       reveal has finished).
// True: the hold is over.
inline bool SplashWorkDone(bool fadeOutStarted, bool sceneBuildPending, float fadeAmount) {
    return fadeOutStarted || (!sceneBuildPending && fadeAmount <= 0.0f);
}

// The whole hold decision, as GameLoadDialogLoopT::Update makes it (the unit
// tests drive this; the call site spells the same terms inline so the retail
// arm evaluates nothing new). `quick` = the row, latched at the screen's
// Startup.
inline bool SplashHoldOver(bool quick, int64_t elapsedTicks, int64_t delayTicks,
                           bool fadeOutStarted, bool sceneBuildPending, float fadeAmount) {
    return elapsedTicks >= delayTicks ||
           (quick && SplashWorkDone(fadeOutStarted, sceneBuildPending, fadeAmount));
}

// The save dialog's minimum operation time under ext.quick_screens, in
// System::Time ticks. Save::SaveLoad's gates are `_elapsed <= _minOperationTime`
// -> wait, so 0 lets the step through on the first tick the worker is idle
// (Update has added a frame delta by then).
constexpr int64_t kNoMinOperationTime = 0;

// The save list's in-frame stepping budget: how long one frame may spend
// waiting on the persistence worker and stepping the list. The "please wait"
// page with its spinner is all that is on screen meanwhile.
constexpr double kListPumpBudgetMs = 8.0;

// The save list's in-frame loop, on callables so the unit tests drive it.
//   nowMs()        a monotonic clock, milliseconds.
//   waitIdle(left) waits up to `left` ms for the worker to be idle; false when
//                  it is still busy.
//   step()         takes one step of the list; false when the list has
//                  finished or the step changed nothing (the next one needs a
//                  new frame).
// Returns the steps taken. Never starts a wait once the budget is spent.
template <class NowMs, class WaitIdle, class Step>
inline int PumpListSteps(double budgetMs, NowMs&& nowMs, WaitIdle&& waitIdle, Step&& step) {
    const double deadline = nowMs() + budgetMs;
    int steps = 0;
    for (;;) {
        const double left = deadline - nowMs();
        if (left <= 0.0) break;
        if (!waitIdle(left)) break;
        ++steps;
        if (!step()) break;
    }
    return steps;
}

}  // namespace QuickScreens
}  // namespace Ext
