#include "app/options.h"
#include "VideoMetalData.h"
#include "fast_pipeline_cache.h"
#include "retire_pool.h"
#include "shader_cache.h"
#include "inline_bytes.h"
#include "device_support.h"
#include "platform/common/platform_window_hooks.h"
#include "system/Error.h"
#include "display_geometry.h"
#include "passes/tree_passes.h"
#include "passes/assao_pass.h"
#include "compress_ao/bc4_create.h"
#include "compress_ao/bc4_chain.h"
#include "share_gpu_data/share_create.h"
#include "create_on_demand/lazy_create.h"
#include "seeded_create.h"
#include "video/Device.h"
#include "platform/common/cursor_image.h"
#include "video/Buffers.h"
#include "video/ImageBuffer.h"
#include "video/ShaderProgram.h"
#include "video/VertexLayout.h"
#include "video/RenderState.h"
#include "video/Context.h"
#include "io/Serializer.h"
#include "system/MemoryAllocator.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_metal.h>
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <new>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <cstdlib>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <memory>
#include <atomic>
#include <dispatch/dispatch.h>
// Serial single-sample baseline: translated DXBC and native Metal draw encoding.
namespace VideoMetal {
static id<MTLDevice> gpu;
static id<MTLCommandQueue> queue;
static CAMetalLayer* layer;
static SDL_MetalView view;
static id<CAMetalDrawable> drawable;
// Render pipe ON: the render thread encodes while loader/main threads create, lock and destroy buffers and images.
// This one narrow lock guards the state both sides touch: bufferUses, expandedInputs, the alias sets, the stat
// allocation lists and bound.vertex/bound.index (a rename re-points them). It is never held across a GPU wait,
// a shader/pipeline compile or a memcpy of buffer contents. Everything else in `bound` is render-thread only.
static std::recursive_mutex stateMutex;
using StateGuard = std::lock_guard<std::recursive_mutex>;

static id<MTLCommandBuffer> commands;
static id<MTLRenderCommandEncoder> activeEncoder;
static id<MTLTexture> encoderColor, encoderDepth;
static id<MTLRenderPipelineState> activePipeline;
static void EndEncoder() {
    if (activeEncoder) [activeEncoder endEncoding];
    activeEncoder = nil;
    encoderColor = nil;
    encoderDepth = nil;
    activePipeline = nil;
}
static id<MTLTexture> mainDepth, mainColor, targetColor, targetDepth;   // render thread only
static std::unordered_set<ImageBufferData*> colorAliases, depthAliases;
static id<MTLTexture> ImageTexture(const Video::ImageBuffer* image) {
    return image && image->_data ? static_cast<ImageBufferData*>(image->_data)->texture : nil;
}
// Retained bindings are applied when the draw encoder is created.
struct Bindings {
    id<MTLBuffer> vertex[31] = {}, index = nil, constants[2][31] = {};
    Shader* vs = nullptr;
    Shader* ps = nullptr;
    bool scissor = false;
    bool vertexDynamic[31] = {};
    NSUInteger vertexOffsets[31] = {}, strides[31] = {};
    id<MTLTexture> images[31] = {};
    id<MTLSamplerState> samplers[31] = {};
    MTLVertexDescriptor* layout = nil;
    id<MTLDepthStencilState> depth = nil;
    MTLRenderPipelineColorAttachmentDescriptor* blend = nil;
    MTLCullMode cull = MTLCullModeBack;
    MTLTriangleFillMode fill = MTLTriangleFillModeFill;
    float bias = 0, slopeBias = 0;
    unsigned stencilReference = 0;
};
static Bindings bound;
// Per-frame stall accumulators (ms), written as extra columns of standalone_metal_frames.csv when
// BANISHED_METAL_STATS=1.
static double statSem = 0, statDrawable = 0, statPipeline = 0, statBuffer = 0;
static unsigned statPipelinesNew = 0;
static inline double StatNow() {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}
static dispatch_semaphore_t inFlight;
static std::atomic<uint64_t> frameGeneration{ 0 };   // read by Lock() on any thread
static unsigned frameSlot = 0;
constexpr unsigned kFramesInFlight = 3;
struct UploadArena {
    std::vector<id<MTLBuffer>> pages;
    size_t page = 0, cursor = 0;
};
static UploadArena uploads[kFramesInFlight];
// Completion tickets cover encoded, submitted and executing draws. They never
// retain Metal objects; the command buffer itself retains its encoded resources.
using Completion = std::shared_ptr<std::atomic<bool>>;
static Completion currentCompletion;
// Every access to `commands`, the encoder state and currentCompletion happens with the device lock held
// (Video::Device::Lock, recursive): the render thread takes it in BeginCommands BEFORE NewCommands and drops it in
// Finish; ImageBufferData::Read (main thread, readback) takes it for its whole body. The device lock is already held
// for the whole frame, so Read cannot start a segment in the middle of the render thread's encoding. A private
// command buffer for Read would not work: it has to see the frame's already-encoded draws (FrameCopy, screenshots).
// Taking the lock after the in-flight semaphore wait keeps the time the main thread can be blocked to the encode
// itself.
static id<MTLCommandBuffer> NewCommands() {
    auto cb = [queue commandBuffer];
    auto done = std::make_shared<std::atomic<bool>>(false);
    {
        StateGuard guard(stateMutex);
        currentCompletion = done;
    }
    [cb addCompletedHandler:^(id<MTLCommandBuffer>) {
      done->store(true, std::memory_order_release);
    }];
    return cb;
}
struct BufferUse {
    __weak id<MTLBuffer> buffer;
    Completion completion;
};
static std::unordered_map<void*, BufferUse> bufferUses;
static bool BufferBusy(id<MTLBuffer> buffer) {   // stateMutex held
    auto it = bufferUses.find((__bridge void*)buffer);
    if (it == bufferUses.end()) return false;
    auto resource = it->second.buffer;
    return resource == buffer && !it->second.completion->load(std::memory_order_acquire);
}
static void UseBuffer(id<MTLBuffer> buffer) {
    StateGuard guard(stateMutex);
    if (buffer) bufferUses[(__bridge void*)buffer] = BufferUse{ buffer, currentCompletion };
}
static uint64_t renameCount = 0, renameBytes = 0, inPlaceWrites = 0;
struct BufferAllocation {
    __weak id<MTLBuffer> buffer;
    const char* kind;
};
static std::vector<BufferAllocation> bufferAllocations;
static id<MTLBuffer> NewBuffer(NSUInteger size, const char* kind) {
    auto b = [gpu newBufferWithLength:size options:MTLResourceStorageModeShared];
    if (Opt().metalStats && b) {
        StateGuard guard(stateMutex);
        bufferAllocations.push_back({ b, kind });
    }
    return b;
}
static std::vector<__weak id<MTLTexture>> textureAllocations;
static id<MTLTexture> NewTextureResource(MTLTextureDescriptor* descriptor) {
    auto t = [gpu newTextureWithDescriptor:descriptor];
    if (Opt().metalStats && t) {
        StateGuard guard(stateMutex);
        textureAllocations.push_back(t);
    }
    return t;
}
static void ReportBuffers() {
    StateGuard guard(stateMutex);
    size_t textureCount = 0, textureBytes = 0;
    for (auto it = textureAllocations.begin(); it != textureAllocations.end();) {
        auto t = *it;
        if (!t) {
            it = textureAllocations.erase(it);
            continue;
        }
        ++textureCount;
        textureBytes += t.allocatedSize;
        ++it;
    }
    fprintf(stderr, "[MetalTextures] count=%zu bytes=%zu\n", textureCount, textureBytes);
    std::unordered_map<std::string, std::pair<size_t, size_t>> kinds;
    for (auto it = bufferAllocations.begin(); it != bufferAllocations.end();) {
        auto b = it->buffer;
        if (!b) {
            it = bufferAllocations.erase(it);
            continue;
        }
        auto& k = kinds[it->kind];
        ++k.first;
        k.second += b.length;
        ++it;
    }
    for (auto& k : kinds)
        fprintf(stderr, "[MetalBuffers] kind=%s count=%zu bytes=%zu\n", k.first.c_str(), k.second.first,
                k.second.second);
}

// Dynamic buffers (the engine's per-frame vertex/index streams) are written once per frame while the previous
// frames' draws are still reading the allocation, so they cannot be overwritten in place. Instead of a fresh
// full-size MTLBuffer every frame, a buffer the GPU has just finished with is recycled: RenameBuffer retires the
// busy allocation here, tagged with the completion ticket of the last command buffer that used it, and takes
// an equally sized retired one whose ticket is done. Per dynamic buffer this settles on a ring of up to
// kFramesInFlight allocations, the "N buffers per dynamic buffer indexed by the in-flight slot" Apple's guidance
// asks for, but without a per-buffer slot table: any retired buffer of the same length is interchangeable (a
// DISCARD write does not care about the old contents; a preserving write copies them). Entries that find no taker
// for kRetiredBufferMaxAge frames (their owner was destroyed or resized) are dropped by BeginCommands.
constexpr size_t kRetiredBufferMax = 128;   // retired-buffer list bound (see retire_pool.h)
static RetirePool<id<MTLBuffer>> retiredBuffers(kRetiredBufferMax);   // stateMutex
static uint64_t recycledBuffers = 0;
constexpr uint64_t kRetiredBufferMaxAge = 180;
// stateMutex held. Hands out a retired buffer of `length` only when no unfinished command buffer references it:
// its retire-time ticket AND its current last-use ticket (BufferBusy) must both be complete.
static id<MTLBuffer> TakeRetiredBuffer(NSUInteger length) {
    id<MTLBuffer> recycled = nil;
    retiredBuffers.Take(length, [](id<MTLBuffer> candidate) { return BufferBusy(candidate); }, recycled);
    return recycled;
}
static void TrimRetiredBuffers() {   // stateMutex held
    retiredBuffers.Trim(frameGeneration.load(), kRetiredBufferMaxAge);
}
// Loading runs outside Draw/BeginCommands pools. Metal's transient autoreleased
// references must drain here too, or even released terrain copies survive load.
static void InvalidateExpanded(id<MTLBuffer> source);
static void RenameBuffer(id<MTLBuffer> __strong& buffer, bool preserve) {
    @autoreleasepool {
        auto previous = buffer;
        Completion previousCompletion;
        id<MTLBuffer> fresh = nil;
        {
            StateGuard guard(stateMutex);
            if (!BufferBusy(previous)) {
                ++inPlaceWrites;
                return;
            }
            previousCompletion = bufferUses[(__bridge void*)previous].completion;
            fresh = TakeRetiredBuffer(previous.length);
        }
        double t0 = StatNow();
        if (fresh) {
            ++recycledBuffers;
            // The recycled allocation may still be a key of a cached expansion (it was a source before it was retired).
            InvalidateExpanded(fresh);
        } else {
            fresh = NewBuffer(previous.length, "rename");
        }
        if (preserve) {
            memcpy(fresh.contents, previous.contents, previous.length);
        }
        StateGuard guard(stateMutex);
        ++renameCount;
        renameBytes += previous.length;
        statBuffer += StatNow() - t0;
        buffer = fresh;
        for (auto& vertex : bound.vertex) {
            if (vertex == previous) {
                vertex = buffer;
            }
        }
        if (bound.index == previous) {
            bound.index = buffer;
        }
        retiredBuffers.Retire(previous, previous.length, previousCompletion, frameGeneration.load());
    }
}
static NSUInteger constantOffsets[2][31] = {};
static NSUInteger constantSizes[2][31] = {};
// A block of `size` bytes in this frame slot's upload pages. A page holds at least 8 MB; a block bigger than the
// page it lands on gets a page of its own (the slot's pages are reused every third frame, so a larger later upload
// replaces a too-small page instead of overflowing it).
static id<MTLBuffer> Upload(const void* src, size_t size, NSUInteger& offset) {
    constexpr size_t kUploadPage = 8 * 1024 * 1024;
    auto& arena = uploads[frameSlot];
    size_t aligned = (size + 255) & ~size_t(255);
    int replacements = 0;
    for (;;) {
        if (arena.page >= arena.pages.size()) {
            double t0 = StatNow();
            arena.pages.push_back(NewBuffer(std::max(kUploadPage, aligned), "constant-upload"));
            statBuffer += StatNow() - t0;
        }
        if (arena.cursor + aligned <= arena.pages[arena.page].length) {
            break;
        }
        if (arena.cursor == 0) {
            // An existing page that is too small for this block: replace it with one that fits.
            if (++replacements > 2) {
                static bool logged = false;
                if (!logged) {
                    logged = true;
                    fprintf(stderr, "[Metal] upload of %zu bytes failed: no buffer could be allocated\n", size);
                }
                return nil;
            }
            arena.pages[arena.page] = NewBuffer(aligned, "constant-upload");
            continue;
        }
        ++arena.page;
        arena.cursor = 0;
    }
    auto buffer = arena.pages[arena.page];
    if (!buffer) {
        return nil;
    }
    offset = arena.cursor;
    memcpy(static_cast<uint8_t*>(buffer.contents) + offset, src, size);
    arena.cursor += aligned;
    return buffer;
}
// Keyed by the raw addresses of the bound function / layout / blend objects (plus POD state); the owners'
// destructors evict their addresses (EvictPipelineOwner) so a recycled address can never hit a stale pipeline.
static FastPipelineCache<id<MTLRenderPipelineState>> pipelines;
void EvictPipelineOwner(const void* owner) {
    pipelines.Evict(owner);
}
static id<MTLBuffer> zeroConstants;
struct ExpandedInput {
    id<MTLBuffer> source = nil, expanded = nil;
};
static std::unordered_map<std::string, ExpandedInput> expandedInputs;   // stateMutex
static void InvalidateExpanded(id<MTLBuffer> source) {
    StateGuard guard(stateMutex);
    for (auto it = expandedInputs.begin(); it != expandedInputs.end();)
        if (it->second.source == source)
            it = expandedInputs.erase(it);
        else
            ++it;
}
static uint64_t totalDraws = 0, indexedDraws = 0, instancedDraws = 0, shadowDraws = 0;
template <class T>
static void Key(std::string& key, const T& value) {
    key.append(reinterpret_cast<const char*>(&value), sizeof(value));
}
#include "pipeline_cache.h"
static id<MTLRenderPipelineState> Pipeline() {
    if (!bound.vs || !bound.vs->function || (!targetColor && !targetDepth)) return nil;
    std::string key;
    auto vf = (__bridge void*)bound.vs->function;
    auto pf = (__bridge void*)(bound.ps ? bound.ps->function : nil);
    auto layout = (__bridge void*)bound.layout;
    auto blendKey = (__bridge void*)bound.blend;
    bool a2c = bound.ps && TreePasses::IsCoverage(bound.ps) && !(bound.blend && bound.blend.blendingEnabled);
    Key(key, vf);
    Key(key, pf);
    Key(key, layout);
    Key(key, blendKey);
    Key(key, a2c);
    for (auto stride : bound.strides)
        Key(key, stride);
    unsigned samples = targetColor ? unsigned(targetColor.sampleCount) : unsigned(targetDepth.sampleCount);
    auto colorFormat = targetColor ? targetColor.pixelFormat : MTLPixelFormatInvalid;
    auto depthFormat = targetDepth ? targetDepth.pixelFormat : MTLPixelFormatInvalid;
    Key(key, colorFormat);
    Key(key, depthFormat);
    Key(key, samples);
    {
        id<MTLRenderPipelineState> hit = nil;
        if (pipelines.Find(key, hit)) return hit;
    }
    auto desc = [MTLRenderPipelineDescriptor new];
    desc.vertexFunction = bound.vs->function;
    desc.fragmentFunction = bound.ps ? bound.ps->function : nil;
    desc.vertexDescriptor = [bound.layout copy];
    for (unsigned i = 0; i < 23; ++i)
        if (bound.strides[i]) desc.vertexDescriptor.layouts[i + 8].stride = bound.strides[i];
    for (MTLVertexAttribute* input in bound.vs->function.vertexAttributes) {
        auto a = desc.vertexDescriptor.attributes[input.attributeIndex];
        if (input.attributeType == MTLDataTypeInt4 && a.format == MTLVertexFormatUChar4) {
            a.format = MTLVertexFormatInt4;
            a.bufferIndex = 7;
            a.offset = 0;
            desc.vertexDescriptor.layouts[7].stride = 16;
        }
    }
    auto blend = bound.blend;
    if (blend) desc.colorAttachments[0] = [blend copy];
    if (a2c && samples > 1) {
        // FOLIAGE_AA 6.3: coverage comes from o0.w; the blend writes colour*1 and alpha 0 (retail's mov o0.w,0).
        auto c = desc.colorAttachments[0];
        c.blendingEnabled = YES;
        c.sourceRGBBlendFactor = MTLBlendFactorOne;
        c.destinationRGBBlendFactor = MTLBlendFactorZero;
        c.rgbBlendOperation = MTLBlendOperationAdd;
        c.sourceAlphaBlendFactor = MTLBlendFactorZero;
        c.destinationAlphaBlendFactor = MTLBlendFactorZero;
        c.alphaBlendOperation = MTLBlendOperationAdd;
        c.writeMask = MTLColorWriteMaskAll;
        desc.alphaToCoverageEnabled = YES;
    }
    desc.colorAttachments[0].pixelFormat = targetColor ? targetColor.pixelFormat : MTLPixelFormatInvalid;
    desc.depthAttachmentPixelFormat = targetDepth ? targetDepth.pixelFormat : MTLPixelFormatInvalid;
    desc.stencilAttachmentPixelFormat = desc.depthAttachmentPixelFormat;
    desc.rasterSampleCount = samples;
    auto state = CachedPipeline(desc, bound.vs, bound.ps, true);
    if (state) {
        pipelines.Insert(key, state, { vf, pf, layout, blendKey });
        if (pipelines.Size() <= 15) fprintf(stderr, "[Metal] pipeline %zu created\n", pipelines.Size());
    }
    return state;
}
// DXMT's SM50 tables contain GPU addresses/resource IDs, indexed in qwords by
// SM50GetArgumentsInfo. The public reflection's legacy 29/30 bind indices are
// stale: setup_binding_table2 uses SM50_BINDING_INDEX_* (0 and 1).
static void Resources(id<MTLRenderCommandEncoder> enc, Shader& shader, bool vertex) {
    MTLRenderStages stages = vertex ? MTLRenderStageVertex : MTLRenderStageFragment;
    if (!zeroConstants) zeroConstants = NewBuffer(65536, "zero-constant");
    std::vector<uint64_t> cb(std::max(1u, shader.reflection.NumConstantBuffers), 0);
    for (auto& a : shader.constants) {
        auto b = a.SM50BindingSlot < 31 ? bound.constants[vertex ? 1 : 0][a.SM50BindingSlot] : nil;
        if (!b) b = zeroConstants;
        if (a.StructurePtrOffset < cb.size())
            cb[a.StructurePtrOffset] =
                b.gpuAddress + (b == zeroConstants ? 0 : constantOffsets[vertex ? 1 : 0][a.SM50BindingSlot]);
        [enc useResource:b usage:MTLResourceUsageRead stages:stages];
    }
    if (shader.reflection.NumConstantBuffers) {
        if (vertex)
            [enc setVertexBytes:cb.data() length:cb.size() * 8 atIndex:SM50_BINDING_INDEX_CONSTANT_BUFFER];
        else
            [enc setFragmentBytes:cb.data() length:cb.size() * 8 atIndex:SM50_BINDING_INDEX_CONSTANT_BUFFER];
    }
    std::vector<uint64_t> args(std::max(1u, shader.reflection.ArgumentTableQwords), 0);
    for (auto& a : shader.arguments) {
        auto slot = a.SM50BindingSlot;
        auto off = a.StructurePtrOffset;
        if (slot >= 31 || off >= args.size()) continue;
        if (a.Type == SM50BindingType::Sampler) {
            auto sampler = bound.samplers[slot];
            args[off] = sampler.gpuResourceID._impl;
            if (off + 1 < args.size()) args[off + 1] = args[off];
        } else if (a.Type == SM50BindingType::SRV && (a.Flags & MTL_SM50_SHADER_ARGUMENT_TEXTURE)) {
            auto tex = bound.images[slot];
            if (tex) {
                args[off] = tex.gpuResourceID._impl;
                if (off + 1 < args.size()) args[off + 1] = uint64_t(tex.arrayLength) << 32;
                [enc useResource:tex usage:MTLResourceUsageRead stages:stages];
            }
        }
    }
    if (shader.reflection.NumArguments) {
        if (vertex)
            [enc setVertexBytes:args.data() length:args.size() * 8 atIndex:SM50_BINDING_INDEX_ARGUMENT_TABLE];
        else
            [enc setFragmentBytes:args.data() length:args.size() * 8 atIndex:SM50_BINDING_INDEX_ARGUMENT_TABLE];
    }
}
static void Draw(Video::Context* ctx, int primitive, int count, int instances, bool indexed, int start,
                 int baseVertex) {
    @autoreleasepool {
        if (!commands || count <= 0 || instances <= 0) {
            return;
        }
        auto targetSamples = targetColor ? targetColor.sampleCount : (targetDepth ? targetDepth.sampleCount : 1);
        TreePasses::BlendGuard treeBlend(
            bound.ps,
            bound.blend.blendingEnabled || (targetSamples <= 1 && TreePasses::IsCoverage(bound.ps)), bound.vs);
        auto pipeline = Pipeline();
        if (!pipeline) {
            return;
        }
        if (activeEncoder && (encoderColor != targetColor || encoderDepth != targetDepth)) {
            EndEncoder();
        }
        if (!activeEncoder) {
            auto pass = [MTLRenderPassDescriptor renderPassDescriptor];
            auto a = pass.colorAttachments[0];
            a.texture = targetColor;
            a.loadAction = MTLLoadActionLoad;
            a.storeAction = MTLStoreActionStore;
            if (targetDepth) {
                pass.depthAttachment.texture = targetDepth;
                pass.depthAttachment.loadAction = MTLLoadActionLoad;
                pass.depthAttachment.storeAction = MTLStoreActionStore;
                pass.stencilAttachment.texture = targetDepth;
                pass.stencilAttachment.loadAction = MTLLoadActionLoad;
                pass.stencilAttachment.storeAction = MTLStoreActionStore;
            }
            activeEncoder = [commands renderCommandEncoderWithDescriptor:pass];
            encoderColor = targetColor;
            encoderDepth = targetDepth;
        }
        auto enc = activeEncoder;
        // Snapshot the buffer bindings: a loader thread's rename may re-point bound.vertex/index at any time.
        id<MTLBuffer> vertexBuffers[31] = {};
        id<MTLBuffer> indexBuffer = nil;
        {
            StateGuard guard(stateMutex);
            for (unsigned i = 0; i < 31; ++i) {
                vertexBuffers[i] = bound.vertex[i];
                if (i < 23 && vertexBuffers[i]) {
                    UseBuffer(vertexBuffers[i]);   // recursive lock: the use is recorded with the snapshot
                }
            }
            indexBuffer = bound.index;
            if (indexed && indexBuffer) {
                UseBuffer(indexBuffer);
            }
        }
        if (activePipeline != pipeline) {
            [enc setRenderPipelineState:pipeline];
            activePipeline = pipeline;
        }
        [enc setDepthStencilState:bound.depth];
        [enc setCullMode:bound.cull];
        [enc setFrontFacingWinding:MTLWindingClockwise];
        [enc setTriangleFillMode:bound.fill];
        [enc setDepthBias:bound.bias slopeScale:bound.slopeBias clamp:0];
        [enc setStencilReferenceValue:bound.stencilReference];
        auto vp = ctx->_viewport;
        [enc setViewport:MTLViewport{ double(vp.x), double(vp.y), double(vp.width), double(vp.height), 0, 1 }];
        auto extent = targetColor ? targetColor : targetDepth;
        if (bound.scissor) {
            auto r = ctx->_scissor;
            NSUInteger x = std::clamp(r.x, 0, int(extent.width));
            NSUInteger y = std::clamp(r.y, 0, int(extent.height));
            NSUInteger right = std::clamp(r.x + std::max(0, r.width), int(x), int(extent.width));
            NSUInteger bottom = std::clamp(r.y + std::max(0, r.height), int(y), int(extent.height));
            [enc setScissorRect:MTLScissorRect{ x, y, right - x, bottom - y }];
        } else {
            [enc setScissorRect:MTLScissorRect{ 0, 0, extent.width, extent.height }];
        }
        for (unsigned i = 0; i < 23; ++i) {
            if (vertexBuffers[i]) {
                [enc setVertexBuffer:vertexBuffers[i] offset:bound.vertexOffsets[i] atIndex:i + 8];
            }
        }
        // DXBC int4 vertex inputs fed by a UChar4 layout are widened to Int4 into buffer slot 7.
        for (MTLVertexAttribute* input in bound.vs->function.vertexAttributes) {
            auto a = bound.layout.attributes[input.attributeIndex];
            if (input.attributeType != MTLDataTypeInt4 || a.format != MTLVertexFormatUChar4) {
                continue;
            }
            unsigned slot = unsigned(a.bufferIndex) - 8;
            auto source = vertexBuffers[slot];
            size_t stride = bound.strides[slot];
            if (!source || !stride) {
                continue;
            }
            size_t begin = bound.vertexOffsets[slot];
            if (begin >= source.length) {
                continue;   // an offset past the end has nothing to expand (and would underflow the count)
            }
            size_t n = (source.length - begin) / stride;
            std::string key;
            auto address = source.gpuAddress;
            Key(key, address);
            Key(key, begin);
            Key(key, stride);
            Key(key, a.offset);
            if (!bound.vertexDynamic[slot]) {
                id<MTLBuffer> hit = nil;
                {
                    StateGuard guard(stateMutex);
                    auto cached = expandedInputs.find(key);
                    if (cached != expandedInputs.end()) {
                        hit = cached->second.expanded;
                    }
                }
                if (hit) {
                    [enc setVertexBuffer:hit offset:0 atIndex:7];
                    continue;
                }
            }
            // A dynamic stream is re-expanded every draw and never cached: when it is small the bytes go into the
            // command stream (inline_bytes.h), not into a new MTLBuffer.
            size_t expandedBytes = std::max(size_t(16), n * 16);
            bool inlineBytes = bound.vertexDynamic[slot] && FitsInlineBytes(expandedBytes);
            int32_t inlineStorage[kInlineBytesLimit / sizeof(int32_t)];
            id<MTLBuffer> expanded = nil;
            int32_t* dst = inlineStorage;
            if (!inlineBytes) {
                expanded = NewBuffer(expandedBytes, "expanded-vertex");
                dst = static_cast<int32_t*>(expanded.contents);
            }
            memset(dst, 0, expandedBytes);
            auto src = static_cast<const uint8_t*>(source.contents) + begin;
            for (size_t v = 0; v < n; ++v) {
                for (unsigned k = 0; k < 4; ++k) {
                    dst[v * 4 + k] = src[v * stride + a.offset + k];
                }
            }
            if (inlineBytes) {
                SetVertexBytesIfSmall(enc, dst, expandedBytes, 7);
                continue;
            }
            if (!bound.vertexDynamic[slot]) {
                StateGuard guard(stateMutex);
                expandedInputs.emplace(key, ExpandedInput{ source, expanded });
            }
            [enc setVertexBuffer:expanded offset:0 atIndex:7];
        }
        Resources(enc, *bound.vs, true);
        if (bound.ps) {
            Resources(enc, *bound.ps, false);
        }
        MTLPrimitiveType type = primitive == 2 ? MTLPrimitiveTypeLine : MTLPrimitiveTypeTriangle;
        if (indexed) {
            NSUInteger offset = NSUInteger(ctx->_indexOffset) + NSUInteger(start) * 2;
            if (indexBuffer && offset + NSUInteger(count) * 2 <= indexBuffer.length) {
                [enc drawIndexedPrimitives:type
                                indexCount:count
                                 indexType:MTLIndexTypeUInt16
                               indexBuffer:indexBuffer
                         indexBufferOffset:offset
                             instanceCount:instances
                                baseVertex:baseVertex
                              baseInstance:0];
            }
        } else {
            [enc drawPrimitives:type vertexStart:start vertexCount:count instanceCount:instances];
        }
        if (!targetColor && targetDepth) {
            ++shadowDraws;
        }
        ++totalDraws;
        if (indexed) {
            ++indexedDraws;
        }
        if (instances > 1) {
            ++instancedDraws;
        }
    }
}
static unsigned clearColor = 0xff202830;
static id<MTLTexture> NewTexture(int w, int h, MTLPixelFormat f, MTLTextureUsage usage) {
    auto d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:f
                                                                width:std::max(1, w)
                                                               height:std::max(1, h)
                                                            mipmapped:NO];
    d.usage = usage;
    d.storageMode = MTLStorageModeShared;
    return NewTextureResource(d);
}
static unsigned msaaSamples = 1;
static unsigned SupportedSamples(unsigned want) {
    for (unsigned n : { 8u, 4u, 2u })
        if (n <= want && [gpu supportsTextureSampleCount:n]) return n;
    return 1;
}
// VideoMultisample is an INDEX in the retail option list: sample count = index + 1
// (0 none, 1 = 2x, 3 = 4x, 7 = 8x), exactly as the DX11 backend's ResolveMultisampleMode.
static unsigned DesiredSamples() {
    auto cfg = Video::Configuration::_singleton;
    int index = cfg ? int(cfg->multisample) : 0;
    return SupportedSamples(unsigned(std::clamp(index, 0, 31) + 1));
}
static id<MTLTexture> NewMainColor(int w, int h, unsigned samples) {
    if (samples <= 1)
        return NewTexture(w, h, MTLPixelFormatBGRA8Unorm, MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead);
    auto d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                                width:std::max(1, w)
                                                               height:std::max(1, h)
                                                            mipmapped:NO];
    d.textureType = MTLTextureType2DMultisample;
    d.sampleCount = samples;
    d.storageMode = MTLStorageModePrivate;
    d.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
    return NewTextureResource(d);
}
// Depth format: D32F+S8, retail is D24S8 (DXGI_FORMAT_D24_UNORM_S8_UINT). Apple silicon has no 24-bit depth format
// (MTLPixelFormatDepth24Unorm_Stencil8 is Intel/AMD only), so the depth bias behaves differently; the accepted
// target is "visually identical to the best Metal can do". Recorded in docs/known_port_issues.md ("Depth precision").
// The shadow-map and render-buffer depth images (ImageBufferData::Create, format 7) use the same format.
static id<MTLTexture> NewMainDepth(int w, int h, unsigned samples) {
    auto d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float_Stencil8
                                                                width:std::max(1, w)
                                                               height:std::max(1, h)
                                                            mipmapped:NO];
    if (samples > 1) {
        d.textureType = MTLTextureType2DMultisample;
        d.sampleCount = samples;
    }
    d.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
    d.storageMode = MTLStorageModePrivate;
    return NewTextureResource(d);
}
// The engine's default render buffer owns the main targets; re-point it on a sample-count change
// (Options Apply). In-flight command buffers retain the old textures.
static void RebuildMainTargets(Video::Context* context, unsigned samples, int newW = 0, int newH = 0) {
    if (!mainColor) return;
    int w = newW > 0 ? newW : int(mainColor.width), h = newH > 0 ? newH : int(mainColor.height);
    EndEncoder();
    msaaSamples = samples;
    mainColor = NewMainColor(w, h, samples);
    mainDepth = NewMainDepth(w, h, samples);
    // FrameCopyImageBuffer (water, screenshots, thumbnails) owns another alias;
    // updating just the default RenderBuffer leaves it reading the old frame.
    {
        StateGuard guard(stateMutex);
        for (auto image : colorAliases)
            image->texture = mainColor;
        for (auto image : depthAliases)
            image->texture = mainDepth;
    }
    if (auto rb = context->_defaultRenderBuffer) {
        if (rb->colorBuffer && rb->colorBuffer->_data)
            static_cast<ImageBufferData*>(rb->colorBuffer->_data)->texture = mainColor;
        if (rb->depthBuffer && rb->depthBuffer->_data)
            static_cast<ImageBufferData*>(rb->depthBuffer->_data)->texture = mainDepth;
        if (newW > 0) {   // a size change: the buffers' own recorded size must follow (RenderBuffer::Width reads it)
            if (rb->colorBuffer) {
                auto b = const_cast<Video::ImageBuffer*>(rb->colorBuffer);
                b->_width = w;
                b->_height = h;
            }
            if (rb->depthBuffer) {
                auto b = const_cast<Video::ImageBuffer*>(rb->depthBuffer);
                b->_width = w;
                b->_height = h;
            }
        }
    }
    fprintf(stderr, "[Metal] multisample %ux (%dx%d)\n", samples, w, h);
}
void Present() {
    @autoreleasepool {
        if (!commands) {
            return;
        }
        EndEncoder();
        {
            double drawableT0 = StatNow();
            drawable = [layer nextDrawable];
            statDrawable += StatNow() - drawableT0;
        }
        auto semaphore = inFlight;
        if (!drawable) {
            [commands addCompletedHandler:^(id<MTLCommandBuffer>) {
              dispatch_semaphore_signal(semaphore);
            }];
            [commands commit];
            commands = nil;
            return;
        }
        auto pass = [MTLRenderPassDescriptor renderPassDescriptor];
        pass.colorAttachments[0].texture = drawable.texture;
        pass.colorAttachments[0].loadAction = MTLLoadActionClear;
        pass.colorAttachments[0].storeAction = MTLStoreActionStore;
        pass.colorAttachments[0].clearColor =
            MTLClearColorMake(((clearColor >> 16) & 255) / 255., ((clearColor >> 8) & 255) / 255.,
                              (clearColor & 255) / 255., ((clearColor >> 24) & 255) / 255.);
        static bool info = false;
        if (!info) {
            info = true;
            fprintf(stderr, "[Metal] present color=%lux%lu x%lu drawable=%lux%lu\n", (unsigned long)mainColor.width,
                    (unsigned long)mainColor.height, (unsigned long)mainColor.sampleCount,
                    (unsigned long)drawable.texture.width, (unsigned long)drawable.texture.height);
        }
        bool sameSize =
            mainColor && mainColor.width == drawable.texture.width && mainColor.height == drawable.texture.height;
        if (sameSize && mainColor.sampleCount > 1) {
            auto resolve = [MTLRenderPassDescriptor renderPassDescriptor];
            auto a = resolve.colorAttachments[0];
            a.texture = mainColor;
            a.resolveTexture = drawable.texture;
            a.loadAction = MTLLoadActionLoad;
            a.storeAction = MTLStoreActionMultisampleResolve;
            [[commands renderCommandEncoderWithDescriptor:resolve] endEncoding];
        } else if (sameSize) {
            auto blit = [commands blitCommandEncoder];
            [blit copyFromTexture:mainColor
                      sourceSlice:0
                      sourceLevel:0
                     sourceOrigin:MTLOriginMake(0, 0, 0)
                       sourceSize:MTLSizeMake(mainColor.width, mainColor.height, 1)
                        toTexture:drawable.texture
                 destinationSlice:0
                 destinationLevel:0
                destinationOrigin:MTLOriginMake(0, 0, 0)];
            [blit endEncoding];
        } else {
            auto enc = [commands renderCommandEncoderWithDescriptor:pass];
            [enc endEncoding];
        }
        [commands presentDrawable:drawable];
        [commands addCompletedHandler:^(id<MTLCommandBuffer> cb) {
          if (cb.error) {
              fprintf(stderr, "[Metal] command error: %s\n", cb.error.localizedDescription.UTF8String);
          }
          dispatch_semaphore_signal(semaphore);
        }];
        [commands commit];   // the error, if any, is reported by the completion handler above (it is nil this early)
        static unsigned frames = 0;
        if (Opt().metalStats) {
            using Clock = std::chrono::steady_clock;
            static auto first = Clock::now();
            static auto last = first;
            auto now = Clock::now();
            static FILE* timing = std::fopen("standalone_metal_frames.csv", "w");
            if (timing) {
                if (!frames) {
                    std::fprintf(timing,
                                 "frame,seconds,frame_ms,sem_ms,drawable_ms,pipeline_ms,new_pipelines,newbuf_ms\n");
                }
                std::fprintf(timing, "%u,%.6f,%.6f,%.4f,%.4f,%.4f,%u,%.4f\n", frames,
                             std::chrono::duration<double>(now - first).count(),
                             std::chrono::duration<double, std::milli>(now - last).count(), statSem, statDrawable,
                             statPipeline, statPipelinesNew, statBuffer);
                std::fflush(timing);
            }
            last = now;
            statSem = statDrawable = statPipeline = statBuffer = 0;
            statPipelinesNew = 0;
        }
        ++frames;
        if (Opt().metalStats && (frames == 1 || frames % 300 == 0)) {
            fprintf(stderr, "[Metal] frame=%u status=%lu pipelines=%zu draws=%llu indexed=%llu instanced=%llu\n",
                    frames, (unsigned long)commands.status, pipelines.Size(), (unsigned long long)totalDraws,
                    (unsigned long long)indexedDraws, (unsigned long long)instancedDraws);
        }
        commands = nil;
        drawable = nil;
    }
}
bool DeviceLive() { return gpu != nil; }
id<MTLDevice> LiveDevice() { return gpu; }
unsigned CurrentSamples() { return msaaSamples; }
bool DepthReadable() {
    return mainDepth != nil && (mainDepth.usage & MTLTextureUsageShaderRead);
}
static int g_lastFullscreen = -1;
bool Enumerate(Video::Device* dev, const char* name) {
    using D = Video::Device;
    using A = System::MemoryAllocator;
    auto old = dev->interfaceData;
    int n = dev->interfaceCount;
    auto list = static_cast<D::GraphicsInterface*>(A::MemAlloc(sizeof(D::GraphicsInterface) * (n + 1), 4));
    for (int i = 0; i < n; ++i) {
        new (list + i) D::GraphicsInterface(old[i]);
        old[i].~GraphicsInterface();
    }
    A::MemFree(old);
    new (list + n) D::GraphicsInterface();
    auto& g = list[n];
    g.name.Set(name);
    g.adapterData = static_cast<D::Adapter*>(A::MemAlloc(sizeof(D::Adapter), 4));
    new (g.adapterData) D::Adapter();
    g.adapterCount = g.adapterCapacity = 1;
    auto& a = *g.adapterData;
    a.name.Set("Apple Metal");
    a.dxgiAdapterIndex = a.dxgiOutputIndex = 0;
    a.maxFilterLevel = 4;
    a.multisampleData = static_cast<uint8_t*>(A::MemAlloc(8, 4));
    memset(a.multisampleData, 0, 8);
    a.multisampleData[0] = 1;
    a.multisampleCount = a.multisampleCap = 8;
    if (auto probe = MTLCreateSystemDefaultDevice()) {
        for (unsigned samples : { 2u, 4u, 8u }) {
            if ([probe supportsTextureSampleCount:samples]) {
                a.multisampleData[samples - 1] = 1;
            }
        }
    }
    D::RefreshRate rate{ 60, 1 };
    int mw = 0;
    int mh = 0;
    int nw = 0;
    int nh = 0;
    Platform::Window::QueryScreenForModes(&mw, &mh, &nw, &nh);
    int32_t sizes[32][2];
    // Engine PIXELS, filtered to the usable screen.
    int count = Platform::Window::BuildResolutionList(mw, mh, nw, nh, sizes, 32);
    for (int i = 0; i < count; ++i) {
        D::DisplayMode mode{};
        mode.width = sizes[i][0];
        mode.height = sizes[i][1];
        a.FindOrAddMode(mode, rate);
    }
    dev->interfaceData = list;
    dev->interfaceCount = dev->interfaceCapacity = n + 1;
    return true;
}

bool DeviceData::EnumerateInterfaces() {
    return Enumerate(_owner, "Metal");
}
// SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED (the window moved to a display of another density, or the display's scale
// changed): the back buffer's pixel size changes while the window's point size does not. Raise settingsChanged so
// BeginCommands re-reads the window's pixel size and rebuilds the drawable and every dynamic target
// (ApplyEngineSettingsChange). Runs where SDL delivers the event (the thread that pumps events) and only writes the
// flag.
static SDL_Window* watchedWindow = nullptr;
static bool SDLCALL OnWindowEvent(void* userdata, SDL_Event* event) {
    if (event->type != SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED) {
        return true;
    }
    if (SDL_GetWindowFromID(event->window.windowID) != static_cast<SDL_Window*>(userdata)) {
        return true;
    }
    if (auto dev = Video::Device::_singleton) {
        dev->settingsChanged = 1;
    }
    return true;
}
static bool FailCreateDevice(const char* message) {
    System::Error::SetText(message, true);
    System::Error::Display();
    return false;
}
bool DeviceData::CreateDevice() {
    @autoreleasepool {
        gpu = MTLCreateSystemDefaultDevice();
        if (!gpu) {
            return FailCreateDevice("No Metal device was found on this Mac. OpenBA's Metal renderer needs a GPU "
                                    "that supports Metal 3.");
        }
        std::string unsupported;
        if (!CheckDeviceSupport(gpu, &unsupported)) {
            gpu = nil;
            return FailCreateDevice(unsupported.c_str());
        }
        queue = [gpu newCommandQueue];
        inFlight = dispatch_semaphore_create(kFramesInFlight);
        auto w = static_cast<SDL_Window*>(_owner->windowHandle);
        if (!w) {
            return false;
        }
        if (Opt().background) {
            _owner->backBufferWidth = 640;
            _owner->backBufferHeight = 360;
        }
        auto* cfg = Video::Configuration::_singleton;
        if (cfg && !cfg->fullscreen && !Opt().background) {
            // docs/reference/macos_input_window.md "Opening size"
            Platform::Window::ApplyInitialWindowGeometry(w, cfg->windowX, cfg->windowY, cfg->windowWidth,
                                                         cfg->windowHeight, cfg->width, cfg->height);
        } else if (_owner->backBufferWidth <= 1 || _owner->backBufferHeight <= 1) {
            // A saved fullscreen with no usable size opened a 1x1 window.
            Platform::Window::ApplyInitialWindowGeometry(w, -1, -1, -1, -1, cfg ? cfg->width : 0,
                                                         cfg ? cfg->height : 0);
        } else {
            SDL_SetWindowSize(w, std::max(1, _owner->backBufferWidth), std::max(1, _owner->backBufferHeight));
        }
        SDL_SyncWindow(w);
        SDL_GetWindowSizeInPixels(w, &_owner->backBufferWidth, &_owner->backBufferHeight);
        if (cfg) {
            g_lastFullscreen = cfg->fullscreen ? 1 : 0;
            // The window opened at a pixel size the Options list may not show (first run: VideoWidth is read as
            // POINTS): when it is a listed mode, make the configuration say so, so the dialog and the engine agree.
            if (!cfg->fullscreen && !Opt().background) {
                for (int i = 0; i < _owner->modeCount; ++i) {
                    auto m = _owner->modeData + i;
                    if (m->width == _owner->backBufferWidth && m->height == _owner->backBufferHeight) {
                        cfg->width = m->width;
                        cfg->height = m->height;
                        cfg->windowWidth = m->width;
                        cfg->windowHeight = m->height;
                        _owner->displayModeIndex = i;
                        break;
                    }
                }
            }
        }
        // The pixel density is not cached here: Platform::Window::PixelDensity reads it live. A change of display scale
        // arrives as an event and raises settingsChanged (OnWindowEvent).
        view = SDL_Metal_CreateView(w);
        if (!view) {
            fprintf(stderr, "[Metal] view failed: %s\n", SDL_GetError());
            return false;
        }
        if (!watchedWindow) {
            SDL_AddEventWatch(OnWindowEvent, w);
            watchedWindow = w;
        }
        // A small unobtrusive/occluded window lets macOS App Nap + memory compression freeze the whole process for
        // seconds (soak 2026-10-19: RSS fell to 7 MB at 0 % CPU during the 8.6 s "hitch"); a game is user-initiated,
        // latency-critical work.
        static id appActivity = [[NSProcessInfo processInfo]
            beginActivityWithOptions:(NSActivityUserInitiatedAllowingIdleSystemSleep | NSActivityLatencyCritical)
                              reason:@"OpenBA real-time rendering"];
        (void)appActivity;
        layer = (__bridge CAMetalLayer*)SDL_Metal_GetLayer(view);
        layer.device = gpu;
        layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
        layer.framebufferOnly = NO;
        layer.displaySyncEnabled = !Opt().stress;
        SDL_ShowWindow(w);
        Platform::Window::ConfigureBackgroundWindow(w);
        _owner->settingsChanged = 0;
        fprintf(stderr, "[Metal] device %s created\n", gpu.name.UTF8String);
        // NOT RETAIL (ext.create_on_demand): objects waiting for their first use are made on this device.
        CreateOnDemand::Resume();
        return true;
    }
}
void DeviceData::Shutdown() {
    // NOT RETAIL (ext.create_on_demand): no first-use create runs against the device going away (the rest
    // wait for the next device).
    CreateOnDemand::Pause();
    if (watchedWindow) {
        SDL_RemoveEventWatch(OnWindowEvent, watchedWindow);
        watchedWindow = nullptr;
    }
    EndEncoder();
    FlushPipelineRecords();   // pipelines.txt is written once, here, not per new pipeline on the render thread
    bound = Bindings{};
    pipelines.Clear();
    warmPipelines.clear();
    pipelineRecords = nil;
    pipelineFunctions = nil;
    {
        StateGuard guard(stateMutex);
        expandedInputs.clear();
        bufferUses.clear();
        retiredBuffers.Clear();
    }
    // NOT RETAIL (ext.share_gpu_data): nothing made on this device is handed out again; the table's
    // references go (exit and a device rebuild come through here). Users keep their objects.
    ShareGpuData::Clear();
    zeroConstants = nil;
    if (inFlight) {
        for (unsigned i = 0; i < kFramesInFlight; ++i) {
            dispatch_semaphore_wait(inFlight, DISPATCH_TIME_FOREVER);
        }
        // libdispatch requires the semaphore's original count at destruction.
        for (unsigned i = 0; i < kFramesInFlight; ++i) {
            dispatch_semaphore_signal(inFlight);
        }
        inFlight = nil;
    }
    for (auto& a : uploads) {
        a = UploadArena{};
    }
    commands = nil;
    drawable = nil;
    mainDepth = nil;
    mainColor = nil;
    targetColor = nil;
    targetDepth = nil;
    layer = nil;
    if (view) {
        SDL_Metal_DestroyView(view);
    }
    view = nullptr;
    queue = nil;
    gpu = nil;
}
// NOT RETAIL (ext.compress_ao, ext.share_gpu_data): the loader bracket's close prints the load's BC4
// summary and asks the derived cache to persist its keep list and release its mapping, then the run's
// sharing summary and the deferred creates' summary (ext.create_on_demand) -- DX11's Slot4(false)
// (devicedata_stubs.cpp). Retail's body is empty here.
void DeviceData::Slot4(bool loading) {
    if (!loading) {
        ::CompressAo::LoadEnd();
        ShareGpuData::LoadEnd();
        CreateOnDemand::LoadEnd();
    }
}
float DeviceData::Slot5() { return 0; }
bool DeviceData::Slot6() {
    return false;
}
bool DeviceData::Slot7() { return true; }
void DeviceData::SetImageBuffer(Video::ImageBuffer* imageBuffer, int hotX, int hotY) {
    // Retail slot 8 (devicedata.cpp): the game cursor from a 32x32 ImageBuffer, rebuilt only when
    // (image, hotspot) change. Without this the main-menu/game cursor was never set (macOS: invisible).
    static Video::ImageBuffer* shownImage = nullptr;
    static int shownX = 0, shownY = 0;
    if (imageBuffer == shownImage && hotX == shownX && hotY == shownY) return;
    if (!imageBuffer) return;
    uint32_t texels[32 * 32];
    _owner->Lock();
    bool ok = imageBuffer->CopyFromTexture(texels, 32, 32);
    _owner->Unlock();
    if (!ok) {
        fprintf(stderr, "[Metal] cursor image not readable\n");
        return;
    }
    Platform::Cursor::SetFromTexels(texels, hotX, hotY);
    shownImage = imageBuffer;
    shownX = hotX;
    shownY = hotY;
}
Video::ContextData* DeviceData::CreateContextData(Video::Context* owner) {
    return new ContextData(owner);
}
Video::ImageBufferData* DeviceData::CreateImageBufferData(Video::ImageBuffer* owner) {
    return new ImageBufferData(owner);
}
Video::ConstantBufferData* DeviceData::CreateConstantBufferData(Video::ConstantBuffer* owner) {
    return new ConstantBufferData(owner);
}
Video::IndexBufferData* DeviceData::CreateIndexBufferData(Video::IndexBuffer* owner) {
    return new IndexBufferData(owner);
}
Video::VertexBufferData* DeviceData::CreateVertexBufferData(Video::VertexBuffer* owner) {
    return new VertexBufferData(owner);
}
Video::VertexLayoutData* DeviceData::CreateVertexLayoutData(Video::VertexLayout* owner) {
    return new VertexLayoutData(owner);
}
Video::VertexProgramData* DeviceData::CreateVertexProgramData(Video::VertexProgram* owner) {
    return new VertexProgramData(owner);
}
Video::PixelProgramData* DeviceData::CreatePixelProgramData(Video::PixelProgram* owner) {
    return new PixelProgramData(owner);
}
Video::RenderStateData* DeviceData::CreateRenderStateData(Video::RenderState* owner) {
    return new RenderStateData(owner);
}
Video::VideoProgramData* DeviceData::CreateVideoProgramData(Video::VideoProgram* owner) {
    return new VideoProgramData(owner);
}
Video::RenderBufferData* DeviceData::CreateRenderBufferData(Video::RenderBuffer* owner) {
    return new RenderBufferData(owner);
}
// Options Apply (resolution / fullscreen) and a user drag-resize raise settingsChanged. The DX11 backend answers with
// Dx11_DeviceData_Resize: Device::AdjustDisplay sizes the real window to the engine's back buffer (Platform::Window
// units
// are converted to points there), then the buffers are rebuilt. Metal did nothing, so the engine believed in a client
// area
// the window never had (mouse mapping off). Here: size the window if the engine's size differs from it or fullscreen
// flipped, publish the REAL pixel size as the back buffer, then rebuild the drawable and all dynamic targets (including
// the shadow map).
static void ApplyEngineSettingsChange(Video::Context* ctx, Video::Device* dev) {
    auto w = static_cast<SDL_Window*>(dev->windowHandle);
    if (!w || (SDL_GetWindowFlags(w) & SDL_WINDOW_MINIMIZED)) return;
    auto cfg = Video::Configuration::_singleton;
    int fs = cfg && cfg->fullscreen ? 1 : 0;
    int cw = 0, ch = 0;
    SDL_GetWindowSizeInPixels(w, &cw, &ch);
    bool fsFlip = g_lastFullscreen >= 0 && g_lastFullscreen != fs;
    g_lastFullscreen = fs;
    if (fsFlip || dev->backBufferWidth != cw || dev->backBufferHeight != ch) {
        int wantW = dev->backBufferWidth, wantH = dev->backBufferHeight;
        dev->AdjustDisplay();
        SDL_SyncWindow(w);
        SDL_GetWindowSizeInPixels(w, &cw, &ch);
        dev->backBufferWidth = cw;
        dev->backBufferHeight = ch;
        if (cfg && !fs) {
            cfg->windowWidth = cw;
            cfg->windowHeight = ch;
        }
        fprintf(stderr, "[Metal] resize: wanted %dx%d px fullscreen=%d -> window %dx%d px\n", wantW, wantH, fs, cw, ch);
    }
    if (cw > 0 && ch > 0 && mainColor) {
        // Apply also changes shadowScalar without changing the window. Recreate
        // every dynamic image/render buffer so textures and cached extents agree.
        EndEncoder();
        dev->ReleaseDynamic();
        mainColor = nil;
        mainDepth = nil;
        dev->RecreateDynamic();
        if (!mainColor) mainColor = NewMainColor(cw, ch, DesiredSamples());
        // Repoint the default render buffer, its aliases and the AO depth.
        RebuildMainTargets(ctx, unsigned(mainColor.sampleCount), cw, ch);
        fprintf(stderr, "[Metal] back buffer rebuilt %dx%d\n", cw, ch);
    }
}
bool ContextData::BeginCommands() {
    @autoreleasepool {
        if (!queue || !layer) {
            return false;
        }
        auto dev = Video::Device::_singleton;
        if (dev->settingsChanged) {
            // Ends the encoder and rebuilds the dynamic targets: shared state, so under the device lock (taken
            // here and dropped before the in-flight semaphore wait; no GPU wait happens inside).
            dev->Lock();
            ApplyEngineSettingsChange(_owner, dev);
            dev->Unlock();
        }
        int w = 0;
        int h = 0;
        SDL_GetWindowSizeInPixels(static_cast<SDL_Window*>(dev->windowHandle), &w, &h);
        if (w <= 0 || h <= 0) {
            return false;
        }
        CGSize wantSize = CGSizeMake(w, h);
        CGSize have = layer.drawableSize;
        if (have.width != wantSize.width || have.height != wantSize.height) {
            layer.drawableSize = wantSize;
        }
        dev->settingsChanged = 0;
        double semT0 = StatNow();
        dispatch_semaphore_wait(inFlight, DISPATCH_TIME_FOREVER);
        statSem += StatNow() - semT0;
        // The device lock is taken after the semaphore wait and BEFORE the upload-arena reset, the saved-constant
        // rebinding and the frame's command buffer: that state (and `commands`, the encoder state and the completion
        // ticket) is shared with ImageBufferData::Read on the main thread, which holds this same lock for its whole
        // body. Finish releases it after Present.
        dev->Lock();
        {
            StateGuard guard(stateMutex);
            for (auto it = bufferUses.begin(); it != bufferUses.end();) {
                if (it->second.completion->load(std::memory_order_acquire)) {
                    it = bufferUses.erase(it);
                } else {
                    ++it;
                }
            }
            TrimRetiredBuffers();
        }
        frameSlot = unsigned(frameGeneration.fetch_add(1) % kFramesInFlight);
        // Preserve bindings that the engine leaves clean across frames before arena reuse.
        std::vector<uint8_t> saved[2][31];
        for (int stage = 0; stage < 2; ++stage) {
            for (int i = 0; i < 31; ++i) {
                if (bound.constants[stage][i] && constantSizes[stage][i]) {
                    auto src =
                        static_cast<const uint8_t*>(bound.constants[stage][i].contents) + constantOffsets[stage][i];
                    saved[stage][i].assign(src, src + constantSizes[stage][i]);
                }
            }
        }
        uploads[frameSlot].page = uploads[frameSlot].cursor = 0;
        for (int stage = 0; stage < 2; ++stage) {
            for (int i = 0; i < 31; ++i) {
                if (!saved[stage][i].empty()) {
                    bound.constants[stage][i] =
                        Upload(saved[stage][i].data(), saved[stage][i].size(), constantOffsets[stage][i]);
                }
            }
        }
        if (Opt().metalStats && frameGeneration % 120 == 0) {
            ReportBuffers();
            StateGuard guard(stateMutex);
            size_t pages = 0;
            size_t bytes = 0;
            size_t expanded = 0;
            size_t retired = 0;
            for (auto& a : uploads) {
                for (auto b : a.pages) {
                    ++pages;
                    bytes += b.length;
                }
            }
            for (auto& e : expandedInputs) {
                expanded += e.second.expanded.length;
            }
            for (auto& r : retiredBuffers.Entries()) {
                retired += r.buffer.length;
            }
            fprintf(stderr,
                    "[MetalMemory] renames=%llu recycled=%llu copied_or_discarded_bytes=%llu inplace=%llu tracked=%zu "
                    "retired=%zu retired_bytes=%zu upload_pages=%zu upload_bytes=%zu expanded_count=%zu "
                    "expanded_bytes=%zu device_bytes=%llu\n",
                    (unsigned long long)renameCount, (unsigned long long)recycledBuffers,
                    (unsigned long long)renameBytes, (unsigned long long)inPlaceWrites, bufferUses.size(),
                    retiredBuffers.Size(), retired, pages, bytes, expandedInputs.size(), expanded,
                    (unsigned long long)gpu.currentAllocatedSize);
        }
        if (Opt().metalStats && frameGeneration % 120 == 0) {
            fprintf(stderr, "[MetalShadow] frame=%llu depth-only draws=%llu\n", (unsigned long long)frameGeneration,
                    (unsigned long long)shadowDraws);
        }
        shadowDraws = 0;
        TreePasses::BeginFrame();
        // The drawable is acquired lazily in Present(): nextDrawable blocks on the compositor, and acquiring it here
        // stalled the whole frame's CPU encoding behind it (every p99 frame in the 2026-10-19 soak was a 15-20 ms
        // nextDrawable wait).
        drawable = nil;
        commands = NewCommands();
        if (!commands) {
            dispatch_semaphore_signal(inFlight);
            dev->Unlock();
            return false;
        }
        if (unsigned want = DesiredSamples(); want != msaaSamples) {
            RebuildMainTargets(_owner, want);
        }
        _owner->_trianglesSubmitted = _owner->_verticesSubmitted = _owner->_drawsSubmitted = _owner->_objectsSubmitted =
            0;
        _owner->SetRenderBuffer(_owner->_defaultRenderBuffer);
        Video::Viewport vp{};
        vp.width = dev->backBufferWidth;
        vp.height = dev->backBufferHeight;
        _owner->SetViewport(vp);
        return true;
    }
}
void ContextData::EndCommands() {}
void ContextData::Finish() {
    Present();
    _owner->_device->Unlock();
}
void ContextData::Clear(unsigned flags, unsigned color, float depth, int stencil) {
    if (flags & 1) clearColor = color;
    BindRenderBuffer();
    if (!commands) return;
    EndEncoder();
    auto pass = [MTLRenderPassDescriptor renderPassDescriptor];
    if (targetColor) {
        auto a = pass.colorAttachments[0];
        a.texture = targetColor;
        a.loadAction = (flags & 1) ? MTLLoadActionClear : MTLLoadActionLoad;
        a.storeAction = MTLStoreActionStore;
        a.clearColor = MTLClearColorMake(((color >> 16) & 255) / 255., ((color >> 8) & 255) / 255.,
                                         (color & 255) / 255., (color >> 24) / 255.);
    }
    if (targetDepth) {
        pass.depthAttachment.texture = targetDepth;
        pass.depthAttachment.loadAction = (flags & 2) ? MTLLoadActionClear : MTLLoadActionLoad;
        pass.depthAttachment.storeAction = MTLStoreActionStore;
        pass.depthAttachment.clearDepth = depth;
        pass.stencilAttachment.texture = targetDepth;
        pass.stencilAttachment.loadAction = (flags & 4) ? MTLLoadActionClear : MTLLoadActionLoad;
        pass.stencilAttachment.storeAction = MTLStoreActionStore;
        pass.stencilAttachment.clearStencil = stencil;
    }
    if (targetColor || targetDepth) {
        auto enc = [commands renderCommandEncoderWithDescriptor:pass];
        [enc endEncoding];
    }
}
void ContextData::BindRenderBuffer() {
    if (auto rb = _owner->_renderBuffer) {
        targetColor = ImageTexture(rb->colorBuffer);
        targetDepth = ImageTexture(rb->depthBuffer);
    }
}
// Viewport and scissor are read from the owner when each encoder is created.
void ContextData::BindViewport() {}
void ContextData::BindScissor() {}
void ContextData::BindTransforms() {
    auto c = _owner;
    if (c->_dirty & 0x4000) c->_transformConstants.Bind(*c, c->_transforms, 0, c->_transformCount << 6);
    if (c->_unknown0024 > 0) c->_boneConstants.Bind(*c, c->_boneAndProjectionStaging, 0, 0x0cc0);
}
void ContextData::BindTextureNone(int slot) {
    if (slot >= 0 && slot < 31) {
        bound.images[slot] = nil;
        bound.samplers[slot] = nil;
    }
}
void ContextData::BindIndexNone() {
    StateGuard guard(stateMutex);
    bound.index = nil;
}
void ContextData::BindVertexNone(int slot) {
    if (slot >= 0 && slot < 31) {
        StateGuard guard(stateMutex);
        bound.vertex[slot] = nil;
    }
}
void ContextData::DrawIndexed(int primitiveType, int baseVertex, int vertexCount, int startIndex,
                              int indexCount, int instanceCount) {
    _owner->BindResources();
    Draw(_owner, primitiveType, indexCount, instanceCount, true, startIndex, baseVertex);
    _owner->_trianglesSubmitted += (indexCount / 3) * instanceCount;
    _owner->_verticesSubmitted += vertexCount * instanceCount;
    _owner->_drawsSubmitted++;
    _owner->_objectsSubmitted += instanceCount;
}
int ContextData::GetMaxDynamicInstances(int a, int b, int c) {
    return 52;
}
bool ContextData::BeginVertices(int vertexCount, int vertexStride) {
    if (vertexCount < 0 || vertexStride <= 0) return false;
    auto c = _owner;
    c->_dynamicVertexStride = vertexStride;
    c->_unknown2104 = vertexCount;
    c->_dynamicVertexCursor =
        static_cast<uint8_t*>(c->_dynamicVertexBuffer->Lock(vertexCount * vertexStride, c->_unknown2100));
    c->_unknown2108 = c->_dynamicVertexCursor;
    return c->_dynamicVertexCursor != nullptr;
}
void ContextData::EndVertices(int primitiveType) {
    auto c = _owner;
    c->_dynamicVertexCursor = nullptr;
    c->_dynamicVertexBuffer->Unlock();
    c->SetVertexBuffer(c->_dynamicVertexBuffer, 0, c->_unknown2100, c->_dynamicVertexStride);
    c->BindResources();
    Draw(c, primitiveType, c->_unknown2104, 1, false, 0, 0);
    c->_trianglesSubmitted += c->_unknown2104 / 3;
    c->_verticesSubmitted += c->_unknown2104;
    c->_drawsSubmitted++;
    c->_objectsSubmitted++;
}
bool ContextData::BeginIndexedVertices(int vertexCount, int vertexStride, int indexCount) {
    return BeginInstancedIndexedVertices(vertexCount, vertexStride, indexCount, 1);
}
void ContextData::EndIndexedVertices(int primitiveType) {
    EndInstancedIndexedVertices(primitiveType);
}
bool ContextData::BeginInstancedIndexedVertices(int vertexCount, int vertexStride, int indexCount, int instanceCount) {
    if (!BeginVertices(vertexCount, vertexStride) || indexCount < 0) return false;
    auto c = _owner;
    c->_instanceCount = instanceCount;
    c->_unknown2124 = indexCount;
    c->_dynamicIndexCursor = static_cast<uint16_t*>(c->_dynamicIndexBuffer->Lock(indexCount * 2, c->_unknown2120));
    c->_unknown2128 = c->_dynamicIndexCursor;
    return c->_dynamicIndexCursor != nullptr;
}
void ContextData::EndInstancedIndexedVertices(int primitiveType) {
    auto c = _owner;
    c->_dynamicIndexCursor = nullptr;
    c->_dynamicIndexBuffer->Unlock();
    c->_dynamicVertexCursor = nullptr;
    c->_dynamicVertexBuffer->Unlock();
    c->SetVertexBuffer(c->_dynamicVertexBuffer, 0, c->_unknown2100, c->_dynamicVertexStride);
    if (c->_indexBuffer != c->_dynamicIndexBuffer || c->_indexOffset != c->_unknown2120) {
        c->_dirty |= 0x10;
        c->_indexBuffer = c->_dynamicIndexBuffer;
        c->_indexOffset = c->_unknown2120;
    }
    DrawIndexed(primitiveType, 0, c->_unknown2104, 0, c->_unknown2124, c->_instanceCount);
}
ImageBufferData::~ImageBufferData() {
    CreateOnDemand::Forget(this);   // NOT RETAIL (ext.create_on_demand): no first-use create after this
    if (shared) {
        ShareGpuData::DropTexture(texture);   // NOT RETAIL (ext.share_gpu_data): one user less
    }
    StateGuard guard(stateMutex);
    colorAliases.erase(this);
    depthAliases.erase(this);
}
void ImageBufferData::AttachColorBuffer() {
    CreateOnDemand::Forget(this);   // NOT RETAIL (ext.create_on_demand): this replaces a deferred one
    auto dev = Video::Device::_singleton;
    _owner->_width = dev->backBufferWidth;
    _owner->_height = dev->backBufferHeight;
    _owner->_dimension = 0;
    _owner->_kind = 0;
    _owner->_format = 5;
    _owner->_flags = 0x10;
    if (!mainColor) {
        msaaSamples = DesiredSamples();
        mainColor = NewMainColor(_owner->Width(), _owner->Height(), msaaSamples);
    }
    {
        StateGuard guard(stateMutex);
        colorAliases.insert(this);
    }
    if (shared) {
        ShareGpuData::DropTexture(texture);   // NOT RETAIL (ext.share_gpu_data): one user less
        shared = false;
    }
    texture = mainColor;
}
void ImageBufferData::AttachDepthBuffer() {
    CreateOnDemand::Forget(this);   // NOT RETAIL (ext.create_on_demand): this replaces a deferred one
    if (_owner->Width() <= 0 || _owner->Height() <= 0) {
        auto dev = Video::Device::_singleton;
        _owner->_width = dev->backBufferWidth;
        _owner->_height = dev->backBufferHeight;
    }
    _owner->_dimension = 0;
    _owner->_kind = 1;
    _owner->_format = 7;
    if (!mainDepth) {
        msaaSamples = mainColor ? unsigned(mainColor.sampleCount) : DesiredSamples();
        mainDepth = NewMainDepth(_owner->Width(), _owner->Height(), msaaSamples);
    }
    {
        StateGuard guard(stateMutex);
        depthAliases.insert(this);
    }
    if (shared) {
        ShareGpuData::DropTexture(texture);   // NOT RETAIL (ext.share_gpu_data): one user less
        shared = false;
    }
    texture = mainDepth;
}
// One sampler per image from its flags; `point` is the sharp_ui variant (D3D11_FILTER_MIN_MAG_MIP_POINT: no filtering,
// no anisotropy).
// `mipCount` is the texture's (a deferred texture's descriptor's: the sampler is made before it).
static id<MTLSamplerState> MakeSampler(const Video::ImageBuffer* image, NSUInteger mipCount, bool point) {
    auto sd = [MTLSamplerDescriptor new];
    sd.supportArgumentBuffers = YES;
    sd.compareFunction = (!point && (image->_flags & 0x40)) ? MTLCompareFunctionLess : MTLCompareFunctionNever;
    sd.lodMaxClamp = (image->_flags & 1) ? float(mipCount - 1) : 0;
    sd.minFilter = sd.magFilter =
        (point || int(image->_filter) == 0) ? MTLSamplerMinMagFilterNearest : MTLSamplerMinMagFilterLinear;
    sd.mipFilter = (!point && int(image->_filter) == 2) ? MTLSamplerMipFilterLinear : MTLSamplerMipFilterNearest;
    // D3D11 table (imagebufferdata.cpp CreateSamplerState): _filter 2 with textureFilter>0 is ANISOTROPIC (capped at
    // 16).
    if (!point) {
        auto cfg = Video::Configuration::_singleton;
        int aniso = cfg ? int(cfg->textureFilter) : 0;
        if (!(image->_flags & 0x40) && int(image->_filter) == 2 && aniso > 0)
            sd.maxAnisotropy = std::clamp(aniso, 1, 16);
    }
    const MTLSamplerAddressMode modes[] = { MTLSamplerAddressModeRepeat, MTLSamplerAddressModeClampToEdge,
                                            MTLSamplerAddressModeClampToEdge,
                                            MTLSamplerAddressModeClampToBorderColor };
    sd.sAddressMode = modes[std::clamp(int(image->_addressU), 0, 3)];
    sd.tAddressMode = modes[std::clamp(int(image->_addressV), 0, 3)];
    sd.rAddressMode = modes[std::clamp(int(image->_addressW), 0, 3)];
    return [gpu newSamplerStateWithDescriptor:sd];
}
// The seeded creates (seeded_create.h): the load's, and ext.create_on_demand's at first use.
void PlanUploads(const TextureShape& shape, MTLTextureDescriptor* d, const SourceLevel* levels, int count,
                 UploadPlan* plan) {
    const int w = shape.width;
    const int h = shape.height;
    const int f = shape.format;
    // EXT SEAM (NOT RETAIL, docs/extensions/handins/COMPRESSAO.md): ext.compress_ao -- a 2D R8 texture
    // seeded from a resource file (the baked AO maps), every level of the chain present and the top level
    // a multiple of 4 each way, is created as BC4 from its levels encoded (or found in the derived cache).
    // ImageBuffer keeps format 4; only the pixel format and the pitches change.
    std::vector<size_t> bc4Offsets;
    bool asBc4 = false;
    if (CompressAo::Requested() && count > 0 && shape.kind == 2 && f == 4 && shape.faces == 1 && (w & 3) == 0 &&
        (h & 3) == 0 && count == int(d.mipmapLevelCount) && count == shape.levelCount) {
        std::vector<::CompressAo::Level> in(static_cast<size_t>(count));
        for (int i = 0; i < count; ++i) {
            in[i].data = static_cast<const uint8_t*>(levels[i].data);
            in[i].size = static_cast<int64_t>(levels[i].size);
        }
        asBc4 = ::CompressAo::Encode(in.data(), count, w, h, uint32_t(f), &plan->bc4, &bc4Offsets);
        if (asBc4) d.pixelFormat = MTLPixelFormatBC4_RUnorm;
    }
    // The levels as they are uploaded: R8 rows, a retail format's blocks, or the BC4 blocks above (8 bytes
    // per 4x4 block, a level under 4 pixels wide or high still one block).
    const int bytes[] = { 8, 16, 16, 1, 1, 4, 4, 4 };
    const bool blocks = f < 3 || asBc4;
    const int unitBytes = asBc4 ? 8 : bytes[f];
    for (int i = 0; i < count; ++i) {
        unsigned mip = i % d.mipmapLevelCount, face = i / d.mipmapLevelCount;
        if (face >= shape.faces) break;
        int mw = std::max(1, w >> mip), mh = std::max(1, h >> mip);
        size_t row = (blocks ? std::max(1, (mw + 3) / 4) : mw) * unitBytes;
        size_t total = row * (blocks ? std::max(1, (mh + 3) / 4) : mh);
        const void* data = levels[i].data;
        size_t size = levels[i].size;
        if (asBc4) {
            data = plan->bc4.data() + bc4Offsets[i];
            size = bc4Offsets[i + 1] - bc4Offsets[i];
        }
        if (size < total) {
            plan->shortLevel = true;
            break;
        }
        plan->uploads.push_back(LevelUpload{ mip, face, mw, mh, ShareGpuData::LevelBytes{ data, row, total } });
    }
    plan->complete = !plan->shortLevel && plan->uploads.size() == size_t(d.mipmapLevelCount) * shape.faces;
}
id<MTLTexture> MakeTexture(MTLTextureDescriptor* d, const UploadPlan& plan, bool seeded, bool* shared) {
    *shared = false;
    const std::vector<LevelUpload>* uploads = &plan.uploads;
    id<MTLTexture> (^make)(void) = ^id<MTLTexture>(void) {
      id<MTLTexture> made = NewTextureResource(d);
      if (!made) return nil;
      for (const auto& u : *uploads)
          [made replaceRegion:MTLRegionMake2D(0, 0, u.mw, u.mh)
                  mipmapLevel:u.mip
                        slice:u.face
                    withBytes:u.bytes.data
                  bytesPerRow:u.bytes.row
                bytesPerImage:u.bytes.size];
      return made;
    };
    // EXT SEAM (NOT RETAIL, docs/extensions/handins/SHAREGPUDATA.md): ext.share_gpu_data -- a texture
    // seeded from a resource file (kind 2: shader read only, never a render target) with every mip of
    // every face uploaded is made through the create-or-share.
    if (!ShareGpuData::Requested() || !seeded) {
        return make();
    }
    std::vector<ShareGpuData::LevelBytes> keyLevels;
    keyLevels.reserve(plan.uploads.size());
    for (const auto& u : plan.uploads) {
        keyLevels.push_back(u.bytes);
    }
    return ShareGpuData::CreateTexture(gpu, d, keyLevels.data(), int(keyLevels.size()), plan.complete, make, shared);
}
void ForgetBufferUses(id<MTLBuffer> buffer) {
    InvalidateExpanded(buffer);
}
id<MTLBuffer> MakeBuffer(bool index, bool dynamic, const void* data, uint32_t size, bool* shared) {
    const char* kind = index ? "index" : "vertex";
    id<MTLBuffer> (^make)(void) = ^id<MTLBuffer>(void) {
      id<MTLBuffer> made = NewBuffer(std::max(1u, size), kind);
      if (data && size && made) memcpy(made.contents, data, size);
      return made;
    };
    // EXT SEAM (NOT RETAIL, docs/extensions/handins/SHAREGPUDATA.md): ext.share_gpu_data -- a static seeded
    // buffer equal to one already made shares it; anything else, and the row off, is the block above.
    return ShareGpuData::CreateBuffer(gpu, index, dynamic, data, size, make, shared);
}
bool ImageBufferData::Create(const Collection::Array<Collection::Array<uint8_t>>* levels) {
    CreateOnDemand::Forget(this);   // NOT RETAIL (ext.create_on_demand): this create replaces a deferred one
    @autoreleasepool {
        int w = _owner->Width(), h = _owner->Height();
        if (w <= 0 || h <= 0) return true;
        static const MTLPixelFormat formats[] = {
            MTLPixelFormatBC1_RGBA, MTLPixelFormatBC3_RGBA,
            MTLPixelFormatBC3_RGBA, MTLPixelFormatA8Unorm,
            MTLPixelFormatR8Unorm, MTLPixelFormatBGRA8Unorm,
            MTLPixelFormatR32Float, MTLPixelFormatDepth32Float_Stencil8
        };
        int f = int(_owner->_format);
        if (f < 0 || f >= 8) return false;
        // NOT RETAIL (ext.share_gpu_data): a re-create lets go of a shared texture; this user drops first.
        if (shared) {
            ShareGpuData::DropTexture(texture);
            shared = false;
        }
        // Format 7 (retail D24S8) is D32F+S8 here; see NewMainDepth and docs/known_port_issues.md "Depth precision".
        if (f == 7) {
            auto depth =
                [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float_Stencil8
                                                                   width:w
                                                                  height:h
                                                               mipmapped:NO];
            depth.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
            depth.storageMode = MTLStorageModePrivate;
            texture = NewTextureResource(depth);
        } else {
            auto d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:formats[f]
                                                                        width:w
                                                                       height:h
                                                                    mipmapped:(_owner->_flags & 1) != 0];
            if (_owner->_dimension != 0) d.textureType = MTLTextureTypeCube;
            d.storageMode = MTLStorageModeShared;
            d.usage = MTLTextureUsageShaderRead;
            TextureShape shape;
            shape.width = w;
            shape.height = h;
            shape.format = f;
            shape.kind = int(_owner->_kind);
            shape.faces = _owner->_dimension != 0 ? 6u : 1u;
            shape.levelCount = _owner->GetLevelCount();
            if (f >= 3 && shape.kind != 2) d.usage |= MTLTextureUsageRenderTarget;

            // EXT SEAM (NOT RETAIL, docs/extensions/handins/CREATEONDEMAND.md): ext.create_on_demand -- the
            // engine's level reader skipped this image's level bytes (Video::ImageBuffer::_extLevelSource):
            // the descriptor and the sampler are made as for any texture; the texture (after compress_ao's
            // conversion, through share_gpu_data's create-or-share) waits for the first use. The level
            // arrays are empty: nothing here reads them.
            if (levels && shape.kind == 2 && CreateOnDemand::HasPendingLevels(_owner)) {
                texture = nil;   // a re-create's old texture goes now, as the eager path replaces it
                CreateOnDemand::DeferTexture(this, shape, d, &texture, &shared);
                pointSampler = nil;
                sampler = MakeSampler(_owner, d.mipmapLevelCount, false);
                return true;
            }

            // The load's create (seeded_create.h): ext.compress_ao's BC4 conversion, then
            // ext.share_gpu_data's create-or-share for a texture seeded from a resource file (kind 2), else
            // the plain create. A short level still leaves the texture made with the levels before it, and
            // Create answers false.
            std::vector<SourceLevel> source;
            if (levels) {
                source.reserve(static_cast<size_t>(levels->GetSize()));
                for (int i = 0; i < levels->GetSize(); ++i) {
                    source.push_back(SourceLevel{ (*levels)[i].GetData(), size_t((*levels)[i].GetSize()) });
                }
            }
            UploadPlan plan;
            PlanUploads(shape, d, source.data(), int(source.size()), &plan);
            bool isShared = false;
            texture = MakeTexture(d, plan, levels != nullptr && shape.kind == 2, &isShared);
            shared = isShared;
            if (!texture) return false;
            if (plan.shortLevel) return false;
        }
        if (Opt().metalStats && (_owner->_flags & 0x40))
            fprintf(stderr, "[MetalShadow] create %dx%d flags=%x samples=%lu format=%lu\n", w, h,
                    _owner->_flags, (unsigned long)texture.sampleCount, (unsigned long)texture.pixelFormat);
        pointSampler = nil;
        sampler = MakeSampler(_owner, texture.mipmapLevelCount, false);
        return true;
    }
}
void ImageBufferData::Bind(void* context, unsigned slot) {
    // NOT RETAIL (ext.create_on_demand): the first bind of a deferred texture makes it; a made one pays the
    // nil test. The scope stamps the use and keeps an eviction away until the bind is done.
    CreateOnDemand::UseScope use(this);
    if (!texture) CreateOnDemand::Ensure(this);
    if (slot >= 31) return;
    bound.images[slot] = texture;
    bound.samplers[slot] = sampler;
    // EXT SEAM (ext.sharp_ui, NOT RETAIL), same condition as the DX11 body: _extPointSampling is only set inside a UI
    // pass
    // drawn at an integer ratio >= 2 (SharpUI::Draw); a comparison (shadow) image keeps its own sampler.
    if (Video::ImageBufferData::_extPointSampling && ((_owner->_flags >> 6) & 1) == 0 && texture) {
        if (!pointSampler) pointSampler = MakeSampler(_owner, texture.mipmapLevelCount, true);
        if (pointSampler) bound.samplers[slot] = pointSampler;
    }
}
bool ImageBufferData::Update(void* data, int width, int height) {
    CreateOnDemand::UseScope use(this);           // NOT RETAIL (ext.create_on_demand): stamp, no eviction meanwhile
    if (!texture) CreateOnDemand::Ensure(this);   // NOT RETAIL (ext.create_on_demand)
    if (!texture || !data || width <= 0 || height <= 0 || width > int(texture.width) ||
        height > int(texture.height))
        return false;
    int f = int(_owner->_format);
    if (f < 3 || f == 7) return false;
    // NOT RETAIL (ext.share_gpu_data, ext.compress_ao): a shared texture is never written (D3D11 refuses its
    // IMMUTABLE one), and a BC4 texture does not hold the R8 rows the engine would write.
    if (shared || texture.pixelFormat == MTLPixelFormatBC4_RUnorm) return false;
    size_t row = width * (f == 3 || f == 4 ? 1 : 4);
    [texture replaceRegion:MTLRegionMake2D(0, 0, width, height) mipmapLevel:0 withBytes:data bytesPerRow:row];
    return true;
}
// Holds the device lock (recursive) for a scope: see NewCommands for why every user of `commands` needs it.
struct DeviceLockScope {
    Video::Device* device;
    DeviceLockScope() : device(Video::Device::_singleton) {
        if (device) {
            device->Lock();
        }
    }
    ~DeviceLockScope() {
        if (device) {
            device->Unlock();
        }
    }
};
bool ImageBufferData::Read(void* dst, int width, int height) {
    CreateOnDemand::UseScope use(this);           // NOT RETAIL (ext.create_on_demand): stamp, no eviction meanwhile
    if (!texture) CreateOnDemand::Ensure(this);   // NOT RETAIL (ext.create_on_demand), before the device lock
    // Main thread with the render thread on: the lock keeps this whole body (it ends and commits the frame's
    // current segment and starts the next one) out of the render thread's encoding.
    DeviceLockScope deviceLock;
    if (!texture || !dst || width <= 0 || height <= 0 || width > int(texture.width) || height > int(texture.height)) {
        return false;
    }
    int f = int(_owner->_format);
    if (f < 3 || f == 7) {
        return false;
    }
    // NOT RETAIL (ext.compress_ao): a BC4 texture holds blocks, not the R8 rows the caller expects.
    if (texture.pixelFormat == MTLPixelFormatBC4_RUnorm) {
        return false;
    }
    size_t row = width * (f == 3 || f == 4 ? 1 : 4);
    // Read uses the same command stream as rendering/FrameCopy Resolve. Commit
    // this segment and resume encoding without consuming another in-flight slot.
    EndEncoder();
    auto cb = commands ? commands : [queue commandBuffer];
    auto source = texture;
    if (source.sampleCount > 1) {
        auto d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:source.pixelFormat
                                                                    width:source.width
                                                                   height:source.height
                                                                mipmapped:NO];
        d.storageMode = MTLStorageModePrivate;
        d.usage = MTLTextureUsageRenderTarget;
        auto resolved = NewTextureResource(d);
        if (!resolved) {
            return false;
        }
        auto pass = [MTLRenderPassDescriptor renderPassDescriptor];
        auto a = pass.colorAttachments[0];
        a.texture = source;
        a.resolveTexture = resolved;
        a.loadAction = MTLLoadActionLoad;
        a.storeAction = MTLStoreActionStoreAndMultisampleResolve;
        [[cb renderCommandEncoderWithDescriptor:pass] endEncoding];
        source = resolved;
    }
    size_t pitch = (row + 255) & ~size_t(255);
    auto staging = NewBuffer(pitch * height, "readback");
    if (!staging) {
        return false;
    }
    auto blit = [cb blitCommandEncoder];
    [blit copyFromTexture:source
                     sourceSlice:0
                     sourceLevel:0
                    sourceOrigin:MTLOriginMake(0, 0, 0)
                      sourceSize:MTLSizeMake(width, height, 1)
                        toBuffer:staging
               destinationOffset:0
          destinationBytesPerRow:pitch
        destinationBytesPerImage:pitch * height];
    [blit endEncoding];
    bool resume = commands != nil;
    // Intentional: the device lock stays held through waitUntilCompleted. This is the screenshot / readback path only
    // (never per frame), and it cannot deadlock: the GPU completes this command buffer on its own, its completion
    // handlers (retire/reuse bookkeeping) do not take the device lock, and no other thread waits on this one.
    [cb commit];
    [cb waitUntilCompleted];
    if (resume) {
        commands = NewCommands();
    }
    if (cb.error) {
        fprintf(stderr, "[Metal] readback: %s\n", cb.error.localizedDescription.UTF8String);
        return false;
    }
    // Format 5 is already BGRA8 (also DX11's staging format). Preserve encoded
    // sRGB bytes; the engine's BMP writer consumes packed 0xAARRGGBB directly.
    for (int y = 0; y < height; ++y) {
        memcpy(static_cast<uint8_t*>(dst) + y * row, static_cast<const uint8_t*>(staging.contents) + y * pitch, row);
    }
    return true;
}
// The Serialize body of both buffer kinds. Load: the payload is read and handed to Create as its seed (the
// DX11 load arm's order; before ext.share_gpu_data the Metal arm created an unseeded buffer and read the
// payload into it, the same bytes); a buffer of size 0 keeps the old order. Save: the contents are written.
template <class Data>
static void SerializeBuffer(Data* self, IO::Serializer* serializer, bool index) {
    auto* owner = self->_owner;
    const bool loading = static_cast<int>(serializer->GetDirection()) == 0;
    // NOT RETAIL (ext.create_on_demand): no eviction while this reads `buffer`, and the save arm's deferred
    // buffer is made first.
    CreateOnDemand::UseScope use(self);
    if (!loading && !self->buffer) CreateOnDemand::Ensure(self);
    serializer->Serialize(owner->dynamic, nullptr);
    serializer->Serialize(owner->size, nullptr);
    if (loading && owner->size > 0) {
        // EXT SEAM (NOT RETAIL, docs/extensions/handins/CREATEONDEMAND.md): ext.create_on_demand -- a static
        // buffer read from a packed resource file is not read: its create waits for its first use, and the
        // stream skips the payload. The owner already holds the size / dynamic Create would store.
        if (CreateOnDemand::Requested() && owner->dynamic == 0 && !self->buffer) {
            self->cursor = 0;
            if (CreateOnDemand::DeferBuffer(self, serializer->_stream, index, uint32_t(owner->size), &self->buffer,
                                            &self->shared))
                return;
        }
        std::vector<uint8_t> payload(static_cast<size_t>(owner->size));
        serializer->SerializeBytes(owner->size, payload.data(), nullptr);
        self->Create(owner->size, owner->dynamic, payload.data());
        return;
    }
    if (loading) self->Create(owner->size, owner->dynamic, nullptr);
    if (self->buffer) serializer->SerializeBytes(owner->size, self->buffer.contents, nullptr);
}
IndexBufferData::~IndexBufferData() {
    CreateOnDemand::Forget(this);   // NOT RETAIL (ext.create_on_demand): no first-use create after this
    if (shared) {
        ShareGpuData::DropBuffer(buffer);   // NOT RETAIL (ext.share_gpu_data): one user less
    }
}
bool IndexBufferData::Create(uint32_t sizeBytes, uint8_t dynamic, const void* data) {
    CreateOnDemand::Forget(this);   // NOT RETAIL (ext.create_on_demand): this create replaces a deferred one
    _owner->size = sizeBytes;
    _owner->dynamic = dynamic;
    // NOT RETAIL (ext.share_gpu_data, docs/extensions/handins/SHAREGPUDATA.md): a re-create lets go of a
    // shared buffer (this user drops first); MakeBuffer shares a static seeded buffer equal to one made.
    if (shared) {
        ShareGpuData::DropBuffer(buffer);
    }
    bool isShared = false;
    auto fresh = MakeBuffer(true, dynamic != 0, data, sizeBytes, &isShared);
    {
        StateGuard guard(stateMutex);
        buffer = fresh;
        shared = isShared;
    }
    cursor = 0;
    return buffer != nil;
}
bool IndexBufferData::SetData(int32_t sizeBytes, int32_t offsetBytes, const void* src) {
    CreateOnDemand::UseScope use(this);          // NOT RETAIL (ext.create_on_demand): no eviction meanwhile
    if (!buffer) CreateOnDemand::Ensure(this);   // NOT RETAIL (ext.create_on_demand)
    if (sizeBytes < 0 || offsetBytes < 0 || !buffer || size_t(offsetBytes) + sizeBytes > buffer.length ||
        (!src && sizeBytes))
        return false;
    // NOT RETAIL (ext.share_gpu_data): a shared buffer is never written; D3D11's UpdateSubresource on its
    // IMMUTABLE buffer does nothing and SetData still answers true.
    if (shared) return true;
    if (sizeBytes) {
        InvalidateExpanded(buffer);
        // Hold the state lock across the rename and the write: when the in-place path is taken (the GPU is done with
        // the buffer), a Draw on the render thread cannot snapshot and encode it until the write has finished.
        StateGuard guard(stateMutex);
        RenameBuffer(buffer, true);
        memcpy(static_cast<uint8_t*>(buffer.contents) + offsetBytes, src, sizeBytes);
    }
    return true;
}
void* IndexBufferData::Lock(int32_t sizeBytes, int32_t* outByteOffset) {
    CreateOnDemand::UseScope use(this);          // NOT RETAIL (ext.create_on_demand): no eviction meanwhile
    if (!buffer) CreateOnDemand::Ensure(this);   // NOT RETAIL (ext.create_on_demand)
    if (!buffer || sizeBytes < 0 || size_t(sizeBytes) > buffer.length) return nullptr;
    // NOT RETAIL (ext.share_gpu_data): a shared buffer is never written; D3D11's Map fails on its
    // IMMUTABLE buffer and Lock returns null.
    if (shared) return nullptr;
    // Rewind on the first write of a frame or wrap (DISCARD); rename only
    // while encoded/in-flight draws read the allocation. Appends are NO_OVERWRITE.
    if (generation != frameGeneration || cursor + sizeBytes > buffer.length) {
        InvalidateExpanded(buffer);
        RenameBuffer(buffer, false);
        cursor = 0;
        generation = frameGeneration;
    }
    // Accepted window: Lock() hands the pointer out after the state lock is released, so on the in-place path a Draw
    // that snapshots this buffer before the caller finishes writing could be encoded early. This relies on the
    // engine locking, writing and unlocking a dynamic buffer before it issues the draw that reads it; that
    // convention has NOT been audited in the engine code. If it is violated the GPU can read a half-written range
    // of the buffer for that frame (the allocation itself stays valid, so no out-of-bounds read).
    if (outByteOffset) *outByteOffset = int(cursor);
    auto ptr = static_cast<uint8_t*>(buffer.contents) + cursor;
    cursor += sizeBytes;
    return ptr;
}
void IndexBufferData::Unlock() {}
void IndexBufferData::Bind(void* context) {
    CreateOnDemand::UseScope use(this);          // NOT RETAIL (ext.create_on_demand): stamp, no eviction meanwhile
    if (!buffer) CreateOnDemand::Ensure(this);   // NOT RETAIL (ext.create_on_demand): before the state lock
    StateGuard guard(stateMutex);
    bound.index = buffer;
}
void IndexBufferData::Serialize(IO::Serializer* serializer) {
    SerializeBuffer(this, serializer, true);
}
VertexBufferData::~VertexBufferData() {
    CreateOnDemand::Forget(this);   // NOT RETAIL (ext.create_on_demand): no first-use create after this
    InvalidateExpanded(buffer);
    if (shared) {
        ShareGpuData::DropBuffer(buffer);   // NOT RETAIL (ext.share_gpu_data): one user less
    }
}
bool VertexBufferData::Create(uint32_t sizeBytes, uint8_t dynamic, const void* data) {
    CreateOnDemand::Forget(this);   // NOT RETAIL (ext.create_on_demand): this create replaces a deferred one
    InvalidateExpanded(buffer);
    _owner->size = sizeBytes;
    _owner->dynamic = dynamic;
    // NOT RETAIL (ext.share_gpu_data, docs/extensions/handins/SHAREGPUDATA.md): a re-create lets go of a
    // shared buffer (this user drops first); MakeBuffer shares a static seeded buffer equal to one made.
    if (shared) {
        ShareGpuData::DropBuffer(buffer);
    }
    bool isShared = false;
    auto fresh = MakeBuffer(false, dynamic != 0, data, sizeBytes, &isShared);
    {
        StateGuard guard(stateMutex);
        buffer = fresh;
        shared = isShared;
    }
    cursor = 0;
    return buffer != nil;
}
bool VertexBufferData::SetData(int32_t sizeBytes, int32_t offsetBytes, const void* src) {
    CreateOnDemand::UseScope use(this);          // NOT RETAIL (ext.create_on_demand): no eviction meanwhile
    if (!buffer) CreateOnDemand::Ensure(this);   // NOT RETAIL (ext.create_on_demand)
    if (sizeBytes < 0 || offsetBytes < 0 || !buffer || size_t(offsetBytes) + sizeBytes > buffer.length ||
        (!src && sizeBytes))
        return false;
    // NOT RETAIL (ext.share_gpu_data): a shared buffer is never written; D3D11's UpdateSubresource on its
    // IMMUTABLE buffer does nothing and SetData still answers true.
    if (shared) return true;
    if (sizeBytes) {
        InvalidateExpanded(buffer);
        // Hold the state lock across the rename and the write: when the in-place path is taken (the GPU is done with
        // the buffer), a Draw on the render thread cannot snapshot and encode it until the write has finished.
        StateGuard guard(stateMutex);
        RenameBuffer(buffer, true);
        memcpy(static_cast<uint8_t*>(buffer.contents) + offsetBytes, src, sizeBytes);
    }
    return true;
}
void* VertexBufferData::Lock(int32_t sizeBytes, int32_t* outByteOffset) {
    CreateOnDemand::UseScope use(this);          // NOT RETAIL (ext.create_on_demand): no eviction meanwhile
    if (!buffer) CreateOnDemand::Ensure(this);   // NOT RETAIL (ext.create_on_demand)
    if (!buffer || sizeBytes < 0 || size_t(sizeBytes) > buffer.length) return nullptr;
    // NOT RETAIL (ext.share_gpu_data): a shared buffer is never written; D3D11's Map fails on its
    // IMMUTABLE buffer and Lock returns null.
    if (shared) return nullptr;
    // Rewind on the first write of a frame or wrap (DISCARD); rename only
    // while encoded/in-flight draws read the allocation. Appends are NO_OVERWRITE.
    if (generation != frameGeneration || cursor + sizeBytes > buffer.length) {
        InvalidateExpanded(buffer);
        RenameBuffer(buffer, false);
        cursor = 0;
        generation = frameGeneration;
    }
    // Accepted window: Lock() hands the pointer out after the state lock is released, so on the in-place path a Draw
    // that snapshots this buffer before the caller finishes writing could be encoded early. The contract is that a
    // dynamic buffer is locked, written and unlocked before the draw that reads it is issued (same thread of the
    // engine); a loader-thread rename only ever races a buffer no draw uses yet. Worst case if violated: one frame
    // of stale vertex data in the not-yet-written range, never a crash or a read outside the allocation.
    if (outByteOffset) *outByteOffset = int(cursor);
    auto ptr = static_cast<uint8_t*>(buffer.contents) + cursor;
    cursor += sizeBytes;
    return ptr;
}
void VertexBufferData::Unlock() {}
void VertexBufferData::Bind(void* context, uint32_t startSlot, uint32_t offsetBytes, uint32_t strideBytes) {
    // NOT RETAIL (ext.create_on_demand): the first bind of a deferred buffer makes it (before the state lock);
    // a made one pays the nil test. The scope stamps the use and keeps an eviction away until the bind is done.
    CreateOnDemand::UseScope use(this);
    if (!buffer) CreateOnDemand::Ensure(this);
    if (startSlot < 31) {
        StateGuard guard(stateMutex);
        bound.vertex[startSlot] = buffer;
        bound.vertexDynamic[startSlot] = _owner->dynamic != 0;
        bound.vertexOffsets[startSlot] = offsetBytes;
        bound.strides[startSlot] = strideBytes;
    }
}
void VertexBufferData::Serialize(IO::Serializer* serializer) {
    SerializeBuffer(this, serializer, false);
}
bool ConstantBufferData::Create() {
    int offset = 0;
    for (int i = 0; i < _owner->_constantCount; ++i) {
        auto& e = _owner->_constantData[i];
        e.offset = offset;
        offset += e.size;
    }
    buffer = NewBuffer(std::max(16, _owner->_registerCount * 16), "constant");
    return !gpu || buffer != nil;
}
void ConstantBufferData::Bind(void* context, const void* srcData, int32_t first, int32_t sizeBytes) {
    if (sizeBytes <= 0 || !srcData) return;
    int stage = _owner->_bindToVertex ? 1 : 0, slot = _owner->_uploadIndex;
    if (slot < 0 || slot >= 31) return;
    size_t size = std::max(sizeBytes, _owner->_registerCount * 16);
    std::vector<uint8_t> bytes(size, 0);
    memcpy(bytes.data(), srcData, sizeBytes);
    buffer = Upload(bytes.data(), size, constantOffsets[stage][slot]);
    constantSizes[stage][slot] = size;
    bound.constants[stage][slot] = buffer;
}
int32_t ConstantBufferData::SlotIndexPlusOne() {
    return _owner->_uploadIndex + 1;
}
bool RenderBufferData::Slot1() { return true; }
void RenderBufferData::Bind(void* context) {
    targetColor = ImageTexture(_owner->colorBuffer);
    targetDepth = ImageTexture(_owner->depthBuffer);
}
void RenderBufferData::Resolve(void* context, Video::RenderBuffer* other, bool flag) {
    auto dst = ImageTexture(_owner->colorBuffer);
    auto src = other ? ImageTexture(other->colorBuffer) : nil;
    if (!src || !dst || src == dst || src.pixelFormat != dst.pixelFormat || src.width != dst.width ||
        src.height != dst.height)
        return;
    EndEncoder();
    if (src.sampleCount > 1) {
        if (dst.sampleCount != 1 || !(dst.usage & MTLTextureUsageRenderTarget))
            return;
        auto cb = commands ? commands : [queue commandBuffer];
        auto pass = [MTLRenderPassDescriptor renderPassDescriptor];
        auto a = pass.colorAttachments[0];
        a.texture = src;
        a.resolveTexture = dst;
        a.loadAction = MTLLoadActionLoad;
        a.storeAction = MTLStoreActionStoreAndMultisampleResolve;
        [[cb renderCommandEncoderWithDescriptor:pass] endEncoding];
        if (!commands) {
            [cb commit];
            [cb waitUntilCompleted];
        }
        return;
    }
    auto cb = commands ? commands : [queue commandBuffer];
    auto blit = [cb blitCommandEncoder];
    [blit copyFromTexture:src
              sourceSlice:0
              sourceLevel:0
             sourceOrigin:MTLOriginMake(0, 0, 0)
               sourceSize:MTLSizeMake(src.width, src.height, 1)
                toTexture:dst
         destinationSlice:0
         destinationLevel:0
        destinationOrigin:MTLOriginMake(0, 0, 0)];
    [blit endEncoding];
    if (!commands) {
        [cb commit];
        [cb waitUntilCompleted];
    }
}
void VertexLayoutData::Bind(void* context) {
    bound.layout = descriptor;
}
VertexLayoutData::~VertexLayoutData() {
    EvictPipelineOwner((__bridge void*)descriptor);
}
void VertexLayoutData::PlatformCreate() {
    EvictPipelineOwner((__bridge void*)descriptor);
    descriptor = [MTLVertexDescriptor vertexDescriptor];
    const MTLVertexFormat formats[] = { MTLVertexFormatFloat4,
                                        MTLVertexFormatFloat3,
                                        MTLVertexFormatFloat2,
                                        MTLVertexFormatFloat,
                                        MTLVertexFormatShort4,
                                        MTLVertexFormatShort2,
                                        MTLVertexFormatUChar4Normalized_BGRA,
                                        MTLVertexFormatUChar4,
                                        MTLVertexFormatUChar4Normalized };
    const size_t sizes[] = { 16, 12, 8, 4, 8, 4, 4, 4, 4 };
    for (int i = 0; i < std::min(31, _owner->_elements.GetSize()); ++i) {
        auto& e = _owner->_elements[i];
        if (e.format < 0 || e.format >= 9 || e.stream < 0 || e.stream >= 23) continue;
        auto a = descriptor.attributes[i];
        a.format = formats[e.format];
        a.bufferIndex = e.stream + 8;
        a.offset = e.offset;
        auto l = descriptor.layouts[e.stream + 8];
        l.stride = std::max(l.stride, NSUInteger(e.offset + sizes[e.format]));
        l.stepFunction = MTLVertexStepFunctionPerVertex;
        l.stepRate = 1;
    }
}
// The fast cache keys on each translated function's address: evict before the Shader (and its function) is released.
static void EvictShaders(const std::vector<Shader>& shaders) {
    for (auto& s : shaders)
        EvictPipelineOwner((__bridge void*)s.function);
}
VertexProgramData::~VertexProgramData() {
    EvictShaders(shaders);
}
PixelProgramData::~PixelProgramData() {
    EvictShaders(shaders);
}
bool VertexProgramData::PlatformCreate() {
    EvictShaders(shaders);
    bytecode.clear();
    shaders.clear();
    bool ok = true;
    for (int i = 0; i < _owner->_byteCode.GetSize(); ++i) {
        auto& b = _owner->_byteCode[i];
        bytecode.emplace_back(b.GetData(), b.GetData() + b.GetSize());
        shaders.push_back(Translate(bytecode.back(), gpu));
        ok &= shaders.back().function != nil;
    }
    return ok;
}
void VertexProgramData::Bind(void* context, int permutation) {
    bound.vs = permutation >= 0 && size_t(permutation) < shaders.size()
                   ? TreePasses::Select(&shaders[permutation], bytecode[permutation], gpu, true)
                   : nullptr;
}
bool PixelProgramData::PlatformCreate() {
    EvictShaders(shaders);
    bytecode.clear();
    shaders.clear();
    bool ok = true;
    for (int i = 0; i < _owner->_byteCode.GetSize(); ++i) {
        auto& b = _owner->_byteCode[i];
        bytecode.emplace_back(b.GetData(), b.GetData() + b.GetSize());
        shaders.push_back(Translate(bytecode.back(), gpu));
        ok &= shaders.back().function != nil;
    }
    return ok;
}
void PixelProgramData::Bind(void* context, int permutation) {
    bound.ps = permutation >= 0 && size_t(permutation) < shaders.size()
                   ? TreePasses::Select(&shaders[permutation], bytecode[permutation], gpu, false)
                   : nullptr;
}
RenderStateData::~RenderStateData() {
    EvictPipelineOwner((__bridge void*)blend);
}
bool RenderStateData::Create() {
    EvictPipelineOwner((__bridge void*)blend);
    auto rs = _owner;
    const MTLCompareFunction cmp[] = { MTLCompareFunctionNever, MTLCompareFunctionAlways,
                                       MTLCompareFunctionEqual, MTLCompareFunctionLess,
                                       MTLCompareFunctionLessEqual, MTLCompareFunctionGreater,
                                       MTLCompareFunctionGreaterEqual, MTLCompareFunctionNotEqual };
    auto d = [MTLDepthStencilDescriptor new];
    d.depthCompareFunction = rs->_depth.test ? cmp[std::clamp<int>(rs->_depth.func, 0, 7)] : MTLCompareFunctionAlways;
    d.depthWriteEnabled = rs->_depth.write;
    if (rs->_stencil.enabled) {
        const MTLStencilOperation ops[] = {
            MTLStencilOperationKeep, MTLStencilOperationZero,
            MTLStencilOperationIncrementWrap, MTLStencilOperationDecrementWrap,
            MTLStencilOperationIncrementClamp, MTLStencilOperationDecrementClamp,
            MTLStencilOperationReplace, MTLStencilOperationInvert
        };
        auto st = [MTLStencilDescriptor new];
        st.stencilCompareFunction = cmp[std::clamp<int>(rs->_stencil.func, 0, 7)];
        st.stencilFailureOperation = ops[std::clamp<int>(rs->_stencil.failOp, 0, 7)];
        st.depthFailureOperation = ops[std::clamp<int>(rs->_stencil.depthFailOp, 0, 7)];
        st.depthStencilPassOperation = ops[std::clamp<int>(rs->_stencil.passOp, 0, 7)];
        d.frontFaceStencil = st;
        d.backFaceStencil = [st copy];
        if (rs->_stencil.twoSided) {
            d.backFaceStencil.stencilFailureOperation = ops[std::clamp<int>(rs->_stencil.backFailOp, 0, 7)];
            d.backFaceStencil.depthFailureOperation = ops[std::clamp<int>(rs->_stencil.backDepthFailOp, 0, 7)];
            d.backFaceStencil.depthStencilPassOperation = ops[std::clamp<int>(rs->_stencil.backPassOp, 0, 7)];
        }
    }
    depthState = [gpu newDepthStencilStateWithDescriptor:d];
    blend = [MTLRenderPipelineColorAttachmentDescriptor new];
    blend.pixelFormat = MTLPixelFormatBGRA8Unorm;
    blend.blendingEnabled = rs->_alphaBlend.enabled;
    const MTLBlendFactor factors[] = { MTLBlendFactorZero,
                                       MTLBlendFactorOne,
                                       MTLBlendFactorSourceColor,
                                       MTLBlendFactorOneMinusSourceColor,
                                       MTLBlendFactorSourceAlpha,
                                       MTLBlendFactorOneMinusSourceAlpha,
                                       MTLBlendFactorDestinationColor,
                                       MTLBlendFactorOneMinusDestinationColor,
                                       MTLBlendFactorDestinationAlpha,
                                       MTLBlendFactorOneMinusDestinationAlpha };
    blend.sourceRGBBlendFactor = blend.sourceAlphaBlendFactor =
        factors[std::clamp<int>(rs->_alphaBlend.srcBlend, 0, 9)];
    blend.destinationRGBBlendFactor = blend.destinationAlphaBlendFactor =
        factors[std::clamp<int>(rs->_alphaBlend.dstBlend, 0, 9)];
    blend.rgbBlendOperation = blend.alphaBlendOperation =
        MTLBlendOperation(std::clamp<int>(rs->_alphaBlend.blendOp, 0, 2));
    unsigned mask = rs->_alphaBlend.writeMask;
    blend.writeMask = MTLColorWriteMask(
        ((mask & 1) ? MTLColorWriteMaskRed : 0) | ((mask & 2) ? MTLColorWriteMaskGreen : 0) |
        ((mask & 4) ? MTLColorWriteMaskBlue : 0) | ((mask & 8) ? MTLColorWriteMaskAlpha : 0));
    return depthState != nil;
}
void RenderStateData::Bind(void* context) {
    bound.depth = depthState;
    bound.blend = blend;
    bound.scissor = _owner->_flag70 != 0;
    const MTLCullMode modes[] = { MTLCullModeFront, MTLCullModeBack, MTLCullModeNone };
    bound.cull = modes[std::clamp<int>(_owner->_cullMode, 0, 2)];
    bound.fill = _owner->_fillMode ? MTLTriangleFillModeLines : MTLTriangleFillModeFill;
    // Match DX11 RenderStateData: engine bias is normalized to D24 units,
    // and slope bias is stored in 1/4096 units. Metal also takes rasterizer units.
    bound.bias = float(int(_owner->_depth.bias * 16777215.0f));
    bound.slopeBias = _owner->_depth.slopeBias * 4096.0f;
    bound.stencilReference = _owner->_stencil.reference;
}
bool VideoProgramData::Slot1() { return true; }
void VideoProgramData::Bind(void* context, int detail) {}
void Assao::AfterOpaque(Video::Context& context, const Graphics::ExtAoFrame& frame,
                        const Graphics::RendererSceneEntry& entry) {
    EndEncoder();
    Assao::EncodeAfterOpaque(gpu, commands, context, frame, entry);
}
}  // namespace VideoMetal
