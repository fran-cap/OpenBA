// ============================================================================
// src/render/vulkan/shader_rewrites.cpp -- shader_rewrites.h, and the bodies of FaaBackend::,
// BbsBackend:: and SmokeVsBackend:: (src/render/common/better_trees/, draw_batch/)
// ============================================================================
// NOT RETAIL (P4). The three stages of src/render/dx11/better_trees/foliage_aa_stage.cpp,
// billboard_shadow_stage.cpp and src/render/dx11/draw_batch/smoke_vs_stage.cpp over one mutex, with
// the D3D shader creates replaced by a conversion into a shader-registry entry. Log lines keep the
// DX11 tags: `[FoliageAA]`, `[BillboardShadow]`, `[DrawBatch]` on stderr (standalone_error.log).
// ============================================================================
#include "shader_rewrites.h"

#include "VideoVulkanData.h"
#include "device_objects.h"
#include "pipelines.h"
#include "shader_convert.h"

#include "better_trees/billboard_shadow_backend.h"
#include "better_trees/cutout_rewrite.h"
#include "better_trees/edge_dither.h"
#include "better_trees/foliage_aa_backend.h"
#include "better_trees/natural_growth.h"
#include "better_trees/receiver_rewrite.h"
#include "draw_batch/smoke_vs_backend.h"
#include "draw_batch/smoke_vs_rewrite.h"
#include "ext/derived_cache/derived_cache.h"
#include "video/Device.h"          // Video::Configuration::multisample (foliage_aa's MSAA test)
#include "video/ShaderProgram.h"   // the programs' DXBC permutations, Video::VertexProgram::_data

#include <atomic>
#include <cstdio>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace VideoVulkan {
namespace Rewrites {

namespace {

struct Copies {
    ShaderEntry* xf[kPermutationCount] = {};       // the rewritten permutations (null: retail)
    ShaderEntry* retail[kPermutationCount] = {};   // what they were made beside (a re-create is detected)
    bool cov[kPermutationCount] = {};               // the copy writes coverage (foliage_aa)
};

using CopyMap = std::unordered_map<const void*, Copies>;

struct FaaState {
    // requested (main thread, FaaBackend::Request) ...
    int reqMode = 0;
    float reqK = 0.0f;
    float reqFloor = 0.0f;
    BbsRewrite::Params reqDither = BbsRewrite::kRetailParams;
    bool reqNg = false;   // NATURAL_GROWTH.md
    uint32_t reqGen = 0;
    // ... and applied (the binding thread / the loaders)
    uint32_t gen = 0;
    int mode = 0;
    bool msaa = false;
    FaaRewrite::Options opt;
    BbsRewrite::Params dither = BbsRewrite::kRetailParams;
    bool ng = false;
    CopyMap programs;
    int blobsSeen = 0;
    int blobsTransformed = 0;
    int programsTransformed = 0;
    int createFailed = 0;
    int64_t xfBinds = 0;
    int64_t pixelBinds = 0;
};

struct VertexStage {
    bool reqOn = false;
    bool reqNg = false;   // NATURAL_GROWTH.md (billboard_shadow's stage only) ...
    bool ng = false;      // ... and what the current copies were made with
#if BANISHED_TWEAK_OVERLAY
    BbsRewrite::Params reqParams = BbsRewrite::kDefaultParams;   // billboard_shadow only (TS-TWEAK)
    BbsRewrite::Params params = BbsRewrite::kDefaultParams;
#endif
    CopyMap programs;
    int blobsSeen = 0;
    int blobsTransformed = 0;
    int createFailed = 0;
    int64_t xfBinds = 0;
};

std::mutex g_mu;
FaaState g_faa;
VertexStage g_bbs;
VertexStage g_smoke;
std::set<std::string> g_logged;   // a blob is logged once per run (per generation when transformed)
std::atomic<bool> g_active{false};

// Under g_mu: a stage is active while it is requested or still holds copies to release at a bind.
void UpdateActive() {
    const bool any = g_faa.reqMode > 0 || g_faa.mode > 0 || !g_faa.programs.empty() || g_bbs.reqOn ||
                     !g_bbs.programs.empty() || g_smoke.reqOn || !g_smoke.programs.empty();
    g_active.store(any, std::memory_order_release);
}

std::string HashName(const uint32_t sum[4]) {
    char b[24];
    std::snprintf(b, sizeof b, "%08x%08x", sum[0], sum[1]);
    return b;
}

void ReleaseCopies(Copies& c) {
    for (ShaderEntry*& x : c.xf) {
        ReleaseShader(x);
        x = nullptr;
    }
    for (bool& b : c.cov) b = false;
}

void ReleaseAll(CopyMap& map) {
    for (auto& kv : map)
        ReleaseCopies(kv.second);
    map.clear();
}

// The registry entry of a rewritten blob (one reference): an equal blob already registered is shared,
// else it is converted (through the user's derived-data cache: our transform of their own bytecode)
// and made a module. Null with *why on failure.
// NATURAL_GROWTH.md: natural growth on (`blob`, `size`) when it matches, into `out`; the caller then reads `out`.
bool Grow(const uint8_t* blob, size_t size, std::vector<uint8_t>* out, const char* tag, int i) {
    NgRewrite::Match nm;
    NgRewrite::Analyse(blob, size, &nm);
    if (nm.kind == NgRewrite::Kind::None) return false;
    const std::string hash = HashName(nm.cont.checksum);
    if (!NgRewrite::Transform(nm, out)) {
        std::fprintf(stderr, "[NaturalGrowth] %s rewrite of %s v%d FAILED its self-check: kept retail\n", tag, hash.c_str(), i);
        return false;
    }
    if (g_logged.insert("ng" + hash).second) {
        std::fprintf(stderr, "[NaturalGrowth] rewrote %s v%d (%s): %zu B -> %zu B (Vulkan)\n", hash.c_str(), i,
                     NgRewrite::KindName(nm.kind), size, out->size());
    }
    return true;
}

ShaderEntry* EntryOf(const std::vector<uint8_t>& dxbc, Shaders::Stage stage, std::string* why) {
    const uint64_t hash = ShaderHash(dxbc.data(), dxbc.size(), stage);
    if (ShaderEntry* e = AcquireShader(hash)) return e;
    std::vector<uint32_t> spirv;
    Shaders::ShaderReflection refl;
    bool hit = false;
    Shaders::ConvertOptions options;
    options.combineSamplers = true;   // P8: as the retail programs (shader_convert.h)
    if (!Shaders::LoadOrConvert(Ext::DerivedCache::OpenShared(), dxbc.data(), dxbc.size(), stage, &spirv, &refl, &hit,
                                why, options)) {
        return nullptr;
    }
    VkShaderModule module = CreateShaderModule(spirv, why);
    if (!module) return nullptr;
    return AddShader(hash, stage, module, refl, dxbc.data(), dxbc.size());
}

bool Matches(const Copies& c, ShaderEntry* const shaders[kPermutationCount], int permutation) {
    return c.retail[permutation] == shaders[permutation];
}

// ---- foliage_aa (foliage_aa_stage.cpp) ----

bool MsaaNow() {
    const Video::Configuration* cfg = Video::Configuration::_singleton;
    return cfg != nullptr && cfg->multisample > 0;
}

// Under g_mu: every copy released, the requested generation applied.
void FaaReset(const char* why) {
    if (g_faa.gen != 0 || g_faa.programsTransformed) {
        std::fprintf(stderr,
                     "[FoliageAA] stage reset (%s): previous gen %u mode %d -- %d blobs seen, %d transformed "
                     "(%d programs), %d create failures, %lld of %lld pixel binds transformed\n",
                     why, g_faa.gen, g_faa.mode, g_faa.blobsSeen, g_faa.blobsTransformed, g_faa.programsTransformed,
                     g_faa.createFailed, static_cast<long long>(g_faa.xfBinds),
                     static_cast<long long>(g_faa.pixelBinds));
    }
    ReleaseAll(g_faa.programs);
    g_faa.blobsSeen = 0;
    g_faa.blobsTransformed = 0;
    g_faa.programsTransformed = 0;
    g_faa.createFailed = 0;
    g_faa.xfBinds = 0;
    g_faa.pixelBinds = 0;
    g_faa.gen = g_faa.reqGen;
    g_faa.mode = g_faa.reqMode;
    g_faa.msaa = MsaaNow();
    g_faa.opt = FaaRewrite::OptionsFor(g_faa.mode, g_faa.reqK, g_faa.msaa, g_faa.reqFloor);
    g_faa.dither = g_faa.reqDither;
    g_faa.ng = g_faa.mode > 0 && g_faa.reqNg;
    if (g_faa.mode > 0) {
        std::fprintf(stderr, "[FoliageAA] stage gen %u: mode %d (%s), MSAA %s -> %s, fill k %.3f floor %.3f, "
                             "edge dither noise %.2f scale %.0f (Vulkan: copies through the shader converter)\n",
                     g_faa.gen, g_faa.mode, g_faa.mode == 2 ? "smooth edges + distance fill" : "smooth edges",
                     g_faa.msaa ? "on" : "off",
                     g_faa.opt.coverage ? "alpha to coverage, discard dropped" : "discard kept",
                     static_cast<double>(g_faa.opt.fillK), static_cast<double>(g_faa.opt.fillFloor),
                     static_cast<double>(g_faa.dither.noise), static_cast<double>(g_faa.dither.noiseScale));
    } else {
        std::fprintf(stderr, "[FoliageAA] stage off: copies released\n");
    }
    UpdateActive();
}

// Under g_mu: the request, the MSAA setting still those the copies were made for (else a reset).
void FaaCurrent() {
    if (g_faa.gen != g_faa.reqGen) {
        FaaReset("mode / k request");
    } else if (g_faa.mode > 0 && g_faa.msaa != MsaaNow()) {
        FaaReset("MSAA changed");
    }
}

// Under g_mu: this program's copies, made now from its own byte code (foliage_aa_stage.cpp Build: the
// cutout where it matches, then the edge dither on the result, on the cutout programs only).
Copies& FaaBuild(PixelProgramData* p) {
    Copies& c = g_faa.programs[p];
    ReleaseCopies(c);
    const Video::ShaderProgram* owner = p->_owner;
    bool any = false;
    for (int i = 0; i < kPermutationCount; ++i) {
        c.retail[i] = p->shaders[i];
        if (i >= owner->_byteCode.GetSize() || p->shaders[i] == nullptr) continue;
        const Collection::Array<uint8_t>& b = owner->_byteCode.GetData()[i];
        const uint8_t* blob = b.GetData();
        size_t size = static_cast<size_t>(b.GetSize());
        ++g_faa.blobsSeen;
        // NATURAL_GROWTH.md: natural growth first (it keeps the cutout's grammar).
        std::vector<uint8_t> grown;
        const bool ng = g_faa.ng && Grow(blob, size, &grown, "pixel", i);
        if (ng) {
            blob = grown.data();
            size = grown.size();
        }
        FaaRewrite::Match m;
        FaaRewrite::Analyse(blob, size, &m);
        const std::string hash = m.container ? HashName(m.cont.checksum) : std::string("(no container)");
        std::vector<uint8_t> cur(blob, blob + size);
        bool changed = ng;
        if (!m.colour) {
            if ((m.discards > 0 || !m.program) && g_logged.insert("fu" + hash).second) {
                std::fprintf(stderr, "[FoliageAA] untouched %s v%d (%zu B): %s\n", hash.c_str(), i, size, m.why.c_str());
            }
        } else {
            std::vector<uint8_t> cut;
            if (FaaRewrite::Transform(m, g_faa.opt, &cut)) {
                cur = std::move(cut);
                changed = true;
                c.cov[i] = g_faa.opt.coverage;
                if (g_logged.insert("ft" + std::to_string(g_faa.gen) + ":" + hash).second) {
                    std::fprintf(stderr, "[FoliageAA] transformed %s v%d gen %u%s%s: %zu B -> %zu B\n", hash.c_str(), i,
                                 g_faa.gen, g_faa.opt.coverage ? " coverage" : "",
                                 g_faa.opt.fillK > 0.0f ? " fill" : "", size, cur.size());
                }
            } else if (g_faa.opt.coverage || g_faa.opt.fillK > 0.0f) {
                ++g_faa.createFailed;
                std::fprintf(stderr, "[FoliageAA] transform of %s v%d FAILED its self-check: kept %s\n",
                             hash.c_str(), i, ng ? "natural growth alone" : "retail");
                if (!ng) continue;
            }
        }
        if (g_faa.dither.noise > 0.0f && m.colour) {
            BbsDither::Match dm;
            BbsDither::Analyse(cur.data(), cur.size(), &dm);
            if (dm.matched) {
                std::vector<uint8_t> dithered;
                if (BbsDither::Transform(dm, &dithered, g_faa.dither)) {
                    cur = std::move(dithered);
                    changed = true;
                } else {
                    ++g_faa.createFailed;
                    std::fprintf(stderr, "[FoliageAA] dither of %s v%d FAILED its self-check: kept the cutout / "
                                         "retail\n", hash.c_str(), i);
                }
            }
        }
        if (!changed) continue;
        std::string why;
        c.xf[i] = EntryOf(cur, Shaders::Stage::Pixel, &why);
        if (c.xf[i] != nullptr && ng) c.xf[i]->naturalGrowth = true;
        if (c.xf[i] == nullptr) {
            c.cov[i] = false;
            ++g_faa.createFailed;
            std::fprintf(stderr, "[FoliageAA] the copy of %s v%d did not convert (%s): kept retail\n", hash.c_str(), i,
                         why.c_str());
            continue;
        }
        any = true;
        ++g_faa.blobsTransformed;
    }
    if (any) ++g_faa.programsTransformed;
    return c;
}

// ---- the vertex stages (billboard_shadow_stage.cpp, smoke_vs_stage.cpp) ----

enum class VertexKind { Smoke, Billboard };

const char* Tag(VertexKind k) {
    return k == VertexKind::Smoke ? "[DrawBatch] smoke VS" : "[BillboardShadow]";
}

void VertexRelease(VertexStage& s, VertexKind k, const char* why) {
    if (s.blobsTransformed || s.xfBinds) {
        std::fprintf(stderr, "%s stage reset (%s): %d blobs seen, %d rewritten, %d create failures, %lld rewritten "
                             "binds\n",
                     Tag(k), why, s.blobsSeen, s.blobsTransformed, s.createFailed, static_cast<long long>(s.xfBinds));
    }
    ReleaseAll(s.programs);
    s.blobsSeen = 0;
    s.blobsTransformed = 0;
    s.createFailed = 0;
    s.xfBinds = 0;
}

Copies& VertexBuild(VertexStage& s, VertexKind k, VertexProgramData* p) {
    Copies& c = s.programs[p];
    ReleaseCopies(c);
    const Video::ShaderProgram* owner = p->_owner;
    for (int i = 0; i < kPermutationCount; ++i) {
        c.retail[i] = p->shaders[i];
        if (i >= owner->_byteCode.GetSize() || p->shaders[i] == nullptr) continue;
        const Collection::Array<uint8_t>& b = owner->_byteCode.GetData()[i];
        const uint8_t* blob = b.GetData();
        const size_t size = static_cast<size_t>(b.GetSize());
        ++s.blobsSeen;
        std::vector<uint8_t> out;
        bool built = false;
        bool ng = false;
        std::string name;
        if (k == VertexKind::Smoke) {
            SmokeVs::Match m;
            SmokeVs::Analyse(blob, size, &m);
            if (!m.matched) continue;   // not a particle program: untouched, silently
            built = SmokeVs::Transform(m, &out);
            name = m.container ? HashName(m.cont.checksum) : std::string("(no container)");
        } else {
            // NATURAL_GROWTH.md: natural growth first (billboard colour / depth, foliage), then the receiver.
            std::vector<uint8_t> grown;
            ng = s.ng && Grow(blob, size, &grown, "vertex", i);
            const uint8_t* src = ng ? grown.data() : blob;
            const size_t srcSize = ng ? grown.size() : size;
            BbsRewrite::Match m;
            BbsRewrite::Analyse(src, srcSize, &m);
            name = m.container ? HashName(m.cont.checksum) : std::string("(no container)");
            if (!m.matched) {
                if ((m.candidate || !m.program) && g_logged.insert("bu" + name).second) {
                    std::fprintf(stderr, "[BillboardShadow] untouched %s v%d (%zu B): %s\n", name.c_str(), i, srcSize,
                                 m.why.c_str());
                }
                if (!ng) continue;
                out = std::move(grown);
                built = true;
            } else {
#if BANISHED_TWEAK_OVERLAY
                built = BbsRewrite::TransformWithParams(m, &out, s.params);
#else
                built = BbsRewrite::Transform(m, &out);
#endif
            }
        }
        if (!built) {
            ++s.createFailed;
            std::fprintf(stderr, "%s rewrite of %s v%d FAILED its self-check: kept retail\n", Tag(k), name.c_str(), i);
            continue;
        }
        std::string why;
        c.xf[i] = EntryOf(out, Shaders::Stage::Vertex, &why);
        if (c.xf[i] != nullptr && ng) c.xf[i]->naturalGrowth = true;
        if (c.xf[i] == nullptr) {
            ++s.createFailed;
            std::fprintf(stderr, "%s copy of %s v%d did not convert (%s): kept retail\n", Tag(k), name.c_str(), i,
                         why.c_str());
            continue;
        }
        ++s.blobsTransformed;
        if (g_logged.insert(std::string(k == VertexKind::Smoke ? "st" : "bt") + name).second) {
            std::fprintf(stderr, "%s rewrote %s v%d: %zu B -> %zu B (Vulkan: through the shader converter)\n", Tag(k),
                         name.c_str(), i, size, out.size());
        }
    }
    return c;
}

// Under g_mu: the stage's copy of `p`'s permutation (made when missing), or null.
ShaderEntry* VertexCopy(VertexStage& s, VertexKind k, VertexProgramData* p, int permutation) {
    if (!s.reqOn) {
        if (!s.programs.empty()) {
            VertexRelease(s, k, "off");
            std::fprintf(stderr, "%s stage off: copies released\n", Tag(k));
        }
        return nullptr;
    }
    if (s.ng != s.reqNg) {   // NATURAL_GROWTH.md: re-made with (or without) it
        VertexRelease(s, k, s.reqNg ? "natural growth on" : "natural growth off");
        s.ng = s.reqNg;
    }
    auto it = s.programs.find(p);
    Copies* c = it != s.programs.end() ? &it->second : nullptr;
    if (c == nullptr || !Matches(*c, p->shaders, permutation)) c = &VertexBuild(s, k, p);
    ShaderEntry* const xf = c->xf[permutation];
    if (xf != nullptr && ++s.xfBinds == 1) std::fprintf(stderr, "%s first rewritten bind\n", Tag(k));
    return xf;
}

bool Complete(const Copies& c, const VertexProgramData* p) {
    bool any = false;
    for (int i = 0; i < kPermutationCount; ++i) {
        if (p->shaders[i] == nullptr) continue;
        if (c.xf[i] == nullptr || c.retail[i] != p->shaders[i]) return false;
        any = true;
    }
    return any;
}

}  // namespace

bool Active() {
    return g_active.load(std::memory_order_acquire);
}

ShaderEntry* BindVertex(VertexProgramData* p, int permutation) {
    std::lock_guard<std::mutex> lock(g_mu);
#if BANISHED_TWEAK_OVERLAY
    if (g_bbs.params != g_bbs.reqParams) {   // TS-TWEAK: a new set; the copies are re-made with it
        VertexRelease(g_bbs, VertexKind::Billboard, "params changed");
        g_bbs.params = g_bbs.reqParams;
    }
#endif
    // dx11 shaderprograms.cpp's order: draw_batch's copy first, then billboard_shadow's.
    ShaderEntry* xf = VertexCopy(g_smoke, VertexKind::Smoke, p, permutation);
    ShaderEntry* const bbs = VertexCopy(g_bbs, VertexKind::Billboard, p, permutation);
    if (xf == nullptr) xf = bbs;
    UpdateActive();
    return xf != nullptr ? AcquireShader(xf->hash) : nullptr;
}

ShaderEntry* BindPixel(PixelProgramData* p, int permutation, bool* coverage) {
    *coverage = false;
    std::lock_guard<std::mutex> lock(g_mu);
    FaaCurrent();
    if (g_faa.mode == 0) {
        UpdateActive();
        return nullptr;
    }
    ++g_faa.pixelBinds;
    auto it = g_faa.programs.find(p);
    Copies* c = it != g_faa.programs.end() ? &it->second : nullptr;
    if (c == nullptr || !Matches(*c, p->shaders, permutation)) c = &FaaBuild(p);
    ShaderEntry* const xf = c->xf[permutation];
    if (xf == nullptr) return nullptr;
    if (++g_faa.xfBinds == 1) std::fprintf(stderr, "[FoliageAA] first transformed bind of gen %u\n", g_faa.gen);
    *coverage = c->cov[permutation];
    return AcquireShader(xf->hash);
}

void VertexCreated(VertexProgramData* p) {
    std::lock_guard<std::mutex> lock(g_mu);
    if (g_smoke.reqOn) VertexBuild(g_smoke, VertexKind::Smoke, p);
    if (g_bbs.reqOn && g_bbs.ng == g_bbs.reqNg) VertexBuild(g_bbs, VertexKind::Billboard, p);
    UpdateActive();
}

void PixelCreated(PixelProgramData* p) {
    std::lock_guard<std::mutex> lock(g_mu);
    // A pending request (the boot request, before the first draw) is applied here, so the copies are
    // made at load instead of at the first bind (nothing of the stage is bound on this backend).
    if (g_faa.reqMode > 0) FaaCurrent();
    if (g_faa.mode == 0 || g_faa.gen != g_faa.reqGen) {
        auto it = g_faa.programs.find(p);
        if (it != g_faa.programs.end()) {
            ReleaseCopies(it->second);
            g_faa.programs.erase(it);
        }
        return;
    }
    FaaBuild(p);
    UpdateActive();
}

void VertexDestroyed(VertexProgramData* p) {
    std::lock_guard<std::mutex> lock(g_mu);
    for (VertexStage* s : { &g_smoke, &g_bbs }) {
        auto it = s->programs.find(p);
        if (it == s->programs.end()) continue;
        ReleaseCopies(it->second);
        s->programs.erase(it);
    }
    UpdateActive();
}

void PixelDestroyed(PixelProgramData* p) {
    std::lock_guard<std::mutex> lock(g_mu);
    auto it = g_faa.programs.find(p);
    if (it == g_faa.programs.end()) return;
    ReleaseCopies(it->second);
    g_faa.programs.erase(it);
    UpdateActive();
}

void Shutdown() {
    std::lock_guard<std::mutex> lock(g_mu);
    ReleaseAll(g_faa.programs);
    ReleaseAll(g_bbs.programs);
    ReleaseAll(g_smoke.programs);
    // The next device applies the request again (the copies are re-made per program).
    g_faa.gen = g_faa.reqGen - 1;
    UpdateActive();
}

}  // namespace Rewrites
}  // namespace VideoVulkan

// ---- the backend-neutral interfaces ----------------------------------------------------------------

namespace FaaBackend {

// foliage_aa_stage.cpp's Request: the same normalisation and the same generation rule.
void Request(int mode, float fillK, float fillFloor, const BbsRewrite::Params* dither, bool naturalGrowth) {
    using namespace VideoVulkan::Rewrites;
    std::lock_guard<std::mutex> lock(g_mu);
    if (mode < 0 || mode > 2) mode = 0;
    if (mode == 0) fillK = 0.0f;
    if (fillK <= 0.0f) fillFloor = 0.0f;
    BbsRewrite::Params d = BbsRewrite::kRetailParams;
    if (dither != nullptr && mode > 0) {
        d.noise = dither->noise > 0.0f ? dither->noise : 0.0f;
        d.noiseScale = dither->noiseScale > 0.0f ? dither->noiseScale : BbsRewrite::kRetailParams.noiseScale;
    }
    naturalGrowth = mode > 0 && naturalGrowth;
    if (mode == g_faa.reqMode && fillK == g_faa.reqK && fillFloor == g_faa.reqFloor && d == g_faa.reqDither &&
        naturalGrowth == g_faa.reqNg)
        return;
    g_faa.reqNg = naturalGrowth;
    g_faa.reqMode = mode;
    g_faa.reqK = fillK;
    g_faa.reqFloor = fillFloor;
    g_faa.reqDither = d;
    ++g_faa.reqGen;
    UpdateActive();
    std::fprintf(stderr, "[FoliageAA] request mode %d k %.3f floor %.3f dither %.2f/%.0f (gen %u)\n", mode,
                 static_cast<double>(fillK), static_cast<double>(fillFloor), static_cast<double>(d.noise),
                 static_cast<double>(d.noiseScale), g_faa.reqGen);
}

#if BANISHED_TWEAK_OVERLAY
TweakStats Stats() {
    using namespace VideoVulkan::Rewrites;
    std::lock_guard<std::mutex> lock(g_mu);
    TweakStats t;
    t.reqMode = g_faa.reqMode;
    t.mode = g_faa.mode;
    t.reqGen = g_faa.reqGen;
    t.gen = g_faa.gen;
    t.msaa = g_faa.msaa;
    t.coverage = g_faa.opt.coverage;
    t.fillK = g_faa.opt.fillK;
    t.fillFloor = g_faa.opt.fillFloor;
    t.blobsSeen = g_faa.blobsSeen;
    t.blobsTransformed = g_faa.blobsTransformed;
    t.programsTransformed = g_faa.programsTransformed;
    t.createFailed = g_faa.createFailed;
    t.xfBinds = g_faa.xfBinds;
    t.pixelBinds = g_faa.pixelBinds;
    return t;
}
#endif

}  // namespace FaaBackend

namespace BbsBackend {

void Request(bool on, bool naturalGrowth) {
    using namespace VideoVulkan::Rewrites;
    std::lock_guard<std::mutex> lock(g_mu);
    g_bbs.reqNg = on && naturalGrowth;
    if (on == g_bbs.reqOn) return;
    g_bbs.reqOn = on;
    UpdateActive();
    std::fprintf(stderr, "[BillboardShadow] request %s\n", on ? "on" : "off: released at the next vertex bind");
}

#if BANISHED_TWEAK_OVERLAY
void RequestParams(const BbsRewrite::Params& p) {
    using namespace VideoVulkan::Rewrites;
    std::lock_guard<std::mutex> lock(g_mu);
    g_bbs.reqParams = p;
}

BbsRewrite::Params AppliedParams() {
    using namespace VideoVulkan::Rewrites;
    std::lock_guard<std::mutex> lock(g_mu);
    return g_bbs.params;
}
#endif

}  // namespace BbsBackend

namespace SmokeVsBackend {

void Request(bool on) {
    using namespace VideoVulkan::Rewrites;
    std::lock_guard<std::mutex> lock(g_mu);
    if (on == g_smoke.reqOn) return;
    g_smoke.reqOn = on;
    UpdateActive();
    std::fprintf(stderr, "[DrawBatch] smoke VS request %s\n", on ? "on" : "off: released at the next vertex bind");
}

// smoke_vs_stage.cpp's Rewritten: every compiled permutation has its copy, made beside the permutation
// the program holds now.
bool Rewritten(const Video::VertexProgram* vp) {
    using namespace VideoVulkan::Rewrites;
    if (vp == nullptr || vp->_data == nullptr) return false;
    std::lock_guard<std::mutex> lock(g_mu);
    if (!g_smoke.reqOn) return false;
    const auto* p = static_cast<const VideoVulkan::VertexProgramData*>(vp->_data);
    auto it = g_smoke.programs.find(p);
    if (it == g_smoke.programs.end()) return false;
    return Complete(it->second, p);
}

}  // namespace SmokeVsBackend
