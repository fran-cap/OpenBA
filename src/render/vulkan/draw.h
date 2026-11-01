#pragma once
// ============================================================================
// src/render/vulkan/draw.h -- the frame's pass, the bound state, descriptors and draws
// ============================================================================
// NOT RETAIL. docs/vulkan/PLAN.md §4.3 / §4.4. Bodies: draw.cpp, which also holds the ContextData
// slots that record work (Clear, the Bind* slots, DrawIndexed, the dynamic-vertex slots); the frame
// slots (BeginCommands / EndCommands / Finish) stay in device.cpp and call the frame hooks below.
//
// THE BOUND STATE mirrors a D3D11 immediate context: the engine's BindResources binds only what is
// dirty, so every binding below persists across draws and frames until the engine replaces it. The
// companions' Bind slots record into it (Draw* calls); a draw resolves it into a pipeline, two
// descriptor sets and the vertex / index buffers.
//
// THE PASS (§4.4): dynamic rendering on the bound render buffer's attachments, begun lazily at the
// first draw; a Clear only records intent, so it becomes the pass's LOAD_OP_CLEAR (a Clear after draws
// ends the pass and the next draw begins a new one: no mid-pass clear is emitted). A clear nothing draws
// after still lands: a target change, a Resolve, a read-back or the frame's end runs it as an empty pass.
//
// TARGETS (P3). A render buffer's colour and depth are either the MAIN targets (the default render
// buffer's two images, and every other ImageBuffer AttachColorBuffer / AttachDepthBuffer aliased to
// them, as FrameCopyImageBuffer's) or a companion's own render-buffer image (ImageBufferData:
// attachment view + layout state). The main targets are the backend's:
//   * 1x: the acquired swap-chain image and the backend's depth image (P2's pass);
//   * MSAA (Configuration::multisample, the dx11 ResolveMultisampleMode index), resolve-attachment path:
//     a multisampled colour image resolved into the swap-chain image at the end of EVERY main pass, and
//     a multisampled depth image. Retail resolves its multisampled colour into the back buffer once a
//     frame (EndCommands); here each pass resolves, so the swap-chain image always holds the resolved
//     frame so far, which is what the frame copy reads. The multisampled images are stored (not
//     transient): the main target is split into several passes a frame (the frame copy, read-backs),
//     and a later pass loads what the earlier one drew;
//   * MSAA where the device has VK_EXT_multisampled_render_to_single_sampled (the S25): a single-sampled
//     colour image and depth image made for it, rendered with the tiler's own MSAA (resolved on the
//     tile), and the colour copied to the swap-chain image at the frame's end.
// Every other render buffer is single-sampled, as retail's textures are.
//
// LAYOUTS (§4.4): every tracked image (resources.h ImageState) is moved with sync2 barriers at pass
// boundaries only: to an attachment layout when a pass begins on it, to SHADER_READ_ONLY when a draw
// samples it, to TRANSFER_SRC / DST for a Resolve or a read-back. The frame's end moves every companion
// image it touched back to SHADER_READ_ONLY and the swap-chain image to PRESENT_SRC. A texture that is
// an attachment of the bound render buffer reads the placeholder (D3D11 unbinds such an SRV).
//
// CONSTANT BUFFERS: a per-frame-slot ring of host-visible chunks; each constant buffer's bytes are
// copied in at the first draw that reads them after a Bind (or in a new frame), at a 256-byte-or-
// device-aligned offset, and bound as a dynamic offset (pipelines.h). DESCRIPTOR SETS come from a
// per-frame-slot pool, reset when the slot comes round again, and are reused within the frame for
// the same contents.
// ============================================================================
#include "vk_api.h"

#include <cstdint>
#include <string>

namespace Video {
class Context;
class RenderBuffer;
}  // namespace Video

namespace VideoVulkan {

class VertexBufferData;
class IndexBufferData;
class VertexLayoutData;
class VertexProgramData;
class PixelProgramData;
class RenderStateData;
class ImageBufferData;
class ConstantBufferData;

// ---- lifecycle (device.cpp) ----------------------------------------------------------------------
bool DrawStartup(std::string* why);
void DrawShutdown();   // device idle

// ---- the frame (device.cpp) ------------------------------------------------------------------------
struct FrameTarget {
    VkCommandBuffer cb = VK_NULL_HANDLE;
    unsigned slot = 0;                  // the frame-in-flight slot (its fence has been waited for)
    VkImage image = VK_NULL_HANDLE;     // the acquired swap-chain image (pre-rotated: the landscape source)
    VkImageView view = VK_NULL_HANDLE;
    VkExtent2D extent{};
    // A pre-rotated swap chain (present.h): the acquired swap-chain image the frame's end turns `image`
    // into. Null otherwise: `image` is the swap-chain image itself.
    VkImage presentImage = VK_NULL_HANDLE;
    VkImageView presentView = VK_NULL_HANDLE;
    VkExtent2D presentExtent{};
};
// BeginCommands, after the slot's fence wait and the acquire: resets the slot's ring and pools.
void DrawBeginFrame(const FrameTarget& target);
// Finish, before vkEndCommandBuffer: ends the pass (running one for a frame that drew nothing, so its
// clear lands), runs the present pass when the swap chain is pre-rotated, and moves the swap-chain
// image to PRESENT_SRC.
void DrawEndFrame();
// The periodic frame line: the engine's counters of this frame plus the backend's own.
void DrawLogFrame(uint64_t frame, const Video::Context* context);
// The swap chain was rebuilt or the device is going: the next pass remakes the depth image.
void DrawTargetsChanged();

// ---- the companions' Bind slots ----------------------------------------------------------------------
void DrawSetVertexStream(uint32_t slot, const VertexBufferData* vb, uint32_t offset, uint32_t stride);
void DrawSetIndexBuffer(const IndexBufferData* ib);
void DrawSetLayout(const VertexLayoutData* layout);
void DrawSetVertexProgram(const VertexProgramData* program);
void DrawSetPixelProgram(const PixelProgramData* program);
void DrawSetRenderState(const RenderStateData* state);
void DrawSetTexture(uint32_t slot, const ImageBufferData* image);
void DrawSetConstants(const ConstantBufferData* cb, bool vertexStage, uint32_t slot);
void DrawSetRenderBuffer(const Video::RenderBuffer* renderBuffer);
// A companion is being destroyed: every binding that names it is dropped (D3D11 holds a reference on
// what is bound; here the binding just falls back to the placeholder). A render buffer's companion
// passes its owner (the bound target is the Video::RenderBuffer).
void DrawForget(const void* companion);
// An image companion is going away or replacing its image: a pass rendering into it ends, and the
// frame's end no longer moves its state back.
void DrawForgetImage(const ImageBufferData* image);

// ---- P3: copies and read-backs ---------------------------------------------------------------------------
// RenderBufferData::Resolve: `src`'s colour into `dst`'s, in the frame's command stream.
void DrawResolve(const Video::RenderBuffer* dst, const Video::RenderBuffer* src);
// ImageBufferData::Read: mip 0 of the image into `dst`, `width` x `height` texels, rows `width` texels
// apart. Synchronous: inside a frame the frame's commands so far are submitted first (SubmitFrameSoFar),
// outside one a command buffer of its own is submitted and waited for. False for a format the read
// cannot express (compressed, depth) or a size past the image's.
bool DrawReadImage(ImageBufferData* image, void* dst, int width, int height);

// ---- P4: ext.ambient_occlusion's pass (ao/assao_pass.cpp) -------------------------------------------
// THE READABLE DEPTH (any thread): while any reader asks for it, the main targets are made with the
// depth image sampleable too (SAMPLED usage and a depth-aspect view beside the attachment view), the
// dx11 R24G8_TYPELESS + SRV choice; with none, retail's attachment-only depth. Two readers: the AO pass
// (AoBackend::RequestReadableDepth) and ext.better_water's scene depth copy (better_water/
// scene_depth.cpp); each sets or clears its own bit. The request is taken when a frame begins: a change
// remakes the main targets at the next frame's first pass, as a sample-count change does (never between
// two passes of one frame, which would drop the depth and colour drawn so far). Returns true when this
// reader's request changed. DrawDepthReadable: what exists now.
enum DepthReader : uint32_t {
    kDepthReaderAo = 1u,
    kDepthReaderSceneDepth = 2u,
};
bool DrawRequestReadableDepth(uint32_t reader, bool on);
bool DrawDepthReadable();
// The index of the frame being recorded (FramesSubmitted when it began); ~0 outside a frame.
uint64_t DrawFrameIndex();
// What the AO pass draws with: the frame's command buffer and slot, the main depth's sampled view (in
// SHADER_READ_ONLY after DrawAoBegin) and the main targets' shape.
struct AoTargets {
    VkCommandBuffer cb = VK_NULL_HANDLE;
    unsigned slot = 0;
    uint64_t frame = 0;
    VkImageView depthView = VK_NULL_HANDLE;
    VkExtent2D extent{};
    uint32_t samples = 1;
    bool msrtss = false;
    VkFormat colorFormat = VK_FORMAT_UNDEFINED;
};
// Renderer::Draw's AO seam, on the recording thread inside a frame: the pass in progress ends (its
// clears land) and the main depth moves to SHADER_READ_ONLY. ext.better_water's scene depth copy uses
// the same bracket (DrawAoBegin, its own pass, DrawAoEnd). False, nothing done, outside a frame or
// while the depth is not readable (the frames between the request and the remake).
bool DrawAoBegin(AoTargets* out);
// A pass on the main colour alone (LOAD, no depth; resolved like every main pass), for the AO apply.
bool DrawAoBeginApply();
// After the AO pass: its pass ends and the command buffer's bound pipeline, sets, viewport, scissor and
// vertex buffers are forgotten, so the next engine draw binds everything again (dx11's StateBackup).
void DrawAoEnd();

}  // namespace VideoVulkan
