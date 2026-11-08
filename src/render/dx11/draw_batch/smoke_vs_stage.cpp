// ============================================================================
// src/render/dx11/draw_batch/smoke_vs_stage.cpp -- smoke_vs_stage.h, smoke_vs_backend.h
// ============================================================================
// NOT RETAIL (ext.draw_batch). Log lines: `[DrawBatch] ...` on stderr (standalone_error.log).
// The shape is billboard_shadow_stage.cpp's (one mutex, copies per program, lazy re-make).
// ============================================================================
#include "draw_batch/smoke_vs_stage.h"

#include "draw_batch/smoke_vs_backend.h"
#include "draw_batch/smoke_vs_rewrite.h"

#include "dx11_backend_access.h"
#include "video/ShaderProgram.h"   // Video::VertexProgram::_data

#include <cstdio>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace VideoDX11 {

std::atomic<DbStage::Stage*> g_extDrawBatchVs{nullptr};

namespace DbStage {
namespace {

struct Program {
    VertexProgramData*  data = nullptr;                      // the backend object (the map's key, as its base)
    ID3D11VertexShader* xf[kShaderVariantCount]     = {};
    ID3D11VertexShader* retail[kShaderVariantCount] = {};   // what the copies were made beside
};

struct State {
    std::mutex    mu;
    bool          reqOn  = false;   // the extension's request (main thread)
    ID3D11Device* device = nullptr; // the device the copies belong to
    // Keyed by the engine-side base pointer (Video::VertexProgram::_data) so Rewritten finds it.
    std::unordered_map<const ::Video::VertexProgramData*, Program> programs;
    int  blobsSeen = 0, blobsTransformed = 0, createFailed = 0;
    long xfBinds = 0;
    bool loggedFirstBind = false;
};

State& S() {
    // Never destroyed: the process exit must not Release COM objects after the device is gone.
    static State* s = new State();
    return *s;
}

Stage g_stage;

template <class T> void Rel(T*& p) {
    if (p) {
        p->Release();
        p = nullptr;
    }
}

void ReleaseAll(State& s, const char* why) {
    if (s.blobsTransformed || s.xfBinds)
        std::fprintf(stderr, "[DrawBatch] smoke VS stage reset (%s): %d blobs seen, %d rewritten, %d create failures, "
                             "%ld rewritten binds\n",
                     why, s.blobsSeen, s.blobsTransformed, s.createFailed, s.xfBinds);
    for (auto& kv : s.programs)
        for (ID3D11VertexShader*& x : kv.second.xf) Rel(x);
    s.programs.clear();
    s.blobsSeen = s.blobsTransformed = s.createFailed = 0;
    s.xfBinds = 0;
}

// This program's copies, made now from its own byte code. Under s.mu.
Program& Build(State& s, VertexProgramData* p) {
    if (s.device != Dx11Device()) {
        if (s.device != nullptr) ReleaseAll(s, "device changed");
        s.device = Dx11Device();
    }
    Program& pr = s.programs[p];
    pr.data = p;
    for (ID3D11VertexShader*& x : pr.xf) Rel(x);
    const ::Video::ShaderProgram* owner = p->_owner;
    const RtByteCodeBlob* blobs = owner->_byteCode.GetData();
    for (int i = 0; i < kShaderVariantCount; ++i) {
        pr.retail[i] = p->variants[i];
        if (i >= owner->_byteCode.GetSize() || p->variants[i] == nullptr) continue;
        const uint8_t* blob = blobs[i].GetData();
        const size_t   size = static_cast<size_t>(blobs[i].GetSize());
        SmokeVs::Match m;
        SmokeVs::Analyse(blob, size, &m);
        ++s.blobsSeen;
        if (!m.matched) continue;   // not a particle program: untouched, silently
        std::vector<uint8_t> out;
        if (!SmokeVs::Transform(m, &out)) {
            ++s.createFailed;
            std::fprintf(stderr, "[DrawBatch] smoke VS rewrite v%d FAILED its self-check: kept retail\n", i);
            continue;
        }
        ID3D11VertexShader* vs = nullptr;
        const HRESULT hr = s.device->CreateVertexShader(out.data(), out.size(), nullptr, &vs);
        if (hr != S_OK || vs == nullptr) {
            ++s.createFailed;
            std::fprintf(stderr, "[DrawBatch] CreateVertexShader of the smoke VS copy v%d failed hr=0x%08lx: kept retail\n",
                         i, static_cast<unsigned long>(hr));
            Rel(vs);
            continue;
        }
        pr.xf[i] = vs;
        ++s.blobsTransformed;
        std::fprintf(stderr, "[DrawBatch] rewrote a particle VS v%d: %zu B -> %zu B (instance id v%u + emitter slot v0.w)\n",
                     i, size, out.size(), m.iidReg);
    }
    return pr;
}

// Every compiled variant has its copy, made beside the variant now in the program.
bool Complete(const Program& pr, const VertexProgramData* p) {
    bool any = false;
    for (int i = 0; i < kShaderVariantCount; ++i) {
        if (p->variants[i] == nullptr) continue;
        if (pr.xf[i] == nullptr || pr.retail[i] != p->variants[i]) return false;
        any = true;
    }
    return any;
}

}  // namespace

bool Stage::BindVertex(ID3D11DeviceContext* ctx, VertexProgramData* p, int variant) {
    State& s = S();
    std::lock_guard<std::mutex> lock(s.mu);
    if (!s.reqOn) {
        // Off: everything released, the seam cleared, retail from this bind on.
        ReleaseAll(s, "off");
        g_extDrawBatchVs.store(nullptr, std::memory_order_release);
        std::fprintf(stderr, "[DrawBatch] smoke VS stage off: copies released, seam cleared\n");
        return false;
    }
    if (s.device != Dx11Device()) {
        ReleaseAll(s, "device changed");
        s.device = Dx11Device();
    }
    ID3D11VertexShader* const retail = p->variants[variant];
    auto it = s.programs.find(p);
    Program* pr = it != s.programs.end() ? &it->second : nullptr;
    if (pr == nullptr || pr->retail[variant] != retail) pr = &Build(s, p);   // first bind, or re-created
    ID3D11VertexShader* const xf = pr->xf[variant];
    if (xf == nullptr) return false;
    ++s.xfBinds;
    if (!s.loggedFirstBind) {
        s.loggedFirstBind = true;
        std::fprintf(stderr, "[DrawBatch] first rewritten particle VS bind\n");
    }
    ctx->VSSetShader(xf, nullptr, 0);
    return true;
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

}  // namespace DbStage
}  // namespace VideoDX11

namespace SmokeVsBackend {
BANISHED_RENDER_IMPL_BEGIN

void Request(bool on) {
    using namespace VideoDX11;
    DbStage::State& s = DbStage::S();
    std::lock_guard<std::mutex> lock(s.mu);
    if (on == s.reqOn) return;
    s.reqOn = on;
    if (on) g_extDrawBatchVs.store(&DbStage::g_stage, std::memory_order_release);
    std::fprintf(stderr, "[DrawBatch] smoke VS request %s%s\n", on ? "on: seam set" : "off",
                 on ? "" : (g_extDrawBatchVs.load(std::memory_order_acquire) ? ": cleared at the next vertex bind" : ""));
}

bool Rewritten(const Video::VertexProgram* vp) {
    using namespace VideoDX11;
    if (vp == nullptr || vp->_data == nullptr) return false;
    DbStage::State& s = DbStage::S();
    std::lock_guard<std::mutex> lock(s.mu);
    if (!s.reqOn || s.device != Dx11Device()) return false;
    auto it = s.programs.find(vp->_data);
    if (it == s.programs.end()) return false;
    return DbStage::Complete(it->second, it->second.data);
}

BANISHED_RENDER_IMPL_END
}  // namespace SmokeVsBackend
