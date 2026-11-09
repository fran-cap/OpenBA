// ============================================================================
// src/render/vulkan/tweak_overlay_vulkan.cpp -- tweak_overlay_vulkan.h (NOT RETAIL; debug flavour only)
// ============================================================================
// The overlay's frame is built by src/diag/win/tweak_overlay.cpp; this file renders it. The pass loads
// the image (the game's frame stays under the panels), alpha-blends, scissors per draw command and
// leaves the image in COLOR_ATTACHMENT_OPTIMAL for DrawEndFrame's move to PRESENT_SRC.
// ============================================================================
#if BANISHED_TWEAK_OVERLAY

#include "tweak_overlay_vulkan.h"

#include "tweak_overlay.h"   // src/diag: Diag::TweakOverlay::Renderer
#include "tweak_overlay/overlay_spirv.h"

#include "device_objects.h"
#include "resources.h"
#include "vulkan11.h"

#include "imgui.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace VideoVulkan {

namespace {

constexpr uint32_t kMaxTextures = 16;   // ImGui makes one (the font atlas); a few spare

// The push constant (overlay.vert).
struct FrameConstants {
    float scale[2];
    float translate[2];
};

// One ImGui texture: the image and the descriptor set naming it (the set is the ImTextureID).
struct OverlayTexture {
    GpuImage image;
    VkDescriptorSet set = VK_NULL_HANDLE;
};

VkShaderModule MakeModule(const uint32_t* words, size_t bytes, VkResult* r) {
    VkShaderModuleCreateInfo ci = Make<VkShaderModuleCreateInfo>(VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO);
    ci.codeSize = bytes;
    ci.pCode = words;
    VkShaderModule m = VK_NULL_HANDLE;
    *r = vkCreateShaderModule(LiveDevice(), &ci, nullptr, &m);
    return m;
}

bool Failed(std::string* why, const char* what, VkResult r) {
    *why = std::string(what) + ": " + ResultName(r);
    return false;
}

class VulkanRenderer final : public Diag::TweakOverlay::Renderer {
public:
    // This frame's target, set before each Diag::TweakOverlay::BeforePresent.
    VkCommandBuffer cb = VK_NULL_HANDLE;
    unsigned slot = 0;
    VkImageView view = VK_NULL_HANDLE;
    VkExtent2D extent{};
    VkFormat format = VK_FORMAT_UNDEFINED;

    const char* Name() const override { return "Vulkan"; }
    bool Init() override;
    void NewFrame() override {}
    void Render(ImDrawData* drawData) override;
    void Shutdown() override;

    // The device objects (the device idle): Shutdown, and the device shutdown for what a failed Init left.
    void ReleaseObjects();

private:
    bool MakeObjects(std::string* why);
    bool MakePipeline(VkFormat targetFormat, std::string* why);
    void UpdateTexture(ImTextureData* tex);
    void DestroyTexture(ImTextureData* tex, bool now);
    bool EnsureBuffer(GpuBuffer* b, VkDeviceSize size, VkBufferUsageFlags usage);
    void SetupState(ImDrawData* drawData);

    VkShaderModule vert_ = VK_NULL_HANDLE;
    VkShaderModule frag_ = VK_NULL_HANDLE;
    VkSampler sampler_ = VK_NULL_HANDLE;   // the shared sampler cache owns it
    VkDescriptorSetLayout setLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    VkDescriptorPool pool_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkFormat pipelineFormat_ = VK_FORMAT_UNDEFINED;
    GpuBuffer vertices_[kFramesInFlight];
    GpuBuffer indices_[kFramesInFlight];
};

VulkanRenderer g_renderer;

bool VulkanRenderer::MakeObjects(std::string* why) {
    if (pool_ != VK_NULL_HANDLE) return true;
    VkResult r = VK_SUCCESS;
    vert_ = MakeModule(kOverlayVertSpirv, sizeof(kOverlayVertSpirv), &r);
    if (r != VK_SUCCESS) return Failed(why, "vertex module", r);
    frag_ = MakeModule(kOverlayFragSpirv, sizeof(kOverlayFragSpirv), &r);
    if (r != VK_SUCCESS) return Failed(why, "fragment module", r);

    SamplerDesc sd;
    sd.magLinear = 1;
    sd.minLinear = 1;
    sd.addressU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sd.addressV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sd.addressW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_ = GetSampler(sd);

    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    binding.pImmutableSamplers = &sampler_;
    auto sl = Make<VkDescriptorSetLayoutCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO);
    sl.bindingCount = 1;
    sl.pBindings = &binding;
    r = vkCreateDescriptorSetLayout(LiveDevice(), &sl, nullptr, &setLayout_);
    if (r != VK_SUCCESS) return Failed(why, "vkCreateDescriptorSetLayout", r);

    VkPushConstantRange range{};
    range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    range.offset = 0;
    range.size = sizeof(FrameConstants);
    auto pl = Make<VkPipelineLayoutCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO);
    pl.setLayoutCount = 1;
    pl.pSetLayouts = &setLayout_;
    pl.pushConstantRangeCount = 1;
    pl.pPushConstantRanges = &range;
    r = vkCreatePipelineLayout(LiveDevice(), &pl, nullptr, &layout_);
    if (r != VK_SUCCESS) return Failed(why, "vkCreatePipelineLayout", r);

    VkDescriptorPoolSize size{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kMaxTextures };
    auto pc = Make<VkDescriptorPoolCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO);
    pc.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;   // a texture's set goes with it
    pc.maxSets = kMaxTextures;
    pc.poolSizeCount = 1;
    pc.pPoolSizes = &size;
    r = vkCreateDescriptorPool(LiveDevice(), &pc, nullptr, &pool_);
    if (r != VK_SUCCESS) {
        pool_ = VK_NULL_HANDLE;
        return Failed(why, "vkCreateDescriptorPool", r);
    }
    return true;
}

bool VulkanRenderer::MakePipeline(VkFormat targetFormat, std::string* why) {
    if (pipeline_ != VK_NULL_HANDLE && pipelineFormat_ == targetFormat) return true;
    if (pipeline_ != VK_NULL_HANDLE) {
        // The swap-chain format changed (a rebuild): the old pipeline may still be in a frame in flight.
        const VkPipeline old = pipeline_;
        Retire([old] { vkDestroyPipeline(LiveDevice(), old, nullptr); });
        pipeline_ = VK_NULL_HANDLE;
    }

    VkPipelineShaderStageCreateInfo stages[2];
    stages[0] = Make<VkPipelineShaderStageCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO);
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert_;
    stages[0].pName = "main";
    stages[1] = Make<VkPipelineShaderStageCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO);
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag_;
    stages[1].pName = "main";

    VkVertexInputBindingDescription vb{};
    vb.binding = 0;
    vb.stride = sizeof(ImDrawVert);
    vb.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    VkVertexInputAttributeDescription attrs[3] = {};
    attrs[0].location = 0;
    attrs[0].format = VK_FORMAT_R32G32_SFLOAT;
    attrs[0].offset = offsetof(ImDrawVert, pos);
    attrs[1].location = 1;
    attrs[1].format = VK_FORMAT_R32G32_SFLOAT;
    attrs[1].offset = offsetof(ImDrawVert, uv);
    attrs[2].location = 2;
    attrs[2].format = VK_FORMAT_R8G8B8A8_UNORM;
    attrs[2].offset = offsetof(ImDrawVert, col);
    auto vertexInput = Make<VkPipelineVertexInputStateCreateInfo>(
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO);
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &vb;
    vertexInput.vertexAttributeDescriptionCount = 3;
    vertexInput.pVertexAttributeDescriptions = attrs;
    auto assembly = Make<VkPipelineInputAssemblyStateCreateInfo>(
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO);
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    auto viewport = Make<VkPipelineViewportStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO);
    viewport.viewportCount = 1;
    viewport.scissorCount = 1;
    auto raster = Make<VkPipelineRasterizationStateCreateInfo>(
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO);
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth = 1.0f;
    auto multisample = Make<VkPipelineMultisampleStateCreateInfo>(
        VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO);
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    // ImGui's blend: straight alpha over the frame.
    VkPipelineColorBlendAttachmentState attachment{};
    attachment.blendEnable = VK_TRUE;
    attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    attachment.colorBlendOp = VK_BLEND_OP_ADD;
    attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    attachment.alphaBlendOp = VK_BLEND_OP_ADD;
    attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
                                VK_COLOR_COMPONENT_A_BIT;
    auto blend = Make<VkPipelineColorBlendStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO);
    blend.attachmentCount = 1;
    blend.pAttachments = &attachment;
    const VkDynamicState dynamicStates[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    auto dynamic = Make<VkPipelineDynamicStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO);
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates = dynamicStates;
    auto rendering = Make<VkPipelineRenderingCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO);
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachmentFormats = &targetFormat;

    auto ci = Make<VkGraphicsPipelineCreateInfo>(VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO);
    ci.stageCount = 2;
    ci.pStages = stages;
    ci.pVertexInputState = &vertexInput;
    ci.pInputAssemblyState = &assembly;
    ci.pViewportState = &viewport;
    ci.pRasterizationState = &raster;
    ci.pMultisampleState = &multisample;
    ci.pColorBlendState = &blend;
    ci.pDynamicState = &dynamic;
    SetPipelineTargets(&ci, &rendering);
    ci.layout = layout_;
    const VkResult r = vkCreateGraphicsPipelines(LiveDevice(), VK_NULL_HANDLE, 1, &ci, nullptr, &pipeline_);
    if (r != VK_SUCCESS) {
        pipeline_ = VK_NULL_HANDLE;
        return Failed(why, "vkCreateGraphicsPipelines", r);
    }
    pipelineFormat_ = targetFormat;
    return true;
}

bool VulkanRenderer::Init() {
    if (LiveDevice() == VK_NULL_HANDLE) return false;
    std::string why;
    if (!MakeObjects(&why) || !MakePipeline(format, &why)) {
        std::fprintf(stderr, "[TweakOverlay] Vulkan renderer: %s\n", why.c_str());
        ReleaseObjects();
        return false;
    }
    ImGuiIO& io = ImGui::GetIO();
    io.BackendRendererName = "openba_vulkan";
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasTextures;
    return true;
}

void VulkanRenderer::UpdateTexture(ImTextureData* tex) {
    if (tex->Status == ImTextureStatus_WantCreate) {
        IM_ASSERT(tex->Format == ImTextureFormat_RGBA32);
        auto* t = IM_NEW(OverlayTexture)();
        ImageDesc d;
        d.width = static_cast<uint32_t>(tex->Width);
        d.height = static_cast<uint32_t>(tex->Height);
        d.format = VK_FORMAT_R8G8B8A8_UNORM;
        std::string why;
        if (!CreateImage(d, &t->image, &why)) {
            std::fprintf(stderr, "[TweakOverlay] Vulkan renderer: texture %dx%d: %s\n", tex->Width, tex->Height,
                         why.c_str());
            IM_DELETE(t);
            return;   // still WantCreate: asked again next frame
        }
        auto ai = Make<VkDescriptorSetAllocateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO);
        ai.descriptorPool = pool_;
        ai.descriptorSetCount = 1;
        ai.pSetLayouts = &setLayout_;
        const VkResult r = vkAllocateDescriptorSets(LiveDevice(), &ai, &t->set);
        if (r != VK_SUCCESS) {
            std::fprintf(stderr, "[TweakOverlay] Vulkan renderer: vkAllocateDescriptorSets: %s\n", ResultName(r));
            DestroyImageNow(&t->image);
            IM_DELETE(t);
            return;
        }
        VkDescriptorImageInfo ii{};
        ii.imageView = t->image.view;
        ii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        auto w = Make<VkWriteDescriptorSet>(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
        w.dstSet = t->set;
        w.dstBinding = 0;
        w.descriptorCount = 1;
        w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        w.pImageInfo = &ii;
        vkUpdateDescriptorSets(LiveDevice(), 1, &w, 0, nullptr);
        tex->BackendUserData = t;
        tex->SetTexID(static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(t->set)));
    }
    if (tex->Status == ImTextureStatus_WantCreate || tex->Status == ImTextureStatus_WantUpdates) {
        // The whole image each time (ImageRegion has no x / y): the atlas is small and changes rarely.
        auto* t = static_cast<OverlayTexture*>(tex->BackendUserData);
        ImageRegion region;
        region.width = static_cast<uint32_t>(tex->Width);
        region.height = static_cast<uint32_t>(tex->Height);
        UploadImage(t->image, &region, 1, tex->GetPixels(), static_cast<size_t>(tex->GetSizeInBytes()));
        tex->SetStatus(ImTextureStatus_OK);
    }
    if (tex->Status == ImTextureStatus_WantDestroy && tex->UnusedFrames > 0) DestroyTexture(tex, false);
}

void VulkanRenderer::DestroyTexture(ImTextureData* tex, bool now) {
    if (auto* t = static_cast<OverlayTexture*>(tex->BackendUserData)) {
        const VkDescriptorPool pool = pool_;
        const VkDescriptorSet set = t->set;
        if (now) {
            if (set != VK_NULL_HANDLE && LiveDevice() != VK_NULL_HANDLE) vkFreeDescriptorSets(LiveDevice(), pool, 1, &set);
            DestroyImageNow(&t->image);
        } else {
            if (set != VK_NULL_HANDLE) Retire([pool, set] { vkFreeDescriptorSets(LiveDevice(), pool, 1, &set); });
            RetireImage(&t->image);
        }
        IM_DELETE(t);
        tex->BackendUserData = nullptr;
    }
    tex->SetTexID(ImTextureID_Invalid);
    tex->SetStatus(ImTextureStatus_Destroyed);   // back to WantCreate while ImGui keeps the pixels
}

bool VulkanRenderer::EnsureBuffer(GpuBuffer* b, VkDeviceSize size, VkBufferUsageFlags usage) {
    if (b->buffer != VK_NULL_HANDLE && b->size >= size) return true;
    if (b->buffer != VK_NULL_HANDLE) RetireBuffer(b);
    VkDeviceSize grown = 64 * 1024;
    while (grown < size) grown *= 2;
    std::string why;
    if (!CreateHostBuffer(grown, usage, b, &why)) {
        std::fprintf(stderr, "[TweakOverlay] Vulkan renderer: buffer of %llu bytes: %s\n",
                     static_cast<unsigned long long>(grown), why.c_str());
        *b = GpuBuffer{};
        return false;
    }
    return true;
}

void VulkanRenderer::SetupState(ImDrawData* drawData) {
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    const VkBuffer vb = vertices_[slot].buffer;
    const VkDeviceSize zero = 0;
    vkCmdBindVertexBuffers(cb, 0, 1, &vb, &zero);
    vkCmdBindIndexBuffer(cb, indices_[slot].buffer, 0,
                         sizeof(ImDrawIdx) == 2 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32);
    VkViewport vp{};
    vp.width = static_cast<float>(extent.width);
    vp.height = static_cast<float>(extent.height);
    vp.maxDepth = 1.0f;
    vkCmdSetViewport(cb, 0, 1, &vp);
    FrameConstants fc;
    fc.scale[0] = 2.0f / drawData->DisplaySize.x;
    fc.scale[1] = 2.0f / drawData->DisplaySize.y;
    fc.translate[0] = -1.0f - drawData->DisplayPos.x * fc.scale[0];
    fc.translate[1] = -1.0f - drawData->DisplayPos.y * fc.scale[1];
    vkCmdPushConstants(cb, layout_, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(fc), &fc);
}

void VulkanRenderer::Render(ImDrawData* drawData) {
    if (drawData->Textures != nullptr) {
        for (ImTextureData* tex : *drawData->Textures)
            if (tex->Status != ImTextureStatus_OK) UpdateTexture(tex);
    }
    if (drawData->TotalVtxCount <= 0 || drawData->DisplaySize.x <= 0.0f || drawData->DisplaySize.y <= 0.0f) return;
    std::string why;
    if (!MakePipeline(format, &why)) {
        std::fprintf(stderr, "[TweakOverlay] Vulkan renderer: %s\n", why.c_str());
        return;
    }

    // The slot's fence was waited for: its buffers are free to overwrite.
    const VkDeviceSize vtxBytes = static_cast<VkDeviceSize>(drawData->TotalVtxCount) * sizeof(ImDrawVert);
    const VkDeviceSize idxBytes = static_cast<VkDeviceSize>(drawData->TotalIdxCount) * sizeof(ImDrawIdx);
    if (!EnsureBuffer(&vertices_[slot], vtxBytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT) ||
        !EnsureBuffer(&indices_[slot], idxBytes, VK_BUFFER_USAGE_INDEX_BUFFER_BIT))
        return;
    uint8_t* vdst = vertices_[slot].mapped;
    uint8_t* idst = indices_[slot].mapped;
    for (const ImDrawList* list : drawData->CmdLists) {
        const size_t vbytes = static_cast<size_t>(list->VtxBuffer.Size) * sizeof(ImDrawVert);
        const size_t ibytes = static_cast<size_t>(list->IdxBuffer.Size) * sizeof(ImDrawIdx);
        std::memcpy(vdst, list->VtxBuffer.Data, vbytes);
        std::memcpy(idst, list->IdxBuffer.Data, ibytes);
        vdst += vbytes;
        idst += ibytes;
    }

    VkRenderingAttachmentInfo color = Make<VkRenderingAttachmentInfo>(VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO);
    color.imageView = view;
    color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;   // the game's frame stays under the panels
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    VkRenderingInfo ri = Make<VkRenderingInfo>(VK_STRUCTURE_TYPE_RENDERING_INFO);
    ri.renderArea = { { 0, 0 }, extent };
    ri.layerCount = 1;
    ri.colorAttachmentCount = 1;
    ri.pColorAttachments = &color;
    CmdBeginRendering(cb, ri, &format, VK_FORMAT_UNDEFINED, VK_SAMPLE_COUNT_1_BIT);
    SetupState(drawData);

    const ImVec2 clipOff = drawData->DisplayPos;
    const ImVec2 clipScale = drawData->FramebufferScale;
    const float fbW = static_cast<float>(extent.width);
    const float fbH = static_cast<float>(extent.height);
    VkDescriptorSet bound = VK_NULL_HANDLE;
    uint32_t globalVtx = 0;
    uint32_t globalIdx = 0;
    for (const ImDrawList* list : drawData->CmdLists) {
        for (const ImDrawCmd& cmd : list->CmdBuffer) {
            if (cmd.UserCallback != nullptr) {
                if (cmd.UserCallback == ImDrawCallback_ResetRenderState) {
                    SetupState(drawData);
                    bound = VK_NULL_HANDLE;
                } else {
                    cmd.UserCallback(list, &cmd);
                }
                continue;
            }
            float x0 = (cmd.ClipRect.x - clipOff.x) * clipScale.x;
            float y0 = (cmd.ClipRect.y - clipOff.y) * clipScale.y;
            float x1 = (cmd.ClipRect.z - clipOff.x) * clipScale.x;
            float y1 = (cmd.ClipRect.w - clipOff.y) * clipScale.y;
            if (x0 < 0.0f) x0 = 0.0f;
            if (y0 < 0.0f) y0 = 0.0f;
            if (x1 > fbW) x1 = fbW;
            if (y1 > fbH) y1 = fbH;
            if (x1 <= x0 || y1 <= y0) continue;
            const VkDescriptorSet set = reinterpret_cast<VkDescriptorSet>(static_cast<uintptr_t>(cmd.GetTexID()));
            if (set == VK_NULL_HANDLE) continue;   // a texture whose creation failed this frame
            VkRect2D scissor;
            scissor.offset.x = static_cast<int32_t>(x0);
            scissor.offset.y = static_cast<int32_t>(y0);
            scissor.extent.width = static_cast<uint32_t>(x1 - x0);
            scissor.extent.height = static_cast<uint32_t>(y1 - y0);
            vkCmdSetScissor(cb, 0, 1, &scissor);
            if (set != bound) {
                vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_, 0, 1, &set, 0, nullptr);
                bound = set;
            }
            vkCmdDrawIndexed(cb, cmd.ElemCount, 1, cmd.IdxOffset + globalIdx,
                             static_cast<int32_t>(cmd.VtxOffset + globalVtx), 0);
        }
        globalIdx += static_cast<uint32_t>(list->IdxBuffer.Size);
        globalVtx += static_cast<uint32_t>(list->VtxBuffer.Size);
    }
    CmdEndRendering(cb);
}

void VulkanRenderer::Shutdown() {
    for (ImTextureData* tex : ImGui::GetPlatformIO().Textures)
        if (tex->RefCount == 1) DestroyTexture(tex, true);
    ReleaseObjects();
    ImGuiIO& io = ImGui::GetIO();
    io.BackendRendererName = nullptr;
    io.BackendFlags &= ~(ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasTextures);
}

void VulkanRenderer::ReleaseObjects() {
    const VkDevice d = LiveDevice();
    for (unsigned i = 0; i < kFramesInFlight; ++i) {
        DestroyBufferNow(&vertices_[i]);
        DestroyBufferNow(&indices_[i]);
    }
    if (d != VK_NULL_HANDLE) {
        if (pipeline_) vkDestroyPipeline(d, pipeline_, nullptr);
        if (pool_) vkDestroyDescriptorPool(d, pool_, nullptr);
        if (layout_) vkDestroyPipelineLayout(d, layout_, nullptr);
        if (setLayout_) vkDestroyDescriptorSetLayout(d, setLayout_, nullptr);
        if (vert_) vkDestroyShaderModule(d, vert_, nullptr);
        if (frag_) vkDestroyShaderModule(d, frag_, nullptr);
    }
    pipeline_ = VK_NULL_HANDLE;
    pipelineFormat_ = VK_FORMAT_UNDEFINED;
    pool_ = VK_NULL_HANDLE;
    layout_ = VK_NULL_HANDLE;
    setLayout_ = VK_NULL_HANDLE;
    vert_ = VK_NULL_HANDLE;
    frag_ = VK_NULL_HANDLE;
    sampler_ = VK_NULL_HANDLE;
}

}  // namespace

bool TweakOverlayOpen() {
    return Diag::TweakOverlay::IsOpen();
}

void TweakOverlayRecord(VkCommandBuffer cb, unsigned slot, VkImageView view, VkExtent2D extent, VkFormat format) {
    g_renderer.cb = cb;
    g_renderer.slot = slot;
    g_renderer.view = view;
    g_renderer.extent = extent;
    g_renderer.format = format;
    Diag::TweakOverlay::BeforePresent(g_renderer, extent.width, extent.height);
    g_renderer.cb = VK_NULL_HANDLE;
}

void TweakOverlayShutdown() {
    // Registered: ImGui's textures go back to WantCreate with the objects. Not registered: whatever a
    // failed Init left.
    Diag::TweakOverlay::OnRendererShutdown(g_renderer);
    g_renderer.ReleaseObjects();
}

}  // namespace VideoVulkan

#endif  // BANISHED_TWEAK_OVERLAY
