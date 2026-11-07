#pragma once
// Carbon declares a global Collection typedef; keep it separate from our namespace.
#define Collection AppleCarbonCollection
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#undef Collection
#include "video/CompanionData.h"
#include <vector>
#include <string>
#include "airconv_abi.h"
namespace VideoMetal {
struct Shader {
    std::string identity;
    id<MTLLibrary> library = nil;
    id<MTLFunction> function = nil;
    MTL_SHADER_REFLECTION reflection{};
    std::vector<MTL_SM50_SHADER_ARGUMENT> constants, arguments;
};
Shader Translate(const std::vector<uint8_t>& bytes, id<MTLDevice> device);
void RegisterPipelineShader(const Shader& shader);
// Drops every fast-cache pipeline whose key names this object address (call from the owner's destructor / before it is
// replaced).
void EvictPipelineOwner(const void* owner);
}  // namespace VideoMetal
// Metal objects use ARC. No retail layout constraints apply to backend tails.
namespace VideoMetal {
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
    bool BeginInstancedIndexedVertices(int vertexCount, int vertexStride, int indexCount, int instanceCount) override;
    void EndInstancedIndexedVertices(int primitiveType) override;
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
    id<MTLTexture> texture = nil;
    id<MTLSamplerState> sampler = nil;
    // Point-filtered twin of `sampler`, built on first use: sharp_ui draws UI batches with
    // Video::ImageBufferData::_extPointSampling.
    id<MTLSamplerState> pointSampler = nil;
    // NOT RETAIL (ext.share_gpu_data, share_gpu_data/share_create.h): `texture` is in the share table; this
    // user drops itself before letting go of it, and Update refuses it.
    bool shared = false;
};
class IndexBufferData final : public Video::IndexBufferData {
public:
    explicit IndexBufferData(Owner* o) : Video::IndexBufferData(o) {}
    ~IndexBufferData() override;
    bool Create(uint32_t sizeBytes, uint8_t dynamic, const void* data) override;
    bool SetData(int32_t sizeBytes, int32_t offsetBytes, const void* src) override;
    void* Lock(int32_t sizeBytes, int32_t* outByteOffset) override;
    void Unlock() override;
    void Bind(void* context) override;
    void Serialize(IO::Serializer* serializer) override;
    id<MTLBuffer> buffer = nil;
    size_t cursor = 0;
    uint64_t generation = ~uint64_t(0);
    // NOT RETAIL (ext.share_gpu_data, share_gpu_data/share_create.h): `buffer` is in the share table; this
    // user drops itself before letting go of it, and SetData / Lock refuse it.
    bool shared = false;
};
class VertexBufferData final : public Video::VertexBufferData {
public:
    explicit VertexBufferData(Owner* o) : Video::VertexBufferData(o) {}
    ~VertexBufferData() override;
    bool Create(uint32_t sizeBytes, uint8_t dynamic, const void* data) override;
    bool SetData(int32_t sizeBytes, int32_t offsetBytes, const void* src) override;
    void* Lock(int32_t sizeBytes, int32_t* outByteOffset) override;
    void Unlock() override;
    void Bind(void* context, uint32_t startSlot, uint32_t offsetBytes, uint32_t strideBytes) override;
    void Serialize(IO::Serializer* serializer) override;
    id<MTLBuffer> buffer = nil;
    size_t cursor = 0;
    uint64_t generation = ~uint64_t(0);
    // NOT RETAIL (ext.share_gpu_data, share_gpu_data/share_create.h): `buffer` is in the share table; this
    // user drops itself before letting go of it, and SetData / Lock refuse it.
    bool shared = false;
};
class ConstantBufferData final : public Video::ConstantBufferData {
public:
    explicit ConstantBufferData(Owner* o) : Video::ConstantBufferData(o) {}
    ~ConstantBufferData() override = default;
    bool Create() override;
    void Bind(void* context, const void* srcData, int32_t first, int32_t sizeBytes) override;
    int32_t SlotIndexPlusOne() override;
    id<MTLBuffer> buffer = nil;
};
class RenderBufferData final : public Video::RenderBufferData {
public:
    explicit RenderBufferData(Owner* o) : Video::RenderBufferData(o) {}
    ~RenderBufferData() override = default;
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
    MTLVertexDescriptor* descriptor = nil;
};
class VertexProgramData final : public Video::VertexProgramData {
public:
    explicit VertexProgramData(Owner* o) : Video::VertexProgramData(o) {}
    ~VertexProgramData() override;
    bool PlatformCreate() override;
    void Bind(void* context, int permutation) override;
    std::vector<std::vector<uint8_t>> bytecode;
    std::vector<Shader> shaders;
};
class PixelProgramData final : public Video::PixelProgramData {
public:
    explicit PixelProgramData(Owner* o) : Video::PixelProgramData(o) {}
    ~PixelProgramData() override;
    bool PlatformCreate() override;
    void Bind(void* context, int permutation) override;
    std::vector<std::vector<uint8_t>> bytecode;
    std::vector<Shader> shaders;
};
class RenderStateData final : public Video::RenderStateData {
public:
    explicit RenderStateData(Owner* o) : Video::RenderStateData(o) {}
    ~RenderStateData() override;
    bool Create() override;
    void Bind(void* context) override;
    id<MTLDepthStencilState> depthState = nil;
    MTLRenderPipelineColorAttachmentDescriptor* blend = nil;
};
class VideoProgramData final : public Video::VideoProgramData {
public:
    explicit VideoProgramData(Owner* o) : Video::VideoProgramData(o) {}
    ~VideoProgramData() override = default;
    bool Slot1() override;
    void Bind(void* context, int detail) override;
};
bool Enumerate(Video::Device*, const char*);
void Present();
bool DeviceLive();
// The backend's Metal device, nil before CreateDevice and after Shutdown.
id<MTLDevice> LiveDevice();
unsigned CurrentSamples();
bool DepthReadable();
}  // namespace VideoMetal
