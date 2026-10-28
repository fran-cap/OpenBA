#pragma once
// ============================================================================
// src/render/vulkan/resources.h -- GPU memory, submission, the retire queue, uploads, samplers
// ============================================================================
// NOT RETAIL. docs/vulkan/PLAN.md §4.4 (memory, rings, retire pool). Bodies: resources.cpp.
//
// MEMORY. Every buffer and image is a VMA allocation (vk_mem.h). Static vertex / index buffers and
// textures live in device-local memory and are filled through the UPLOAD BATCH below; host-visible
// persistently mapped buffers serve the dynamic vertex / index buffers and the constant-buffer ring
// (draw.cpp). Every object made here is tracked: ResourcesShutdown destroys whatever is still alive
// before the device goes (the engine releases some companions after the device shut down), and a
// destroy of an object the shutdown already took is a no-op.
//
// SUBMISSION. One graphics queue, one timeline semaphore. Every vkQueueSubmit2 of the backend goes
// through SubmitCommands, under the queue lock, and signals the timeline with the next SERIAL; a
// serial is complete when the timeline's counter reached it. vkQueuePresentKHR takes the same lock.
// The 1.1 path (vulkan11.h) has no timeline: each submit is a vkQueueSubmit signalling a fence of its
// own, and a serial is complete when its fence is signalled (the fences are retired in serial order).
//
// THE UPLOAD BATCH. Creates and updates that need a copy (Create with initial data, SetData, a
// texture's levels, ImageBuffer::Update) are recorded into one open command buffer with its own
// staging memory, from any thread (the loader creates resources while the render thread draws).
// The render thread submits the open batch in Finish, before the frame's own commands, so a frame
// sees every resource made before it was submitted; a batch past 64 MB is submitted at once by the
// thread that filled it. A batch's staging memory is freed when its serial completed. Ordering: a
// global barrier opens and closes every batch (shader and vertex-input reads before, transfer writes
// after), so a copy over a buffer or image an earlier frame still reads waits for it, and every
// later submission sees the copy (submission order on one queue).
// NOT RETAIL, a divergence stated: D3D11 orders UpdateSubresource between the draws it sits
// between; here an update made mid-frame lands before the whole frame. The engine updates its
// dynamic textures (minimap, UI atlases) between frames; a mid-frame update shows one frame early.
//
// THE RETIRE QUEUE (PLAN.md §4.4, Metal's retire pool). A buffer, image or pipeline the engine
// releases may still be referenced by the frame being recorded or by frames in flight. Retire()
// tags the destroy with the index of the frame being recorded and runs it once that frame's submit
// completed (CollectRetired, every BeginCommands after the frame-slot wait); ResourcesShutdown runs
// the rest after the device went idle.
// ============================================================================
#include "vk_mem.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace VideoVulkan {

// ---- lifecycle ------------------------------------------------------------------------------------
// After vkCreateDevice: the allocator, the timeline, the placeholder resources. False with *why.
bool ResourcesStartup(std::string* why);
// The device is idle (Shutdown waited): runs every retired destroy, then destroys everything still
// alive (buffers, images, samplers, staging), the timeline and the allocator.
void ResourcesShutdown();

// ---- submission --------------------------------------------------------------------------------------
// Submits `cb` (may be null: a signal-only submit) waiting on `wait` (may be null), signalling the
// timeline with the next serial plus `signal` (a binary semaphore, may be null) and `fence` (may be
// null). Returns the serial, 0 when the submit failed (logged).
uint64_t SubmitCommands(VkCommandBuffer cb, const VkSemaphoreSubmitInfo* wait, VkSemaphore signal, VkFence fence);
// vkQueuePresentKHR under the queue lock.
VkResult PresentQueue(const VkPresentInfoKHR& info);
// The highest serial the GPU completed.
uint64_t CompletedSerial();
// Blocks until `serial` completed.
void WaitSerial(uint64_t serial);

// ---- frames and the retire queue --------------------------------------------------------------------
// The index of the frame being recorded (= frames submitted so far).
uint64_t FramesSubmitted();
// The number of frames whose submit completed: every frame index below it is done on the GPU.
uint64_t FramesCompleted();
// Finish: the frame just submitted carries `serial`.
void NoteFrameSubmitted(uint64_t serial);
// Runs every retired destroy whose frame completed. BeginCommands, after the frame-slot wait.
void CollectRetired();
// Runs `destroy` once the frame being recorded now has completed on the GPU. Any thread.
void Retire(std::function<void()> destroy);

// ---- buffers ---------------------------------------------------------------------------------------
struct GpuBuffer {
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation allocation = nullptr;
    uint8_t* mapped = nullptr;   // host-visible buffers only (persistently mapped)
    VkDeviceSize size = 0;
};

// A device-local buffer of `size` bytes, filled from `data` through the upload batch (zero-filled when
// `data` is null). TRANSFER_DST and TRANSFER_SRC are added to `usage`.
bool CreateStaticBuffer(VkDeviceSize size, VkBufferUsageFlags usage, const void* data, GpuBuffer* out,
                        std::string* why);
// A host-visible, persistently mapped buffer (coherent where the device offers it; the backend never
// flushes, so it asks for HOST_COHERENT). Contents undefined.
bool CreateHostBuffer(VkDeviceSize size, VkBufferUsageFlags usage, GpuBuffer* out, std::string* why);
// Copies `size` bytes of `data` into a static buffer at `offset`, through the upload batch.
void UploadBuffer(const GpuBuffer& dst, VkDeviceSize offset, const void* data, VkDeviceSize size);
// Reads `size` bytes of a buffer back into `dst`, synchronously (the pending batch is submitted and
// waited for). False when the buffer is not alive.
bool ReadBuffer(const GpuBuffer& src, void* dst, VkDeviceSize size);
// A host-visible, persistently mapped TRANSFER_DST buffer for an image read-back (P3, draw.cpp).
// Read it after the copy's serial completed and InvalidateBuffer.
bool CreateReadbackBuffer(VkDeviceSize size, GpuBuffer* out, std::string* why);
void InvalidateBuffer(const GpuBuffer& b);
// Destroys the buffer now (a no-op for an empty one or one the shutdown already took) and empties *b.
void DestroyBufferNow(GpuBuffer* b);
// Hands the buffer to the retire queue and empties *b.
void RetireBuffer(GpuBuffer* b);

// ---- images ----------------------------------------------------------------------------------------
struct ImageDesc {
    uint32_t width = 1;
    uint32_t height = 1;
    uint32_t mips = 1;
    uint32_t layers = 1;          // 6 for a cube
    bool cube = false;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkImageUsageFlags usage = 0;  // SAMPLED, TRANSFER_SRC and TRANSFER_DST are added unless attachmentOnly
    // P3, the backend's own main targets (draw.cpp): a multisampled image, extra create flags
    // (VK_IMAGE_CREATE_MULTISAMPLED_RENDER_TO_SINGLE_SAMPLED_BIT_EXT), and an attachment-only image
    // (`usage` exactly as given, no sampled view: `view` is the attachment view over every aspect).
    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
    VkImageCreateFlags flags = 0;
    bool attachmentOnly = false;
    // P4 (the AO pass's results texture): `layers` > 1 without `cube` gets a 2D-array view.
    bool array = false;
    VkComponentMapping swizzle{ VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
                                VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY };
};

struct GpuImage {
    VkImage image = VK_NULL_HANDLE;
    VmaAllocation allocation = nullptr;
    VkImageView view = VK_NULL_HANDLE;   // the sampled view (depth aspect only for a depth image)
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkImageAspectFlags aspect = 0;       // every aspect of the format
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t mips = 0;
    uint32_t layers = 0;
};

// One copy into an image: `offset` bytes into the staging blob, the subresource and its extent.
struct ImageRegion {
    VkDeviceSize offset = 0;
    uint32_t mip = 0;
    uint32_t layer = 0;
    uint32_t width = 0;
    uint32_t height = 0;
};

// The layout tracking of one image (P3, draw.cpp): the render targets, the backend's main targets and
// the acquired swap-chain image. `layout` is what the command stream leaves the image in at the point
// being recorded. Every image a companion owns is SHADER_READ_ONLY_OPTIMAL between frames (the frame's
// end puts back the render targets it used), which is what UploadImage / ClearImage assume. GENERAL is
// a pixel-stage storage image (P4: the AO pass's normals UAV).
struct ImageState {
    VkImage image = VK_NULL_HANDLE;
    VkImageAspectFlags aspect = 0;
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
};
// Appends the barrier that moves `state` to `layout` (stages and accesses derived from the two
// layouts; synchronization2, folded onto vkCmdPipelineBarrier on the 1.1 path) to `out`, and records the
// new layout. A move between two equal attachment or transfer-write layouts still emits a memory
// dependency (two passes writing one attachment); an equal read-only layout emits nothing.
void TransitionImage(ImageState& state, VkImageLayout layout, std::vector<VkImageMemoryBarrier2>* out);
// Issues the collected barriers in one vkCmdPipelineBarrier2 and empties the list.
void FlushBarriers(VkCommandBuffer cb, std::vector<VkImageMemoryBarrier2>* barriers);

// Makes the image and its view. Its contents are undefined until UploadImage or ClearImage.
bool CreateImage(const ImageDesc& desc, GpuImage* out, std::string* why);
// Copies `regions` out of `data` (`size` bytes) into the image through the upload batch; the
// subresources the regions name end in SHADER_READ_ONLY_OPTIMAL. Every other subresource keeps what
// it had (the whole image is transitioned, so call ClearImage first on a fresh partial upload).
void UploadImage(const GpuImage& image, const ImageRegion* regions, size_t regionCount, const void* data,
                 size_t size);
// Clears every subresource (colour 0, depth 1, stencil 0) through the upload batch and leaves the
// image in SHADER_READ_ONLY_OPTIMAL. Every image is created through one of the two, so a Bind always
// finds it readable.
void ClearImage(const GpuImage& image);
void DestroyImageNow(GpuImage* image);
void RetireImage(GpuImage* image);

// True when `format` can be sampled with linear filtering in optimal tiling on this device.
bool FormatSampleable(VkFormat format);

// ---- samplers --------------------------------------------------------------------------------------
// Samplers are shared: one VkSampler per distinct description (a device allows only a few thousand),
// made on first use and destroyed at shutdown.
struct SamplerDesc {
    uint8_t magLinear = 0;
    uint8_t minLinear = 0;
    uint8_t mipLinear = 0;
    uint8_t compare = 0;          // a shadow-map comparison sampler (LESS)
    uint8_t addressU = 0;         // VkSamplerAddressMode
    uint8_t addressV = 0;
    uint8_t addressW = 0;
    uint8_t anisotropy = 0;       // 0 = off, else the max anisotropy
    float maxLod = 0.0f;
};
VkSampler GetSampler(const SamplerDesc& desc);

// ---- placeholders (PLAN.md §4.3: never a null descriptor) ----------------------------------------------
VkImageView WhiteImageView();        // 1x1 RGBA8 white, 2D
VkImageView WhiteCubeView();         // 1x1 RGBA8 white, cube
VkSampler DefaultSampler();          // linear, wrap
const GpuBuffer& ZeroBuffer();       // 64 KB of zeros: an unbound vertex stream, an unbound constant buffer

// ---- the upload batch ----------------------------------------------------------------------------------
// Submits the open batch (if any). The render thread calls it in Finish before the frame's submit.
void FlushUploads();
// Counters for the log line: bytes uploaded and batches submitted since the last call.
void TakeUploadStats(uint64_t* bytes, uint32_t* batches);

}  // namespace VideoVulkan
