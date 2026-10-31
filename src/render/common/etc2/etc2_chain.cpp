// ============================================================================
// src/render/common/etc2/etc2_chain.cpp -- BC1 / BC3 levels to ETC2 (NOT RETAIL)
// ============================================================================
// etc2_chain.h has the contract. docs/android/P13_MEM.md.
// ============================================================================
#include "etc2/etc2_chain.h"

#include "bc_decode/bc_decode.h"
#include "etc2/bc_to_etc2.h"
#include "ext/derived_cache/derived_cache.h"
#include "ext/derived_cache/hash64.h"
#include "system/parallel_for.h"

#include <atomic>
#include <chrono>
#include <cstdio>

namespace Etc2Chain {

namespace {

constexpr int kBandRows = 8;   // block rows per band

// The summary LoadEnd prints (relaxed: a log line, not a synchronisation).
std::atomic<uint64_t> g_textures{0};
std::atomic<uint64_t> g_hits{0};
std::atomic<uint64_t> g_encoded{0};
std::atomic<uint64_t> g_punchThrough{0};
std::atomic<uint64_t> g_bytes{0};
std::atomic<uint64_t> g_pixels{0};
std::atomic<int64_t>  g_encodeNs{0};   // wall, on the calling threads
std::atomic<int64_t>  g_lookupNs{0};   // hash + cache lookup, on the calling threads
uint64_t g_printed = 0;                // g_textures at the last line (LoadEnd's thread)

int64_t Since(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t0).count();
}

size_t LevelBytes(int sourceFormat, int w, int h) {
    return sourceFormat == 0 ? BcDecode::Bc1Size(w, h) : BcDecode::Bc3Size(w, h);
}

void EncodeParallel(const Level* levels, int count, int sourceFormat, bool punch, const std::vector<size_t>& offsets,
                    uint8_t* out) {
    struct Band {
        int level;
        int row0;
        int row1;
    };
    std::vector<Band> bands;
    for (int i = 0; i < count; ++i) {
        const int blockRows = (levels[i].height + 3) / 4;
        for (int r = 0; r < blockRows; r += kBandRows)
            bands.push_back({ i, r, r + kBandRows < blockRows ? r + kBandRows : blockRows });
    }
    // Each block is encoded from its own BC block only: any split gives the same bytes.
    System::ParallelFor(0, static_cast<int>(bands.size()), 1, [&](int begin, int end) {
        for (int b = begin; b < end; ++b) {
            const Band& band = bands[static_cast<size_t>(b)];
            const Level& level = levels[band.level];
            uint8_t* dst = out + offsets[static_cast<size_t>(band.level)];
            if (sourceFormat == 0) {
                BcToEtc2::TranscodeBC1(level.data, level.width, level.height, punch, dst, band.row0, band.row1);
            } else {
                BcToEtc2::TranscodeBC3(level.data, level.width, level.height, dst, band.row0, band.row1);
            }
        }
    });
}

}  // namespace

bool Transcode(const Level* levels, int count, int width, int height, int sourceFormat, Ext::DerivedCache::Store* cache,
               std::vector<uint8_t>* out, std::vector<size_t>* offsets, Target* target) {
    if (count <= 0) return false;
    offsets->assign(static_cast<size_t>(count) + 1, 0);
    uint64_t source = 0;
    uint64_t pixels = 0;
    for (int i = 0; i < count; ++i) {
        const size_t need = LevelBytes(sourceFormat, levels[i].width, levels[i].height);
        if (levels[i].data == nullptr || levels[i].size < need) return false;
        source += need;
        pixels += static_cast<uint64_t>(levels[i].width) * static_cast<uint64_t>(levels[i].height);
        (*offsets)[static_cast<size_t>(i) + 1] = (*offsets)[static_cast<size_t>(i)] + need;
    }
    const size_t total = (*offsets)[static_cast<size_t>(count)];
    bool punch = false;
    if (sourceFormat == 0) {
        for (int i = 0; i < count && !punch; ++i)
            punch = BcToEtc2::Bc1HasTransparency(levels[i].data, levels[i].width, levels[i].height);
    }
    *target = sourceFormat != 0 ? Target::Rgba8 : (punch ? Target::Rgb8A1 : Target::Rgb8);
    if (punch) g_punchThrough.fetch_add(1, std::memory_order_relaxed);

    Ext::DerivedCache::Key key;
    if (cache != nullptr) {
        const auto t0 = std::chrono::steady_clock::now();
        key.producer = Ext::DerivedCache::MakeProducer('E', 'T', 'C', '2');
        key.version  = kEncoderVersion;
        key.shape[0] = static_cast<uint32_t>(width);
        key.shape[1] = static_cast<uint32_t>(height);
        key.shape[2] = static_cast<uint32_t>(count);
        key.shape[3] = static_cast<uint32_t>(sourceFormat);
        key.sourceSize = source;
        uint64_t h = 0;
        for (int i = 0; i < count; ++i) {
            const size_t bytes = (*offsets)[static_cast<size_t>(i) + 1] - (*offsets)[static_cast<size_t>(i)];
            h = Ext::DerivedCache::Hash64(levels[i].data, bytes, h);
        }
        key.sourceHash = h;
        const bool hit = cache->Find(key, *out) && out->size() == total;
        g_lookupNs.fetch_add(Since(t0), std::memory_order_relaxed);
        if (hit) {
            g_textures.fetch_add(1, std::memory_order_relaxed);
            g_hits.fetch_add(1, std::memory_order_relaxed);
            g_bytes.fetch_add(total, std::memory_order_relaxed);
            g_pixels.fetch_add(pixels, std::memory_order_relaxed);
            return true;
        }
    }

    const auto t0 = std::chrono::steady_clock::now();
    out->assign(total, 0);
    EncodeParallel(levels, count, sourceFormat, punch, *offsets, out->data());
    g_encodeNs.fetch_add(Since(t0), std::memory_order_relaxed);
    g_textures.fetch_add(1, std::memory_order_relaxed);
    g_encoded.fetch_add(1, std::memory_order_relaxed);
    g_bytes.fetch_add(total, std::memory_order_relaxed);
    g_pixels.fetch_add(pixels, std::memory_order_relaxed);
    if (cache != nullptr) cache->Put(key, *out);   // a copy: `out` goes on to the texture
    return true;
}

void LoadEnd() {
    const uint64_t seen = g_textures.load(std::memory_order_relaxed);
    if (seen == g_printed) return;
    g_printed = seen;
    const uint64_t bytes = g_bytes.load(std::memory_order_relaxed);
    std::fprintf(stderr,
                 "[Etc2] textures=%llu cache_hits=%llu encoded=%llu punch_through=%llu etc2=%.1f MB (RGBA8 would be "
                 "%.1f MB) encode=%.0f ms lookup=%.0f ms\n",
                 static_cast<unsigned long long>(seen),
                 static_cast<unsigned long long>(g_hits.load(std::memory_order_relaxed)),
                 static_cast<unsigned long long>(g_encoded.load(std::memory_order_relaxed)),
                 static_cast<unsigned long long>(g_punchThrough.load(std::memory_order_relaxed)),
                 static_cast<double>(bytes) / 1048576.0,
                 static_cast<double>(g_pixels.load(std::memory_order_relaxed)) * 4.0 / 1048576.0,
                 static_cast<double>(g_encodeNs.load(std::memory_order_relaxed)) / 1e6,
                 static_cast<double>(g_lookupNs.load(std::memory_order_relaxed)) / 1e6);
}

}  // namespace Etc2Chain
