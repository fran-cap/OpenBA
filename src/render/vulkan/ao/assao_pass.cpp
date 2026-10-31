// ============================================================================
// src/render/vulkan/ao/assao_pass.cpp -- ext.ambient_occlusion on the Vulkan backend (assao_pass.h)
// ============================================================================
// NOT RETAIL. Intel ASSAO (MIT, src/render/dx11/ao/assao/LICENSE; THIRD_PARTY_NOTICES.md), driven
// natively over the repository's own precompiled DXBC (src/render/common/ao/shaders/, made by
// tools/ao_shaders.py from ASSAO.hlsl and ao_depth_resolve.hlsl: our shaders, never game content).
//
// THE FRAME (Graphics::ExtAoAfterOpaque, the recording thread, between the Opaque and Blended lists):
//   draw.cpp DrawAoBegin    the main pass ends, the main depth (made readable: draw.h) -> SHADER_READ
//   the resolve             main depth (MSAA: sample 0; 1x: a copy) -> 1x R32F raw depth
//                           (src/render/dx11/ao/ao_depth_resolve.hlsl; ao_pass.cpp's DepthResolve::Run)
//   ASSAO Draw              UpdateConstants(pass 0), PrepareDepths, GenerateSSAO (4 deinterleaved
//                           passes, each a generate and its blurs), then the Apply: a pass on the main
//                           colour (draw.cpp DrawAoBeginApply) multiplying the AO term into it
//                           (blend colour ZERO / SRC_COLOR, alpha ZERO / SRC_ALPHA; no depth)
//   draw.cpp DrawAoEnd      the engine's cached command-buffer state is forgotten: its next draw
//                           rebinds pipeline, sets, viewport, scissor and buffers, and begins its pass
//                           with the main depth back in its attachment layout (LOAD)
// Each ASSAO step is ASSAODX11.cpp's, in its order: the same pixel shaders for the same quality levels,
// the same targets (four half-size R16F depth chains with four mips, a full-size RGBA8 normals UAV, two
// half-size RG8 ping-pong targets, a four-layer RG8 results array), the same samplers (point clamp,
// linear clamp, point mirror, point clamp), the same constants (UpdateConstants, field for field), the
// same one-time clears (m_requiresClear), the same viewports and scissors. ASSAO's inputs are the dx11
// stage's (ao_stage_assao.cpp): NormalSRV null (normals from depth), DrawOpaque false, the projection
// row-major (AoMath::AssaoProjection), the fade from this frame's fog (AoMath::FadeFromFog).
//
// VULKAN SHAPE. One descriptor set layout for the pixel stage (cb0, t0..t5, s0..s3, u4 at the bindings
// shader_convert.h gives those registers), an empty one for the vertex stage (VSMain / VSFullscreen
// read no resource). One dynamic-rendering pass per fullscreen draw, as D3D11 binds targets per draw.
// The pass's images keep a layout per subresource (a depth mip is rendered while the one above it is
// sampled), with sync2 barriers between the draws. Constants go to a per-frame-slot host ring, sets to a
// per-frame-slot pool, both reset when the slot's frame comes round (its fence was waited for).
//
// The tiler-MSAA path (VK_EXT_multisampled_render_to_single_sampled, the S25 with MSAA on): the main
// depth image is single-sampled, its contents the implicit resolve of sample 0 that the main passes
// store (draw.cpp BeginPass), so the depth is read as ASSAO's non-MSAA path reads it (the 1x resolve).
// The apply runs in a main pass at the MSAA count, as every main pass there.
// ============================================================================
#include "ao/assao_pass.h"

#include "device_objects.h"
#include "draw.h"
#include "pipelines.h"
#include "resources.h"
#include "shader_convert.h"
#include "vulkan11.h"

#include "ext/ambient_occlusion/ao_math.h"
#include "ext/derived_cache/derived_cache.h"
#include "graphics/Camera.h"
#include "graphics/renderer_ext_ao.h"
#include "video/Context.h"

#include "ao/shaders/VSMain.h"
#include "ao/shaders/VSFullscreen.h"
#include "ao/shaders/PSResolveDepthMS.h"
#include "ao/shaders/PSResolveDepth1x.h"
#include "ao/shaders/PSPrepareDepths.h"
#include "ao/shaders/PSPrepareDepthsAndNormals.h"
#include "ao/shaders/PSPrepareDepthsHalf.h"
#include "ao/shaders/PSPrepareDepthsAndNormalsHalf.h"
#include "ao/shaders/PSPrepareDepthMip1.h"
#include "ao/shaders/PSPrepareDepthMip2.h"
#include "ao/shaders/PSPrepareDepthMip3.h"
#include "ao/shaders/PSGenerateQ0.h"
#include "ao/shaders/PSGenerateQ1.h"
#include "ao/shaders/PSGenerateQ2.h"
#include "ao/shaders/PSSmartBlur.h"
#include "ao/shaders/PSSmartBlurWide.h"
#include "ao/shaders/PSNonSmartBlur.h"
#include "ao/shaders/PSApply.h"
#include "ao/shaders/PSNonSmartApply.h"
#include "ao/shaders/PSNonSmartHalfApply.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace VideoVulkan {
namespace Assao {

namespace {

constexpr int kMaxBlurPassCount = 6;          // ASSAODX11.cpp cMaxBlurPassCount
constexpr uint32_t kDepthMips = 4;            // SSAO_DEPTH_MIP_LEVELS (tools/ao_shaders.py's define)
constexpr uint32_t kSetsPerPool = 64;
constexpr VkDeviceSize kSlotBytes = 256;      // one constants block per slot (and the descriptor range)
constexpr VkDeviceSize kRingSlots = 64;       // a frame writes five (UpdateConstants: pass 0, then each pass)
constexpr uint32_t kTextureSlots = 6;         // t0..t5 (ASSAO's SSAO_TEXTURE_SLOT0..5)
constexpr uint32_t kSamplerSlots = 4;         // s0..s3
constexpr uint32_t kNormalsUav = 4;           // u4 (SSAO_NORMALMAP_OUT_UAV_SLOT)

// ASSAOConstants (ASSAODX11.cpp, SSAO_ENABLE_NORMAL_WORLD_TO_VIEW_CONVERSION 0): cb0, 240 bytes.
struct Constants {
    float viewportPixelSize[2];
    float halfViewportPixelSize[2];
    float depthUnpackConsts[2];
    float cameraTanHalfFov[2];
    float ndcToViewMul[2];
    float ndcToViewAdd[2];
    int32_t perPassFullResCoordOffset[2];
    float perPassFullResUvOffset[2];
    float viewport2xPixelSize[2];
    float viewport2xPixelSizeX025[2];
    float effectRadius;
    float effectShadowStrength;
    float effectShadowPow;
    float effectShadowClamp;
    float effectFadeOutMul;
    float effectFadeOutAdd;
    float effectHorizonAngleThreshold;
    float effectSamplingRadiusNearLimitRec;
    float depthPrecisionOffsetMod;
    float negRecEffectRadius;
    float loadCounterAvgDiv;
    float adaptiveSampleCountLimit;
    float invSharpness;
    int32_t passIndex;
    float quarterResPixelSize[2];
    float patternRotScaleMatrices[5][4];
    float normalsUnpackMul;
    float normalsUnpackAdd;
    float detailAoStrength;
    float dummy0;
};
static_assert(sizeof(Constants) == 240, "ASSAOConstants is 240 bytes (ASSAO.hlsl)");
static_assert(sizeof(Constants) <= kSlotBytes, "one constants block per ring slot");

enum Program : int {
    kVsMain,
    kVsFullscreen,
    kResolveMs,
    kResolve1x,
    kPrepareDepths,
    kPrepareDepthsAndNormals,
    kPrepareDepthsHalf,
    kPrepareDepthsAndNormalsHalf,
    kDepthMip1,
    kDepthMip2,
    kDepthMip3,
    kGenerateQ0,
    kGenerateQ1,
    kGenerateQ2,
    kSmartBlur,
    kSmartBlurWide,
    kNonSmartBlur,
    kApply,
    kNonSmartApply,
    kNonSmartHalfApply,
    kProgramCount
};

struct ProgramBlob {
    const uint8_t* bytes;
    size_t size;
    Shaders::Stage stage;
    const char* name;
};

#define AO_BLOB(name, stage) { g_ao_##name, sizeof(g_ao_##name), Shaders::Stage::stage, #name }
const ProgramBlob kBlobs[kProgramCount] = {
    AO_BLOB(VSMain, Vertex),
    AO_BLOB(VSFullscreen, Vertex),
    AO_BLOB(PSResolveDepthMS, Pixel),
    AO_BLOB(PSResolveDepth1x, Pixel),
    AO_BLOB(PSPrepareDepths, Pixel),
    AO_BLOB(PSPrepareDepthsAndNormals, Pixel),
    AO_BLOB(PSPrepareDepthsHalf, Pixel),
    AO_BLOB(PSPrepareDepthsAndNormalsHalf, Pixel),
    AO_BLOB(PSPrepareDepthMip1, Pixel),
    AO_BLOB(PSPrepareDepthMip2, Pixel),
    AO_BLOB(PSPrepareDepthMip3, Pixel),
    AO_BLOB(PSGenerateQ0, Pixel),
    AO_BLOB(PSGenerateQ1, Pixel),
    AO_BLOB(PSGenerateQ2, Pixel),
    AO_BLOB(PSSmartBlur, Pixel),
    AO_BLOB(PSSmartBlurWide, Pixel),
    AO_BLOB(PSNonSmartBlur, Pixel),
    AO_BLOB(PSApply, Pixel),
    AO_BLOB(PSNonSmartApply, Pixel),
    AO_BLOB(PSNonSmartHalfApply, Pixel),
};
#undef AO_BLOB

struct Module {
    VkShaderModule module = VK_NULL_HANDLE;
    Shaders::ShaderReflection reflection;
};

// One of the pass's images: a layout per subresource ([layer * mips + mip]), one attachment view per
// subresource (colour targets), one sampled view per mip (single-layer images). `gpu.view` is the
// sampled view over every mip and layer (2D, or 2D array for the results).
struct Image {
    GpuImage gpu;
    uint32_t mips = 1;
    uint32_t layers = 1;
    std::vector<VkImageLayout> layouts;
    std::vector<VkImageView> attach;
    std::vector<VkImageView> mipViews;
};

struct SlotState {
    uint64_t frame = ~0ull;
    std::vector<VkDescriptorPool> pools;
    size_t pool = 0;
    GpuBuffer ring;
    VkDeviceSize ringAt = 0;
};

struct State {
    VkDevice device = VK_NULL_HANDLE;   // the device everything below belongs to
    bool ready = false;
    bool pipelineFailed = false;   // P8 round 2: a pipeline the driver refused: the pass is not supported here
    Module modules[kProgramCount];
    VkDescriptorSetLayout vertexLayout = VK_NULL_HANDLE;   // empty
    VkDescriptorSetLayout pixelLayout = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkSampler samplers[kSamplerSlots] = {};
    GpuBuffer triangle;
    std::unordered_map<uint64_t, VkPipeline> pipelines;
    SlotState slots[kFramesInFlight];
    // ASSAODX11.cpp UpdateTextures: m_size, m_halfSize, m_quarterSize.
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t halfWidth = 0;
    uint32_t halfHeight = 0;
    uint32_t quarterWidth = 0;
    uint32_t quarterHeight = 0;
    Image depthResolved;   // the resolve's 1x R32F raw depth (ao_pass.cpp's DepthResolve target)
    Image depths[4];       // m_halfDepths: R16F, half size, four mips
    Image normals;         // m_normals: RGBA8, full size, the UAV
    Image pingA;           // m_pingPongHalfResultA: RG8, half size
    Image pingB;           // m_pingPongHalfResultB
    Image results;         // m_finalResults: RG8, half size, four layers
    bool requiresClear = false;
    uint32_t logged = 0;
    uint64_t frames = 0;
};

State g;
// Ready (main thread) and AfterOpaque (the recording thread) both hold it for their whole body, so a
// device change can never reset the state under a frame's pass.
std::mutex g_mutex;

void LogOnce(uint32_t bit, const char* message) {
    if (g.logged & bit) return;
    g.logged |= bit;
    std::fprintf(stderr, "[AmbientOcclusion] Vulkan: %s\n", message);
}

bool SameName(const std::string& a, const char* b) {
    if (a.size() != std::strlen(b)) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        const char x = (a[i] >= 'a' && a[i] <= 'z') ? static_cast<char>(a[i] - 32) : a[i];
        if (x != b[i]) return false;
    }
    return true;
}

// ---- images -----------------------------------------------------------------------------------------

VkImageView MakeView(VkImage image, VkFormat format, uint32_t mip, uint32_t mipCount, uint32_t layer,
                     uint32_t layerCount) {
    VkImageViewCreateInfo vi = Make<VkImageViewCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO);
    vi.image = image;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = format;
    vi.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, mip, mipCount, layer, layerCount };
    VkImageView view = VK_NULL_HANDLE;
    const VkResult r = vkCreateImageView(LiveDevice(), &vi, nullptr, &view);
    if (r != VK_SUCCESS) {
        std::fprintf(stderr, "[AmbientOcclusion] Vulkan: vkCreateImageView: %s\n", ResultName(r));
        return VK_NULL_HANDLE;
    }
    return view;
}

bool MakeImage(Image* im, uint32_t width, uint32_t height, VkFormat format, uint32_t mips, uint32_t layers,
               VkImageUsageFlags usage) {
    ImageDesc d;
    d.width = width;
    d.height = height;
    d.mips = mips;
    d.layers = layers;
    d.array = layers > 1;
    d.format = format;
    d.usage = usage;
    std::string why;
    if (!CreateImage(d, &im->gpu, &why)) {
        std::fprintf(stderr, "[AmbientOcclusion] Vulkan: target %ux%u format %d: %s\n", width, height,
                     static_cast<int>(format), why.c_str());
        return false;
    }
    im->mips = mips;
    im->layers = layers;
    im->layouts.assign(static_cast<size_t>(mips) * layers, VK_IMAGE_LAYOUT_UNDEFINED);
    if (usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) {
        for (uint32_t l = 0; l < layers; ++l) {
            for (uint32_t m = 0; m < mips; ++m) {
                im->attach.push_back(MakeView(im->gpu.image, format, m, 1, l, 1));
                if (!im->attach.back()) return false;
            }
        }
    }
    if (layers == 1) {
        for (uint32_t m = 0; m < mips; ++m) {
            im->mipViews.push_back(MakeView(im->gpu.image, format, m, 1, 0, 1));
            if (!im->mipViews.back()) return false;
        }
    }
    return true;
}

// `now`: the device is idle (Shutdown); otherwise the frames in flight may still read it (a resize).
void DropImage(Image* im, bool now) {
    std::vector<VkImageView> views = im->attach;
    views.insert(views.end(), im->mipViews.begin(), im->mipViews.end());
    if (now) {
        for (VkImageView v : views) {
            ForgetImageView(v);
            if (v && LiveDevice()) vkDestroyImageView(LiveDevice(), v, nullptr);
        }
        DestroyImageNow(&im->gpu);
    } else {
        Retire([views]() {
            for (VkImageView v : views) {
                ForgetImageView(v);
                if (v && LiveDevice()) vkDestroyImageView(LiveDevice(), v, nullptr);
            }
        });
        RetireImage(&im->gpu);
    }
    *im = Image{};
}

void DropTargets(bool now) {
    DropImage(&g.depthResolved, now);
    for (Image& d : g.depths)
        DropImage(&d, now);
    DropImage(&g.normals, now);
    DropImage(&g.pingA, now);
    DropImage(&g.pingB, now);
    DropImage(&g.results, now);
    g.width = 0;
    g.height = 0;
}

// ASSAODX11.cpp UpdateTextures: the sizes, then every target (re)made for them and the one-time clear.
bool Resize(uint32_t width, uint32_t height) {
    if (width == g.width && height == g.height && g.depthResolved.gpu.image) return true;
    DropTargets(false);
    const uint32_t hw = (width + 1) / 2;
    const uint32_t hh = (height + 1) / 2;
    const VkImageUsageFlags target = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    bool ok = MakeImage(&g.depthResolved, width, height, VK_FORMAT_R32_SFLOAT, 1, 1, target);
    for (Image& d : g.depths)
        ok = ok && MakeImage(&d, hw, hh, VK_FORMAT_R16_SFLOAT, kDepthMips, 1, target);
    ok = ok && MakeImage(&g.normals, width, height, VK_FORMAT_R8G8B8A8_UNORM, 1, 1, VK_IMAGE_USAGE_STORAGE_BIT);
    ok = ok && MakeImage(&g.pingA, hw, hh, VK_FORMAT_R8G8_UNORM, 1, 1, target);
    ok = ok && MakeImage(&g.pingB, hw, hh, VK_FORMAT_R8G8_UNORM, 1, 1, target);
    ok = ok && MakeImage(&g.results, hw, hh, VK_FORMAT_R8G8_UNORM, 1, 4, target);
    if (!ok) {
        DropTargets(false);
        return false;
    }
    g.width = width;
    g.height = height;
    g.halfWidth = hw;
    g.halfHeight = hh;
    g.quarterWidth = (hw + 1) / 2;
    g.quarterHeight = (hh + 1) / 2;
    g.requiresClear = true;
    std::fprintf(stderr, "[AmbientOcclusion] Vulkan: ASSAO targets allocated for %ux%u (half %ux%u)\n", width, height,
                 hw, hh);
    return true;
}

// ---- barriers (per subresource) -------------------------------------------------------------------

struct Use {
    VkPipelineStageFlags2 stages;
    VkAccessFlags2 access;
    bool writes;
};

Use UseOf(VkImageLayout layout) {
    switch (layout) {
    case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
        return { VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                 VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, true };
    case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
        return { VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT, false };
    case VK_IMAGE_LAYOUT_GENERAL:
        return { VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                 VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, true };
    case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
        return { VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, true };
    default:
        return { VK_PIPELINE_STAGE_2_NONE, 0, false };
    }
}

// Moves [mip, mip + mipCount) x [layer, layer + layerCount) of `im` to `to`, one barrier per subresource
// whose layout changes (or that was written: two passes writing one target in a row).
void Move(Image& im, uint32_t mip, uint32_t mipCount, uint32_t layer, uint32_t layerCount, VkImageLayout to,
          std::vector<VkImageMemoryBarrier2>* out) {
    for (uint32_t l = layer; l < layer + layerCount; ++l) {
        for (uint32_t m = mip; m < mip + mipCount; ++m) {
            VkImageLayout& now = im.layouts[static_cast<size_t>(l) * im.mips + m];
            const Use from = UseOf(now);
            if (now == to && !from.writes) continue;
            const Use dst = UseOf(to);
            VkImageMemoryBarrier2 b = Make<VkImageMemoryBarrier2>(VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2);
            b.srcStageMask = from.stages;
            b.srcAccessMask = from.writes ? from.access : 0;
            b.dstStageMask = dst.stages;
            b.dstAccessMask = dst.access;
            b.oldLayout = now;
            b.newLayout = to;
            b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.image = im.gpu.image;
            b.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, m, 1, l, 1 };
            out->push_back(b);
            now = to;
        }
    }
}

void MoveAll(Image& im, VkImageLayout to, std::vector<VkImageMemoryBarrier2>* out) {
    Move(im, 0, im.mips, 0, im.layers, to, out);
}

// ASSAODX11.cpp Draw's m_requiresClear: depths 0, ping A 1, ping B 0, results 1, normals 0.
void ClearTargets(VkCommandBuffer cb) {
    std::vector<VkImageMemoryBarrier2> barriers;
    Image* images[] = { &g.depths[0], &g.depths[1], &g.depths[2], &g.depths[3], &g.pingA, &g.pingB, &g.results,
                        &g.normals };
    const float values[] = { 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f };
    for (Image* im : images)
        MoveAll(*im, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &barriers);
    FlushBarriers(cb, &barriers);
    for (size_t i = 0; i < sizeof(images) / sizeof(images[0]); ++i) {
        VkClearColorValue c{};
        for (float& v : c.float32)
            v = values[i];
        const VkImageSubresourceRange range = { VK_IMAGE_ASPECT_COLOR_BIT, 0, images[i]->mips, 0, images[i]->layers };
        vkCmdClearColorImage(cb, images[i]->gpu.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &c, 1, &range);
    }
}

// ---- pipelines ----------------------------------------------------------------------------------------

struct PipelineShape {
    Program vs = kVsMain;
    Program ps = kApply;
    uint32_t colorCount = 0;
    VkFormat formats[4] = {};
    uint32_t samples = 1;
    bool multiply = false;
};

uint64_t ShapeKey(const PipelineShape& s) {
    uint64_t h = 1469598103934665603ull;
    const auto mix = [&h](uint64_t v) {
        h ^= v;
        h *= 1099511628211ull;
    };
    mix(static_cast<uint64_t>(s.vs));
    mix(static_cast<uint64_t>(s.ps));
    mix(s.colorCount);
    for (VkFormat f : s.formats)
        mix(static_cast<uint64_t>(f));
    mix(s.samples);
    mix(s.multiply ? 1 : 0);
    return h;
}

// ASSAODX11.cpp's fixed states: CULL_NONE with the scissor on, depth off, blend opaque or multiply
// (colour ZERO / SRC_COLOR, alpha ZERO / SRC_ALPHA, ADD, every channel written).
VkPipeline PipelineFor(const PipelineShape& s) {
    const uint64_t key = ShapeKey(s);
    auto found = g.pipelines.find(key);
    if (found != g.pipelines.end()) return found->second;

    const Module& vs = g.modules[s.vs];
    std::vector<VkVertexInputBindingDescription> bindings;
    std::vector<VkVertexInputAttributeDescription> attributes;
    // VSMain reads ASSAO's CommonSimpleVertex (SV_Position float4 at 0, TEXCOORD float2 at 16, 24-byte
    // stride); VSFullscreen reads nothing (SV_VertexID).
    for (const Shaders::IoElement& in : vs.reflection.inputs) {
        if (in.builtIn != Shaders::IoElement::kNoBuiltIn) continue;
        VkVertexInputAttributeDescription a{};
        a.location = in.location;
        a.binding = 0;
        if (SameName(in.semantic, "SV_POSITION")) {
            a.format = VK_FORMAT_R32G32B32A32_SFLOAT;
            a.offset = 0;
        } else {
            a.format = VK_FORMAT_R32G32_SFLOAT;
            a.offset = 16;
        }
        attributes.push_back(a);
    }
    if (!attributes.empty()) bindings.push_back({ 0, 24, VK_VERTEX_INPUT_RATE_VERTEX });

    VkPipelineShaderStageCreateInfo stages[2];
    stages[0] = Make<VkPipelineShaderStageCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO);
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vs.module;
    stages[0].pName = "main";
    stages[1] = Make<VkPipelineShaderStageCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO);
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = g.modules[s.ps].module;
    stages[1].pName = "main";
    auto vi = Make<VkPipelineVertexInputStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO);
    vi.vertexBindingDescriptionCount = static_cast<uint32_t>(bindings.size());
    vi.pVertexBindingDescriptions = bindings.data();
    vi.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size());
    vi.pVertexAttributeDescriptions = attributes.data();
    auto ia = Make<VkPipelineInputAssemblyStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO);
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;   // D3D's 3-vertex strip: the one triangle
    auto vp = Make<VkPipelineViewportStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO);
    vp.viewportCount = 1;
    vp.scissorCount = 1;
    auto rs = Make<VkPipelineRasterizationStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO);
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rs.lineWidth = 1.0f;
    auto ms = Make<VkPipelineMultisampleStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO);
    ms.rasterizationSamples = static_cast<VkSampleCountFlagBits>(s.samples);
    auto ds = Make<VkPipelineDepthStencilStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO);
    ds.depthCompareOp = VK_COMPARE_OP_ALWAYS;
    VkPipelineColorBlendAttachmentState blends[4];
    for (VkPipelineColorBlendAttachmentState& b : blends) {
        b = VkPipelineColorBlendAttachmentState{};
        b.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
                           VK_COLOR_COMPONENT_A_BIT;
        if (s.multiply) {
            b.blendEnable = VK_TRUE;
            b.srcColorBlendFactor = VK_BLEND_FACTOR_ZERO;
            b.dstColorBlendFactor = VK_BLEND_FACTOR_SRC_COLOR;
            b.colorBlendOp = VK_BLEND_OP_ADD;
            b.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
            b.dstAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            b.alphaBlendOp = VK_BLEND_OP_ADD;
        }
    }
    auto cbs = Make<VkPipelineColorBlendStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO);
    cbs.attachmentCount = s.colorCount;
    cbs.pAttachments = blends;
    // The binding stride is dynamic as in the game's pipelines (pipelines.cpp): the game's draws in the same
    // command buffer set strides with vkCmdBindVertexBuffers2, which a pipeline without the state may not
    // follow. These pipelines have no vertex bindings, so no stride is ever set for them.
    const VkDynamicState dynamics[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR,
                                        VK_DYNAMIC_STATE_VERTEX_INPUT_BINDING_STRIDE };
    auto dyn = Make<VkPipelineDynamicStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO);
    dyn.dynamicStateCount = 3;
    dyn.pDynamicStates = dynamics;
    auto ri = Make<VkPipelineRenderingCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO);
    ri.colorAttachmentCount = s.colorCount;
    ri.pColorAttachmentFormats = s.formats;

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
        std::fprintf(stderr, "[AmbientOcclusion] Vulkan: pipeline %s: %s: the AO pass is not supported on this "
                             "device (the row turns inactive)\n", kBlobs[s.ps].name, ResultName(r));
        pipeline = VK_NULL_HANDLE;
        g.pipelineFailed = true;
    }
    g.pipelines.emplace(key, pipeline);
    return pipeline;
}

// ---- descriptors and constants ------------------------------------------------------------------------

VkDescriptorSet AllocateSet(SlotState& slot) {
    for (;;) {
        if (slot.pool >= slot.pools.size()) {
            const VkDescriptorPoolSize sizes[] = {
                { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, kSetsPerPool },
                { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, kSetsPerPool * kTextureSlots },
                { VK_DESCRIPTOR_TYPE_SAMPLER, kSetsPerPool * kSamplerSlots },
                { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, kSetsPerPool },
            };
            auto pi = Make<VkDescriptorPoolCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO);
            pi.maxSets = kSetsPerPool;
            pi.poolSizeCount = 4;
            pi.pPoolSizes = sizes;
            VkDescriptorPool pool = VK_NULL_HANDLE;
            const VkResult r = vkCreateDescriptorPool(LiveDevice(), &pi, nullptr, &pool);
            if (r != VK_SUCCESS) {
                std::fprintf(stderr, "[AmbientOcclusion] Vulkan: vkCreateDescriptorPool: %s\n", ResultName(r));
                return VK_NULL_HANDLE;
            }
            slot.pools.push_back(pool);
        }
        auto ai = Make<VkDescriptorSetAllocateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO);
        ai.descriptorPool = slot.pools[slot.pool];
        ai.descriptorSetCount = 1;
        ai.pSetLayouts = &g.pixelLayout;
        VkDescriptorSet set = VK_NULL_HANDLE;
        const VkResult r = vkAllocateDescriptorSets(LiveDevice(), &ai, &set);
        if (r == VK_SUCCESS) return set;
        if (r != VK_ERROR_OUT_OF_POOL_MEMORY && r != VK_ERROR_FRAGMENTED_POOL) {
            std::fprintf(stderr, "[AmbientOcclusion] Vulkan: vkAllocateDescriptorSets: %s\n", ResultName(r));
            return VK_NULL_HANDLE;
        }
        ++slot.pool;
    }
}

// ASSAODX11.cpp UpdateConstants, field for field. `p` is the row-major projection ASSAO is handed
// (MatricesRowMajorOrder true: m[i][j] = p[i * 4 + j]).
Constants MakeConstants(const float p[16], const Graphics::ExtAoFrame& f, int pass) {
    Constants c{};
    const float w = static_cast<float>(g.width);
    const float h = static_cast<float>(g.height);
    c.viewportPixelSize[0] = 1.0f / w;
    c.viewportPixelSize[1] = 1.0f / h;
    c.halfViewportPixelSize[0] = 1.0f / static_cast<float>(g.halfWidth);
    c.halfViewportPixelSize[1] = 1.0f / static_cast<float>(g.halfHeight);
    c.viewport2xPixelSize[0] = c.viewportPixelSize[0] * 2.0f;
    c.viewport2xPixelSize[1] = c.viewportPixelSize[1] * 2.0f;
    c.viewport2xPixelSizeX025[0] = c.viewport2xPixelSize[0] * 0.25f;
    c.viewport2xPixelSizeX025[1] = c.viewport2xPixelSize[1] * 0.25f;
    const float depthLinearizeMul = -p[3 * 4 + 2];
    float depthLinearizeAdd = p[2 * 4 + 2];
    if (depthLinearizeMul * depthLinearizeAdd < 0) depthLinearizeAdd = -depthLinearizeAdd;
    c.depthUnpackConsts[0] = depthLinearizeMul;
    c.depthUnpackConsts[1] = depthLinearizeAdd;
    const float tanHalfFovY = 1.0f / p[1 * 4 + 1];
    const float tanHalfFovX = 1.0f / p[0 * 4 + 0];
    c.cameraTanHalfFov[0] = tanHalfFovX;
    c.cameraTanHalfFov[1] = tanHalfFovY;
    c.ndcToViewMul[0] = c.cameraTanHalfFov[0] * 2.0f;
    c.ndcToViewMul[1] = c.cameraTanHalfFov[1] * -2.0f;
    c.ndcToViewAdd[0] = c.cameraTanHalfFov[0] * -1.0f;
    c.ndcToViewAdd[1] = c.cameraTanHalfFov[1] * 1.0f;
    c.effectRadius = std::clamp(f.radius, 0.0f, 100000.0f);
    c.effectShadowStrength = std::clamp(f.shadowMultiplier * 4.3f, 0.0f, 10.0f);
    c.effectShadowPow = std::clamp(f.shadowPower, 0.0f, 10.0f);
    c.effectShadowClamp = std::clamp(0.98f, 0.0f, 1.0f);   // ASSAO_Settings::ShadowClamp's default
    c.effectFadeOutMul = -1.0f / (f.fadeOutTo - f.fadeOutFrom);
    c.effectFadeOutAdd = f.fadeOutFrom / (f.fadeOutTo - f.fadeOutFrom) + 1.0f;
    c.effectHorizonAngleThreshold = std::clamp(f.horizonAngleThreshold, 0.0f, 1.0f);
    float effectSamplingRadiusNearLimit = f.radius * 1.2f;
    c.depthPrecisionOffsetMod = 0.9992f;
    c.loadCounterAvgDiv =
        9.0f / static_cast<float>(static_cast<double>(g.quarterWidth * g.quarterHeight) * 255.0);
    if (f.qualityLevel <= 0) {
        effectSamplingRadiusNearLimit *= 1.50f;
        if (f.qualityLevel < 0) c.effectRadius *= 0.8f;
    }
    effectSamplingRadiusNearLimit /= tanHalfFovY;
    c.effectSamplingRadiusNearLimitRec = 1.0f / effectSamplingRadiusNearLimit;
    c.adaptiveSampleCountLimit = 0.45f;   // ASSAO_Settings::AdaptiveQualityLimit's default
    c.negRecEffectRadius = -1.0f / c.effectRadius;
    c.perPassFullResCoordOffset[0] = pass % 2;
    c.perPassFullResCoordOffset[1] = pass / 2;
    c.perPassFullResUvOffset[0] = (static_cast<float>(pass % 2) - 0.0f) / w;
    c.perPassFullResUvOffset[1] = (static_cast<float>(pass / 2) - 0.0f) / h;
    c.invSharpness = std::clamp(1.0f - f.sharpness, 0.0f, 1.0f);
    c.passIndex = pass;
    c.quarterResPixelSize[0] = 1.0f / static_cast<float>(g.quarterWidth);
    c.quarterResPixelSize[1] = 1.0f / static_cast<float>(g.quarterHeight);
    // The temporal supersampling offsets stay at ASSAO's no-op defaults (0, 1): there is no TAA.
    const float additionalAngleOffset = 0.0f;
    const float additionalRadiusScale = 1.0f;
    const int subPassCount = 5;
    for (int subPass = 0; subPass < subPassCount; ++subPass) {
        const int a = pass;
        const int spmap[5] = { 0, 1, 4, 3, 2 };
        const int b = spmap[subPass];
        float angle0 = (static_cast<float>(a) + static_cast<float>(b) / static_cast<float>(subPassCount)) *
                       3.1415926535897932384626433832795f * 0.5f;
        angle0 += additionalAngleOffset;
        const float ca = std::cos(angle0);
        const float sa = std::sin(angle0);
        float scale = 1.0f + (static_cast<float>(a) - 1.5f +
                              (static_cast<float>(b) - (static_cast<float>(subPassCount) - 1.0f) * 0.5f) /
                                  static_cast<float>(subPassCount)) *
                                 0.07f;
        scale *= additionalRadiusScale;
        c.patternRotScaleMatrices[subPass][0] = scale * ca;
        c.patternRotScaleMatrices[subPass][1] = scale * -sa;
        c.patternRotScaleMatrices[subPass][2] = -scale * sa;
        c.patternRotScaleMatrices[subPass][3] = -scale * ca;
    }
    c.normalsUnpackMul = 2.0f;
    c.normalsUnpackAdd = -1.0f;
    c.detailAoStrength = f.detailShadowStrength;
    c.dummy0 = 0.0f;
    return c;
}

// The next ring slot of this frame slot, filled with `c`: the offset the following draws bind (D3D11's
// Map DISCARD of the one constant buffer). ~0 when the ring is full.
VkDeviceSize WriteConstants(SlotState& slot, const Constants& c) {
    const VkDeviceSize align =
        std::max<VkDeviceSize>(LiveProperties().limits.minUniformBufferOffsetAlignment, kSlotBytes);
    const VkDeviceSize at = (slot.ringAt + align - 1) / align * align;
    if (at + kSlotBytes > slot.ring.size) return ~0ull;
    std::memcpy(slot.ring.mapped + at, &c, sizeof(c));
    slot.ringAt = at + kSlotBytes;
    return at;
}

// ---- one fullscreen draw ----------------------------------------------------------------------------

struct DrawDesc {
    Program vs = kVsMain;
    Program ps = kApply;
    VkImageView colors[4] = {};
    VkFormat formats[4] = {};
    uint32_t colorCount = 0;
    VkExtent2D area{};             // the render area (the targets' extent)
    float viewportWidth = 0.0f;    // ASSAO's viewport (a mip's is rounded up, as its Size)
    float viewportHeight = 0.0f;
    VkRect2D scissor{};
    VkImageView textures[kTextureSlots] = {};
    VkImageView uav = VK_NULL_HANDLE;
    VkDeviceSize constants = ~0ull;
    uint32_t samples = 1;
    bool multiply = false;
    bool mainPass = false;         // the apply: draw.cpp's pass on the main colour is already begun
};

bool Draw(VkCommandBuffer cb, SlotState& slot, const DrawDesc& d) {
    PipelineShape shape;
    shape.vs = d.vs;
    shape.ps = d.ps;
    shape.colorCount = d.colorCount;
    for (uint32_t i = 0; i < d.colorCount; ++i)
        shape.formats[i] = d.formats[i];
    shape.samples = d.samples;
    shape.multiply = d.multiply;
    const VkPipeline pipeline = PipelineFor(shape);
    if (!pipeline) return false;

    // Only what the pixel program reads is written (an unused binding is never read).
    const Shaders::ShaderReflection& refl = g.modules[d.ps].reflection;
    const VkDescriptorSet set = AllocateSet(slot);
    if (!set) return false;
    VkWriteDescriptorSet writes[1 + kTextureSlots + kSamplerSlots + 1];
    VkDescriptorBufferInfo buffer{};
    VkDescriptorImageInfo images[kTextureSlots];
    VkDescriptorImageInfo samplers[kSamplerSlots];
    VkDescriptorImageInfo storage{};
    uint32_t n = 0;
    if (refl.constantBufferMask & 1u) {
        if (d.constants == ~0ull) return false;
        buffer = { slot.ring.buffer, d.constants, kSlotBytes };
        writes[n] = Make<VkWriteDescriptorSet>(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
        writes[n].dstSet = set;
        writes[n].dstBinding = Shaders::BindingOfConstantBuffer(0);
        writes[n].descriptorCount = 1;
        writes[n].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[n].pBufferInfo = &buffer;
        ++n;
    }
    for (uint32_t t = 0; t < kTextureSlots; ++t) {
        if (!((refl.textureMask >> t) & 1u)) continue;
        if (!d.textures[t]) {
            std::fprintf(stderr, "[AmbientOcclusion] Vulkan: %s reads t%u, which the pass did not bind\n",
                         kBlobs[d.ps].name, t);
            return false;
        }
        images[t] = { VK_NULL_HANDLE, d.textures[t], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        writes[n] = Make<VkWriteDescriptorSet>(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
        writes[n].dstSet = set;
        writes[n].dstBinding = Shaders::BindingOfTexture(t);
        writes[n].descriptorCount = 1;
        writes[n].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        writes[n].pImageInfo = &images[t];
        ++n;
    }
    for (uint32_t s = 0; s < kSamplerSlots; ++s) {
        if (!((refl.samplerMask >> s) & 1u)) continue;
        samplers[s] = { g.samplers[s], VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED };
        writes[n] = Make<VkWriteDescriptorSet>(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
        writes[n].dstSet = set;
        writes[n].dstBinding = Shaders::BindingOfSampler(s);
        writes[n].descriptorCount = 1;
        writes[n].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
        writes[n].pImageInfo = &samplers[s];
        ++n;
    }
    if ((refl.uavMask >> kNormalsUav) & 1u) {
        if (!d.uav) return false;
        storage = { VK_NULL_HANDLE, d.uav, VK_IMAGE_LAYOUT_GENERAL };
        writes[n] = Make<VkWriteDescriptorSet>(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
        writes[n].dstSet = set;
        writes[n].dstBinding = Shaders::BindingOfUav(kNormalsUav);
        writes[n].descriptorCount = 1;
        writes[n].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes[n].pImageInfo = &storage;
        ++n;
    }
    vkUpdateDescriptorSets(LiveDevice(), n, writes, 0, nullptr);

    if (!d.mainPass) {
        VkRenderingAttachmentInfo colors[4];
        for (uint32_t i = 0; i < d.colorCount; ++i) {
            colors[i] = Make<VkRenderingAttachmentInfo>(VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO);
            colors[i].imageView = d.colors[i];
            colors[i].imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            // Every draw covers its whole target (the scissor is the target): nothing to load.
            colors[i].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            colors[i].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        }
        VkRenderingInfo ri = Make<VkRenderingInfo>(VK_STRUCTURE_TYPE_RENDERING_INFO);
        ri.renderArea = { { 0, 0 }, d.area };
        ri.layerCount = 1;
        ri.colorAttachmentCount = d.colorCount;
        ri.pColorAttachments = colors;
        vkCmdBeginRendering(cb, &ri);
    }
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    // The engine's Y flip (draw.cpp): a negative-height viewport keeps D3D's orientation.
    const VkViewport viewport{ 0.0f, d.viewportHeight, d.viewportWidth, -d.viewportHeight, 0.0f, 1.0f };
    vkCmdSetViewport(cb, 0, 1, &viewport);
    vkCmdSetScissor(cb, 0, 1, &d.scissor);
    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, g.layout, Shaders::kSetPixel, 1, &set, 0, nullptr);
    if (d.vs == kVsMain) {
        const VkDeviceSize offset = 0;
        const VkDeviceSize stride = 24;
        vkCmdBindVertexBuffers2(cb, 0, 1, &g.triangle.buffer, &offset, nullptr, &stride);
    }
    vkCmdDraw(cb, 3, 1, 0, 0);
    if (!d.mainPass) vkCmdEndRendering(cb);
    return true;
}

// ---- the steps --------------------------------------------------------------------------------------

VkRect2D Rect(uint32_t width, uint32_t height) {
    return VkRect2D{ { 0, 0 }, { width, height } };
}

// ao_pass.cpp's DepthResolve::Run: sample 0 of the multisampled depth, or a 1x copy, into the R32F target.
bool ResolveDepth(VkCommandBuffer cb, SlotState& slot, const AoTargets& t) {
    std::vector<VkImageMemoryBarrier2> barriers;
    MoveAll(g.depthResolved, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, &barriers);
    FlushBarriers(cb, &barriers);
    DrawDesc d;
    d.vs = kVsFullscreen;
    // The tiler's MSAA: the depth image is single-sampled (sample 0, resolved by the main passes).
    d.ps = t.samples > 1 && !t.msrtss ? kResolveMs : kResolve1x;
    d.colors[0] = g.depthResolved.attach[0];
    d.formats[0] = VK_FORMAT_R32_SFLOAT;
    d.colorCount = 1;
    d.area = { g.width, g.height };
    d.viewportWidth = static_cast<float>(g.width);
    d.viewportHeight = static_cast<float>(g.height);
    d.scissor = Rect(g.width, g.height);
    d.textures[0] = t.depthView;
    return Draw(cb, slot, d);
}

// ASSAODX11.cpp PrepareDepths with generateNormals (NormalSRV null).
bool PrepareDepths(VkCommandBuffer cb, SlotState& slot, const Graphics::ExtAoFrame& f, VkDeviceSize constants) {
    std::vector<VkImageMemoryBarrier2> barriers;
    MoveAll(g.depthResolved, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, &barriers);
    MoveAll(g.normals, VK_IMAGE_LAYOUT_GENERAL, &barriers);
    DrawDesc d;
    d.textures[0] = g.depthResolved.gpu.view;
    d.uav = g.normals.gpu.view;
    d.area = { g.halfWidth, g.halfHeight };
    d.viewportWidth = static_cast<float>(g.halfWidth);
    d.viewportHeight = static_cast<float>(g.halfHeight);
    d.scissor = Rect(g.halfWidth, g.halfHeight);
    d.constants = constants;
    if (f.qualityLevel < 0) {
        const int two[2] = { 0, 3 };   // twoDepths: m_halfDepths[0], [3]
        for (int i = 0; i < 2; ++i) {
            Move(g.depths[two[i]], 0, 1, 0, 1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, &barriers);
            d.colors[i] = g.depths[two[i]].attach[0];
            d.formats[i] = VK_FORMAT_R16_SFLOAT;
        }
        d.colorCount = 2;
        d.ps = kPrepareDepthsAndNormalsHalf;
    } else {
        for (int i = 0; i < 4; ++i) {
            Move(g.depths[i], 0, 1, 0, 1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, &barriers);
            d.colors[i] = g.depths[i].attach[0];
            d.formats[i] = VK_FORMAT_R16_SFLOAT;
        }
        d.colorCount = 4;
        d.ps = kPrepareDepthsAndNormals;
    }
    FlushBarriers(cb, &barriers);
    if (!Draw(cb, slot, d)) return false;

    // Only for the higher quality levels (not beneficial on 1, detrimental on 0): ASSAO's own rule.
    if (f.qualityLevel > 1) {
        uint32_t vw = g.halfWidth;
        uint32_t vh = g.halfHeight;
        for (uint32_t mip = 1; mip < kDepthMips; ++mip) {
            vw = std::max((vw + 1) / 2, 1u);   // ReCreateMIPViewIfNeeded's Size
            vh = std::max((vh + 1) / 2, 1u);
            DrawDesc m;
            m.ps = static_cast<Program>(kDepthMip1 + static_cast<int>(mip) - 1);
            for (int i = 0; i < 4; ++i) {
                Move(g.depths[i], mip - 1, 1, 0, 1, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, &barriers);
                Move(g.depths[i], mip, 1, 0, 1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, &barriers);
                m.colors[i] = g.depths[i].attach[mip];
                m.formats[i] = VK_FORMAT_R16_SFLOAT;
                m.textures[i] = g.depths[i].mipViews[mip - 1];
            }
            m.colorCount = 4;
            const uint32_t aw = std::max(g.halfWidth >> mip, 1u);
            const uint32_t ah = std::max(g.halfHeight >> mip, 1u);
            m.area = { aw, ah };
            m.viewportWidth = static_cast<float>(vw);
            m.viewportHeight = static_cast<float>(vh);
            m.scissor = Rect(aw, ah);
            m.constants = constants;
            FlushBarriers(cb, &barriers);
            if (!Draw(cb, slot, m)) return false;
        }
    }
    return true;
}

// ASSAODX11.cpp GenerateSSAO (not the adaptive base pass). Returns the constants the last pass wrote
// (the apply's), ~0 on failure.
VkDeviceSize GenerateSsao(VkCommandBuffer cb, SlotState& slot, const float projection[16],
                          const Graphics::ExtAoFrame& f) {
    VkDeviceSize constants = ~0ull;
    std::vector<VkImageMemoryBarrier2> barriers;
    for (int pass = 0; pass < 4; ++pass) {
        if (f.qualityLevel < 0 && (pass == 1 || pass == 2)) continue;
        int blurPasses = std::min(f.blurPassCount, kMaxBlurPassCount);
        if (f.qualityLevel <= 0) blurPasses = std::min(1, f.blurPassCount);   // just one for the minimum quality
        blurPasses = std::max(blurPasses, 0);
        constants = WriteConstants(slot, MakeConstants(projection, f, pass));
        if (constants == ~0ull) return ~0ull;
        Image* ping = &g.pingA;
        Image* pong = &g.pingB;

        // Generate.
        MoveAll(g.depths[pass], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, &barriers);
        MoveAll(g.normals, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, &barriers);
        DrawDesc d;
        d.area = { g.halfWidth, g.halfHeight };
        d.viewportWidth = static_cast<float>(g.halfWidth);
        d.viewportHeight = static_cast<float>(g.halfHeight);
        d.scissor = Rect(g.halfWidth, g.halfHeight);   // m_halfResOutScissorRect: the whole half target
        d.constants = constants;
        d.formats[0] = VK_FORMAT_R8G8_UNORM;
        d.colorCount = 1;
        if (blurPasses == 0) {
            Move(g.results, 0, 1, static_cast<uint32_t>(pass), 1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, &barriers);
            d.colors[0] = g.results.attach[static_cast<size_t>(pass)];
        } else {
            MoveAll(*ping, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, &barriers);
            d.colors[0] = ping->attach[0];
        }
        d.textures[0] = g.depths[pass].gpu.view;
        d.textures[1] = g.normals.gpu.view;
        d.ps = static_cast<Program>(kGenerateQ0 + std::min(std::max(0, f.qualityLevel), 2));
        FlushBarriers(cb, &barriers);
        if (!Draw(cb, slot, d)) return ~0ull;

        // Blur.
        int wideBlursRemaining = std::max(0, blurPasses - 2);
        for (int i = 0; i < blurPasses; ++i) {
            DrawDesc b = d;
            b.textures[0] = VK_NULL_HANDLE;
            b.textures[1] = VK_NULL_HANDLE;
            MoveAll(*ping, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, &barriers);
            if (i == blurPasses - 1) {
                Move(g.results, 0, 1, static_cast<uint32_t>(pass), 1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                     &barriers);
                b.colors[0] = g.results.attach[static_cast<size_t>(pass)];
            } else {
                MoveAll(*pong, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, &barriers);
                b.colors[0] = pong->attach[0];
            }
            b.textures[2] = ping->gpu.view;
            if (f.qualityLevel > 0) {
                if (wideBlursRemaining > 0) {
                    b.ps = kSmartBlurWide;
                    --wideBlursRemaining;
                } else {
                    b.ps = kSmartBlur;
                }
            } else {
                b.ps = kNonSmartBlur;   // just for quality level 0 (and -1)
            }
            FlushBarriers(cb, &barriers);
            if (!Draw(cb, slot, b)) return ~0ull;
            std::swap(ping, pong);
        }
    }
    return constants;
}

// ASSAODX11.cpp Draw's Apply into the restored render target, blend multiply (DrawOpaque false).
bool Apply(SlotState& slot, const AoTargets& t, const Graphics::ExtAoFrame& f, VkDeviceSize constants) {
    std::vector<VkImageMemoryBarrier2> barriers;
    MoveAll(g.results, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, &barriers);
    FlushBarriers(t.cb, &barriers);
    if (!DrawAoBeginApply()) return false;
    DrawDesc d;
    d.mainPass = true;
    d.formats[0] = t.colorFormat;
    d.colorCount = 1;
    d.samples = t.samples;
    d.multiply = true;
    d.area = { g.width, g.height };
    d.viewportWidth = static_cast<float>(g.width);
    d.viewportHeight = static_cast<float>(g.height);
    d.scissor = Rect(g.width, g.height);   // m_fullResOutScissorRect: the whole viewport
    d.textures[4] = g.results.gpu.view;
    d.constants = constants;
    if (f.qualityLevel < 0) {
        d.ps = kNonSmartHalfApply;
    } else if (f.qualityLevel == 0) {
        d.ps = kNonSmartApply;
    } else {
        d.ps = kApply;
    }
    return Draw(t.cb, slot, d);
}

// ---- lifetime ----------------------------------------------------------------------------------------

void DestroyLocked() {
    VkDevice dev = LiveDevice();
    if (dev) {
        for (auto& kv : g.pipelines) {
            if (kv.second) vkDestroyPipeline(dev, kv.second, nullptr);
        }
        for (SlotState& s : g.slots) {
            for (VkDescriptorPool p : s.pools)
                vkDestroyDescriptorPool(dev, p, nullptr);
            DestroyBufferNow(&s.ring);
        }
        if (g.layout) vkDestroyPipelineLayout(dev, g.layout, nullptr);
        if (g.pixelLayout) vkDestroyDescriptorSetLayout(dev, g.pixelLayout, nullptr);
        if (g.vertexLayout) vkDestroyDescriptorSetLayout(dev, g.vertexLayout, nullptr);
    }
    DropTargets(true);
    DestroyBufferNow(&g.triangle);
    for (Module& m : g.modules)
        DestroyShaderModule(m.module);
    g = State{};
}

// One conversion and one set of objects per device. The caller holds g_mutex.
bool InitLocked() {
    VkDevice dev = LiveDevice();
    if (!dev) return false;
    if (g.device == dev) return g.ready;
    if (g.device) DestroyLocked();
    g.device = dev;
    const Caps& caps = LiveCaps();
    if (caps.vulkan11) {
        // The apply draws inside draw.cpp's main pass with a pipeline made for its colour alone, which a
        // render pass with the main depth attached is not compatible with (vulkan11.h).
        std::fprintf(stderr, "[AmbientOcclusion] Vulkan: not supported on the Vulkan 1.1 path\n");
        return false;
    }
    if (!caps.fragmentStores || !caps.storageWriteWithoutFormat || !caps.aoFormats) {
        std::fprintf(stderr, "[AmbientOcclusion] Vulkan: not supported on this device (fragment-stage storage writes "
                             "%d, format-less storage writes %d, ASSAO target formats %d)\n",
                     caps.fragmentStores, caps.storageWriteWithoutFormat, caps.aoFormats);
        return false;
    }
    Ext::DerivedCache::Store* const cache = Ext::DerivedCache::OpenShared();
    unsigned hits = 0;
    for (int i = 0; i < kProgramCount; ++i) {
        std::vector<uint32_t> spirv;
        bool hit = false;
        std::string why;
        if (!Shaders::LoadOrConvert(cache, kBlobs[i].bytes, kBlobs[i].size, kBlobs[i].stage, &spirv,
                                    &g.modules[i].reflection, &hit, &why)) {
            std::fprintf(stderr, "[AmbientOcclusion] Vulkan: %s did not convert (%s): not supported\n", kBlobs[i].name,
                         why.c_str());
            return false;
        }
        g.modules[i].module = CreateShaderModule(spirv, &why);
        if (!g.modules[i].module) {
            std::fprintf(stderr, "[AmbientOcclusion] Vulkan: %s: %s: not supported\n", kBlobs[i].name, why.c_str());
            return false;
        }
        if (hit) ++hits;
    }

    // The pixel stage's set: cb0, t0..t5, s0..s3, u4 at shader_convert.h's bindings; the vertex stage's
    // is empty (its programs read nothing).
    std::vector<VkDescriptorSetLayoutBinding> bindings;
    const auto add = [&bindings](uint32_t binding, VkDescriptorType type) {
        VkDescriptorSetLayoutBinding b{};
        b.binding = binding;
        b.descriptorType = type;
        b.descriptorCount = 1;
        b.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        bindings.push_back(b);
    };
    add(Shaders::BindingOfConstantBuffer(0), VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    for (uint32_t t = 0; t < kTextureSlots; ++t)
        add(Shaders::BindingOfTexture(t), VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
    for (uint32_t s = 0; s < kSamplerSlots; ++s)
        add(Shaders::BindingOfSampler(s), VK_DESCRIPTOR_TYPE_SAMPLER);
    add(Shaders::BindingOfUav(kNormalsUav), VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
    auto li = Make<VkDescriptorSetLayoutCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO);
    li.bindingCount = static_cast<uint32_t>(bindings.size());
    li.pBindings = bindings.data();
    VkResult r = vkCreateDescriptorSetLayout(dev, &li, nullptr, &g.pixelLayout);
    auto empty = Make<VkDescriptorSetLayoutCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO);
    if (r == VK_SUCCESS) r = vkCreateDescriptorSetLayout(dev, &empty, nullptr, &g.vertexLayout);
    VkDescriptorSetLayout sets[2] = { g.vertexLayout, g.pixelLayout };
    auto pl = Make<VkPipelineLayoutCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO);
    pl.setLayoutCount = 2;
    pl.pSetLayouts = sets;
    if (r == VK_SUCCESS) r = vkCreatePipelineLayout(dev, &pl, nullptr, &g.layout);
    if (r != VK_SUCCESS) {
        std::fprintf(stderr, "[AmbientOcclusion] Vulkan: the pass's layouts: %s: not supported\n", ResultName(r));
        return false;
    }

    // ASSAODX11.cpp's samplers: point clamp, linear clamp, point mirror, point clamp (the depth taps);
    // CD3D11_SAMPLER_DESC's defaults otherwise (MaxLOD FLT_MAX, no anisotropy, no comparison).
    for (uint32_t s = 0; s < kSamplerSlots; ++s) {
        SamplerDesc d;
        const uint8_t linear = s == 1 ? 1 : 0;
        d.magLinear = linear;
        d.minLinear = linear;
        d.mipLinear = linear;
        const uint8_t address = s == 2 ? static_cast<uint8_t>(VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT)
                                       : static_cast<uint8_t>(VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
        d.addressU = address;
        d.addressV = address;
        d.addressW = address;
        d.maxLod = VK_LOD_CLAMP_NONE;
        g.samplers[s] = GetSampler(d);
    }

    // ASSAO's fullscreen triangle (CommonSimpleVertex: position, uv).
    const float vertices[18] = { -1.0f, 1.0f,  0.0f, 1.0f, 0.0f, 0.0f, 3.0f, 1.0f, 0.0f,
                                 1.0f,  2.0f,  0.0f, -1.0f, -3.0f, 0.0f, 1.0f, 0.0f, 2.0f };
    std::string why;
    if (!CreateStaticBuffer(sizeof(vertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, vertices, &g.triangle, &why)) {
        std::fprintf(stderr, "[AmbientOcclusion] Vulkan: fullscreen triangle: %s: not supported\n", why.c_str());
        return false;
    }
    for (SlotState& s : g.slots) {
        if (!CreateHostBuffer(kSlotBytes * kRingSlots, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, &s.ring, &why)) {
            std::fprintf(stderr, "[AmbientOcclusion] Vulkan: constants ring: %s: not supported\n", why.c_str());
            return false;
        }
    }
    g.ready = true;
    std::fprintf(stderr, "[AmbientOcclusion] Vulkan: ASSAO ready, %d programs (%u from the cache, %d converted); "
                         "normals UAV at set %u binding %u\n",
                 static_cast<int>(kProgramCount), hits, static_cast<int>(kProgramCount) - static_cast<int>(hits),
                 Shaders::kSetPixel, Shaders::BindingOfUav(kNormalsUav));
    return true;
}

}  // namespace

bool Ready() {
    std::lock_guard<std::mutex> guard(g_mutex);
    // P8 round 2: a pipeline the driver refused makes the pass "not supported" (AoBackend::Supported), so
    // the row goes inactive and the Enhanced tab shows it disabled, instead of a pass that silently draws
    // nothing every frame.
    return InitLocked() && !g.pipelineFailed;
}

void AfterOpaque(Video::Context& context, const Graphics::ExtAoFrame& frame, const Graphics::RendererSceneEntry& entry) {
    std::lock_guard<std::mutex> guard(g_mutex);
    if (!InitLocked()) return;
    // The readable depth's view; none while the rebuild that makes the depth readable has not run yet
    // (the frames between the toggle and the remake): no AO for those frames, as on DX11.
    AoTargets t;
    if (!DrawAoBegin(&t)) return;
    if (t.msrtss) {
        LogOnce(1, "the main target uses the tiler's MSAA (multisampled_render_to_single_sampled): the AO pass "
                   "reads its single-sampled depth (sample 0) as the non-MSAA path");
    }
    if (t.extent.width < 16 || t.extent.height < 16 || !Resize(t.extent.width, t.extent.height)) {
        DrawAoEnd();
        return;
    }
    SlotState& slot = g.slots[t.slot];
    if (slot.frame != t.frame) {
        // The slot's previous frame completed (BeginCommands waited for its fence).
        for (VkDescriptorPool p : slot.pools)
            vkResetDescriptorPool(LiveDevice(), p, 0);
        slot.pool = 0;
        slot.ringAt = 0;
        slot.frame = t.frame;
    }
    float projection[16];
    Ext::AmbientOcclusion::AoMath::AssaoProjection(entry._camera._projection.m, projection);
    // The fade follows this frame's fog (ao_pass.cpp: the materials fogged their pixels already).
    Graphics::ExtAoFrame fogged = frame;
    Ext::AmbientOcclusion::AoMath::FadeFromFog(context._fogParams.x, context._fogParams.y, fogged);

    VkCommandBuffer cb = t.cb;
    if (g.requiresClear) {
        ClearTargets(cb);
        g.requiresClear = false;
    }
    bool ok = ResolveDepth(cb, slot, t);
    // ASSAODX11.cpp Draw: UpdateConstants(pass 0), PrepareDepths, GenerateSSAO, Apply.
    const VkDeviceSize first = ok ? WriteConstants(slot, MakeConstants(projection, fogged, 0)) : ~0ull;
    ok = ok && first != ~0ull && PrepareDepths(cb, slot, fogged, first);
    const VkDeviceSize last = ok ? GenerateSsao(cb, slot, projection, fogged) : ~0ull;
    ok = ok && last != ~0ull && Apply(slot, t, fogged, last);
    if (!ok) LogOnce(2, "a step of the pass failed (logged above): this frame has no AO");
    if (g.frames++ == 0) {
        std::fprintf(stderr, "[AmbientOcclusion] Vulkan: first AO frame %ux%u x%u, quality %d, blur %d\n", g.width,
                     g.height, t.samples, fogged.qualityLevel, fogged.blurPassCount);
    }
    DrawAoEnd();
}

void Shutdown() {
    std::lock_guard<std::mutex> guard(g_mutex);
    if (!g.device) return;
    DestroyLocked();
}

}  // namespace Assao
}  // namespace VideoVulkan
