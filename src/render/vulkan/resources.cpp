// ============================================================================
// src/render/vulkan/resources.cpp -- GPU memory, submission, the retire queue, uploads, samplers
// ============================================================================
// NOT RETAIL. resources.h has the contract (memory, submission, the upload batch, the retire queue).
// ============================================================================

#include "resources.h"
#include "device_objects.h"
#include "vulkan11.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>
#include <unordered_map>
#include <utility>

namespace VideoVulkan {

namespace {

constexpr VkDeviceSize kStagingChunk = 8ull << 20;     // a batch's staging grows in 8 MB chunks
constexpr VkDeviceSize kBatchLimit = 64ull << 20;      // a batch past this is submitted at once
constexpr VkDeviceSize kInFlightLimit = 256ull << 20;  // staging of submitted batches before a wait
constexpr VkDeviceSize kZeroBufferBytes = 64ull << 10;

VmaAllocator g_allocator = nullptr;
VkSemaphore g_timeline = VK_NULL_HANDLE;

// ---- submission ----
std::mutex g_queueLock;
uint64_t g_lastSerial = 0;   // the last serial handed out (under g_queueLock)

// ---- the 1.1 path's serials (vulkan11.h): a fence per submit instead of the timeline ----
struct SerialFence {
    uint64_t serial = 0;
    VkFence fence = VK_NULL_HANDLE;
};
std::mutex g_fenceLock;                   // taken under g_queueLock, never the other way round
std::deque<SerialFence> g_pendingFences;  // submitted and not yet seen signalled, in serial order
std::vector<VkFence> g_freeFences;        // reset, ready for the next submit
uint64_t g_fenceCompleted = ~0ull;        // the highest serial seen complete (~0: no device)

// ---- frames ----
std::mutex g_frameLock;
std::deque<uint64_t> g_frameSerials;   // the serials of frames submitted and not yet seen complete
uint64_t g_framesSubmitted = 0;
uint64_t g_framesCompleted = 0;

// ---- the retire queue ----
struct RetireEntry {
    uint64_t frame = 0;
    std::function<void()> destroy;
};
std::mutex g_retireLock;
std::vector<RetireEntry> g_retired;

// ---- live objects (destroyed at shutdown if the engine has not released them) ----
struct LiveImage {
    VmaAllocation allocation = nullptr;
    VkImageView view = VK_NULL_HANDLE;
    bool initialized = false;   // has been uploaded or cleared at least once (under g_uploadLock)
};
std::mutex g_liveLock;
std::unordered_map<VkBuffer, VmaAllocation> g_liveBuffers;
std::unordered_map<VkImage, LiveImage> g_liveImages;

// ---- samplers ----
struct SamplerHash {
    size_t operator()(const SamplerDesc& d) const {
        uint64_t h = 1469598103934665603ull;
        const auto* p = reinterpret_cast<const uint8_t*>(&d);   // CAST-OK: hashing a POD's bytes
        for (size_t i = 0; i < sizeof(SamplerDesc); ++i) {
            h ^= p[i];
            h *= 1099511628211ull;
        }
        return static_cast<size_t>(h);
    }
};
struct SamplerEqual {
    bool operator()(const SamplerDesc& a, const SamplerDesc& b) const {
        return std::memcmp(&a, &b, sizeof(SamplerDesc)) == 0;
    }
};
static_assert(sizeof(SamplerDesc) == 12, "SamplerDesc is hashed as bytes: no padding");
std::mutex g_samplerLock;
std::unordered_map<SamplerDesc, VkSampler, SamplerHash, SamplerEqual> g_samplers;

// ---- placeholders ----
GpuImage g_white;
GpuImage g_whiteCube;
VkSampler g_defaultSampler = VK_NULL_HANDLE;
GpuBuffer g_zero;

// ---- the upload batch ----
struct UploadBatch {
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBuffer cb = VK_NULL_HANDLE;
    std::vector<GpuBuffer> staging;   // chunks; the last one is being filled
    VkDeviceSize chunkUsed = 0;       // bytes used in the last chunk
    VkDeviceSize bytes = 0;           // staging bytes of the batch
    uint64_t serial = 0;              // its submit's serial
};
std::mutex g_uploadLock;
UploadBatch* g_open = nullptr;
std::deque<UploadBatch*> g_inFlight;
std::vector<UploadBatch*> g_freeBatches;
VkDeviceSize g_inFlightBytes = 0;
uint64_t g_statBytes = 0;
uint32_t g_statBatches = 0;

VkDevice Dev() {
    return LiveDevice();
}

void MemoryBarrier(VkCommandBuffer cb, VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
                   VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess) {
    VkMemoryBarrier2 b = Make<VkMemoryBarrier2>(VK_STRUCTURE_TYPE_MEMORY_BARRIER_2);
    b.srcStageMask = srcStage;
    b.srcAccessMask = srcAccess;
    b.dstStageMask = dstStage;
    b.dstAccessMask = dstAccess;
    VkDependencyInfo d = Make<VkDependencyInfo>(VK_STRUCTURE_TYPE_DEPENDENCY_INFO);
    d.memoryBarrierCount = 1;
    d.pMemoryBarriers = &b;
    CmdPipelineBarrier2(cb, d);
}

void ImageBarrier(VkCommandBuffer cb, const GpuImage& image, VkImageLayout from, VkImageLayout to,
                  VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess, VkPipelineStageFlags2 dstStage,
                  VkAccessFlags2 dstAccess) {
    VkImageMemoryBarrier2 b = Make<VkImageMemoryBarrier2>(VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2);
    b.srcStageMask = srcStage;
    b.srcAccessMask = srcAccess;
    b.dstStageMask = dstStage;
    b.dstAccessMask = dstAccess;
    b.oldLayout = from;
    b.newLayout = to;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = image.image;
    b.subresourceRange = { image.aspect, 0, image.mips, 0, image.layers };
    VkDependencyInfo d = Make<VkDependencyInfo>(VK_STRUCTURE_TYPE_DEPENDENCY_INFO);
    d.imageMemoryBarrierCount = 1;
    d.pImageMemoryBarriers = &b;
    CmdPipelineBarrier2(cb, d);
}

constexpr VkPipelineStageFlags2 kReadStages = VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT |
                                              VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT |
                                              VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
constexpr VkAccessFlags2 kReadAccess = VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_2_INDEX_READ_BIT |
                                       VK_ACCESS_2_UNIFORM_READ_BIT | VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;

bool MakeBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VmaAllocationCreateFlags flags,
                VkMemoryPropertyFlags required, GpuBuffer* out, std::string* why) {
    *out = GpuBuffer{};
    if (!g_allocator) {
        if (why) *why = "no live device";
        return false;
    }
    VkBufferCreateInfo bi = Make<VkBufferCreateInfo>(VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO);
    bi.size = std::max<VkDeviceSize>(size, 4);
    bi.usage = usage;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VmaAllocationCreateInfo ai{};
    ai.usage = VMA_MEMORY_USAGE_AUTO;
    ai.flags = flags;
    ai.requiredFlags = required;
    VmaAllocationInfo info{};
    const VkResult r = vmaCreateBuffer(g_allocator, &bi, &ai, &out->buffer, &out->allocation, &info);
    if (r != VK_SUCCESS) {
        if (why) *why = std::string("vmaCreateBuffer: ") + ResultName(r);
        *out = GpuBuffer{};
        return false;
    }
    out->mapped = static_cast<uint8_t*>(info.pMappedData);
    out->size = bi.size;
    std::lock_guard<std::mutex> guard(g_liveLock);
    g_liveBuffers[out->buffer] = out->allocation;
    return true;
}

// ---- the upload batch (all under g_uploadLock) ----

void RecycleBatchesLocked() {
    const uint64_t done = CompletedSerial();
    while (!g_inFlight.empty() && g_inFlight.front()->serial <= done) {
        UploadBatch* b = g_inFlight.front();
        g_inFlight.pop_front();
        for (GpuBuffer& s : b->staging)
            DestroyBufferNow(&s);
        b->staging.clear();
        g_inFlightBytes -= b->bytes;
        b->bytes = 0;
        b->chunkUsed = 0;
        g_freeBatches.push_back(b);
    }
}

UploadBatch* OpenBatchLocked() {
    if (g_open) return g_open;
    RecycleBatchesLocked();
    UploadBatch* b = nullptr;
    if (!g_freeBatches.empty()) {
        b = g_freeBatches.back();
        g_freeBatches.pop_back();
        vkResetCommandPool(Dev(), b->pool, 0);
    } else {
        b = new UploadBatch();
        auto pi = Make<VkCommandPoolCreateInfo>(VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO);
        pi.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        pi.queueFamilyIndex = LiveGraphicsFamily();
        vkCreateCommandPool(Dev(), &pi, nullptr, &b->pool);
        auto ai = Make<VkCommandBufferAllocateInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO);
        ai.commandPool = b->pool;
        ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = 1;
        vkAllocateCommandBuffers(Dev(), &ai, &b->cb);
    }
    auto bi = Make<VkCommandBufferBeginInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO);
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(b->cb, &bi);
    // Copies wait for earlier reads (vertex input, shaders) and earlier copies of the same memory.
    MemoryBarrier(b->cb, kReadStages | VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                  VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT | VK_ACCESS_2_TRANSFER_READ_BIT);
    g_open = b;
    return b;
}

void SubmitBatchLocked() {
    UploadBatch* b = g_open;
    if (!b) return;
    g_open = nullptr;
    MemoryBarrier(b->cb, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                  kReadStages | VK_PIPELINE_STAGE_2_TRANSFER_BIT, kReadAccess | VK_ACCESS_2_TRANSFER_READ_BIT);
    vkEndCommandBuffer(b->cb);
    // A failed submit (logged) answers serial 0: nothing of it runs, so the recycle frees it at once.
    b->serial = SubmitCommands(b->cb, nullptr, VK_NULL_HANDLE, VK_NULL_HANDLE);
    g_inFlight.push_back(b);
    g_inFlightBytes += b->bytes;
    ++g_statBatches;
    while (g_inFlightBytes > kInFlightLimit && !g_inFlight.empty()) {
        WaitSerial(g_inFlight.front()->serial);
        RecycleBatchesLocked();
    }
}

// Room for `size` bytes at `align` in the open batch's staging. Returns the mapped pointer and the
// staging buffer / offset of the bytes. Null when no staging memory could be made (logged).
uint8_t* StageLocked(UploadBatch* b, VkDeviceSize size, VkDeviceSize align, VkBuffer* buffer,
                     VkDeviceSize* offset) {
    VkDeviceSize at = (b->chunkUsed + align - 1) / align * align;
    if (b->staging.empty() || at + size > b->staging.back().size) {
        GpuBuffer chunk;
        std::string why;
        if (!MakeBuffer(std::max(kStagingChunk, size), VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
                        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &chunk, &why)) {
            std::fprintf(stderr, "[Vulkan] staging memory for %llu bytes: %s\n",
                         static_cast<unsigned long long>(size), why.c_str());
            return nullptr;
        }
        b->staging.push_back(chunk);
        b->bytes += chunk.size;
        at = 0;
    }
    b->chunkUsed = at + size;
    *buffer = b->staging.back().buffer;
    *offset = at;
    g_statBytes += size;
    return b->staging.back().mapped + at;
}

void SubmitIfLargeLocked() {
    if (g_open && g_open->bytes > kBatchLimit) SubmitBatchLocked();
}

void MarkInitialized(VkImage image) {
    std::lock_guard<std::mutex> guard(g_liveLock);
    auto it = g_liveImages.find(image);
    if (it != g_liveImages.end()) it->second.initialized = true;
}

bool Initialized(VkImage image) {
    std::lock_guard<std::mutex> guard(g_liveLock);
    auto it = g_liveImages.find(image);
    return it != g_liveImages.end() && it->second.initialized;
}

bool IsDepthFormat(VkFormat f) {
    return f == VK_FORMAT_D24_UNORM_S8_UINT || f == VK_FORMAT_D32_SFLOAT_S8_UINT || f == VK_FORMAT_D32_SFLOAT ||
           f == VK_FORMAT_D16_UNORM;
}

bool HasStencil(VkFormat f) {
    return f == VK_FORMAT_D24_UNORM_S8_UINT || f == VK_FORMAT_D32_SFLOAT_S8_UINT;
}

}  // namespace

// ---- lifecycle ------------------------------------------------------------------------------------

bool ResourcesStartup(std::string* why) {
    VmaVulkanFunctions f{};
    f.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
    f.vkGetDeviceProcAddr = vkGetDeviceProcAddr;
    f.vkGetPhysicalDeviceProperties = vkGetPhysicalDeviceProperties;
    f.vkGetPhysicalDeviceMemoryProperties = vkGetPhysicalDeviceMemoryProperties;
    f.vkAllocateMemory = vkAllocateMemory;
    f.vkFreeMemory = vkFreeMemory;
    f.vkMapMemory = vkMapMemory;
    f.vkUnmapMemory = vkUnmapMemory;
    f.vkFlushMappedMemoryRanges = vkFlushMappedMemoryRanges;
    f.vkInvalidateMappedMemoryRanges = vkInvalidateMappedMemoryRanges;
    f.vkBindBufferMemory = vkBindBufferMemory;
    f.vkBindImageMemory = vkBindImageMemory;
    f.vkGetBufferMemoryRequirements = vkGetBufferMemoryRequirements;
    f.vkGetImageMemoryRequirements = vkGetImageMemoryRequirements;
    f.vkCreateBuffer = vkCreateBuffer;
    f.vkDestroyBuffer = vkDestroyBuffer;
    f.vkCreateImage = vkCreateImage;
    f.vkDestroyImage = vkDestroyImage;
    f.vkCmdCopyBuffer = vkCmdCopyBuffer;
    f.vkGetBufferMemoryRequirements2KHR = vkGetBufferMemoryRequirements2;
    f.vkGetImageMemoryRequirements2KHR = vkGetImageMemoryRequirements2;
    f.vkBindBufferMemory2KHR = vkBindBufferMemory2;
    f.vkBindImageMemory2KHR = vkBindImageMemory2;
    f.vkGetPhysicalDeviceMemoryProperties2KHR = vkGetPhysicalDeviceMemoryProperties2;
    f.vkGetDeviceBufferMemoryRequirements = vkGetDeviceBufferMemoryRequirements;
    f.vkGetDeviceImageMemoryRequirements = vkGetDeviceImageMemoryRequirements;
    VmaAllocatorCreateInfo ci{};
    ci.physicalDevice = LivePhysical();
    ci.device = Dev();
    ci.instance = LiveInstance();
    // The 1.1 path (vulkan11.h): VMA keeps to the 1.1 calls (the two 1.3 ones above are null there).
    ci.vulkanApiVersion = Vulkan11Path() ? VK_API_VERSION_1_1 : VK_API_VERSION_1_3;
    ci.pVulkanFunctions = &f;
    VkResult r = vmaCreateAllocator(&ci, &g_allocator);
    if (r != VK_SUCCESS) {
        *why = std::string("vmaCreateAllocator: ") + ResultName(r);
        g_allocator = nullptr;
        return false;
    }

    if (Vulkan11Path()) {
        std::lock_guard<std::mutex> guard(g_fenceLock);
        g_fenceCompleted = 0;
    } else {
        auto ti = Make<VkSemaphoreTypeCreateInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO);
        ti.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
        ti.initialValue = 0;
        auto si = Make<VkSemaphoreCreateInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO);
        si.pNext = &ti;
        r = vkCreateSemaphore(Dev(), &si, nullptr, &g_timeline);
        if (r != VK_SUCCESS) {
            *why = std::string("timeline semaphore: ") + ResultName(r);
            return false;
        }
    }
    g_lastSerial = 0;
    {
        std::lock_guard<std::mutex> guard(g_frameLock);
        g_frameSerials.clear();
        g_framesSubmitted = 0;
        g_framesCompleted = 0;
    }

    // The placeholders: an unbound texture slot reads white, an unbound stream or constant buffer zero.
    ImageDesc white;
    white.format = VK_FORMAT_R8G8B8A8_UNORM;
    if (!CreateImage(white, &g_white, why)) return false;
    ImageDesc cube = white;
    cube.layers = 6;
    cube.cube = true;
    if (!CreateImage(cube, &g_whiteCube, why)) return false;
    const uint32_t texels[6] = { 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu };
    ImageRegion region;
    region.width = 1;
    region.height = 1;
    UploadImage(g_white, &region, 1, texels, 4);
    ImageRegion faces[6];
    for (uint32_t i = 0; i < 6; ++i) {
        faces[i].offset = 4 * i;
        faces[i].layer = i;
        faces[i].width = 1;
        faces[i].height = 1;
    }
    UploadImage(g_whiteCube, faces, 6, texels, sizeof(texels));
    if (!CreateStaticBuffer(kZeroBufferBytes,
                            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                            nullptr, &g_zero, why)) {
        return false;
    }
    SamplerDesc def;
    def.magLinear = 1;
    def.minLinear = 1;
    def.mipLinear = 1;
    def.addressU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    def.addressV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    def.addressW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    def.maxLod = VK_LOD_CLAMP_NONE;
    g_defaultSampler = GetSampler(def);
    FlushUploads();
    return true;
}

void ResourcesShutdown() {
    {
        std::lock_guard<std::mutex> guard(g_uploadLock);
        if (g_open) {
            vkEndCommandBuffer(g_open->cb);
            g_inFlight.push_back(g_open);
            g_inFlightBytes += g_open->bytes;
            g_open = nullptr;
        }
        for (UploadBatch* b : g_inFlight)
            g_freeBatches.push_back(b);
        g_inFlight.clear();
        g_inFlightBytes = 0;
        for (UploadBatch* b : g_freeBatches) {
            for (GpuBuffer& s : b->staging)
                DestroyBufferNow(&s);
            if (b->pool) vkDestroyCommandPool(Dev(), b->pool, nullptr);
            delete b;
        }
        g_freeBatches.clear();
    }
    std::vector<RetireEntry> retired;
    {
        std::lock_guard<std::mutex> guard(g_retireLock);
        retired.swap(g_retired);
    }
    for (RetireEntry& e : retired)
        e.destroy();
    DestroyImageNow(&g_white);
    DestroyImageNow(&g_whiteCube);
    DestroyBufferNow(&g_zero);
    g_defaultSampler = VK_NULL_HANDLE;
    {
        std::lock_guard<std::mutex> guard(g_samplerLock);
        for (auto& kv : g_samplers)
            vkDestroySampler(Dev(), kv.second, nullptr);
        g_samplers.clear();
    }
    size_t buffers = 0;
    size_t images = 0;
    {
        std::lock_guard<std::mutex> guard(g_liveLock);
        buffers = g_liveBuffers.size();
        images = g_liveImages.size();
        for (auto& kv : g_liveBuffers)
            vmaDestroyBuffer(g_allocator, kv.first, kv.second);
        g_liveBuffers.clear();
        for (auto& kv : g_liveImages) {
            ForgetImageView(kv.second.view);
            if (kv.second.view) vkDestroyImageView(Dev(), kv.second.view, nullptr);
            vmaDestroyImage(g_allocator, kv.first, kv.second.allocation);
        }
        g_liveImages.clear();
    }
    if (buffers || images) {
        std::fprintf(stderr, "[Vulkan] shutdown: %zu buffer(s) and %zu image(s) still held, destroyed with the device\n",
                     buffers, images);
    }
    if (g_timeline) vkDestroySemaphore(Dev(), g_timeline, nullptr);
    g_timeline = VK_NULL_HANDLE;
    {
        // The 1.1 path's serial fences; the device is idle, so every serial handed out is complete.
        std::lock_guard<std::mutex> guard(g_fenceLock);
        for (const SerialFence& s : g_pendingFences)
            vkDestroyFence(Dev(), s.fence, nullptr);
        g_pendingFences.clear();
        for (VkFence fence : g_freeFences)
            vkDestroyFence(Dev(), fence, nullptr);
        g_freeFences.clear();
        g_fenceCompleted = ~0ull;
    }
    if (g_allocator) vmaDestroyAllocator(g_allocator);
    g_allocator = nullptr;
}

// ---- submission ------------------------------------------------------------------------------------

namespace {

// g_fenceLock held. Retires the serial fences signalled so far, in serial order.
void RetireSignalledLocked() {
    while (!g_pendingFences.empty()) {
        const SerialFence s = g_pendingFences.front();
        const VkResult r = vkGetFenceStatus(Dev(), s.fence);
        if (r != VK_SUCCESS) {
            NoteResult(r, "vkGetFenceStatus");
            return;
        }
        g_fenceCompleted = s.serial;
        vkResetFences(Dev(), 1, &s.fence);
        g_freeFences.push_back(s.fence);
        g_pendingFences.pop_front();
    }
}

// The 1.1 path's SubmitCommands (g_queueLock held): vkQueueSubmit signalling the serial's own fence. A
// caller's fence is signalled by an empty submit right after it (vkQueueSubmit takes one fence; an empty
// submit's fence signals once everything submitted before it completed).
uint64_t SubmitCommands11(VkCommandBuffer cb, const VkSemaphoreSubmitInfo* wait, VkSemaphore signal,
                          VkFence fence, uint64_t serial) {
    VkFence serialFence = VK_NULL_HANDLE;
    {
        std::lock_guard<std::mutex> guard(g_fenceLock);
        if (!g_freeFences.empty()) {
            serialFence = g_freeFences.back();
            g_freeFences.pop_back();
        }
    }
    if (!serialFence) {
        auto fi = Make<VkFenceCreateInfo>(VK_STRUCTURE_TYPE_FENCE_CREATE_INFO);
        const VkResult r = vkCreateFence(Dev(), &fi, nullptr, &serialFence);
        if (r != VK_SUCCESS) {
            std::fprintf(stderr, "[Vulkan] serial fence: %s\n", ResultName(r));
            return 0;
        }
    }
    const VkPipelineStageFlags waitStage = wait ? StageFlags1(wait->stageMask, false) : 0;
    auto si = Make<VkSubmitInfo>(VK_STRUCTURE_TYPE_SUBMIT_INFO);
    si.waitSemaphoreCount = wait ? 1 : 0;
    si.pWaitSemaphores = wait ? &wait->semaphore : nullptr;
    si.pWaitDstStageMask = wait ? &waitStage : nullptr;
    si.commandBufferCount = cb ? 1 : 0;
    si.pCommandBuffers = cb ? &cb : nullptr;
    si.signalSemaphoreCount = signal ? 1 : 0;
    si.pSignalSemaphores = signal ? &signal : nullptr;
    VkResult r = vkQueueSubmit(LiveQueue(), 1, &si, serialFence);
    if (r != VK_SUCCESS) {
        std::fprintf(stderr, "[Vulkan] vkQueueSubmit: %s\n", ResultName(r));
        NoteResult(r, "vkQueueSubmit");
        std::lock_guard<std::mutex> guard(g_fenceLock);
        g_freeFences.push_back(serialFence);
        return 0;
    }
    {
        std::lock_guard<std::mutex> guard(g_fenceLock);
        g_pendingFences.push_back({ serial, serialFence });
    }
    if (fence) {
        r = vkQueueSubmit(LiveQueue(), 0, nullptr, fence);
        if (r != VK_SUCCESS) {
            std::fprintf(stderr, "[Vulkan] vkQueueSubmit (fence): %s\n", ResultName(r));
            NoteResult(r, "vkQueueSubmit");
        }
    }
    return serial;
}

}  // namespace

uint64_t SubmitCommands(VkCommandBuffer cb, const VkSemaphoreSubmitInfo* wait, VkSemaphore signal, VkFence fence) {
    std::lock_guard<std::mutex> guard(g_queueLock);
    const uint64_t serial = g_lastSerial + 1;
    if (Vulkan11Path()) {
        if (SubmitCommands11(cb, wait, signal, fence, serial) == 0) return 0;
        g_lastSerial = serial;
        return serial;
    }
    VkSemaphoreSubmitInfo signals[2];
    signals[0] = Make<VkSemaphoreSubmitInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO);
    signals[0].semaphore = g_timeline;
    signals[0].value = serial;
    signals[0].stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    uint32_t signalCount = 1;
    if (signal) {
        signals[1] = Make<VkSemaphoreSubmitInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO);
        signals[1].semaphore = signal;
        signals[1].stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        signalCount = 2;
    }
    auto cbi = Make<VkCommandBufferSubmitInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO);
    cbi.commandBuffer = cb;
    VkSubmitInfo2 si = Make<VkSubmitInfo2>(VK_STRUCTURE_TYPE_SUBMIT_INFO_2);
    si.waitSemaphoreInfoCount = wait ? 1 : 0;
    si.pWaitSemaphoreInfos = wait;
    si.commandBufferInfoCount = cb ? 1 : 0;
    si.pCommandBufferInfos = cb ? &cbi : nullptr;
    si.signalSemaphoreInfoCount = signalCount;
    si.pSignalSemaphoreInfos = signals;
    const VkResult r = vkQueueSubmit2(LiveQueue(), 1, &si, fence);
    if (r != VK_SUCCESS) {
        std::fprintf(stderr, "[Vulkan] vkQueueSubmit2: %s\n", ResultName(r));
        NoteResult(r, "vkQueueSubmit2");   // P4: a lost device enters the recovery at the frame's Finish
        return 0;
    }
    g_lastSerial = serial;
    return serial;
}

VkResult PresentQueue(const VkPresentInfoKHR& info) {
    std::lock_guard<std::mutex> guard(g_queueLock);
    return vkQueuePresentKHR(LiveQueue(), &info);
}

uint64_t CompletedSerial() {
    if (Vulkan11Path()) {
        std::lock_guard<std::mutex> guard(g_fenceLock);
        RetireSignalledLocked();
        return g_fenceCompleted;
    }
    if (!g_timeline) return ~0ull;
    uint64_t value = 0;
    vkGetSemaphoreCounterValue(Dev(), g_timeline, &value);
    return value;
}

void WaitSerial(uint64_t serial) {
    if (serial == 0) return;
    if (Vulkan11Path()) {
        // Under the fence lock: a fence being waited on is never reset by a concurrent retire.
        std::lock_guard<std::mutex> guard(g_fenceLock);
        for (const SerialFence& s : g_pendingFences) {
            if (s.serial < serial) continue;
            NoteResult(vkWaitForFences(Dev(), 1, &s.fence, VK_TRUE, UINT64_MAX), "vkWaitForFences");
            break;
        }
        RetireSignalledLocked();
        return;
    }
    if (!g_timeline) return;
    auto wi = Make<VkSemaphoreWaitInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO);
    wi.semaphoreCount = 1;
    wi.pSemaphores = &g_timeline;
    wi.pValues = &serial;
    NoteResult(vkWaitSemaphores(Dev(), &wi, UINT64_MAX), "vkWaitSemaphores");
}

// ---- frames and the retire queue --------------------------------------------------------------------

uint64_t FramesSubmitted() {
    std::lock_guard<std::mutex> guard(g_frameLock);
    return g_framesSubmitted;
}

uint64_t FramesCompleted() {
    const uint64_t done = CompletedSerial();
    std::lock_guard<std::mutex> guard(g_frameLock);
    while (!g_frameSerials.empty() && g_frameSerials.front() <= done) {
        g_frameSerials.pop_front();
        ++g_framesCompleted;
    }
    return g_framesCompleted;
}

void NoteFrameSubmitted(uint64_t serial) {
    std::lock_guard<std::mutex> guard(g_frameLock);
    // A failed submit (serial 0) completes at once: nothing of it runs on the GPU.
    g_frameSerials.push_back(serial);
    ++g_framesSubmitted;
}

void CollectRetired() {
    const uint64_t completed = FramesCompleted();
    std::vector<RetireEntry> ready;
    {
        std::lock_guard<std::mutex> guard(g_retireLock);
        auto keep = std::partition(g_retired.begin(), g_retired.end(), [completed](const RetireEntry& e) {
            return e.frame >= completed;
        });
        ready.assign(std::make_move_iterator(keep), std::make_move_iterator(g_retired.end()));
        g_retired.erase(keep, g_retired.end());
    }
    for (RetireEntry& e : ready)
        e.destroy();
    std::lock_guard<std::mutex> guard(g_uploadLock);
    RecycleBatchesLocked();
}

void Retire(std::function<void()> destroy) {
    RetireEntry e;
    e.frame = FramesSubmitted();
    e.destroy = std::move(destroy);
    std::lock_guard<std::mutex> guard(g_retireLock);
    g_retired.push_back(std::move(e));
}

// ---- buffers ---------------------------------------------------------------------------------------

bool CreateStaticBuffer(VkDeviceSize size, VkBufferUsageFlags usage, const void* data, GpuBuffer* out,
                        std::string* why) {
    if (!MakeBuffer(size, usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, 0, 0, out,
                    why)) {
        return false;
    }
    if (data) {
        UploadBuffer(*out, 0, data, size);
        return true;
    }
    std::lock_guard<std::mutex> guard(g_uploadLock);
    UploadBatch* b = OpenBatchLocked();
    vkCmdFillBuffer(b->cb, out->buffer, 0, VK_WHOLE_SIZE, 0);
    // A SetData later in the same batch copies over the fill (synchronization validation: write after
    // write): the fill lands first.
    MemoryBarrier(b->cb, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                  VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
    return true;
}

bool CreateHostBuffer(VkDeviceSize size, VkBufferUsageFlags usage, GpuBuffer* out, std::string* why) {
    return MakeBuffer(size, usage, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
                      VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, out, why);
}

void UploadBuffer(const GpuBuffer& dst, VkDeviceSize offset, const void* data, VkDeviceSize size) {
    if (!dst.buffer || !data || size == 0) return;
    std::lock_guard<std::mutex> guard(g_uploadLock);
    UploadBatch* b = OpenBatchLocked();
    VkBuffer staging = VK_NULL_HANDLE;
    VkDeviceSize at = 0;
    uint8_t* p = StageLocked(b, size, 16, &staging, &at);
    if (!p) return;
    std::memcpy(p, data, static_cast<size_t>(size));
    VkBufferCopy copy{ at, offset, size };
    vkCmdCopyBuffer(b->cb, staging, dst.buffer, 1, &copy);
    SubmitIfLargeLocked();
}

bool ReadBuffer(const GpuBuffer& src, void* dst, VkDeviceSize size) {
    {
        std::lock_guard<std::mutex> guard(g_liveLock);
        if (!src.buffer || g_liveBuffers.find(src.buffer) == g_liveBuffers.end()) return false;
    }
    GpuBuffer readback;
    std::string why;
    if (!MakeBuffer(size, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                    VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT, 0, &readback,
                    &why)) {
        std::fprintf(stderr, "[Vulkan] read-back buffer: %s\n", why.c_str());
        return false;
    }
    uint64_t serial = 0;
    {
        std::lock_guard<std::mutex> guard(g_uploadLock);
        UploadBatch* b = OpenBatchLocked();
        // Earlier copies in this batch (the buffer's own upload) land before the read.
        MemoryBarrier(b->cb, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                      VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
        VkBufferCopy copy{ 0, 0, size };
        vkCmdCopyBuffer(b->cb, src.buffer, readback.buffer, 1, &copy);
        MemoryBarrier(b->cb, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                      VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_READ_BIT);
        UploadBatch* submitted = g_open;
        SubmitBatchLocked();
        serial = submitted->serial;
    }
    WaitSerial(serial);
    vmaInvalidateAllocation(g_allocator, readback.allocation, 0, VK_WHOLE_SIZE);
    std::memcpy(dst, readback.mapped, static_cast<size_t>(size));
    DestroyBufferNow(&readback);
    return true;
}

bool CreateReadbackBuffer(VkDeviceSize size, GpuBuffer* out, std::string* why) {
    return MakeBuffer(size, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                      VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT, 0, out, why);
}

void InvalidateBuffer(const GpuBuffer& b) {
    if (b.allocation) vmaInvalidateAllocation(g_allocator, b.allocation, 0, VK_WHOLE_SIZE);
}

void DestroyBufferNow(GpuBuffer* b) {
    if (!b->buffer) return;
    {
        std::lock_guard<std::mutex> guard(g_liveLock);
        auto it = g_liveBuffers.find(b->buffer);
        if (it != g_liveBuffers.end()) {
            vmaDestroyBuffer(g_allocator, it->first, it->second);
            g_liveBuffers.erase(it);
        }
    }
    *b = GpuBuffer{};
}

void RetireBuffer(GpuBuffer* b) {
    if (!b->buffer) return;
    GpuBuffer copy = *b;
    *b = GpuBuffer{};
    Retire([copy]() mutable {
        DestroyBufferNow(&copy);
    });
}

// ---- images ----------------------------------------------------------------------------------------

bool CreateImage(const ImageDesc& desc, GpuImage* out, std::string* why) {
    *out = GpuImage{};
    if (!g_allocator) {
        *why = "no live device";
        return false;
    }
    const bool depth = IsDepthFormat(desc.format);
    VkImageCreateInfo ii = Make<VkImageCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO);
    ii.flags = (desc.cube ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0) | desc.flags;
    ii.imageType = VK_IMAGE_TYPE_2D;
    ii.format = desc.format;
    ii.extent = { std::max(desc.width, 1u), std::max(desc.height, 1u), 1 };
    ii.mipLevels = std::max(desc.mips, 1u);
    ii.arrayLayers = std::max(desc.layers, 1u);
    ii.samples = desc.samples;
    ii.tiling = VK_IMAGE_TILING_OPTIMAL;
    ii.usage = desc.attachmentOnly ? desc.usage
                                   : (desc.usage | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                                      VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
    ii.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VmaAllocationCreateInfo ai{};
    ai.usage = (ii.usage & VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT) ? VMA_MEMORY_USAGE_GPU_LAZILY_ALLOCATED
                                                                      : VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    VkResult r = vmaCreateImage(g_allocator, &ii, &ai, &out->image, &out->allocation, nullptr);
    if (r != VK_SUCCESS) {
        *why = std::string("vmaCreateImage: ") + ResultName(r);
        *out = GpuImage{};
        return false;
    }
    out->format = desc.format;
    out->aspect = depth ? (VK_IMAGE_ASPECT_DEPTH_BIT | (HasStencil(desc.format) ? VK_IMAGE_ASPECT_STENCIL_BIT : 0))
                        : VK_IMAGE_ASPECT_COLOR_BIT;
    out->width = ii.extent.width;
    out->height = ii.extent.height;
    out->mips = ii.mipLevels;
    out->layers = ii.arrayLayers;
    VkImageViewCreateInfo vi = Make<VkImageViewCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO);
    vi.image = out->image;
    vi.viewType = desc.cube    ? VK_IMAGE_VIEW_TYPE_CUBE
                  : desc.array ? VK_IMAGE_VIEW_TYPE_2D_ARRAY
                               : VK_IMAGE_VIEW_TYPE_2D;
    vi.format = desc.format;
    vi.components = desc.swizzle;
    // A depth image is sampled through its depth aspect (retail's R24_UNORM_X8_TYPELESS view); an
    // attachment-only image's view is the attachment view over every aspect.
    const bool depthOnlyView = depth && !desc.attachmentOnly;
    vi.subresourceRange = { depthOnlyView ? static_cast<VkImageAspectFlags>(VK_IMAGE_ASPECT_DEPTH_BIT) : out->aspect,
                            0, out->mips, 0, out->layers };
    r = vkCreateImageView(Dev(), &vi, nullptr, &out->view);
    if (r != VK_SUCCESS) {
        *why = std::string("vkCreateImageView: ") + ResultName(r);
        vmaDestroyImage(g_allocator, out->image, out->allocation);
        *out = GpuImage{};
        return false;
    }
    std::lock_guard<std::mutex> guard(g_liveLock);
    LiveImage live;
    live.allocation = out->allocation;
    live.view = out->view;
    g_liveImages[out->image] = live;
    return true;
}

void UploadImage(const GpuImage& image, const ImageRegion* regions, size_t regionCount, const void* data,
                 size_t size) {
    if (!image.image || regionCount == 0) return;
    std::lock_guard<std::mutex> guard(g_uploadLock);
    UploadBatch* b = OpenBatchLocked();
    VkBuffer staging = VK_NULL_HANDLE;
    VkDeviceSize at = 0;
    uint8_t* p = StageLocked(b, size, 16, &staging, &at);
    if (!p) return;
    std::memcpy(p, data, size);
    const VkImageLayout from = Initialized(image.image) ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                                                         : VK_IMAGE_LAYOUT_UNDEFINED;
    ImageBarrier(b->cb, image, from, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, kReadStages, 0,
                 VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
    std::vector<VkBufferImageCopy> copies(regionCount);
    const VkImageAspectFlags copyAspect = (image.aspect & VK_IMAGE_ASPECT_DEPTH_BIT)
                                              ? static_cast<VkImageAspectFlags>(VK_IMAGE_ASPECT_DEPTH_BIT)
                                              : image.aspect;
    for (size_t i = 0; i < regionCount; ++i) {
        VkBufferImageCopy& c = copies[i];
        c = VkBufferImageCopy{};
        c.bufferOffset = at + regions[i].offset;
        c.imageSubresource = { copyAspect, regions[i].mip, regions[i].layer, 1 };
        c.imageExtent = { regions[i].width, regions[i].height, 1 };
    }
    vkCmdCopyBufferToImage(b->cb, staging, image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           static_cast<uint32_t>(copies.size()), copies.data());
    ImageBarrier(b->cb, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                 VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, kReadStages,
                 VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    MarkInitialized(image.image);
    SubmitIfLargeLocked();
}

void ClearImage(const GpuImage& image) {
    if (!image.image) return;
    std::lock_guard<std::mutex> guard(g_uploadLock);
    UploadBatch* b = OpenBatchLocked();
    const VkImageLayout from = Initialized(image.image) ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                                                         : VK_IMAGE_LAYOUT_UNDEFINED;
    ImageBarrier(b->cb, image, from, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, kReadStages, 0,
                 VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
    const VkImageSubresourceRange range{ image.aspect, 0, image.mips, 0, image.layers };
    if (image.aspect & VK_IMAGE_ASPECT_DEPTH_BIT) {
        // Far plane: an unrendered shadow map shadows nothing.
        VkClearDepthStencilValue value{ 1.0f, 0 };
        vkCmdClearDepthStencilImage(b->cb, image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &value, 1, &range);
    } else {
        VkClearColorValue value{};
        vkCmdClearColorImage(b->cb, image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &value, 1, &range);
    }
    ImageBarrier(b->cb, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                 VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, kReadStages,
                 VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    MarkInitialized(image.image);
}

void DestroyImageNow(GpuImage* image) {
    if (!image->image) return;
    {
        std::lock_guard<std::mutex> guard(g_liveLock);
        auto it = g_liveImages.find(image->image);
        if (it != g_liveImages.end()) {
            ForgetImageView(it->second.view);
            if (it->second.view) vkDestroyImageView(Dev(), it->second.view, nullptr);
            vmaDestroyImage(g_allocator, it->first, it->second.allocation);
            g_liveImages.erase(it);
        }
    }
    *image = GpuImage{};
}

void RetireImage(GpuImage* image) {
    if (!image->image) return;
    GpuImage copy = *image;
    *image = GpuImage{};
    Retire([copy]() mutable {
        DestroyImageNow(&copy);
    });
}

// ---- layout tracking ----------------------------------------------------------------------------

namespace {

// What a layout means for one half of a barrier: the stages that touch an image in it and how.
struct LayoutUse {
    VkPipelineStageFlags2 stages;
    VkAccessFlags2 access;
    bool writes;
};

constexpr VkPipelineStageFlags2 kDepthStages =
    VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;

LayoutUse UseOf(VkImageLayout layout) {
    switch (layout) {
    case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
        return { VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                 VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, true };
    case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
        return { kDepthStages,
                 VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, true };
    case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
        return { VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                 VK_ACCESS_2_SHADER_SAMPLED_READ_BIT, false };
    case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
        return { VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT, false };
    case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
        return { VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, true };
    case VK_IMAGE_LAYOUT_GENERAL:
        // P4: the AO pass's storage image, written (and read) by the pixel stage.
        return { VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                 VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, true };
    case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
        // As a destination: the presentation engine waits on the frame's semaphore.
        return { VK_PIPELINE_STAGE_2_NONE, 0, false };
    default:
        // UNDEFINED (a fresh image, or the acquired swap-chain image): no contents to keep, but the last
        // frame's work on the same memory, and the acquire semaphore (waited at colour output), come first.
        return { VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT | kDepthStages | VK_PIPELINE_STAGE_2_TRANSFER_BIT, 0,
                 false };
    }
}

}  // namespace

void TransitionImage(ImageState& state, VkImageLayout layout, std::vector<VkImageMemoryBarrier2>* out) {
    if (!state.image) return;
    const LayoutUse from = UseOf(state.layout);
    const LayoutUse to = UseOf(layout);
    if (state.layout == layout && !from.writes) return;
    VkImageMemoryBarrier2 b = Make<VkImageMemoryBarrier2>(VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2);
    b.srcStageMask = from.stages;
    b.srcAccessMask = from.writes ? from.access : 0;
    b.dstStageMask = to.stages;
    b.dstAccessMask = to.access;
    b.oldLayout = state.layout;
    b.newLayout = layout;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = state.image;
    b.subresourceRange = { state.aspect, 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS };
    out->push_back(b);
    state.layout = layout;
}

void FlushBarriers(VkCommandBuffer cb, std::vector<VkImageMemoryBarrier2>* barriers) {
    if (barriers->empty()) return;
    VkDependencyInfo d = Make<VkDependencyInfo>(VK_STRUCTURE_TYPE_DEPENDENCY_INFO);
    d.imageMemoryBarrierCount = static_cast<uint32_t>(barriers->size());
    d.pImageMemoryBarriers = barriers->data();
    CmdPipelineBarrier2(cb, d);
    barriers->clear();
}

bool FormatSampleable(VkFormat format) {
    VkFormatProperties fp{};
    vkGetPhysicalDeviceFormatProperties(LivePhysical(), format, &fp);
    return (fp.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) != 0;
}

// ---- samplers --------------------------------------------------------------------------------------

VkSampler GetSampler(const SamplerDesc& desc) {
    std::lock_guard<std::mutex> guard(g_samplerLock);
    auto it = g_samplers.find(desc);
    if (it != g_samplers.end()) return it->second;
    VkSamplerCreateInfo si = Make<VkSamplerCreateInfo>(VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO);
    si.magFilter = desc.magLinear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    si.minFilter = desc.minLinear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    si.mipmapMode = desc.mipLinear ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
    si.addressModeU = static_cast<VkSamplerAddressMode>(desc.addressU);
    si.addressModeV = static_cast<VkSamplerAddressMode>(desc.addressV);
    si.addressModeW = static_cast<VkSamplerAddressMode>(desc.addressW);
    si.mipLodBias = 0.0f;
    si.anisotropyEnable = (desc.anisotropy > 0 && LiveCaps().samplerAnisotropy) ? VK_TRUE : VK_FALSE;
    si.maxAnisotropy = si.anisotropyEnable
                           ? std::min(static_cast<float>(desc.anisotropy), LiveProperties().limits.maxSamplerAnisotropy)
                           : 1.0f;
    si.compareEnable = desc.compare ? VK_TRUE : VK_FALSE;
    si.compareOp = desc.compare ? VK_COMPARE_OP_LESS : VK_COMPARE_OP_NEVER;
    si.minLod = 0.0f;
    si.maxLod = desc.maxLod;
    si.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
    si.unnormalizedCoordinates = VK_FALSE;
    VkSampler sampler = VK_NULL_HANDLE;
    const VkResult r = vkCreateSampler(Dev(), &si, nullptr, &sampler);
    if (r != VK_SUCCESS) {
        std::fprintf(stderr, "[Vulkan] vkCreateSampler: %s\n", ResultName(r));
        return g_defaultSampler;
    }
    g_samplers.emplace(desc, sampler);
    // P8 round 2: the Options' texture filter made visible in the log (one line per anisotropy level).
    if (desc.anisotropy > 0) {
        static uint32_t s_loggedLevels = 0;
        if (desc.anisotropy < 32 && !((s_loggedLevels >> desc.anisotropy) & 1u)) {
            s_loggedLevels |= 1u << desc.anisotropy;
            std::fprintf(stderr, "[Vulkan] sampler: anisotropic %ux requested, %s (device max %.0f)\n",
                         static_cast<unsigned>(desc.anisotropy),
                         si.anisotropyEnable ? "enabled" : "NOT enabled: samplerAnisotropy is not supported",
                         static_cast<double>(LiveProperties().limits.maxSamplerAnisotropy));
        }
    }
    return sampler;
}

// ---- placeholders ------------------------------------------------------------------------------------

VkImageView WhiteImageView() {
    return g_white.view;
}

VkImageView WhiteCubeView() {
    return g_whiteCube.view;
}

VkSampler DefaultSampler() {
    return g_defaultSampler;
}

const GpuBuffer& ZeroBuffer() {
    return g_zero;
}

// ---- the upload batch ------------------------------------------------------------------------------

void FlushUploads() {
    std::lock_guard<std::mutex> guard(g_uploadLock);
    SubmitBatchLocked();
    RecycleBatchesLocked();
}

void TakeUploadStats(uint64_t* bytes, uint32_t* batches) {
    std::lock_guard<std::mutex> guard(g_uploadLock);
    *bytes = g_statBytes;
    *batches = g_statBatches;
    g_statBytes = 0;
    g_statBatches = 0;
}

}  // namespace VideoVulkan
