#pragma once
// ============================================================================
// src/render/common/better_water/scene_depth_backend.h -- ext.better_water Fancy's contact foam: the
// scene depth the water pass reads (docs/extensions/handins/BETTERWATER.md section 8)
// ============================================================================
// NOT RETAIL. No graphics-API types: src/ext/better_water/better_water.cpp and the frame-copy seam
// include it (the ao/ao_backend.h, better_trees/foliage_aa_backend.h precedent). Every backend
// implements the four calls (src/render/<name>/); one that cannot answers Supported() == false and
// the rest are no-ops / null, so Fancy simply draws no contact foam there.
//
// WHY. Foam where the water meets a model (a pier, a bridge foot, a trading post's stone base, a
// boat) needs to know how far below the water surface the model behind a water pixel is. The frame
// copy's alpha (the shore depth the other passes read) is written only by the terrain and the
// OpaqueInWater models; an ordinary model writes 0 there. The scene's depth buffer has it for every
// model. The water draws with that depth bound for its own test, so the backend hands the water pass
// a COPY.
//
// WHEN. Retail makes its frame copy for the water once per frame, right before the water draws:
// Graphics::FrameCopyImageBuffer::Generate (wave23_lane_gfxbody.cpp), from MaterialInstance::
// GenerateTextures when the water material is set up. The seam there calls Copy() right after
// retail's colour resolve, on the drawing thread, inside the frame's command stream: the copy holds
// the opaque scene's depth as the water then sees it (everything drawn before the water). Generate
// also runs for the screenshot / save-thumbnail frame copies; a copy there is harmless.
//
// WHAT. The copy is single-sampled (a multisampled main depth is resolved: the nearest / first
// sample is enough, the foam is a soft band), the full size of the render target the water draws
// into, and holds the HARDWARE depth (0 near .. 1 far, D3D convention, no reverse Z: what
// SV_Position.z of the water pixel is compared against) in its first channel as a float, readable
// with Texture2D<float>.Load(int3(SV_Position.xy, 0)) -- no sampler is needed. It is exposed as an
// engine Video::ImageBuffer whose platform companion the backend fills, so the extension binds it
// with the engine's own Context::SetImageBuffer(image, 4) (pixel texture slot t4, which no water
// material uses: t0 water, t1 frame, t2 reflection, t3 highlight, t5 shadow) and the backend's own
// bind path does the rest.
// ============================================================================

namespace Video {
class ImageBuffer;
}

namespace SceneDepthBackend {

// The backend can make the copy on this device. Any thread; false before the device exists.
bool Supported();

// The extension wants the copy (Fancy with contact foam on). Main thread or drawing thread,
// idempotent; off releases what the copy holds at the backend's next convenient point. Until it is
// requested Copy() is a no-op and Image() is null, so Plain and the faithful path pay nothing.
void Request(bool on);

// The frame-copy seam (FrameCopyImageBuffer::Generate, drawing thread): copy the depth of the scene
// being drawn (the depth buffer bound for the current render target; the device's main depth) into
// the backend's copy. `context` is the Video::Context* Generate receives. Remakes the copy when the
// target's size or sample count changed (a window resize, an Options Apply, a device restart).
void Copy(void* context);

// The copy as an engine image buffer, valid from the Copy() of this frame until the next frame's;
// null when not requested, not supported, or no Copy() ran yet on the current device.
const Video::ImageBuffer* Image();

}  // namespace SceneDepthBackend
