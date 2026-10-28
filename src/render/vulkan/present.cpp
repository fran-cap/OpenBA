// ============================================================================
// src/render/vulkan/present.cpp -- the pre-rotated present pass (NOT RETAIL)
// ============================================================================
// present.h has the design. One pipeline (fullscreen triangle, one combined image sampler, a 24-byte
// push constant with the inverse transform), one descriptor set naming the source image, and the
// source image itself: the landscape frame, sized to the back buffer, in the swap-chain format.
// ============================================================================
#include "present.h"

#include "device_objects.h"
#include "present/present_spirv.h"
#include "resources.h"
#include "spirv_legalize.h"
#include "vulkan11.h"

#include <cstdio>
#include <string>
#include <vector>

namespace VideoVulkan {

namespace {

VkShaderModule g_vert = VK_NULL_HANDLE;
VkShaderModule g_frag = VK_NULL_HANDLE;
VkDescriptorSetLayout g_setLayout = VK_NULL_HANDLE;
VkPipelineLayout g_layout = VK_NULL_HANDLE;
VkDescriptorPool g_pool = VK_NULL_HANDLE;
VkDescriptorSet g_set = VK_NULL_HANDLE;
VkPipeline g_pipeline = VK_NULL_HANDLE;
VkFormat g_pipelineFormat = VK_FORMAT_UNDEFINED;
VkSampler g_sampler = VK_NULL_HANDLE;

VkSurfaceTransformFlagBitsKHR g_transform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
GpuImage g_source;
VkExtent2D g_sourceExtent{};
bool g_sourceRead = false;   // a recorded present pass has left the source in SHADER_READ_ONLY
bool g_active = false;       // the frame renders into the source (pre-rotated, or scaled: P7)

// The push constant: uv = A * st + b, A row-wise (present.vert).
struct Transform {
    float a[4];
    float b[2];
};

// The inverse of `transform`: where in the landscape source the swap-chain image's point st lies.
// ROTATE_90 means the content was turned 90 degrees clockwise for the panel, so the panel's
// (s, t) shows the source's (t, 1 - s); ROTATE_270 the other way round.
Transform InverseOf(VkSurfaceTransformFlagBitsKHR transform) {
    switch (transform) {
    case VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR:
        return Transform{ { 0.0f, 1.0f, -1.0f, 0.0f }, { 0.0f, 1.0f } };
    case VK_SURFACE_TRANSFORM_ROTATE_180_BIT_KHR:
        return Transform{ { -1.0f, 0.0f, 0.0f, -1.0f }, { 1.0f, 1.0f } };
    case VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR:
        return Transform{ { 0.0f, -1.0f, 1.0f, 0.0f }, { 1.0f, 0.0f } };
    default:
        return Transform{ { 1.0f, 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f } };
    }
}

bool Fail(std::string* why, const char* what, VkResult r) {
    *why = std::string("present pass: ") + what + ": " + ResultName(r);
    return false;
}

VkShaderModule MakeModule(const uint32_t* words, size_t bytes, VkResult* r) {
    // glslc wrote SPIR-V 1.6; the 1.1 path takes 1.3 (spirv_legalize.h).
    std::vector<uint32_t> downgraded;
    if (Vulkan11Path()) {
        downgraded.assign(words, words + bytes / sizeof(uint32_t));
        std::string why;
        if (!Shaders::DowngradeSpirv(&downgraded, &why)) {
            std::fprintf(stderr, "[Vulkan] present pass: %s\n", why.c_str());
            *r = VK_ERROR_INITIALIZATION_FAILED;
            return VK_NULL_HANDLE;
        }
        words = downgraded.data();
        bytes = downgraded.size() * sizeof(uint32_t);
    }
    VkShaderModuleCreateInfo ci = Make<VkShaderModuleCreateInfo>(VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO);
    ci.codeSize = bytes;
    ci.pCode = words;
    VkShaderModule m = VK_NULL_HANDLE;
    *r = vkCreateShaderModule(LiveDevice(), &ci, nullptr, &m);
    return m;
}

bool MakePipeline(VkFormat format, std::string* why) {
    if (g_pipeline && g_pipelineFormat == format) return true;
    if (g_pipeline) vkDestroyPipeline(LiveDevice(), g_pipeline, nullptr);
    g_pipeline = VK_NULL_HANDLE;

    VkPipelineShaderStageCreateInfo stages[2];
    stages[0] = Make<VkPipelineShaderStageCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO);
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = g_vert;
    stages[0].pName = "main";
    stages[1] = Make<VkPipelineShaderStageCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO);
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = g_frag;
    stages[1].pName = "main";

    auto vertexInput = Make<VkPipelineVertexInputStateCreateInfo>(
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO);
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
    raster.frontFace = VK_FRONT_FACE_CLOCKWISE;
    raster.lineWidth = 1.0f;
    auto multisample = Make<VkPipelineMultisampleStateCreateInfo>(
        VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO);
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState attachment{};
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
    rendering.pColorAttachmentFormats = &format;

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
    ci.layout = g_layout;
    const VkResult r = vkCreateGraphicsPipelines(LiveDevice(), VK_NULL_HANDLE, 1, &ci, nullptr, &g_pipeline);
    if (r != VK_SUCCESS) {
        g_pipeline = VK_NULL_HANDLE;
        return Fail(why, "vkCreateGraphicsPipelines", r);
    }
    g_pipelineFormat = format;
    return true;
}

void ReleaseSource() {
    if (g_source.image) DestroyImageNow(&g_source);
    g_source = GpuImage{};
    g_sourceExtent = VkExtent2D{};
    g_sourceRead = false;
}

}  // namespace

bool PresentStartup(std::string* why) {
    VkResult r = VK_SUCCESS;
    g_vert = MakeModule(kPresentVertSpirv, sizeof(kPresentVertSpirv), &r);
    if (r != VK_SUCCESS) return Fail(why, "vertex module", r);
    g_frag = MakeModule(kPresentFragSpirv, sizeof(kPresentFragSpirv), &r);
    if (r != VK_SUCCESS) return Fail(why, "fragment module", r);

    SamplerDesc sd;
    sd.magLinear = 1;
    sd.minLinear = 1;
    sd.addressU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sd.addressV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sd.addressW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    g_sampler = GetSampler(sd);

    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    binding.pImmutableSamplers = &g_sampler;
    auto sl = Make<VkDescriptorSetLayoutCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO);
    sl.bindingCount = 1;
    sl.pBindings = &binding;
    r = vkCreateDescriptorSetLayout(LiveDevice(), &sl, nullptr, &g_setLayout);
    if (r != VK_SUCCESS) return Fail(why, "vkCreateDescriptorSetLayout", r);

    VkPushConstantRange range{};
    range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    range.offset = 0;
    range.size = sizeof(Transform);
    auto pl = Make<VkPipelineLayoutCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO);
    pl.setLayoutCount = 1;
    pl.pSetLayouts = &g_setLayout;
    pl.pushConstantRangeCount = 1;
    pl.pPushConstantRanges = &range;
    r = vkCreatePipelineLayout(LiveDevice(), &pl, nullptr, &g_layout);
    if (r != VK_SUCCESS) return Fail(why, "vkCreatePipelineLayout", r);

    VkDescriptorPoolSize size{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 };
    auto pc = Make<VkDescriptorPoolCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO);
    pc.maxSets = 1;
    pc.poolSizeCount = 1;
    pc.pPoolSizes = &size;
    r = vkCreateDescriptorPool(LiveDevice(), &pc, nullptr, &g_pool);
    if (r != VK_SUCCESS) return Fail(why, "vkCreateDescriptorPool", r);
    auto ai = Make<VkDescriptorSetAllocateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO);
    ai.descriptorPool = g_pool;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts = &g_setLayout;
    r = vkAllocateDescriptorSets(LiveDevice(), &ai, &g_set);
    if (r != VK_SUCCESS) return Fail(why, "vkAllocateDescriptorSets", r);
    return true;
}

void PresentShutdown() {
    VkDevice d = LiveDevice();
    ReleaseSource();
    if (d) {
        if (g_pipeline) vkDestroyPipeline(d, g_pipeline, nullptr);
        if (g_pool) vkDestroyDescriptorPool(d, g_pool, nullptr);
        if (g_layout) vkDestroyPipelineLayout(d, g_layout, nullptr);
        if (g_setLayout) vkDestroyDescriptorSetLayout(d, g_setLayout, nullptr);
        if (g_vert) vkDestroyShaderModule(d, g_vert, nullptr);
        if (g_frag) vkDestroyShaderModule(d, g_frag, nullptr);
    }
    g_pipeline = VK_NULL_HANDLE;
    g_pipelineFormat = VK_FORMAT_UNDEFINED;
    g_pool = VK_NULL_HANDLE;
    g_set = VK_NULL_HANDLE;
    g_layout = VK_NULL_HANDLE;
    g_setLayout = VK_NULL_HANDLE;
    g_vert = VK_NULL_HANDLE;
    g_frag = VK_NULL_HANDLE;
    g_sampler = VK_NULL_HANDLE;   // the shared sampler cache owns it
    g_transform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    g_active = false;
}

bool PresentConfigure(VkSurfaceTransformFlagBitsKHR transform, VkFormat format, VkExtent2D renderExtent,
                      VkExtent2D swapExtent, std::string* why) {
    g_transform = transform;
    const bool scaled = renderExtent.width != swapExtent.width || renderExtent.height != swapExtent.height;
    g_active = transform != VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR || scaled;
    if (!g_active) {
        ReleaseSource();
        return true;
    }
    if (!MakePipeline(format, why)) return false;
    if (!g_source.image || g_source.format != format || g_sourceExtent.width != renderExtent.width ||
        g_sourceExtent.height != renderExtent.height) {
        ReleaseSource();
        ImageDesc d;
        d.width = renderExtent.width;
        d.height = renderExtent.height;
        d.format = format;
        d.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;   // + SAMPLED / TRANSFER (resources.h ImageDesc)
        // P7: with the tiler's MSAA (VK_EXT_multisampled_render_to_single_sampled) the source is also
        // the main colour of a multisampled frame (draw.cpp MainColorInto): the frame resolves into it
        // on the tile and this pass samples it, with no full-screen copy at the frame's end.
        if (LiveCaps().msrtss) d.flags = VK_IMAGE_CREATE_MULTISAMPLED_RENDER_TO_SINGLE_SAMPLED_BIT_EXT;
        if (!CreateImage(d, &g_source, why)) {
            *why = "present pass: source image: " + *why;
            return false;
        }
        g_sourceExtent = renderExtent;
        VkDescriptorImageInfo ii{};
        ii.imageView = g_source.view;
        ii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        auto w = Make<VkWriteDescriptorSet>(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
        w.dstSet = g_set;
        w.dstBinding = 0;
        w.descriptorCount = 1;
        w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        w.pImageInfo = &ii;
        vkUpdateDescriptorSets(LiveDevice(), 1, &w, 0, nullptr);
    }
    return true;
}

bool PresentActive() {
    return g_active && g_source.image != VK_NULL_HANDLE;
}

VkImage PresentSourceImage() {
    return g_source.image;
}

VkImageView PresentSourceView() {
    return g_source.view;
}

VkImageLayout PresentSourceLayout() {
    return g_sourceRead ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED;
}

void PresentRecord(VkCommandBuffer cb, VkImage swapImage, VkImageView swapView, VkExtent2D swapExtent) {
    (void)swapImage;
    VkRenderingAttachmentInfo color = Make<VkRenderingAttachmentInfo>(VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO);
    color.imageView = swapView;
    color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;   // the triangle covers every pixel
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    VkRenderingInfo ri = Make<VkRenderingInfo>(VK_STRUCTURE_TYPE_RENDERING_INFO);
    ri.renderArea = { { 0, 0 }, swapExtent };
    ri.layerCount = 1;
    ri.colorAttachmentCount = 1;
    ri.pColorAttachments = &color;
    CmdBeginRendering(cb, ri, &g_pipelineFormat, VK_FORMAT_UNDEFINED, VK_SAMPLE_COUNT_1_BIT);
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, g_pipeline);
    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, g_layout, 0, 1, &g_set, 0, nullptr);
    const Transform t = InverseOf(g_transform);
    vkCmdPushConstants(cb, g_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(t), &t);
    VkViewport vp{};
    vp.width = static_cast<float>(swapExtent.width);
    vp.height = static_cast<float>(swapExtent.height);
    vp.maxDepth = 1.0f;
    vkCmdSetViewport(cb, 0, 1, &vp);
    const VkRect2D scissor{ { 0, 0 }, swapExtent };
    vkCmdSetScissor(cb, 0, 1, &scissor);
    vkCmdDraw(cb, 3, 1, 0, 0);
    CmdEndRendering(cb);
    g_sourceRead = true;
}

}  // namespace VideoVulkan
