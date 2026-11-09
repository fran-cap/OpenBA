// ============================================================================
// src/render/vulkan/dof/dof_pass.cpp -- ext.depth_of_field's depth blur on this backend
// ============================================================================
// NOT RETAIL. The contract is src/render/common/dof/dof_backend.h (docs/extensions/DOF.md section 3.1); the
// DX11 pass is src/render/dx11/dof/dof_pass.cpp.
//
// THE FRAME (the scene-draw seam, the recording thread, after the main entry's last list), in draw.cpp's AO
// bracket as ext.better_clouds' shadow pass runs:
//   the depth      THE SHARED DEPTH COPY (depth_copy/depth_copy.h): taken as it is when an earlier reader
//                  made it this frame and nothing has written the depth since (DrawAoBeginKeepDepth), else
//                  DrawAoBegin (the main depth readable: this pass's own reader bit) and the copy made here;
//   the colour     DrawAoCopyMainColor: the main colour as it stands (the resolved frame so far) copied into
//                  an engine BGRA8 image of ours;
//   prefilter      copy + depth -> half A       (dof.hlsl, as every pass below; dynamic rendering on our own
//   gather         A -> half B                   images, each left in SHADER_READ_ONLY)
//   tent           B -> half A
//   composite      DrawAoBeginApply's pass on the main colour: A + depth, blend SRC_ALPHA /
//                  ONE_MINUS_SRC_ALPHA, RGB only (the alpha holds the shore depth the water reads)
//   DrawAoEnd      the engine's next draw rebinds everything
// Every program is our own DXBC through the backend's one DXBC -> SPIR-V converter.
//
// THE IMAGES are engine Video::ImageBuffers (Create: 2D, a colour render buffer (kind 0), BGRA8 (format 5)),
// so their companions are this backend's ImageBufferData (attachment view, sampled view, layout tracking),
// as the depth copy's image is. Remade when the frame's size changes, released when the row goes off and
// with the device (Shutdown).
//
// LIFETIME. The modules, layouts, pools, pipelines, sampler and the constants ring per device (Shutdown,
// from DeviceData::Shutdown, destroys them; the next device makes them again on its first Supported()).
// ============================================================================
#include "dof/dof_backend.h"
#include "dof/dof_pass.h"

#include "VideoVulkanData.h"
#include "depth_copy/depth_copy.h"
#include "device_objects.h"
#include "draw.h"
#include "pipelines.h"
#include "resources.h"
#include "shader_convert.h"
#include "vulkan_entry.h"

#include "ext/derived_cache/derived_cache.h"
#include "video/ImageBuffer.h"

#include "ao/shaders/VSFullscreen.h"
#include "dof/dof_composite_ps.h"
#include "dof/dof_gather_ps.h"
#include "dof/dof_prefilter_ps.h"
#include "dof/dof_tent_ps.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace VideoVulkan {
namespace Dof {

namespace {

constexpr uint32_t kSetsPerSlot = 16;         // a frame draws four passes once
constexpr VkDeviceSize kRingSlots = 4;        // a frame writes one constants block
constexpr VkDeviceSize kSlotBytes = 256;      // one block, at the strictest uniform offset alignment
static_assert(sizeof(DofBackend::Frame) <= kSlotBytes, "one constants block per ring slot");
constexpr VkFormat kHalfFormat = VK_FORMAT_B8G8R8A8_UNORM;   // the engine's format 5

enum Program : int {
    kVsFullscreen,
    kPrefilter,
    kGather,
    kTent,
    kComposite,
    kProgramCount
};

struct ProgramBlob {
    const uint8_t* bytes;
    size_t size;
    Shaders::Stage stage;
    const char* name;
};

const ProgramBlob kBlobs[kProgramCount] = {
    { g_ao_VSFullscreen, sizeof(g_ao_VSFullscreen), Shaders::Stage::Vertex, "VSFullscreen" },
    { g_dof_PSPrefilter, sizeof(g_dof_PSPrefilter), Shaders::Stage::Pixel, "PSDofPrefilter" },
    { g_dof_PSGather, sizeof(g_dof_PSGather), Shaders::Stage::Pixel, "PSDofGather" },
    { g_dof_PSTent, sizeof(g_dof_PSTent), Shaders::Stage::Pixel, "PSDofTent" },
    { g_dof_PSComposite, sizeof(g_dof_PSComposite), Shaders::Stage::Pixel, "PSDofComposite" },
};

// The device objects of the pass, one set per device (Shutdown resets it).
struct State {
    VkDevice device = VK_NULL_HANDLE;
    bool ready = false;
    bool failed = false;                                     // a pipeline the driver refused: not supported
    VkShaderModule modules[kProgramCount] = {};
    VkDescriptorSetLayout vertexLayout = VK_NULL_HANDLE;     // empty: VSFullscreen reads nothing
    VkDescriptorSetLayout pixelLayout = VK_NULL_HANDLE;      // b0, t0, t1, s0
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkPipeline half[kProgramCount] = {};                     // prefilter, gather, tent into a BGRA8 image
    std::unordered_map<uint64_t, VkPipeline> composite;      // by the main pass's shape
    VkSampler sampler = VK_NULL_HANDLE;                      // linear, clamp
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

// The images (engine objects; they outlive a device): the colour copy and the two half targets.
Video::ImageBuffer* g_images[3] = {};
uint32_t g_width = 0;
uint32_t g_height = 0;
const void* g_companions[3] = {};   // the companions the last (re)make was logged for

void LogOnce(uint32_t bit, const char* message) {
    if (g.logged & bit) return;
    g.logged |= bit;
    std::fprintf(stderr, "[DepthOfField] Vulkan: %s\n", message);
}

void ReleaseImagesLocked() {
    bool had = false;
    for (Video::ImageBuffer*& image : g_images) {
        if (image == nullptr) continue;
        // The companion's destructor retires the image (the frame being recorded may still read it).
        delete image;
        image = nullptr;
        had = true;
    }
    for (const void*& c : g_companions)
        c = nullptr;
    g_width = 0;
    g_height = 0;
    if (had) std::fprintf(stderr, "[DepthOfField] Vulkan: images released\n");
}

void DestroyLocked() {
    VkDevice dev = LiveDevice();
    if (dev) {
        for (VkPipeline p : g.half) {
            if (p) vkDestroyPipeline(dev, p, nullptr);
        }
        for (auto& kv : g.composite) {
            if (kv.second) vkDestroyPipeline(dev, kv.second, nullptr);
        }
        for (VkDescriptorPool p : g.pools) {
            if (p) vkDestroyDescriptorPool(dev, p, nullptr);
        }
        if (g.sampler) vkDestroySampler(dev, g.sampler, nullptr);
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

// The fullscreen triangle into `color` (and the depth / stencil formats and sample count of its pass):
// `over` blends alpha over the colour, RGB only (the composite); otherwise it writes everything.
VkPipeline MakePipeline(Program ps, VkFormat color, VkFormat depth, VkFormat stencil, uint32_t samples, bool over) {
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
    if (over) {
        blend.blendEnable = VK_TRUE;
        blend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blend.colorBlendOp = VK_BLEND_OP_ADD;
        blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        blend.alphaBlendOp = VK_BLEND_OP_ADD;
        blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT;
    } else {
        blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
                               VK_COLOR_COMPONENT_A_BIT;
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
        std::fprintf(stderr, "[DepthOfField] Vulkan: pipeline %s: %s: the depth blur is not supported\n",
                     kBlobs[ps].name, ResultName(r));
        g.failed = true;
        return VK_NULL_HANDLE;
    }
    return pipeline;
}

VkPipeline CompositePipeline(VkFormat color, VkFormat depth, VkFormat stencil, uint32_t samples) {
    const uint64_t key = (static_cast<uint64_t>(color) << 40) ^ (static_cast<uint64_t>(depth) << 24) ^
                         (static_cast<uint64_t>(stencil) << 8) ^ samples;
    auto found = g.composite.find(key);
    if (found != g.composite.end()) return found->second;
    const VkPipeline p = MakePipeline(kComposite, color, depth, stencil, samples, true);
    g.composite[key] = p;
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
        std::fprintf(stderr, "[DepthOfField] Vulkan: the depth blur is not supported on the Vulkan 1.1 path\n");
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
            std::fprintf(stderr, "[DepthOfField] Vulkan: %s did not convert (%s): the depth blur is not supported\n",
                         kBlobs[i].name, why.c_str());
            return false;
        }
        // The pixel layout holds b0, t0, t1 and s0: a program must read no more than that.
        if ((refl.constantBufferMask & ~1u) != 0 || (refl.textureMask & ~3u) != 0 || (refl.samplerMask & ~1u) != 0 ||
            refl.uavMask != 0) {
            std::fprintf(stderr, "[DepthOfField] Vulkan: %s reads more than the pass binds: the depth blur is not "
                                 "supported\n", kBlobs[i].name);
            return false;
        }
        g.modules[i] = CreateShaderModule(spirv, &why);
        if (!g.modules[i]) {
            std::fprintf(stderr, "[DepthOfField] Vulkan: %s: %s: the depth blur is not supported\n", kBlobs[i].name,
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
    bindings[3].binding = Shaders::BindingOfSampler(0);
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
    if (r == VK_SUCCESS) {
        auto si = Make<VkSamplerCreateInfo>(VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO);
        si.magFilter = VK_FILTER_LINEAR;
        si.minFilter = VK_FILTER_LINEAR;
        si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.maxLod = 0.0f;
        r = vkCreateSampler(dev, &si, nullptr, &g.sampler);
    }
    if (r != VK_SUCCESS) {
        std::fprintf(stderr, "[DepthOfField] Vulkan: the pass's layouts, pools and sampler: %s: the depth blur is "
                             "not supported\n", ResultName(r));
        return false;
    }
    std::string why;
    for (GpuBuffer& b : g.rings) {
        if (!CreateHostBuffer(kSlotBytes * kRingSlots, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, &b, &why)) {
            std::fprintf(stderr, "[DepthOfField] Vulkan: constants ring: %s: the depth blur is not supported\n",
                         why.c_str());
            return false;
        }
    }
    for (Program p : { kPrefilter, kGather, kTent }) {
        g.half[p] = MakePipeline(p, kHalfFormat, VK_FORMAT_UNDEFINED, VK_FORMAT_UNDEFINED, 1, false);
        if (!g.half[p]) return false;
    }
    g.ready = true;
    std::fprintf(stderr, "[DepthOfField] Vulkan: pass ready\n");
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

VkDeviceSize WriteConstantsLocked(unsigned slot, const DofBackend::Frame& f) {
    const VkDeviceSize align =
        std::max<VkDeviceSize>(LiveProperties().limits.minUniformBufferOffsetAlignment, kSlotBytes);
    const VkDeviceSize at = (g.ringAt[slot] + align - 1) / align * align;
    if (at + kSlotBytes > g.rings[slot].size) return ~0ull;
    std::memcpy(g.rings[slot].mapped + at, &f, sizeof(f));
    g.ringAt[slot] = at + kSlotBytes;
    return at;
}

// A set with every binding of the layout filled (each program reads its own of them).
VkDescriptorSet SetLocked(unsigned slot, VkDeviceSize constants, VkImageView t0, VkImageView t1) {
    auto ai = Make<VkDescriptorSetAllocateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO);
    ai.descriptorPool = g.pools[slot];
    ai.descriptorSetCount = 1;
    ai.pSetLayouts = &g.pixelLayout;
    VkDescriptorSet set = VK_NULL_HANDLE;
    if (vkAllocateDescriptorSets(LiveDevice(), &ai, &set) != VK_SUCCESS) {
        LogOnce(2, "more depth blur passes in one frame than the pool holds: the rest are skipped");
        return VK_NULL_HANDLE;
    }
    const VkDescriptorBufferInfo buffer{ g.rings[slot].buffer, constants, kSlotBytes };
    const VkDescriptorImageInfo images[2] = {
        { VK_NULL_HANDLE, t0, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
        { VK_NULL_HANDLE, t1, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
    };
    const VkDescriptorImageInfo sampler{ g.sampler, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED };
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
    writes[3].dstBinding = Shaders::BindingOfSampler(0);
    writes[3].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
    writes[3].pImageInfo = &sampler;
    vkUpdateDescriptorSets(LiveDevice(), 4, writes, 0, nullptr);
    return set;
}

void Fullscreen(VkCommandBuffer cb, VkPipeline pipeline, VkDescriptorSet set, uint32_t w, uint32_t h) {
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    // The engine's Y flip (draw.cpp), as the AO pass sets it; the passes address by pixel either way.
    const float fw = static_cast<float>(w);
    const float fh = static_cast<float>(h);
    const VkViewport viewport{ 0.0f, fh, fw, -fh, 0.0f, 1.0f };
    vkCmdSetViewport(cb, 0, 1, &viewport);
    const VkRect2D scissor{ { 0, 0 }, { w, h } };
    vkCmdSetScissor(cb, 0, 1, &scissor);
    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, g.layout, Shaders::kSetPixel, 1, &set, 0, nullptr);
    vkCmdDraw(cb, 3, 1, 0, 0);
}

ImageBufferData* CompanionOf(int i) {
    return g_images[i] != nullptr ? static_cast<ImageBufferData*>(g_images[i]->_data) : nullptr;
}

// The three images at the frame's size, (re)made when it changed. The caller holds g_mutex.
bool EnsureImagesLocked(const AoTargets& t) {
    const uint32_t w = t.extent.width;
    const uint32_t h = t.extent.height;
    if (w == 0 || h == 0) return false;
    const uint32_t sizes[3][2] = { { w, h }, { (w + 1) / 2, (h + 1) / 2 }, { (w + 1) / 2, (h + 1) / 2 } };
    const bool resize = w != g_width || h != g_height;
    for (int i = 0; i < 3; ++i) {
        bool make = resize;
        if (g_images[i] == nullptr) {
            g_images[i] = static_cast<Video::ImageBuffer*>(Video::ImageBuffer::CreateNewObject());
            if (g_images[i] == nullptr) return false;
            make = true;
        }
        // Between the engine's ReleaseDynamic and RecreateDynamic (a device restart) the image has no
        // companion; no frame is recorded then, so this is the pass's own guard, not a retail path.
        if (g_images[i]->_data == nullptr) return false;
        if (make && !g_images[i]->Create(0, 0, 5, 0u, static_cast<int32_t>(sizes[i][0]),
                                         static_cast<int32_t>(sizes[i][1]), 0, 1, 1, 1, nullptr)) {
            LogOnce(1, "an image of the pass could not be made: no depth blur");
            return false;
        }
        const ImageBufferData* c = CompanionOf(i);
        if (!c->attachmentView || !c->image.view || !c->state.image) return false;
    }
    g_width = w;
    g_height = h;
    if (CompanionOf(0) != g_companions[0]) {
        for (int i = 0; i < 3; ++i)
            g_companions[i] = CompanionOf(i);
        std::fprintf(stderr, "[DepthOfField] Vulkan: images %ux%u (half %ux%u) made\n", w, h, sizes[1][0],
                     sizes[1][1]);
    }
    return true;
}

// One half-resolution pass into image `target` (left in SHADER_READ_ONLY).
bool HalfPassLocked(const AoTargets& t, Program ps, int target, VkDeviceSize constants, VkImageView t0,
                    VkImageView t1) {
    const VkDescriptorSet set = SetLocked(t.slot, constants, t0, t1);
    if (!set) return false;
    ImageBufferData* c = CompanionOf(target);
    VkCommandBuffer cb = t.cb;
    std::vector<VkImageMemoryBarrier2> barriers;
    TransitionImage(c->state, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, &barriers);
    FlushBarriers(cb, &barriers);
    const uint32_t w = c->image.width;
    const uint32_t h = c->image.height;
    auto color = Make<VkRenderingAttachmentInfo>(VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO);
    color.imageView = c->attachmentView;
    color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;   // the draw covers the whole target
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    auto ri = Make<VkRenderingInfo>(VK_STRUCTURE_TYPE_RENDERING_INFO);
    ri.renderArea = { { 0, 0 }, { w, h } };
    ri.layerCount = 1;
    ri.colorAttachmentCount = 1;
    ri.pColorAttachments = &color;
    vkCmdBeginRendering(cb, &ri);
    Fullscreen(cb, g.half[ps], set, w, h);
    vkCmdEndRendering(cb);
    TransitionImage(c->state, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, &barriers);
    FlushBarriers(cb, &barriers);
    DrawNoteAoPass();
    return true;
}

// The blend over the main colour, in DrawAoBeginApply's pass.
bool CompositeLocked(const AoTargets& t, VkDeviceSize constants, VkImageView blurred, VkImageView depth) {
    const VkDescriptorSet set = SetLocked(t.slot, constants, blurred, depth);
    if (!set) return false;
    VkFormat depthFormat = VK_FORMAT_UNDEFINED;
    VkFormat stencilFormat = VK_FORMAT_UNDEFINED;
    if (!DrawAoBeginApply(&depthFormat, &stencilFormat)) return false;
    const VkPipeline pipeline = CompositePipeline(t.colorFormat, depthFormat, stencilFormat, t.samples);
    if (!pipeline) return false;
    Fullscreen(t.cb, pipeline, set, t.extent.width, t.extent.height);
    return true;
}

}  // namespace

void Shutdown() {
    std::lock_guard<std::mutex> guard(g_mutex);
    // The images go with the device, as the depth copy's (depth_copy.cpp Shutdown): an engine image nothing
    // deletes would hold its attachment view past the device. The next frame on a new device makes them again.
    ReleaseImagesLocked();
    DestroyLocked();
}

}  // namespace Dof
}  // namespace VideoVulkan

namespace DofBackend {
BANISHED_RENDER_IMPL_BEGIN

using namespace VideoVulkan::Dof;

bool Supported() {
    if (!VideoVulkan::DeviceLive()) return false;
    {
        std::lock_guard<std::mutex> guard(g_mutex);
        if (!InitLocked()) return false;
    }
    return VideoVulkan::DepthCopy::Supported();
}

void Request(bool on) {
    if (g_requested.exchange(on) == on) return;
    VideoVulkan::DrawRequestReadableDepth(VideoVulkan::kDepthReaderDof, on);
    VideoVulkan::DepthCopy::Request(VideoVulkan::kDepthReaderDof, on);
    if (!on) {
        std::lock_guard<std::mutex> guard(g_mutex);
        ReleaseImagesLocked();
    }
}

void Draw(Video::Context& /*context*/, const Frame& frame) {
    if (!g_requested.load()) return;
    std::lock_guard<std::mutex> guard(g_mutex);
    if (!InitLocked()) return;
    // Outside a frame, or the frames before the main depth is remade readable: no blur this frame.
    // The shared copy when it is still fresh, else made here.
    const Video::ImageBuffer* depthCopy = VideoVulkan::DepthCopy::Fresh();
    VideoVulkan::AoTargets t;
    if (depthCopy != nullptr) {
        if (!VideoVulkan::DrawAoBeginKeepDepth(&t)) return;
    } else {
        if (!VideoVulkan::DrawAoBegin(&t)) return;
        depthCopy = VideoVulkan::DepthCopy::InBracket(t);
    }
    bool ok = t.slot < VideoVulkan::kFramesInFlight && depthCopy != nullptr && EnsureImagesLocked(t);
    ok = ok && VideoVulkan::DrawAoCopyMainColor(g_images[0]);
    VkDeviceSize constants = ~0ull;
    if (ok) {
        BeginSlotLocked(t);
        const uint32_t hw = (t.extent.width + 1) / 2;
        const uint32_t hh = (t.extent.height + 1) / 2;
        Frame f = frame;
        f.screen[0] = 1.0f / static_cast<float>(t.extent.width);
        f.screen[1] = 1.0f / static_cast<float>(t.extent.height);
        f.screen[2] = static_cast<float>(t.extent.width);
        f.screen[3] = static_cast<float>(t.extent.height);
        f.halfRes[0] = 1.0f / static_cast<float>(hw);
        f.halfRes[1] = 1.0f / static_cast<float>(hh);
        f.halfRes[2] = static_cast<float>(hw);
        f.halfRes[3] = static_cast<float>(hh);
        f.lens[2] = MaxCocFor(frame.lens[2], static_cast<int>(t.extent.height));
        constants = WriteConstantsLocked(t.slot, f);
        ok = constants != ~0ull;
    }
    const VkImageView depth = VideoVulkan::DepthCopy::ViewOf(depthCopy);
    const VkImageView copy = ok ? CompanionOf(0)->image.view : VK_NULL_HANDLE;
    const VkImageView a = ok ? CompanionOf(1)->image.view : VK_NULL_HANDLE;
    const VkImageView b = ok ? CompanionOf(2)->image.view : VK_NULL_HANDLE;
    ok = ok && HalfPassLocked(t, kPrefilter, 1, constants, copy, depth);
    ok = ok && HalfPassLocked(t, kGather, 2, constants, a, depth);
    ok = ok && HalfPassLocked(t, kTent, 1, constants, b, depth);
    ok = ok && CompositeLocked(t, constants, a, depth);
    if (!ok) LogOnce(4, "a step of the depth blur failed (logged above): those frames are not blurred");
    VideoVulkan::DrawAoEnd();
}

BANISHED_RENDER_IMPL_END
}  // namespace DofBackend
