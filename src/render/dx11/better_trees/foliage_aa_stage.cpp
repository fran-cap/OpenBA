// ============================================================================
// src/render/dx11/better_trees/foliage_aa_stage.cpp -- foliage_aa_stage.h, foliage_aa_backend.h
// ============================================================================
// NOT RETAIL (ext.foliage_aa). Log lines: `[FoliageAA] ...` on stderr (standalone_error.log).
// ============================================================================
#include "better_trees/foliage_aa_stage.h"

#include "better_trees/edge_dither.h"   // BETTER-TREES: the per-texel shadow dither, after the cutout
#include "better_trees/cutout_rewrite.h"
#include "better_trees/foliage_aa_backend.h"
#include "better_trees/billboard_shadow_stage.h"   // NATURAL_GROWTH.md: g_ngVertexBound
#include "better_trees/natural_growth.h"

#include "dx11_backend_access.h"
#include "video/CompanionData.h"

#include <cstdio>
#include <cstring>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace VideoDX11 {

std::atomic<FaaStage::Stage*> g_extFoliageAA{nullptr};

namespace FaaStage {
namespace {

// One per PixelProgramData the stage has looked at (all-null `xf` = nothing matched: retail).
struct Program {
    ID3D11PixelShader* xf[kShaderVariantCount]     = {};
    ID3D11PixelShader* retail[kShaderVariantCount] = {};   // what the copies were made beside
    bool               cov[kShaderVariantCount]    = {};   // the copy writes coverage (needs the A2C clone)
    bool               ngVs[kShaderVariantCount]   = {};   // NATURAL_GROWTH.md: it reads the natural-growth interpolant
};

// One per retail blend state seen under a transformed program. `clone` null = the state blends
// (or the create failed): keep the retail program there.
struct Clone {
    ID3D11BlendState* clone = nullptr;
};

struct State {
    std::mutex mu;
    // requested (main thread, FaaBackend::Request) ...
    int      reqMode = 0;
    float    reqK    = 0.0f;
    float    reqFloor = 0.0f;
    BbsRewrite::Params reqDither = BbsRewrite::kRetailParams;   // BETTER-TREES: noise 0 = no dither
    bool     reqNg   = false;   // NATURAL_GROWTH.md
    uint32_t reqGen  = 0;
    // ... and applied (the binding thread / the loaders)
    uint32_t       gen    = 0;
    int            mode   = 0;
    bool           msaa   = false;
    ID3D11Device*  device = nullptr;
    FaaRewrite::Options opt;
    BbsRewrite::Params dither = BbsRewrite::kRetailParams;   // the set the current copies were made with
    bool           ng     = false;                          // natural growth in the current copies
    std::unordered_map<PixelProgramData*, Program>  programs;
    std::unordered_map<ID3D11BlendState*, Clone>    clones;
    // the bound pair (the immediate context's PS + blend state, as far as the stage is concerned)
    ID3D11PixelShader* curRetail = nullptr;   // what the engine bound (retail)
    ID3D11PixelShader* curXf     = nullptr;   // its transformed copy (null: none)
    bool               curCov    = false;     // curXf writes coverage
    bool               curNgVs   = false;     // curXf runs only beside a natural-growth vertex copy
    bool               psIsXf    = false;     // the stage bound curXf in curRetail's place
    ID3D11BlendState*  curBlend  = nullptr;   // the retail blend state RenderStateData last bound
    bool               haveBlend = false;     // curBlend is known (a render state was bound since the reset)
    bool               blendIsClone = false;  // the stage bound curBlend's clone in its place
    // evidence counters for the current generation
    int  blobsSeen = 0, blobsTransformed = 0, programsTransformed = 0, createFailed = 0;
    long xfBinds = 0, pixelBinds = 0;
    std::set<std::string> loggedHashes;   // a blob is logged once per run (untouched) / per gen (transformed)
};

State& S() {
    // Never destroyed: the process exit must not Release COM objects after the device is gone.
    static State* s = new State();
    return *s;
}

Stage g_stage;

const FLOAT kBlendFactor[4] = {0.0f, 0.0f, 0.0f, 0.0f};   // retail's (RenderStateData::Bind)
constexpr UINT kSampleMask = 0xFFFFFFFFu;

template <class T> void Rel(T*& p) {
    if (p) { p->Release(); p = nullptr; }
}

std::string HashName(const uint32_t sum[4]) {
    char b[24];
    std::snprintf(b, sizeof b, "%08x%08x", sum[0], sum[1]);
    return b;
}

void ReleaseAll(State& s) {
    for (auto& kv : s.programs)
        for (ID3D11PixelShader*& x : kv.second.xf) Rel(x);
    s.programs.clear();
    for (auto& kv : s.clones) Rel(kv.second.clone);
    s.clones.clear();
}

// The retail pair back on the context, everything released, the generation applied. Under s.mu.
void Reset(State& s, ID3D11DeviceContext* ctx, const char* why) {
    const bool sameDevice = s.device == Dx11Device();
    if (ctx != nullptr && sameDevice) {
        if (s.blendIsClone && s.haveBlend) ctx->OMSetBlendState(s.curBlend, kBlendFactor, kSampleMask);
        if (s.psIsXf && s.curRetail) ctx->PSSetShader(s.curRetail, nullptr, 0);
    }
    if (s.gen != 0 || s.programsTransformed)
        std::fprintf(stderr,
                     "[FoliageAA] stage reset (%s): previous gen %u mode %d -- %d blobs seen, %d transformed "
                     "(%d programs), %d create failures, %ld of %ld pixel binds transformed\n",
                     why, s.gen, s.mode, s.blobsSeen, s.blobsTransformed, s.programsTransformed, s.createFailed,
                     s.xfBinds, s.pixelBinds);
    ReleaseAll(s);
    s.curRetail = s.curXf = nullptr;
    s.curCov = s.curNgVs = false;
    s.psIsXf = s.blendIsClone = false;
    if (!sameDevice) {   // same device: the retail state just restored is still the bound one
        s.curBlend = nullptr;
        s.haveBlend = false;
    }
    s.blobsSeen = s.blobsTransformed = s.programsTransformed = s.createFailed = 0;
    s.xfBinds = s.pixelBinds = 0;
    s.gen    = s.reqGen;
    s.mode   = s.reqMode;
    s.device = Dx11Device();
    s.msaa   = ::Video::Configuration::_singleton->multisample > 0;
    s.opt    = FaaRewrite::OptionsFor(s.mode, s.reqK, s.msaa, s.reqFloor);
    s.dither = s.reqDither;
    s.ng     = s.mode > 0 && s.reqNg;
    if (s.mode > 0) {
        std::fprintf(stderr, "[FoliageAA] stage gen %u: mode %d (%s), MSAA %s -> %s, fill k %.3f floor %.3f, "
                             "edge dither noise %.2f scale %.0f\n", s.gen,
                     s.mode, s.mode == 2 ? "smooth edges + distance fill" : "smooth edges", s.msaa ? "on" : "off",
                     s.opt.coverage ? "alpha to coverage, discard dropped" : "discard kept",
                     static_cast<double>(s.opt.fillK), static_cast<double>(s.opt.fillFloor),
                     static_cast<double>(s.dither.noise), static_cast<double>(s.dither.noiseScale));
    } else {
        // Off: the faithful path from the next bind on. Under the mutex, so a Request(on) racing this
        // either ran before (reqMode > 0: we never get here) or runs after and sets the seam again.
        g_extFoliageAA.store(nullptr, std::memory_order_release);
        std::fprintf(stderr, "[FoliageAA] stage off: retail state restored, seam cleared\n");
    }
}

// Under s.mu: the generation, the device and the MSAA setting still those the state was built for.
// Returns false when the stage went off (the caller binds retail).
bool Current(State& s, ID3D11DeviceContext* ctx) {
    const bool msaa = ::Video::Configuration::_singleton->multisample > 0;
    if (s.gen != s.reqGen) Reset(s, ctx, "mode / k request");
    else if (s.device != Dx11Device()) Reset(s, ctx, "device changed");
    else if (s.msaa != msaa) Reset(s, ctx, "MSAA changed");
    return s.mode > 0;
}

// This program's copies, made now from its own byte code. Under s.mu. Two rewrites in sequence on
// one blob (BETTER-TREES): the cutout (FaaRewrite) where it matches, then the edge dither
// (BbsDither) on the result -- or on the retail blob when the cutout matched but had nothing to do (mode 1
// without MSAA) -- on the cutout programs only, where it matches and the noise is not 0. A copy is made
// when either ran.
Program& Build(State& s, PixelProgramData* p) {
    Program& pr = s.programs[p];
    for (ID3D11PixelShader*& x : pr.xf) Rel(x);
    const ::Video::ShaderProgram* owner = p->_owner;
    const RtByteCodeBlob* blobs = owner->_byteCode.GetData();
    bool any = false;
    for (int i = 0; i < kShaderVariantCount; ++i) {
        pr.retail[i] = p->variants[i];
        if (i >= owner->_byteCode.GetSize() || p->variants[i] == nullptr) continue;
        const uint8_t* blob = blobs[i].GetData();
        size_t         size = static_cast<size_t>(blobs[i].GetSize());
        pr.cov[i] = pr.ngVs[i] = false;
        ++s.blobsSeen;
        // NATURAL_GROWTH.md: natural growth first (it keeps the cutout's grammar), then the cutout and the
        // dither on its result.
        std::vector<uint8_t> grown;
        bool changed = false;
        if (s.ng) {
            NgRewrite::Match nm;
            NgRewrite::Analyse(blob, size, &nm);
            const std::string nh = nm.container ? HashName(nm.cont.checksum) : std::string("(no container)");
            if (nm.kind != NgRewrite::Kind::None) {
                if (NgRewrite::Transform(nm, &grown)) {
                    if (s.loggedHashes.insert("n" + nh).second)
                        std::fprintf(stderr, "[NaturalGrowth] rewrote %s v%d (%s): %zu B -> %zu B\n", nh.c_str(), i,
                                     NgRewrite::KindName(nm.kind), size, grown.size());
                    blob = grown.data();
                    size = grown.size();
                    changed = true;
                    pr.ngVs[i] = true;
                } else {
                    ++s.createFailed;
                    std::fprintf(stderr, "[NaturalGrowth] rewrite of %s v%d FAILED its self-check: kept retail\n",
                                 nh.c_str(), i);
                }
            } else if (nm.candidate && s.loggedHashes.insert("nu" + nh).second) {
                std::fprintf(stderr, "[NaturalGrowth] untouched %s v%d: %s\n", nh.c_str(), i, nm.why.c_str());
            }
        }
        FaaRewrite::Match m;
        FaaRewrite::Analyse(blob, size, &m);
        const std::string hash = m.container ? HashName(m.cont.checksum) : std::string("(no container)");
        std::vector<uint8_t> cut;      // the cutout's output (when it applied)
        const uint8_t* cur = blob;     // what the next step / the create reads
        size_t curSize = size;
        if (!m.colour) {
            // Untouched by the cutout. Logged once per blob per run when it looked like a cutout (it
            // discards) or is not something we can read; the plain opaque programs are not worth a line.
            if ((m.discards > 0 || !m.program) && s.loggedHashes.insert("u" + hash).second)
                std::fprintf(stderr, "[FoliageAA] untouched %s v%d (%zu B): %s\n", hash.c_str(), i, size,
                             m.why.c_str());
        } else if (FaaRewrite::Transform(m, s.opt, &cut)) {
            cur = cut.data();
            curSize = cut.size();
            changed = true;
            pr.cov[i] = s.opt.coverage;
            char key[48];
            std::snprintf(key, sizeof key, "t%u:", s.gen);
            if (s.loggedHashes.insert(key + hash).second)
                std::fprintf(stderr, "[FoliageAA] transformed %s v%d gen %u%s%s: %zu B -> %zu B (temps %u, cut %g, "
                                     "season mad %s, alpha r%u.%c)\n",
                             hash.c_str(), i, s.gen, s.opt.coverage ? " coverage" : "",
                             s.opt.fillK > 0.0f ? " fill" : "", size, cut.size(), m.temps,
                             static_cast<double>(m.cut), m.iMad >= 0 ? "yes" : "no", m.alpha.index[0],
                             "xyzw"[m.alphaComp & 3]);
        } else if (s.opt.coverage || s.opt.fillK > 0.0f) {   // else: nothing to do in this mode
            ++s.createFailed;
            std::fprintf(stderr, "[FoliageAA] transform of %s v%d FAILED its self-check: kept %s\n",
                         hash.c_str(), i, pr.ngVs[i] ? "natural growth alone" : "retail");
            if (!pr.ngVs[i]) continue;
        }
        std::vector<uint8_t> dithered;
        // Only the cutout programs (m.colour): every lit pixel shader compares the shadow from an input
        // and would match, but only the billboard vertex shader writes the extent, so anything else would
        // pay the 11 instructions for an amplitude of 0 (the first look arm dithered the terrain too).
        if (s.dither.noise > 0.0f && m.colour) {
            BbsDither::Match dm;
            BbsDither::Analyse(cur, curSize, &dm);
            if (dm.matched) {
                if (BbsDither::Transform(dm, &dithered, s.dither)) {
                    const size_t before = curSize;
                    cur = dithered.data();
                    curSize = dithered.size();
                    changed = true;
                    char key[64];
                    std::snprintf(key, sizeof key, "d%u:", s.gen);
                    if (s.loggedHashes.insert(key + hash).second)
                        std::fprintf(stderr, "[FoliageAA] dithered %s v%d gen %u: %zu B -> %zu B (shadow v%u.%c, uv v%u, "
                                             "noise %.2f scale %.0f%s)\n",
                                     hash.c_str(), i, s.gen, before, curSize, dm.shadowIn, "xyzw"[dm.shadowComp & 3],
                                     dm.uvIn, static_cast<double>(s.dither.noise),
                                     static_cast<double>(s.dither.noiseScale), cut.empty() ? "" : ", after the cutout");
                } else {
                    ++s.createFailed;
                    std::fprintf(stderr, "[FoliageAA] dither of %s v%d FAILED its self-check: kept the cutout / retail\n",
                                 hash.c_str(), i);
                }
            } else if (dm.candidate && s.loggedHashes.insert("ud" + hash).second) {
                std::fprintf(stderr, "[FoliageAA] not dithered %s v%d: %s\n", hash.c_str(), i, dm.why.c_str());
            }
        }
        if (!changed) continue;
        ID3D11PixelShader* ps = nullptr;
        const HRESULT hr = s.device->CreatePixelShader(cur, curSize, nullptr, &ps);
        if (hr != S_OK || ps == nullptr) {
            ++s.createFailed;
            std::fprintf(stderr, "[FoliageAA] CreatePixelShader of the copy of %s v%d failed hr=0x%08lx: kept retail\n",
                         hash.c_str(), i, static_cast<unsigned long>(hr));
            pr.cov[i] = pr.ngVs[i] = false;
            Rel(ps);
            continue;
        }
        pr.xf[i] = ps;
        any = true;
        ++s.blobsTransformed;
    }
    if (any) ++s.programsTransformed;
    return pr;
}

// The clone for `retail` (made on first use). Null: no clone (the state blends, or a failure).
ID3D11BlendState* CloneOf(State& s, ID3D11BlendState* retail) {
    auto it = s.clones.find(retail);
    if (it != s.clones.end()) return it->second.clone;
    Clone& c = s.clones[retail];
    D3D11_BLEND_DESC d;
    if (retail) {
        retail->GetDesc(&d);
    } else {   // a null state binds D3D11's default: blending off, full mask
        std::memset(&d, 0, sizeof d);
        d.RenderTarget[0].SrcBlend = d.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
        d.RenderTarget[0].DestBlend = d.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
        d.RenderTarget[0].BlendOp = d.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        d.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    }
    if (d.RenderTarget[0].BlendEnable || d.IndependentBlendEnable) {
        std::fprintf(stderr, "[FoliageAA] blend state %p blends: no clone, its cutout draws stay retail\n",
                     static_cast<void*>(retail));
        return nullptr;
    }
    // FOLIAGE_AA §6.3: the coverage comes from o0.w (taken before blending); the blend then writes
    // colour * 1 and alpha * 0 + dst * 0 = 0 -- the retail program's own `mov o0.w, l(0)`.
    d.AlphaToCoverageEnable = TRUE;
    D3D11_RENDER_TARGET_BLEND_DESC& b = d.RenderTarget[0];
    b.BlendEnable    = TRUE;
    b.SrcBlend       = D3D11_BLEND_ONE;
    b.DestBlend      = D3D11_BLEND_ZERO;
    b.BlendOp        = D3D11_BLEND_OP_ADD;
    b.SrcBlendAlpha  = D3D11_BLEND_ZERO;
    b.DestBlendAlpha = D3D11_BLEND_ZERO;
    b.BlendOpAlpha   = D3D11_BLEND_OP_ADD;
    b.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    const HRESULT hr = s.device->CreateBlendState(&d, &c.clone);
    std::fprintf(stderr, "[FoliageAA] A2C clone of blend state %p: hr=0x%08lx\n", static_cast<void*>(retail),
                 static_cast<unsigned long>(hr));
    if (hr != S_OK) Rel(c.clone);
    return c.clone;
}

// Make the context hold what the bound pair wants. `psBind`: the engine is binding a program now,
// so the PS is set whatever the stage held before (once: the retail one or its copy).
void Apply(State& s, ID3D11DeviceContext* ctx, bool psBind) {
    ID3D11BlendState* clone = nullptr;
    bool wantXf = s.curXf != nullptr;
    // NATURAL_GROWTH.md: a copy that reads the natural-growth interpolant never runs beside a vertex
    // shader that does not write it (retail's program there).
    if (wantXf && s.curNgVs && !g_ngVertexBound.load(std::memory_order_relaxed)) wantXf = false;
    if (wantXf && s.curCov) {
        clone  = s.haveBlend ? CloneOf(s, s.curBlend) : nullptr;
        wantXf = clone != nullptr;   // no A2C state under it: the transformed program must not run
    }
    const bool wantClone = wantXf && s.curCov;
    if (psBind || wantXf != s.psIsXf) {
        ctx->PSSetShader(wantXf ? s.curXf : s.curRetail, nullptr, 0);
        s.psIsXf = wantXf;
        if (wantXf && ++s.xfBinds == 1)
            std::fprintf(stderr, "[FoliageAA] first transformed bind of gen %u%s\n", s.gen,
                         wantClone ? " (A2C clone bound)" : "");
    }
    if (wantClone != s.blendIsClone && s.haveBlend) {
        ctx->OMSetBlendState(wantClone ? clone : s.curBlend, kBlendFactor, kSampleMask);
        s.blendIsClone = wantClone;
    }
}

}  // namespace

void Stage::BindPixel(ID3D11DeviceContext* ctx, PixelProgramData* p, int variant) {
    State& s = S();
    std::lock_guard<std::mutex> lock(s.mu);
    ID3D11PixelShader* const retail = p->variants[variant];
    if (!Current(s, ctx)) {   // went off just now: retail, as the faithful body
        ctx->PSSetShader(retail, nullptr, 0);
        return;
    }
    ++s.pixelBinds;
    auto it = s.programs.find(p);
    Program* pr = it != s.programs.end() ? &it->second : nullptr;
    if (pr == nullptr || pr->retail[variant] != retail) pr = &Build(s, p);   // first bind, or re-created
    s.curRetail = retail;
    s.curXf     = pr->xf[variant];
    s.curCov    = pr->cov[variant];
    s.curNgVs   = pr->ngVs[variant];
    Apply(s, ctx, true);
}

void Stage::VertexPairChanged(ID3D11DeviceContext* ctx) {
    State& s = S();
    std::lock_guard<std::mutex> lock(s.mu);
    if (s.gen != s.reqGen || s.mode == 0 || s.device != Dx11Device() || s.curRetail == nullptr) return;
    Apply(s, ctx, false);
}

void Stage::AfterRenderState(ID3D11DeviceContext* ctx, ID3D11BlendState* retailBlend) {
    State& s = S();
    std::lock_guard<std::mutex> lock(s.mu);
    // Recorded whatever the generation: retail just bound this state, and a pending reset must
    // restore over THIS one, not the one before.
    s.curBlend     = retailBlend;
    s.haveBlend    = true;
    s.blendIsClone = false;
    if (s.gen != s.reqGen || s.mode == 0 || s.device != Dx11Device()) return;   // the next pixel bind resets
    Apply(s, ctx, false);
}

void Stage::PixelCreated(PixelProgramData* p) {
    State& s = S();
    std::lock_guard<std::mutex> lock(s.mu);
    // A pending request with nothing of ours bound (the boot request, before the first draw) is
    // applied here, so the copies are made at load instead of at the first bind. Anything bound
    // needs the binding thread's context to restore: that reset waits for the next bind.
    if (s.reqMode > 0 && (s.gen != s.reqGen || s.device != Dx11Device()) && !s.psIsXf && !s.blendIsClone)
        Reset(s, nullptr, "program load");
    // Only when the applied state is the requested one on this device; otherwise the next bind
    // resets and builds lazily.
    if (s.mode == 0 || s.gen != s.reqGen || s.device != Dx11Device()) {
        auto it = s.programs.find(p);
        if (it != s.programs.end()) {
            for (ID3D11PixelShader*& x : it->second.xf) Rel(x);
            s.programs.erase(it);
        }
        return;
    }
    Build(s, p);
}

void Stage::PixelDestroyed(PixelProgramData* p) {
    State& s = S();
    std::lock_guard<std::mutex> lock(s.mu);
    auto it = s.programs.find(p);
    if (it == s.programs.end()) return;
    for (int i = 0; i < kShaderVariantCount; ++i) {
        if (it->second.retail[i] != nullptr && it->second.retail[i] == s.curRetail) {
            // bound now: forget the pair (the engine binds another program before the next draw)
            s.curRetail = s.curXf = nullptr;
            s.curCov = s.curNgVs = false;
            s.psIsXf = false;
        }
        Rel(it->second.xf[i]);
    }
    s.programs.erase(it);
}

void Stage::RenderStateDestroyed(ID3D11BlendState* retailBlend) {
    State& s = S();
    std::lock_guard<std::mutex> lock(s.mu);
    auto it = s.clones.find(retailBlend);
    if (it != s.clones.end()) {
        Rel(it->second.clone);
        s.clones.erase(it);
    }
    if (s.haveBlend && s.curBlend == retailBlend) {
        s.curBlend = nullptr;
        s.haveBlend = false;
        s.blendIsClone = false;
    }
}

}  // namespace FaaStage
}  // namespace VideoDX11

namespace FaaBackend {

void Request(int mode, float fillK, float fillFloor, const BbsRewrite::Params* dither, bool naturalGrowth) {
    using namespace VideoDX11;
    FaaStage::State& s = FaaStage::S();
    std::lock_guard<std::mutex> lock(s.mu);
    if (mode < 0 || mode > 2) mode = 0;
    if (mode == 0) fillK = 0.0f;
    if (fillK <= 0.0f) fillFloor = 0.0f;
    // BETTER-TREES: only the dither's two fields matter here (the others are the vertex rewrite's).
    BbsRewrite::Params d = BbsRewrite::kRetailParams;
    if (dither != nullptr && mode > 0) {
        d.noise      = dither->noise > 0.0f ? dither->noise : 0.0f;
        d.noiseScale = dither->noiseScale > 0.0f ? dither->noiseScale : BbsRewrite::kRetailParams.noiseScale;
    }
    naturalGrowth = mode > 0 && naturalGrowth;
    if (mode == s.reqMode && fillK == s.reqK && fillFloor == s.reqFloor && d == s.reqDither &&
        naturalGrowth == s.reqNg)
        return;
    const bool seamWasSet = g_extFoliageAA.load(std::memory_order_acquire) != nullptr;
    s.reqMode = mode;
    s.reqK    = fillK;
    s.reqFloor = fillFloor;
    s.reqDither = d;
    s.reqNg   = naturalGrowth;
    ++s.reqGen;
    if (mode > 0) {
        g_extFoliageAA.store(&FaaStage::g_stage, std::memory_order_release);
    } else if (!seamWasSet) {
        // never applied anything (or already off): nothing bound to restore
        s.gen = s.reqGen;
        s.mode = 0;
    }
    std::fprintf(stderr, "[FoliageAA] request mode %d k %.3f floor %.3f dither %.2f/%.0f natural growth %d (gen %u)%s\n",
                 mode, static_cast<double>(fillK), static_cast<double>(fillFloor), static_cast<double>(d.noise),
                 static_cast<double>(d.noiseScale), naturalGrowth ? 1 : 0, s.reqGen, mode > 0 ? ": seam set" : (seamWasSet ? ": off at the next pixel bind" : ""));
}

#if BANISHED_TWEAK_OVERLAY
TweakStats Stats() {
    using namespace VideoDX11;
    FaaStage::State& s = FaaStage::S();
    std::lock_guard<std::mutex> lock(s.mu);
    TweakStats t;
    t.reqMode = s.reqMode;
    t.mode = s.mode;
    t.reqGen = s.reqGen;
    t.gen = s.gen;
    t.msaa = s.msaa;
    t.coverage = s.opt.coverage;
    t.fillK = s.opt.fillK;
    t.fillFloor = s.opt.fillFloor;
    t.blobsSeen = s.blobsSeen;
    t.blobsTransformed = s.blobsTransformed;
    t.programsTransformed = s.programsTransformed;
    t.createFailed = s.createFailed;
    t.xfBinds = s.xfBinds;
    t.pixelBinds = s.pixelBinds;
    return t;
}
#endif

}  // namespace FaaBackend
