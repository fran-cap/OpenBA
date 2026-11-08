// ============================================================================
// tests/unit/ext_draw_batch_test.cpp -- DRAWBATCH: ext.draw_batch (class A)
// ============================================================================
// src/ext/draw_batch/batch_fill.{h,cpp} on plain buffers, and the registry row:
//   * the fill is the concatenation of the spans' vertex bytes, and each span's indices
//     plus the vertices before it -- so every index names the vertex it named alone;
//   * retail's capacity test, the sic index-vs-vertex-capacity compare included;
//   * a run is cut where it would pass 65536 vertices or fail the capacity test;
//   * the registry row: class A, on by default, off under the faithful profile.
// The DrawItemList copy and the smoke VS rewrite are the exe's (docs/extensions/handins/DRAWBATCH.md).
// ============================================================================
#include "unit.h"
#include "ext/draw_batch/batch_fill.h"
#include "draw_batch/smoke_vs_rewrite.h"
#include "ext/registry.h"
#include "io/settings_file.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>

namespace {

using Ext::DrawBatch::FillRun;
using Ext::DrawBatch::kMaxRunVertices;
using Ext::DrawBatch::RetailAccepts;
using Ext::DrawBatch::RunFits;
using Ext::DrawBatch::Span;

using IO::SettingsFile::Entries;

bool EntriesLookup(void* ctx, const char* key, std::string& value) {
    const Entries* entries = static_cast<const Entries*>(ctx);
    for (const IO::SettingsFile::Entry& e : *entries) {
        if (e.key == key) {
            value = e.value;
            return true;
        }
    }
    return false;
}

Ext::State ResolveText(const char* text) {
    Entries entries = IO::SettingsFile::Parse(std::string(text));
    return Ext::Resolve(&EntriesLookup, &entries, nullptr, nullptr);
}

constexpr int32_t kStride = 0x14;

struct Batch {
    std::vector<uint8_t>  vertices;   // count * kStride bytes
    std::vector<uint16_t> indices;
    int32_t Count() const { return static_cast<int32_t>(vertices.size() / kStride); }
};

Batch MakeBatch(std::mt19937& rng, int32_t vertexCount, int32_t triangles) {
    Batch b;
    b.vertices.resize(static_cast<size_t>(vertexCount) * kStride);
    for (uint8_t& v : b.vertices) v = static_cast<uint8_t>(rng());
    std::uniform_int_distribution<int32_t> pick(0, vertexCount - 1);
    for (int32_t t = 0; t < triangles * 3; ++t) b.indices.push_back(static_cast<uint16_t>(pick(rng)));
    return b;
}

std::vector<Span> SpansOf(const std::vector<Batch>& batches) {
    std::vector<Span> spans;
    for (const Batch& b : batches) {
        Span s;
        s.vertices    = b.vertices.data();
        s.vertexCount = b.Count();
        s.indices     = b.indices.data();
        s.indexCount  = static_cast<int32_t>(b.indices.size());
        spans.push_back(s);
    }
    return spans;
}

}  // namespace

UNIT_TEST(ext_draw_batch_fill_is_the_concatenation_rebased) {
    std::mt19937 rng(20261020);
    std::vector<Batch> batches;
    for (int k = 0; k < 9; ++k) batches.push_back(MakeBatch(rng, 4 + k * 3, 2 + k));
    const std::vector<Span> spans = SpansOf(batches);

    size_t vbytes = 0;
    size_t icount = 0;
    for (const Batch& b : batches) {
        vbytes += b.vertices.size();
        icount += b.indices.size();
    }
    std::vector<uint8_t>  vb(vbytes + 8, 0xCD);
    std::vector<uint16_t> ib(icount + 4, 0xCDCD);
    const int64_t written = FillRun(spans.data(), static_cast<int32_t>(spans.size()), kStride, vb.data(), ib.data());
    CHECK_EQ(written, static_cast<int64_t>(icount));

    // Every merged triangle corner reads the same vertex bytes the batch's own draw read.
    size_t voff = 0;
    size_t ioff = 0;
    uint32_t base = 0;
    for (const Batch& b : batches) {
        CHECK(std::memcmp(vb.data() + voff, b.vertices.data(), b.vertices.size()) == 0);
        for (size_t k = 0; k < b.indices.size(); ++k) {
            const uint16_t merged = ib[ioff + k];
            CHECK_EQ(static_cast<uint32_t>(merged), b.indices[k] + base);
            CHECK(std::memcmp(vb.data() + static_cast<size_t>(merged) * kStride,
                              b.vertices.data() + static_cast<size_t>(b.indices[k]) * kStride, kStride) == 0);
        }
        voff += b.vertices.size();
        ioff += b.indices.size();
        base += static_cast<uint32_t>(b.Count());
    }
    // Nothing past the run is touched.
    CHECK_EQ(vb[vbytes], 0xCD);
    CHECK_EQ(ib[icount], 0xCDCD);
}

UNIT_TEST(ext_draw_batch_the_largest_index_is_65535) {
    std::mt19937 rng(7);
    // Two batches whose sum is exactly 65536 vertices: the second's last vertex is index 65535.
    std::vector<Batch> batches;
    batches.push_back(MakeBatch(rng, 65000, 1));
    batches.push_back(MakeBatch(rng, 536, 1));
    batches[1].indices[0] = 535;
    CHECK(RunFits(0x800000, kStride, 0, 0, 65000, 3));
    CHECK(RunFits(0x800000, kStride, 65000, 3, 536, 3));
    CHECK(!RunFits(0x800000, kStride, 65000, 3, 537, 3));   // 65537 vertices: cut
    const std::vector<Span> spans = SpansOf(batches);
    std::vector<uint8_t>  vb(65536 * kStride);
    std::vector<uint16_t> ib(6);
    FillRun(spans.data(), 2, kStride, vb.data(), ib.data());
    CHECK_EQ(ib[3], 65535);
}

UNIT_TEST(ext_draw_batch_retail_capacity_test) {
    // reject capacity <= vertices * stride (strict: equal is rejected)
    CHECK(RetailAccepts(1000, 49, 0x14, 0));
    CHECK(!RetailAccepts(1000, 50, 0x14, 0));
    // reject indices * 2 >= the VERTEX capacity (sic), whatever the index buffer's size
    CHECK(RetailAccepts(1000, 1, 0x14, 499));
    CHECK(!RetailAccepts(1000, 1, 0x14, 500));
    // a run is cut where the merged request fails, even when each item alone passes
    CHECK(RunFits(1000, 0x14, 0, 0, 30, 300));
    CHECK(!RunFits(1000, 0x14, 30, 300, 30, 0));    // 60 * 0x14 = 1200 bytes
    CHECK(!RunFits(1000, 0x14, 10, 300, 10, 200));  // 500 indices * 2 >= 1000
    CHECK(RunFits(1000, 0x14, 10, 300, 10, 199));
}

UNIT_TEST(ext_draw_batch_split_runs_cover_every_item_once) {
    // The greedy cut the DrawItemList copy uses: walk the items, start a new run when RunFits says no.
    std::mt19937 rng(42);
    std::uniform_int_distribution<int32_t> verts(1, 9000);
    const int32_t capacity = 0x800000;   // the 8 MB dynamic vertex buffer
    int64_t runV = 0;
    int64_t runI = 0;
    int64_t totalV = 0;
    int64_t seenV = 0;
    int runs = 1;
    for (int k = 0; k < 400; ++k) {
        const int32_t v = verts(rng);
        const int32_t idx = v * 3;
        totalV += v;
        if (!RunFits(capacity, kStride, runV, runI, v, idx)) {
            CHECK(runV > 0);
            seenV += runV;
            runV = 0;
            runI = 0;
            ++runs;
            CHECK(RunFits(capacity, kStride, 0, 0, v, idx));
        }
        runV += v;
        runI += idx;
        CHECK(runV <= kMaxRunVertices);
    }
    seenV += runV;
    CHECK_EQ(seenV, totalV);
    CHECK(runs > 1);
}

UNIT_TEST(ext_draw_batch_registry_row) {
    const Ext::Entry* e = Ext::FindEntry("draw_batch");
    CHECK(e != nullptr);
    if (!e) return;
    CHECK(e->tier == Ext::Tier::Extension);
    CHECK(e->cls == Ext::Class::A);
    CHECK(e->defaultOn);
    CHECK(Ext::SettingsKey(*e) == "ext.draw_batch");
    CHECK(ResolveText("").ext.draw_batch);                      // class A: on by default
    CHECK(!ResolveText("ext.draw_batch=0\n").ext.draw_batch);
    CHECK(!ResolveText("profile=faithful\n").ext.draw_batch);   // the faithful profile turns it off
}

// ============================================================================
// Idea B: the particle writer and the smoke VS rewrite
// ============================================================================
namespace {

using Ext::DrawBatch::ParticleVertex;
using Ext::DrawBatch::WriteParticles;

struct V3 {
    float x, y, z;
};
// The fields of RtGraphics::ParticleRecord WriteParticles reads, under their names.
struct FakeRecord {
    V3    _pos, _posPrev, _color, _colorPrev;
    float _alpha, _alphaPrev, _rot, _rotPrev, _size, _sizePrev;
};

FakeRecord MakeRecord(std::mt19937& rng) {
    std::uniform_real_distribution<float> f(-50.0f, 50.0f);
    FakeRecord r;
    r._pos       = {f(rng), f(rng), f(rng)};
    r._posPrev   = {f(rng), f(rng), f(rng)};
    r._color     = {f(rng), f(rng), f(rng)};
    r._colorPrev = {f(rng), f(rng), f(rng)};
    r._alpha     = f(rng);
    r._alphaPrev = f(rng);
    r._rot       = f(rng);
    r._rotPrev   = f(rng);
    r._size      = f(rng);
    r._sizePrev  = f(rng);
    return r;
}

uint32_t Bits(float f) {
    uint32_t b;
    std::memcpy(&b, &f, 4);
    return b;
}

}  // namespace

UNIT_TEST(ext_draw_batch_particles_are_retails_bytes_at_slot_0) {
    // The banner of ParticleInstance::Draw (wave27_lane_gfxpart.cpp), computed here independently:
    // record (count-1-i) for quad i, +0x0c the int 0, indices (b-2, b, b-1, b-2, b+1, b) from b = 2.
    std::mt19937 rng(5);
    std::vector<FakeRecord> recs;
    for (int k = 0; k < 7; ++k) recs.push_back(MakeRecord(rng));
    const float a = 0.37f;
    std::vector<ParticleVertex> vb(28);
    std::vector<uint16_t> ib(42);
    uint8_t* const out = reinterpret_cast<uint8_t*>(vb.data());   // CAST-OK: byte_io (the test's own buffer)
    CHECK_EQ(WriteParticles(recs.data(), 7, a, 0.0f, 0, out, ib.data()), 28);
    const float pi180 = Ext::DrawBatch::FloatBits(0x3C8EFA35u);
    for (int i = 0; i < 7; ++i) {
        const FakeRecord& r = recs[static_cast<size_t>(6 - i)];
        const float size = ((1.0f - a) * r._sizePrev) + (a * r._size);
        for (int c = 0; c < 4; ++c) {
            const ParticleVertex& v = vb[static_cast<size_t>(i * 4 + c)];
            CHECK_EQ(Bits(v.pos[0]), Bits((r._pos.x - r._posPrev.x) * a + r._posPrev.x));
            CHECK_EQ(Bits(v.color[2]), Bits((r._color.z - r._colorPrev.z) * a + r._colorPrev.z));
            CHECK_EQ(Bits(v.slot), 0u);   // retail's int 0
            CHECK_EQ(Bits(v.alpha), Bits(((1.0f - a) * r._alphaPrev) + (a * r._alpha)));
            CHECK_EQ(Bits(v.rot), Bits((((1.0f - a) * r._rotPrev) * pi180) + ((a * r._rot) * pi180)));
            const float cx = (c == 0 || c == 3) ? -1.0f : 1.0f;
            CHECK_EQ(Bits(v.cx), Bits(size * cx));
        }
        const uint16_t b = static_cast<uint16_t>(2 + 4 * i);
        const uint16_t want[6] = {static_cast<uint16_t>(b - 2), b, static_cast<uint16_t>(b - 1),
                                  static_cast<uint16_t>(b - 2), static_cast<uint16_t>(b + 1), b};
        for (int k = 0; k < 6; ++k) {
            CHECK_EQ(ib[static_cast<size_t>(i * 6 + k)], want[k]);
        }
    }
}

UNIT_TEST(ext_draw_batch_particles_in_a_run_only_move_the_slot_and_the_base) {
    // An emitter instance written at vertex 100 with slot 9: the same bytes as at vertex 0 with slot
    // 0, except +0x0c = 9.0f, and every index 100 higher.
    std::mt19937 rng(11);
    std::vector<FakeRecord> recs;
    for (int k = 0; k < 5; ++k) recs.push_back(MakeRecord(rng));
    std::vector<ParticleVertex> v0(20);
    std::vector<ParticleVertex> v9(20);
    std::vector<uint16_t> i0(30);
    std::vector<uint16_t> i9(30);
    uint8_t* const out0 = reinterpret_cast<uint8_t*>(v0.data());   // CAST-OK: byte_io (the test's own buffer)
    uint8_t* const out9 = reinterpret_cast<uint8_t*>(v9.data());   // CAST-OK: byte_io (the test's own buffer)
    WriteParticles(recs.data(), 5, 0.81f, 0.0f, 0, out0, i0.data());
    WriteParticles(recs.data(), 5, 0.81f, 9.0f, 100, out9, i9.data());
    for (size_t k = 0; k < 20; ++k) {
        CHECK_EQ(v9[k].slot, 9.0f);
        ParticleVertex patched = v9[k];
        patched.slot = 0.0f;
        CHECK(std::memcmp(&patched, &v0[k], sizeof patched) == 0);
    }
    for (size_t k = 0; k < 30; ++k) {
        CHECK_EQ(static_cast<int>(i9[k]), static_cast<int>(i0[k]) + 100);
    }
}

namespace {

namespace Op  = Dxbc::Op;
namespace OT  = Dxbc::OperandType;
namespace Enc = Dxbc::Enc;
using Tokens  = std::vector<uint32_t>;

constexpr uint32_t kXYZW = Enc::Swizzle(0, 1, 2, 3);

uint32_t Comp4(uint32_t sel, uint32_t bits, uint32_t type, uint32_t dims) {
    return 2u | (sel << 2) | (bits << 4) | (type << 12) | (dims << 20);
}
void Dst(Tokens& t, uint32_t type, uint32_t reg, uint32_t mask) {
    t.push_back(Comp4(0, mask, type, 1));
    t.push_back(reg);
}
void Swz(Tokens& t, uint32_t type, uint32_t reg, uint32_t swz) {
    t.push_back(Comp4(1, swz, type, 1));
    t.push_back(reg);
}
void Imm1(Tokens& t, uint32_t v) {
    t.push_back(1u | (OT::Imm32 << 12));
    t.push_back(v);
}
struct Ins {
    Tokens&  t;
    size_t   at;
    uint32_t extra;
    explicit Ins(Tokens& tt, uint32_t ex = 0) : t(tt), at(tt.size()), extra(ex) { t.push_back(0); }
    void End(uint32_t op) { t[at] = Enc::Opcode(op, static_cast<uint32_t>(t.size() - at), false, extra); }
};

constexpr uint32_t kDclInput    = 95;
constexpr uint32_t kDclInputSgv = 96;
constexpr uint32_t kFtou        = 28;
constexpr uint32_t kIadd        = 30;
constexpr uint32_t kIshl        = 41;

struct SmokeShape {
    uint32_t v0Mask   = 7;       // dcl_input v0.xyz
    bool     readV0w  = false;   // an instruction reads v0.w
    bool     twoReads = false;   // the instance id is read twice
    uint32_t shift    = 2;       // ishl's amount
    uint32_t cbSize   = 208;
    uint32_t v3Mask   = 1;       // the ISGN mask of v3 (the particle's rotation, float1)
};

// The shape of a particle VS: cb0 globals, cb3[208] dynamically indexed transforms, the particle
// inputs, the instance id, `ishl r0.x, v4.x, l(2)` and a use of r0.x.
Tokens SmokeProgram(const SmokeShape& s) {
    Tokens t;
    {
        Ins i(t);
        t.push_back(Comp4(1, kXYZW, OT::ConstantBuffer, 2));
        t.push_back(0);
        t.push_back(12);
        i.End(Op::DclConstantBuffer);
    }
    {
        Ins i(t, 1u << 11);   // dynamicIndexed
        t.push_back(Comp4(1, kXYZW, OT::ConstantBuffer, 2));
        t.push_back(3);
        t.push_back(s.cbSize);
        i.End(Op::DclConstantBuffer);
    }
    {
        Ins i(t);
        Dst(t, OT::Input, 0, s.v0Mask);
        i.End(kDclInput);
    }
    {
        Ins i(t);
        Dst(t, OT::Input, 1, 0xF);
        i.End(kDclInput);
    }
    {
        Ins i(t);
        Dst(t, OT::Input, 2, 0xF);
        i.End(kDclInput);
    }
    {
        Ins i(t);
        Dst(t, OT::Input, 3, 0x1);
        i.End(kDclInput);
    }
    {
        Ins i(t);   // dcl_input_sgv v4.x, instanceid
        Dst(t, OT::Input, 4, 0x1);
        t.push_back(8);
        i.End(kDclInputSgv);
    }
    {
        Ins i(t);
        t.push_back(3);
        i.End(Op::DclTemps);
    }
    {
        Ins i(t);   // ishl r0.x, v4.x, l(2)
        Dst(t, OT::Temp, 0, 1);
        Swz(t, OT::Input, 4, Enc::Rep(0));
        Imm1(t, s.shift);
        i.End(kIshl);
    }
    {
        Ins i(t);   // mov r1.xyz, v0.xyzx (the compiler's swizzle for an xyz read), or v0.wwww
        Dst(t, OT::Temp, 1, 7);
        Swz(t, OT::Input, 0, s.readV0w ? Enc::Rep(3) : Enc::Swizzle(0, 1, 2, 0));
        i.End(Op::Mov);
    }
    if (s.twoReads) {
        Ins i(t);
        Dst(t, OT::Temp, 2, 1);
        Swz(t, OT::Input, 4, Enc::Rep(0));
        i.End(Op::Mov);
    }
    {
        Ins i(t);   // mov r2.y, r0.x (stands for the cb3[r0.x + n] reads)
        Dst(t, OT::Temp, 2, 2);
        Swz(t, OT::Temp, 0, Enc::Rep(0));
        i.End(Op::Mov);
    }
    {
        Ins i(t);
        i.End(Op::Ret);
    }
    return t;
}

std::vector<uint8_t> SmokeIsgn(const SmokeShape& s) {
    std::vector<uint8_t> d;
    auto w32 = [&](uint32_t v) {
        for (int i = 0; i < 4; ++i) d.push_back(static_cast<uint8_t>(v >> (8 * i)));
    };
    w32(5);
    w32(8);
    const uint32_t texcoord = 8 + 5 * 24;
    const uint32_t iid      = texcoord + 12;
    const uint8_t  masks[4] = {0xF, 0xF, 0xF, static_cast<uint8_t>(s.v3Mask)};
    const uint8_t  used[4]  = {static_cast<uint8_t>(s.readV0w ? 0xF : 0x7), 0xF, 0xF, 0x1};
    for (uint32_t r = 0; r < 4; ++r) {
        w32(texcoord);
        w32(r);
        w32(0);
        w32(3);
        w32(r);
        d.push_back(masks[r]);
        d.push_back(used[r]);
        d.push_back(0);
        d.push_back(0);
    }
    w32(iid);
    w32(0);
    w32(8);
    w32(1);
    w32(4);
    d.push_back(1);
    d.push_back(1);
    d.push_back(0);
    d.push_back(0);
    for (char c : std::string("TEXCOORD")) d.push_back(static_cast<uint8_t>(c));
    while (d.size() < iid) d.push_back(0);
    for (char c : std::string("SV_InstanceID")) d.push_back(static_cast<uint8_t>(c));
    d.push_back(0);
    while (d.size() % 4) d.push_back(0);
    return d;
}

std::vector<uint8_t> SmokeBlob(const SmokeShape& s) {
    Dxbc::Program p;
    p.version = 0x10040;   // vs_4_0
    p.tokens  = SmokeProgram(s);
    Dxbc::Container c;
    Dxbc::Chunk a;
    a.fourcc = Dxbc::FourCC('I', 'S', 'G', 'N');
    a.data   = SmokeIsgn(s);
    Dxbc::Chunk sh;
    sh.fourcc = Dxbc::FourCC('S', 'H', 'D', 'R');
    sh.data   = Dxbc::EmitProgram(p);
    c.chunks  = {a, sh};
    return Dxbc::Emit(c);
}

// A tiny evaluator of the integer path: r0.x after the program, given v0.w's bits and the instance id.
bool EvalIndex(const std::vector<uint8_t>& blob, uint32_t v0wBits, uint32_t iid, uint32_t* out) {
    Dxbc::Container c;
    std::string err;
    if (!Dxbc::Parse(blob.data(), blob.size(), &c, &err)) return false;
    Dxbc::Program p;
    if (!Dxbc::ParseProgram(*c.Find(Dxbc::FourCC('S', 'H', 'D', 'R')), &p, &err)) return false;
    uint32_t r[3][4] = {};
    auto src = [&](const Dxbc::Operand& o) -> uint32_t {
        if (o.type == OT::Imm32) return o.imm[0];
        if (o.type == OT::Input && o.index[0] == 4) return iid;
        if (o.type == OT::Input && o.index[0] == 0 && o.Swz(0) == 3) return v0wBits;
        if (o.type == OT::Temp) return r[o.index[0]][o.Swz(0)];
        return 0xDEADu;
    };
    for (const Dxbc::Instr& in : p.instrs) {
        if (in.isDecl || in.operands.empty()) continue;
        const Dxbc::Operand& d = in.operands[0];
        uint32_t comp = 0;
        while (comp < 4 && ((d.Mask() >> comp) & 1) == 0) ++comp;
        if (d.type != OT::Temp || comp == 4) continue;
        uint32_t v = 0;
        if (in.opcode == kFtou) {
            float f;
            const uint32_t b = src(in.operands[1]);
            std::memcpy(&f, &b, 4);
            v = static_cast<uint32_t>(f);
        } else if (in.opcode == kIadd) {
            v = src(in.operands[1]) + src(in.operands[2]);
        } else if (in.opcode == kIshl) {
            v = src(in.operands[1]) << (src(in.operands[2]) & 31);
        } else {
            continue;
        }
        r[d.index[0]][comp] = v;
    }
    *out = r[0][0];
    return true;
}

SmokeVs::Match AnalyseSmoke(const std::vector<uint8_t>& b) {
    SmokeVs::Match m;
    SmokeVs::Analyse(b.data(), b.size(), &m);
    return m;
}

}  // namespace

UNIT_TEST(ext_draw_batch_smoke_vs_match_and_meaning) {
    const std::vector<uint8_t> orig = SmokeBlob(SmokeShape());
    const SmokeVs::Match m = AnalyseSmoke(orig);
    CHECK(m.matched);
    if (!m.matched) {
        std::fprintf(stderr, "  smoke VS not matched: %s\n", m.why.c_str());
        return;
    }
    std::vector<uint8_t> xf;
    CHECK(SmokeVs::Transform(m, &xf));
    // The copy computes retail's index for every vertex retail writes (+0x0C = int 0)...
    for (uint32_t iid = 0; iid < 52; ++iid) {
        uint32_t a = 0;
        uint32_t b = 0;
        CHECK(EvalIndex(orig, 0, iid, &a));
        CHECK(EvalIndex(xf, 0, iid, &b));
        CHECK_EQ(a, iid << 2);
        CHECK_EQ(b, a);
    }
    // ...and transforms[k + iid] for a merged vertex with k at +0x0C.
    for (uint32_t k = 0; k < 52; ++k) {
        uint32_t b = 0;
        CHECK(EvalIndex(xf, Bits(static_cast<float>(k)), 0, &b));
        CHECK_EQ(b, k << 2);
    }
    // v0 is declared and signed as read in w; the copy is not a candidate a second time.
    CHECK(!AnalyseSmoke(xf).matched);
    Dxbc::Container c;
    std::string err;
    CHECK(Dxbc::Parse(xf.data(), xf.size(), &c, &err));
    const Dxbc::Chunk* isgn = c.Find(Dxbc::FourCC('I', 'S', 'G', 'N'));
    CHECK(isgn != nullptr);
    if (isgn) CHECK_EQ(isgn->data[8 + 21], 0xF);
    Dxbc::Program p;
    CHECK(Dxbc::ParseProgram(*c.Find(Dxbc::FourCC('S', 'H', 'D', 'R')), &p, &err));
    CHECK_EQ(p.instrs.size(), m.prog.instrs.size() + 2);
    // Every original instruction but the ishl and dcl_input v0 is in the copy unchanged, in order.
    size_t at = 0;
    for (size_t i = 0; i < m.prog.instrs.size(); ++i) {
        const Dxbc::Instr& a = m.prog.instrs[i];
        if (static_cast<int>(i) == m.shiftIdx) {
            CHECK_EQ(p.instrs[at].opcode, kFtou);
            CHECK_EQ(p.instrs[at + 1].opcode, kIadd);
            CHECK_EQ(p.instrs[at + 2].opcode, kIshl);
            at += 3;
            continue;
        }
        const Dxbc::Instr& b = p.instrs[at];
        CHECK_EQ(a.length, b.length);
        const bool same = std::memcmp(m.prog.tokens.data() + a.offset, p.tokens.data() + b.offset, a.length * 4) == 0;
        CHECK(same == (static_cast<int>(i) != m.dclInputIdx));
        ++at;
    }
}

UNIT_TEST(ext_draw_batch_smoke_vs_refusals) {
    {
        SmokeShape s;
        s.readV0w = true;   // v0.w already read
        CHECK(!AnalyseSmoke(SmokeBlob(s)).matched);
    }
    {
        SmokeShape s;
        s.v0Mask = 0xF;   // dcl_input v0.xyzw
        CHECK(!AnalyseSmoke(SmokeBlob(s)).matched);
    }
    {
        SmokeShape s;
        s.twoReads = true;   // the instance id read twice
        CHECK(!AnalyseSmoke(SmokeBlob(s)).matched);
    }
    {
        SmokeShape s;
        s.shift = 3;   // not the matrix stride
        CHECK(!AnalyseSmoke(SmokeBlob(s)).matched);
    }
    {
        SmokeShape s;
        s.cbSize = 192;   // not 52 matrices
        CHECK(!AnalyseSmoke(SmokeBlob(s)).matched);
    }
    {
        SmokeShape s;
        s.v3Mask = 3;   // not the particle vertex
        CHECK(!AnalyseSmoke(SmokeBlob(s)).matched);
    }
    const std::vector<uint8_t> junk(64, 0x5A);
    CHECK(!AnalyseSmoke(junk).matched);
}

// ---------------------------------------------------------------------------
// Issue #253: wide instancing batches (batch_fill.h WideBatchCap / ContinuesPastCut).
// ---------------------------------------------------------------------------
namespace {

using Ext::DrawBatch::ContinuesPastCut;
using Ext::DrawBatch::InstanceKey;
using Ext::DrawBatch::JoinsBatch;
using Ext::DrawBatch::kRetailInstanceSlots;
using Ext::DrawBatch::WideBatchCap;
using Ext::DrawBatch::WideBatchesOfChain;

int g_instanceTag = 0;
int g_meshTag = 0;

InstanceKey Tree(uint32_t opacityBits = 0x3F800000u) {
    InstanceKey k;
    k.instance = &g_instanceTag;
    k.listIndex = 0x1003;
    k.userData = &g_meshTag;
    k.animationHandle = 0xffffffffu;
    k.maxInstances = kRetailInstanceSlots;
    k.opacity = opacityBits;
    k.opacityPrev = opacityBits;
    k.wideOk = true;
    return k;
}

// Retail's batch length at keys[first] (DrawItemList: the join keys, the head's _arg8, the list's end).
int32_t RetailLength(const std::vector<InstanceKey>& keys, size_t first) {
    int32_t n = 1;
    while (first + n < keys.size() && n < keys[first].maxInstances && JoinsBatch(keys[first], keys[first + n])) ++n;
    return n;
}

// draw_batch.cpp's batch loop on keys alone: the same tests in the same order.
int32_t WideLength(const std::vector<InstanceKey>& keys, size_t first, int32_t bankCap) {
    const InstanceKey& head = keys[first];
    const int32_t wideCap = WideBatchCap(head.maxInstances, bankCap);
    int32_t n = 0;
    for (;;) {
        ++n;
        const size_t at = first + n;
        if (at >= keys.size()) break;
        if (n >= head.maxInstances && n % head.maxInstances == 0) {
            const InstanceKey* after = at + 1 < keys.size() ? &keys[at + 1] : nullptr;
            const bool goesOn = n < wideCap && ContinuesPastCut(head, keys[at], after);
            if (!goesOn) break;
        }
        if (!JoinsBatch(head, keys[at])) break;
    }
    return n;
}

}  // namespace

UNIT_TEST(ext_draw_batch_wide_cap_is_a_multiple_of_the_full_bank) {
    CHECK_EQ(WideBatchCap(52, 52), 52);
    CHECK_EQ(WideBatchCap(52, 104), 104);
    CHECK_EQ(WideBatchCap(52, 256), 208);    // rounded down to a retail cut
    CHECK_EQ(WideBatchCap(52, 1024), 988);
    CHECK_EQ(WideBatchCap(32, 1024), 32);    // a mesh that does not fill the bank keeps its own cap
    CHECK_EQ(WideBatchCap(1, 1024), 1);
    CHECK_EQ(WideBatchCap(52, 40), 52);      // a smaller backend never shrinks retail's batch
}

UNIT_TEST(ext_draw_batch_wide_continues_only_where_retail_draws_the_same) {
    const InstanceKey head = Tree();
    const InstanceKey next = Tree();
    CHECK(ContinuesPastCut(head, next, &next));
    CHECK(!ContinuesPastCut(head, next, nullptr));    // the retail batch there is a single draw
    InstanceKey other = Tree();
    other.listIndex = 0x1004;
    CHECK(!ContinuesPastCut(head, next, &other));     // ... likewise
    CHECK(!ContinuesPastCut(head, other, &other));    // another list
    const InstanceKey fading = Tree(0x3F000000u);
    CHECK(!ContinuesPastCut(head, fading, &fading));  // MeshInstance::Draw's colour is the head's
    InstanceKey skinned = Tree();
    skinned.wideOk = false;
    CHECK(!ContinuesPastCut(head, skinned, &skinned));
    CHECK(!ContinuesPastCut(skinned, next, &next));
    InstanceKey smaller = Tree();
    smaller.maxInstances = 26;
    CHECK(!ContinuesPastCut(head, smaller, &smaller));
}

UNIT_TEST(ext_draw_batch_wide_batches_end_where_retail_batches_end) {
    // A forest list: long runs of one tree list, a fading tree here and there, other lists between.
    std::mt19937 rng(253);
    std::vector<InstanceKey> keys;
    for (int run = 0; run < 400; ++run) {
        const int len = static_cast<int>(rng() % 700) + 1;
        for (int k = 0; k < len; ++k) {
            const bool fade = rng() % 97 == 0;
            keys.push_back(Tree(fade ? 0x3F400000u : 0x3F800000u));
        }
        InstanceKey other = Tree();
        other.listIndex = 0x2000u + static_cast<uint32_t>(rng() % 3);
        other.wideOk = rng() % 2 == 0;
        keys.push_back(other);
    }
    std::vector<bool> retailHead(keys.size(), false);
    for (size_t i = 0; i < keys.size(); i += static_cast<size_t>(RetailLength(keys, i))) retailHead[i] = true;
    for (int32_t bank : { 52, 104, 208, 988 }) {
        size_t i = 0;
        int64_t draws = 0;
        while (i < keys.size()) {
            CHECK(retailHead[i]);   // every wide batch starts (so the one before ends) where a retail one does
            const int32_t n = WideLength(keys, i, bank);
            CHECK(n <= WideBatchCap(keys[i].maxInstances, bank));
            // Every retail head crossed has the wide head's keys and opacity and starts a draw of two or more.
            for (int32_t k = 1; k < n; ++k) {
                if (!retailHead[i + k]) continue;
                CHECK(JoinsBatch(keys[i], keys[i + k]));
                CHECK(keys[i + k].opacity == keys[i].opacity);
                CHECK(RetailLength(keys, i + k) >= 2);
            }
            if (bank == 52) CHECK_EQ(n, RetailLength(keys, i));   // 52 is retail's batch exactly
            i += static_cast<size_t>(n);
            ++draws;
        }
        CHECK(i == keys.size());
        CHECK(draws > 0);
    }
}

UNIT_TEST(ext_draw_batch_wide_batches_of_a_chain) {
    CHECK_EQ(WideBatchesOfChain(0, 4), 0);
    CHECK_EQ(WideBatchesOfChain(1, 4), 1);
    CHECK_EQ(WideBatchesOfChain(4, 4), 1);
    CHECK_EQ(WideBatchesOfChain(5, 4), 2);
    CHECK_EQ(WideBatchesOfChain(7, 1), 7);
    CHECK_EQ(WideBatchesOfChain(40, 19), 3);
}
