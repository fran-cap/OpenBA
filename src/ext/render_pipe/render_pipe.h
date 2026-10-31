// ============================================================================
// src/ext/render_pipe/render_pipe.h -- ext.render_pipe (class A, default OFF)
// ============================================================================
// NOT RETAIL. docs/extensions/handins/RENDERPIPE0.md; THREADRESEARCH.md section 1.5.
//
// The frame's emit (Renderer::Draw pass 2, the post-scene draws, the post-draw
// callbacks, EndCommands, the frame index, Present) runs one frame behind the
// gather (GameUpdateLoop::Draw steps 1-6 and pass 1). RENDERPIPE-1 runs it on a
// render thread (docs/extensions/handins/RENDERPIPE1.md) when the machine has
// at least three physical cores (Cpu::RenderThread); otherwise RENDERPIPE-0's
// sequential split runs it on the main thread in the PIPELINED ORDER (pass 1 of
// frame N+1, then the held packet of frame N). The sim is unchanged: before any
// fixed step the packet is joined (MainLoop::_extFrame, kExtBeforeSteps).
//
// Selected through ONE engine seam, Engine::MainLoop::_extFrame (MainLoop.h),
// set by Sync() at boot and on every Extensions-dialog toggle. Switching off
// takes effect at the next Frame tail (drain, then the seam is removed).
// ============================================================================
#pragma once

#include <cstdint>

namespace Ext {
namespace RenderPipe {

// Install the seam when ext.render_pipe is on (the switch-off is the hook's own,
// at the next Frame tail). Called by Ext::InitFromSettings and Ext::SetAndRecord.
void Sync();
bool Installed();

// Per CSV window, for the [Stress::pipe] line (src/diag/stress_harness.cpp).
struct Counters {
    uint64_t pipelined   = 0;   // frames whose first half ran pipelined (a packet was held)
    uint64_t sequential  = 0;   // frames run unpipelined while installed (the faithful tail)
    uint64_t emits       = 0;   // packets emitted
    uint64_t drainStep   = 0;   // emitted before a fixed step (the fence)
    uint64_t drainOther  = 0;   // emitted at a tail that then ran unpipelined
    uint64_t discarded   = 0;   // new packets dropped (device lost / BeginCommands failed in the emit)
    uint64_t idleWrites  = 0;   // frames that ran SunLight::Set / UpdateCamera (nothing pending)
    uint64_t idleSkips   = 0;   // frames that skipped them (fixes 4 and 5)
    // RENDERPIPE-1 (the render thread; all 0 in the sequential split)
    uint64_t waits       = 0;   // joins that blocked on the render thread (hand-off + fence)
    uint64_t waitsFence  = 0;   //   of which at the fence / a transition drain
    uint64_t waitUs      = 0;   // the time those joins blocked, us
    uint64_t latencyUs   = 0;   // sum over joined packets of hand-off -> Present done, us
    uint64_t latencyN    = 0;
    uint64_t recoveries  = 0;   // device-lost recoveries run on the main thread at a join
    // P13_OVERLAP (docs/android/P13_OVERLAP.md; render thread only)
    uint64_t overlapped  = 0;   // fences skipped: the steps ran beside a packet's emit (its snapshot)
    uint64_t snapshots   = 0;   // packets built with frozen copies
    uint64_t refused     = 0;   // packets the snapshot refused (they keep the fence)
    uint64_t snapshotUs  = 0;   // main-thread time building the copies, us
    uint64_t snapshotKB  = 0;   // arena bytes written, KB
    // The overlap governor at the window's end (overlap_governor.h): its state and smoothed measures
    bool     governorOn            = false;
    int64_t  governorRemovableUs   = 0;   // the fence wait a snapshot removes, per Frame
    int64_t  governorIdleUs        = 0;   // the main thread's other waits, per Frame
    int64_t  governorIdleWithoutUs = 0;   // on: what it would idle without the copies (off: = idle)
};

// True while the packet runs on the render thread (installed, >= 3 physical cores).
bool Threaded();
Counters TakeWindow();

}  // namespace RenderPipe
}  // namespace Ext
