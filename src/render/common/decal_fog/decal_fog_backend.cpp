// ============================================================================
// src/render/common/decal_fog/decal_fog_backend.cpp -- decal_fog_backend.h (the shared half)
// ============================================================================
// NOT RETAIL (fix.decal_fog_order, docs/retail_bugs.md section 32). Log lines: `[DecalFog] ...` on
// stderr (standalone_error.log), at program load only, capped.
// ============================================================================
#include "decal_fog/decal_fog_backend.h"

#include "decal_fog/decal_fog_rewrite.h"

#include <cstdio>

namespace DecalFogBackend {

namespace {

std::atomic<const std::atomic<bool>*> g_live{nullptr};
std::atomic<int> g_logLines{0};
constexpr int kMaxLogLines = 24;   // a few programs x four permutations, then silence

bool Log() { return g_logLines.fetch_add(1, std::memory_order_relaxed) < kMaxLogLines; }

}  // namespace

void SetFixFlag(const std::atomic<bool>* live) {
    g_live.store(live, std::memory_order_release);
}

bool FixedCopy(const uint8_t* blob, size_t size, std::vector<uint8_t>* out) {
    const std::atomic<bool>* live = g_live.load(std::memory_order_acquire);
    if (live == nullptr || !live->load(std::memory_order_relaxed)) return false;
    DecalFog::Match m;
    DecalFog::Analyse(blob, size, &m);
    if (!m.matched) {
        // A program with the tint that is refused for any reason but the right order is worth a line:
        // a mod's program, or a changed install, the rewrite does not know.
        if (m.tint && !m.tintFirst && Log())
            std::fprintf(stderr, "[DecalFog] pixel program with the underwater tint kept retail: %s\n", m.why.c_str());
        return false;
    }
    if (!DecalFog::Transform(m, out)) {
        if (Log()) std::fprintf(stderr, "[DecalFog] rewrite FAILED its self-check: kept retail\n");
        return false;
    }
    if (Log())
        std::fprintf(stderr, "[DecalFog] decal pixel program: tint before fog (%zu B -> %zu B)\n", size, out->size());
    return true;
}

}  // namespace DecalFogBackend
