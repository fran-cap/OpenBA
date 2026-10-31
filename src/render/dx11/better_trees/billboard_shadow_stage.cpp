// ============================================================================
// src/render/dx11/better_trees/billboard_shadow_stage.cpp -- billboard_shadow_stage.h,
// billboard_shadow_backend.h
// ============================================================================
// NOT RETAIL (ext.billboard_shadow). Log lines: `[BillboardShadow] ...` on stderr
// (standalone_error.log).
// ============================================================================
#include "better_trees/billboard_shadow_stage.h"

#include "better_trees/billboard_shadow_backend.h"
#include "better_trees/foliage_aa_stage.h"     // NATURAL_GROWTH.md: the pixel stage's pairing re-apply
#include "better_trees/natural_growth.h"
#include "better_trees/receiver_rewrite.h"

#include "dx11_backend_access.h"

#include <cstdio>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace VideoDX11 {

std::atomic<BbsStage::Stage*> g_extBillboardShadow{nullptr};
std::atomic<bool> g_ngVertexBound{false};

namespace BbsStage {
namespace {

struct Program {
    ID3D11VertexShader* xf[kShaderVariantCount]     = {};
    ID3D11VertexShader* retail[kShaderVariantCount] = {};   // what the copies were made beside
    bool                ng[kShaderVariantCount]     = {};   // the copy carries natural growth
};

struct State {
    std::mutex    mu;
    bool          reqOn  = false;   // the extension's request (main thread)
    bool          reqNg  = false;   // NATURAL_GROWTH.md: natural tree growth requested ...
    bool          ng     = false;   // ... and what the current copies were made with
#if BANISHED_TWEAK_OVERLAY
    BbsRewrite::Params reqParams = BbsRewrite::kDefaultParams;   // TS-TWEAK: the overlay's request
    BbsRewrite::Params params    = BbsRewrite::kDefaultParams;   // what the current copies were made with
#endif
    ID3D11Device* device = nullptr; // the device the copies belong to
    std::unordered_map<VertexProgramData*, Program> programs;
    // evidence counters (since the last reset)
    int  blobsSeen = 0, blobsTransformed = 0, ngTransformed = 0, createFailed = 0;
    long xfBinds = 0, vertexBinds = 0;
    std::set<std::string> logged;   // a blob is logged once per run
};

State& S() {
    // Never destroyed: the process exit must not Release COM objects after the device is gone.
    static State* s = new State();
    return *s;
}

Stage g_stage;

template <class T> void Rel(T*& p) {
    if (p) { p->Release(); p = nullptr; }
}

std::string HashName(const uint32_t sum[4]) {
    char b[24];
    std::snprintf(b, sizeof b, "%08x%08x", sum[0], sum[1]);
    return b;
}

void ReleaseAll(State& s, const char* why) {
    if (s.blobsTransformed || s.xfBinds)
        std::fprintf(stderr,
                     "[BillboardShadow] stage reset (%s): %d blobs seen, %d rewritten, %d create failures, "
                     "%ld of %ld vertex binds rewritten (%d with natural growth)\n",
                     why, s.blobsSeen, s.blobsTransformed, s.createFailed, s.xfBinds, s.vertexBinds, s.ngTransformed);
    for (auto& kv : s.programs)
        for (ID3D11VertexShader*& x : kv.second.xf) Rel(x);
    s.programs.clear();
    s.blobsSeen = s.blobsTransformed = s.ngTransformed = s.createFailed = 0;
    s.xfBinds = s.vertexBinds = 0;
}

// This program's copies, made now from its own byte code. Under s.mu.
Program& Build(State& s, VertexProgramData* p) {
    if (s.device != Dx11Device()) {
        if (s.device != nullptr) ReleaseAll(s, "device changed");
        s.device = Dx11Device();
    }
    Program& pr = s.programs[p];
    for (ID3D11VertexShader*& x : pr.xf) Rel(x);
    const ::Video::ShaderProgram* owner = p->_owner;
    const RtByteCodeBlob* blobs = owner->_byteCode.GetData();
    for (int i = 0; i < kShaderVariantCount; ++i) {
        pr.retail[i] = p->variants[i];
        pr.ng[i] = false;
        if (i >= owner->_byteCode.GetSize() || p->variants[i] == nullptr) continue;
        const uint8_t* blob = blobs[i].GetData();
        size_t         size = static_cast<size_t>(blobs[i].GetSize());
        ++s.blobsSeen;
        // NATURAL_GROWTH.md: natural growth first (the billboard colour / depth and foliage programs), then
        // the receiver lookup on its result.
        std::vector<uint8_t> grown;
        if (s.ng) {
            NgRewrite::Match nm;
            NgRewrite::Analyse(blob, size, &nm);
            const std::string nh = nm.container ? HashName(nm.cont.checksum) : std::string("(no container)");
            if (nm.kind != NgRewrite::Kind::None) {
                if (NgRewrite::Transform(nm, &grown)) {
                    if (s.logged.insert("n" + nh).second)
                        std::fprintf(stderr, "[NaturalGrowth] rewrote %s v%d (%s): %zu B -> %zu B\n", nh.c_str(), i,
                                     NgRewrite::KindName(nm.kind), size, grown.size());
                    blob = grown.data();
                    size = grown.size();
                    pr.ng[i] = true;
                } else {
                    ++s.createFailed;
                    std::fprintf(stderr, "[NaturalGrowth] rewrite of %s v%d FAILED its self-check: kept retail\n",
                                 nh.c_str(), i);
                }
            } else if (nm.candidate && s.logged.insert("nu" + nh).second) {
                std::fprintf(stderr, "[NaturalGrowth] untouched %s v%d: %s\n", nh.c_str(), i, nm.why.c_str());
            }
        }
        BbsRewrite::Match m;
        BbsRewrite::Analyse(blob, size, &m);
        const std::string hash = m.container ? HashName(m.cont.checksum) : std::string("(no container)");
        if (!m.matched && pr.ng[i]) {
            // natural growth alone (the depth and foliage programs, and DETAIL0's colour one)
            ID3D11VertexShader* vs = nullptr;
            const HRESULT hr = s.device->CreateVertexShader(blob, size, nullptr, &vs);
            if (hr != S_OK || vs == nullptr) {
                ++s.createFailed;
                pr.ng[i] = false;
                std::fprintf(stderr, "[NaturalGrowth] CreateVertexShader of %s v%d failed hr=0x%08lx: kept retail\n",
                             hash.c_str(), i, static_cast<unsigned long>(hr));
                Rel(vs);
                continue;
            }
            pr.xf[i] = vs;
            ++s.blobsTransformed;
            ++s.ngTransformed;
            continue;
        }
        if (!m.matched) {
            // Untouched. Logged once per blob per run when it looked like ours (it reads both windows)
            // or could not be read at all; every other vertex shader is not worth a line.
            if ((m.candidate || !m.program) && s.logged.insert("u" + hash).second)
                std::fprintf(stderr, "[BillboardShadow] untouched %s v%d (%zu B): %s\n", hash.c_str(), i, size,
                             m.why.c_str());
            continue;
        }
        std::vector<uint8_t> out;
#if BANISHED_TWEAK_OVERLAY
        const bool built = BbsRewrite::TransformWithParams(m, &out, s.params);
#else
        const bool built = BbsRewrite::Transform(m, &out);
#endif
        if (!built) {
            ++s.createFailed;
            pr.ng[i] = false;
            std::fprintf(stderr, "[BillboardShadow] rewrite of %s v%d FAILED its self-check: kept retail\n",
                         hash.c_str(), i);
            continue;
        }
        ID3D11VertexShader* vs = nullptr;
        const HRESULT hr = s.device->CreateVertexShader(out.data(), out.size(), nullptr, &vs);
        if (hr != S_OK || vs == nullptr) {
            ++s.createFailed;
            std::fprintf(stderr, "[BillboardShadow] CreateVertexShader of the copy of %s v%d failed hr=0x%08lx: "
                                 "kept retail\n", hash.c_str(), i, static_cast<unsigned long>(hr));
            pr.ng[i] = false;
            Rel(vs);
            continue;
        }
        pr.xf[i] = vs;
        ++s.blobsTransformed;
        if (pr.ng[i]) ++s.ngTransformed;
#if BANISHED_TWEAK_OVERLAY
        const BbsRewrite::Params k = s.params;
        char kTag[48];
        std::snprintf(kTag, sizeof kTag, "@%.2f/%.2f/%.2f", static_cast<double>(k.basis), static_cast<double>(k.margin),
                      static_cast<double>(k.jitter));
        if (s.logged.insert("t" + hash + kTag).second)   // once per blob per parameter set
#else
        const BbsRewrite::Params k = BbsRewrite::kDefaultParams;
        if (s.logged.insert("t" + hash).second)
#endif
            std::fprintf(stderr, "[BillboardShadow] rewrote %s v%d: %zu B -> %zu B (temps %u -> %u, shadow o%u, "
                                 "centre saved at #%d, lookup = P r%u moved along the light by basis %.2f d.L - "
                                 "(%.2f + %.2f jitter) |C - P| at #%d, extent -r in o%u.w after #%d)\n",
                         hash.c_str(), i, size, out.size(), m.temps, m.temps + 3, m.shadowOut, m.centerIdx,
                         m.posReg, static_cast<double>(k.basis), static_cast<double>(k.margin),
                         static_cast<double>(k.jitter), m.insertAt, m.shadowOut, m.extentIdx);
    }
    return pr;
}

// NATURAL_GROWTH.md: publish whether the bound vertex shader writes the per-tree interpolant; on a
// change the pixel stage re-applies its bound pair (a copy that reads the interpolant runs only beside one).
void SetNgBound(ID3D11DeviceContext* ctx, bool ng) {
    if (g_ngVertexBound.load(std::memory_order_relaxed) == ng) return;
    g_ngVertexBound.store(ng, std::memory_order_relaxed);
    if (FaaStage::Stage* faa = g_extFoliageAA.load(std::memory_order_acquire)) faa->VertexPairChanged(ctx);
}

}  // namespace

void Stage::BindVertex(ID3D11DeviceContext* ctx, VertexProgramData* p, int variant) {
    State& s = S();
    std::lock_guard<std::mutex> lock(s.mu);
    ID3D11VertexShader* const retail = p->variants[variant];
    if (!s.reqOn) {
        // Off: everything released, the seam cleared, retail from this bind on.
        ReleaseAll(s, "off");
        g_extBillboardShadow.store(nullptr, std::memory_order_release);
        std::fprintf(stderr, "[BillboardShadow] stage off: copies released, seam cleared\n");
        ctx->VSSetShader(retail, nullptr, 0);
        SetNgBound(ctx, false);
        return;
    }
    if (s.ng != s.reqNg) {   // NATURAL_GROWTH.md: the copies are re-made with (or without) it below / lazily
        ReleaseAll(s, s.reqNg ? "natural growth on" : "natural growth off");
        s.ng = s.reqNg;
    }
    ++s.vertexBinds;
    if (s.device != Dx11Device()) {
        ReleaseAll(s, "device changed");
        s.device = Dx11Device();
    }
#if BANISHED_TWEAK_OVERLAY
    if (s.params != s.reqParams) {   // TS-TWEAK: a new set; the copies are re-made with it below / lazily
        ReleaseAll(s, "params changed");
        std::fprintf(stderr, "[BillboardShadow] params basis/margin/jitter %.2f/%.2f/%.2f -> %.2f/%.2f/%.2f: copies re-created\n",
                     static_cast<double>(s.params.basis), static_cast<double>(s.params.margin), static_cast<double>(s.params.jitter),
                     static_cast<double>(s.reqParams.basis), static_cast<double>(s.reqParams.margin), static_cast<double>(s.reqParams.jitter));
        s.params = s.reqParams;
    }
#endif
    auto it = s.programs.find(p);
    Program* pr = it != s.programs.end() ? &it->second : nullptr;
    if (pr == nullptr || pr->retail[variant] != retail) pr = &Build(s, p);   // first bind, or re-created
    ID3D11VertexShader* const xf = pr->xf[variant];
    const bool use = xf != nullptr;
    if (use && ++s.xfBinds == 1) std::fprintf(stderr, "[BillboardShadow] first rewritten bind\n");
    ctx->VSSetShader(use ? xf : retail, nullptr, 0);
    SetNgBound(ctx, use && pr->ng[variant]);
}

void Stage::VertexCreated(VertexProgramData* p) {
    State& s = S();
    std::lock_guard<std::mutex> lock(s.mu);
    if (!s.reqOn) return;   // the next bind clears the seam
    Build(s, p);
}

void Stage::VertexDestroyed(VertexProgramData* p) {
    State& s = S();
    std::lock_guard<std::mutex> lock(s.mu);
    auto it = s.programs.find(p);
    if (it == s.programs.end()) return;
    for (ID3D11VertexShader*& x : it->second.xf) Rel(x);
    s.programs.erase(it);
}

}  // namespace BbsStage
}  // namespace VideoDX11

namespace BbsBackend {

void Request(bool on, bool naturalGrowth) {
    using namespace VideoDX11;
    VideoDX11::BbsStage::State& s = VideoDX11::BbsStage::S();
    std::lock_guard<std::mutex> lock(s.mu);
    naturalGrowth = on && naturalGrowth;
    if (naturalGrowth != s.reqNg) {
        s.reqNg = naturalGrowth;
        std::fprintf(stderr, "[NaturalGrowth] vertex request %s (applied at the next vertex bind)\n",
                     naturalGrowth ? "on" : "off");
    }
    if (on == s.reqOn) return;
    s.reqOn = on;
    if (on) g_extBillboardShadow.store(&BbsStage::g_stage, std::memory_order_release);
    std::fprintf(stderr, "[BillboardShadow] request %s%s\n", on ? "on: seam set" : "off",
                 on ? "" : (g_extBillboardShadow.load(std::memory_order_acquire) ? ": cleared at the next vertex bind" : ""));
}

#if BANISHED_TWEAK_OVERLAY
void RequestParams(const BbsRewrite::Params& p) {
    VideoDX11::BbsStage::State& s = VideoDX11::BbsStage::S();
    std::lock_guard<std::mutex> lock(s.mu);
    if (p == s.reqParams) return;
    s.reqParams = p;
    std::fprintf(stderr, "[BillboardShadow] request params basis/margin/jitter %.2f/%.2f/%.2f (applied at the next vertex bind)\n",
                 static_cast<double>(p.basis), static_cast<double>(p.margin), static_cast<double>(p.jitter));
}

BbsRewrite::Params AppliedParams() {
    VideoDX11::BbsStage::State& s = VideoDX11::BbsStage::S();
    std::lock_guard<std::mutex> lock(s.mu);
    return s.params;
}
#endif

}  // namespace BbsBackend
