#pragma once
// ============================================================================
// src/ext/kept_render_state.h -- a render state an extension derives from a material's and keeps
// ============================================================================
// NOT RETAIL. Three rows draw with a copy of a loaded material's render state that differs in its depth
// fields: ext.terrain_late (the terrain's state with a "less" test), ext.show_grid and ext.map_overlays
// (the tile overlay's state with the depth test on and depth writes off). Each makes its copy once and
// keeps it.
//
// THE DEVICE. A video restart (a renderer change, a display change, a lost device) destroys every video
// External the factory knows and loads it again by path, in place (Engine::MainLoop::OnVideoError /
// OnVideoRestart). A copy made here has no path and the factory never sees it, so it kept the device
// half of the device that went away while its source came back at the same address: the next draw bound
// a state of a dead device, or of the other renderer backend (broken textures on everything drawn after
// the terrain once a building was selected; a crash in the other backend's Bind after a renderer
// change). OnVideoLost therefore destructs every kept state while its device still exists, as
// OnVideoError does to the factory's own, and the next request makes it again in the same storage.
//
// Threads: For runs on the drawing thread (ext.render_pipe's render thread when it is on); OnVideoLost
// runs from MainLoop::_extVideoLost, when no frame is being drawn (the other rows' OnVideoLost rely on
// the same).
// ============================================================================

namespace Video { class RenderState; }

namespace Ext {

class KeptRenderState {
public:
    // Fills `ours`, a freshly constructed state, from `source` (RenderState::Initialize).
    using InitFn = void (*)(Video::RenderState& ours, const Video::RenderState& source);

    KeptRenderState();

    // The kept state derived from `source` (not null), made by `init` when there is none, when the kept
    // one is of another source, or when a video restart dropped it. Null when no state could be made.
    const Video::RenderState* For(const Video::RenderState* source, InitFn init);

    // Engine::MainLoop::_extVideoLost (src/ext/registry_boot.cpp): every KeptRenderState of the process
    // releases its device half.
    static void OnVideoLost();

private:
    void Drop();

    const Video::RenderState* _source = nullptr;
    Video::RenderState*       _ours   = nullptr;   // the kept state, or its storage while _lost
    bool                      _lost   = false;     // _ours was destructed by Drop
    KeptRenderState*          _next   = nullptr;   // the process's list (never unlinked: statics)
};

}  // namespace Ext
