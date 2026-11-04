// ============================================================================
// src/render/vulkan/shader_convert.cpp -- DXBC to SPIR-V through dxbc-spirv (shader_convert.h)
// ============================================================================
// NOT RETAIL. shader_convert.h has the contract, the pipeline and the binding layout. This file
// needs no Vulkan header and no device; the cache's Store calls live in shader_cache.cpp, so the
// headless check (tools/vulkan/shader_check.cpp) links this file alone.
// ============================================================================
#include "shader_convert.h"
#include "spirv_combine.h"
#include "spirv_legalize.h"

#include "ext/derived_cache/derived_cache.h"
#include "ext/derived_cache/hash64.h"

#include "dxbc/dxbc_container.h"
#include "dxbc/dxbc_converter.h"
#include "dxbc/dxbc_parser.h"
#include "ir/ir.h"
#include "ir/ir_builder.h"
#include "ir/ir_legalize.h"
#include "ir/passes/ir_pass_lower_io.h"
#include "spirv/spirv_builder.h"
#include "spirv/spirv_mapping.h"
#include "util/util_bit.h"
#include "util/util_log.h"

#include <cstring>
#include <string>
#include <utility>
#include <vector>

#ifndef BANISHED_DXBC_SPIRV_COMMIT
#define BANISHED_DXBC_SPIRV_COMMIT "unknown"
#endif

namespace VideoVulkan {
namespace Shaders {

namespace ir   = dxbc_spv::ir;
namespace dxbc = dxbc_spv::dxbc;
namespace spv  = dxbc_spv::spirv;

namespace {

constexpr uint32_t kDxbcMagic  = 0x43425844u;   // "DXBC"
constexpr uint32_t kSpirvMagic = 0x07230203u;
constexpr uint32_t kEntryMagic = 0x5653424bu;   // "KBSV": the cache payload

uint32_t ReadU32(const uint8_t* p) {
    uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}

// dxbc-spirv logs through a per-thread logger; this one keeps the errors (and the warnings, for
// context) as the message a failed conversion returns. One per conversion, on the calling thread.
class CaptureLogger final : public dxbc_spv::util::Logger {
public:
    void message(dxbc_spv::util::LogLevel severity, const char* text) override {
        if (severity < dxbc_spv::util::LogLevel::eWarn) return;
        if (!messages.empty()) messages += "; ";
        messages += text;
    }
    dxbc_spv::util::LogLevel getMinimumSeverity() override {
        return dxbc_spv::util::LogLevel::eWarn;
    }
    std::string messages;
};

void SetWhy(std::string* why, const std::string& text) {
    if (why) *why = text;
}

// PLAN.md §4.3 as a dxbc-spirv ResourceMapping: the stage picks the set, the register class and
// number the binding. A register outside the layout, a register space other than 0, or a UAV counter
// (nothing here uses one) is recorded and fails the conversion afterwards; the binding handed back for
// it is a harmless placeholder that is never used. A UAV (P4: the AO programs' u4) maps past the game
// layout (shader_convert.h BindingOfUav).
class LayoutMapping final : public spv::ResourceMapping {
public:
    explicit LayoutMapping(Stage stage) : _stage(stage) {}

    spv::DescriptorBinding mapDescriptor(ir::ScalarType type, uint32_t regSpace, uint32_t regIndex) override {
        spv::DescriptorBinding out;
        out.set = SetOfStage(_stage);
        if (regSpace != 0) {
            Fail("register space " + std::to_string(regSpace));
            return out;
        }
        if (type == ir::ScalarType::eCbv) {
            if (regIndex >= kMaxConstantBuffers) Fail("constant buffer b" + std::to_string(regIndex));
            out.binding = BindingOfConstantBuffer(regIndex);
        } else if (type == ir::ScalarType::eSrv) {
            if (regIndex >= kMaxTextures) Fail("texture t" + std::to_string(regIndex));
            out.binding = BindingOfTexture(regIndex);
        } else if (type == ir::ScalarType::eSampler) {
            if (regIndex >= kMaxSamplers) Fail("sampler s" + std::to_string(regIndex));
            out.binding = BindingOfSampler(regIndex);
        } else if (type == ir::ScalarType::eUav) {
            if (regIndex >= kMaxUavs) Fail("UAV u" + std::to_string(regIndex));
            out.binding = BindingOfUav(regIndex);
        } else {
            Fail("a UAV counter (u" + std::to_string(regIndex) + ")");
        }
        return out;
    }

    // No push data: SM4 has none, and nothing here lowers anything to it.
    uint32_t mapPushData(ir::ShaderStageMask stages) override {
        (void)stages;
        return 0;
    }

    const std::string& Error() const { return _error; }

private:
    void Fail(const std::string& what) {
        if (!_error.empty()) return;
        _error = what + " is outside the Vulkan binding layout (shader_convert.h)";
    }

    Stage       _stage;
    std::string _error;
};

// DXVK's compile options (dxvk_shader_ir.cpp DxvkIrShader::convertShader, master cbd2206), with the
// device-dependent switches at the values the floor of PLAN.md §4.1 allows on every target: no
// native 16-bit arithmetic (min16 stays 32-bit), no driver workarounds, descriptor indexing present
// (core 1.2, confirmed on the S25), 16-byte storage-buffer alignment (the SM4 programs have no
// storage buffers; the value only steers typed vs raw access for them).
ir::CompileOptions DxvkCompileOptions() {
    ir::CompileOptions options;
    options.arithmeticOptions.lowerDot = true;
    options.arithmeticOptions.lowerSinCos = false;
    options.arithmeticOptions.lowerMsad = true;
    options.arithmeticOptions.lowerUdiv = true;
    options.arithmeticOptions.lowerF32toF16 = false;
    options.arithmeticOptions.lowerConvertFtoI = false;
    options.arithmeticOptions.lowerGsVertexCountIn = false;
    options.arithmeticOptions.hasNvUnsignedItoFBug = false;

    options.min16Options.enableFloat16 = false;
    options.min16Options.enableInt16 = false;

    options.resourceOptions.allowSubDwordScratchAndLds = true;
    options.resourceOptions.flattenLds = false;
    options.resourceOptions.flattenScratch = false;
    options.resourceOptions.structuredCbv = true;
    options.resourceOptions.structuredSrvUav = true;

    const uint32_t ssboAlignment = 16;
    options.bufferOptions.useTypedForRaw = ssboAlignment > 16u;
    options.bufferOptions.useTypedForStructured = ssboAlignment > 4u;
    options.bufferOptions.useTypedForSparseFeedback = true;
    options.bufferOptions.useRawForTypedAtomic = ssboAlignment <= 4u;
    options.bufferOptions.forceFormatForTypedUavRead = false;
    options.bufferOptions.minStructureAlignment = ssboAlignment;

    options.scalarizeOptions.subDwordVectors = true;

    options.syncOptions.insertRovLocks = true;
    options.syncOptions.insertLdsBarriers = false;
    options.syncOptions.insertUavBarriers = false;

    options.derivativeOptions.hoistNontrivialDerivativeOps = true;
    options.derivativeOptions.hoistNontrivialImplicitLodOps = false;
    options.derivativeOptions.hoistDescriptorLoads = true;

    options.cseOptions.relocateDescriptorLoad = true;
    options.cseOptions.resolveOverlappingLoads = true;

    options.descriptorIndexing.optimizeDescriptorIndexing = true;
    return options;
}

// DXVK's SpirvBuilder options without float controls (DXVK enables them only when the device has
// independent denorm modes plus RTE and flush for fp32; leaving them out is always valid and keeps
// one module for every device). maxCbvSize: a D3D constant buffer is at most 4096 vec4 = 64 KiB,
// which is maxUniformBufferRange on the desktop drivers and the Adreno 8xx stack.
spv::SpirvBuilder::Options SpirvOptions() {
    spv::SpirvBuilder::Options options;
    options.includeDebugNames = true;
    options.nvRawAccessChains = false;
    options.dualSourceBlending = false;
    options.floatControls2 = false;
    options.maxCbvSize = 65536;
    options.maxCbvCount = -1;
    options.zeroInitializeScratch = false;
    return options;
}

ir::ShaderStage IrStage(Stage stage) {
    return stage == Stage::Vertex ? ir::ShaderStage::eVertex : ir::ShaderStage::ePixel;
}

IoElement ElementOf(const ir::IoMap& map, const ir::IoLocation& loc) {
    IoElement e;
    e.componentMask = loc.getComponentMask();
    if (loc.getType() == ir::IoEntryType::eBuiltIn) {
        e.builtIn = static_cast<uint8_t>(loc.getBuiltIn());
        e.location = static_cast<uint32_t>(loc.getBuiltIn());
    } else {
        e.location = loc.getLocationIndex();
    }
    ir::IoSemantic semantic = map.getSemanticForEntry(loc);
    e.semantic = semantic.name;
    e.semanticIndex = semantic.index;
    return e;
}

ir::IoLocation LocationOf(const IoElement& e) {
    if (e.builtIn != IoElement::kNoBuiltIn) {
        return ir::IoLocation(static_cast<ir::BuiltIn>(e.builtIn), e.componentMask);
    }
    return ir::IoLocation(ir::IoEntryType::ePerVertex, static_cast<uint8_t>(e.location), e.componentMask);
}

// The IoMap of a reflected element list (the previous stage's outputs for the link passes).
ir::IoMap MapOf(const std::vector<IoElement>& elements) {
    ir::IoMap map;
    for (const IoElement& e : elements) {
        ir::IoSemantic semantic;
        semantic.name = e.semantic;
        semantic.index = e.semanticIndex;
        map.add(LocationOf(e), semantic);
    }
    return map;
}

uint32_t RegisterBits(uint32_t first, uint32_t count) {
    uint32_t bits = 0;
    if (count == 0) count = 1;   // an unbounded array: the first register is all the mask can say
    for (uint32_t r = first; r < first + count && r < 32; ++r)
        bits |= 1u << r;
    return bits;
}

// The registers each resource declaration of the legalized module names. Operands of DclCbv /
// DclSrv / DclUav / DclSampler: the entry point, the register space, the first register, the count.
void ReflectResources(const ir::Builder& builder, ShaderReflection* refl) {
    auto [a, b] = builder.getDeclarations();
    for (auto it = a; it != b; ++it) {
        const ir::OpCode code = it->getOpCode();
        const bool resource = code == ir::OpCode::eDclCbv || code == ir::OpCode::eDclSrv ||
                              code == ir::OpCode::eDclUav || code == ir::OpCode::eDclSampler;
        if (!resource) continue;
        const uint32_t first = uint32_t(it->getOperand(2u));
        const uint32_t count = uint32_t(it->getOperand(3u));
        const uint32_t bits = RegisterBits(first, count);
        if (code == ir::OpCode::eDclCbv) refl->constantBufferMask |= bits;
        if (code == ir::OpCode::eDclSrv) {
            refl->textureMask |= bits;
            // Operand 4 is the resource kind: a cube needs a cube view, also from a placeholder.
            const auto kind = ir::ResourceKind(it->getOperand(4u));
            if (kind == ir::ResourceKind::eImageCube || kind == ir::ResourceKind::eImageCubeArray)
                refl->textureCubeMask |= bits;
        }
        if (code == ir::OpCode::eDclSampler) refl->samplerMask |= bits;
        if (code == ir::OpCode::eDclUav) refl->uavMask |= bits;
    }
}

void ReflectIo(const ir::Builder& builder, ShaderReflection* refl) {
    const ir::IoMap inputs = ir::IoMap::forInputs(builder);
    for (const ir::IoLocation& loc : inputs)
        refl->inputs.push_back(ElementOf(inputs, loc));
    // P7: the numeric type the converter gave each plain input (the vertex attribute's format must
    // agree with it: Vulkan's VUID 08733, which D3D11 does not check).
    const ir::ShaderStage stage = ir::IoMap::getStageForBuilder(builder);
    auto [a, b] = builder.getDeclarations();
    for (auto it = a; it != b; ++it) {
        if (it->getOpCode() != ir::OpCode::eDclInput) continue;
        const ir::IoLocation loc = ir::IoMap::getEntryForOp(stage, *it);
        const ir::BasicType type = it->getType().getBaseType(0u);
        uint8_t numeric = IoElement::kFloat;
        if (type.isSignedIntType()) numeric = IoElement::kSint;
        if (type.isUnsignedIntType()) numeric = IoElement::kUint;
        for (IoElement& e : refl->inputs) {
            if (e.builtIn == IoElement::kNoBuiltIn && e.location == loc.getLocationIndex()) e.numeric = numeric;
        }
    }
    const ir::IoMap outputs = ir::IoMap::forOutputs(builder, 0u);
    for (const ir::IoLocation& loc : outputs)
        refl->outputs.push_back(ElementOf(outputs, loc));
}

// ---- little-endian payload writer / reader --------------------------------------------------
void Put32(std::vector<uint8_t>& out, uint32_t v) {
    for (int i = 0; i < 4; ++i)
        out.push_back(static_cast<uint8_t>(v >> (8 * i)));
}

void PutElements(std::vector<uint8_t>& out, const std::vector<IoElement>& elements) {
    Put32(out, static_cast<uint32_t>(elements.size()));
    for (const IoElement& e : elements) {
        Put32(out, e.semanticIndex);
        Put32(out, e.location);
        out.push_back(e.componentMask);
        out.push_back(e.builtIn);
        out.push_back(e.numeric);
        Put32(out, static_cast<uint32_t>(e.semantic.size()));
        out.insert(out.end(), e.semantic.begin(), e.semantic.end());
    }
}

struct Reader {
    const std::vector<uint8_t>& data;
    size_t at = 0;
    bool ok = true;

    uint32_t U32() {
        if (!ok || data.size() - at < 4) {
            ok = false;
            return 0;
        }
        const uint32_t v = ReadU32(data.data() + at);
        at += 4;
        return v;
    }
    uint8_t U8() {
        if (!ok || at >= data.size()) {
            ok = false;
            return 0;
        }
        return data[at++];
    }
    std::string Bytes(uint32_t n) {
        if (!ok || data.size() - at < n) {
            ok = false;
            return std::string();
        }
        std::string s(reinterpret_cast<const char*>(data.data() + at), n);
        at += n;
        return s;
    }
};

bool GetElements(Reader& r, std::vector<IoElement>* elements) {
    const uint32_t count = r.U32();
    if (!r.ok || count > 256) return false;
    elements->clear();
    for (uint32_t i = 0; i < count; ++i) {
        IoElement e;
        e.semanticIndex = r.U32();
        e.location = r.U32();
        e.componentMask = r.U8();
        e.builtIn = r.U8();
        e.numeric = r.U8();
        const uint32_t length = r.U32();
        if (!r.ok || length > 256) return false;
        e.semantic = r.Bytes(length);
        elements->push_back(e);
    }
    return r.ok;
}

uint64_t LinkFingerprint(const ConvertOptions& options) {
    if (options.previousStage == nullptr) return 0;
    std::vector<uint8_t> bytes;
    PutElements(bytes, options.previousStage->outputs);
    return Ext::DerivedCache::Hash64(bytes.data(), bytes.size()) | 1u;   // never 0 (= unlinked)
}

// Hardening (review F-029, F-030): two token-stream shapes that dxbc-spirv turns into memory
// corruption, refused here before the converter sees the program, so third_party/dxbc-spirv stays
// upstream. Valid programs (fxc output) hold neither, and convert unchanged.
//   - A temp register r# at or past D3D11's 4096 (D3D11_COMMONSHADER_TEMP_REGISTER_COUNT).
//     RegisterFile::getOrDeclareTemp indexes its table with 4 * index + component in uint32_t: for
//     r1073741823.w that wraps to 0xFFFFFFFF, the table is resized to 0 and m_rRegs[0xFFFFFFFF] is
//     read and written; below the wrap it allocates gigabytes. dxbc-spirv ignores dcl_temps on
//     purpose (custom DXBC emitters exceed it), so the bound is the API's limit, not the declaration.
//   - A double compare (deq / dge / dlt / dne) or a double-to-32-bit conversion (dtoi / dtou / dtof)
//     writing more than 2 components. Converter::convertMaskTo64Bit doubles the component count into
//     a read mask of up to 8 bits, and ir::composite writes one entry per bit into a
//     std::array<SsaDef, 4> (a stack buffer overflow). D3D allows at most 2 (two doubles).
constexpr uint32_t kMaxTempRegisters = 4096;

// The operand and the relative-index operands nested in it (r0.x in cb0[r0.x + 1]), walked without
// recursion. The visit cap is far above any real operand (at most 3 index dimensions, each nested
// at most a level or two deep).
bool CheckOperandTemps(const dxbc::Instruction& op, const dxbc::Operand& operand, std::string* why) {
    std::vector<const dxbc::Operand*> pending;
    pending.push_back(&operand);
    uint32_t visited = 0;
    while (!pending.empty()) {
        const dxbc::Operand* o = pending.back();
        pending.pop_back();
        if (++visited > 64) {
            SetWhy(why, "a DXBC operand nests its relative indices too deeply");
            return false;
        }
        const uint32_t dims = o->getIndexDimensions();
        if (o->getRegisterType() == dxbc::RegisterType::eTemp && dims >= 1 &&
            o->getIndexType(0) == dxbc::IndexType::eImm32 && o->getIndex(0) >= kMaxTempRegisters) {
            SetWhy(why, "temp register r" + std::to_string(o->getIndex(0)) + " is past the D3D11 limit of " +
                            std::to_string(kMaxTempRegisters));
            return false;
        }
        for (uint32_t d = 0; d < dims; ++d) {
            if (dxbc::hasRelativeIndexing(o->getIndexType(d))) {
                pending.push_back(&op.getRawOperand(o->getIndexOperand(d)));
            }
        }
    }
    return true;
}

bool CheckProgramTokens(const dxbc::Container& container, std::string* why) {
    dxbc_spv::util::ByteReader code = container.getCodeChunk();
    if (!code) return true;   // the converter reports a missing code chunk itself
    dxbc::Parser parser(code);
    if (!parser.getShaderInfo().getDwordCount()) return true;   // and an unparsable one
    while (parser) {
        const dxbc::Instruction op = parser.parseInstruction();
        if (!op) break;   // and a malformed instruction
        for (uint32_t i = 0; i < op.getDstCount(); ++i) {
            if (!CheckOperandTemps(op, op.getDst(i), why)) return false;
        }
        for (uint32_t i = 0; i < op.getSrcCount(); ++i) {
            if (!CheckOperandTemps(op, op.getSrc(i), why)) return false;
        }
        for (uint32_t i = 0; i < op.getExtraCount(); ++i) {
            if (!CheckOperandTemps(op, op.getExtra(i), why)) return false;
        }
        switch (op.getOpToken().getOpCode()) {
        case dxbc::OpCode::eDEq:
        case dxbc::OpCode::eDGe:
        case dxbc::OpCode::eDLt:
        case dxbc::OpCode::eDNe:
        case dxbc::OpCode::eDtoI:
        case dxbc::OpCode::eDtoU:
        case dxbc::OpCode::eDtoF:
            if (op.getDstCount() >= 1 && dxbc_spv::util::popcnt(uint8_t(op.getDst(0).getWriteMask())) > 2) {
                SetWhy(why, "opcode " + std::to_string(uint32_t(op.getOpToken().getOpCode())) +
                                " writes more than 2 components from a 64-bit source");
                return false;
            }
            break;
        default:
            break;
        }
    }
    return true;
}

}  // namespace

const char* StageName(Stage stage) {
    return stage == Stage::Vertex ? "vertex" : "pixel";
}

DescriptorSlot ConstantBufferSlot(Stage stage, uint32_t reg) {
    return DescriptorSlot{SetOfStage(stage), BindingOfConstantBuffer(reg)};
}

DescriptorSlot TextureSlot(Stage stage, uint32_t reg) {
    return DescriptorSlot{SetOfStage(stage), BindingOfTexture(reg)};
}

DescriptorSlot SamplerSlot(Stage stage, uint32_t reg) {
    return DescriptorSlot{SetOfStage(stage), BindingOfSampler(reg)};
}

const char* CompilerCommit() {
    return BANISHED_DXBC_SPIRV_COMMIT;
}

// The container header is "DXBC", a 16-byte checksum, version 1, the total size, the chunk count,
// then one offset per chunk; a chunk is a FourCC, its size, its bytes. The SHDR / SHEX chunk starts
// with the version token: the program type in bits 16..31 (0 pixel, 1 vertex, 2 geometry, 3 hull,
// 4 domain, 5 compute), the model in bits 0..7.
bool StageOfDxbc(const uint8_t* dxbc, size_t size, Stage* stage, std::string* why) {
    if (dxbc == nullptr || size < 32 || ReadU32(dxbc) != kDxbcMagic) {
        SetWhy(why, "not a DXBC container");
        return false;
    }
    const uint32_t total = ReadU32(dxbc + 24);
    const uint32_t chunks = ReadU32(dxbc + 28);
    if (total > size || chunks > 64 || 32u + 4u * chunks > size) {
        SetWhy(why, "truncated DXBC container");
        return false;
    }
    for (uint32_t i = 0; i < chunks; ++i) {
        const uint32_t offset = ReadU32(dxbc + 32 + 4 * i);
        if (offset > size - 12) {
            SetWhy(why, "DXBC chunk offset past the end");
            return false;
        }
        const uint32_t fourcc = ReadU32(dxbc + offset);
        if (fourcc != 0x52444853u && fourcc != 0x58454853u) continue;   // "SHDR" / "SHEX"
        const uint32_t type = ReadU32(dxbc + offset + 8) >> 16;
        if (type == 0) {
            *stage = Stage::Pixel;
            return true;
        }
        if (type == 1) {
            *stage = Stage::Vertex;
            return true;
        }
        SetWhy(why, "program type " + std::to_string(type) + " (only vertex and pixel programs run here)");
        return false;
    }
    SetWhy(why, "no SHDR / SHEX chunk");
    return false;
}

bool ConvertDxbc(const uint8_t* dxbc, size_t size, Stage stage, std::vector<uint32_t>* spirv,
                 ShaderReflection* refl, std::string* why, const ConvertOptions& options) {
    spirv->clear();
    *refl = ShaderReflection();
    refl->stage = stage;

    Stage declared = Stage::Vertex;
    if (!StageOfDxbc(dxbc, size, &declared, why)) return false;
    if (declared != stage) {
        SetWhy(why, std::string("the blob is a ") + StageName(declared) + " program, " + StageName(stage) +
                        " was expected");
        return false;
    }

    CaptureLogger logger;
    dxbc::Container container(dxbc, size);
    if (!container) {
        SetWhy(why, "dxbc-spirv could not parse the container: " + logger.messages);
        return false;
    }
    if (!CheckProgramTokens(container, why)) return false;

    // Semantic names ride on the IR's debug names (dxbc-spirv emits them only with
    // includeDebugNames), and the reflection and the link passes need them.
    dxbc::Converter::Options convertOptions;
    convertOptions.includeDebugNames = true;
    ir::Builder builder;
    dxbc::Converter converter(std::move(container), convertOptions);
    if (!converter.convertShader(builder)) {
        SetWhy(why, "dxbc-spirv conversion failed: " + logger.messages);
        return false;
    }
    if (ir::IoMap::getStageForBuilder(builder) != IrStage(stage)) {
        SetWhy(why, "dxbc-spirv produced a different stage");
        return false;
    }

    ir::legalizeIr(builder, DxvkCompileOptions());

    // The IO lowering DXVK runs before SPIR-V (DxvkIrShader::getCode).
    {
        ir::LowerIoPass io(builder);
        if (stage == Stage::Pixel) {
            io.resolveUnwrittenPsOutputs();
            if (options.previousStage != nullptr) {
                const ir::IoMap previous = MapOf(options.previousStage->outputs);
                const ir::IoMap inputs = ir::IoMap::forInputs(builder);
                if (!ir::IoMap::checkCompatibility(ir::ShaderStage::eVertex, previous, ir::ShaderStage::ePixel,
                                                   inputs, true)) {
                    io.resolveSemanticIo(previous);
                    io.resolveMismatchedIo(ir::ShaderStage::eVertex, previous);
                }
            }
        }
    }

    ReflectResources(builder, refl);
    ReflectIo(builder, refl);

    LayoutMapping mapping(stage);
    spv::SpirvBuilder spirvBuilder(builder, mapping, SpirvOptions());
    spirvBuilder.buildSpirvBinary();
    if (!mapping.Error().empty()) {
        SetWhy(why, mapping.Error());
        spirv->clear();
        return false;
    }
    *spirv = spirvBuilder.getSpirvBinary();
    std::string structure;
    if (!CheckSpirvStructure(*spirv, &structure)) {
        SetWhy(why, "dxbc-spirv produced a malformed module: " + structure);
        spirv->clear();
        return false;
    }
    // P8 round 2: OpBitFieldInsert lowered to shifts and masks (spirv_legalize.h: the Adreno 830 driver
    // refuses a vertex shader holding one). Every module, the AO pass's included.
    {
        std::string legalize;
        uint32_t changed = 0;
        if (!LowerBitFieldInsert(spirv, &changed, &legalize)) {
            SetWhy(why, legalize);
            spirv->clear();
            return false;
        }
        if (changed != 0 && !CheckSpirvStructure(*spirv, &structure)) {
            SetWhy(why, "the bit-field lowering produced a malformed module: " + structure);
            spirv->clear();
            return false;
        }
    }
    // P8: one COMBINED_IMAGE_SAMPLER per sampled (texture, sampler) pair (spirv_combine.h).
    if (options.combineSamplers) {
        std::string combine;
        if (!CombineImageSamplers(spirv, SetOfStage(stage), refl, &combine)) {
            SetWhy(why, combine);
            spirv->clear();
            return false;
        }
        if (!CheckSpirvStructure(*spirv, &structure)) {
            SetWhy(why, "the combined-sampler pass produced a malformed module: " + structure);
            spirv->clear();
            return false;
        }
    }
    return true;
}

bool StagesCompatible(const ShaderReflection& vs, const ShaderReflection& ps) {
    return ir::IoMap::checkCompatibility(ir::ShaderStage::eVertex, MapOf(vs.outputs), ir::ShaderStage::ePixel,
                                         MapOf(ps.inputs), true);
}

bool CheckSpirvStructure(const std::vector<uint32_t>& spirv, std::string* why) {
    if (spirv.size() < 5) {
        SetWhy(why, "shorter than the 5-word header");
        return false;
    }
    if (spirv[0] != kSpirvMagic) {
        SetWhy(why, "bad magic");
        return false;
    }
    const uint32_t version = spirv[1];
    if ((version & 0xFF0000FFu) != 0 || version < 0x00010000u || version > 0x00010600u) {
        SetWhy(why, "unknown SPIR-V version");
        return false;
    }
    if (spirv[3] == 0) {
        SetWhy(why, "id bound 0");
        return false;
    }
    if (spirv[4] != 0) {
        SetWhy(why, "schema word not 0");
        return false;
    }
    size_t at = 5;
    while (at < spirv.size()) {
        const uint32_t words = spirv[at] >> 16;
        if (words == 0) {
            SetWhy(why, "instruction with word count 0 at word " + std::to_string(at));
            return false;
        }
        if (words > spirv.size() - at) {
            SetWhy(why, "instruction at word " + std::to_string(at) + " runs past the end");
            return false;
        }
        at += words;
    }
    return true;
}

Ext::DerivedCache::Key CacheKey(const uint8_t* dxbc, size_t size, Stage stage, const ConvertOptions& options) {
    Ext::DerivedCache::Key key;
    key.producer = Ext::DerivedCache::MakeProducer('V', 'K', 'S', 'V');
    key.version = kMappingVersion;
    const char* commit = CompilerCommit();
    const uint64_t compiler = Ext::DerivedCache::Hash64(commit, std::strlen(commit));
    const uint64_t link = LinkFingerprint(options);
    key.shape[0] = static_cast<uint32_t>(stage);
    key.shape[1] = static_cast<uint32_t>(compiler);
    key.shape[2] = static_cast<uint32_t>(compiler >> 32);
    key.shape[3] = static_cast<uint32_t>(link ^ (link >> 32));
    // P8: the combined module is a different entry (bit 31 of the stage word; the stage is 0 or 1).
    if (options.combineSamplers) key.shape[0] |= 0x80000000u;
    key.sourceSize = size;
    key.sourceHash = Ext::DerivedCache::Hash64(dxbc, size);
    return key;
}

std::vector<uint8_t> EncodeEntry(const std::vector<uint32_t>& spirv, const ShaderReflection& refl) {
    std::vector<uint8_t> out;
    out.reserve(64 + spirv.size() * 4);
    Put32(out, kEntryMagic);
    Put32(out, kMappingVersion);
    Put32(out, static_cast<uint32_t>(refl.stage));
    Put32(out, refl.constantBufferMask);
    Put32(out, refl.textureMask);
    Put32(out, refl.samplerMask);
    Put32(out, refl.textureCubeMask);
    Put32(out, refl.uavMask);
    out.push_back(refl.combined ? 1 : 0);
    out.insert(out.end(), refl.textureSampler.begin(), refl.textureSampler.end());
    Put32(out, static_cast<uint32_t>(refl.extraPairs.size()));
    for (const CombinedPair& p : refl.extraPairs) {
        out.push_back(p.binding);
        out.push_back(p.texture);
        out.push_back(p.sampler);
    }
    PutElements(out, refl.inputs);
    PutElements(out, refl.outputs);
    Put32(out, static_cast<uint32_t>(spirv.size()));
    for (uint32_t word : spirv)
        Put32(out, word);
    return out;
}

bool DecodeEntry(const std::vector<uint8_t>& payload, std::vector<uint32_t>* spirv, ShaderReflection* refl) {
    Reader r{payload};
    if (r.U32() != kEntryMagic || r.U32() != kMappingVersion || !r.ok) return false;
    ShaderReflection out;
    const uint32_t stage = r.U32();
    if (stage > 1) return false;
    out.stage = static_cast<Stage>(stage);
    out.constantBufferMask = r.U32();
    out.textureMask = r.U32();
    out.samplerMask = r.U32();
    out.textureCubeMask = r.U32();
    out.uavMask = r.U32();
    out.combined = r.U8() != 0;
    for (uint8_t& s : out.textureSampler)
        s = r.U8();
    const uint32_t extras = r.U32();
    if (!r.ok || extras > kMaxSamplers) return false;
    for (uint32_t i = 0; i < extras; ++i) {
        CombinedPair p;
        p.binding = r.U8();
        p.texture = r.U8();
        p.sampler = r.U8();
        out.extraPairs.push_back(p);
    }
    if (!GetElements(r, &out.inputs) || !GetElements(r, &out.outputs)) return false;
    // A cache entry is checked only by unkeyed hashes, so a foreign or forged one decodes here. The
    // draw and pipeline code index 32-entry binding arrays with these fields and shift 32-bit masks
    // by them: an out-of-range reflection is a miss (reconverted, the entry replaced), never decoded
    // (review S-014).
    if ((out.constantBufferMask >> kMaxConstantBuffers) != 0 || (out.textureMask >> kMaxTextures) != 0 ||
        (out.samplerMask >> kMaxSamplers) != 0 || (out.textureCubeMask >> kMaxTextures) != 0) {
        return false;
    }
    for (uint8_t s : out.textureSampler) {
        if (s != kNoSampler && s >= kMaxSamplers) return false;
    }
    for (const CombinedPair& p : out.extraPairs) {
        if (p.binding < BindingOfSampler(0) || p.binding >= BindingOfSampler(kMaxSamplers)) return false;
        if (p.texture >= kMaxTextures || p.sampler >= kMaxSamplers) return false;
    }
    const uint32_t words = r.U32();
    if (!r.ok || payload.size() - r.at != static_cast<size_t>(words) * 4) return false;
    std::vector<uint32_t> code(words);
    for (uint32_t i = 0; i < words; ++i)
        code[i] = ReadU32(payload.data() + r.at + 4 * i);
    if (!CheckSpirvStructure(code, nullptr)) return false;
    *spirv = std::move(code);
    *refl = std::move(out);
    return true;
}

}  // namespace Shaders
}  // namespace VideoVulkan
