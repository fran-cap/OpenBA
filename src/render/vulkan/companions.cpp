// ============================================================================
// src/render/vulkan/companions.cpp -- the resource companions: buffers, images, programs, layouts, states
// ============================================================================
// NOT RETAIL. The buffer, image, program, layout and state companions of VideoVulkanData.h. The slot
// roles are the DX11 files' (src/render/dx11/buffers.cpp, imagebufferdata.cpp, shaderprograms.cpp,
// vertexlayoutdata.cpp, renderstatedata.cpp); the GPU objects come from resources.h, the pipeline
// pieces from pipelines.h, and every Bind records into the bound state of draw.h.
//
// P2 (docs/vulkan/PLAN.md §5): everything here is real. P3: a render-buffer image gets an attachment
// view and layout tracking (draw.h TARGETS / LAYOUTS), and ImageBufferData::Read reads back through
// draw.cpp (DrawReadImage).
//
// The buffers' Serialize is part of the resource stream format (a mesh's vertex and index bytes sit
// inline in the .rsc after `dynamic` and `size`): the load arm reads them and creates the buffer from
// them, exactly as the DX11 and Metal load arms do; the save arm reads the GPU buffer back.
// ============================================================================

#include "VideoVulkanData.h"
#include "device_objects.h"
#include "draw.h"
#include "vulkan11.h"                            // ForgetImageView
#include "seeded_create.h"                       // P4: the seeded creates (compress_ao, share_gpu_data)
#include "async_gpu_create/async_create.h"       // P4: ext.async_gpu_create
#include "create_on_demand/lazy_create.h"        // P4: ext.create_on_demand
#include "shader_rewrites.h"                     // P4: the DXBC rewrite stages (better_trees, draw_batch)

#include "ext/derived_cache/derived_cache.h"   // the SPIR-V cache's store (OpenShared)
#include "texture_quality/texture_quality.h"   // P4: ext.texture_quality's DropFor
#include "io/Serializer.h"
#include "video/Buffers.h"
#include "video/ConstantBuffer.h"   // ConstantBuffer::_uploadIndex
#include "video/Device.h"           // Video::Configuration::_singleton, Device::maxAnisotropy
#include "video/ImageBuffer.h"
#include "video/RenderState.h"
#include "video/ShaderProgram.h"    // ShaderProgram::_byteCode, the permutation blobs
#include "video/VertexLayout.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>

namespace VideoVulkan {

namespace {

std::atomic<uint32_t> g_logOnce{0};

void LogOnce(uint32_t bit, const char* message) {
    if (g_logOnce.fetch_or(bit, std::memory_order_relaxed) & bit) return;
    std::fprintf(stderr, "[Vulkan] %s\n", message);
}

// ---- buffers (dx11 buffers.cpp GpuBuffer_* / BufferData_*) ----

// P4: a shared static buffer drops its use (seeded_create.h ReleaseBuffer), any other is retired.
void ReleaseStore(BufferStore& store) {
    ReleaseBuffer(&store.gpu, &store.shared);
    for (BufferStore::Version& v : store.versions)
        RetireBuffer(&v.buffer);
    store.versions.clear();
    store.current = -1;
    store.writeCursor = 0;
    store.immutable = false;
}

// Slot 1 of both buffer kinds: owner->size / dynamic are stored first, as retail does; a zero size
// makes nothing. A dynamic buffer starts with one host-visible version (seeded from `data`), a static
// one is device-local and filled through the upload batch (P4: through seeded_create.h, so
// ext.share_gpu_data may hand out an equal one; seeded = IMMUTABLE, as DX11 makes it).
template <class Data>
bool CreateBuffer(Data* self, uint32_t sizeBytes, uint8_t dynamicByte, const void* data, VkBufferUsageFlags usage) {
    AsyncCreate::Await(self);       // NOT RETAIL (ext.async_gpu_create): a queued create lands first
    CreateOnDemand::Forget(self);   // NOT RETAIL (ext.create_on_demand): this create replaces a deferred one
    auto* owner = self->_owner;
    owner->size = static_cast<int32_t>(sizeBytes);
    owner->dynamic = dynamicByte;
    BufferStore& store = self->store;
    ReleaseStore(store);
    store.dynamic = dynamicByte != 0;
    if (owner->size == 0) return true;
    std::string why;
    if (store.dynamic) {
        BufferStore::Version v;
        if (!CreateHostBuffer(sizeBytes, usage, &v.buffer, &why)) {
            std::fprintf(stderr, "[Vulkan] dynamic buffer of %u bytes: %s\n", sizeBytes, why.c_str());
            return false;
        }
        if (data) std::memcpy(v.buffer.mapped, data, sizeBytes);
        store.versions.push_back(v);
        store.current = 0;
        return true;
    }
    const bool index = (usage & VK_BUFFER_USAGE_INDEX_BUFFER_BIT) != 0;
    if (!MakeStaticBuffer(index, data, sizeBytes, usage, &store.gpu, &store.shared, &why)) {
        std::fprintf(stderr, "[Vulkan] buffer of %u bytes: %s\n", sizeBytes, why.c_str());
        return false;
    }
    store.immutable = data != nullptr;
    return true;
}

// NOT RETAIL (ext.async_gpu_create): the deferred twin of CreateBuffer's static arm, for the Serialize
// load arm inside an armed loader bracket. The engine-visible stores (size, dynamic: read by the
// serializer) and the release of the old buffer happen here, on the loader, as CreateBuffer does them;
// the worker makes the buffer from its own copy of the payload and publishes it before the object
// counts as done.
template <class Data>
void CreateBufferDeferred(Data* self, std::vector<uint8_t>&& bytes, VkBufferUsageFlags usage) {
    AsyncCreate::Await(self);
    CreateOnDemand::Forget(self);
    auto* owner = self->_owner;
    BufferStore* const store = &self->store;
    ReleaseStore(*store);
    store->dynamic = false;
    const uint32_t size = static_cast<uint32_t>(owner->size);
    const bool index = (usage & VK_BUFFER_USAGE_INDEX_BUFFER_BIT) != 0;
    auto payload = std::make_shared<std::vector<uint8_t>>(std::move(bytes));
    AsyncCreate::Enqueue(self, size, [store, payload, size, usage, index]() {
        std::string why;
        GpuBuffer made;
        bool shared = false;
        if (!MakeStaticBuffer(index, payload->data(), size, usage, &made, &shared, &why)) {
            std::fprintf(stderr, "[Vulkan] buffer of %u bytes: %s\n", size, why.c_str());
            return;
        }
        store->gpu = made;
        store->shared = shared;
        store->immutable = true;
    });
}

// Slot 2: D3D11 UpdateSubresource over [offset, offset + size). A static buffer takes it through the
// upload batch (resources.h: it lands before the frame being recorded); a dynamic one is written in
// place (D3D11 never updates a DYNAMIC buffer this way; nothing in the game does).
template <class Data>
bool SetBufferData(Data* self, int32_t sizeBytes, int32_t offsetBytes, const void* src) {
    AsyncCreate::Await(self);   // NOT RETAIL (ext.async_gpu_create)
    if (self->_owner->size == 0) return true;
    if (sizeBytes <= 0 || offsetBytes < 0 || !src) return true;
    BufferStore& store = self->store;
    // P4: D3D11 refuses UpdateSubresource on the IMMUTABLE buffer a seeded static create made
    // (buffers.cpp); so does this backend (and a buffer still waiting for its first use is not made for a
    // write that would do nothing).
    if (store.immutable) return true;
    if (store.dynamic) {
        if (store.current >= 0 && offsetBytes + sizeBytes <= self->_owner->size) {
            std::memcpy(store.versions[store.current].buffer.mapped + offsetBytes, src,
                        static_cast<size_t>(sizeBytes));
        }
        return true;
    }
    UploadBuffer(store.gpu, static_cast<VkDeviceSize>(offsetBytes), src, static_cast<VkDeviceSize>(sizeBytes));
    return true;
}

// Slot 3: dx11's Lock. Past the end of the buffer it is a MAP_WRITE_DISCARD (cursor back to 0, and
// here a fresh version: the one the frames in flight read stays untouched); otherwise
// MAP_WRITE_NO_OVERWRITE at the cursor. The cursor advances by the size rounded up to even. A
// non-dynamic buffer cannot be mapped (D3D11 fails the Map): null, as retail gets.
template <class Data>
void* LockBuffer(Data* self, int32_t sizeBytes, int32_t* outByteOffset) {
    AsyncCreate::Await(self);   // NOT RETAIL (ext.async_gpu_create)
    BufferStore& store = self->store;
    const int32_t size = self->_owner->size;
    if (!store.dynamic || store.current < 0) return nullptr;
    if (store.writeCursor + sizeBytes > size) {
        const uint64_t recording = FramesSubmitted();
        store.versions[store.current].busyUntilFrame = recording;
        const uint64_t completed = FramesCompleted();
        int next = -1;
        for (size_t i = 0; i < store.versions.size(); ++i) {
            if (static_cast<int>(i) != store.current && store.versions[i].busyUntilFrame < completed) {
                next = static_cast<int>(i);
                break;
            }
        }
        if (next < 0) {
            BufferStore::Version v;
            std::string why;
            if (!CreateHostBuffer(static_cast<VkDeviceSize>(size), Data::kUsage, &v.buffer, &why)) {
                std::fprintf(stderr, "[Vulkan] dynamic buffer version of %d bytes: %s\n", size, why.c_str());
                return nullptr;
            }
            store.versions.push_back(v);
            next = static_cast<int>(store.versions.size()) - 1;
        }
        store.current = next;
        store.writeCursor = 0;
    }
    *outByteOffset = store.writeCursor;
    uint8_t* p = store.versions[store.current].buffer.mapped + store.writeCursor;
    store.writeCursor += ((sizeBytes + 1) / 2) * 2;
    return p;
}

template <class Data>
void SerializeBuffer(Data* self, IO::Serializer* serializer, VkBufferUsageFlags usage) {
    if (!serializer->GetEncoding().IsBinary()) return;
    // NOT RETAIL (ext.async_gpu_create): the save arm reads the buffer; a deferred create finishes first.
    AsyncCreate::Await(self);
    // NOT RETAIL (ext.create_on_demand): no eviction while this reads the buffer, and a buffer still
    // waiting for its first use is made first (the save arm).
    CreateOnDemand::UseScope use(self);
    const bool loading = static_cast<int>(serializer->GetDirection()) == 0;
    if (!loading && self->store.Buffer() == VK_NULL_HANDLE) CreateOnDemand::Ensure(self);
    auto* owner = self->_owner;
    serializer->Serialize(owner->dynamic, nullptr);
    serializer->Serialize(owner->size, nullptr);
    if (owner->size <= 0) return;
    if (loading) {
        // EXT SEAM (NOT RETAIL, docs/extensions/handins/CREATEONDEMAND.md): ext.create_on_demand -- a static
        // buffer read from a packed resource file is not read: its create waits for its first use and the
        // stream skips the payload. The owner already holds the size / dynamic Create would store.
        if (CreateOnDemand::Requested() && owner->dynamic == 0 && self->store.Buffer() == VK_NULL_HANDLE) {
            const bool index = (usage & VK_BUFFER_USAGE_INDEX_BUFFER_BIT) != 0;
            if (CreateOnDemand::DeferBuffer(self, serializer->_stream, index, static_cast<uint32_t>(owner->size), usage,
                                            &self->store)) {
                self->store.dynamic = false;
                self->store.immutable = true;   // what the deferred create will be (BufferRecord::Publish)
                return;
            }
        }
        std::vector<uint8_t> payload(static_cast<size_t>(owner->size), 0);
        serializer->SerializeBytes(owner->size, payload.data(), nullptr);
        // EXT SEAM (NOT RETAIL, docs/extensions/handins/ASYNCGPUCREATE.md): inside an armed loader bracket
        // a static buffer's create goes to a worker, which owns the payload.
        if (AsyncCreate::Deferring() && owner->dynamic == 0) {
            CreateBufferDeferred(self, std::move(payload), usage);
            return;
        }
        CreateBuffer(self, static_cast<uint32_t>(owner->size), owner->dynamic, payload.data(), usage);
        return;
    }
    std::vector<uint8_t> bytes(static_cast<size_t>(owner->size), 0);
    const BufferStore& store = self->store;
    if (store.dynamic && store.current >= 0) {
        std::memcpy(bytes.data(), store.versions[store.current].buffer.mapped, bytes.size());
    } else if (!ReadBuffer(store.gpu, bytes.data(), bytes.size())) {
        LogOnce(1, "a buffer save read back nothing (the buffer is gone): zeros written");
    }
    serializer->SerializeBytes(owner->size, bytes.data(), nullptr);
}

// ---- images (dx11 imagebufferdata.cpp) ----

// dx11's kFormatToDxgi, as Vulkan formats: BC1, BC3, BC3, A8, R8, BGRA8, R32F, D24S8 (or D32S8).
// NOT RETAIL: a device without textureCompressionBC (the Galaxy S10's Adreno 640) gets RGBA8 for the
// three BC formats here; MakeTexture (seeded_create.cpp) makes a seeded texture ETC2 instead when the
// device samples it (P13_MEM.md), else decodes the levels to RGBA8 (bc_decode.h).
VkFormat ImageFormat(int format) {
    const bool bc = LiveCaps().textureCompressionBC;
    switch (format) {
    case 0: return bc ? VK_FORMAT_BC1_RGBA_UNORM_BLOCK : VK_FORMAT_R8G8B8A8_UNORM;
    case 1: return bc ? VK_FORMAT_BC3_UNORM_BLOCK : VK_FORMAT_R8G8B8A8_UNORM;
    case 2: return bc ? VK_FORMAT_BC3_UNORM_BLOCK : VK_FORMAT_R8G8B8A8_UNORM;
    case 3: return VK_FORMAT_R8_UNORM;   // A8_UNORM: R8 with an alpha swizzle (no core A8)
    case 4: return VK_FORMAT_R8_UNORM;
    case 5: return VK_FORMAT_B8G8R8A8_UNORM;
    case 6: return VK_FORMAT_R32_SFLOAT;
    case 7: return DepthStencilFormat();
    default: return VK_FORMAT_UNDEFINED;
    }
}

// dx11's SamplerDescFor: the engine's filter index into a three-entry table patched by the
// configuration's texture filter and the comparison flag, wrap / clamp addressing, MaxLOD the mip count.
// `droppedLevels` (NOT RETAIL, ext.texture_quality): the top levels the image was made without; maxLod
// counts the levels it has (dx11 SamplerDescFor's same parameter; the point sampler keeps it too, as
// dx11 AcquirePointSampler takes the creation-time MaxLOD).
SamplerDesc SamplerFor(const Video::ImageBuffer* ib, bool point, int droppedLevels) {
    static const uint8_t kAddress[2] = { VK_SAMPLER_ADDRESS_MODE_REPEAT, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE };
    int aniso = Video::Configuration::_singleton ? Video::Configuration::_singleton->textureFilter : 0;
    const bool comparison = ((ib->_flags >> 6) & 1) != 0;
    // filter entry: 0 point, 1 min/mag linear mip point, 2 anisotropic (as patched below).
    enum Kind { Point, LinearMipPoint, LinearAll, Anisotropic, CompareLinear };
    Kind table[3] = { Point, LinearMipPoint, Anisotropic };
    if (comparison) {
        table[1] = CompareLinear;
        aniso = 0;
    } else if (aniso == -1) {
        table[2] = LinearMipPoint;
        aniso = 0;
    } else if (aniso == 0) {
        table[2] = LinearAll;
        aniso = 0;
    } else if (aniso < 0) {
        aniso = 0;
    }
    const Video::Device* dev = Video::Device::_singleton;
    if (dev && aniso > dev->maxAnisotropy) aniso = dev->maxAnisotropy;
    const int filter = std::clamp(static_cast<int>(ib->_filter), 0, 2);
    const Kind kind = point ? Point : table[filter];
    SamplerDesc d;
    d.magLinear = kind != Point;
    d.minLinear = kind != Point;
    d.mipLinear = kind == LinearAll || kind == Anisotropic || kind == CompareLinear;
    d.compare = kind == CompareLinear;
    d.anisotropy = kind == Anisotropic ? static_cast<uint8_t>(std::max(aniso, 0)) : 0;
    d.addressU = kAddress[ib->_addressU & 1];
    d.addressV = kAddress[ib->_addressV & 1];
    d.addressW = kAddress[ib->_addressW & 1];
    d.maxLod = (ib->_flags & 1) ? static_cast<float>(ib->GetLevelCount() - droppedLevels) - 1.0f : 0.0f;
    return d;
}

// ---- render states (dx11 renderstatedata.cpp's tables, as Vulkan values) ----
const uint8_t kBlendFactor[10] = {
    VK_BLEND_FACTOR_ZERO,                 // 0 D3D11_BLEND_ZERO
    VK_BLEND_FACTOR_ONE,                  // 1 D3D11_BLEND_ONE
    VK_BLEND_FACTOR_SRC_COLOR,            // 2 D3D11_BLEND_SRC_COLOR
    VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR,  // 3 D3D11_BLEND_INV_SRC_COLOR
    VK_BLEND_FACTOR_SRC_ALPHA,            // 4 D3D11_BLEND_SRC_ALPHA
    VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,  // 5 D3D11_BLEND_INV_SRC_ALPHA
    VK_BLEND_FACTOR_DST_COLOR,            // 6 D3D11_BLEND_DEST_COLOR
    VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR,  // 7 D3D11_BLEND_INV_DEST_COLOR
    VK_BLEND_FACTOR_DST_ALPHA,            // 8 D3D11_BLEND_DEST_ALPHA
    VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA,  // 9 D3D11_BLEND_INV_DEST_ALPHA
};
const uint8_t kBlendOp[3] = { VK_BLEND_OP_ADD, VK_BLEND_OP_SUBTRACT, VK_BLEND_OP_REVERSE_SUBTRACT };
const uint8_t kCompare[8] = {
    VK_COMPARE_OP_NEVER, VK_COMPARE_OP_ALWAYS, VK_COMPARE_OP_EQUAL, VK_COMPARE_OP_LESS,
    VK_COMPARE_OP_LESS_OR_EQUAL, VK_COMPARE_OP_GREATER, VK_COMPARE_OP_GREATER_OR_EQUAL, VK_COMPARE_OP_NOT_EQUAL,
};
// D3D11 INCR / DECR wrap; INCR_SAT / DECR_SAT clamp.
const uint8_t kStencilOp[8] = {
    VK_STENCIL_OP_KEEP, VK_STENCIL_OP_ZERO, VK_STENCIL_OP_INCREMENT_AND_WRAP, VK_STENCIL_OP_DECREMENT_AND_WRAP,
    VK_STENCIL_OP_INCREMENT_AND_CLAMP, VK_STENCIL_OP_DECREMENT_AND_CLAMP, VK_STENCIL_OP_REPLACE, VK_STENCIL_OP_INVERT,
};
const uint8_t kCull[3] = { VK_CULL_MODE_FRONT_BIT, VK_CULL_MODE_BACK_BIT, VK_CULL_MODE_NONE };
const uint8_t kFill[2] = { VK_POLYGON_MODE_FILL, VK_POLYGON_MODE_LINE };

template <size_t N>
uint8_t Pick(const uint8_t (&table)[N], int index, uint8_t fallback) {
    return (index >= 0 && static_cast<size_t>(index) < N) ? table[index] : fallback;
}

}  // namespace

// ---- ImageBufferData (AttachColorBuffer / AttachDepthBuffer are in device.cpp: they alias the
// main targets) ---------------------------------------------------------------------------

// The attachment view of a render-buffer image (retired with it: the frame being recorded may use it).
static void RetireAttachmentView(VkImageView* view) {
    if (!*view) return;
    VkImageView v = *view;
    *view = VK_NULL_HANDLE;
    Retire([v]() {
        ForgetImageView(v);
        if (LiveDevice()) vkDestroyImageView(LiveDevice(), v, nullptr);
    });
}

ImageBufferData::~ImageBufferData() {
    AsyncCreate::Await(this);       // NOT RETAIL (ext.async_gpu_create): never free under a running create
    CreateOnDemand::Forget(this);   // NOT RETAIL (ext.create_on_demand): nor under a materialization
    DrawForget(this);
    DrawForgetImage(this);
    RetireAttachmentView(&attachmentView);
    ReleaseImage(&image, &shared);   // NOT RETAIL (ext.share_gpu_data): a shared image drops its use
}

// Slot 3 (dx11 Create). Same early-out (a non-generated, non-rendered owner with no extent makes
// nothing), same shape: 2D or a cube (6 layers), one mip or the full chain (dx11 MipLevels =
// ~flags & 1, 0 meaning the full chain), the level arrays face-major as D3D11's subresource order.
// A render-buffer kind gets attachment usage (draw.h TARGETS renders into it); every image is created
// readable: uploaded, or cleared (colour 0, depth 1) when it has no levels.
bool ImageBufferData::Create(const Collection::Array<Collection::Array<uint8_t>>* levels) {
    AsyncCreate::Await(this);       // NOT RETAIL (ext.async_gpu_create): a queued create lands first
    CreateOnDemand::Forget(this);   // NOT RETAIL (ext.create_on_demand): this create replaces a deferred one
    Video::ImageBuffer* const ib = _owner;
    const int width = ib->Width();
    const int height = ib->Height();
    if (!ib->IsGenerated() && !ib->IsRendered() && (width == 0 || height == 0)) return true;
    DrawForgetImage(this);
    RetireAttachmentView(&attachmentView);
    ReleaseImage(&image, &shared);   // NOT RETAIL (ext.share_gpu_data): a shared image drops its use
    bc4 = false;
    state = ImageState{};
    const int format = static_cast<int>(ib->_format);
    const VkFormat vkFormat = ImageFormat(format);
    if (vkFormat == VK_FORMAT_UNDEFINED) {
        std::fprintf(stderr, "[Vulkan] image %p: format %d is not one retail uses\n", static_cast<void*>(ib), format);
        return false;
    }
    const int kind = static_cast<int>(ib->_kind);
    // EXT SEAM (NOT RETAIL, docs/extensions/handins/TEXQUALITY.md): ext.texture_quality -- a qualifying
    // texture seeded from a resource file is made without its `drop` largest levels (the engine's level
    // reader skipped their bytes, or the array still holds them and they are left out here): the image at
    // the first kept level's size with that many fewer mips, the levels from it, both samplers' maxLod
    // lowered by the same count (dx11 imagebufferdata.cpp Create). 0 for every other image and while the
    // row is off: retail's create.
    const int drop = (levels != nullptr && kind == 2) ? TextureQuality::DropFor(*ib, levels->GetSize()) : 0;
    cube = ib->_dimension != 0;
    TextureShape shape;
    shape.width = static_cast<uint32_t>(std::max(width >> drop, 1));
    shape.height = static_cast<uint32_t>(std::max(height >> drop, 1));
    shape.mips = ib->HasMipMaps() ? static_cast<uint32_t>(ib->GetLevelCount() - drop) : 1;
    shape.layers = cube ? 6 : 1;
    shape.cube = cube;
    shape.format = format;
    shape.kind = kind;
    shape.levelCount = ib->GetLevelCount();
    shape.vkFormat = vkFormat;
    if (format == 3) {
        shape.swizzle = { VK_COMPONENT_SWIZZLE_ZERO, VK_COMPONENT_SWIZZLE_ZERO, VK_COMPONENT_SWIZZLE_ZERO,
                          VK_COMPONENT_SWIZZLE_R };
    }
    if (ib->IsRenderBuffer()) {
        shape.usage = ib->IsDepthBuffer() ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT
                                          : VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    }
    sampler = GetSampler(SamplerFor(ib, false, drop));
    pointSampler = GetSampler(SamplerFor(ib, true, drop));

    // EXT SEAM (NOT RETAIL, docs/extensions/handins/CREATEONDEMAND.md): ext.create_on_demand -- the engine's
    // level reader skipped this image's level bytes (Video::ImageBuffer::_extLevelSource): the shape and
    // the samplers are made as for any texture; the image (after compress_ao's conversion, through
    // share_gpu_data's create-or-share) waits for its first use. The level arrays are empty.
    if (levels != nullptr && kind == 2 && CreateOnDemand::HasPendingLevels(ib)) {
        CreateOnDemand::DeferTexture(this, shape);
        return true;
    }

    // The level blobs the image is made from: ext.texture_quality's dropped ones left out (2D only).
    std::vector<SourceLevel> source;
    if (levels != nullptr) {
        for (int i = drop; i < levels->GetSize(); ++i) {
            const Collection::Array<uint8_t>& level = levels->GetData()[i];
            source.push_back(SourceLevel{ level.GetData(), static_cast<size_t>(level.GetSize()) });
        }
    }
    const bool seeded = levels != nullptr && kind == 2;

    // EXT SEAM (NOT RETAIL, docs/extensions/handins/ASYNCGPUCREATE.md): inside an armed loader bracket a
    // plain texture seeded from a resource file (kind 2: never a render target) is made on a worker, from
    // a copy of its level bytes; the samplers above are the loader's, as on DX11.
    if (seeded && AsyncCreate::Deferring() && !ib->IsRenderBuffer()) {
        auto bytes = std::make_shared<std::vector<uint8_t>>();
        std::vector<size_t> offsets;
        for (const SourceLevel& s : source) {
            offsets.push_back(bytes->size());
            const auto* p = static_cast<const uint8_t*>(s.data);
            bytes->insert(bytes->end(), p, p + s.size);
        }
        const uint64_t total = bytes->size();
        ImageBufferData* const self = this;
        AsyncCreate::Enqueue(this, total, [self, shape, bytes, offsets]() {
            std::vector<SourceLevel> copy;
            for (size_t i = 0; i < offsets.size(); ++i) {
                const size_t end = i + 1 < offsets.size() ? offsets[i + 1] : bytes->size();
                copy.push_back(SourceLevel{ bytes->data() + offsets[i], end - offsets[i] });
            }
            GpuImage made;
            TextureMade info;
            if (!MakeTexture(shape, copy.data(), static_cast<int>(copy.size()), true, &made, &info)) return;
            if (info.shortLevel) {
                LogOnce(2, "an image level holds fewer bytes than its extent needs: that level is left cleared");
            }
            self->image = made;
            self->shared = info.shared;
            self->bc4 = info.bc4;
            self->state.image = made.image;
            self->state.aspect = made.aspect;
            self->state.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        });
        return true;
    }

    // The load's create (seeded_create.h): ext.compress_ao's BC4 conversion, then ext.share_gpu_data's
    // create-or-share for a texture seeded from a resource file (kind 2), else the plain create.
    TextureMade info;
    if (!MakeTexture(shape, source.data(), static_cast<int>(source.size()), seeded, &image, &info)) return false;
    shared = info.shared;
    bc4 = info.bc4;
    if (info.shortLevel) {
        LogOnce(2, "an image level holds fewer bytes than its extent needs: that level is left cleared");
    }
    // P3: every image is tracked from here (ClearImage / UploadImage leave it SHADER_READ_ONLY in the
    // upload batch, which runs before any frame that uses it). A 2D render-buffer image also gets dx11's
    // RTV / DSV: the attachment view over mip 0 and every aspect, no swizzle (a cube gets none, as dx11
    // makes none).
    state.image = image.image;
    state.aspect = image.aspect;
    state.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    if (ib->IsRenderBuffer() && !cube) {
        VkImageViewCreateInfo vi = Make<VkImageViewCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO);
        vi.image = image.image;
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = image.format;
        vi.subresourceRange = { image.aspect, 0, 1, 0, 1 };
        const VkResult r = vkCreateImageView(LiveDevice(), &vi, nullptr, &attachmentView);
        if (r != VK_SUCCESS) {
            std::fprintf(stderr, "[Vulkan] attachment view %dx%d format %d: %s\n", width, height, format,
                         ResultName(r));
            attachmentView = VK_NULL_HANDLE;
        }
    }
    return true;
}

// Slot 4: the pixel stage's texture and sampler at `slot` (dx11 PSSetSamplers + PSSetShaderResources).
void ImageBufferData::Bind(void* context, unsigned slot) {
    (void)context;   // never read by retail either
    AsyncCreate::Await(this);   // NOT RETAIL (ext.async_gpu_create): a queued create lands first
    // NOT RETAIL (ext.create_on_demand): the first bind of a deferred texture makes it (the draw makes
    // it again after an eviction: draw.cpp); a made one pays the null test.
    CreateOnDemand::UseScope use(this);
    if (!image.view) CreateOnDemand::Ensure(this);
    DrawSetTexture(slot, this);
}

// Slot 5: the dynamic textures' refresh (dx11: Map DISCARD for a dynamic owner, UpdateSubresource
// otherwise; both replace mip 0 from `data`, `width * 4` bytes a row).
bool ImageBufferData::Update(void* data, int width, int height) {
    AsyncCreate::Await(this);                      // NOT RETAIL (ext.async_gpu_create)
    CreateOnDemand::UseScope use(this);            // NOT RETAIL (ext.create_on_demand): no eviction meanwhile
    if (!image.view) CreateOnDemand::Ensure(this);
    // NOT RETAIL (ext.share_gpu_data, ext.compress_ao): a shared image is never written (D3D11 refuses
    // its IMMUTABLE texture), and a BC4 image does not hold the R8 rows the engine would write (the Metal
    // backend's answer).
    if (shared || bc4) return false;
    if (!image.image || !data || width <= 0 || height <= 0) return true;
    const uint32_t w = std::min(static_cast<uint32_t>(width), image.width);
    const uint32_t h = std::min(static_cast<uint32_t>(height), image.height);
    std::vector<uint8_t> rows(static_cast<size_t>(w) * h * 4);
    const auto* src = static_cast<const uint8_t*>(data);
    for (uint32_t y = 0; y < h; ++y)
        std::memcpy(rows.data() + static_cast<size_t>(y) * w * 4, src + static_cast<size_t>(y) * width * 4, w * 4);
    ImageRegion r;
    r.width = w;
    r.height = h;
    UploadImage(image, &r, 1, rows.data(), rows.size());
    return true;
}

// Slot 6 (dx11 0x180002ef0: a staging copy, Map READ_WRITE, rows `width * 4` bytes apart). Under the
// device lock (the Metal backend's rule): a caller outside the frame waits for the recording thread's
// frame to end; the recording thread itself (minimap, save thumbnail, screenshot) reads inside it.
bool ImageBufferData::Read(void* dst, int width, int height) {
    AsyncCreate::Await(this);                      // NOT RETAIL (ext.async_gpu_create)
    CreateOnDemand::UseScope use(this);            // NOT RETAIL (ext.create_on_demand): no eviction meanwhile
    if (!image.view) CreateOnDemand::Ensure(this);
    Video::Device* const dev = Video::Device::_singleton;
    dev->Lock();
    const bool ok = DrawReadImage(this, dst, width, height);
    dev->Unlock();
    return ok;
}

// ---- IndexBufferData -----------------------------------------------------------------------

IndexBufferData::~IndexBufferData() {
    AsyncCreate::Await(this);       // NOT RETAIL (ext.async_gpu_create): never free under a running create
    CreateOnDemand::Forget(this);   // NOT RETAIL (ext.create_on_demand): nor under a materialization
    DrawForget(this);
    ReleaseStore(store);
}

bool IndexBufferData::Create(uint32_t sizeBytes, uint8_t dynamic, const void* data) {
    return CreateBuffer(this, sizeBytes, dynamic, data, kUsage);
}

bool IndexBufferData::SetData(int32_t sizeBytes, int32_t offsetBytes, const void* src) {
    return SetBufferData(this, sizeBytes, offsetBytes, src);
}

void* IndexBufferData::Lock(int32_t sizeBytes, int32_t* outByteOffset) {
    return LockBuffer(this, sizeBytes, outByteOffset);
}

// Host-coherent memory: nothing to flush.
void IndexBufferData::Unlock() {
}

// dx11: IASetIndexBuffer(buffer, R16_UINT, 0).
void IndexBufferData::Bind(void* context) {
    (void)context;
    AsyncCreate::Await(this);   // NOT RETAIL (ext.async_gpu_create)
    CreateOnDemand::UseScope use(this);   // NOT RETAIL (ext.create_on_demand): made at its first bind
    if (store.Buffer() == VK_NULL_HANDLE) CreateOnDemand::Ensure(this);
    DrawSetIndexBuffer(this);
}

void IndexBufferData::Serialize(IO::Serializer* serializer) {
    SerializeBuffer(this, serializer, kUsage);
}

// ---- VertexBufferData ----------------------------------------------------------------------

VertexBufferData::~VertexBufferData() {
    AsyncCreate::Await(this);       // NOT RETAIL (ext.async_gpu_create): never free under a running create
    CreateOnDemand::Forget(this);   // NOT RETAIL (ext.create_on_demand): nor under a materialization
    DrawForget(this);
    ReleaseStore(store);
}

bool VertexBufferData::Create(uint32_t sizeBytes, uint8_t dynamic, const void* data) {
    return CreateBuffer(this, sizeBytes, dynamic, data, kUsage);
}

bool VertexBufferData::SetData(int32_t sizeBytes, int32_t offsetBytes, const void* src) {
    return SetBufferData(this, sizeBytes, offsetBytes, src);
}

void* VertexBufferData::Lock(int32_t sizeBytes, int32_t* outByteOffset) {
    return LockBuffer(this, sizeBytes, outByteOffset);
}

void VertexBufferData::Unlock() {
}

// dx11: IASetVertexBuffers(startSlot, 1, buffer, stride, offset).
void VertexBufferData::Bind(void* context, uint32_t startSlot, uint32_t offsetBytes, uint32_t strideBytes) {
    (void)context;
    AsyncCreate::Await(this);   // NOT RETAIL (ext.async_gpu_create)
    CreateOnDemand::UseScope use(this);   // NOT RETAIL (ext.create_on_demand): made at its first bind
    if (store.Buffer() == VK_NULL_HANDLE) CreateOnDemand::Ensure(this);
    DrawSetVertexStream(startSlot, this, offsetBytes, strideBytes);
}

void VertexBufferData::Serialize(IO::Serializer* serializer) {
    SerializeBuffer(this, serializer, kUsage);
}

// ---- ConstantBufferData --------------------------------------------------------------------

ConstantBufferData::~ConstantBufferData() {
    DrawForget(this);
}

// Slot 1 (dx11): lay the constants out back to back (each entry's offset is the running total of the
// sizes before it; the engine reads those offsets). The storage is the frame's ring (draw.cpp).
bool ConstantBufferData::Create() {
    Video::ConstantBuffer* owner = _owner;
    int32_t running = 0;
    for (int i = 0; i < owner->_constantCount; ++i) {
        Video::ConstantBuffer::ConstantInfo& entry = owner->_constantData[i];
        entry.offset = running;
        running += entry.size;
    }
    return true;
}

// Slot 2 (dx11): UpdateSubresource of the whole bound buffer from `srcData`: register count * 16
// bytes, or for a banked buffer the rung the size picks (bankStride * (size / bankStride)). Those
// bytes are kept and copied into the frame's ring at the draw that reads them.
void ConstantBufferData::Bind(void* context, const void* srcData, int32_t first, int32_t sizeBytes) {
    (void)context;
    (void)first;   // never read by retail
    const Video::ConstantBuffer* owner = _owner;
    int32_t bytes = owner->_registerCount << 4;
    if (owner->_banked && owner->_bankStride > 0) bytes = owner->_bankStride * (sizeBytes / owner->_bankStride);
    if (bytes > static_cast<int32_t>(kConstantBufferRange)) {
        LogOnce(4, "a constant buffer larger than the descriptor range is cut to it (pipelines.h kConstantBufferRange)");
        bytes = static_cast<int32_t>(kConstantBufferRange);
    }
    if (bytes < 0) bytes = 0;
    const auto* src = static_cast<const uint8_t*>(srcData);
    shadow.assign(src, src + bytes);
    ++generation;
    if (owner->_uploadIndex >= 0) {
        DrawSetConstants(this, owner->_bindToVertex != 0, static_cast<uint32_t>(owner->_uploadIndex));
    }
}

int32_t ConstantBufferData::SlotIndexPlusOne() {
    return _owner->_uploadIndex + 1;
}

// ---- RenderBufferData (Bind / Resolve are in device.cpp with the targets) -------------------

// Retail's destructor is empty (the views belong to the images). Here the bound target names the
// owner, so a render buffer released while bound stops being the target (draw.cpp).
RenderBufferData::~RenderBufferData() {
    DrawForget(_owner);
}

bool RenderBufferData::Slot1() {
    return true;
}

// ---- VertexLayoutData ----------------------------------------------------------------------

VertexLayoutData::~VertexLayoutData() {
    DrawForget(this);
}

void VertexLayoutData::Bind(void* context) {
    (void)context;
    DrawSetLayout(this);
}

// dx11: one input element per engine element, element i as (TEXCOORD, i), format from the table,
// stream and offset from the element; nothing is made without a signature. Here the list is
// interned for the pipeline key; the locations come from the vertex shader's reflection when a
// pipeline is built (pipelines.cpp BuildVertexInput).
void VertexLayoutData::PlatformCreate() {
    const Video::VertexLayout* owner = _owner;
    layoutHash = 0;
    streamMask = 0;
    if (owner->_signature.GetSize() == 0) return;
    int count = owner->_elements.GetSize();
    if (count > 64) {
        std::fprintf(stderr, "[Vulkan] vertex layout: %d elements exceeds the original's 64; clamping\n", count);
        count = 64;
    }
    std::vector<LayoutElement> elements(static_cast<size_t>(count));
    const Video::VertexLayout::Element* elems = owner->_elements.GetData();
    for (int i = 0; i < count; ++i) {
        elements[i].format = static_cast<uint8_t>(elems[i].format);
        elements[i].stream = static_cast<uint8_t>(elems[i].stream);
        elements[i].offset = static_cast<uint16_t>(elems[i].offset);
        if (elems[i].stream >= 0 && elems[i].stream < 16) streamMask |= 1u << elems[i].stream;
    }
    layoutHash = InternLayout(elements.data(), elements.size());
}

// ---- the programs (P1) -------------------------------------------------------------------------
// The slot roles are src/render/dx11/shaderprograms.cpp's: PlatformCreate makes one shader per
// permutation blob the owner has (Video::ShaderProgram::_byteCode, at most four), SKIPPING the
// permutations past the owner's count rather than stopping, and returns false on the first failure
// with the shaders already made left in place (the destructor releases them). Bind keeps the DX11
// fallback: the requested permutation when it was built, else permutation 0, with no bounds check
// on the request (retail has none; a clamp would hide a caller bug).
//
// A blob goes through the shader registry (pipelines.h) first: a blob another program already
// registered is shared, not converted again. Otherwise Shaders::LoadOrConvert (shader_convert.h): the
// user's derived-data cache (<Documents>\Banished\Cache; none under the faithful profile, which reads
// and writes no cache: every boot then converts), else dxbc-spirv, stored for the next start.

namespace {

std::atomic<unsigned> g_programModules{0};    // modules made since the last summary
std::atomic<unsigned> g_programCacheHits{0};
std::atomic<unsigned> g_programShared{0};
std::atomic<unsigned> g_programFailures{0};
std::atomic<unsigned> g_programLogLines{0};

template <class Data>
bool CreateProgram(Data* self, Shaders::Stage stage) {
    const Video::ShaderProgram* owner = self->_owner;
    Ext::DerivedCache::Store* const cache = Ext::DerivedCache::OpenShared();
    for (int i = 0; i < kPermutationCount; ++i) {
        if (i >= owner->_byteCode.GetSize()) continue;
        const Collection::Array<uint8_t>& blob = owner->_byteCode.GetData()[i];
        // The retail permutation. ext.better_trees' foliage_aa / billboard_shadow rewrites and
        // ext.draw_batch's particle VS rewrite make their copies from these same bytes, converted the same
        // way into a second registry entry per permutation (shader_rewrites.h, after this loop).
        const uint8_t* bytes = blob.GetData();
        const size_t size = static_cast<size_t>(blob.GetSize());
        const uint64_t hash = ShaderHash(bytes, size, stage);
        ReleaseShader(self->shaders[i]);   // a second PlatformCreate replaces, it does not leak
        self->shaders[i] = AcquireShader(hash);
        if (self->shaders[i]) {
            g_programShared.fetch_add(1, std::memory_order_relaxed);
            continue;
        }
        std::vector<uint32_t> spirv;
        Shaders::ShaderReflection reflection;
        bool hit = false;
        std::string why;
        Shaders::ConvertOptions options;
        options.combineSamplers = true;   // P8: the game programs are combined (shader_convert.h)
        if (!Shaders::LoadOrConvert(cache, bytes, size, stage, &spirv, &reflection, &hit, &why, options)) {
            g_programFailures.fetch_add(1, std::memory_order_relaxed);
            std::fprintf(stderr, "[Vulkan] %s program %p permutation %d (%zu bytes): %s\n", Shaders::StageName(stage),
                         static_cast<const void*>(owner), i, size, why.c_str());
            return false;
        }
        VkShaderModule module = CreateShaderModule(spirv, &why);
        if (module == VK_NULL_HANDLE) {
            g_programFailures.fetch_add(1, std::memory_order_relaxed);
            std::fprintf(stderr, "[Vulkan] %s program %p permutation %d: %s\n", Shaders::StageName(stage),
                         static_cast<const void*>(owner), i, why.c_str());
            return false;
        }
        self->shaders[i] = AddShader(hash, stage, module, reflection, bytes, size);
        g_programModules.fetch_add(1, std::memory_order_relaxed);
        if (hit) g_programCacheHits.fetch_add(1, std::memory_order_relaxed);
        if (g_programLogLines.fetch_add(1, std::memory_order_relaxed) < 4) {
            std::fprintf(stderr, "[Vulkan] %s program permutation %d: %s, %zu SPIR-V words, cb 0x%x tex 0x%x smp 0x%x\n",
                         Shaders::StageName(stage), i, hit ? "cache" : "converted", spirv.size(),
                         reflection.constantBufferMask, reflection.textureMask, reflection.samplerMask);
        }
    }
    return true;
}

template <class Data>
void BindProgram(Data* self, int permutation) {
    self->boundPermutation = self->shaders[permutation] != nullptr ? permutation : 0;
}

template <class Data>
void DestroyProgram(Data* self) {
    DrawForget(self);
    ReleaseShader(self->rewritten);   // P4: the copy the last Bind chose (shader_rewrites.h)
    self->rewritten = nullptr;
    for (int i = 0; i < kPermutationCount; ++i) {
        ReleaseShader(self->shaders[i]);
        self->shaders[i] = nullptr;
    }
}

}  // namespace

void LogProgramSummary() {
    const unsigned made = g_programModules.exchange(0, std::memory_order_relaxed);
    const unsigned hits = g_programCacheHits.exchange(0, std::memory_order_relaxed);
    const unsigned shared = g_programShared.exchange(0, std::memory_order_relaxed);
    const unsigned failed = g_programFailures.exchange(0, std::memory_order_relaxed);
    if (made == 0 && failed == 0 && shared == 0) return;
    std::fprintf(stderr,
                 "[Vulkan] programs: %u shader module(s), %u from the cache, %u converted, %u shared, %u failed\n",
                 made, hits, made - hits, shared, failed);
}

VertexProgramData::~VertexProgramData() {
    Rewrites::VertexDestroyed(this);   // NOT RETAIL (P4): the rewrite stages forget this program
    DestroyProgram(this);
}

bool VertexProgramData::PlatformCreate() {
    if (!CreateProgram(this, Shaders::Stage::Vertex)) return false;
    // EXT SEAM (NOT RETAIL, P4): ext.draw_batch / ext.better_trees make this program's copies now when on.
    if (Rewrites::Active()) Rewrites::VertexCreated(this);
    return true;
}

void VertexProgramData::Bind(void* context, int permutation) {
    (void)context;   // the DX11 slot ignores it too (shaderprograms.cpp header)
    BindProgram(this, permutation);
    // EXT SEAM (NOT RETAIL, P4): the rewritten copy the draw uses instead, when a stage has one
    // (shader_rewrites.h); one relaxed load while every stage is off.
    ReleaseShader(rewritten);
    rewritten = Rewrites::Active() ? Rewrites::BindVertex(this, boundPermutation) : nullptr;
    DrawSetVertexProgram(this);
}

PixelProgramData::~PixelProgramData() {
    Rewrites::PixelDestroyed(this);   // NOT RETAIL (P4)
    DestroyProgram(this);
}

bool PixelProgramData::PlatformCreate() {
    if (!CreateProgram(this, Shaders::Stage::Pixel)) return false;
    if (Rewrites::Active()) Rewrites::PixelCreated(this);   // EXT SEAM (NOT RETAIL, P4): foliage_aa
    return true;
}

void PixelProgramData::Bind(void* context, int permutation) {
    (void)context;
    BindProgram(this, permutation);
    // EXT SEAM (NOT RETAIL, P4): foliage_aa's copy (shader_rewrites.h).
    ReleaseShader(rewritten);
    coverage = false;
    rewritten = Rewrites::Active() ? Rewrites::BindPixel(this, boundPermutation, &coverage) : nullptr;
    DrawSetPixelProgram(this);
}

// ---- RenderStateData -----------------------------------------------------------------------

RenderStateData::~RenderStateData() {
    DrawForget(this);
}

// dx11 Create: the blend (one render target; colour and alpha from the same factors), rasterizer
// and depth-stencil descriptions from the engine's state, through renderstatedata.cpp's tables.
// D3D11 REJECTS a blend description whose alpha factors end in _COLOR (CreateBlendState fails, so
// retail binds a NULL blend state: blending off, every channel written); that outcome is kept.
bool RenderStateData::Create() {
    const Video::RenderState* rs = _owner;
    StateBlock s;
    const int src = rs->_alphaBlend.srcBlend;
    const int dst = rs->_alphaBlend.dstBlend;
    const auto colorFactor = [](int f) {
        return f == 2 || f == 3 || f == 6 || f == 7;
    };
    if (colorFactor(src) || colorFactor(dst)) {
        LogOnce(8, "a render state blends alpha with a _COLOR factor: D3D11 rejects it, so it draws unblended "
                   "as on DX11");
        s.blendEnable = 0;
        s.srcBlend = VK_BLEND_FACTOR_ONE;
        s.dstBlend = VK_BLEND_FACTOR_ZERO;
        s.blendOp = VK_BLEND_OP_ADD;
        s.writeMask = 0xF;
    } else {
        s.blendEnable = rs->_alphaBlend.enabled != 0;
        s.srcBlend = Pick(kBlendFactor, src, VK_BLEND_FACTOR_ONE);
        s.dstBlend = Pick(kBlendFactor, dst, VK_BLEND_FACTOR_ZERO);
        s.blendOp = Pick(kBlendOp, rs->_alphaBlend.blendOp, VK_BLEND_OP_ADD);
        s.writeMask = static_cast<uint8_t>(static_cast<uint8_t>(rs->_alphaBlend.writeMask) & 0x0Fu);
    }
    s.polygonMode = Pick(kFill, rs->_fillMode, VK_POLYGON_MODE_FILL);
    s.cullMode = Pick(kCull, rs->_cullMode, VK_CULL_MODE_BACK_BIT);
    s.depthBias = static_cast<int32_t>(rs->_depth.bias * 16777215.0f);
    s.slopeBias = rs->_depth.slopeBias * 4096.0f;
    s.depthTest = rs->_depth.test != 0;
    s.depthWrite = rs->_depth.write != 0;
    s.depthFunc = Pick(kCompare, rs->_depth.func, VK_COMPARE_OP_LESS_OR_EQUAL);
    s.stencilEnable = rs->_stencil.enabled != 0;
    s.frontFail = Pick(kStencilOp, rs->_stencil.failOp, VK_STENCIL_OP_KEEP);
    s.frontDepthFail = Pick(kStencilOp, rs->_stencil.depthFailOp, VK_STENCIL_OP_KEEP);
    s.frontPass = Pick(kStencilOp, rs->_stencil.passOp, VK_STENCIL_OP_KEEP);
    s.frontFunc = Pick(kCompare, rs->_stencil.func, VK_COMPARE_OP_ALWAYS);
    if (!rs->_stencil.twoSided) {
        s.backFail = VK_STENCIL_OP_KEEP;
        s.backDepthFail = VK_STENCIL_OP_KEEP;
        s.backPass = VK_STENCIL_OP_KEEP;
        s.backFunc = VK_COMPARE_OP_ALWAYS;
    } else {
        s.backFail = Pick(kStencilOp, rs->_stencil.backFailOp, VK_STENCIL_OP_KEEP);
        s.backDepthFail = Pick(kStencilOp, rs->_stencil.backDepthFailOp, VK_STENCIL_OP_KEEP);
        s.backPass = Pick(kStencilOp, rs->_stencil.backPassOp, VK_STENCIL_OP_KEEP);
        s.backFunc = Pick(kCompare, rs->_stencil.func, VK_COMPARE_OP_ALWAYS);
    }
    block = s;
    stencilReference = rs->_stencil.reference;
    scissorEnable = rs->_flag70 != 0;
    return true;
}

// dx11 Bind: OMSetBlendState + OMSetDepthStencilState(reference) + RSSetState.
void RenderStateData::Bind(void* context) {
    (void)context;
    DrawSetRenderState(this);
}

// ---- VideoProgramData (dx11: Slot1 true, Bind inert) ---------------------------------------

bool VideoProgramData::Slot1() {
    return true;
}

void VideoProgramData::Bind(void* context, int detail) {
    (void)context;
    (void)detail;
}

}  // namespace VideoVulkan
