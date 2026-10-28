// ============================================================================
// src/render/vulkan/vulkan11.cpp -- THE 1.1 PATH (vulkan11.h)
// ============================================================================
// NOT RETAIL. vulkan11.h has the design: render passes and framebuffers for dynamic rendering, the
// synchronization2 barriers folded onto vkCmdPipelineBarrier.
// ============================================================================
#include "vulkan11.h"

#include "device_objects.h"
#include "resources.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <vector>

namespace VideoVulkan {

namespace Vulkan11Detail {
bool g_active = false;
}  // namespace Vulkan11Detail

namespace {

constexpr uint32_t kMaxColors = 4;
constexpr uint32_t kMaxAttachments = 2 * kMaxColors + 1;   // colours, their resolves, depth-stencil

// One render pass: the attachments in the order colours, resolves (of the colours that have one, in
// colour order), depth-stencil. Plain words: compared with memcmp.
struct PassKey {
    uint32_t colorCount = 0;
    uint32_t samples = 1;
    uint32_t colorFormats[kMaxColors] = {};   // VK_FORMAT_UNDEFINED: an unused colour slot
    uint32_t colorLayouts[kMaxColors] = {};
    uint32_t colorLoad[kMaxColors] = {};
    uint32_t colorStore[kMaxColors] = {};
    uint32_t resolveLayouts[kMaxColors] = {};   // VK_IMAGE_LAYOUT_UNDEFINED: no resolve attachment
    uint32_t depthFormat = 0;                   // VK_FORMAT_UNDEFINED: no depth-stencil attachment
    uint32_t depthLayout = 0;
    uint32_t depthLoad = 0;
    uint32_t depthStore = 0;
    uint32_t stencilLoad = 0;
    uint32_t stencilStore = 0;
};

struct FramebufferKey {
    VkRenderPass pass = VK_NULL_HANDLE;
    VkImageView views[kMaxAttachments] = {};
    uint32_t width = 0;
    uint32_t height = 0;
};

std::mutex g_lock;   // the pipeline builds (the warm-up worker) and the recording thread share the passes
std::vector<std::pair<PassKey, VkRenderPass>> g_passes;
std::vector<std::pair<FramebufferKey, VkFramebuffer>> g_framebuffers;   // the frame being recorded's
uint64_t g_framebufferFrame = ~0ull;

// g_lock held. The pass of `key`, made on first use; VK_NULL_HANDLE when vkCreateRenderPass fails (logged).
VkRenderPass PassForLocked(const PassKey& key) {
    for (const auto& entry : g_passes) {
        if (std::memcmp(&entry.first, &key, sizeof(PassKey)) == 0) return entry.second;
    }
    VkAttachmentDescription attachments[kMaxAttachments] = {};
    VkAttachmentReference colors[kMaxColors] = {};
    VkAttachmentReference resolves[kMaxColors] = {};
    VkAttachmentReference depth{};
    uint32_t n = 0;
    bool anyResolve = false;
    for (uint32_t i = 0; i < key.colorCount; ++i) {
        colors[i] = { VK_ATTACHMENT_UNUSED, VK_IMAGE_LAYOUT_UNDEFINED };
        resolves[i] = { VK_ATTACHMENT_UNUSED, VK_IMAGE_LAYOUT_UNDEFINED };
        if (key.colorFormats[i] == VK_FORMAT_UNDEFINED) continue;
        VkAttachmentDescription& a = attachments[n];
        a.format = static_cast<VkFormat>(key.colorFormats[i]);
        a.samples = static_cast<VkSampleCountFlagBits>(key.samples);
        a.loadOp = static_cast<VkAttachmentLoadOp>(key.colorLoad[i]);
        a.storeOp = static_cast<VkAttachmentStoreOp>(key.colorStore[i]);
        a.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        a.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        a.initialLayout = static_cast<VkImageLayout>(key.colorLayouts[i]);
        a.finalLayout = a.initialLayout;
        colors[i] = { n, a.initialLayout };
        ++n;
    }
    for (uint32_t i = 0; i < key.colorCount; ++i) {
        if (key.resolveLayouts[i] == VK_IMAGE_LAYOUT_UNDEFINED) continue;
        VkAttachmentDescription& a = attachments[n];
        a.format = static_cast<VkFormat>(key.colorFormats[i]);
        a.samples = VK_SAMPLE_COUNT_1_BIT;
        a.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        a.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        a.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        a.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        a.initialLayout = static_cast<VkImageLayout>(key.resolveLayouts[i]);
        a.finalLayout = a.initialLayout;
        resolves[i] = { n, a.initialLayout };
        anyResolve = true;
        ++n;
    }
    if (key.depthFormat != VK_FORMAT_UNDEFINED) {
        VkAttachmentDescription& a = attachments[n];
        a.format = static_cast<VkFormat>(key.depthFormat);
        a.samples = static_cast<VkSampleCountFlagBits>(key.samples);
        a.loadOp = static_cast<VkAttachmentLoadOp>(key.depthLoad);
        a.storeOp = static_cast<VkAttachmentStoreOp>(key.depthStore);
        a.stencilLoadOp = static_cast<VkAttachmentLoadOp>(key.stencilLoad);
        a.stencilStoreOp = static_cast<VkAttachmentStoreOp>(key.stencilStore);
        a.initialLayout = static_cast<VkImageLayout>(key.depthLayout);
        a.finalLayout = a.initialLayout;
        depth = { n, a.initialLayout };
        ++n;
    }
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = key.colorCount;
    subpass.pColorAttachments = key.colorCount ? colors : nullptr;
    subpass.pResolveAttachments = anyResolve ? resolves : nullptr;
    subpass.pDepthStencilAttachment = key.depthFormat != VK_FORMAT_UNDEFINED ? &depth : nullptr;
    auto ci = Make<VkRenderPassCreateInfo>(VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO);
    ci.attachmentCount = n;
    ci.pAttachments = attachments;
    ci.subpassCount = 1;
    ci.pSubpasses = &subpass;
    VkRenderPass pass = VK_NULL_HANDLE;
    const VkResult r = vkCreateRenderPass(LiveDevice(), &ci, nullptr, &pass);
    if (r != VK_SUCCESS) {
        std::fprintf(stderr, "[Vulkan] 1.1 path: vkCreateRenderPass: %s\n", ResultName(r));
        return VK_NULL_HANDLE;
    }
    g_passes.emplace_back(key, pass);
    return pass;
}

// g_lock held. The frame's framebuffer of `key`; a new one is retired with the frame being recorded.
VkFramebuffer FramebufferForLocked(const FramebufferKey& key, uint32_t viewCount) {
    const uint64_t frame = FramesSubmitted();
    if (frame != g_framebufferFrame) {
        g_framebuffers.clear();   // their destroys are queued already
        g_framebufferFrame = frame;
    }
    for (const auto& entry : g_framebuffers) {
        if (std::memcmp(&entry.first, &key, sizeof(FramebufferKey)) == 0) return entry.second;
    }
    auto ci = Make<VkFramebufferCreateInfo>(VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO);
    ci.renderPass = key.pass;
    ci.attachmentCount = viewCount;
    ci.pAttachments = key.views;
    ci.width = key.width;
    ci.height = key.height;
    ci.layers = 1;
    VkFramebuffer framebuffer = VK_NULL_HANDLE;
    const VkResult r = vkCreateFramebuffer(LiveDevice(), &ci, nullptr, &framebuffer);
    if (r != VK_SUCCESS) {
        std::fprintf(stderr, "[Vulkan] 1.1 path: vkCreateFramebuffer: %s\n", ResultName(r));
        return VK_NULL_HANDLE;
    }
    g_framebuffers.emplace_back(key, framebuffer);
    Retire([framebuffer]() {
        vkDestroyFramebuffer(LiveDevice(), framebuffer, nullptr);
    });
    return framebuffer;
}

}  // namespace

void SetVulkan11Path(bool on) {
    Vulkan11Detail::g_active = on;
}

VkPipelineStageFlags StageFlags1(VkPipelineStageFlags2 stages, bool source) {
    auto out = static_cast<VkPipelineStageFlags>(stages & 0xFFFFFFFFull);
    if (stages & (VK_PIPELINE_STAGE_2_COPY_BIT | VK_PIPELINE_STAGE_2_RESOLVE_BIT |
                  VK_PIPELINE_STAGE_2_BLIT_BIT | VK_PIPELINE_STAGE_2_CLEAR_BIT)) {
        out |= VK_PIPELINE_STAGE_TRANSFER_BIT;
    }
    if (stages & (VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT | VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT)) {
        out |= VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
    }
    if (stages & VK_PIPELINE_STAGE_2_PRE_RASTERIZATION_SHADERS_BIT) out |= VK_PIPELINE_STAGE_VERTEX_SHADER_BIT;
    if (out == 0) out = source ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT : VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    return out;
}

VkAccessFlags AccessFlags1(VkAccessFlags2 access) {
    auto out = static_cast<VkAccessFlags>(access & 0xFFFFFFFFull);
    if (access & (VK_ACCESS_2_SHADER_SAMPLED_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT)) {
        out |= VK_ACCESS_SHADER_READ_BIT;
    }
    if (access & VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT) out |= VK_ACCESS_SHADER_WRITE_BIT;
    return out;
}

void Vulkan11Detail::BeginRendering(VkCommandBuffer cb, const VkRenderingInfo& info,
                                    const VkFormat* colorFormats, VkFormat depthFormat,
                                    VkSampleCountFlagBits samples) {
    PassKey key;
    FramebufferKey fb;
    VkClearValue clears[kMaxAttachments] = {};
    uint32_t n = 0;
    key.colorCount = std::min(info.colorAttachmentCount, kMaxColors);
    key.samples = samples;
    for (uint32_t i = 0; i < key.colorCount; ++i) {
        const VkRenderingAttachmentInfo& a = info.pColorAttachments[i];
        if (!a.imageView) continue;
        key.colorFormats[i] = colorFormats[i];
        key.colorLayouts[i] = a.imageLayout;
        key.colorLoad[i] = a.loadOp;
        key.colorStore[i] = a.storeOp;
        fb.views[n] = a.imageView;
        clears[n] = a.clearValue;
        ++n;
    }
    for (uint32_t i = 0; i < key.colorCount; ++i) {
        const VkRenderingAttachmentInfo& a = info.pColorAttachments[i];
        if (!a.imageView || a.resolveMode == VK_RESOLVE_MODE_NONE || !a.resolveImageView) continue;
        key.resolveLayouts[i] = a.resolveImageLayout;
        fb.views[n] = a.resolveImageView;
        ++n;
    }
    const VkRenderingAttachmentInfo* d = info.pDepthAttachment;
    const VkRenderingAttachmentInfo* s = info.pStencilAttachment;
    const VkRenderingAttachmentInfo* ds = (d && d->imageView) ? d : s;
    if (ds && ds->imageView) {
        key.depthFormat = depthFormat;
        key.depthLayout = ds->imageLayout;
        key.depthLoad = (d && d->imageView) ? d->loadOp : VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        key.depthStore = (d && d->imageView) ? d->storeOp : VK_ATTACHMENT_STORE_OP_DONT_CARE;
        key.stencilLoad = (s && s->imageView) ? s->loadOp : VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        key.stencilStore = (s && s->imageView) ? s->storeOp : VK_ATTACHMENT_STORE_OP_DONT_CARE;
        fb.views[n] = ds->imageView;
        clears[n].depthStencil.depth = (d && d->imageView) ? d->clearValue.depthStencil.depth : 1.0f;
        clears[n].depthStencil.stencil = (s && s->imageView) ? s->clearValue.depthStencil.stencil : 0;
        ++n;
    }
    fb.width = static_cast<uint32_t>(info.renderArea.offset.x) + info.renderArea.extent.width;
    fb.height = static_cast<uint32_t>(info.renderArea.offset.y) + info.renderArea.extent.height;
    VkFramebuffer framebuffer = VK_NULL_HANDLE;
    {
        std::lock_guard<std::mutex> guard(g_lock);
        fb.pass = PassForLocked(key);
        if (fb.pass) framebuffer = FramebufferForLocked(fb, n);
    }
    auto bi = Make<VkRenderPassBeginInfo>(VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO);
    bi.renderPass = fb.pass;
    bi.framebuffer = framebuffer;
    bi.renderArea = info.renderArea;
    bi.clearValueCount = n;
    bi.pClearValues = clears;
    vkCmdBeginRenderPass(cb, &bi, VK_SUBPASS_CONTENTS_INLINE);
}

void Vulkan11Detail::PipelineBarrier(VkCommandBuffer cb, const VkDependencyInfo& dependency) {
    thread_local std::vector<VkMemoryBarrier> memory;
    thread_local std::vector<VkBufferMemoryBarrier> buffers;
    thread_local std::vector<VkImageMemoryBarrier> images;
    memory.clear();
    buffers.clear();
    images.clear();
    VkPipelineStageFlags2 src = 0;
    VkPipelineStageFlags2 dst = 0;
    for (uint32_t i = 0; i < dependency.memoryBarrierCount; ++i) {
        const VkMemoryBarrier2& b = dependency.pMemoryBarriers[i];
        auto m = Make<VkMemoryBarrier>(VK_STRUCTURE_TYPE_MEMORY_BARRIER);
        m.srcAccessMask = AccessFlags1(b.srcAccessMask);
        m.dstAccessMask = AccessFlags1(b.dstAccessMask);
        memory.push_back(m);
        src |= b.srcStageMask;
        dst |= b.dstStageMask;
    }
    for (uint32_t i = 0; i < dependency.bufferMemoryBarrierCount; ++i) {
        const VkBufferMemoryBarrier2& b = dependency.pBufferMemoryBarriers[i];
        auto m = Make<VkBufferMemoryBarrier>(VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER);
        m.srcAccessMask = AccessFlags1(b.srcAccessMask);
        m.dstAccessMask = AccessFlags1(b.dstAccessMask);
        m.srcQueueFamilyIndex = b.srcQueueFamilyIndex;
        m.dstQueueFamilyIndex = b.dstQueueFamilyIndex;
        m.buffer = b.buffer;
        m.offset = b.offset;
        m.size = b.size;
        buffers.push_back(m);
        src |= b.srcStageMask;
        dst |= b.dstStageMask;
    }
    for (uint32_t i = 0; i < dependency.imageMemoryBarrierCount; ++i) {
        const VkImageMemoryBarrier2& b = dependency.pImageMemoryBarriers[i];
        auto m = Make<VkImageMemoryBarrier>(VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER);
        m.srcAccessMask = AccessFlags1(b.srcAccessMask);
        m.dstAccessMask = AccessFlags1(b.dstAccessMask);
        m.oldLayout = b.oldLayout;
        m.newLayout = b.newLayout;
        m.srcQueueFamilyIndex = b.srcQueueFamilyIndex;
        m.dstQueueFamilyIndex = b.dstQueueFamilyIndex;
        m.image = b.image;
        m.subresourceRange = b.subresourceRange;
        images.push_back(m);
        src |= b.srcStageMask;
        dst |= b.dstStageMask;
    }
    vkCmdPipelineBarrier(cb, StageFlags1(src, true), StageFlags1(dst, false), dependency.dependencyFlags,
                         static_cast<uint32_t>(memory.size()), memory.data(),
                         static_cast<uint32_t>(buffers.size()), buffers.data(),
                         static_cast<uint32_t>(images.size()), images.data());
}

void SetPipelineTargets(VkGraphicsPipelineCreateInfo* ci, const VkPipelineRenderingCreateInfo* rendering) {
    if (!Vulkan11Path()) {
        ci->pNext = rendering;
        return;
    }
    // Render-pass compatibility: the formats and the sample count; the ops and layouts are free.
    PassKey key;
    key.colorCount = std::min(rendering->colorAttachmentCount, kMaxColors);
    key.samples = ci->pMultisampleState ? ci->pMultisampleState->rasterizationSamples : VK_SAMPLE_COUNT_1_BIT;
    for (uint32_t i = 0; i < key.colorCount; ++i) {
        key.colorFormats[i] = rendering->pColorAttachmentFormats[i];
        key.colorLayouts[i] = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        key.colorLoad[i] = VK_ATTACHMENT_LOAD_OP_LOAD;
        key.colorStore[i] = VK_ATTACHMENT_STORE_OP_STORE;
    }
    VkFormat depth = rendering->depthAttachmentFormat;
    if (depth == VK_FORMAT_UNDEFINED) depth = rendering->stencilAttachmentFormat;
    if (depth != VK_FORMAT_UNDEFINED) {
        key.depthFormat = depth;
        key.depthLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        key.depthLoad = VK_ATTACHMENT_LOAD_OP_LOAD;
        key.depthStore = VK_ATTACHMENT_STORE_OP_STORE;
        key.stencilLoad = VK_ATTACHMENT_LOAD_OP_LOAD;
        key.stencilStore = VK_ATTACHMENT_STORE_OP_STORE;
    }
    std::lock_guard<std::mutex> guard(g_lock);
    ci->renderPass = PassForLocked(key);
    ci->subpass = 0;
}

void Vulkan11Shutdown() {
    std::lock_guard<std::mutex> guard(g_lock);
    g_framebuffers.clear();
    g_framebufferFrame = ~0ull;
    VkDevice device = LiveDevice();
    for (const auto& entry : g_passes) {
        if (device) vkDestroyRenderPass(device, entry.second, nullptr);
    }
    g_passes.clear();
}

}  // namespace VideoVulkan
