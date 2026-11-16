// ============================================================================
// src/render/common/texture_quality/texture_quality.cpp -- ext.texture_quality: the run's drop and
// its counters (docs/extensions/handins/TEXQUALITY.md). The contract is in texture_quality.h.
// ============================================================================
// NOT RETAIL.
// ============================================================================

#include "texture_quality/texture_quality.h"

#include "video/ImageBuffer.h"

#include <atomic>
#include <cstdio>
#include <mutex>

namespace TextureQuality {

namespace {

std::atomic<int> g_drop{0};

// Per run; relaxed: a summary, read by the loader bracket's close.
std::atomic<int64_t> g_dropped{0};
std::atomic<int64_t> g_droppedLazy{0};
std::atomic<int64_t> g_skippedBytes{0};
std::atomic<int64_t> g_discardedBytes{0};
std::atomic<int64_t> g_keptBytes{0};
std::atomic<int64_t> g_kept[kVerdictCount];   // images read with every level, by verdict
std::atomic<int>     g_logged[kVerdictCount];

// The first images of each verdict, dropped ones included, are named in the log (the rule's check:
// sprite sheets, fonts and small textures must be among the kept ones, world textures must not).
constexpr int kLogPerVerdict = 12;

int64_t g_lastDropped = -1;
int64_t g_lastKept    = -1;

}  // namespace

void SetDrop(int levels) {
    const int d = levels < 0 ? 0 : (levels > kMaxDrop ? kMaxDrop : levels);
    g_drop.store(d, std::memory_order_relaxed);
}

int Drop() {
    return g_drop.load(std::memory_order_relaxed);
}

Verdict ClassifyImage(const Video::ImageBuffer& image, int32_t fileLevels) {
    const int drop = Drop();
    if (drop == 0) return Verdict::Off;   // the row off: one load, no level ladder walked
    return Classify(drop, image._kind, image._dimension, image._flags, image._width, image._height,
                    image.GetLevelCount(), fileLevels);
}

int DropFor(const Video::ImageBuffer& image, int32_t fileLevels) {
    return ClassifyImage(image, fileLevels) == Verdict::Drop ? Drop() : 0;
}

void NoteImage(const Video::ImageBuffer& image, int32_t fileLevels, Verdict v) {
    if (v == Verdict::Off) return;
    const int i = static_cast<int>(v);
    if (i >= kVerdictCount) return;
    if (v != Verdict::Drop) g_kept[i].fetch_add(1, std::memory_order_relaxed);
    if (g_logged[i].fetch_add(1, std::memory_order_relaxed) >= kLogPerVerdict) return;
    const char* const path = image._path._data != nullptr ? image._path._data : "(no path)";
    const bool dropped = v == Verdict::Drop;
    std::fprintf(stderr, "[TextureQuality] %s%s%s: %s %dx%d levels %d/%d format %d flags 0x%x\n",
                 dropped ? "" : "full (", VerdictName(v), dropped ? "" : ")", path, image._width, image._height,
                 fileLevels, image.GetLevelCount(), image._format, image._flags);
}

void NoteDropped(bool lazy, int64_t skippedBytes, int64_t discardedBytes, int64_t keptBytes) {
    g_dropped.fetch_add(1, std::memory_order_relaxed);
    if (lazy) g_droppedLazy.fetch_add(1, std::memory_order_relaxed);
    g_skippedBytes.fetch_add(skippedBytes, std::memory_order_relaxed);
    g_discardedBytes.fetch_add(discardedBytes, std::memory_order_relaxed);
    g_keptBytes.fetch_add(keptBytes, std::memory_order_relaxed);
}

void PrintIfChanged(const char* when) {
    const int drop = Drop();
    if (drop == 0) return;
    static std::mutex printMutex;
    std::lock_guard<std::mutex> lock(printMutex);
    const int64_t dropped = g_dropped.load(std::memory_order_relaxed);
    int64_t kept = 0;
    int64_t byVerdict[kVerdictCount] = {};
    for (int i = static_cast<int>(Verdict::NotUsage2); i < kVerdictCount; ++i) {
        byVerdict[i] = g_kept[i].load(std::memory_order_relaxed);
        kept += byVerdict[i];
    }
    if (dropped == g_lastDropped && kept == g_lastKept) return;
    g_lastDropped = dropped;
    g_lastKept    = kept;
    const int64_t lazy      = g_droppedLazy.load(std::memory_order_relaxed);
    const int64_t skipped   = g_skippedBytes.load(std::memory_order_relaxed);
    const int64_t discarded = g_discardedBytes.load(std::memory_order_relaxed);
    const int64_t keptBytes = g_keptBytes.load(std::memory_order_relaxed);
    const double mb = 1.0 / (1024.0 * 1024.0);
    std::fprintf(stderr,
                 "[TextureQuality] %s: drop=%d textures=%lld (lazy %lld, eager %lld) skipped_bytes=%lld "
                 "(%.1f MB never decoded, %.1f MB decoded and discarded) kept_bytes=%lld (%.1f MB); full: "
                 "%lld (not mipmapped %lld, cube %lld, level count %lld, few levels %lld, small %lld, odd size "
                 "%lld)\n",
                 when, drop, static_cast<long long>(dropped), static_cast<long long>(lazy),
                 static_cast<long long>(dropped - lazy), static_cast<long long>(skipped + discarded), skipped * mb,
                 discarded * mb, static_cast<long long>(keptBytes), keptBytes * mb, static_cast<long long>(kept),
                 static_cast<long long>(byVerdict[static_cast<int>(Verdict::NotMipmapped)]),
                 static_cast<long long>(byVerdict[static_cast<int>(Verdict::Cube)]),
                 static_cast<long long>(byVerdict[static_cast<int>(Verdict::LevelCount)]),
                 static_cast<long long>(byVerdict[static_cast<int>(Verdict::FewLevels)]),
                 static_cast<long long>(byVerdict[static_cast<int>(Verdict::Small)]),
                 static_cast<long long>(byVerdict[static_cast<int>(Verdict::OddSize)]));
}

}  // namespace TextureQuality
