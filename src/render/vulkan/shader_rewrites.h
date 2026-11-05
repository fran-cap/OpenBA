#pragma once
// ============================================================================
// src/render/vulkan/shader_rewrites.h -- the DXBC rewrite stages on the Vulkan backend
// ============================================================================
// NOT RETAIL (P4). ext.better_trees' foliage_aa (FaaBackend) and billboard_shadow (BbsBackend), and
// ext.draw_batch's merged smoke runs (SmokeVsBackend), rewrite the retail programs' own DXBC
// (src/render/common/better_trees/, draw_batch/: pure byte work, unit-tested). On DX11 the rewritten copy
// becomes a second D3D shader (src/render/dx11/better_trees/, draw_batch/); here it goes through the same
// DXBC -> SPIR-V converter as every program (shader_convert.h, cached per user) into a second
// shader-registry entry per permutation, so the rewrites need no second port. The stages' rules are the
// DX11 ones, rule for rule:
//   - a program's copies are made at its load (PlatformCreate) when the stage is on, else at its first
//     bind; a mode / k / dither change (foliage_aa), an MSAA change (foliage_aa) or a parameter change
//     (billboard_shadow, debug overlay) re-makes them lazily; off releases them at the next bind;
//   - the vertex stage tries draw_batch's copy first, then billboard_shadow's (dx11 shaderprograms.cpp
//     VertexProgramData::Bind's order);
//   - foliage_aa with coverage (MSAA on) runs its copy only under a render state that does not blend,
//     and the draw then uses the A2C clone of that state: alpha to coverage, colour ONE / ZERO / ADD,
//     alpha ZERO / ZERO / ADD, every channel written (foliage_aa_stage.cpp CloneOf). Here the clone is
//     two bits of the pipeline key's render-state block (pipelines.h StateBlock::extFlags), applied by
//     the draw (draw.cpp), because a Vulkan pipeline is resolved at the draw, after both binds.
// A Bind resolves the copy the draw uses and keeps one registry reference on it until the next Bind or
// the program's destruction (the companion's `rewritten`), so a reset never frees what a recorded
// draw still names. Bodies: shader_rewrites.cpp, with the bodies of the three backend interfaces.
// ============================================================================

namespace VideoVulkan {

class VertexProgramData;
class PixelProgramData;
struct ShaderEntry;

namespace Rewrites {

// True while any of the three stages is requested (one relaxed load): the Bind slots ask the stages.
bool Active();

// VertexProgramData::Bind / PixelProgramData::Bind, after the permutation is chosen: the copy the draw
// uses for it (one registry reference, the caller's), or null for the retail program. `coverage`: the
// copy needs the A2C clone of a non-blending state (and must not run under a blending one). `noBloom`
// (issue #253): the same copy made without natural growth's blossom (one reference, the caller's), or null.
ShaderEntry* BindVertex(VertexProgramData* p, int permutation);
ShaderEntry* BindPixel(PixelProgramData* p, int permutation, bool* coverage, ShaderEntry** noBloom);

// Issue #253: false while the calling thread's draws can show no blossom (FaaBackend::SetDrawBloom): a
// draw then uses the pixel copy's `noBloom` twin when it has one (the same pixels, a shorter shader).
bool DrawBloom();

// PlatformCreate succeeded / the program is going away.
void VertexCreated(VertexProgramData* p);
void PixelCreated(PixelProgramData* p);
void VertexDestroyed(VertexProgramData* p);
void PixelDestroyed(PixelProgramData* p);

// DeviceData::Shutdown (device idle, before the pipelines): every copy is released; the next device
// makes them again.
void Shutdown();

}  // namespace Rewrites
}  // namespace VideoVulkan
