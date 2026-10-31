#pragma once
// ============================================================================
// src/render/vulkan/vulkan11.h -- THE 1.1 PATH: the backend on a device without Vulkan 1.3
// ============================================================================
// NOT RETAIL. docs/vulkan/PLAN.md §4.1. The backend is written against Vulkan 1.3 (dynamic rendering,
// synchronization2, timeline semaphores, dynamic vertex strides). A device that lacks any of them (the
// Galaxy S10's Adreno 640 driver reports 1.1.128 and has none of their extensions) runs the same code
// through the wrappers below, chosen once at CreateDevice from the device's caps, never by a flag or a
// platform. With the 1.3 path every wrapper is the 1.3 call itself behind one predictable branch.
//
// What the 1.1 path does instead, per 1.3 feature:
//   dynamic rendering   a VkRenderPass per attachment set (formats, samples, load / store ops, layouts;
//                       cached for the device's life) and a VkFramebuffer per (pass, views, size), cached
//                       for as long as its views live: every vkDestroyImageView is preceded by
//                       ForgetImageView, which destroys the framebuffers of that view (a view's handle can
//                       be reused once it is destroyed). The Adreno driver backs each framebuffer with
//                       kernel memory, so making them per frame cost ~1.5 ms a frame (P13_RENDER.md). The
//                       attachments are already in their layouts (the callers' barriers), so every
//                       attachment's initial, subpass and final layout is the one VkRenderingInfo names:
//                       the pass makes no transition. A pipeline is made against a pass of the same
//                       formats and sample count (render-pass compatibility; a single subpass ignores the
//                       resolve attachments).
//   synchronization2    vkCmdPipelineBarrier: the barriers' stages ORed into one source and one
//                       destination mask (an over-wide wait, never a missing one), the 64-bit stage and
//                       access bits folded onto their 1.0 equivalents (copy -> transfer, sampled / storage
//                       read -> shader read, ...). vkQueueSubmit for vkQueueSubmit2 (resources.cpp).
//   timeline semaphore  a fence per submit (resources.cpp SUBMISSION): a serial is complete when its
//                       fence is signalled.
//   dynamic strides     the vertex strides are part of the pipeline key (pipelines.h PipelineKey::strides)
//                       and of the pipeline's binding descriptions; vkCmdBindVertexBuffers binds.
//   SPIR-V 1.6          every module is rewritten to SPIR-V 1.3 before vkCreateShaderModule
//                       (spirv_legalize.h DowngradeSpirv).
// Left out on the 1.1 path: ext.ambient_occlusion (its apply draws inside the main pass with a pipeline
// made for colour alone) and the tiler MSAA (VK_EXT_multisampled_render_to_single_sampled).
// ============================================================================
#include "vk_api.h"

#include <cstdint>

namespace VideoVulkan {

namespace Vulkan11Detail {
extern bool g_active;
void BeginRendering(VkCommandBuffer cb, const VkRenderingInfo& info, const VkFormat* colorFormats,
                    VkFormat depthFormat, VkSampleCountFlagBits samples);
void PipelineBarrier(VkCommandBuffer cb, const VkDependencyInfo& dependency);
void ForgetView(VkImageView view);
}  // namespace Vulkan11Detail

// True on the 1.1 path (set by CreateDevice before anything is recorded or built).
inline bool Vulkan11Path() {
    return Vulkan11Detail::g_active;
}
void SetVulkan11Path(bool on);

// Called before every vkDestroyImageView: on the 1.1 path the cached framebuffers that use `view` are
// destroyed with it (the GPU is done with them whenever it is done with the view: they are used only in
// the commands that use the view). Nothing on the 1.3 path.
inline void ForgetImageView(VkImageView view) {
    if (Vulkan11Path()) Vulkan11Detail::ForgetView(view);
}

// vkCmdBeginRendering. The 1.1 path needs what a VkRenderPass is made of and VkRenderingInfo does not
// carry: the colour attachments' formats (one per info.colorAttachmentCount; a resolve attachment has its
// colour's format), the depth-stencil format (VK_FORMAT_UNDEFINED without one) and the sample count.
inline void CmdBeginRendering(VkCommandBuffer cb, const VkRenderingInfo& info, const VkFormat* colorFormats,
                              VkFormat depthFormat, VkSampleCountFlagBits samples) {
    if (Vulkan11Path()) {
        Vulkan11Detail::BeginRendering(cb, info, colorFormats, depthFormat, samples);
        return;
    }
    vkCmdBeginRendering(cb, &info);
}

inline void CmdEndRendering(VkCommandBuffer cb) {
    if (Vulkan11Path()) {
        vkCmdEndRenderPass(cb);
        return;
    }
    vkCmdEndRendering(cb);
}

inline void CmdPipelineBarrier2(VkCommandBuffer cb, const VkDependencyInfo& dependency) {
    if (Vulkan11Path()) {
        Vulkan11Detail::PipelineBarrier(cb, dependency);
        return;
    }
    vkCmdPipelineBarrier2(cb, &dependency);
}

// vkCmdBindVertexBuffers2 with dynamic strides; on the 1.1 path the strides are the pipeline's.
inline void CmdBindVertexBuffers2(VkCommandBuffer cb, uint32_t first, uint32_t count, const VkBuffer* buffers,
                                  const VkDeviceSize* offsets, const VkDeviceSize* strides) {
    if (Vulkan11Path()) {
        vkCmdBindVertexBuffers(cb, first, count, buffers, offsets);
        return;
    }
    vkCmdBindVertexBuffers2(cb, first, count, buffers, offsets, nullptr, strides);
}

// The attachments `ci` renders to: `rendering` chained on the 1.3 path, the compatible render pass on
// the 1.1 path (ci->pMultisampleState gives the sample count; set it first).
void SetPipelineTargets(VkGraphicsPipelineCreateInfo* ci, const VkPipelineRenderingCreateInfo* rendering);

// The 1.1 path's 1.0 equivalents of synchronization2's stage and access bits (exposed for the submit's
// wait stage). `source` picks what an empty stage mask means (TOP_OF_PIPE / BOTTOM_OF_PIPE).
VkPipelineStageFlags StageFlags1(VkPipelineStageFlags2 stages, bool source);
VkAccessFlags AccessFlags1(VkAccessFlags2 access);

// Destroys the render passes and the framebuffers still cached. DeviceData::Shutdown, after
// ResourcesShutdown (the device is idle).
void Vulkan11Shutdown();

}  // namespace VideoVulkan
