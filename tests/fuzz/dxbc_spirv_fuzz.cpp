// ============================================================================
// tests/fuzz/dxbc_spirv_fuzz.cpp -- the Vulkan backend's DXBC -> SPIR-V conversion over arbitrary bytes
// ============================================================================
// P0_MAP input surface rank 5: ConvertDxbc (src/render/vulkan/shader_convert.cpp) over a program blob
// of the user's packages or a mod .pkm, the dxbc-spirv front end and IR passes it runs, the word-level
// passes after it (LowerBitFieldInsert, CombineImageSamplers), then what the device does with the
// module on the Vulkan 1.1 path (DowngradeSpirv, WidenInputs) and the cache entry round trip
// (EncodeEntry -> DecodeEntry).
//
// Build: the dxbc-spirv library is not in the prebuilt fuzz archive instrumented, so this target is
// built by the scratchpad helper fzx.sh (dxbc-spirv compiled with the archive's sanitizer flags plus
// NDEBUG, as every non-debug flavour ships it) together with shader_convert.cpp, spirv_legalize.cpp and
// spirv_combine.cpp.
//
// Input: byte 0 is a flag byte, the rest is the blob.
//   bit 0  ConvertOptions::combineSamplers (every game program sets it)
//   bit 1  a linked pixel conversion (ConvertOptions::previousStage = a fixed vertex reflection)
//   bit 2  WRAP: the rest is the body of the SHDR / SHEX chunk; the target puts it in a container
//          with an ISGN / OSGN pair picked by bits 3..4 (our own DXBC writer, better_trees/dxbc.h),
//          so the mutator works on the token stream instead of the container's offsets
//   bit 5  WRAP only: the chunk is SHEX instead of SHDR
//   bit 6  WRAP only: patch the program's length token (dword 1) to the chunk's dword count
//   bit 7  convert as the OTHER stage than the declared one too (the stage-mismatch arm)
// No retail DXBC: seeds are our own programs (src/render/common/ao/shaders, better_water, the unit
// test's synthetic programs), made by the FUZZ_SEEDGEN main below.
// ============================================================================
#include "better_trees/dxbc.h"
#include "ext/derived_cache/derived_cache.h"
#include "render/vulkan/shader_convert.h"
#include "render/vulkan/spirv_legalize.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace VideoVulkan::Shaders;

namespace {

// ISGN / OSGN: count, 8, then 24-byte elements {name offset, semantic index, system value, component
// type, register, mask, read-write mask, 2 pad}, then the names (tests/unit/vulkan_shader_convert_test).
struct Element {
    const char* name;
    uint32_t    index;
    uint32_t    systemValue;
    uint32_t    componentType;
    uint32_t    reg;
    uint8_t     mask;
    uint8_t     rw;
};

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
        w32(e.componentType);
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

// Four signature pairs: the unit test's vertex and pixel programs, an integer-input one, and empty.
void SignaturePair(uint32_t which, std::vector<Element>* in, std::vector<Element>* out) {
    switch (which & 3u) {
    case 0:
        *in = {{"POSITION", 0, 0, 3, 0, 0xF, 0xF}, {"TEXCOORD", 0, 0, 3, 1, 0x3, 0x3}};
        *out = {{"SV_POSITION", 0, 1, 3, 0, 0xF, 0x0}, {"COLOR", 0, 0, 3, 1, 0xF, 0x0},
                {"TEXCOORD", 0, 0, 3, 2, 0x3, 0x0}};
        break;
    case 1:
        *in = {{"SV_POSITION", 0, 1, 3, 0, 0xF, 0x0}, {"TEXCOORD", 0, 0, 3, 1, 0x3, 0x3}};
        *out = {{"SV_TARGET", 0, 64, 3, 0, 0xF, 0x0}};
        break;
    case 2:
        *in = {{"BLENDINDICES", 0, 0, 1, 0, 0xF, 0xF}, {"SV_VertexID", 0, 6, 1, 1, 0x1, 0x1},
               {"TEXCOORD", 3, 0, 2, 2, 0x7, 0x7}};
        *out = {{"SV_POSITION", 0, 1, 3, 0, 0xF, 0x0}, {"TEXCOORD", 3, 0, 1, 1, 0xF, 0x0},
                {"SV_TARGET", 1, 64, 3, 2, 0xF, 0x0}};
        break;
    default:
        in->clear();
        out->clear();
        break;
    }
}

std::vector<uint8_t> Wrap(uint8_t flags, const uint8_t* body, size_t size) {
    std::vector<Element> in;
    std::vector<Element> out;
    SignaturePair(flags >> 3, &in, &out);
    Dxbc::Chunk isgn;
    isgn.fourcc = Dxbc::FourCC('I', 'S', 'G', 'N');
    isgn.data = Signature(in);
    Dxbc::Chunk osgn;
    osgn.fourcc = Dxbc::FourCC('O', 'S', 'G', 'N');
    osgn.data = Signature(out);
    Dxbc::Chunk code;
    code.fourcc = (flags & 0x20) ? Dxbc::FourCC('S', 'H', 'E', 'X') : Dxbc::FourCC('S', 'H', 'D', 'R');
    code.data.assign(body, body + size);
    if ((flags & 0x40) && code.data.size() >= 8) {
        const uint32_t dwords = static_cast<uint32_t>(code.data.size() / 4);
        std::memcpy(code.data.data() + 4, &dwords, 4);
    }
    Dxbc::Container c;
    c.chunks = {isgn, osgn, code};
    return Dxbc::Emit(c);
}

// The vertex reflection a linked pixel conversion is given: the unit test's vertex program's outputs.
const ShaderReflection& PreviousStage() {
    static const ShaderReflection vs = [] {
        ShaderReflection r;
        r.stage = Stage::Vertex;
        IoElement pos;
        pos.semantic = "SV_POSITION";
        pos.location = 0;
        pos.componentMask = 0xF;
        pos.builtIn = 0;   // dxbc-spirv ir::BuiltIn::ePosition
        r.outputs.push_back(pos);
        IoElement color;
        color.semantic = "COLOR";
        color.location = 1;
        color.componentMask = 0xF;
        r.outputs.push_back(color);
        IoElement tex;
        tex.semantic = "TEXCOORD";
        tex.location = 2;
        tex.componentMask = 0x3;
        r.outputs.push_back(tex);
        return r;
    }();
    return vs;
}

void Convert(const std::vector<uint8_t>& blob, Stage stage, uint8_t flags) {
    ConvertOptions options;
    options.combineSamplers = (flags & 1) != 0;
    if ((flags & 2) && stage == Stage::Pixel) options.previousStage = &PreviousStage();
    std::vector<uint32_t> spirv;
    ShaderReflection refl;
    std::string why;
    (void)CacheKey(blob.data(), blob.size(), stage, options);
    if (!ConvertDxbc(blob.data(), blob.size(), stage, &spirv, &refl, &why, options)) return;

    // The cache entry round trip: our own encoding must decode to the same module.
    const std::vector<uint8_t> entry = EncodeEntry(spirv, refl);
    std::vector<uint32_t> decoded;
    ShaderReflection decodedRefl;
    if (!DecodeEntry(entry, &decoded, &decodedRefl) || decoded != spirv) {
        std::fprintf(stderr, "DecodeEntry rejected or changed our own EncodeEntry payload\n");
        __builtin_trap();
    }
    (void)StagesCompatible(PreviousStage(), refl);

    // The device's Vulkan 1.1 path: the 1.3 downgrade of every module, the widened pixel inputs.
    std::vector<uint32_t> downgraded = spirv;
    if (DowngradeSpirv(&downgraded, &why) && !CheckSpirvStructure(downgraded, &why)) {
        std::fprintf(stderr, "DowngradeSpirv produced a malformed module: %s\n", why.c_str());
        __builtin_trap();
    }
    if (stage == Stage::Pixel) {
        uint32_t widths[32];
        for (uint32_t i = 0; i < 32; ++i)
            widths[i] = 4;
        uint32_t changed = 0;
        std::vector<uint32_t> widened = spirv;
        if (WidenInputs(&widened, widths, &changed, &why) && !CheckSpirvStructure(widened, &why)) {
            std::fprintf(stderr, "WidenInputs produced a malformed module: %s\n", why.c_str());
            __builtin_trap();
        }
    }
}

}  // namespace

#ifndef FUZZ_SEEDGEN
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size < 2 || size > 0x10000)
        return 0;
    const uint8_t flags = data[0];
    std::vector<uint8_t> blob;
    if (flags & 4)
        blob = Wrap(flags, data + 1, size - 1);
    else
        blob.assign(data + 1, data + size);   // heap buffer of exactly the blob: ASan sees an over-read

#ifndef FUZZ_NO_MASK   // -DFUZZ_NO_MASK: the proof build, every input reaches the converter
    // MASK (finding: dxbc-spirv RegisterFile::getOrDeclareTemp, dxbc_registers.cpp:461, resizes its
    // temp table to 4 * the r# index of any operand, uncapped and unchecked against dcl_temps: an r#
    // index of 2^30 asks for 16 GB). Recorded once; inputs holding a dword that looks like a 1-D
    // immediate-indexed temp operand token followed by an index > 4096 are skipped, so the campaign
    // reaches past it. Over-rejects a few unrelated dword pairs, which is harmless.
    for (size_t at = 0; at + 8 <= blob.size(); at += 4) {
        uint32_t token;
        uint32_t index;
        std::memcpy(&token, blob.data() + at, 4);
        std::memcpy(&index, blob.data() + at + 4, 4);
        if ((token & 0x01FFF000u) == 0x00100000u && index > 4096) return 0;
    }
#endif

    Stage declared = Stage::Vertex;
    std::string why;
    const bool known = StageOfDxbc(blob.data(), blob.size(), &declared, &why);
    if (!known) {
        // The rejection arm of ConvertDxbc (it calls StageOfDxbc first).
        std::vector<uint32_t> spirv;
        ShaderReflection refl;
        (void)ConvertDxbc(blob.data(), blob.size(), Stage::Pixel, &spirv, &refl, &why);
        return 0;
    }
    Convert(blob, declared, flags);
    if (flags & 0x80) Convert(blob, declared == Stage::Pixel ? Stage::Vertex : Stage::Pixel, flags);
    return 0;
}
#else
// ---- FUZZ_SEEDGEN: the corpus, from our own programs only ------------------------------------------
// The ext programs compiled from OUR HLSL (ASSAO, better_water) and the unit test's two synthetic SM4
// programs, assembled token by token here as tests/unit/vulkan_shader_convert_test.cpp does.
#include "render/common/ao/shaders/PSApply.h"
#include "render/common/ao/shaders/PSGenerateQ0.h"
#include "render/common/ao/shaders/PSGenerateQ1.h"
#include "render/common/ao/shaders/PSGenerateQ2.h"
#include "render/common/ao/shaders/PSNonSmartApply.h"
#include "render/common/ao/shaders/PSNonSmartBlur.h"
#include "render/common/ao/shaders/PSNonSmartHalfApply.h"
#include "render/common/ao/shaders/PSPrepareDepthMip1.h"
#include "render/common/ao/shaders/PSPrepareDepthMip2.h"
#include "render/common/ao/shaders/PSPrepareDepthMip3.h"
#include "render/common/ao/shaders/PSPrepareDepths.h"
#include "render/common/ao/shaders/PSPrepareDepthsAndNormals.h"
#include "render/common/ao/shaders/PSPrepareDepthsAndNormalsHalf.h"
#include "render/common/ao/shaders/PSPrepareDepthsHalf.h"
#include "render/common/ao/shaders/PSResolveDepth1x.h"
#include "render/common/ao/shaders/PSResolveDepthMS.h"
#include "render/common/ao/shaders/PSSmartBlur.h"
#include "render/common/ao/shaders/PSSmartBlurWide.h"
#include "render/common/ao/shaders/VSFullscreen.h"
#include "render/common/ao/shaders/VSMain.h"
#include "render/common/better_water/shore_contact_lake_ps.h"
#include "render/common/better_water/shore_contact_river_ps.h"
#include "render/common/better_water/shore_overlay_lake_ps.h"
#include "render/common/better_water/shore_overlay_river_ps.h"
#include "render/common/better_water/shore_strip_ps.h"

namespace {

namespace Op = Dxbc::Op;
namespace OT = Dxbc::OperandType;
namespace Enc = Dxbc::Enc;
using Tokens = std::vector<uint32_t>;

constexpr uint32_t kXYZW = Enc::Swizzle(0, 1, 2, 3);

uint32_t Comp4(uint32_t sel, uint32_t bits, uint32_t type, uint32_t dims) {
    return 2u | (sel << 2) | (bits << 4) | (type << 12) | (dims << 20);
}

void Instr(Tokens& t, uint32_t op, const Tokens& operands, uint32_t extra = 0) {
    t.push_back(Enc::Opcode(op, static_cast<uint32_t>(operands.size() + 1), false, extra));
    t.insert(t.end(), operands.begin(), operands.end());
}

Tokens Dst(uint32_t type, uint32_t reg, uint32_t mask) {
    return {Comp4(0, mask, type, 1), reg};
}
Tokens Src(uint32_t type, uint32_t reg, uint32_t swz) {
    return {Comp4(1, swz, type, 1), reg};
}
Tokens Cb(uint32_t cb, uint32_t row) {
    return {Comp4(1, kXYZW, OT::ConstantBuffer, 2), cb, row};
}
Tokens Cat(std::initializer_list<Tokens> parts) {
    Tokens out;
    for (const Tokens& p : parts)
        out.insert(out.end(), p.begin(), p.end());
    return out;
}

Tokens VertexTokens() {
    Tokens t;
    Instr(t, Op::DclConstantBuffer, Cb(0, 4));
    Instr(t, 95, Dst(OT::Input, 0, 0xF));
    Instr(t, 95, Dst(OT::Input, 1, 0x3));
    Instr(t, 103, {Comp4(0, 0xF, OT::Output, 1), 0, 1});
    Instr(t, 101, Dst(OT::Output, 1, 0xF));
    Instr(t, 101, Dst(OT::Output, 2, 0x3));
    Instr(t, Op::DclTemps, {1});
    Instr(t, Op::Mul, Cat({Dst(OT::Temp, 0, 0xF), Src(OT::Input, 0, Enc::Rep(1)), Cb(0, 1)}));
    Instr(t, Op::Mad, Cat({Dst(OT::Temp, 0, 0xF), Cb(0, 0), Src(OT::Input, 0, Enc::Rep(0)), Src(OT::Temp, 0, kXYZW)}));
    Instr(t, Op::Mad, Cat({Dst(OT::Temp, 0, 0xF), Cb(0, 2), Src(OT::Input, 0, Enc::Rep(2)), Src(OT::Temp, 0, kXYZW)}));
    Instr(t, Op::Mad, Cat({Dst(OT::Output, 0, 0xF), Cb(0, 3), Src(OT::Input, 0, Enc::Rep(3)), Src(OT::Temp, 0, kXYZW)}));
    Instr(t, Op::Mov, Cat({Dst(OT::Output, 1, 0xF), Cb(0, 0)}));
    Instr(t, Op::Mov, Cat({Dst(OT::Output, 2, 0x3), Src(OT::Input, 1, Enc::Swizzle(0, 1, 0, 0))}));
    Instr(t, Op::Ret, {});
    return t;
}

Tokens PixelTokens(bool twoSamplers) {
    constexpr uint32_t kSampler = (OT::Sampler << 12) | (1u << 20);
    constexpr uint32_t kResource = (OT::Resource << 12) | (1u << 20);
    Tokens t;
    Instr(t, Op::DclSampler, {kSampler, 0});
    if (twoSamplers) Instr(t, Op::DclSampler, {kSampler, 1});
    Instr(t, Op::DclResource, {kResource, 0, 0x5555}, 3u << 11);
    Instr(t, Op::DclConstantBuffer, Cb(1, 1));
    Instr(t, 98, Dst(OT::Input, 1, 0x3), 2u << 11);
    Instr(t, 101, Dst(OT::Output, 0, 0xF));
    Instr(t, Op::DclTemps, {2});
    Instr(t, Op::Sample, Cat({Dst(OT::Temp, 0, 0xF), Src(OT::Input, 1, Enc::Swizzle(0, 1, 0, 0)),
                              Src(OT::Resource, 0, kXYZW), Tokens{kSampler, 0}}));
    if (twoSamplers) {
        Instr(t, Op::Sample, Cat({Dst(OT::Temp, 1, 0xF), Src(OT::Input, 1, Enc::Swizzle(0, 1, 0, 0)),
                                  Src(OT::Resource, 0, kXYZW), Tokens{kSampler, 1}}));
        Instr(t, Op::Mul, Cat({Dst(OT::Temp, 0, 0xF), Src(OT::Temp, 0, kXYZW), Src(OT::Temp, 1, kXYZW)}));
    }
    Instr(t, Op::Mul, Cat({Dst(OT::Output, 0, 0xF), Src(OT::Temp, 0, kXYZW), Cb(1, 0)}));
    Instr(t, Op::Ret, {});
    return t;
}

std::vector<uint8_t> Body(uint32_t version, const Tokens& tokens) {
    Dxbc::Program p;
    p.version = version;
    p.tokens = tokens;
    return Dxbc::EmitProgram(p);
}

int g_seed = 0;
void Write(const char* dir, uint8_t flags, const std::vector<uint8_t>& blob) {
    char path[512];
    std::snprintf(path, sizeof(path), "%s/seed_%03d_%02x", dir, g_seed++, flags);
    FILE* f = std::fopen(path, "wb");
    if (!f) return;
    std::fputc(flags, f);
    std::fwrite(blob.data(), 1, blob.size(), f);
    std::fclose(f);
}

// A raw container in three option sets, and its code chunk body in wrap mode with signature pair `sig`.
void Emit(const char* dir, const std::vector<uint8_t>& blob, uint32_t sig) {
    Write(dir, 0x00, blob);
    Write(dir, 0x01, blob);
    Write(dir, 0x03, blob);
    Dxbc::Container c;
    std::string err;
    if (!Dxbc::Parse(blob.data(), blob.size(), &c, &err)) return;
    for (const Dxbc::Chunk& ch : c.chunks) {
        const bool shex = ch.fourcc == Dxbc::FourCC('S', 'H', 'E', 'X');
        if (!shex && ch.fourcc != Dxbc::FourCC('S', 'H', 'D', 'R')) continue;
        const uint8_t flags = static_cast<uint8_t>(0x05 | (sig << 3) | (shex ? 0x20 : 0) | 0x40);
        Write(dir, flags, ch.data);
    }
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) return 1;
    const char* dir = argv[1];
    const struct {
        const uint8_t* data;
        size_t size;
        uint32_t sig;
    } ours[] = {
        {g_ao_PSApply, sizeof(g_ao_PSApply), 1},
        {g_ao_PSGenerateQ0, sizeof(g_ao_PSGenerateQ0), 1},
        {g_ao_PSGenerateQ1, sizeof(g_ao_PSGenerateQ1), 1},
        {g_ao_PSGenerateQ2, sizeof(g_ao_PSGenerateQ2), 1},
        {g_ao_PSNonSmartApply, sizeof(g_ao_PSNonSmartApply), 1},
        {g_ao_PSNonSmartBlur, sizeof(g_ao_PSNonSmartBlur), 1},
        {g_ao_PSNonSmartHalfApply, sizeof(g_ao_PSNonSmartHalfApply), 1},
        {g_ao_PSPrepareDepthMip1, sizeof(g_ao_PSPrepareDepthMip1), 1},
        {g_ao_PSPrepareDepthMip2, sizeof(g_ao_PSPrepareDepthMip2), 1},
        {g_ao_PSPrepareDepthMip3, sizeof(g_ao_PSPrepareDepthMip3), 1},
        {g_ao_PSPrepareDepths, sizeof(g_ao_PSPrepareDepths), 1},
        {g_ao_PSPrepareDepthsAndNormals, sizeof(g_ao_PSPrepareDepthsAndNormals), 1},
        {g_ao_PSPrepareDepthsAndNormalsHalf, sizeof(g_ao_PSPrepareDepthsAndNormalsHalf), 1},
        {g_ao_PSPrepareDepthsHalf, sizeof(g_ao_PSPrepareDepthsHalf), 1},
        {g_ao_PSResolveDepth1x, sizeof(g_ao_PSResolveDepth1x), 1},
        {g_ao_PSResolveDepthMS, sizeof(g_ao_PSResolveDepthMS), 1},
        {g_ao_PSSmartBlur, sizeof(g_ao_PSSmartBlur), 1},
        {g_ao_PSSmartBlurWide, sizeof(g_ao_PSSmartBlurWide), 1},
        {g_ao_VSFullscreen, sizeof(g_ao_VSFullscreen), 0},
        {g_ao_VSMain, sizeof(g_ao_VSMain), 0},
        {g_bw_PSContactLake, sizeof(g_bw_PSContactLake), 1},
        {g_bw_PSContactRiver, sizeof(g_bw_PSContactRiver), 1},
        {g_bw_PSShoreLake, sizeof(g_bw_PSShoreLake), 1},
        {g_bw_PSShoreRiver, sizeof(g_bw_PSShoreRiver), 1},
        {g_bw_PSShoreStrip, sizeof(g_bw_PSShoreStrip), 1},
    };
    for (const auto& o : ours)
        Emit(dir, std::vector<uint8_t>(o.data, o.data + o.size), o.sig);

    // The unit test's programs, in their containers (Wrap with flags 0 / 1 builds exactly those).
    const std::vector<uint8_t> vs = Body(0x10040, VertexTokens());
    const std::vector<uint8_t> ps = Body(0x40, PixelTokens(false));
    const std::vector<uint8_t> ps2 = Body(0x40, PixelTokens(true));
    Emit(dir, Wrap(0x04 | (0 << 3), vs.data(), vs.size()), 0);
    Emit(dir, Wrap(0x04 | (1 << 3), ps.data(), ps.size()), 1);
    Emit(dir, Wrap(0x04 | (1 << 3), ps2.data(), ps2.size()), 1);
    Write(dir, 0x83, Wrap(0x04 | (1 << 3), ps2.data(), ps2.size()));
    std::printf("%d seeds in %s\n", g_seed, dir);
    return 0;
}
#endif
