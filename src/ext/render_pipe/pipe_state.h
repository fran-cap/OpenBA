// ============================================================================
// src/ext/render_pipe/pipe_state.h -- RENDERPIPE: the drain state machine
// ============================================================================
// NOT RETAIL. ext.render_pipe (docs/extensions/handins/RENDERPIPE0.md section 4).
//
// Pure: no engine types, so tests/unit/ext_render_pipe_test.cpp drives it.
// At most one packet is pending (depth 1). Two Renderer slots ping-pong: the
// pending packet holds one, pass 1 fills the other.
//
// The transitions (THREADRESEARCH section 1.5, "drain, then run the frame
// exactly as today"):
//   * before the first fixed step of a Frame: drain (the fence)
//   * at the Frame tail, drain and run the frame unpipelined when the extension
//     was switched off (and uninstall), the current loop is not the built game,
//     a quit is pending, a resize is pending (Device::settingsChanged), the
//     minimap terrain is still to paint, ext.eye_camera is engaged, a quicksave /
//     quickload screenshot is due, or a diagnostic needs the sequential frame
//   * otherwise pipelined: pass 1 into the free slot; then the pending packet,
//     if any, is emitted; then the new packet is held -- unless the emit lost
//     the device (or failed BeginCommands), when the new packet is discarded
// ============================================================================
#pragma once

#include <cstdint>

namespace Ext {
namespace RenderPipe {

struct TailInputs {
    bool want            = true;    // Ext::Ext().render_pipe
    bool gameLoop        = true;    // the current loop is GameUpdateLoop with its world built
    bool quitting        = false;   // MainLoop::_quit set by this Frame's steps
    bool settingsChanged = false;   // Video::Device::settingsChanged (resize / mode change)
    bool paintTerrain    = false;   // Minimap::terrainImage still null (Draw step 3)
    bool eyeCamera       = false;   // ext.eye_camera engaged (ApplyFrame is alpha-dependent)
    bool screenshot      = false;   // m_quickSavePressed / m_quickLoadPressed (TakeScreenshot clears them)
    bool diagnostic      = false;   // a diagnostic that brackets the sequential frame
};

struct TailPlan {
    bool    drain      = false;   // emit the pending packet first
    bool    pipelined  = false;   // false: the faithful tail (MainLoop::Draw) runs this frame
    bool    uninstall  = false;   // the extension was switched off: remove the seam after the drain
    int32_t pass1Slot  = -1;      // pipelined: the Renderer slot pass 1 fills
    bool    renderIdle = false;   // pipelined: nothing pending, fixes 4 and 5 may write
};

class PipeState {
public:
    bool    Pending() const     { return _pending; }
    int32_t PendingSlot() const { return _pending ? _pendingSlot : -1; }

    // The slot pass 1 may fill: never the pending packet's.
    int32_t FreeSlot() const { return _pending ? 1 - _pendingSlot : 1 - _lastSlot; }

    // Top of the fixed-step loop: true = drain now (a step will run and a packet is pending).
    bool DrainBeforeSteps(bool stepsDue) const { return stepsDue && _pending; }

    TailPlan PlanTail(const TailInputs& in) const {
        TailPlan plan;
        const bool sequential = !in.want || !in.gameLoop || in.quitting || in.settingsChanged ||
                                in.paintTerrain || in.eyeCamera || in.screenshot || in.diagnostic;
        if (sequential) {
            plan.drain     = _pending;
            plan.uninstall = !in.want;
            return plan;
        }
        plan.pipelined  = true;
        plan.pass1Slot  = FreeSlot();
        plan.renderIdle = !_pending;
        return plan;
    }

    // The pending packet was emitted (or dropped by a failed BeginCommands).
    void Drained() { _pending = false; }

    // A pass-1 packet in `slot` is now pending.
    void Hold(int32_t slot) { _pending = true; _pendingSlot = slot; _lastSlot = slot; }

    // The new packet in `slot` was discarded (device lost during the emit).
    void Discard(int32_t slot) { _pending = false; _lastSlot = slot; }

    // A faithful frame drew with the loop's own Renderer (slot 0).
    void FaithfulFrame() { _lastSlot = 0; }

private:
    bool    _pending     = false;
    int32_t _pendingSlot = 0;
    int32_t _lastSlot    = 0;
};

}  // namespace RenderPipe
}  // namespace Ext
