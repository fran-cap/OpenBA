#pragma once
// ============================================================================
// src/render/vulkan/pipelines.h -- shader registry, vertex layouts, render states, the pipeline cache
// ============================================================================
// NOT RETAIL. docs/vulkan/PLAN.md §4.3. Bodies: pipelines.cpp.
//
// ONE PIPELINE LAYOUT for the game: descriptor set 0 is the vertex stage, set 1 the pixel stage,
// each with the binding layout of shader_convert.h (b0..b7 at 0..7, t0..t15 at 8..23, s0..s15 at
// 24..39). The constant buffers are UNIFORM_BUFFER_DYNAMIC on the per-frame ring (draw.cpp), as
// many per set as the device allows dynamic (half of maxDescriptorSetUniformBuffersDynamic, at most
// 8: 7 on the NVIDIA dev box, retail uses b0..b4); the rest are plain uniform buffers, whose offset
// is then written into the set. P8: textures are COMBINED_IMAGE_SAMPLER descriptors, each with the
// sampler of its pair (shader_convert.h COMBINED IMAGE SAMPLERS); the 24..39 range holds only a
// texture's further pairs, which no retail program has.
//
// THE SHADER REGISTRY. One VkShaderModule per distinct DXBC blob (Hash64 of the bytes, mixed with
// the stage), shared by every program permutation that carries the same bytes and reference-counted
// by them. A pipeline key names shaders by that hash, so a key is the same in every run and can be
// written to the warm list.
//
// PIPELINES. A monolithic VkPipeline per PipelineKey: (vertex shader, pixel shader, linked flag,
// vertex layout, render state, topology, colour format, depth format, sample count). Viewport,
// scissor, stencil reference and the vertex strides are dynamic state (strides: core 1.3
// VERTEX_INPUT_BINDING_STRIDE), so none of them is in the key; the 1.1 path (vulkan11.h) has no
// dynamic stride and keys the strides (PipelineKey::strides), which the warm list then records.
// Vertex-input dynamic state (VK_EXT_vertex_input_dynamic_state) is NOT used in P2: in this game a
// vertex shader implies its layout, so dropping the layout from the key saves almost no pipelines; P7
// stopped enabling the extension (Qualcomm's Adreno guidance: avoid it).
// A pair whose interpolants do not link by register (Shaders::StagesCompatible false) gets a pixel
// module converted again with the vertex stage's outputs (ConvertOptions::previousStage), cached per
// pair. The VkPipelineCache persists in the user's derived-data folder (vulkan_pipelines.bin in
// <Documents>\Banished\Cache, header-validated against the device and written atomically at shutdown;
// none under the faithful profile), and vulkan_pipelines.txt lists the keys a session built: the
// next start compiles them on a worker thread as soon as both shaders of a key are registered
// (Metal's pipelines.txt).
// ============================================================================
#include "vk_api.h"
#include "shader_convert.h"

#include <cstdint>
#include <string>
#include <vector>

namespace VideoVulkan {

// ---- lifecycle ------------------------------------------------------------------------------------
// After ResourcesStartup: the set layouts, the pipeline layout, the pipeline cache (loaded from the
// user's cache folder), the warm list and its worker.
bool PipelinesStartup(std::string* why);
// The device is idle: stops the worker, writes the cache and the warm list, destroys every pipeline,
// linked module and layout.
void PipelinesShutdown();
// P7: writes the warm list and the VkPipelineCache now, as the shutdown does (the device stays up).
// The mobile lifecycle calls it when the app goes to the background: Android may kill a background
// process without a quit (device.cpp, THE MOBILE LIFECYCLE).
void PipelinesPersist();

// ---- the binding model ------------------------------------------------------------------------------
VkPipelineLayout GamePipelineLayout();
// The persistent VkPipelineCache (VK_NULL_HANDLE when none could be made). P4: the AO pass builds its
// own pipelines through it too.
VkPipelineCache GamePipelineCache();
VkDescriptorSetLayout StageSetLayout(Shaders::Stage stage);
// How many of the constant-buffer bindings 0..7 of each set are UNIFORM_BUFFER_DYNAMIC (the first N).
uint32_t DynamicConstantBuffers();
// P7, THE PAIR LAYOUTS (the push-descriptor path). When the device has VK_KHR_push_descriptor (the
// S25, the NVIDIA dev box, lavapipe), every (vertex, pixel) pair gets a pipeline layout of its own
// whose two set layouts hold only the bindings each stage's shader declares (the binding numbers stay
// those of shader_convert.h). A pipeline layout may hold ONE push set, so:
//   set 1 (pixel), the one whose textures change from draw to draw, is PUSHED
//     (vkCmdPushDescriptorSetKHR; constant buffers plain UNIFORM_BUFFER, the ring offset in the write),
//     again only when its contents or the layout changed;
//   set 0 (vertex), constant buffers as UNIFORM_BUFFER_DYNAMIC, is a set from the frame's pools as
//     before, found again by comparing with the last one (its buffers change only when the ring
//     moves to another chunk) and bound with its dynamic offsets.
// Measured on the S25: the descriptor work was 37-42% of the render thread on the 40-binding layouts
// (docs/android/PERF_P7.md §3.3). Without the extension the one-layout path above stays.
// P13, THE 1.1 PATH's PIXEL SET (docs/android/P13_CPU.md): on a device without Vulkan 1.3 (vulkan11.h)
// set 1 is a pool set like set 0 (constant buffers UNIFORM_BUFFER_DYNAMIC, found again by its contents,
// bound with the ring offsets): the S10's Adreno 640 driver prices a push at ~2.8 us and a bind at
// almost nothing. The 1.3 path keeps the push (P7 measured the opposite trade on the S25's 1.3 driver).
// A pair whose two stages' constant buffers exceed maxDescriptorSetUniformBuffersDynamic pushes.
struct PairLayout {
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkDescriptorSetLayout vertexSet = VK_NULL_HANDLE;   // set 0, allocated from the frame's pools
    uint32_t vertexCbs = 0;                             // its dynamic constant buffers (popcount)
    uint32_t vertexBindings = 0;                        // its descriptors in all
    VkDescriptorSetLayout pixelSet = VK_NULL_HANDLE;    // P13: set 1 as a pool set; null = pushed
};
bool PushDescriptors();
// The pair's layouts on the push path (cached; the pointer stays valid until PipelinesShutdown; null
// when the pixel stage declares more bindings than maxPushDescriptors). `ps` may be null.
const PairLayout* PairLayoutFor(const Shaders::ShaderReflection& vs, const Shaders::ShaderReflection* ps);
// The range of every constant-buffer descriptor: the largest constant buffer a retail shader declares
// fits (D3D10 allows 64 KB; the game's largest is the 52-instance transform bank, 3328 bytes).
constexpr uint32_t kConstantBufferRange = 16384;

// ---- the shader registry -----------------------------------------------------------------------------
struct ShaderEntry {
    uint64_t hash = 0;
    Shaders::Stage stage = Shaders::Stage::Vertex;
    VkShaderModule module = VK_NULL_HANDLE;
    Shaders::ShaderReflection reflection;
    std::vector<uint8_t> dxbc;   // pixel stage only: the linked conversion reads it again
    int refs = 0;
    // NOT RETAIL (docs/extensions/NATURAL_GROWTH.md): a rewrite stage's natural-growth copy. A pixel one reads
    // an interpolant only a natural-growth vertex copy writes: the draw pairs them (draw.cpp).
    bool naturalGrowth = false;
};
// The registry key of a blob: Hash64 of its bytes, mixed with the stage.
uint64_t ShaderHash(const uint8_t* dxbc, size_t size, Shaders::Stage stage);
// The entry for `hash` with one more reference, or null when no program registered it yet.
ShaderEntry* AcquireShader(uint64_t hash);
// Registers a module made for `hash` (one reference). When another thread registered the same hash
// meanwhile, `module` is destroyed and the existing entry is returned (with one more reference).
ShaderEntry* AddShader(uint64_t hash, Shaders::Stage stage, VkShaderModule module,
                       const Shaders::ShaderReflection& reflection, const uint8_t* dxbc, size_t size);
// Drops one reference; the module is destroyed with the last one.
void ReleaseShader(ShaderEntry* entry);

// ---- vertex layouts -----------------------------------------------------------------------------------
// One element of an engine VertexLayout: the DX11 input element i is (TEXCOORD, i), so the element's
// index is its semantic index.
struct LayoutElement {
    uint8_t format = 0;   // the engine's format 0..8 (dx11 vertexlayoutdata.cpp kElementFormat)
    uint8_t stream = 0;   // the vertex stream (D3D11 InputSlot)
    uint16_t offset = 0;  // the byte offset in the stream
};
static_assert(sizeof(LayoutElement) == 4, "LayoutElement is hashed and written as 4 bytes");
// The hash of an element list, kept for the pipelines that name it. 0 for an empty list.
uint64_t InternLayout(const LayoutElement* elements, size_t count);

// ---- render states ----------------------------------------------------------------------------------
// The pipeline half of an engine RenderState, translated to Vulkan values (renderstatedata's tables)
// at RenderStateData::Create. Plain bytes: it is hashed and written to the warm list.
struct StateBlock {
    uint8_t blendEnable = 0;
    uint8_t srcBlend = 0;        // VkBlendFactor (colour and alpha: retail sets both from one field)
    uint8_t dstBlend = 0;
    uint8_t blendOp = 0;         // VkBlendOp
    uint8_t writeMask = 0xF;     // VkColorComponentFlags
    uint8_t depthTest = 0;
    uint8_t depthWrite = 0;
    uint8_t depthFunc = 0;       // VkCompareOp
    uint8_t stencilEnable = 0;
    uint8_t frontFunc = 0;       // VkCompareOp
    uint8_t frontFail = 0;       // VkStencilOp
    uint8_t frontDepthFail = 0;
    uint8_t frontPass = 0;
    uint8_t backFunc = 0;
    uint8_t backFail = 0;
    uint8_t backDepthFail = 0;
    uint8_t backPass = 0;
    uint8_t cullMode = 0;        // VkCullModeFlags
    uint8_t polygonMode = 0;     // VkPolygonMode
    // P4, NOT RETAIL: ext.foliage_aa's A2C clone of a non-blending state (shader_rewrites.h), set by the
    // draw: kExtAlphaToCoverage, and kExtAlphaZero (the alpha factors ZERO / ZERO while the colour keeps
    // srcBlend / dstBlend). 0 for every retail state (the byte was padding: the warm list's keys are the
    // same).
    uint8_t extFlags = 0;
    int32_t depthBias = 0;       // D3D11 DepthBias (the engine's bias * 16777215, truncated)
    float slopeBias = 0.0f;      // D3D11 SlopeScaledDepthBias (the engine's slope bias * 4096)
};
static_assert(sizeof(StateBlock) == 28, "StateBlock is hashed and written as bytes: no padding");
constexpr uint8_t kExtAlphaToCoverage = 1;
constexpr uint8_t kExtAlphaZero = 2;

// ---- pipelines ---------------------------------------------------------------------------------------
struct PipelineKey {
    uint64_t vs = 0;             // ShaderHash of the vertex shader
    uint64_t ps = 0;             // ShaderHash of the pixel shader, 0 for none
    uint64_t layout = 0;         // InternLayout hash
    StateBlock state;
    uint32_t colorFormat = 0;    // VkFormat
    uint32_t depthFormat = 0;    // VkFormat
    uint8_t topology = 0;        // VkPrimitiveTopology
    uint8_t samples = 1;
    uint8_t linked = 0;          // the pixel shader is the pair's linked conversion
    uint8_t msrtss = 0;          // P3: `samples` is the tiler's MSAA on single-sampled attachments
    // The 1.1 path (vulkan11.h): the vertex strides of streams 0..7, baked into the pipeline (no
    // VERTEX_INPUT_BINDING_STRIDE there). All 0 on the 1.3 path, where the stride is dynamic.
    uint8_t strides[8] = {};
};
static_assert(sizeof(PipelineKey) == 72, "PipelineKey is hashed as bytes: no padding");

// The pipeline for `key` (render thread, at draw time): from the cache, or built now. `vs` / `ps`
// are the bound programs' entries (ps may be null). `key.linked` is filled here. VK_NULL_HANDLE when
// the pipeline cannot be built (logged once per key). *pair: the push path's pair layouts (null on
// the one-layout path).
VkPipeline ResolvePipeline(PipelineKey& key, const ShaderEntry* vs, const ShaderEntry* ps, const PairLayout** pair);

// Counters for the frame log: pipelines built on the render thread, built by the warm-up worker,
// and failures, since the last call.
void TakePipelineStats(uint32_t* built, uint32_t* warmed, uint32_t* failed);

}  // namespace VideoVulkan
