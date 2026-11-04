// ============================================================================
// tests/unit/vulkan_malformed_input_test.cpp -- malformed DXBC / SPIR-V the Vulkan converter refuses
// ============================================================================
// BUILT ONLY WITH THE VULKAN BACKEND (BANISHED_RENDER_BACKEND=vulkan), like every vulkan_*_test.cpp.
// The inputs are the fuzz findings of the 2026-11-03 review (docs/history/review_20261103), carried
// inline as words:
//   F-029  a temp register index that wraps dxbc-spirv's temp table (r1073741823.w: 4 * index + 3 =
//          0xFFFFFFFF, resize(0), then m_rRegs[0xFFFFFFFF]) -- ConvertDxbc refuses r# >= 4096
//   F-030  dtou writing 4 components from a 64-bit source: the doubled read mask has 8 bits and
//          ir::composite writes past its std::array<SsaDef, 4> -- ConvertDxbc refuses > 2 components
//   F-089  a 1-word OpTypeSampler, F-090 a 1-word OpEntryPoint, F-091 a 1-word OpLoad, each the last
//          instruction of the module: CombineImageSamplers read their operands past the module's end
//          -- refused as short instructions
// NO RETAIL BYTES: the DXBC programs are the fuzz target's own synthetic seeds (tests/fuzz/
// dxbc_spirv_fuzz.cpp, the unit test's vertex program) with one operand changed.
// ============================================================================
#include "unit.h"

#include "better_trees/dxbc.h"
#include "render/vulkan/shader_convert.h"
#include "render/vulkan/spirv_combine.h"

#include <cstring>
#include <string>
#include <vector>

using namespace VideoVulkan::Shaders;

namespace {

// ISGN / OSGN: count, 8, then 24-byte elements {name offset, semantic index, system value, component
// type, register, mask, read-write mask, 2 pad}, then the names (the fuzz target's signature pair 0).
struct Element {
    const char* name;
    uint32_t    index;
    uint32_t    systemValue;
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

// The fuzz target's WRAP mode with flags 0x45: the SHDR body (version, length, tokens) in a container
// with signature pair 0.
std::vector<uint8_t> Wrap(const std::vector<uint32_t>& body) {
    Dxbc::Chunk isgn;
    isgn.fourcc = Dxbc::FourCC('I', 'S', 'G', 'N');
    isgn.data = Signature({{"POSITION", 0, 0, 0, 0xF, 0xF}, {"TEXCOORD", 0, 0, 1, 0x3, 0x3}});
    Dxbc::Chunk osgn;
    osgn.fourcc = Dxbc::FourCC('O', 'S', 'G', 'N');
    osgn.data = Signature({{"SV_POSITION", 0, 1, 0, 0xF, 0x0}, {"COLOR", 0, 0, 1, 0xF, 0x0},
                           {"TEXCOORD", 0, 0, 2, 0x3, 0x0}});
    Dxbc::Chunk code;
    code.fourcc = Dxbc::FourCC('S', 'H', 'D', 'R');
    code.data.resize(body.size() * 4);
    std::memcpy(code.data.data(), body.data(), code.data.size());
    Dxbc::Container c;
    c.chunks = {isgn, osgn, code};
    return Dxbc::Emit(c);
}

// scratchpad/crashes/dxbc_spirv/getOrDeclareTemp_wrap_oob (297 bytes: flag 0x45 + this body): the
// unit test's vs_4_0 with the first mul's dst "r0.xyzw" (0x001000F2, 0) made "r1073741823.w"
// (0x00100082, 0x3FFFFFFF), at words 25 and 26.
const std::vector<uint32_t> kTempWrapBody = {
    0x00010040, 0x0000004a, 0x04000059, 0x00208e46, 0x00000000, 0x00000004, 0x0300005f, 0x001010f2,
    0x00000000, 0x0300005f, 0x00101032, 0x00000001, 0x04000067, 0x001020f2, 0x00000000, 0x00000001,
    0x03000065, 0x001020f2, 0x00000001, 0x03000065, 0x00102032, 0x00000002, 0x02000068, 0x00000001,
    0x08000038, 0x00100082, 0x3fffffff, 0x00101556, 0x00000000, 0x00208e46, 0x00000000, 0x00000001,
    0x0a000032, 0x001000f2, 0x00000000, 0x00208e46, 0x00000000, 0x00000000, 0x00101006, 0x00000000,
    0x00100e46, 0x00000000, 0x0a000032, 0x001000f2, 0x00000000, 0x00208e46, 0x00000000, 0x00000002,
    0x00101aa6, 0x00000000, 0x00100e46, 0x00000000, 0x0a000032, 0x001020f2, 0x00000000, 0x00208e46,
    0x00000000, 0x00000003, 0x00101ff6, 0x00000000, 0x00100e46, 0x00000000, 0x06000036, 0x001020f2,
    0x00000001, 0x00208e46, 0x00000000, 0x00000000, 0x05000036, 0x00102032, 0x00000002, 0x00101046,
    0x00000001, 0x0100003e,
};
constexpr size_t kTempWrapIndexWord = 26;

// scratchpad/crashes/dxbc_spirv/composite_stack_overflow_write (25 bytes: flag 0x45 + this body, the
// length word patched to the chunk's 6 dwords as flag bit 6 does): ps_4_0, "dtou r0.xyzw, v.xy".
const std::vector<uint32_t> kDtouBody = {
    0x00000040, 0x00000006, 0x040000d7, 0x001000f2, 0x00000000, 0x00001046,
};

bool Convert(const std::vector<uint8_t>& blob, Stage stage, std::string* why) {
    std::vector<uint32_t> spirv;
    ShaderReflection refl;
    ConvertOptions options;
    options.combineSamplers = true;
    return ConvertDxbc(blob.data(), blob.size(), stage, &spirv, &refl, why, options);
}

bool Has(const std::string& text, const char* part) {
    return text.find(part) != std::string::npos;
}

}  // namespace

UNIT_TEST(vulkan_malformed_temp_index_refused) {
    // F-029: r1073741823.w wraps dxbc-spirv's temp table index (heap corruption 16 GB past it).
    std::string why;
    CHECK(!Convert(Wrap(kTempWrapBody), Stage::Vertex, &why));
    CHECK(Has(why, "temp register r1073741823"));

    // r4096 (the uncapped shape: 4 * index entries) is past D3D11's limit too; r4095 is the last one
    // allowed and converts, although the program declares one temp (dxbc-spirv ignores dcl_temps on
    // purpose, as DXVK does).
    std::vector<uint32_t> body = kTempWrapBody;
    body[kTempWrapIndexWord] = 4096;
    why.clear();
    CHECK(!Convert(Wrap(body), Stage::Vertex, &why));
    CHECK(Has(why, "temp register r4096"));
    body[kTempWrapIndexWord] = 4095;
    why.clear();
    CHECK(Convert(Wrap(body), Stage::Vertex, &why));
    CHECK(why.empty());
}

UNIT_TEST(vulkan_malformed_double_mask_refused) {
    // F-030: dtou r0.xyzw reads 8 components out of 4 doubles; ir::composite's 4-entry array overflows.
    std::string why;
    CHECK(!Convert(Wrap(kDtouBody), Stage::Pixel, &why));
    CHECK(Has(why, "64-bit source"));
}

UNIT_TEST(vulkan_malformed_combine_short_instructions) {
    // F-089: header + a 1-word OpTypeSampler (scratchpad/crashes/spirv_passes/combine_typesampler_oob).
    {
        std::vector<uint32_t> m = {0x07230203, 0x00010600, 0x00000000, 0x00000001, 0x00000000, 0x0001001a};
        ShaderReflection refl;
        std::string why;
        CHECK(!CombineImageSamplers(&m, 0, &refl, &why));
        CHECK(Has(why, "short instruction (opcode 26)"));
    }
    // F-090: a texture variable of set 1, then a 1-word OpEntryPoint as the last instruction
    // (combine_entrypoint_oob).
    {
        std::vector<uint32_t> m = {
            0x07230203, 0x00010600, 0x00000000, 0x0000003a, 0x00000000, 0x00040047, 0x00000008, 0x00000022,
            0x00000001, 0x00040047, 0x00000008, 0x00000021, 0x00000008, 0x00090019, 0x00000006, 0x00370005,
            0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000001, 0x00000000, 0x00040020, 0x00000007,
            0x00000000, 0x00000006, 0x0004003b, 0x00000007, 0x00000008, 0x00000005, 0x00020036, 0x00000019,
            0x0001000f,
        };
        ShaderReflection refl;
        std::string why;
        CHECK(!CombineImageSamplers(&m, 1, &refl, &why));
        CHECK(Has(why, "short instruction (opcode 15)"));
    }
    // F-091: a texture load, then a 1-word OpLoad as the last instruction (combine_opload_oob).
    {
        std::vector<uint32_t> m = {
            0x07230203, 0x00010600, 0x00000000, 0x0000003a, 0x00000000, 0x00040047, 0x00000008, 0x00000022,
            0x00000001, 0x00040047, 0x00000008, 0x00000021, 0x00000008, 0x00090019, 0x00000006, 0x00000005,
            0x00000001, 0x00205100, 0x00c0e902, 0x00000000, 0x00000001, 0x00000000, 0x00040020, 0x00000007,
            0x00000000, 0x00000006, 0x0004003b, 0x00000007, 0x00000008, 0x00000000, 0x00050036, 0x00000017,
            0x00000001, 0x00000000, 0x00000018, 0x0004003d, 0x00000006, 0x0000001a, 0x00000008, 0x0001003d,
        };
        ShaderReflection refl;
        std::string why;
        CHECK(!CombineImageSamplers(&m, 1, &refl, &why));
        CHECK(Has(why, "short instruction (opcode 61)"));
    }
}
