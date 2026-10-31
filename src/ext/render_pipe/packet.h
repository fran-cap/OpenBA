// ============================================================================
// src/ext/render_pipe/packet.h -- RENDERPIPE: one frame's second half, as values
// ============================================================================
// NOT RETAIL. ext.render_pipe (class A, docs/extensions/handins/RENDERPIPE0.md,
// THREADRESEARCH.md section 1.5).
//
// The frame is split where `Renderer::Draw`'s pass 1 ends. The main thread runs
// frame N+1's first half (GameUpdateLoop::Draw steps 1-6 and pass 1) and records
// here, AS VALUES, everything frame N+1's second half needs; the second half
// (pass 2, the post-scene draws, the post-draw callbacks, EndCommands, the frame
// index, Present) runs later from this record. RENDERPIPE-0 runs both halves on
// the main thread in the PIPELINED ORDER (pass 1 of N+1, then the packet of N);
// RENDERPIPE-1 moves the second half onto a thread.
//
// This header is engine-free (forward declarations and plain arrays only) so
// the unit tests can drive `Emit` with a recording `Ops`.
// ============================================================================
#pragma once

#include <cstdint>

#include "graphics/renderer_ext_ao.h"   // Graphics::ExtAoFrame: plain data, no engine include

class GameUpdateLoop;
class Tool;
namespace Graphics { class Renderer; class Material; }
namespace Save { class SaveLoad; }

namespace Ext {
namespace RenderPipe {

struct Packet {
    // ---- identity --------------------------------------------------------
    GameUpdateLoop*     game     = nullptr;   // the loop whose frame this is
    Graphics::Renderer* renderer = nullptr;   // fix 1: the instance pass 1 filled
    int32_t             slot     = -1;        // 0 = the loop's own Renderer, 1 = the extension's

    // ---- what pass 2 sees (fix 2; the RenderPipeFrame shadow checks them) --
    int32_t frameIndex   = 0;   // the main-owned index pass 1 latched (Context::_frameIndex at emit)
    int32_t modelIndex   = 0;   // the sequential order's index for this frame (shadow model)
    float   alpha        = 0;   // GameUpdateLoop::Draw step 1's alpha (the pause override applied)
    int32_t shaderDetail = 0;   // step 6: Video::Configuration::shaderDetail for this frame

    // ---- fix 3: the context writes made before pass 2, replayed in retail's order
    float   frameTime     = 0;   // MainLoop::Frame tail: Context::_time.x
    int64_t frameStartTicks = 0; // RENDERPIPE-1: MainLoop::_frameStartTicks of the Frame that gathered it (FrameUpdateTime)
    int64_t gameTimeTicks = 0;   // step 2: SetTime's game time (the frame time is read at emit)
    float   weather[4]    = {};  // step 5: WeatherSimulation +0x1C -> Context +0x3B8 and pixel[4]
    float   fogNear       = 0;   // step 5: SetFog
    float   fogFar        = 0;
    float   fogColor[4]   = {};
    bool    forceSnow     = false;   // the BANISHED_FORCE_SNOW diagnostic (Opt().forceSnow)
    float   snow          = 0;

    // ---- the post-scene draws (steps 8, 10, 11, 12) ----------------------
    Save::SaveLoad*     saveLoad   = nullptr;   // step 8 (null: not drawn)
    Graphics::Material* uiMaterial = nullptr;   // step 8's ExternalPtr<Material>
    float               view[16]   = {};        // step 10: AsMatrix of the camera's transform
    float               proj[16]   = {};        // step 10: the camera's projection
    Tool*               tool       = nullptr;   // step 11 (null: not drawn)
    char                toolFlag   = 0;         // step 11: UI captured or paused
    bool                sharpUi    = false;     // step 12: ext.sharp_ui at record time

    // ---- ext.ambient_occlusion (AOCORE): the game's arm, carried by value ----
    // MainHalf records what GameUpdateLoop::Draw step 7 would arm the static
    // Renderer::_extAo with; pass 2 (passes.cpp) reads it from here, so the
    // render thread never reads the main thread's static.
    bool                 aoOn = false;
    Graphics::ExtAoFrame ao   = {};

    // ---- P13_OVERLAP (docs/android/P13_OVERLAP.md) -------------------------
    // The items point at frozen copies (snapshot.h): the emit up to Pass2 reads
    // nothing the steps write, and the Gate holds the rest until they are over.
    bool overlap = false;
    // The governor asks the main half for the copies (overlap_governor.h); the main half
    // reports their main-thread time (us) when it built them.
    bool    snapshot = false;
    int64_t copyUs   = 0;
    // The main half's own wait (the frame cap's sleep, us): idle time for the governor.
    int64_t idleUs   = 0;

    // ---- the RenderPipe shadow (COMPPROF=4 only) -------------------------
    bool     hashed = false;
    uint64_t hash   = 0;        // the lists and the state their items reference, at hand-off
};

// The second half, in retail's order (MainLoop::Frame tail -> MainLoop::Draw ->
// GameUpdateLoop::Draw from the context half of step 2 on). `Ops` supplies each
// step; the extension's Ops calls the engine, the unit test's records names.
// Steps 3, 4, 6 and pass 1 are the main half's; step 9 / 13 (the quicksave and
// quickload screenshots) never reach a packet: such a frame runs unpipelined.
// `Released` is not a retail step: it marks the end of what the sim may not
// overlap (RENDERPIPE_FENCE.md). Nothing after it reads sim, scene, UI or
// Renderer state -- only the device, the Context and MainLoop's timing fields.
// `Gate` is not a retail step either (P13_OVERLAP.md): with a snapshot on the
// render thread, the next Frame's steps may start during everything before it,
// and then the emit waits there until they are over; everything after it reads
// live UI, tool and save-load state.
template <class Ops>
void Emit(Ops& ops, const Packet& p) {
    ops.FrameTailTime(p);              // MainLoop::Frame: Context::_time, dirty 0x800
    if (!ops.BeginCommands()) {        // MainLoop::Draw: BeginCommands (the device lock)
        ops.BeginFailed();             //   DrawFailed, _contextDirty = 0, Context::Reset
        return;
    }
    ops.ClearResourcesIfDirty();       // MainLoop::Draw: _contextDirty
    ops.ViewportReconcile();           // MainLoop::Draw: the default buffer's extent
    ops.PacketBegin();                 // (harness: the packet's share of GameUpdateLoop::Draw)
    ops.RenderDocFrame();              // GameUpdateLoop::Draw: RenderDocIntegration::OnFrame
    ops.SetTime(p);                    // step 2
    ops.WeatherContext(p);             // step 5: the four floats, dirty 0x10800, SetFog
    ops.ForceSnow(p);                  // step 5: the snow diagnostic
    ops.Pass2(p);                      // step 7: Renderer::Draw pass 2
    ops.Gate(p);                       // (P13_OVERLAP: no step of the next Frame runs from here)
    ops.SaveLoadDraw(p);               // step 8
    ops.SetCamera(p);                  // step 10
    ops.ToolDraw(p);                   // step 11
    ops.UIDraw(p);                     // step 12
    ops.PacketEnd();
    ops.PostDraw();                    // MainLoop::Draw: the post-draw callbacks
    ops.Released(p);                   // (RENDERPIPE_FENCE: the fence may let the steps run from here)
    ops.EndCommands();                 // MainLoop::Draw: EndCommands (resolve)
    ops.FrameUpdateTime();             // MainLoop::Draw: _frameUpdateTime
    ops.IncFrameIndex();               // MainLoop::Draw: ++Context::_frameIndex
    ops.Finish();                      // MainLoop::Draw: Finish (Present, drops the lock)
}

}  // namespace RenderPipe
}  // namespace Ext
