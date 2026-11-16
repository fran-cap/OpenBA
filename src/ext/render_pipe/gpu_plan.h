// ============================================================================
// src/ext/render_pipe/gpu_plan.h -- SMOOTHREPLAY: when the GPU is done with
// what was presented
// ============================================================================
// NOT RETAIL. ext.render_pipe_replay (docs/extensions/handins/SMOOTHREPLAY.md section 4).
//
// A replayed frame is submitted in a few milliseconds of the render thread's time, but
// the GPU needs its own time for it, and the real frame that follows waits behind it
// (the backend's begin waits for the frame two back). Where the GPU's time for a frame
// is about the sim step's (the S25's stress town at 50x: 12 ms against 13), a replayed
// frame planned by its submission alone made every real frame late.
//
// The backend measures the GPU's time for a frame (finish_split.h GpuFrameUs). This keeps
// the time the GPU is expected to be done with everything presented so far, so the replay
// loop can ask when a frame presented now would be complete. Engine-free (the unit test
// drives it); the render thread only.
// ============================================================================
#pragma once

#include <cstdint>

namespace Ext {
namespace RenderPipe {

class GpuPlan {
public:
    // The estimate never runs further ahead than this many frames (the backend's frames in
    // flight and one: its begin blocks beyond that, so a real queue is never longer).
    static constexpr int64_t kMaxQueued = 3;

    // A frame was presented at `nowUs`; the GPU needs `frameUs` for one.
    void Presented(int64_t nowUs, int64_t frameUs) {
        _freeUs = (_freeUs > nowUs ? _freeUs : nowUs) + frameUs;
        const int64_t latest = nowUs + frameUs * kMaxQueued;
        if (_freeUs > latest) _freeUs = latest;
    }

    // How long from `nowUs` until a frame presented now would be complete on the GPU.
    int64_t CompleteIn(int64_t nowUs, int64_t frameUs) const {
        return (_freeUs > nowUs ? _freeUs - nowUs : 0) + frameUs;
    }

private:
    int64_t _freeUs = 0;   // when the GPU is done with what was presented (the driver's NowUs clock)
};

}  // namespace RenderPipe
}  // namespace Ext
