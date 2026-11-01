// ============================================================================
// src/render/vulkan/better_water/scene_depth.cpp -- ext.better_water's scene depth copy on this backend
// ============================================================================
// NOT RETAIL. The contract is src/render/common/better_water/scene_depth_backend.h (docs/extensions/
// handins/BETTERWATER.md section 8).
//
// THE COPY. One fullscreen draw into a 1x R32_SFLOAT image, at the frame-copy seam: the main depth
// (draw.h THE READABLE DEPTH, requested while the copy is) is read through its depth-aspect view and
// written as a float, sample 0 of a multisampled depth, a plain copy of a single-sampled one. The
// programs are ext.ambient_occlusion's depth resolve (src/render/dx11/ao/ao_depth_resolve.hlsl, our
// own shaders, the committed DXBC under src/render/common/ao/shaders/) through the backend's one DXBC
// -> SPIR-V converter, as the AO pass runs them (ao/assao_pass.cpp ResolveDepth). A vkCmdCopyImage
// would not do: it cannot read a multisampled image, a depth format copies only into the same depth
// format, and a D24 depth is no float to the consumer's Load.
//
// THE IMAGE is an engine Video::ImageBuffer (Create: 2D, a colour render buffer (kind 0), R32F
// (format 6), point filter, clamp), so its companion is this backend's ImageBufferData: a render-buffer
// image with its attachment view and layout tracking, sampled by the contact pass's
// Context::SetImageBuffer(image, 4) like any texture (draw.cpp StageImages: a texture read only with
// Load gets the placeholder sampler). The copy is left in SHADER_READ_ONLY. It is remade when the
// frame's size changes, released when the request goes off, and released with the device
// (SceneDepth::Shutdown); the next Copy on a new device makes it again.
//
// THE FRAME. draw.cpp's AO bracket: DrawAoBegin ends the pass in progress and moves the main depth to
// SHADER_READ_ONLY, the copy runs in a pass of its own, DrawAoEnd makes the engine's next draw rebind
// everything; that draw (the water) begins a new main pass that loads the depth. The depth is not
// discarded while a reader asks for it (THE DEPTH DISCARD's `allowed`).
//
// THE DEPTH CONVENTION holds as on DX11: the engine's D3D projection goes through unchanged (clip z
// 0..1 is Vulkan's own), the viewport's depth range is 0..1, the depth clears to 1 (far), and the copy
// reads and writes at framebuffer pixel positions, which the main pass's flipped viewport does not
// change: SV_Position.xy of the water pixel addresses the same pixel in the copy.
// ============================================================================
#include "better_water/scene_depth_backend.h"
#include "better_water/scene_depth_pass.h"

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

#include <atomic>
#include <cstdio>
#include <mutex>
#include <string>
#include <vector>

namespace VideoVulkan {
namespace SceneDepth {

namespace {

constexpr uint32_t kSetsPerSlot = 8;   // a frame copies once (the screenshot copies run outside a frame)

enum Program : int {
    kVsFullscreen,
    kResolveMs,
    kResolve1x,
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
    { g_ao_PSResolveDepthMS, sizeof(g_ao_PSResolveDepthMS), Shaders::Stage::Pixel, "PSResolveDepthMS" },
    { g_ao_PSResolveDepth1x, sizeof(g_ao_PSResolveDepth1x), Shaders::Stage::Pixel, "PSResolveDepth1x" },
};

// The device objects of the copy pass, one set per device (Shutdown resets it).
struct State {
    VkDevice device = VK_NULL_HANDLE;   // the device everything below belongs to
    bool ready = false;
    VkShaderModule modules[kProgramCount] = {};
    VkDescriptorSetLayout vertexLayout = VK_NULL_HANDLE;   // empty: VSFullscreen reads nothing
    VkDescriptorSetLayout pixelLayout = VK_NULL_HANDLE;    // t0, a sampled image
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkPipeline pipelines[kProgramCount] = {};              // by pixel program
    VkDescriptorPool pools[kFramesInFlight] = {};
    uint64_t poolFrame[kFramesInFlight] = {};
    uint32_t logged = 0;
};

State g;
// Every entry point holds it for its whole body: the copy, the bind and the release never overlap.
std::mutex g_mutex;

std::atomic<bool> g_requested{false};
// The copy (an engine object; it outlives a device), its shape, and the frame its contents are of.
Video::ImageBuffer* g_image = nullptr;
const void* g_companion = nullptr;   // the companion the last (re)make was logged for
uint32_t g_width = 0;
uint32_t g_height = 0;
uint32_t g_samples = 0;
uint64_t g_copiedFrame = ~0ull;

void LogOnce(uint32_t bit, const char* message) {
    if (g.logged & bit) return;
    g.logged |= bit;
    std::fprintf(stderr, "[SceneDepth] Vulkan: %s\n", message);
}

void DestroyLocked() {
    VkDevice dev = LiveDevice();
    if (dev) {
        for (VkPipeline p : g.pipelines) {
            if (p) vkDestroyPipeline(dev, p, nullptr);
        }
        for (VkDescriptorPool p : g.pools) {
            if (p) vkDestroyDescriptorPool(dev, p, nullptr);
        }
        if (g.layout) vkDestroyPipelineLayout(dev, g.layout, nullptr);
        if (g.pixelLayout) vkDestroyDescriptorSetLayout(dev, g.pixelLayout, nullptr);
        if (g.vertexLayout) vkDestroyDescriptorSetLayout(dev, g.vertexLayout, nullptr);
    }
    for (VkShaderModule m : g.modules)
        DestroyShaderModule(m);
    g = State{};
    g_copiedFrame = ~0ull;
}

// The fullscreen triangle into one R32F target: no blend, no depth, culling off (ao_pass.cpp's resolve).
VkPipeline MakePipeline(Program ps) {
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
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    auto ds = Make<VkPipelineDepthStencilStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO);
    ds.depthCompareOp = VK_COMPARE_OP_ALWAYS;
    VkPipelineColorBlendAttachmentState blend{};
    blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT;
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
    const VkFormat format = VK_FORMAT_R32_SFLOAT;
    auto ri = Make<VkPipelineRenderingCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO);
    ri.colorAttachmentCount = 1;
    ri.pColorAttachmentFormats = &format;

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
        std::fprintf(stderr, "[SceneDepth] Vulkan: pipeline %s: %s: not supported\n", kBlobs[ps].name, ResultName(r));
        return VK_NULL_HANDLE;
    }
    return pipeline;
}

// One conversion and one set of objects per device. The caller holds g_mutex.
bool InitLocked() {
    VkDevice dev = LiveDevice();
    if (!dev) return false;
    if (g.device == dev) return g.ready;
    if (g.device) DestroyLocked();
    g.device = dev;
    if (LiveCaps().vulkan11) {
        // The pass is dynamic rendering (vkCmdBeginRendering), which the 1.1 path does not have.
        std::fprintf(stderr, "[SceneDepth] Vulkan: not supported on the Vulkan 1.1 path (no contact foam)\n");
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
            std::fprintf(stderr, "[SceneDepth] Vulkan: %s did not convert (%s): not supported\n", kBlobs[i].name,
                         why.c_str());
            return false;
        }
        // The layout below holds t0 alone (the pixel programs) and nothing for the vertex stage.
        const uint32_t textures = kBlobs[i].stage == Shaders::Stage::Pixel ? 1u : 0u;
        if (refl.textureMask != textures || refl.samplerMask != 0 || refl.constantBufferMask != 0 ||
            refl.uavMask != 0) {
            std::fprintf(stderr, "[SceneDepth] Vulkan: %s reads more than the pass binds: not supported\n",
                         kBlobs[i].name);
            return false;
        }
        g.modules[i] = CreateShaderModule(spirv, &why);
        if (!g.modules[i]) {
            std::fprintf(stderr, "[SceneDepth] Vulkan: %s: %s: not supported\n", kBlobs[i].name, why.c_str());
            return false;
        }
    }

    VkDescriptorSetLayoutBinding depth{};
    depth.binding = Shaders::BindingOfTexture(0);
    depth.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    depth.descriptorCount = 1;
    depth.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    auto li = Make<VkDescriptorSetLayoutCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO);
    li.bindingCount = 1;
    li.pBindings = &depth;
    VkResult r = vkCreateDescriptorSetLayout(dev, &li, nullptr, &g.pixelLayout);
    auto empty = Make<VkDescriptorSetLayoutCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO);
    if (r == VK_SUCCESS) r = vkCreateDescriptorSetLayout(dev, &empty, nullptr, &g.vertexLayout);
    const VkDescriptorSetLayout sets[2] = { g.vertexLayout, g.pixelLayout };
    static_assert(Shaders::kSetVertex == 0 && Shaders::kSetPixel == 1, "set 0 vertex, set 1 pixel");
    auto pl = Make<VkPipelineLayoutCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO);
    pl.setLayoutCount = 2;
    pl.pSetLayouts = sets;
    if (r == VK_SUCCESS) r = vkCreatePipelineLayout(dev, &pl, nullptr, &g.layout);
    const VkDescriptorPoolSize size = { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, kSetsPerSlot };
    auto poolInfo = Make<VkDescriptorPoolCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO);
    poolInfo.maxSets = kSetsPerSlot;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &size;
    for (unsigned s = 0; s < kFramesInFlight && r == VK_SUCCESS; ++s) {
        r = vkCreateDescriptorPool(dev, &poolInfo, nullptr, &g.pools[s]);
        g.poolFrame[s] = ~0ull;
    }
    if (r != VK_SUCCESS) {
        std::fprintf(stderr, "[SceneDepth] Vulkan: the pass's layouts and pools: %s: not supported\n", ResultName(r));
        return false;
    }
    g.pipelines[kResolveMs] = MakePipeline(kResolveMs);
    g.pipelines[kResolve1x] = MakePipeline(kResolve1x);
    if (!g.pipelines[kResolveMs] || !g.pipelines[kResolve1x]) return false;
    g.ready = true;
    return true;
}

// The copy's image at the frame's size, (re)made when the size changed. Logged once per (re)make, per
// companion (a device restart's RecreateDynamic gives the image a new one) and per sample count read.
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
    // companion; no frame is recorded then, so this is the copy's own guard, not a retail path.
    if (g_image->_data == nullptr) return false;
    if (make || w != g_width || h != g_height) {
        if (!g_image->Create(0, 0, 6, 0u, static_cast<int32_t>(w), static_cast<int32_t>(h), 0, 1, 1, 1, nullptr)) {
            LogOnce(1, "the copy's image could not be made: no contact foam");
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
        std::fprintf(stderr, "[SceneDepth] Vulkan: copy %ux%u R32F made, from the main depth x%u (%s)\n", w, h,
                     t.samples,
                     t.samples > 1 && !t.msrtss ? "sample 0" : (t.msrtss ? "the tiler's resolve" : "a plain copy"));
    }
    return true;
}

// The fullscreen draw: the main depth (SHADER_READ_ONLY, DrawAoBegin) -> the copy, left SHADER_READ_ONLY.
bool RecordLocked(const AoTargets& t) {
    if (t.slot >= kFramesInFlight) return false;
    VkDevice dev = LiveDevice();
    if (g.poolFrame[t.slot] != t.frame) {
        // The slot's previous frame completed (BeginCommands waited for its fence).
        vkResetDescriptorPool(dev, g.pools[t.slot], 0);
        g.poolFrame[t.slot] = t.frame;
    }
    auto ai = Make<VkDescriptorSetAllocateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO);
    ai.descriptorPool = g.pools[t.slot];
    ai.descriptorSetCount = 1;
    ai.pSetLayouts = &g.pixelLayout;
    VkDescriptorSet set = VK_NULL_HANDLE;
    if (vkAllocateDescriptorSets(dev, &ai, &set) != VK_SUCCESS) {
        LogOnce(2, "more copies in one frame than the pool holds: the rest are skipped");
        return false;
    }
    VkDescriptorImageInfo image{ VK_NULL_HANDLE, t.depthView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
    auto write = Make<VkWriteDescriptorSet>(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
    write.dstSet = set;
    write.dstBinding = Shaders::BindingOfTexture(0);
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    write.pImageInfo = &image;
    vkUpdateDescriptorSets(dev, 1, &write, 0, nullptr);

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
    const Program ps = t.samples > 1 && !t.msrtss ? kResolveMs : kResolve1x;
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, g.pipelines[ps]);
    // The engine's Y flip (draw.cpp), as the AO pass sets it; the copy is addressed by pixel either way.
    const float fw = static_cast<float>(g_width);
    const float fh = static_cast<float>(g_height);
    const VkViewport viewport{ 0.0f, fh, fw, -fh, 0.0f, 1.0f };
    vkCmdSetViewport(cb, 0, 1, &viewport);
    const VkRect2D scissor{ { 0, 0 }, { g_width, g_height } };
    vkCmdSetScissor(cb, 0, 1, &scissor);
    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, g.layout, Shaders::kSetPixel, 1, &set, 0, nullptr);
    vkCmdDraw(cb, 3, 1, 0, 0);
    vkCmdEndRendering(cb);

    TransitionImage(c->state, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, &barriers);
    FlushBarriers(cb, &barriers);
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
    g_copiedFrame = ~0ull;
    std::fprintf(stderr, "[SceneDepth] Vulkan: copy released\n");
}

}  // namespace

void Shutdown() {
    std::lock_guard<std::mutex> guard(g_mutex);
    // The copy goes with the device: its companion's views and image are retired here and destroyed by
    // ResourcesShutdown (an engine image nothing deletes would hold its attachment view past the
    // device). Round a device restart the engine's ReleaseDynamic took the companion already; the next
    // Copy makes the image again on the new device.
    ReleaseImageLocked();
    DestroyLocked();
}

}  // namespace SceneDepth
}  // namespace VideoVulkan

namespace SceneDepthBackend {

using namespace VideoVulkan::SceneDepth;

bool Supported() {
    if (!VideoVulkan::DeviceLive()) return false;
    std::lock_guard<std::mutex> guard(g_mutex);
    return InitLocked();
}

void Request(bool on) {
    g_requested.store(on);
    VideoVulkan::DrawRequestReadableDepth(VideoVulkan::kDepthReaderSceneDepth, on);
    if (on) return;
    std::lock_guard<std::mutex> guard(g_mutex);
    ReleaseImageLocked();
}

void Copy(void* /*context*/) {
    if (!g_requested.load()) return;
    std::lock_guard<std::mutex> guard(g_mutex);
    if (!InitLocked()) return;
    // Outside a frame (the screenshot's frame copies), or the frames before the main depth is remade
    // readable: no copy, and Image() answers null for this frame.
    VideoVulkan::AoTargets t;
    if (!VideoVulkan::DrawAoBegin(&t)) return;
    const bool copied = EnsureImageLocked(t) && RecordLocked(t);
    VideoVulkan::DrawAoEnd();
    if (copied) g_copiedFrame = t.frame;
}

const Video::ImageBuffer* Image() {
    if (!g_requested.load()) return nullptr;
    std::lock_guard<std::mutex> guard(g_mutex);
    if (g_image == nullptr || g_image->_data == nullptr) return nullptr;
    if (g_copiedFrame != VideoVulkan::DrawFrameIndex()) return nullptr;
    return g_image;
}

}  // namespace SceneDepthBackend
