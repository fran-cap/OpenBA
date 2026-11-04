// NOT RETAIL. Intel ASSAO (MIT, see ../../dx11/ao/assao/LICENSE), native
// Metal orchestration of the repository's extension-owned HLSL/DXBC assets.
#include "assao_pass.h"
#include "video/Context.h"
#include "video/Buffers.h"
#include "video/ImageBuffer.h"
#include "graphics/Camera.h"
#include "ext/ambient_occlusion/ao_math.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>
#include "ao/shaders/VSMain.h"
#include "ao/shaders/PSPrepareDepthsAndNormals.h"
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

namespace VideoMetal::Assao {
namespace {
struct Constants {
    // Byte offsets match ASSAOConstants in ASSAO.hlsl (240 bytes).
    float viewport[2], halfViewport[2], depthUnpack[2], tanFov[2];
    float ndcMul[2], ndcAdd[2];
    int offset[2];
    float uvOffset[2], doublePixel[2], halfPixel[2];
    float radius, strength, power, clamp;
    float fadeMul, fadeAdd, horizon, nearLimitRec;
    float precision, negRadiusRec, loadDiv, adaptiveLimit;
    float invSharpness;
    int pass;
    float quarterPixel[2];
    float pattern[5][4];
    float normalsMul, normalsAdd, detail, padding;
};
static_assert(sizeof(Constants) == 240);
struct State {
    id<MTLDevice> device = nil;
    Shader vs, prepare, mip[3], generate[3], blur, wideBlur, simpleBlur, apply[3];
    id<MTLSamplerState> samplers[4];
    id<MTLBuffer> triangle;
    id<MTLBuffer> constantRing = nil;
    size_t constantNext = 0;
    id<MTLTexture> depths[4], normals, ping, pong, results, depthResolved;
    id<MTLRenderPipelineState> depthResolve = nil;
    unsigned width = 0, height = 0, hw = 0, hh = 0;
    std::unordered_map<std::string, id<MTLRenderPipelineState>> pipelines;
    bool ready = false;
};
State state;
template <size_t N>
Shader Load(const uint8_t (&bytes)[N], id<MTLDevice> device) {
    return Translate(std::vector<uint8_t>(bytes, bytes + N), device);
}
// The one lock over `state`: Ready (main thread) and Draw (render thread) both take it, Draw for
// the whole pass, so a device change in Init can never reset the state under a running Draw.
std::mutex stateMutex;
// The constants of every ASSAO draw live in one shared ring (kRingSlots slots of kSlotBytes),
// written in place instead of allocating a buffer per draw. The ring holds far more slots than
// one frame writes (about 50), so a slot is never reused while a frame still reads it.
constexpr size_t kSlotBytes = 256;
constexpr size_t kRingSlots = 4096;
static_assert(sizeof(Constants) <= kSlotBytes);

// One conversion per device. The caller holds stateMutex.
bool InitLocked(id<MTLDevice> device) {
    if (state.device == device) {
        return state.ready;
    }
    state = State{};
    state.device = device;
#define LOAD(field, name)                                                                 \
    state.field = Load(g_ao_##name, device);                                              \
    if (!state.field.function) {                                                          \
        std::fprintf(stderr, "[MetalPasses] ASSAO shader " #name " failed to convert\n"); \
        return false;                                                                     \
    }
    LOAD(vs, VSMain);
    LOAD(prepare, PSPrepareDepthsAndNormals);
    LOAD(mip[0], PSPrepareDepthMip1);
    LOAD(mip[1], PSPrepareDepthMip2);
    LOAD(mip[2], PSPrepareDepthMip3);
    LOAD(generate[0], PSGenerateQ0);
    LOAD(generate[1], PSGenerateQ1);
    LOAD(generate[2], PSGenerateQ2);
    LOAD(blur, PSSmartBlur);
    LOAD(wideBlur, PSSmartBlurWide);
    LOAD(simpleBlur, PSNonSmartBlur);
    LOAD(apply[0], PSNonSmartHalfApply);
    LOAD(apply[1], PSNonSmartApply);
    LOAD(apply[2], PSApply);
#undef LOAD
    for (unsigned i = 0; i < 4; ++i) {
        auto d = [MTLSamplerDescriptor new];
        const bool linear = i == 1;
        d.minFilter = linear ? MTLSamplerMinMagFilterLinear : MTLSamplerMinMagFilterNearest;
        d.magFilter = d.minFilter;
        d.mipFilter = linear ? MTLSamplerMipFilterLinear : MTLSamplerMipFilterNearest;
        d.sAddressMode = i == 2 ? MTLSamplerAddressModeMirrorRepeat : MTLSamplerAddressModeClampToEdge;
        d.tAddressMode = d.sAddressMode;
        d.rAddressMode = d.sAddressMode;
        d.supportArgumentBuffers = YES;
        state.samplers[i] = [device newSamplerStateWithDescriptor:d];
    }
    const float vertices[18] = { -1, 1, 0, 1, 0, 0, 3, 1, 0, 1, 2, 0, -1, -3, 0, 1, 0, 2 };
    state.triangle = [device newBufferWithBytes:vertices length:sizeof(vertices) options:MTLResourceStorageModeShared];
    state.constantRing = [device newBufferWithLength:kSlotBytes * kRingSlots options:MTLResourceStorageModeShared];
    if (!state.triangle || !state.constantRing) {
        // An allocation failed (nil): the pass stays not ready for this device (Ready() answers no) instead of
        // writing its constants through a nil ring's NULL contents on the first AO frame.
        std::fprintf(stderr, "[MetalPasses] ASSAO buffers failed to allocate\n");
        return false;
    }
    state.constantNext = 0;
    state.ready = true;
    std::fprintf(stderr, "[MetalPasses] ASSAO shaders converted and loaded\n");
    return true;
}

// Copies `c` into the next ring slot; returns the slot's GPU address.
uint64_t WriteConstants(const Constants& c) {
    const size_t offset = (state.constantNext % kRingSlots) * kSlotBytes;
    state.constantNext++;
    std::memcpy(static_cast<char*>(state.constantRing.contents) + offset, &c, sizeof(c));
    return state.constantRing.gpuAddress + offset;
}

id<MTLTexture> Texture(unsigned w, unsigned h, MTLPixelFormat format, unsigned mips = 1, unsigned layers = 1,
                       bool write = false) {
    auto d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:format width:w height:h mipmapped:NO];
    d.mipmapLevelCount = mips;
    d.arrayLength = layers;
    if (layers > 1) d.textureType = MTLTextureType2DArray;
    d.storageMode = MTLStorageModePrivate;
    d.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead | MTLTextureUsagePixelFormatView;
    if (write) d.usage |= MTLTextureUsageShaderWrite;
    return [state.device newTextureWithDescriptor:d];
}
void Resize(unsigned w, unsigned h) {
    if (state.width == w && state.height == h) return;
    state.width = w;
    state.height = h;
    state.hw = (w + 1) / 2;
    state.hh = (h + 1) / 2;
    // ASSAO requires four depth mips. Keep small viewports valid too.
    unsigned levels = 1;
    while (levels < 4 && (state.hw >> (levels - 1)) > 1 && (state.hh >> (levels - 1)) > 1)
        ++levels;
    for (auto& depth : state.depths)
        depth = Texture(state.hw, state.hh, MTLPixelFormatR16Float, levels);
    state.normals = Texture(w, h, MTLPixelFormatRGBA8Unorm, 1, 1, true);
    state.ping = Texture(state.hw, state.hh, MTLPixelFormatRG8Unorm);
    state.pong = Texture(state.hw, state.hh, MTLPixelFormatRG8Unorm);
    state.results = Texture(state.hw, state.hh, MTLPixelFormatRG8Unorm, 1, 4);
    state.depthResolved = Texture(w, h, MTLPixelFormatR32Float);
    std::fprintf(stderr, "[MetalPasses] ASSAO targets %ux%u\n", w, h);
}
Constants MakeConstants(const float* p, const Graphics::ExtAoFrame& f, int pass) {
    Constants c{};
    c.viewport[0] = 1.f / state.width;
    c.viewport[1] = 1.f / state.height;
    c.halfViewport[0] = 1.f / state.hw;
    c.halfViewport[1] = 1.f / state.hh;
    c.depthUnpack[0] = -p[14];
    c.depthUnpack[1] = p[10];
    if (c.depthUnpack[0] * c.depthUnpack[1] < 0) c.depthUnpack[1] = -c.depthUnpack[1];
    c.tanFov[0] = 1.f / p[0];
    c.tanFov[1] = 1.f / p[5];
    for (int i = 0; i < 2; ++i) {
        c.ndcMul[i] = c.tanFov[i] * (i ? -2 : 2);
        c.ndcAdd[i] = c.tanFov[i] * (i ? 1 : -1);
        c.doublePixel[i] = 2 * c.viewport[i];
        c.halfPixel[i] = .5f * c.viewport[i];
    }
    c.offset[0] = pass % 2;
    c.offset[1] = pass / 2;
    c.uvOffset[0] = c.offset[0] * c.viewport[0];
    c.uvOffset[1] = c.offset[1] * c.viewport[1];
    c.radius = std::clamp(f.radius, 0.0001f, 100000.f);
    c.strength = std::clamp(f.shadowMultiplier * 4.3f, 0.f, 10.f);
    c.power = std::clamp(f.shadowPower, 0.f, 10.f);
    c.clamp = .98f;
    c.fadeMul = -1.f / (f.fadeOutTo - f.fadeOutFrom);
    c.fadeAdd = 1 + f.fadeOutFrom / (f.fadeOutTo - f.fadeOutFrom);
    c.horizon = std::clamp(f.horizonAngleThreshold, 0.f, 1.f);
    float limit = f.radius * 1.2f;
    if (f.qualityLevel <= 0) limit *= 1.5f;
    if (f.qualityLevel < 0) c.radius *= .8f;
    c.nearLimitRec = c.tanFov[1] / limit;
    c.precision = .9992f;
    c.negRadiusRec = -1.f / c.radius;
    c.quarterPixel[0] = 1.f / ((state.hw + 1) / 2);
    c.quarterPixel[1] = 1.f / ((state.hh + 1) / 2);
    c.loadDiv = 9 * c.quarterPixel[0] * c.quarterPixel[1] / 255;
    c.adaptiveLimit = .45f;
    c.invSharpness = std::clamp(1 - f.sharpness, 0.f, 1.f);
    c.pass = pass;
    const int permutation[5] = { 0, 1, 4, 3, 2 };
    for (int i = 0; i < 5; ++i) {
        int b = permutation[i];
        float angle = (pass + b / 5.f) * 1.57079632679f;
        float scale = 1 + (pass - 1.5f + (b - 2) / 5.f) * .07f;
        c.pattern[i][0] = scale * std::cos(angle);
        c.pattern[i][1] = -scale * std::sin(angle);
        c.pattern[i][2] = -scale * std::sin(angle);
        c.pattern[i][3] = -scale * std::cos(angle);
    }
    c.normalsMul = 2;
    c.normalsAdd = -1;
    c.detail = f.detailShadowStrength;
    return c;
}
// Separate native encoders isolate this pass from all engine cached bindings.
void Bind(id<MTLRenderCommandEncoder> enc, Shader& shader, uint64_t constantsAddress,
          const std::array<id<MTLTexture>, 5>& textures, id<MTLTexture> uav, bool vertex) {
    auto stages = vertex ? MTLRenderStageVertex : MTLRenderStageFragment;
    std::vector<uint64_t> cb(std::max(1u, shader.reflection.NumConstantBuffers));
    for (auto& a : shader.constants) {
        if (a.StructurePtrOffset < cb.size()) {
            cb[a.StructurePtrOffset] = constantsAddress;
        }
    }
    [enc useResource:state.constantRing usage:MTLResourceUsageRead stages:stages];
    std::vector<uint64_t> args(std::max(1u, shader.reflection.ArgumentTableQwords));
    for (auto& a : shader.arguments) {
        auto slot = a.SM50BindingSlot;
        auto off = a.StructurePtrOffset;
        if (off >= args.size()) {
            continue;
        }
        if (a.Type == SM50BindingType::Sampler && slot < 4) {
            args[off] = state.samplers[slot].gpuResourceID._impl;
            if (off + 1 < args.size()) {
                args[off + 1] = args[off];
            }
        } else if (a.Flags & MTL_SM50_SHADER_ARGUMENT_TEXTURE) {
            const bool isUav = a.Type == SM50BindingType::UAV;
            auto t = isUav ? uav : (slot < textures.size() ? textures[slot] : nil);
            if (t) {
                args[off] = t.gpuResourceID._impl;
                if (off + 1 < args.size()) {
                    args[off + 1] = uint64_t(t.arrayLength) << 32;
                }
                [enc useResource:t usage:isUav ? MTLResourceUsageWrite : MTLResourceUsageRead stages:stages];
            }
        }
    }
    if (vertex) {
        [enc setVertexBytes:cb.data() length:cb.size() * 8 atIndex:0];
        [enc setVertexBytes:args.data() length:args.size() * 8 atIndex:1];
    } else {
        [enc setFragmentBytes:cb.data() length:cb.size() * 8 atIndex:0];
        [enc setFragmentBytes:args.data() length:args.size() * 8 atIndex:1];
    }
}

// The pipeline for (shader, sample count, blend, target formats), built once and cached.
id<MTLRenderPipelineState> FullscreenPipeline(Shader& shader, const std::vector<id<MTLTexture>>& targets,
                                              bool multiply, unsigned samples) {
    std::string key = std::to_string(samples) + "s" + std::to_string((uintptr_t)(__bridge void*)shader.function) +
                      (multiply ? "m" : "o");
    auto desc = [MTLRenderPipelineDescriptor new];
    desc.vertexFunction = state.vs.function;
    desc.fragmentFunction = shader.function;
    desc.rasterSampleCount = samples;
    auto layout = [MTLVertexDescriptor vertexDescriptor];
    layout.attributes[0].format = MTLVertexFormatFloat4;
    layout.attributes[0].bufferIndex = 8;
    layout.attributes[1].format = MTLVertexFormatFloat2;
    layout.attributes[1].bufferIndex = 8;
    layout.attributes[1].offset = 16;
    layout.layouts[8].stride = 24;
    desc.vertexDescriptor = layout;
    for (unsigned i = 0; i < targets.size(); ++i) {
        auto a = desc.colorAttachments[i];
        a.pixelFormat = targets[i].pixelFormat;
        key += ":" + std::to_string(a.pixelFormat);
        if (multiply) {
            a.blendingEnabled = YES;
            a.sourceRGBBlendFactor = MTLBlendFactorZero;
            a.destinationRGBBlendFactor = MTLBlendFactorSourceColor;
            a.writeMask = MTLColorWriteMaskRed | MTLColorWriteMaskGreen | MTLColorWriteMaskBlue;
        }
    }
    auto found = state.pipelines.find(key);
    if (found != state.pipelines.end()) {
        return found->second;
    }
    NSError* error = nil;
    auto p = [state.device newRenderPipelineStateWithDescriptor:desc error:&error];
    if (!p) {
        std::fprintf(stderr, "[MetalPasses] ASSAO pipeline: %s\n", error.localizedDescription.UTF8String);
        return nil;
    }
    state.pipelines.emplace(key, p);
    return p;
}

bool Fullscreen(id<MTLCommandBuffer> commands, Shader& shader, const Constants& c,
                const std::vector<id<MTLTexture>>& targets, const std::array<id<MTLTexture>, 5>& textures,
                id<MTLTexture> uav = nil, int slice = 0, bool multiply = false, unsigned samples = 1) {
    auto pipeline = FullscreenPipeline(shader, targets, multiply, samples);
    if (!pipeline) {
        return false;
    }
    auto pass = [MTLRenderPassDescriptor renderPassDescriptor];
    for (unsigned i = 0; i < targets.size(); ++i) {
        auto a = pass.colorAttachments[i];
        a.texture = targets[i];
        a.slice = targets[i].textureType == MTLTextureType2DArray ? slice : 0;
        a.loadAction = multiply ? MTLLoadActionLoad : MTLLoadActionDontCare;
        a.storeAction = MTLStoreActionStore;
    }
    const uint64_t constantsAddress = WriteConstants(c);
    auto enc = [commands renderCommandEncoderWithDescriptor:pass];
    [enc setRenderPipelineState:pipeline];
    [enc setCullMode:MTLCullModeNone];
    [enc setVertexBuffer:state.triangle offset:0 atIndex:8];
    [enc setViewport:MTLViewport{ 0, 0, double(targets[0].width), double(targets[0].height), 0, 1 }];
    Bind(enc, state.vs, constantsAddress, textures, uav, true);
    Bind(enc, shader, constantsAddress, textures, uav, false);
    [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
    [enc endEncoding];
    return true;
}

// MSAA main depth -> single-sample R32Float (sample 0), what DX11's AO pass does before ASSAO.
id<MTLTexture> ResolveDepth(id<MTLCommandBuffer> commands, id<MTLTexture> depth) {
    if (!state.depthResolve) {
        static NSString* const source =
            @"#include <metal_stdlib>\nusing namespace metal;\n"
             "struct V{float4 p [[position]];};\n"
             "vertex V rv(uint i [[vertex_id]]){float2 q=float2(float((i<<1)&2),float(i&2));V "
             "o;o.p=float4(q*2.0-1.0,0.0,1.0);return o;}\n"
             "fragment float4 rf(V v [[stage_in]],texture2d_ms<float,access::read> d [[texture(0)]]){return "
             "float4(d.read(uint2(v.p.xy),0).x,0.0,0.0,1.0);}\n";
        NSError* error = nil;
        auto lib = [state.device newLibraryWithSource:source options:nil error:&error];
        if (!lib) {
            std::fprintf(stderr, "[MetalPasses] depth resolve library: %s\n", error.localizedDescription.UTF8String);
            return nil;
        }
        auto desc = [MTLRenderPipelineDescriptor new];
        desc.vertexFunction = [lib newFunctionWithName:@"rv"];
        desc.fragmentFunction = [lib newFunctionWithName:@"rf"];
        desc.colorAttachments[0].pixelFormat = MTLPixelFormatR32Float;
        state.depthResolve = [state.device newRenderPipelineStateWithDescriptor:desc error:&error];
        if (!state.depthResolve) {
            std::fprintf(stderr, "[MetalPasses] depth resolve pipeline: %s\n", error.localizedDescription.UTF8String);
            return nil;
        }
    }
    auto pass = [MTLRenderPassDescriptor renderPassDescriptor];
    auto a = pass.colorAttachments[0];
    a.texture = state.depthResolved;
    a.loadAction = MTLLoadActionDontCare;
    a.storeAction = MTLStoreActionStore;
    auto enc = [commands renderCommandEncoderWithDescriptor:pass];
    [enc setRenderPipelineState:state.depthResolve];
    [enc setFragmentTexture:depth atIndex:0];
    [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
    [enc endEncoding];
    return state.depthResolved;
}
}  // namespace

// True when the device is live and every ASSAO shader converted (converts them on first use).
bool Ready() {
    if (!DeviceLive()) {
        return false;
    }
    id<MTLDevice> device = LiveDevice();
    if (!device) {
        return false;
    }
    std::lock_guard<std::mutex> guard(stateMutex);
    return InitLocked(device);
}

bool Draw(id<MTLDevice> device, id<MTLCommandBuffer> commands, id<MTLTexture> color, id<MTLTexture> depth,
          const float projection[16], const Graphics::ExtAoFrame& frame) {
    if (!commands || !color || !depth) return false;
    std::lock_guard<std::mutex> guard(stateMutex);
    if (!InitLocked(device)) return false;
    if (depth.width < 16 || depth.height < 16 || depth.width != color.width || depth.height != color.height)
        return false;
    Resize(unsigned(depth.width), unsigned(depth.height));
    id<MTLTexture> source = depth;
    if (depth.sampleCount > 1) {
        source = ResolveDepth(commands, depth);
        if (!source) return false;
    }
    auto c = MakeConstants(projection, frame, 0);
    if (!Fullscreen(commands, state.prepare, c, {state.depths[0], state.depths[1], state.depths[2], state.depths[3]},
                    {source, nil, nil, nil, nil}, state.normals))
        return false;
    if (frame.qualityLevel > 1)
        for (unsigned level = 1; level < state.depths[0].mipmapLevelCount; ++level) {
            std::vector<id<MTLTexture>> targets;
            std::array<id<MTLTexture>, 5> inputs{};
            for (unsigned i = 0; i < 4; ++i) {
                auto d = state.depths[i];
                targets.push_back([d newTextureViewWithPixelFormat:d.pixelFormat
                                                       textureType:MTLTextureType2D
                                                            levels:NSMakeRange(level, 1)
                                                            slices:NSMakeRange(0, 1)]);
                inputs[i] = [d newTextureViewWithPixelFormat:d.pixelFormat
                                                 textureType:MTLTextureType2D
                                                      levels:NSMakeRange(level - 1, 1)
                                                      slices:NSMakeRange(0, 1)];
            }
            if (!Fullscreen(commands, state.mip[level - 1], c, targets, inputs)) return false;
        }
    for (int pass = 0; pass < 4; ++pass) {
        if (frame.qualityLevel < 0 && (pass == 1 || pass == 2)) continue;
        c = MakeConstants(projection, frame, pass);
        int blur = std::clamp(frame.blurPassCount, 0, frame.qualityLevel <= 0 ? 1 : 6);
        auto ping = state.ping, pong = state.pong;
        if (!Fullscreen(commands, state.generate[std::clamp(frame.qualityLevel, 0, 2)], c,
                        {blur ? ping : state.results}, {state.depths[pass], state.normals, nil, nil, nil}, nil, pass))
            return false;
        for (int i = 0; i < blur; ++i) {
            auto& shader = frame.qualityLevel <= 0 ? state.simpleBlur : (i < blur - 2 ? state.wideBlur : state.blur);
            if (!Fullscreen(commands, shader, c, {i == blur - 1 ? state.results : pong}, {nil, nil, ping, nil, nil},
                            nil, pass))
                return false;
            std::swap(ping, pong);
        }
    }
    return Fullscreen(commands, state.apply[frame.qualityLevel < 0 ? 0 : (frame.qualityLevel == 0 ? 1 : 2)], c, {color},
                      {nil, nil, nil, nil, state.results}, nil, 0, true, unsigned(color.sampleCount));
}
void EncodeAfterOpaque(id<MTLDevice> device, id<MTLCommandBuffer> commands,
                       Video::Context& context, const Graphics::ExtAoFrame& frame,
                       const Graphics::RendererSceneEntry& entry) {
    auto rb = context._defaultRenderBuffer;
    if (!rb || !rb->colorBuffer || !rb->depthBuffer || !rb->colorBuffer->_data || !rb->depthBuffer->_data) return;
    auto color = static_cast<ImageBufferData*>(rb->colorBuffer->_data)->texture;
    auto depth = static_cast<ImageBufferData*>(rb->depthBuffer->_data)->texture;
    float projection[16];
    Ext::AmbientOcclusion::AoMath::AssaoProjection(entry._camera._projection.m, projection);
    auto fogged = frame;
    Ext::AmbientOcclusion::AoMath::FadeFromFog(context._fogParams.x, context._fogParams.y, fogged);
    Draw(device, commands, color, depth, projection, fogged);
}
}  // namespace VideoMetal::Assao

namespace VideoMetal {

bool AssaoReady() {
    return Assao::Ready();
}

}  // namespace VideoMetal
