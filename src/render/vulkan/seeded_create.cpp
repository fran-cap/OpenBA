// ============================================================================
// src/render/vulkan/seeded_create.cpp -- seeded_create.h
// ============================================================================
// NOT RETAIL (P4). The image half is the P2 body of ImageBufferData::Create's upload (one staging blob,
// each level at a 16-byte boundary, a partial set cleared first so every subresource is defined), with
// ext.compress_ao's conversion before it and ext.share_gpu_data's create-or-share around it, the order
// of src/render/metal/backend.mm PlanUploads / MakeTexture and of dx11 imagebufferdata.cpp.
// ============================================================================
#include "seeded_create.h"

#include "compress_ao/compress_ao.h"
#include "share_gpu_data/share_create.h"

#include "compress_ao/bc4_chain.h"

#include "bc_decode/bc_decode.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <vector>

namespace VideoVulkan {

namespace {

// Bytes per texel (per 4x4 block for the compressed formats): dx11's kFormatBytes.
uint32_t FormatBytes(int format) {
    static const uint32_t kBytes[8] = { 8, 16, 16, 1, 1, 4, 4, 4 };
    return (format >= 0 && format < 8) ? kBytes[format] : 4;
}

}  // namespace

bool MakeTexture(const TextureShape& s, const SourceLevel* levels, int count, bool seeded, GpuImage* out,
                 TextureMade* made) {
    *made = TextureMade{};
    *out = GpuImage{};
    ImageDesc desc;
    desc.width = s.width;
    desc.height = s.height;
    desc.mips = s.mips;
    desc.layers = s.layers;
    desc.cube = s.cube;
    desc.format = s.vkFormat;
    desc.swizzle = s.swizzle;
    desc.usage = s.usage;
    bool compressed = s.format < 3;
    uint32_t unit = FormatBytes(s.format);

    // EXT SEAM (NOT RETAIL, docs/extensions/handins/COMPRESSAO.md): ext.compress_ao -- a 2D R8 texture
    // seeded from a resource file (the baked AO maps), every level of its chain present and the top level
    // a multiple of 4 each way, is made BC4 from its levels encoded (or found in the derived cache). The
    // engine image keeps format 4; only the Vulkan format and the uploads change.
    std::vector<uint8_t> converted;   // the levels rewritten (BC4, or BC1 / BC3 decoded), back to back
    std::vector<size_t> convertedOffsets;
    bool decoded = false;
    if (seeded && CompressAo::Requested() && s.kind == 2 && s.format == 4 && !s.cube && s.layers == 1 &&
        (s.width & 3) == 0 && (s.height & 3) == 0 && count == static_cast<int>(s.mips) && count > 0) {
        std::vector<::CompressAo::Level> in(static_cast<size_t>(count));
        for (int i = 0; i < count; ++i) {
            in[static_cast<size_t>(i)].data = static_cast<const uint8_t*>(levels[i].data);
            in[static_cast<size_t>(i)].size = static_cast<int64_t>(levels[i].size);
        }
        if (::CompressAo::Encode(in.data(), count, static_cast<int>(s.width), static_cast<int>(s.height),
                                 static_cast<uint32_t>(s.format), &converted, &convertedOffsets)) {
            made->bc4 = true;
            desc.format = VK_FORMAT_BC4_UNORM_BLOCK;
            desc.swizzle = VkComponentMapping{ VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
                                               VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY };
            compressed = true;
            unit = 8;
        }
    }

    // NOT RETAIL: a device without textureCompressionBC (ImageFormat made the image RGBA8): each BC1 /
    // BC3 level decoded, a level shorter than its blocks left empty (counted short below).
    if (compressed && !made->bc4 && desc.format == VK_FORMAT_R8G8B8A8_UNORM && levels != nullptr) {
        static std::atomic<bool> logged{ false };
        if (!logged.exchange(true)) {
            std::fprintf(stderr, "[Vulkan] no textureCompressionBC: BC1 / BC3 textures decoded to RGBA8 "
                                 "at load\n");
        }
        convertedOffsets.push_back(0);
        for (int i = 0; i < count; ++i) {
            const uint32_t mip = static_cast<uint32_t>(i) % desc.mips;
            const int w = static_cast<int>(std::max(desc.width >> mip, 1u));
            const int h = static_cast<int>(std::max(desc.height >> mip, 1u));
            const size_t need = s.format == 0 ? BcDecode::Bc1Size(w, h) : BcDecode::Bc3Size(w, h);
            if (levels[i].size >= need) {
                const size_t at = converted.size();
                converted.resize(at + static_cast<size_t>(w) * h * 4);
                const auto* src = static_cast<const uint8_t*>(levels[i].data);
                if (s.format == 0) {
                    BcDecode::DecodeBC1(src, w, h, converted.data() + at, w * 4);
                } else {
                    BcDecode::DecodeBC3(src, w, h, converted.data() + at, w * 4);
                }
            }
            convertedOffsets.push_back(converted.size());
        }
        decoded = true;
        compressed = false;
        unit = 4;
    }
    const bool rewritten = made->bc4 || decoded;

    // The levels into one staging blob, each region at a 16-byte boundary (a texel / block multiple).
    std::vector<ImageRegion> regions;
    std::vector<ShareGpuData::LevelBytes> keyLevels;
    std::vector<uint8_t> blob;
    size_t at = 0;
    if (levels != nullptr && count > 0) {
        size_t total = 0;
        for (int i = 0; i < count; ++i) {
            const size_t size = rewritten ? convertedOffsets[static_cast<size_t>(i) + 1] -
                                                convertedOffsets[static_cast<size_t>(i)]
                                          : levels[i].size;
            total += (size + 15) / 16 * 16;
        }
        blob.resize(total);
        for (int i = 0; i < count; ++i) {
            const uint32_t mip = static_cast<uint32_t>(i) % desc.mips;
            const uint32_t layer = static_cast<uint32_t>(i) / desc.mips;
            if (layer >= desc.layers) break;
            const uint32_t w = std::max(desc.width >> mip, 1u);
            const uint32_t h = std::max(desc.height >> mip, 1u);
            const size_t need = compressed ? static_cast<size_t>((w + 3) / 4) * ((h + 3) / 4) * unit
                                           : static_cast<size_t>(w) * h * unit;
            const uint8_t* src = static_cast<const uint8_t*>(levels[i].data);
            size_t have = levels[i].size;
            if (rewritten) {
                src = converted.data() + convertedOffsets[static_cast<size_t>(i)];
                have = convertedOffsets[static_cast<size_t>(i) + 1] - convertedOffsets[static_cast<size_t>(i)];
            }
            if (have < need) {
                made->shortLevel = true;
                continue;
            }
            std::memcpy(blob.data() + at, src, need);
            ImageRegion r;
            r.offset = at;
            r.mip = mip;
            r.layer = layer;
            r.width = w;
            r.height = h;
            regions.push_back(r);
            keyLevels.push_back(ShareGpuData::LevelBytes{ blob.data() + at, need });
            at += (need + 15) / 16 * 16;
        }
    }
    const bool complete = !made->shortLevel && regions.size() == static_cast<size_t>(desc.mips) * desc.layers;

    const auto make = [&](GpuImage* image) {
        std::string why;
        if (!CreateImage(desc, image, &why)) {
            std::fprintf(stderr, "[Vulkan] image %ux%u format %d: %s\n", desc.width, desc.height,
                         static_cast<int>(desc.format), why.c_str());
            return false;
        }
        // No levels, or a partial set: cleared first, so every subresource is defined (resources.h).
        if (!complete) ClearImage(*image);
        if (!regions.empty()) UploadImage(*image, regions.data(), regions.size(), blob.data(), at);
        return true;
    };
    // EXT SEAM (NOT RETAIL, docs/extensions/handins/SHAREGPUDATA.md): ext.share_gpu_data -- a texture seeded
    // from a resource file (kind 2: sampled only, never a render target) with every mip of every layer
    // uploaded is made through the create-or-share.
    if (seeded && s.kind == 2 && ShareGpuData::Requested()) {
        return ShareGpuData::CreateImage(desc, keyLevels.data(), static_cast<int>(keyLevels.size()), complete, make, out,
                                         &made->shared);
    }
    return make(out);
}

bool MakeStaticBuffer(bool index, const void* data, uint32_t size, VkBufferUsageFlags usage, GpuBuffer* out,
                      bool* shared, std::string* why) {
    *shared = false;
    const auto make = [&](GpuBuffer* buffer) {
        return CreateStaticBuffer(size, usage, data, buffer, why);
    };
    // EXT SEAM (NOT RETAIL, docs/extensions/handins/SHAREGPUDATA.md): ext.share_gpu_data -- a static seeded
    // buffer equal to one already made shares it; anything else, and the row off, is the plain create.
    if (data != nullptr && ShareGpuData::Requested()) {
        return ShareGpuData::CreateBuffer(index, data, size, usage, make, out, shared);
    }
    return make(out);
}

void ReleaseBuffer(GpuBuffer* buffer, bool* shared) {
    if (*shared) {
        // The last user retires it; any other only forgets its copy.
        if (ShareGpuData::DropBuffer(*buffer)) {
            RetireBuffer(buffer);
        } else {
            *buffer = GpuBuffer{};
        }
    } else {
        RetireBuffer(buffer);
    }
    *shared = false;
}

void ReleaseImage(GpuImage* image, bool* shared) {
    if (*shared) {
        if (ShareGpuData::DropImage(*image)) {
            RetireImage(image);
        } else {
            *image = GpuImage{};
        }
    } else {
        RetireImage(image);
    }
    *shared = false;
}

}  // namespace VideoVulkan
