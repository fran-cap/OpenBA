// ============================================================================
// src/render/vulkan/compress_ao/compress_ao.cpp -- ext.compress_ao, the Vulkan side (NOT RETAIL)
// ============================================================================
// compress_ao.h has the contract; docs/extensions/handins/COMPRESSAO.md. The Metal body's shape
// (src/render/metal/compress_ao/bc4_create.mm): the request is refused on a device without BC4.
// ============================================================================
#include "compress_ao/compress_ao.h"

#include "device_objects.h"
#include "resources.h"
#include "vulkan_entry.h"

#include "compress_ao/bc4_chain.h"
#include "compress_ao/compress_ao_backend.h"

#include <atomic>
#include <cstdio>

namespace VideoVulkan {
namespace CompressAo {

namespace {
std::atomic<bool> g_requested{false};
}  // namespace

bool Requested() {
    return g_requested.load(std::memory_order_relaxed);
}

}  // namespace CompressAo
}  // namespace VideoVulkan

// ---- the backend-neutral interface (compress_ao_backend.h) ----------------------------------------
namespace CompressAoBackend {
BANISHED_RENDER_IMPL_BEGIN

void Request(bool on, Ext::DerivedCache::Store* cache) {
    if (on && !Supported()) on = false;
    ::CompressAo::SetCache(on ? cache : nullptr);
    VideoVulkan::CompressAo::g_requested.store(on, std::memory_order_relaxed);
    std::fprintf(stderr, "[CompressAo] %s (derived cache %s)\n", on ? "on: R8 AO maps load as BC4" : "off",
                 on && cache != nullptr ? "on" : "off");
}

// BC4 needs textureCompressionBC and the format sampleable on the live device (every desktop GPU, the
// Adreno 8xx: docs/vulkan/PLAN.md §2.1). Asked at boot, after Engine::Startup made the device; the
// reason for a "no" is logged once.
bool Supported() {
    static std::atomic<bool> logged{false};
    const char* why = nullptr;
    if (!VideoVulkan::DeviceLive()) {
        why = "no live Vulkan device";
    } else if (!VideoVulkan::LiveCaps().textureCompressionBC) {
        why = "the device has no textureCompressionBC";
    } else if (!VideoVulkan::FormatSampleable(VK_FORMAT_BC4_UNORM_BLOCK)) {
        why = "BC4_UNORM is not sampleable";
    }
    if (why == nullptr) return true;
    if (!logged.exchange(true)) std::fprintf(stderr, "[CompressAo] Vulkan: BC4 not supported (%s)\n", why);
    return false;
}

BANISHED_RENDER_IMPL_END
}  // namespace CompressAoBackend
