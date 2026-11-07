#pragma once
// ============================================================================
// src/render/vulkan/VideoVulkanData.h -- the Vulkan backend's twelve companion classes
// ============================================================================
// NOT RETAIL. The Vulkan subclasses of the runtime's abstract Video::*Data bases
// (src/engine/video/CompanionData.h): the eleven objects Video::Device::Create*Data hands
// out plus the DeviceData itself. Same shape as src/render/metal/VideoMetalData.h; the
// bodies are device.cpp (DeviceData, ContextData, the render-target attach slots) and
// companions.cpp (buffers, images, programs, layouts, states).
//
// P0 (docs/vulkan/PLAN.md §5): the device, swap chain and frame loop are real. P1: the two
// program classes convert their DXBC to SPIR-V shader modules. P2: buffers, images, layouts,
// states and the draws are real (resources.h, pipelines.h, draw.h); P3: the render buffers
// (draw.h TARGETS) and the image read-back. No retail layout constraints apply to a backend
// subclass's own members (the base pins the two words every companion starts with).
// ============================================================================

#include "vk_api.h"
#include "pipelines.h"
#include "resources.h"
#include "shader_convert.h"
#include "video/CompanionData.h"
#include "create_on_demand/lazy_table.h"   // LoadPublished: ext.create_on_demand's published handles

#include <cstdint>
#include <vector>

namespace VideoVulkan {

class DeviceData final : public Video::DeviceData {
public:
    explicit DeviceData(Owner* o) : Video::DeviceData(o) {}
    ~DeviceData() override { Shutdown(); }
    bool EnumerateInterfaces() override;
    bool CreateDevice() override;
    void Shutdown() override;
    void Slot4(bool loading) override;
    float Slot5() override;
    bool Slot6() override;
    bool Slot7() override;
    void SetImageBuffer(Video::ImageBuffer* imageBuffer, int hotX, int hotY) override;
    Video::ContextData* CreateContextData(Video::Context* owner) override;
    Video::ImageBufferData* CreateImageBufferData(Video::ImageBuffer* owner) override;
    Video::ConstantBufferData* CreateConstantBufferData(Video::ConstantBuffer* owner) override;
    Video::IndexBufferData* CreateIndexBufferData(Video::IndexBuffer* owner) override;
    Video::VertexBufferData* CreateVertexBufferData(Video::VertexBuffer* owner) override;
    Video::VertexLayoutData* CreateVertexLayoutData(Video::VertexLayout* owner) override;
    Video::VertexProgramData* CreateVertexProgramData(Video::VertexProgram* owner) override;
    Video::PixelProgramData* CreatePixelProgramData(Video::PixelProgram* owner) override;
    Video::RenderStateData* CreateRenderStateData(Video::RenderState* owner) override;
    Video::VideoProgramData* CreateVideoProgramData(Video::VideoProgram* owner) override;
    Video::RenderBufferData* CreateRenderBufferData(Video::RenderBuffer* owner) override;
    // SetImageBuffer's cache (dx11 DeviceData +0x50 / +0x58 / +0x5c).
    Video::ImageBuffer* cursorImage = nullptr;
    int cursorHotX = 0;
    int cursorHotY = 0;
};

class ContextData final : public Video::ContextData {
public:
    explicit ContextData(Owner* o) : Video::ContextData(o) {}
    ~ContextData() override = default;
    bool BeginCommands() override;
    void EndCommands() override;
    void Finish() override;
    void Clear(Video::ClearFlags flags, unsigned color, float depth, int stencil) override;
    void BindRenderBuffer() override;
    void BindViewport() override;
    void BindScissor() override;
    void BindTransforms() override;
    void BindTextureNone(int slot) override;
    void BindIndexNone() override;
    void BindVertexNone(int slot) override;
    void DrawIndexed(int primitiveType, int baseVertex, int vertexCount, int startIndex, int indexCount,
                     int instanceCount) override;
    int GetMaxDynamicInstances(int a, int b, int c) override;
    bool BeginVertices(int vertexCount, int vertexStride) override;
    void EndVertices(int primitiveType) override;
    bool BeginIndexedVertices(int vertexCount, int vertexStride, int indexCount) override;
    void EndIndexedVertices(int primitiveType) override;
    bool BeginInstancedIndexedVertices(int vertexCount, int vertexStride, int indexCount,
                                       int instanceCount) override;
    void EndInstancedIndexedVertices(int primitiveType) override;
    // Issue #253 (instance_bank.h): the whole transform bank of a batch wider than the Context's 52
    // slots, kVulkanInstanceCap matrices once the first wide slot is written. Slots 52.. are written by
    // InstanceBank::SetWide; BindTransforms copies the Context's 0..51 in front before the upload.
    std::vector<uint8_t> wideBank;
};

class ImageBufferData final : public Video::ImageBufferData {
public:
    explicit ImageBufferData(Owner* o) : Video::ImageBufferData(o) {}
    ~ImageBufferData() override;
    void AttachColorBuffer() override;
    void AttachDepthBuffer() override;
    bool Create(const Collection::Array<Collection::Array<uint8_t>>* levels) override;
    void Bind(void* context, unsigned slot) override;
    bool Update(void* data, int width, int height) override;
    bool Read(void* dst, int width, int height) override;
    // P2: the texture (resources.h), its samplers (retail's and ext point sampling's), and whether the
    // owner is one of the main targets (the swap-chain image / the backend's depth image, which are
    // not sampled: a Bind of one binds the placeholder).
    GpuImage image;
    bool cube = false;
    VkSampler sampler = VK_NULL_HANDLE;
    VkSampler pointSampler = VK_NULL_HANDLE;
    enum class MainTarget : uint8_t { None, Color, Depth };
    MainTarget mainTarget = MainTarget::None;
    // P3, a render-buffer image (ImageBuffer::IsRenderBuffer, 2D): the attachment view (mip 0, every
    // aspect, no swizzle: dx11's RTV / DSV) and the layout tracking (draw.cpp).
    VkImageView attachmentView = VK_NULL_HANDLE;
    ImageState state;
    // P4 (seeded_create.h): the image is ext.share_gpu_data's (never written, released through the
    // table), or ext.compress_ao made it BC4 (the engine still says R8). Both refuse Update.
    bool shared = false;
    bool bc4 = false;
};

// The storage of a vertex or index buffer (companions.cpp). A static buffer is one device-local
// buffer filled through the upload batch. A dynamic one (D3D11_USAGE_DYNAMIC) is host-visible and
// RENAMED the way D3D11's MAP_WRITE_DISCARD renames: a Lock past the end starts a fresh version and
// the old one goes back to the pool once every frame that used it completed; NO_OVERWRITE Locks
// append to the current version (the dx11 writeCursor rule).
struct BufferStore {
    struct Version {
        GpuBuffer buffer;
        uint64_t busyUntilFrame = 0;   // the last frame index that may read it
    };
    GpuBuffer gpu;                     // static
    std::vector<Version> versions;     // dynamic: versions[current] is the one being written
    int current = -1;
    int32_t writeCursor = 0;
    bool dynamic = false;
    // P4. A static buffer created with data is D3D11_USAGE_IMMUTABLE on DX11 (buffers.cpp): its SetData
    // (UpdateSubresource) does nothing there, and here. `shared`: ext.share_gpu_data's (seeded_create.h).
    bool immutable = false;
    bool shared = false;
    VkBuffer Buffer() const {
        return dynamic ? (current >= 0 ? versions[current].buffer.buffer : VK_NULL_HANDLE) : gpu.buffer;
    }
    // ext.create_on_demand: Buffer() with the static handle read by an acquire load, for the tests
    // a prefetch worker's Publish may race (create_on_demand/lazy_table.h LoadPublished).
    VkBuffer PublishedBuffer() const {
        return dynamic ? Buffer() : ::CreateOnDemand::LoadPublished(gpu.buffer);
    }
};

class IndexBufferData final : public Video::IndexBufferData {
public:
    explicit IndexBufferData(Owner* o) : Video::IndexBufferData(o) {}
    static constexpr VkBufferUsageFlags kUsage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    ~IndexBufferData() override;
    bool Create(uint32_t sizeBytes, uint8_t dynamic, const void* data) override;
    bool SetData(int32_t sizeBytes, int32_t offsetBytes, const void* src) override;
    void* Lock(int32_t sizeBytes, int32_t* outByteOffset) override;
    void Unlock() override;
    void Bind(void* context) override;
    void Serialize(IO::Serializer* serializer) override;
    BufferStore store;
};

class VertexBufferData final : public Video::VertexBufferData {
public:
    explicit VertexBufferData(Owner* o) : Video::VertexBufferData(o) {}
    static constexpr VkBufferUsageFlags kUsage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    ~VertexBufferData() override;
    bool Create(uint32_t sizeBytes, uint8_t dynamic, const void* data) override;
    bool SetData(int32_t sizeBytes, int32_t offsetBytes, const void* src) override;
    void* Lock(int32_t sizeBytes, int32_t* outByteOffset) override;
    void Unlock() override;
    void Bind(void* context, uint32_t startSlot, uint32_t offsetBytes, uint32_t strideBytes) override;
    void Serialize(IO::Serializer* serializer) override;
    BufferStore store;
};

class ConstantBufferData final : public Video::ConstantBufferData {
public:
    explicit ConstantBufferData(Owner* o) : Video::ConstantBufferData(o) {}
    ~ConstantBufferData() override;
    bool Create() override;
    void Bind(void* context, const void* srcData, int32_t first, int32_t sizeBytes) override;
    int32_t SlotIndexPlusOne() override;
    // P2: the bytes the last Bind uploaded (what D3D11's UpdateSubresource left in the bound rung);
    // the draw copies them into the frame's constant ring (draw.cpp).
    std::vector<uint8_t> shadow;
    uint32_t generation = 0;   // bumped by every Bind
};

class RenderBufferData final : public Video::RenderBufferData {
public:
    explicit RenderBufferData(Owner* o) : Video::RenderBufferData(o) {}
    ~RenderBufferData() override;
    bool Slot1() override;
    void Bind(void* context) override;
    void Resolve(void* context, Video::RenderBuffer* other, bool flag) override;
};

class VertexLayoutData final : public Video::VertexLayoutData {
public:
    explicit VertexLayoutData(Owner* o) : Video::VertexLayoutData(o) {}
    ~VertexLayoutData() override;
    void Bind(void* context) override;
    void PlatformCreate() override;
    // P2: the element list interned for the pipeline key (pipelines.h) and the streams it reads.
    uint64_t layoutHash = 0;
    uint32_t streamMask = 0;
};

// The programs (P1): the owner's DXBC permutation blobs (Video::ShaderProgram::_byteCode, up to four,
// the DX11 `variants` rule of src/render/dx11/shaderprograms.cpp) converted to SPIR-V
// (shader_convert.h, through the derived-data cache), one shader-registry entry (pipelines.h: one
// VkShaderModule per distinct blob, with its reflection) per permutation the program has. Bind
// records the permutation and makes the program the current one of its stage (draw.cpp).
constexpr int kPermutationCount = 4;

// One log line with the modules made, served from the cache and failed since the last call
// (nothing when none); the loader bracket's end calls it (DeviceData::Slot4). companions.cpp.
void LogProgramSummary();

class VertexProgramData final : public Video::VertexProgramData {
public:
    explicit VertexProgramData(Owner* o) : Video::VertexProgramData(o) {}
    ~VertexProgramData() override;
    bool PlatformCreate() override;
    void Bind(void* context, int permutation) override;
    ShaderEntry* shaders[kPermutationCount] = {};
    int boundPermutation = 0;   // the last Bind's choice: the request when built, else 0
    // P4 (shader_rewrites.h): the rewritten copy the last Bind chose for the draw (draw_batch's or
    // billboard_shadow's), one registry reference held; null = the retail permutation.
    ShaderEntry* rewritten = nullptr;
};

class PixelProgramData final : public Video::PixelProgramData {
public:
    explicit PixelProgramData(Owner* o) : Video::PixelProgramData(o) {}
    ~PixelProgramData() override;
    bool PlatformCreate() override;
    void Bind(void* context, int permutation) override;
    ShaderEntry* shaders[kPermutationCount] = {};
    int boundPermutation = 0;
    // P4 (shader_rewrites.h): foliage_aa's copy the last Bind chose, one reference held, and whether it
    // needs the A2C clone of a non-blending state (draw.cpp).
    ShaderEntry* rewritten = nullptr;
    bool coverage = false;
    // Issue #253: `rewritten` made without natural growth's blossom (one reference held), or null; the draw
    // uses it while Rewrites::DrawBloom() is false.
    ShaderEntry* rewrittenNoBloom = nullptr;
};

class RenderStateData final : public Video::RenderStateData {
public:
    explicit RenderStateData(Owner* o) : Video::RenderStateData(o) {}
    ~RenderStateData() override;
    bool Create() override;
    void Bind(void* context) override;
    // P2: the pipeline half (pipelines.h StateBlock) and the two dynamic pieces.
    StateBlock block;
    uint8_t stencilReference = 0;
    bool scissorEnable = false;
};

class VideoProgramData final : public Video::VideoProgramData {
public:
    explicit VideoProgramData(Owner* o) : Video::VideoProgramData(o) {}
    ~VideoProgramData() override = default;
    bool Slot1() override;
    void Bind(void* context, int detail) override;
};

}  // namespace VideoVulkan
