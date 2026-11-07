// ============================================================================
// src/render/vulkan/better_clouds/cloud_shadow.cpp -- ext.better_clouds Fancy's shadow pass on this backend
// ============================================================================
// NOT RETAIL. The contract is src/render/common/better_clouds/cloud_shadow_backend.h
// (docs/extensions/handins/BETTERCLOUDS.md section 2.6); the DX11 pass is
// src/render/dx11/better_clouds/cloud_shadow.cpp.
//
// THE FRAME (the ground-draw seam, the recording thread, between the Opaque and Blended lists), in
// draw.cpp's AO bracket as ext.better_water's scene depth copy runs:
//   DrawAoBegin      the main pass ends, the main depth (made readable: draw.h THE READABLE DEPTH, this
//                    pass's own reader bit) -> SHADER_READ_ONLY
//   the copy         one fullscreen draw into a 1x R32F image of ours: sample 0 of a multisampled depth,
//                    a plain copy of a single-sampled one (the tiler's MSAA: the main passes resolved
//                    sample 0 already). AO's depth resolve programs (ao_depth_resolve.hlsl), as the AO
//                    pass and the scene depth copy run them. The apply below cannot read the main depth
//                    itself: on the main target its pass has that depth attached.
//   the apply        DrawAoBeginApply's pass on the main colour (on the main target: the Blended list's
//                    pass, the depth attached and never tested): AO's fullscreen triangle with our
//                    program (cloud_shadow.hlsl), blend colour ZERO / SRC_COLOR, RGB only (the alpha
//                    holds the shore depth the water reads)
//   DrawAoEnd        the engine's next draw rebinds everything
// Every program is our own DXBC through the backend's one DXBC -> SPIR-V converter.
//
// LIFETIME. The modules, layouts, pools, pipelines and the constants ring per device (Shutdown, from
// DeviceData::Shutdown, destroys them; the next device makes them again on its first Supported()). The
// copy is an engine Video::ImageBuffer (a colour render buffer, R32F), remade when the frame's size
// changes and released with the device or when the request goes off.
// ============================================================================
#include "better_clouds/cloud_shadow_backend.h"
#include "better_clouds/cloud_shadow_pass.h"

#include "VideoVulkanData.h"
#include "device_objects.h"
#include "draw.h"
#include "pipelines.h"
#include "resources.h"
#include "shader_convert.h"
#include "vulkan_entry.h"

#include "ext/derived_cache/derived_cache.h"
#include "video/ImageBuffer.h"

#include "ao/shaders/VSFullscreen.h"
#include "ao/shaders/PSResolveDepthMS.h"
#include "ao/shaders/PSResolveDepth1x.h"
#include "better_clouds/cloud_shadow_ps.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace VideoVulkan {
namespace CloudShadow {

namespace {

constexpr uint32_t kSetsPerSlot = 8;          // a frame draws the pass once: a copy set and an apply set
constexpr VkDeviceSize kRingSlots = 8;        // a frame writes one constants block
constexpr VkDeviceSize kSlotBytes = 256;      // one block, at the strictest uniform offset alignment
static_assert(sizeof(CloudShadowBackend::Frame) <= kSlotBytes, "one constants block per ring slot");

enum Program : int {
    kVsFullscreen,
    kResolveMs,
    kResolve1x,
    kShadow,
    kProgramCount
};

struct ProgramBlob {
    const uint8_t* bytes;
    size_t size;
    Shaders::Stage stage;
    const char* name;
    uint32_t constants;   // the reflection the set layout below covers
    uint32_t textures;
    uint32_t samplers;
};

const ProgramBlob kBlobs[kProgramCount] = {
    { g_ao_VSFullscreen, sizeof(g_ao_VSFullscreen), Shaders::Stage::Vertex, "VSFullscreen", 0, 0, 0 },
    { g_ao_PSResolveDepthMS, sizeof(g_ao_PSResolveDepthMS), Shaders::Stage::Pixel, "PSResolveDepthMS", 0, 1, 0 },
    { g_ao_PSResolveDepth1x, sizeof(g_ao_PSResolveDepth1x), Shaders::Stage::Pixel, "PSResolveDepth1x", 0, 1, 0 },
    { g_bc_PSCloudShadow, sizeof(g_bc_PSCloudShadow), Shaders::Stage::Pixel, "PSCloudShadow", 1, 3, 2 },
};

// The device objects of the pass, one set per device (Shutdown resets it).
struct State {
    VkDevice device = VK_NULL_HANDLE;
    bool ready = false;
    bool failed = false;                                     // a pipeline the driver refused: not supported
    VkShaderModule modules[kProgramCount] = {};
    VkDescriptorSetLayout vertexLayout = VK_NULL_HANDLE;     // empty: VSFullscreen reads nothing
    VkDescriptorSetLayout pixelLayout = VK_NULL_HANDLE;      // b0, t0, t1, s1
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkPipeline resolve[2] = {};                              // by kResolveMs / kResolve1x - kResolveMs
    std::unordered_map<uint64_t, VkPipeline> apply;          // by the main pass's shape
    VkDescriptorPool pools[kFramesInFlight] = {};
    uint64_t poolFrame[kFramesInFlight] = {};
    GpuBuffer rings[kFramesInFlight];
    VkDeviceSize ringAt[kFramesInFlight] = {};
    uint32_t logged = 0;
};

State g;
// Every entry point holds it for its whole body.
std::mutex g_mutex;

std::atomic<bool> g_requested{false};
Video::ImageBuffer* g_image = nullptr;   // the depth copy (an engine object; it outlives a device)
const void* g_companion = nullptr;
uint32_t g_width = 0;
uint32_t g_height = 0;
uint32_t g_samples = 0;

void LogOnce(uint32_t bit, const char* message) {
    if (g.logged & bit) return;
    g.logged |= bit;
    std::fprintf(stderr, "[BetterClouds] Vulkan: %s\n", message);
}

void DestroyLocked() {
    VkDevice dev = LiveDevice();
    if (dev) {
        for (VkPipeline p : g.resolve) {
            if (p) vkDestroyPipeline(dev, p, nullptr);
        }
        for (auto& kv : g.apply) {
            if (kv.second) vkDestroyPipeline(dev, kv.second, nullptr);
        }
        for (VkDescriptorPool p : g.pools) {
            if (p) vkDestroyDescriptorPool(dev, p, nullptr);
        }
        if (g.layout) vkDestroyPipelineLayout(dev, g.layout, nullptr);
        if (g.pixelLayout) vkDestroyDescriptorSetLayout(dev, g.pixelLayout, nullptr);
        if (g.vertexLayout) vkDestroyDescriptorSetLayout(dev, g.vertexLayout, nullptr);
    }
    for (GpuBuffer& b : g.rings)
        DestroyBufferNow(&b);
    for (VkShaderModule m : g.modules)
        DestroyShaderModule(m);
    g = State{};
}

// The fullscreen triangle. The resolve: into one R32F target, opaque, single-sampled. The apply: into the
// main pass (its colour format, depth / stencil formats and sample count), multiply, RGB only.
VkPipeline MakePipeline(Program ps, VkFormat color, VkFormat depth, VkFormat stencil, uint32_t samples) {
    const bool apply = ps == kShadow;
    VkPipelineShaderStageCreateInfo stages[2];
    stages[0] = Make<VkPipelineShaderStageCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO);
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = g.modules[kVsFullscreen];
    stages[0].pName = "main";
    stages[1] = Make<VkPipelineShaderStageCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO);
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = g.modules[ps];
    stages[1].pName = "main";
    auto vi = Make<VkPipelineVertexInputStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO);
    auto ia = Make<VkPipelineInputAssemblyStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO);
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    auto vp = Make<VkPipelineViewportStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO);
    vp.viewportCount = 1;
    vp.scissorCount = 1;
    auto rs = Make<VkPipelineRasterizationStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO);
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rs.lineWidth = 1.0f;
    auto ms = Make<VkPipelineMultisampleStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO);
    ms.rasterizationSamples = static_cast<VkSampleCountFlagBits>(samples);
    auto ds = Make<VkPipelineDepthStencilStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO);
    ds.depthCompareOp = VK_COMPARE_OP_ALWAYS;
    VkPipelineColorBlendAttachmentState blend{};
    if (apply) {
        blend.blendEnable = VK_TRUE;
        blend.srcColorBlendFactor = VK_BLEND_FACTOR_ZERO;
        blend.dstColorBlendFactor = VK_BLEND_FACTOR_SRC_COLOR;
        blend.colorBlendOp = VK_BLEND_OP_ADD;
        blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        blend.alphaBlendOp = VK_BLEND_OP_ADD;
        blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT;
    } else {
        blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT;
    }
    auto cbs = Make<VkPipelineColorBlendStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO);
    cbs.attachmentCount = 1;
    cbs.pAttachments = &blend;
    // The stride state as the AO pass's pipelines carry it (assao_pass.cpp PipelineFor): the game's draws
    // in the same command buffer set strides with vkCmdBindVertexBuffers2.
    const VkDynamicState dynamics[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR,
                                        VK_DYNAMIC_STATE_VERTEX_INPUT_BINDING_STRIDE };
    auto dyn = Make<VkPipelineDynamicStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO);
    dyn.dynamicStateCount = 3;
    dyn.pDynamicStates = dynamics;
    auto ri = Make<VkPipelineRenderingCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO);
    ri.colorAttachmentCount = 1;
    ri.pColorAttachmentFormats = &color;
    ri.depthAttachmentFormat = depth;
    ri.stencilAttachmentFormat = stencil;

    auto pi = Make<VkGraphicsPipelineCreateInfo>(VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO);
    pi.pNext = &ri;
    pi.stageCount = 2;
    pi.pStages = stages;
    pi.pVertexInputState = &vi;
    pi.pInputAssemblyState = &ia;
    pi.pViewportState = &vp;
    pi.pRasterizationState = &rs;
    pi.pMultisampleState = &ms;
    pi.pDepthStencilState = &ds;
    pi.pColorBlendState = &cbs;
    pi.pDynamicState = &dyn;
    pi.layout = g.layout;
    VkPipeline pipeline = VK_NULL_HANDLE;
    const VkResult r = vkCreateGraphicsPipelines(LiveDevice(), GamePipelineCache(), 1, &pi, nullptr, &pipeline);
    if (r != VK_SUCCESS) {
        std::fprintf(stderr, "[BetterClouds] Vulkan: pipeline %s: %s: the shadows are not supported\n",
                     kBlobs[ps].name, ResultName(r));
        g.failed = true;
        return VK_NULL_HANDLE;
    }
    return pipeline;
}

VkPipeline ApplyPipeline(VkFormat color, VkFormat depth, VkFormat stencil, uint32_t samples) {
    const uint64_t key = (static_cast<uint64_t>(color) << 40) ^ (static_cast<uint64_t>(depth) << 24) ^
                         (static_cast<uint64_t>(stencil) << 8) ^ samples;
    auto found = g.apply.find(key);
    if (found != g.apply.end()) return found->second;
    const VkPipeline p = MakePipeline(kShadow, color, depth, stencil, samples);
    g.apply[key] = p;
    return p;
}

// One conversion and one set of objects per device. The caller holds g_mutex.
bool InitLocked() {
    VkDevice dev = LiveDevice();
    if (!dev) return false;
    if (g.device == dev) return g.ready && !g.failed;
    if (g.device) DestroyLocked();
    g.device = dev;
    if (LiveCaps().vulkan11) {
        // The pass is dynamic rendering (vkCmdBeginRendering), which the 1.1 path does not have.
        std::fprintf(stderr, "[BetterClouds] Vulkan: the shadows are not supported on the Vulkan 1.1 path\n");
        return false;
    }
    Ext::DerivedCache::Store* const cache = Ext::DerivedCache::OpenShared();
    for (int i = 0; i < kProgramCount; ++i) {
        std::vector<uint32_t> spirv;
        Shaders::ShaderReflection refl;
        bool hit = false;
        std::string why;
        if (!Shaders::LoadOrConvert(cache, kBlobs[i].bytes, kBlobs[i].size, kBlobs[i].stage, &spirv, &refl, &hit,
                                    &why)) {
            std::fprintf(stderr, "[BetterClouds] Vulkan: %s did not convert (%s): the shadows are not supported\n",
                         kBlobs[i].name, why.c_str());
            return false;
        }
        // The pixel layout holds b0, t0, t1 and s1: a program must read no more than its row says.
        if ((refl.constantBufferMask & ~kBlobs[i].constants) != 0 || (refl.textureMask & ~kBlobs[i].textures) != 0 ||
            (refl.samplerMask & ~kBlobs[i].samplers) != 0 || refl.uavMask != 0) {
            std::fprintf(stderr, "[BetterClouds] Vulkan: %s reads more than the pass binds: the shadows are not "
                                 "supported\n", kBlobs[i].name);
            return false;
        }
        g.modules[i] = CreateShaderModule(spirv, &why);
        if (!g.modules[i]) {
            std::fprintf(stderr, "[BetterClouds] Vulkan: %s: %s: the shadows are not supported\n", kBlobs[i].name,
                         why.c_str());
            return false;
        }
    }

    VkDescriptorSetLayoutBinding bindings[4] = {};
    bindings[0].binding = Shaders::BindingOfConstantBuffer(0);
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[1].binding = Shaders::BindingOfTexture(0);
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    bindings[2].binding = Shaders::BindingOfTexture(1);
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    bindings[3].binding = Shaders::BindingOfSampler(1);
    bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
    for (VkDescriptorSetLayoutBinding& b : bindings) {
        b.descriptorCount = 1;
        b.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    auto li = Make<VkDescriptorSetLayoutCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO);
    li.bindingCount = 4;
    li.pBindings = bindings;
    VkResult r = vkCreateDescriptorSetLayout(dev, &li, nullptr, &g.pixelLayout);
    auto empty = Make<VkDescriptorSetLayoutCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO);
    if (r == VK_SUCCESS) r = vkCreateDescriptorSetLayout(dev, &empty, nullptr, &g.vertexLayout);
    const VkDescriptorSetLayout sets[2] = { g.vertexLayout, g.pixelLayout };
    static_assert(Shaders::kSetVertex == 0 && Shaders::kSetPixel == 1, "set 0 vertex, set 1 pixel");
    auto pl = Make<VkPipelineLayoutCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO);
    pl.setLayoutCount = 2;
    pl.pSetLayouts = sets;
    if (r == VK_SUCCESS) r = vkCreatePipelineLayout(dev, &pl, nullptr, &g.layout);
    const VkDescriptorPoolSize sizes[3] = {
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, kSetsPerSlot },
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, kSetsPerSlot * 2 },
        { VK_DESCRIPTOR_TYPE_SAMPLER, kSetsPerSlot },
    };
    auto poolInfo = Make<VkDescriptorPoolCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO);
    poolInfo.maxSets = kSetsPerSlot;
    poolInfo.poolSizeCount = 3;
    poolInfo.pPoolSizes = sizes;
    for (unsigned s = 0; s < kFramesInFlight && r == VK_SUCCESS; ++s) {
        r = vkCreateDescriptorPool(dev, &poolInfo, nullptr, &g.pools[s]);
        g.poolFrame[s] = ~0ull;
    }
    if (r != VK_SUCCESS) {
        std::fprintf(stderr, "[BetterClouds] Vulkan: the pass's layouts and pools: %s: the shadows are not "
                             "supported\n", ResultName(r));
        return false;
    }
    std::string why;
    for (GpuBuffer& b : g.rings) {
        if (!CreateHostBuffer(kSlotBytes * kRingSlots, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, &b, &why)) {
            std::fprintf(stderr, "[BetterClouds] Vulkan: constants ring: %s: the shadows are not supported\n",
                         why.c_str());
            return false;
        }
    }
    g.resolve[0] = MakePipeline(kResolveMs, VK_FORMAT_R32_SFLOAT, VK_FORMAT_UNDEFINED, VK_FORMAT_UNDEFINED, 1);
    g.resolve[1] = MakePipeline(kResolve1x, VK_FORMAT_R32_SFLOAT, VK_FORMAT_UNDEFINED, VK_FORMAT_UNDEFINED, 1);
    if (!g.resolve[0] || !g.resolve[1]) return false;
    g.ready = true;
    std::fprintf(stderr, "[BetterClouds] Vulkan: shadow pass ready\n");
    return true;
}

// The depth copy at the frame's size, (re)made when the size changed (scene_depth.cpp EnsureImageLocked).
bool EnsureImageLocked(const AoTargets& t) {
    const uint32_t w = t.extent.width;
    const uint32_t h = t.extent.height;
    if (w == 0 || h == 0) return false;
    bool make = false;
    if (g_image == nullptr) {
        g_image = static_cast<Video::ImageBuffer*>(Video::ImageBuffer::CreateNewObject());
        if (g_image == nullptr) return false;
        make = true;
    }
    // Between the engine's ReleaseDynamic and RecreateDynamic (a device restart) the image has no
    // companion; no frame is recorded then.
    if (g_image->_data == nullptr) return false;
    if (make || w != g_width || h != g_height) {
        // A colour render buffer (kind 0), R32F (format 6), point filter, clamp.
        if (!g_image->Create(0, 0, 6, 0u, static_cast<int32_t>(w), static_cast<int32_t>(h), 0, 1, 1, 1, nullptr)) {
            LogOnce(1, "the depth copy's image could not be made: no shadows");
            return false;
        }
        g_width = w;
        g_height = h;
        g_companion = nullptr;
    }
    const auto* c = static_cast<const ImageBufferData*>(g_image->_data);
    if (!c->attachmentView || !c->image.view || !c->state.image) return false;
    if (c != g_companion || t.samples != g_samples) {
        g_companion = c;
        g_samples = t.samples;
        std::fprintf(stderr, "[BetterClouds] Vulkan: shadow depth copy %ux%u R32F made, from the main depth x%u%s\n",
                     w, h, t.samples, t.msrtss ? " (the tiler's resolve)" : "");
    }
    return true;
}

// The frame slot's pool and ring, reset when the slot's previous frame completed (BeginCommands waited
// for its fence).
void BeginSlotLocked(const AoTargets& t) {
    if (g.poolFrame[t.slot] == t.frame) return;
    vkResetDescriptorPool(LiveDevice(), g.pools[t.slot], 0);
    g.poolFrame[t.slot] = t.frame;
    g.ringAt[t.slot] = 0;
}

VkDeviceSize WriteConstantsLocked(unsigned slot, const CloudShadowBackend::Frame& f) {
    const VkDeviceSize align =
        std::max<VkDeviceSize>(LiveProperties().limits.minUniformBufferOffsetAlignment, kSlotBytes);
    const VkDeviceSize at = (g.ringAt[slot] + align - 1) / align * align;
    if (at + kSlotBytes > g.rings[slot].size) return ~0ull;
    std::memcpy(g.rings[slot].mapped + at, &f, sizeof(f));
    g.ringAt[slot] = at + kSlotBytes;
    return at;
}

// A set with every binding of the layout filled (each program reads its own of them).
VkDescriptorSet SetLocked(unsigned slot, VkDeviceSize constants, VkImageView t0, VkImageView t1, VkSampler s1) {
    auto ai = Make<VkDescriptorSetAllocateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO);
    ai.descriptorPool = g.pools[slot];
    ai.descriptorSetCount = 1;
    ai.pSetLayouts = &g.pixelLayout;
    VkDescriptorSet set = VK_NULL_HANDLE;
    if (vkAllocateDescriptorSets(LiveDevice(), &ai, &set) != VK_SUCCESS) {
        LogOnce(2, "more shadow passes in one frame than the pool holds: the rest are skipped");
        return VK_NULL_HANDLE;
    }
    const VkDescriptorBufferInfo buffer{ g.rings[slot].buffer, constants, kSlotBytes };
    const VkDescriptorImageInfo images[2] = {
        { VK_NULL_HANDLE, t0, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
        { VK_NULL_HANDLE, t1, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
    };
    const VkDescriptorImageInfo sampler{ s1, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED };
    VkWriteDescriptorSet writes[4];
    for (VkWriteDescriptorSet& w : writes) {
        w = Make<VkWriteDescriptorSet>(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
        w.dstSet = set;
        w.descriptorCount = 1;
    }
    writes[0].dstBinding = Shaders::BindingOfConstantBuffer(0);
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[0].pBufferInfo = &buffer;
    writes[1].dstBinding = Shaders::BindingOfTexture(0);
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    writes[1].pImageInfo = &images[0];
    writes[2].dstBinding = Shaders::BindingOfTexture(1);
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    writes[2].pImageInfo = &images[1];
    writes[3].dstBinding = Shaders::BindingOfSampler(1);
    writes[3].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
    writes[3].pImageInfo = &sampler;
    vkUpdateDescriptorSets(LiveDevice(), 4, writes, 0, nullptr);
    return set;
}

void Fullscreen(VkCommandBuffer cb, VkPipeline pipeline, VkDescriptorSet set, uint32_t w, uint32_t h) {
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    // The engine's Y flip (draw.cpp), as the AO pass sets it.
    const float fw = static_cast<float>(w);
    const float fh = static_cast<float>(h);
    const VkViewport viewport{ 0.0f, fh, fw, -fh, 0.0f, 1.0f };
    vkCmdSetViewport(cb, 0, 1, &viewport);
    const VkRect2D scissor{ { 0, 0 }, { w, h } };
    vkCmdSetScissor(cb, 0, 1, &scissor);
    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, g.layout, Shaders::kSetPixel, 1, &set, 0, nullptr);
    vkCmdDraw(cb, 3, 1, 0, 0);
}

// The main depth (SHADER_READ_ONLY, DrawAoBegin) -> the copy, left SHADER_READ_ONLY.
bool CopyDepthLocked(const AoTargets& t, VkDeviceSize constants, VkImageView noise, VkSampler noiseSampler) {
    const VkDescriptorSet set = SetLocked(t.slot, constants, t.depthView, noise, noiseSampler);
    if (!set) return false;
    auto* c = static_cast<ImageBufferData*>(g_image->_data);
    VkCommandBuffer cb = t.cb;
    std::vector<VkImageMemoryBarrier2> barriers;
    TransitionImage(c->state, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, &barriers);
    FlushBarriers(cb, &barriers);
    auto color = Make<VkRenderingAttachmentInfo>(VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO);
    color.imageView = c->attachmentView;
    color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;   // the draw covers the whole target
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    auto ri = Make<VkRenderingInfo>(VK_STRUCTURE_TYPE_RENDERING_INFO);
    ri.renderArea = { { 0, 0 }, { g_width, g_height } };
    ri.layerCount = 1;
    ri.colorAttachmentCount = 1;
    ri.pColorAttachments = &color;
    vkCmdBeginRendering(cb, &ri);
    DrawNoteAoPass();
    // The tiler's MSAA: the depth image is single-sampled (sample 0, resolved by the main passes).
    const bool ms = t.samples > 1 && !t.msrtss;
    Fullscreen(cb, g.resolve[ms ? 0 : 1], set, g_width, g_height);
    vkCmdEndRendering(cb);
    TransitionImage(c->state, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, &barriers);
    FlushBarriers(cb, &barriers);
    return true;
}

// The multiply into the main colour, in DrawAoBeginApply's pass.
bool ApplyLocked(const AoTargets& t, VkDeviceSize constants, VkImageView noise, VkSampler noiseSampler) {
    const auto* c = static_cast<const ImageBufferData*>(g_image->_data);
    const VkDescriptorSet set = SetLocked(t.slot, constants, c->image.view, noise, noiseSampler);
    if (!set) return false;
    VkFormat depth = VK_FORMAT_UNDEFINED;
    VkFormat stencil = VK_FORMAT_UNDEFINED;
    if (!DrawAoBeginApply(&depth, &stencil)) return false;
    const VkPipeline pipeline = ApplyPipeline(t.colorFormat, depth, stencil, t.samples);
    if (!pipeline) return false;
    Fullscreen(t.cb, pipeline, set, t.extent.width, t.extent.height);
    return true;
}

void ReleaseImageLocked() {
    if (g_image == nullptr) return;
    // The companion's destructor retires the image (the frame being recorded may still read it).
    delete g_image;
    g_image = nullptr;
    g_companion = nullptr;
    g_width = 0;
    g_height = 0;
    g_samples = 0;
    std::fprintf(stderr, "[BetterClouds] Vulkan: shadow depth copy released\n");
}

}  // namespace

void Shutdown() {
    std::lock_guard<std::mutex> guard(g_mutex);
    ReleaseImageLocked();
    DestroyLocked();
}

}  // namespace CloudShadow
}  // namespace VideoVulkan

namespace CloudShadowBackend {

using namespace VideoVulkan::CloudShadow;

bool Supported() {
    if (!VideoVulkan::DeviceLive()) return false;
    std::lock_guard<std::mutex> guard(g_mutex);
    return InitLocked();
}

void Request(bool on) {
    if (g_requested.exchange(on) == on) return;
    VideoVulkan::DrawRequestReadableDepth(VideoVulkan::kDepthReaderClouds, on);
    if (on) return;
    std::lock_guard<std::mutex> guard(g_mutex);
    ReleaseImageLocked();
}

void Draw(Video::Context& /*context*/, const Video::ImageBuffer* noise, const Frame& frame) {
    if (!g_requested.load() || noise == nullptr || noise->_data == nullptr) return;
    const auto* n = static_cast<const VideoVulkan::ImageBufferData*>(noise->_data);
    if (!n->image.view) return;
    std::lock_guard<std::mutex> guard(g_mutex);
    if (!InitLocked()) return;
    // Outside a frame, or the frames before the main depth is remade readable: no shadows this frame.
    VideoVulkan::AoTargets t;
    if (!VideoVulkan::DrawAoBegin(&t)) return;
    bool ok = t.slot < VideoVulkan::kFramesInFlight && EnsureImageLocked(t);
    VkDeviceSize constants = ~0ull;
    if (ok) {
        BeginSlotLocked(t);
        Frame f = frame;
        f.screen[0] = 1.0f / static_cast<float>(t.extent.width);
        f.screen[1] = 1.0f / static_cast<float>(t.extent.height);
        constants = WriteConstantsLocked(t.slot, f);
        ok = constants != ~0ull;
    }
    const VkSampler sampler = n->sampler ? n->sampler : VideoVulkan::DefaultSampler();
    ok = ok && CopyDepthLocked(t, constants, n->image.view, sampler);
    ok = ok && ApplyLocked(t, constants, n->image.view, sampler);
    if (!ok) LogOnce(4, "a step of the shadow pass failed (logged above): those frames have no cloud shadows");
    VideoVulkan::DrawAoEnd();
}

}  // namespace CloudShadowBackend
