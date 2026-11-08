// ============================================================================
// src/render/vulkan/share_gpu_data/share_create.cpp -- ext.share_gpu_data, the Vulkan side (NOT RETAIL)
// ============================================================================
// share_create.h has the contract; docs/extensions/handins/SHAREGPUDATA.md. The shape is the Metal
// body's (src/render/metal/share_gpu_data/share_create.mm): the same key, the same counters, the same
// summary line.
// ============================================================================
#include "share_gpu_data/share_create.h"

#include "device_objects.h"

#include "share_gpu_data/share_gpu_data_backend.h"
#include "share_gpu_data/share_table.h"
#include "ext/derived_cache/hash64.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <vector>

namespace VideoVulkan {
namespace ShareGpuData {

namespace {

std::atomic<bool> g_requested{false};
std::atomic<uint64_t> g_hashNs{0};    // time spent hashing seed bytes, all threads
std::atomic<uint64_t> g_refused{0};   // kind-2 images whose levels did not qualify
GpuShare::Stats g_lastPrinted;        // LoadEnd only (loader thread)

GpuShare::Table& Table() {
    static GpuShare::Table t;
    return t;
}

uint64_t NowNs() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

template <class T>
void AppendDesc(GpuShare::Key* key, const T& value) {
    static_assert(sizeof(T) <= GpuShare::kMaxDesc, "descriptor blob too large");
    std::memcpy(key->desc + key->descSize, &value, sizeof(T));
    key->descSize += static_cast<uint32_t>(sizeof(T));
}

void* Handle(VkBuffer b) {
    return reinterpret_cast<void*>(b);   // CAST-OK: a non-dispatchable handle is the table's opaque key
}

void* Handle(VkImage i) {
    return reinterpret_cast<void*>(i);   // CAST-OK: as above
}

// An entry's handles[1]: the copy of the object a new user takes (one of the two members, by kind).
struct SharedCopy {
    GpuBuffer buffer;
    GpuImage image;
};

}  // namespace

bool Requested() {
    return g_requested.load(std::memory_order_relaxed);
}

bool CreateBuffer(bool index, const void* data, size_t size, VkBufferUsageFlags usage,
                  const std::function<bool(GpuBuffer*)>& make, GpuBuffer* out, bool* shared) {
    *shared = false;
    if (!Requested() || data == nullptr || size == 0 || !LiveDevice()) return make(out);

    GpuShare::Key key;
    key.device = LiveDevice();
    key.kind = index ? GpuShare::kIndexBuffer : GpuShare::kVertexBuffer;
    const uint64_t desc[2] = { static_cast<uint64_t>(size), static_cast<uint64_t>(usage) };
    AppendDesc(&key, desc);
    key.payloadBytes = size;
    const uint64_t t0 = NowNs();
    key.payloadHash = Ext::DerivedCache::Hash64(data, size);
    g_hashNs.fetch_add(NowNs() - t0, std::memory_order_relaxed);

    void* handles[GpuShare::kHandles] = {};
    if (Table().Acquire(key, handles)) {
        *out = static_cast<const SharedCopy*>(handles[1])->buffer;
        *shared = true;
        return true;
    }
    if (!make(out)) return false;
    SharedCopy* copy = new SharedCopy();
    copy->buffer = *out;
    handles[0] = Handle(out->buffer);
    handles[1] = copy;
    if (Table().Insert(key, handles)) {
        *shared = true;
        return true;
    }
    // Two creates of an equal buffer raced (the loader and a worker) and the other one's is in the
    // table: nobody has seen ours yet (its upload may still be queued: retired, not destroyed), so it
    // goes and the winner's is handed out instead.
    delete copy;
    void* winner[GpuShare::kHandles] = {};
    if (Table().Acquire(key, winner)) {
        RetireBuffer(out);
        *out = static_cast<const SharedCopy*>(winner[1])->buffer;
        *shared = true;
    }
    return true;
}

bool CreateImage(const ImageDesc& d, const LevelBytes* levels, int count, bool qualifies,
                 const std::function<bool(GpuImage*)>& make, GpuImage* out, bool* shared) {
    *shared = false;
    if (!Requested()) return make(out);
    if (!qualifies || levels == nullptr || count <= 0 || !LiveDevice()) {
        g_refused.fetch_add(1, std::memory_order_relaxed);
        return make(out);
    }

    GpuShare::Key key;
    key.device = LiveDevice();
    key.kind = GpuShare::kTexture;
    const uint64_t shape[8] = {
        static_cast<uint64_t>(d.format), static_cast<uint64_t>(d.width),   static_cast<uint64_t>(d.height),
        static_cast<uint64_t>(d.mips),   static_cast<uint64_t>(d.layers),  static_cast<uint64_t>(d.cube ? 1 : 0),
        static_cast<uint64_t>(d.usage),
        (static_cast<uint64_t>(d.swizzle.r) << 48) | (static_cast<uint64_t>(d.swizzle.g) << 32) |
            (static_cast<uint64_t>(d.swizzle.b) << 16) | static_cast<uint64_t>(d.swizzle.a),
    };
    AppendDesc(&key, shape);
    const uint64_t t0 = NowNs();
    uint64_t h = 0;
    uint64_t total = 0;
    for (int i = 0; i < count; ++i) {
        const uint64_t meta = static_cast<uint64_t>(levels[i].size);
        h = Ext::DerivedCache::Hash64(&meta, sizeof(meta), h);
        h = Ext::DerivedCache::Hash64(levels[i].data, levels[i].size, h);
        total += levels[i].size;
    }
    g_hashNs.fetch_add(NowNs() - t0, std::memory_order_relaxed);
    key.payloadBytes = total;
    key.payloadHash = h;

    void* handles[GpuShare::kHandles] = {};
    if (Table().Acquire(key, handles)) {
        *out = static_cast<const SharedCopy*>(handles[1])->image;
        *shared = true;
        return true;
    }
    if (!make(out)) return false;
    SharedCopy* copy = new SharedCopy();
    copy->image = *out;
    handles[0] = Handle(out->image);
    handles[1] = copy;
    if (Table().Insert(key, handles)) {
        *shared = true;
        return true;
    }
    // A raced equal create is in the table: ours goes, the winner's is handed out (as CreateBuffer).
    delete copy;
    void* winner[GpuShare::kHandles] = {};
    if (Table().Acquire(key, winner)) {
        RetireImage(out);
        *out = static_cast<const SharedCopy*>(winner[1])->image;
        *shared = true;
    }
    return true;
}

bool DropBuffer(const GpuBuffer& buffer) {
    if (!buffer.buffer) return false;
    void* handles[GpuShare::kHandles] = {};
    if (!Table().Drop(Handle(buffer.buffer), handles)) return false;
    delete static_cast<SharedCopy*>(handles[1]);
    return true;
}

bool DropImage(const GpuImage& image) {
    if (!image.image) return false;
    void* handles[GpuShare::kHandles] = {};
    if (!Table().Drop(Handle(image.image), handles)) return false;
    delete static_cast<SharedCopy*>(handles[1]);
    return true;
}

void Clear() {
    std::vector<void*> handles;
    Table().Clear(&handles);
    // Every entry has both handles (object, copy), in that order: only the copies are ours to free.
    size_t freed = 0;
    for (size_t i = 0; i + 1 < handles.size(); i += 2) {
        delete static_cast<SharedCopy*>(handles[i + 1]);
        ++freed;
    }
    if (freed != 0) {
        std::fprintf(stderr, "[ShareGpuData] device going away: the table forgot %zu shared object(s)\n", freed);
    }
}

void LoadEnd() {
    if (!Requested()) return;
    const GpuShare::Stats s = Table().GetStats();
    uint64_t hits = 0;
    for (uint32_t k = 0; k < GpuShare::kKindCount; ++k)
        hits += s.hits[k] - g_lastPrinted.hits[k];
    if (hits == 0 && s.inserts[GpuShare::kTexture] == g_lastPrinted.inserts[GpuShare::kTexture] &&
        s.inserts[GpuShare::kVertexBuffer] == g_lastPrinted.inserts[GpuShare::kVertexBuffer]) {
        return;
    }
    g_lastPrinted = s;
    const double mb = 1.0 / (1024.0 * 1024.0);
    std::fprintf(stderr,
                 "[ShareGpuData] shared (run total): vb hits=%llu (%.1f MB) ib hits=%llu (%.1f MB) "
                 "tex hits=%llu (%.1f MB); entries=%llu (vb %llu / ib %llu / tex %llu made), freed=%llu, "
                 "raced=%llu, tex refused=%llu, hash %.0f ms\n",
                 static_cast<unsigned long long>(s.hits[GpuShare::kVertexBuffer]),
                 s.hitBytes[GpuShare::kVertexBuffer] * mb,
                 static_cast<unsigned long long>(s.hits[GpuShare::kIndexBuffer]),
                 s.hitBytes[GpuShare::kIndexBuffer] * mb,
                 static_cast<unsigned long long>(s.hits[GpuShare::kTexture]), s.hitBytes[GpuShare::kTexture] * mb,
                 static_cast<unsigned long long>(s.live),
                 static_cast<unsigned long long>(s.inserts[GpuShare::kVertexBuffer]),
                 static_cast<unsigned long long>(s.inserts[GpuShare::kIndexBuffer]),
                 static_cast<unsigned long long>(s.inserts[GpuShare::kTexture]),
                 static_cast<unsigned long long>(s.freed), static_cast<unsigned long long>(s.raced),
                 static_cast<unsigned long long>(g_refused.load(std::memory_order_relaxed)),
                 static_cast<double>(g_hashNs.load(std::memory_order_relaxed)) / 1e6);
}

}  // namespace ShareGpuData
}  // namespace VideoVulkan

// ---- the backend-neutral interface (share_gpu_data_backend.h) -------------------------------------
namespace ShareGpuDataBackend {
BANISHED_RENDER_IMPL_BEGIN

// Sharing needs nothing of the device: every Vulkan device supports it.
void Request(bool on) {
    VideoVulkan::ShareGpuData::g_requested.store(on, std::memory_order_relaxed);
    std::fprintf(stderr, "[ShareGpuData] %s\n",
                 on ? "on: equal immutable buffers and textures share one GPU object" : "off");
}

bool Supported() {
    return true;
}

BANISHED_RENDER_IMPL_END
}  // namespace ShareGpuDataBackend
