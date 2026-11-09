// ============================================================================
// src/render/vulkan/fancy_terrain/terrain_light.cpp -- ext.fancy_terrain's terrain light on this backend
// ============================================================================
// NOT RETAIL. The contract is src/render/common/fancy_terrain/terrain_light_backend.h; the DX11 pass is
// src/render/dx11/fancy_terrain/terrain_light.cpp. The frame is the cloud shadow's
// (better_clouds/cloud_shadow.cpp), in draw.cpp's AO bracket:
//   the uploads      the horizon planes and the ground height as three images, all of them when they are
//                    new (a new device, a new generation), else the dirty rectangle (UploadImage with an
//                    offset, through the upload batch, which is submitted before the frame)
//   the copy         THE SHARED DEPTH COPY (depth_copy/depth_copy.h), fresh or made here
//   the apply        DrawAoBeginApply's pass on the main colour: AO's fullscreen triangle with our program
//                    (terrain_light.hlsl), blend colour ZERO / SRC_COLOR, RGB only
//   DrawAoEnd        the engine's next draw rebinds everything
//
// LIFETIME. The images, modules, layouts, pools, pipelines and the constants ring per device (Shutdown,
// from DeviceData::Shutdown, destroys them; the next device makes them again and uploads everything).
// ============================================================================
#include "fancy_terrain/terrain_light_backend.h"
#include "fancy_terrain/terrain_light_pass.h"

#include "VideoVulkanData.h"
#include "depth_copy/depth_copy.h"
#include "device_objects.h"
#include "draw.h"
#include "pipelines.h"
#include "resources.h"
#include "shader_convert.h"
#include "vulkan_entry.h"

#include "ext/derived_cache/derived_cache.h"

#include "ao/shaders/VSFullscreen.h"
#include "fancy_terrain/terrain_light_ps.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace VideoVulkan {
namespace TerrainLight {

namespace {

constexpr uint32_t kSetsPerSlot = 8;          // a frame draws the pass once
constexpr VkDeviceSize kRingSlots = 8;
constexpr VkDeviceSize kSlotBytes = 256;      // one block, at the strictest uniform offset alignment
constexpr uint32_t kMapTextures = 3;          // t1..t3
static_assert(sizeof(TerrainLightBackend::Frame) <= kSlotBytes, "one constants block per ring slot");

enum Program : int {
    kVsFullscreen,
    kLight,
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
    { g_ft_PSTerrainLight, sizeof(g_ft_PSTerrainLight), Shaders::Stage::Pixel, "PSTerrainLight", 1, 0xF, 2 },
};

// The device objects of the pass, one set per device (Shutdown resets it).
struct State {
    VkDevice device = VK_NULL_HANDLE;
    bool ready = false;
    bool failed = false;
    VkShaderModule modules[kProgramCount] = {};
    VkDescriptorSetLayout vertexLayout = VK_NULL_HANDLE;     // empty: VSFullscreen reads nothing
    VkDescriptorSetLayout pixelLayout = VK_NULL_HANDLE;      // b0, t0..t3, s1
    VkPipelineLayout layout = VK_NULL_HANDLE;
    std::unordered_map<uint64_t, VkPipeline> apply;          // by the main pass's shape
    VkDescriptorPool pools[kFramesInFlight] = {};
    uint64_t poolFrame[kFramesInFlight] = {};
    GpuBuffer rings[kFramesInFlight];
    VkDeviceSize ringAt[kFramesInFlight] = {};
    VkSampler sampler = VK_NULL_HANDLE;                      // the shared sampler cache owns it
    GpuImage maps[kMapTextures];                             // horizon 0, horizon 1, ground
    int width = 0;
    int height = 0;
    uint32_t generation = 0;
    std::vector<uint8_t> staging;                            // a dirty rectangle, packed
    uint32_t logged = 0;
};

State g;
// Every entry point holds it for its whole body.
std::mutex g_mutex;

std::atomic<bool> g_requested{false};

void LogOnce(uint32_t bit, const char* message) {
    if (g.logged & bit) return;
    g.logged |= bit;
    std::fprintf(stderr, "[FancyTerrain] Vulkan: %s\n", message);
}

void DestroyMapsLocked() {
    for (GpuImage& m : g.maps)
        DestroyImageNow(&m);
    g.width = 0;
    g.height = 0;
    g.generation = 0;
}

void DestroyLocked() {
    VkDevice dev = LiveDevice();
    DestroyMapsLocked();
    if (dev) {
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

// The fullscreen triangle into the main pass (its colour format, depth / stencil formats and sample
// count), multiply, RGB only.
VkPipeline MakePipeline(VkFormat color, VkFormat depth, VkFormat stencil, uint32_t samples) {
    VkPipelineShaderStageCreateInfo stages[2];
    stages[0] = Make<VkPipelineShaderStageCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO);
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = g.modules[kVsFullscreen];
    stages[0].pName = "main";
    stages[1] = Make<VkPipelineShaderStageCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO);
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = g.modules[kLight];
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
    blend.blendEnable = VK_TRUE;
    blend.srcColorBlendFactor = VK_BLEND_FACTOR_ZERO;
    blend.dstColorBlendFactor = VK_BLEND_FACTOR_SRC_COLOR;
    blend.colorBlendOp = VK_BLEND_OP_ADD;
    blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blend.alphaBlendOp = VK_BLEND_OP_ADD;
    blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT;
    auto cbs = Make<VkPipelineColorBlendStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO);
    cbs.attachmentCount = 1;
    cbs.pAttachments = &blend;
    // The stride state as the AO pass's pipelines carry it: the game's draws in the same command buffer set
    // strides with vkCmdBindVertexBuffers2.
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
        std::fprintf(stderr, "[FancyTerrain] Vulkan: pipeline: %s: the terrain light is not supported\n",
                     ResultName(r));
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
    const VkPipeline p = MakePipeline(color, depth, stencil, samples);
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
        std::fprintf(stderr, "[FancyTerrain] Vulkan: the terrain light is not supported on the Vulkan 1.1 path\n");
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
            std::fprintf(stderr, "[FancyTerrain] Vulkan: %s did not convert (%s): the terrain light is not "
                                 "supported\n", kBlobs[i].name, why.c_str());
            return false;
        }
        if ((refl.constantBufferMask & ~kBlobs[i].constants) != 0 || (refl.textureMask & ~kBlobs[i].textures) != 0 ||
            (refl.samplerMask & ~kBlobs[i].samplers) != 0 || refl.uavMask != 0) {
            std::fprintf(stderr, "[FancyTerrain] Vulkan: %s reads more than the pass binds: the terrain light is "
                                 "not supported\n", kBlobs[i].name);
            return false;
        }
        g.modules[i] = CreateShaderModule(spirv, &why);
        if (!g.modules[i]) {
            std::fprintf(stderr, "[FancyTerrain] Vulkan: %s: %s: the terrain light is not supported\n",
                         kBlobs[i].name, why.c_str());
            return false;
        }
    }

    VkDescriptorSetLayoutBinding bindings[6] = {};
    bindings[0].binding = Shaders::BindingOfConstantBuffer(0);
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    for (uint32_t t = 0; t < 4; ++t) {
        bindings[1 + t].binding = Shaders::BindingOfTexture(t);
        bindings[1 + t].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    }
    bindings[5].binding = Shaders::BindingOfSampler(1);
    bindings[5].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
    for (VkDescriptorSetLayoutBinding& b : bindings) {
        b.descriptorCount = 1;
        b.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    auto li = Make<VkDescriptorSetLayoutCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO);
    li.bindingCount = 6;
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
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, kSetsPerSlot * 4 },
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
        std::fprintf(stderr, "[FancyTerrain] Vulkan: the pass's layouts and pools: %s: the terrain light is not "
                             "supported\n", ResultName(r));
        return false;
    }
    std::string why;
    for (GpuBuffer& b : g.rings) {
        if (!CreateHostBuffer(kSlotBytes * kRingSlots, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, &b, &why)) {
            std::fprintf(stderr, "[FancyTerrain] Vulkan: constants ring: %s: the terrain light is not supported\n",
                         why.c_str());
            return false;
        }
    }
    SamplerDesc sd;
    sd.magLinear = 1;
    sd.minLinear = 1;
    sd.addressU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sd.addressV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sd.addressW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    g.sampler = GetSampler(sd);
    g.ready = true;
    std::fprintf(stderr, "[FancyTerrain] Vulkan: terrain light ready\n");
    return true;
}

// `rows` rows of `rowBytes` from `src` (stride `pitch`) into the staging vector, packed.
const uint8_t* Pack(const uint8_t* src, size_t pitch, size_t rowBytes, int rows) {
    g.staging.resize(rowBytes * static_cast<size_t>(rows));
    for (int r = 0; r < rows; ++r)
        std::memcpy(g.staging.data() + rowBytes * r, src + pitch * r, rowBytes);
    return g.staging.data();
}

// The CPU data into the images: everything when they are new, else the dirty rectangle.
bool UploadLocked(const TerrainLightBackend::Textures& t) {
    if (t.width <= 0 || t.height <= 0 || t.horizon0 == nullptr || t.horizon1 == nullptr || t.ground == nullptr)
        return false;
    bool all = false;
    if (g.width != t.width || g.height != t.height || !g.maps[0].image) {
        DestroyMapsLocked();
        const VkFormat formats[kMapTextures] = { VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM,
                                                 VK_FORMAT_R16_SFLOAT };
        for (uint32_t i = 0; i < kMapTextures; ++i) {
            ImageDesc d;
            d.width = static_cast<uint32_t>(t.width);
            d.height = static_cast<uint32_t>(t.height);
            d.format = formats[i];
            std::string why;
            if (!CreateImage(d, &g.maps[i], &why)) {
                std::fprintf(stderr, "[FancyTerrain] Vulkan: map image %dx%d: %s\n", t.width, t.height, why.c_str());
                DestroyMapsLocked();
                return false;
            }
        }
        g.width = t.width;
        g.height = t.height;
        all = true;
        std::fprintf(stderr, "[FancyTerrain] Vulkan: map images %dx%d made\n", t.width, t.height);
    }
    if (g.generation != t.generation) all = true;
    int x0 = all ? 0 : t.dirtyX0;
    int y0 = all ? 0 : t.dirtyY0;
    int x1 = all ? t.width : t.dirtyX1;
    int y1 = all ? t.height : t.dirtyY1;
    if (x1 > x0 && y1 > y0) {
        const int w = x1 - x0;
        const int h = y1 - y0;
        const uint8_t* sources[kMapTextures] = { t.horizon0, t.horizon1, reinterpret_cast<const uint8_t*>(t.ground) };
        const size_t texel[kMapTextures] = { 4, 4, 2 };
        for (uint32_t i = 0; i < kMapTextures; ++i) {
            const size_t pitch = static_cast<size_t>(t.width) * texel[i];
            const uint8_t* at = sources[i] + (static_cast<size_t>(y0) * t.width + x0) * texel[i];
            const size_t rowBytes = static_cast<size_t>(w) * texel[i];
            const uint8_t* packed = Pack(at, pitch, rowBytes, h);
            ImageRegion region;
            region.width = static_cast<uint32_t>(w);
            region.height = static_cast<uint32_t>(h);
            region.x = x0;
            region.y = y0;
            UploadImage(g.maps[i], &region, 1, packed, rowBytes * static_cast<size_t>(h));
        }
    }
    g.generation = t.generation;
    return true;
}

// The frame slot's pool and ring, reset when the slot's previous frame completed.
void BeginSlotLocked(const AoTargets& t) {
    if (g.poolFrame[t.slot] == t.frame) return;
    vkResetDescriptorPool(LiveDevice(), g.pools[t.slot], 0);
    g.poolFrame[t.slot] = t.frame;
    g.ringAt[t.slot] = 0;
}

VkDeviceSize WriteConstantsLocked(unsigned slot, const TerrainLightBackend::Frame& f) {
    const VkDeviceSize align =
        std::max<VkDeviceSize>(LiveProperties().limits.minUniformBufferOffsetAlignment, kSlotBytes);
    const VkDeviceSize at = (g.ringAt[slot] + align - 1) / align * align;
    if (at + kSlotBytes > g.rings[slot].size) return ~0ull;
    std::memcpy(g.rings[slot].mapped + at, &f, sizeof(f));
    g.ringAt[slot] = at + kSlotBytes;
    return at;
}

VkDescriptorSet SetLocked(unsigned slot, VkDeviceSize constants, VkImageView depth) {
    auto ai = Make<VkDescriptorSetAllocateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO);
    ai.descriptorPool = g.pools[slot];
    ai.descriptorSetCount = 1;
    ai.pSetLayouts = &g.pixelLayout;
    VkDescriptorSet set = VK_NULL_HANDLE;
    if (vkAllocateDescriptorSets(LiveDevice(), &ai, &set) != VK_SUCCESS) {
        LogOnce(2, "more terrain light passes in one frame than the pool holds: the rest are skipped");
        return VK_NULL_HANDLE;
    }
    const VkDescriptorBufferInfo buffer{ g.rings[slot].buffer, constants, kSlotBytes };
    const VkImageView views[4] = { depth, g.maps[0].view, g.maps[1].view, g.maps[2].view };
    VkDescriptorImageInfo images[4];
    for (int i = 0; i < 4; ++i) images[i] = { VK_NULL_HANDLE, views[i], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
    const VkDescriptorImageInfo sampler{ g.sampler, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED };
    VkWriteDescriptorSet writes[6];
    for (VkWriteDescriptorSet& w : writes) {
        w = Make<VkWriteDescriptorSet>(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
        w.dstSet = set;
        w.descriptorCount = 1;
    }
    writes[0].dstBinding = Shaders::BindingOfConstantBuffer(0);
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[0].pBufferInfo = &buffer;
    for (uint32_t t = 0; t < 4; ++t) {
        writes[1 + t].dstBinding = Shaders::BindingOfTexture(t);
        writes[1 + t].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        writes[1 + t].pImageInfo = &images[t];
    }
    writes[5].dstBinding = Shaders::BindingOfSampler(1);
    writes[5].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
    writes[5].pImageInfo = &sampler;
    vkUpdateDescriptorSets(LiveDevice(), 6, writes, 0, nullptr);
    return set;
}

bool ApplyLocked(const AoTargets& t, VkDeviceSize constants, VkImageView depth) {
    const VkDescriptorSet set = SetLocked(t.slot, constants, depth);
    if (!set) return false;
    VkFormat depthFormat = VK_FORMAT_UNDEFINED;
    VkFormat stencilFormat = VK_FORMAT_UNDEFINED;
    if (!DrawAoBeginApply(&depthFormat, &stencilFormat)) return false;
    const VkPipeline pipeline = ApplyPipeline(t.colorFormat, depthFormat, stencilFormat, t.samples);
    if (!pipeline) return false;
    vkCmdBindPipeline(t.cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    // The engine's Y flip (draw.cpp), as the AO pass sets it.
    const float fw = static_cast<float>(t.extent.width);
    const float fh = static_cast<float>(t.extent.height);
    const VkViewport viewport{ 0.0f, fh, fw, -fh, 0.0f, 1.0f };
    vkCmdSetViewport(t.cb, 0, 1, &viewport);
    const VkRect2D scissor{ { 0, 0 }, { t.extent.width, t.extent.height } };
    vkCmdSetScissor(t.cb, 0, 1, &scissor);
    vkCmdBindDescriptorSets(t.cb, VK_PIPELINE_BIND_POINT_GRAPHICS, g.layout, Shaders::kSetPixel, 1, &set, 0, nullptr);
    vkCmdDraw(t.cb, 3, 1, 0, 0);
    return true;
}

}  // namespace

void Shutdown() {
    std::lock_guard<std::mutex> guard(g_mutex);
    DestroyLocked();
}

}  // namespace TerrainLight
}  // namespace VideoVulkan

namespace TerrainLightBackend {
BANISHED_RENDER_IMPL_BEGIN

using namespace VideoVulkan::TerrainLight;

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
    VideoVulkan::DrawRequestReadableDepth(VideoVulkan::kDepthReaderTerrain, on);
    VideoVulkan::DepthCopy::Request(VideoVulkan::kDepthReaderTerrain, on);
}

void Draw(Video::Context& /*context*/, const Textures& textures, const Frame& frame) {
    if (!g_requested.load()) return;
    std::lock_guard<std::mutex> guard(g_mutex);
    if (!InitLocked()) return;
    if (!UploadLocked(textures)) return;
    const Video::ImageBuffer* copy = VideoVulkan::DepthCopy::Fresh();
    VideoVulkan::AoTargets t;
    if (copy != nullptr) {
        if (!VideoVulkan::DrawAoBeginKeepDepth(&t)) return;
    } else {
        if (!VideoVulkan::DrawAoBegin(&t)) return;
        copy = VideoVulkan::DepthCopy::InBracket(t);
    }
    bool ok = t.slot < VideoVulkan::kFramesInFlight && copy != nullptr;
    VkDeviceSize constants = ~0ull;
    if (ok) {
        BeginSlotLocked(t);
        Frame f = frame;
        f.screen[0] = 1.0f / static_cast<float>(t.extent.width);
        f.screen[1] = 1.0f / static_cast<float>(t.extent.height);
        constants = WriteConstantsLocked(t.slot, f);
        ok = constants != ~0ull;
    }
    ok = ok && ApplyLocked(t, constants, VideoVulkan::DepthCopy::ViewOf(copy));
    if (!ok) LogOnce(4, "a step of the terrain light failed (logged above): those frames have no terrain light");
    VideoVulkan::DrawAoEnd();
}

BANISHED_RENDER_IMPL_END
}  // namespace TerrainLightBackend
