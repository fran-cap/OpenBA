#pragma once
// ============================================================================
// src/render/dx11/depth_copy/depth_copy.h -- THE SHARED DEPTH COPY on this backend
// ============================================================================
// NOT RETAIL. The contract and the reuse rule: src/render/common/depth_copy/depth_copy_backend.h. The
// readers: the AO pass (ao/ao_pass.cpp), the cloud shadow (better_clouds/cloud_shadow.cpp) and the
// water's scene depth (better_water/scene_depth.cpp). Bodies: depth_copy.cpp.
//
// THE COPY is a view the readers Load as Texture2D<float> (the hardware depth, 0 near .. 1 far):
//   the main depth readable (AO's request, ao_backend.h RequestReadableDepth: its R24_UNORM_X8 SRV
//     exists)   AO's resolve draw (ao/ao_depth_resolve.h) from that SRV into a 1x R32_FLOAT target,
//     sample 0 of a multisampled depth, a plain copy of a single-sampled one: AO's old path;
//   otherwise   the depth behind the bound DSV copied whole (CopyResource into a typeless twin: a copy
//     reads its source through no view, so the depth stays bound as the DSV), then, multisampled, the
//     same resolve from the twin into R32_FLOAT, or, single-sampled, the twin itself through an
//     R24_UNORM_X8 view: the water's and the clouds' old path.
// Either way the values a reader loads are the ones its own copy held.
// ============================================================================

#include <d3d11.h>

#include <cstdint>

namespace Video {
class Context;
}

namespace VideoDX11 {
namespace DepthCopy {

// The readers, one bit each.
enum Reader : uint32_t {
    kReaderAo = 1u,
    kReaderSceneDepth = 2u,
    kReaderClouds = 4u,
    kReaderTerrain = 8u,   // ext.fancy_terrain's terrain light (fancy_terrain/terrain_light.cpp)
    kReaderDof = 16u,      // ext.depth_of_field's depth blur (dof/dof_pass.cpp), at the scene-draw seam
};

// A reader wants the copy, or no longer does. Any thread. With no reader left the copy's objects are
// released (under the device lock).
void Request(uint32_t reader, bool on);

// Which depth a reader copies: the main depth by name (the default render buffer's depth, readable
// only; the AO pass, as it always read it), or whatever depth is bound as the DSV (the water, the
// clouds, as they always copied it).
enum class Source {
    Main,
    Bound,
};

// At a seam of the drawing thread with the main colour and depth bound: the copy of the `source` depth
// as it stands, made now unless the one made earlier is still fresh (THE RULE). Every binding the copy
// touches is put back as it was. `context`: the engine context (its default render buffer names the
// main depth; its frame index stamps the copy). Null when there is nothing to copy (Main: the depth is
// not readable yet; Bound: no depth bound, an unknown format) or the objects could not be made (logged).
// The view stays valid until the next Acquire or Request(false).
ID3D11ShaderResourceView* Acquire(const Video::Context& context, Source source);

// The engine's draws (contextdata.cpp) and clears report here, on the drawing thread: a draw while the
// bound render state tests and writes depth (renderstatedata.cpp Bind records it), and every depth
// clear. Inline: a bool test per draw. (The extension passes that draw with their own D3D states never
// write depth; the engine's cache keeps the last Bind's state, which their StateBackup puts back.)
namespace Count {
inline bool g_stateWritesDepth = false;
inline uint64_t g_depthWrites = 0;
}  // namespace Count

inline void NoteDepthStateBound(bool writesDepth) {
    Count::g_stateWritesDepth = writesDepth;
}
inline void NoteDraw() {
    if (Count::g_stateWritesDepth) ++Count::g_depthWrites;
}
inline void NoteDepthClear() {
    ++Count::g_depthWrites;
}

}  // namespace DepthCopy
}  // namespace VideoDX11
