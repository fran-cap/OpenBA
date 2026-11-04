// ============================================================================
// tests/unit/vulkan_shader_convert_test.cpp -- src/render/vulkan/shader_convert.{h,cpp} and
// shader_cache.cpp: DXBC to SPIR-V, the binding layout, the VS -> PS link, the SPIR-V cache
// ============================================================================
// BUILT ONLY WITH THE VULKAN BACKEND (BANISHED_RENDER_BACKEND=vulkan): CMakeLists.txt filters
// tests/unit/vulkan_*_test.cpp out of every other configuration, because the converter links
// dxbc-spirv, which only src/render/vulkan/backend.cmake builds. Runs on any host (no device).
//
// NO RETAIL BYTES: both programs are SYNTHETIC, assembled here token by token with the better_trees
// DXBC writer (src/render/common/better_trees/dxbc.h: the container, its checksum, the SM4 token
// encoders). Unlike that test's blobs these are complete programs, with declarations and real
// ISGN / OSGN signatures, because dxbc-spirv converts the whole program:
//   vs_4_0  POSITION0 -> v0, TEXCOORD0 -> v1; o0 = SV_Position = v0 * cb0[0..3], o1 = COLOR0 =
//           cb0[0], o2 = TEXCOORD0 = v1.xy
//   ps_4_0  v1 = TEXCOORD0 (linear); o0 = SV_Target0 = sample(t0, s0, v1.xy) * cb1[0]
// The pixel program reads TEXCOORD0 from v1 while the vertex program writes it to o2: a pair that
// D3D links by semantic and the register numbering would mislink, which is what the linked
// conversion (ConvertOptions::previousStage) is for.
// ============================================================================
#include "unit.h"

#include "better_trees/dxbc.h"
#include "ext/derived_cache/derived_cache.h"
#include "render/vulkan/shader_convert.h"

#include <chrono>
#include <filesystem>
#include <initializer_list>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;
using namespace VideoVulkan::Shaders;

namespace {

namespace Op  = Dxbc::Op;
namespace OT  = Dxbc::OperandType;
namespace Enc = Dxbc::Enc;
using Tokens  = std::vector<uint32_t>;

// D3D10_SB_OPCODE_TYPE values the better_trees writer does not name.
constexpr uint32_t kDclInput     = 95;
constexpr uint32_t kDclInputPs   = 98;
constexpr uint32_t kDclOutput    = 101;
constexpr uint32_t kDclOutputSiv = 103;
constexpr uint32_t kXYZW         = Enc::Swizzle(0, 1, 2, 3);

uint32_t Comp4(uint32_t sel, uint32_t bits, uint32_t type, uint32_t dims) {
    return 2u | (sel << 2) | (bits << 4) | (type << 12) | (dims << 20);
}

struct Ins {
    Tokens& t;
    size_t  at;
    explicit Ins(Tokens& tt) : t(tt), at(tt.size()) { t.push_back(0); }
    void End(uint32_t op, uint32_t extra = 0) {
        t[at] = Enc::Opcode(op, static_cast<uint32_t>(t.size() - at), false, extra);
    }
};

void Dst(Tokens& t, uint32_t type, uint32_t reg, uint32_t mask) {
    t.push_back(Comp4(0, mask, type, 1));
    t.push_back(reg);
}
void Src(Tokens& t, uint32_t type, uint32_t reg, uint32_t swz) {
    t.push_back(Comp4(1, swz, type, 1));
    t.push_back(reg);
}
void Cb(Tokens& t, uint32_t cb, uint32_t row) {
    t.push_back(Comp4(1, kXYZW, OT::ConstantBuffer, 2));
    t.push_back(cb);
    t.push_back(row);
}
void DclCb(Tokens& t, uint32_t cb, uint32_t rows) {
    Ins i(t);
    t.push_back(Comp4(1, kXYZW, OT::ConstantBuffer, 2));
    t.push_back(cb);
    t.push_back(rows);
    i.End(Op::DclConstantBuffer);
}
void DclIo(Tokens& t, uint32_t op, uint32_t type, uint32_t reg, uint32_t mask, uint32_t extra = 0) {
    Ins i(t);
    Dst(t, type, reg, mask);
    i.End(op, extra);
}

// One signature element: name, semantic index, system value, register, mask, read / write mask.
struct Element {
    const char* name;
    uint32_t    index;
    uint32_t    systemValue;
    uint32_t    reg;
    uint8_t     mask;
    uint8_t     rw;
};

// ISGN / OSGN: count, 8, then 24-byte elements {name offset, semantic index, system value, component
// type (3 = float32), register, mask, read-write mask, 2 pad}, then the names.
std::vector<uint8_t> Signature(const std::vector<Element>& elements) {
    std::vector<uint8_t> d;
    auto w32 = [&](uint32_t v) {
        for (int i = 0; i < 4; ++i)
            d.push_back(static_cast<uint8_t>(v >> (8 * i)));
    };
    w32(static_cast<uint32_t>(elements.size()));
    w32(8);
    std::string names;
    std::vector<uint32_t> offsets;
    const uint32_t base = 8 + 24 * static_cast<uint32_t>(elements.size());
    for (const Element& e : elements) {
        offsets.push_back(base + static_cast<uint32_t>(names.size()));
        names += e.name;
        names.push_back('\0');
    }
    for (size_t k = 0; k < elements.size(); ++k) {
        const Element& e = elements[k];
        w32(offsets[k]);
        w32(e.index);
        w32(e.systemValue);
        w32(3);
        w32(e.reg);
        d.push_back(e.mask);
        d.push_back(e.rw);
        d.push_back(0);
        d.push_back(0);
    }
    for (char c : names)
        d.push_back(static_cast<uint8_t>(c));
    while (d.size() % 4)
        d.push_back(0);
    return d;
}

std::vector<uint8_t> Container(uint32_t version, const Tokens& tokens, const std::vector<Element>& in,
                               const std::vector<Element>& out) {
    Dxbc::Program p;
    p.version = version;
    p.tokens  = tokens;
    Dxbc::Chunk isgn;
    isgn.fourcc = Dxbc::FourCC('I', 'S', 'G', 'N');
    isgn.data   = Signature(in);
    Dxbc::Chunk osgn;
    osgn.fourcc = Dxbc::FourCC('O', 'S', 'G', 'N');
    osgn.data   = Signature(out);
    Dxbc::Chunk shdr;
    shdr.fourcc = Dxbc::FourCC('S', 'H', 'D', 'R');
    shdr.data   = Dxbc::EmitProgram(p);
    Dxbc::Container c;
    c.chunks = {isgn, osgn, shdr};
    return Dxbc::Emit(c);
}

// An instruction (or declaration) whose operands are given as raw tokens.
void Raw(Tokens& t, uint32_t op, std::initializer_list<uint32_t> words, uint32_t extra = 0) {
    Ins i(t);
    t.insert(t.end(), words.begin(), words.end());
    i.End(op, extra);
}

// dst = a * b (mul) or dst = a * b + c (mad), each operand appended by its own callback.
template <class A, class B>
void Mul(Tokens& t, uint32_t dstType, uint32_t dstReg, A a, B b) {
    Ins i(t);
    Dst(t, dstType, dstReg, 0xF);
    a(t);
    b(t);
    i.End(Op::Mul);
}

template <class A, class B, class C>
void Mad(Tokens& t, uint32_t dstType, uint32_t dstReg, A a, B b, C c) {
    Ins i(t);
    Dst(t, dstType, dstReg, 0xF);
    a(t);
    b(t);
    c(t);
    i.End(Op::Mad);
}

auto CbOp(uint32_t cb, uint32_t row) {
    return [=](Tokens& t) { Cb(t, cb, row); };
}
auto SrcOp(uint32_t type, uint32_t reg, uint32_t swz) {
    return [=](Tokens& t) { Src(t, type, reg, swz); };
}

constexpr uint32_t kSamplerOperand  = (OT::Sampler << 12) | (1u << 20);    // s#, no components
constexpr uint32_t kResourceOperand = (OT::Resource << 12) | (1u << 20);   // t#, no components

std::vector<uint8_t> VertexBlob() {
    Tokens t;
    DclCb(t, 0, 4);
    DclIo(t, kDclInput, OT::Input, 0, 0xF);
    DclIo(t, kDclInput, OT::Input, 1, 0x3);
    Raw(t, kDclOutputSiv, {Comp4(0, 0xF, OT::Output, 1), 0, 1 /* D3D10_SB_NAME_POSITION */});
    DclIo(t, kDclOutput, OT::Output, 1, 0xF);
    DclIo(t, kDclOutput, OT::Output, 2, 0x3);
    Raw(t, Op::DclTemps, {1});
    // r0 = v0.y * cb0[1]; r0 = cb0[0] * v0.x + r0; r0 = cb0[2] * v0.z + r0; o0 = cb0[3] * v0.w + r0
    Mul(t, OT::Temp, 0, SrcOp(OT::Input, 0, Enc::Rep(1)), CbOp(0, 1));
    Mad(t, OT::Temp, 0, CbOp(0, 0), SrcOp(OT::Input, 0, Enc::Rep(0)), SrcOp(OT::Temp, 0, kXYZW));
    Mad(t, OT::Temp, 0, CbOp(0, 2), SrcOp(OT::Input, 0, Enc::Rep(2)), SrcOp(OT::Temp, 0, kXYZW));
    Mad(t, OT::Output, 0, CbOp(0, 3), SrcOp(OT::Input, 0, Enc::Rep(3)), SrcOp(OT::Temp, 0, kXYZW));
    // o1 = cb0[0]; o2.xy = v1.xy
    {
        Ins i(t);
        Dst(t, OT::Output, 1, 0xF);
        Cb(t, 0, 0);
        i.End(Op::Mov);
    }
    {
        Ins i(t);
        Dst(t, OT::Output, 2, 0x3);
        Src(t, OT::Input, 1, Enc::Swizzle(0, 1, 0, 0));
        i.End(Op::Mov);
    }
    Raw(t, Op::Ret, {});
    return Container(0x10040, t,
                     {{"POSITION", 0, 0, 0, 0xF, 0xF}, {"TEXCOORD", 0, 0, 1, 0x3, 0x3}},
                     {{"SV_POSITION", 0, 1, 0, 0xF, 0x0},
                      {"COLOR", 0, 0, 1, 0xF, 0x0},
                      {"TEXCOORD", 0, 0, 2, 0x3, 0x0}});
}

std::vector<uint8_t> PixelBlob() {
    Tokens t;
    Raw(t, Op::DclSampler, {kSamplerOperand, 0});
    // dcl_resource_texture2d (float x4) t0: dimension 3 in bits 11..15, return type 5 (float) per lane
    Raw(t, Op::DclResource, {kResourceOperand, 0, 0x5555}, 3u << 11);
    DclCb(t, 1, 1);
    DclIo(t, kDclInputPs, OT::Input, 1, 0x3, 2u << 11);   // linear
    DclIo(t, kDclOutput, OT::Output, 0, 0xF);
    Raw(t, Op::DclTemps, {1});
    // sample r0, v1.xyxx, t0, s0
    {
        Ins i(t);
        Dst(t, OT::Temp, 0, 0xF);
        Src(t, OT::Input, 1, Enc::Swizzle(0, 1, 0, 0));
        Src(t, OT::Resource, 0, kXYZW);
        t.push_back(kSamplerOperand);
        t.push_back(0);
        i.End(Op::Sample);
    }
    // o0 = r0 * cb1[0]
    Mul(t, OT::Output, 0, SrcOp(OT::Temp, 0, kXYZW), CbOp(1, 0));
    Raw(t, Op::Ret, {});
    return Container(0x40, t,
                     {{"SV_POSITION", 0, 1, 0, 0xF, 0x0}, {"TEXCOORD", 0, 0, 1, 0x3, 0x3}},
                     {{"SV_TARGET", 0, 64, 0, 0xF, 0x0}});
}

// (set, binding) of every id the module decorates with both (OpDecorate = 71, DescriptorSet = 34,
// Binding = 33).
std::set<std::pair<uint32_t, uint32_t>> Bindings(const std::vector<uint32_t>& spirv) {
    std::map<uint32_t, uint32_t> set;
    std::map<uint32_t, uint32_t> binding;
    for (size_t at = 5; at < spirv.size();) {
        const uint32_t words = spirv[at] >> 16;
        const uint32_t op = spirv[at] & 0xFFFF;
        if (words == 0) break;
        if (op == 71 && words >= 4) {
            if (spirv[at + 2] == 34) set[spirv[at + 1]] = spirv[at + 3];
            if (spirv[at + 2] == 33) binding[spirv[at + 1]] = spirv[at + 3];
        }
        at += words;
    }
    std::set<std::pair<uint32_t, uint32_t>> out;
    for (const auto& kv : binding) {
        auto s = set.find(kv.first);
        out.insert({s == set.end() ? 0xFFFFFFFFu : s->second, kv.second});
    }
    return out;
}

const IoElement* FindSemantic(const std::vector<IoElement>& elements, const char* name) {
    for (const IoElement& e : elements) {
        if (e.semantic == name) return &e;
    }
    return nullptr;
}

struct TempDir {
    fs::path dir;
    TempDir() {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        const std::string name = "banished_vkshader_unit_" + std::to_string(static_cast<long long>(stamp));
        dir = fs::temp_directory_path() / name;
        fs::create_directories(dir);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
    std::string Path() const { return dir.u8string(); }
};

}  // namespace

UNIT_TEST(vulkan_shader_layout_formulas) {
    CHECK_EQ(ConstantBufferSlot(Stage::Vertex, 0).set, kSetVertex);
    CHECK_EQ(ConstantBufferSlot(Stage::Pixel, 3).set, kSetPixel);
    CHECK_EQ(ConstantBufferSlot(Stage::Pixel, 3).binding, 3u);
    CHECK_EQ(TextureSlot(Stage::Pixel, 0).binding, kMaxConstantBuffers);
    CHECK_EQ(SamplerSlot(Stage::Pixel, 2).binding, kMaxConstantBuffers + kMaxTextures + 2);
    CHECK_EQ(kBindingsPerSet, kMaxConstantBuffers + kMaxTextures + kMaxSamplers);
}

UNIT_TEST(vulkan_shader_stage_of_dxbc) {
    Stage s = Stage::Pixel;
    std::string why;
    const std::vector<uint8_t> vs = VertexBlob();
    CHECK(StageOfDxbc(vs.data(), vs.size(), &s, &why));
    CHECK(s == Stage::Vertex);
    const std::vector<uint8_t> ps = PixelBlob();
    CHECK(StageOfDxbc(ps.data(), ps.size(), &s, &why));
    CHECK(s == Stage::Pixel);
    const uint8_t junk[40] = {1, 2, 3};
    CHECK(!StageOfDxbc(junk, sizeof(junk), &s, &why));
    CHECK(!why.empty());
}

UNIT_TEST(vulkan_shader_convert_vertex) {
    const std::vector<uint8_t> blob = VertexBlob();
    std::vector<uint32_t> spirv;
    ShaderReflection refl;
    std::string why;
    const bool ok = ConvertDxbc(blob.data(), blob.size(), Stage::Vertex, &spirv, &refl, &why);
    if (!ok) std::printf("    convert: %s\n", why.c_str());
    CHECK(ok);
    CHECK(CheckSpirvStructure(spirv, &why));
    CHECK_EQ(spirv[0], 0x07230203u);
    CHECK(refl.stage == Stage::Vertex);
    CHECK_EQ(refl.constantBufferMask, 1u);
    CHECK_EQ(refl.textureMask, 0u);
    CHECK_EQ(refl.samplerMask, 0u);
    const auto bindings = Bindings(spirv);
    CHECK_EQ(bindings.size(), (size_t)1);
    CHECK(bindings.count({kSetVertex, BindingOfConstantBuffer(0)}) == 1);
    // the vertex input semantics P2's layouts bind by
    const IoElement* pos = FindSemantic(refl.inputs, "POSITION");
    const IoElement* uv = FindSemantic(refl.inputs, "TEXCOORD");
    CHECK(pos != nullptr && uv != nullptr);
    if (pos && uv) {
        CHECK_EQ(pos->location, 0u);
        CHECK_EQ(uv->location, 1u);
        CHECK_EQ(uv->componentMask, 0x3);
    }
    const IoElement* outUv = FindSemantic(refl.outputs, "TEXCOORD");
    CHECK(outUv != nullptr && outUv->location == 2u);
    // a vertex blob is not a pixel program
    CHECK(!ConvertDxbc(blob.data(), blob.size(), Stage::Pixel, &spirv, &refl, &why));
}

UNIT_TEST(vulkan_shader_convert_pixel_and_link) {
    const std::vector<uint8_t> vsBlob = VertexBlob();
    const std::vector<uint8_t> psBlob = PixelBlob();
    std::vector<uint32_t> vsCode;
    std::vector<uint32_t> psCode;
    ShaderReflection vs;
    ShaderReflection ps;
    std::string why;
    CHECK(ConvertDxbc(vsBlob.data(), vsBlob.size(), Stage::Vertex, &vsCode, &vs, &why));
    const bool ok = ConvertDxbc(psBlob.data(), psBlob.size(), Stage::Pixel, &psCode, &ps, &why);
    if (!ok) std::printf("    convert: %s\n", why.c_str());
    CHECK(ok);
    CHECK_EQ(ps.constantBufferMask, 2u);
    CHECK_EQ(ps.textureMask, 1u);
    CHECK_EQ(ps.samplerMask, 1u);
    const auto bindings = Bindings(psCode);
    CHECK(bindings.count({kSetPixel, BindingOfConstantBuffer(1)}) == 1);
    CHECK(bindings.count({kSetPixel, BindingOfTexture(0)}) == 1);
    CHECK(bindings.count({kSetPixel, BindingOfSampler(0)}) == 1);
    CHECK_EQ(bindings.size(), (size_t)3);
    const IoElement* uv = FindSemantic(ps.inputs, "TEXCOORD");
    CHECK(uv != nullptr && uv->location == 1u);

    // TEXCOORD0 is o2 in the vertex program and v1 here: the register numbering mislinks the pair.
    CHECK(!StagesCompatible(vs, ps));
    ConvertOptions link;
    link.previousStage = &vs;
    std::vector<uint32_t> linkedCode;
    ShaderReflection linked;
    CHECK(ConvertDxbc(psBlob.data(), psBlob.size(), Stage::Pixel, &linkedCode, &linked, &why, link));
    const IoElement* moved = FindSemantic(linked.inputs, "TEXCOORD");
    CHECK(moved != nullptr && moved->location == 2u);
    CHECK(StagesCompatible(vs, linked));
    CHECK(CheckSpirvStructure(linkedCode, &why));
    // a linked conversion is a different cache entry
    CHECK(CacheKey(psBlob.data(), psBlob.size(), Stage::Pixel).shape[3] == 0u);
    CHECK(CacheKey(psBlob.data(), psBlob.size(), Stage::Pixel, link).shape[3] != 0u);
}

UNIT_TEST(vulkan_shader_structure_check_refusals) {
    const std::vector<uint8_t> blob = PixelBlob();
    std::vector<uint32_t> spirv;
    ShaderReflection refl;
    std::string why;
    CHECK(ConvertDxbc(blob.data(), blob.size(), Stage::Pixel, &spirv, &refl, &why));
    std::vector<uint32_t> bad = spirv;
    bad[0] = 0;
    CHECK(!CheckSpirvStructure(bad, &why));
    bad = spirv;
    bad.back() = (2u << 16) | (bad.back() & 0xFFFFu);   // the last (one-word) instruction claims two
    CHECK(!CheckSpirvStructure(bad, &why));
    bad = spirv;
    bad.push_back(0);   // a zero word count
    CHECK(!CheckSpirvStructure(bad, &why));
}

UNIT_TEST(vulkan_shader_cache_round_trip) {
    const std::vector<uint8_t> blob = PixelBlob();
    std::vector<uint32_t> first;
    ShaderReflection firstRefl;
    std::string why;

    // the payload codec alone
    CHECK(ConvertDxbc(blob.data(), blob.size(), Stage::Pixel, &first, &firstRefl, &why));
    std::vector<uint32_t> decoded;
    ShaderReflection decodedRefl;
    std::vector<uint8_t> payload = EncodeEntry(first, firstRefl);
    CHECK(DecodeEntry(payload, &decoded, &decodedRefl));
    CHECK(decoded == first);
    CHECK_EQ(decodedRefl.textureMask, firstRefl.textureMask);
    CHECK_EQ(decodedRefl.inputs.size(), firstRefl.inputs.size());
    CHECK(decodedRefl.inputs[0].semantic == firstRefl.inputs[0].semantic);
    payload.pop_back();
    CHECK(!DecodeEntry(payload, &decoded, &decodedRefl));

    // through a store over a temp folder: a miss that converts, then hits, also after a reopen
    TempDir dir;
    {
        Ext::DerivedCache::Store store;
        CHECK(store.Open(dir.Path(), 64ull << 20) == Ext::DerivedCache::Store::OpenStatus::Opened);
        std::vector<uint32_t> spirv;
        ShaderReflection refl;
        bool hit = true;
        CHECK(LoadOrConvert(&store, blob.data(), blob.size(), Stage::Pixel, &spirv, &refl, &hit, &why));
        CHECK(!hit);
        CHECK(spirv == first);
        store.Flush();
        CHECK(LoadOrConvert(&store, blob.data(), blob.size(), Stage::Pixel, &spirv, &refl, &hit, &why));
        CHECK(hit);
        CHECK(spirv == first);
        // the same bytes as a vertex program are another key (and fail to convert)
        CHECK(!LoadOrConvert(&store, blob.data(), blob.size(), Stage::Vertex, &spirv, &refl, &hit, &why));
        store.Close();
    }
    {
        Ext::DerivedCache::Store store;
        CHECK(store.Open(dir.Path(), 64ull << 20) == Ext::DerivedCache::Store::OpenStatus::Opened);
        std::vector<uint32_t> spirv;
        ShaderReflection refl;
        bool hit = false;
        CHECK(LoadOrConvert(&store, blob.data(), blob.size(), Stage::Pixel, &spirv, &refl, &hit, &why));
        CHECK(hit);
        CHECK(spirv == first);
        CHECK_EQ(refl.samplerMask, firstRefl.samplerMask);
        CHECK(store.GetStats().hits >= 1);
        store.Close();
    }
    // no store: always a conversion
    std::vector<uint32_t> spirv;
    ShaderReflection refl;
    bool hit = true;
    CHECK(LoadOrConvert(nullptr, blob.data(), blob.size(), Stage::Pixel, &spirv, &refl, &hit, &why));
    CHECK(!hit);
}

// ---- P8: combined image samplers (src/render/vulkan/spirv_combine.{h,cpp}) ----------------------

namespace {

// A pixel program that samples each (texture, sampler) pair of `samples` at v1.xy and sums the
// results into o0; `loadOnly` (>= 0) adds a texture read with `ld` alone (no sampler). Textures are 2D
// float4.
std::vector<uint8_t> PairsPixelBlob(const std::vector<std::pair<uint32_t, uint32_t>>& samples, int loadOnly) {
    constexpr uint32_t kImmediate4 = 2u | (4u << 12);   // l(x, y, z, w): immediate32, four components
    Tokens t;
    std::set<uint32_t> textures;
    std::set<uint32_t> samplers;
    for (const auto& p : samples) {
        textures.insert(p.first);
        samplers.insert(p.second);
    }
    if (loadOnly >= 0) textures.insert(static_cast<uint32_t>(loadOnly));
    for (uint32_t s : samplers)
        Raw(t, Op::DclSampler, {kSamplerOperand, s});
    for (uint32_t r : textures)
        Raw(t, Op::DclResource, {kResourceOperand, r, 0x5555}, 3u << 11);
    DclIo(t, kDclInputPs, OT::Input, 1, 0x3, 2u << 11);
    DclIo(t, kDclOutput, OT::Output, 0, 0xF);
    Raw(t, Op::DclTemps, {2});
    {
        Ins i(t);
        Dst(t, OT::Temp, 1, 0xF);
        t.push_back(kImmediate4);
        t.insert(t.end(), {0u, 0u, 0u, 0u});
        i.End(Op::Mov);
    }
    const auto addInto = [&]() {
        Ins i(t);
        Dst(t, OT::Temp, 1, 0xF);
        Src(t, OT::Temp, 1, kXYZW);
        Src(t, OT::Temp, 0, kXYZW);
        i.End(0);   // add
    };
    for (const auto& p : samples) {
        {
            Ins i(t);
            Dst(t, OT::Temp, 0, 0xF);
            Src(t, OT::Input, 1, Enc::Swizzle(0, 1, 0, 0));
            Src(t, OT::Resource, p.first, kXYZW);
            t.push_back(kSamplerOperand);
            t.push_back(p.second);
            i.End(Op::Sample);
        }
        addInto();
    }
    if (loadOnly >= 0) {
        {
            Ins i(t);
            Dst(t, OT::Temp, 0, 0xF);
            t.push_back(kImmediate4);
            t.insert(t.end(), {0u, 0u, 0u, 0u});
            Src(t, OT::Resource, static_cast<uint32_t>(loadOnly), kXYZW);
            i.End(45);   // ld
        }
        addInto();
    }
    {
        Ins i(t);
        Dst(t, OT::Output, 0, 0xF);
        Src(t, OT::Temp, 1, kXYZW);
        i.End(Op::Mov);
    }
    Raw(t, Op::Ret, {});
    return Container(0x40, t,
                     {{"SV_POSITION", 0, 1, 0, 0xF, 0x0}, {"TEXCOORD", 0, 0, 1, 0x3, 0x3}},
                     {{"SV_TARGET", 0, 64, 0, 0xF, 0x0}});
}

// The count of instructions with opcode `op` in the module.
size_t CountOp(const std::vector<uint32_t>& spirv, uint32_t op) {
    size_t n = 0;
    for (size_t at = 5; at < spirv.size();) {
        const uint32_t words = spirv[at] >> 16;
        if (words == 0) break;
        if ((spirv[at] & 0xFFFF) == op) ++n;
        at += words;
    }
    return n;
}

bool ConvertCombined(const std::vector<uint8_t>& blob, std::vector<uint32_t>* code, ShaderReflection* refl) {
    std::string why;
    ConvertOptions options;
    options.combineSamplers = true;
    const bool ok = ConvertDxbc(blob.data(), blob.size(), Stage::Pixel, code, refl, &why, options);
    if (!ok) std::printf("    convert: %s\n", why.c_str());
    return ok;
}

}  // namespace

UNIT_TEST(vulkan_combine_one_pair) {
    std::vector<uint32_t> code;
    ShaderReflection refl;
    const std::vector<uint8_t> blob = PixelBlob();
    CHECK(ConvertCombined(blob, &code, &refl));
    CHECK(refl.combined);
    CHECK_EQ(refl.textureMask, 1u);
    CHECK_EQ(refl.samplerMask, 1u);
    CHECK_EQ(static_cast<uint32_t>(refl.textureSampler[0]), 0u);
    CHECK(refl.extraPairs.empty());
    const auto bindings = Bindings(code);
    CHECK(bindings.count({kSetPixel, BindingOfConstantBuffer(1)}) == 1);
    CHECK(bindings.count({kSetPixel, BindingOfTexture(0)}) == 1);
    CHECK(bindings.count({kSetPixel, BindingOfSampler(0)}) == 0);   // no separate sampler is left
    CHECK_EQ(bindings.size(), (size_t)2);
    CHECK_EQ(CountOp(code, 86 /* OpSampledImage */), (size_t)0);
    std::string why;
    CHECK(CheckSpirvStructure(code, &why));
    // a combined conversion is another cache entry, and its reflection round-trips
    ConvertOptions options;
    options.combineSamplers = true;
    CHECK(CacheKey(blob.data(), blob.size(), Stage::Pixel).shape[0] !=
          CacheKey(blob.data(), blob.size(), Stage::Pixel, options).shape[0]);
    std::vector<uint32_t> back;
    ShaderReflection backRefl;
    CHECK(DecodeEntry(EncodeEntry(code, refl), &back, &backRefl));
    CHECK(back == code);
    CHECK(backRefl.combined);
    CHECK(backRefl.textureSampler == refl.textureSampler);
}

UNIT_TEST(vulkan_combine_texture_with_two_samplers) {
    // t0 with s0 and s1 (D3D allows it), t1 with s0: t0's primary pair (t0, s0) is at its texture
    // binding, (t0, s1) a further pair at BindingOfSampler(0), (t1, s0) at t1's texture binding.
    std::vector<uint32_t> code;
    ShaderReflection refl;
    CHECK(ConvertCombined(PairsPixelBlob({{0, 0}, {0, 1}, {1, 0}}, -1), &code, &refl));
    CHECK(refl.combined);
    CHECK_EQ(refl.textureMask, 3u);
    CHECK_EQ(static_cast<uint32_t>(refl.textureSampler[0]), 0u);
    CHECK_EQ(static_cast<uint32_t>(refl.textureSampler[1]), 0u);
    CHECK_EQ(refl.extraPairs.size(), (size_t)1);
    if (refl.extraPairs.size() == 1) {
        CHECK_EQ(static_cast<uint32_t>(refl.extraPairs[0].binding), BindingOfSampler(0));
        CHECK_EQ(static_cast<uint32_t>(refl.extraPairs[0].texture), 0u);
        CHECK_EQ(static_cast<uint32_t>(refl.extraPairs[0].sampler), 1u);
    }
    const auto bindings = Bindings(code);
    CHECK(bindings.count({kSetPixel, BindingOfTexture(0)}) == 1);
    CHECK(bindings.count({kSetPixel, BindingOfTexture(1)}) == 1);
    CHECK(bindings.count({kSetPixel, BindingOfSampler(0)}) == 1);
    CHECK(bindings.count({kSetPixel, BindingOfSampler(1)}) == 0);
    CHECK_EQ(bindings.size(), (size_t)3);
    CHECK_EQ(CountOp(code, 86 /* OpSampledImage */), (size_t)0);
    std::string why;
    CHECK(CheckSpirvStructure(code, &why));
}

// review S-014: a cache entry is only checked for its record and payload hashes, both unkeyed, so a
// forged entry under the right key reaches the draw code. Its reflection indexes 32-entry binding
// arrays and shifts 32-bit masks: an out-of-range field is a miss, never a decoded entry.
UNIT_TEST(vulkan_shader_cache_entry_rejects_out_of_range_reflection) {
    std::vector<uint32_t> code;
    ShaderReflection refl;
    CHECK(ConvertCombined(PairsPixelBlob({{0, 0}, {0, 1}, {1, 0}}, -1), &code, &refl));
    CHECK_EQ(refl.extraPairs.size(), (size_t)1);
    const std::vector<uint8_t> good = EncodeEntry(code, refl);
    std::vector<uint32_t> back;
    ShaderReflection backRefl;
    CHECK(DecodeEntry(good, &back, &backRefl));
    // the payload layout (EncodeEntry): magic, version, stage, the five masks (cb at 12, texture at
    // 16, sampler at 20, cube at 24, uav at 28), combined at 32, textureSampler[16] at 33, the pair
    // count at 49, then (binding, texture, sampler) per pair from 53
    const auto rejects = [&](size_t at, uint8_t value) {
        std::vector<uint8_t> bad = good;
        bad[at] = value;
        return !DecodeEntry(bad, &back, &backRefl);
    };
    CHECK(rejects(53, 0xFF));                                           // binding past the sampler range
    CHECK(rejects(53, static_cast<uint8_t>(BindingOfSampler(0) - 1)));  // binding below it
    CHECK(rejects(54, 0xFF));                                           // texture
    CHECK(rejects(55, kMaxSamplers));                                   // sampler
    CHECK(rejects(33, kMaxSamplers));                                   // textureSampler[0]
    CHECK(rejects(12 + 1, 0x01));                                       // constant buffer b8
    CHECK(rejects(16 + 2, 0x01));                                       // texture t16
    CHECK(rejects(20 + 2, 0x01));                                       // sampler s16
    CHECK(rejects(24 + 2, 0x01));                                       // cube texture t16
    CHECK(!rejects(33 + 15, kNoSampler));                               // no sampler stays valid
}

UNIT_TEST(vulkan_combine_texture_read_without_sampler) {
    // t2 is read with ld only: still combined (no sampler register), its image taken with OpImage.
    std::vector<uint32_t> code;
    ShaderReflection refl;
    CHECK(ConvertCombined(PairsPixelBlob({{0, 0}}, 2), &code, &refl));
    CHECK_EQ(refl.textureMask, 5u);
    CHECK_EQ(static_cast<uint32_t>(refl.textureSampler[0]), 0u);
    CHECK_EQ(static_cast<uint32_t>(refl.textureSampler[2]), static_cast<uint32_t>(kNoSampler));
    const auto bindings = Bindings(code);
    CHECK(bindings.count({kSetPixel, BindingOfTexture(2)}) == 1);
    CHECK(CountOp(code, 100 /* OpImage */) >= 1);
    std::string why;
    CHECK(CheckSpirvStructure(code, &why));
}
