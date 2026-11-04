// ============================================================================
// src/render/vulkan/spirv_combine.cpp -- combined image samplers (spirv_combine.h)
// ============================================================================
// NOT RETAIL. A word-level rewrite of the module dxbc-spirv built. The steps:
//   1. index the module: the descriptor variables of `set` (texture = an OpTypeImage with Sampled 1,
//      sampler = OpTypeSampler) with their bindings, every OpLoad of one, every OpSampledImage;
//   2. pair each OpSampledImage's texture load with its sampler load -> (t, s); choose the bindings;
//   3. emit: one OpVariable (UniformConstant pointer to OpTypeSampledImage) per pair, decorated with
//      the set and its binding; a texture load becomes OpLoad of the primary combined variable plus
//      OpImage (its result id kept, so a Load or a size query reading it is untouched); an
//      OpSampledImage becomes OpLoad of its pair's variable (result id kept); a sampler load goes;
//      the old variables, their names and decorations and their entry-point interface ids go.
// ============================================================================
#include "spirv_combine.h"

#include "shader_convert.h"

#include <map>
#include <set>
#include <utility>

namespace VideoVulkan {
namespace Shaders {

namespace {

// SPIR-V opcodes and enumerants used here (spirv.hpp: the unified1 grammar).
constexpr uint32_t kOpName = 5;
constexpr uint32_t kOpMemberName = 6;
constexpr uint32_t kOpEntryPoint = 15;
constexpr uint32_t kOpTypeImage = 25;
constexpr uint32_t kOpTypeSampler = 26;
constexpr uint32_t kOpTypeSampledImage = 27;
constexpr uint32_t kOpTypePointer = 32;
constexpr uint32_t kOpFunction = 54;
constexpr uint32_t kOpVariable = 59;
constexpr uint32_t kOpLoad = 61;
constexpr uint32_t kOpDecorate = 71;
constexpr uint32_t kOpSampledImage = 86;
constexpr uint32_t kOpImage = 100;
constexpr uint32_t kDecorationBinding = 33;
constexpr uint32_t kDecorationDescriptorSet = 34;
constexpr uint32_t kStorageUniformConstant = 0;
constexpr uint32_t kDimBuffer = 5;

uint32_t Op(uint32_t word) {
    return word & 0xFFFFu;
}

uint32_t Words(uint32_t word) {
    return word >> 16;
}

uint32_t Head(uint32_t op, uint32_t words) {
    return (words << 16) | op;
}

struct Inst {
    size_t at = 0;      // word offset of the instruction
    uint32_t op = 0;
    uint32_t words = 0;
};

enum class VarKind { Other, Texture, Sampler };

struct Var {
    VarKind kind = VarKind::Other;
    uint32_t imageType = 0;   // Texture: the OpTypeImage id
    uint32_t reg = 0;         // the t / s register
};

void SetWhy(std::string* why, const std::string& text) {
    if (why) *why = text;
}

}  // namespace

bool CombineImageSamplers(std::vector<uint32_t>* spirv, uint32_t set, ShaderReflection* refl, std::string* why) {
    const std::vector<uint32_t>& m = *spirv;
    if (m.size() < 5) {
        SetWhy(why, "combine: module too short");
        return false;
    }
    uint32_t bound = m[3];

    // ---- 1. index ----
    std::vector<Inst> insts;
    for (size_t at = 5; at < m.size();) {
        const uint32_t words = Words(m[at]);
        if (words == 0 || at + words > m.size()) {
            SetWhy(why, "combine: malformed instruction stream");
            return false;
        }
        // Hardening (review F-089, F-090, F-091): the instructions this pass reads at fixed operands,
        // shorter than the grammar's minimum (OpTypeSampler 2 words, OpEntryPoint and OpLoad 4), are
        // refused here; the reads below ran past the instruction, and past the module when it was
        // the last one. dxbc-spirv never emits them.
        const uint32_t op = Op(m[at]);
        if ((op == kOpTypeSampler && words < 2) || (op == kOpEntryPoint && words < 4) ||
            (op == kOpLoad && words < 4)) {
            SetWhy(why, "combine: a short instruction (opcode " + std::to_string(op) + ")");
            return false;
        }
        insts.push_back(Inst{at, op, words});
        at += words;
    }
    std::map<uint32_t, uint32_t> imageDim;          // OpTypeImage id -> Dim
    std::map<uint32_t, uint32_t> imageSampled;      // OpTypeImage id -> Sampled operand
    std::set<uint32_t> samplerTypes;
    std::map<uint32_t, uint32_t> sampledImageOf;    // OpTypeImage id -> an existing OpTypeSampledImage id
    std::map<std::pair<uint32_t, uint32_t>, uint32_t> pointerTypes;   // (storage, pointee) -> id
    std::map<uint32_t, std::pair<uint32_t, uint32_t>> pointerInfo;    // id -> (storage, pointee)
    std::map<uint32_t, uint32_t> varPointer;        // OpVariable id -> its pointer type
    std::map<uint32_t, uint32_t> bindingOf;
    std::map<uint32_t, uint32_t> setOf;
    size_t firstFunction = m.size();
    size_t firstGlobal = m.size();                  // the first type / constant / variable declaration
    for (const Inst& i : insts) {
        const uint32_t* w = &m[i.at];
        const bool global = (i.op >= 19 && i.op <= 39) || (i.op >= 41 && i.op <= 52) || i.op == kOpVariable ||
                            i.op == 1 /* OpUndef */;
        if (global && firstGlobal == m.size() && firstFunction == m.size()) firstGlobal = i.at;
        if (i.op == kOpFunction && firstFunction == m.size()) firstFunction = i.at;
        if (i.op == kOpTypeImage && i.words >= 9) {
            imageDim[w[1]] = w[3];
            imageSampled[w[1]] = w[7];
        } else if (i.op == kOpTypeSampler) {
            samplerTypes.insert(w[1]);
        } else if (i.op == kOpTypeSampledImage && i.words == 3) {
            sampledImageOf.emplace(w[2], w[1]);
        } else if (i.op == kOpTypePointer && i.words == 4) {
            pointerTypes.emplace(std::make_pair(w[2], w[3]), w[1]);
            pointerInfo[w[1]] = std::make_pair(w[2], w[3]);
        } else if (i.op == kOpVariable && i.words >= 4 && firstFunction == m.size()) {
            varPointer[w[2]] = w[1];
        } else if (i.op == kOpDecorate && i.words == 4 && w[2] == kDecorationBinding) {
            bindingOf[w[1]] = w[3];
        } else if (i.op == kOpDecorate && i.words == 4 && w[2] == kDecorationDescriptorSet) {
            setOf[w[1]] = w[3];
        }
    }
    if (firstFunction == m.size() || firstGlobal == m.size()) {
        SetWhy(why, "combine: no function or no declarations");
        return false;
    }

    // The texture and sampler variables of the set.
    std::map<uint32_t, Var> vars;
    for (const auto& [id, ptr] : varPointer) {
        const auto info = pointerInfo.find(ptr);
        if (info == pointerInfo.end() || info->second.first != kStorageUniformConstant) continue;
        const uint32_t pointee = info->second.second;
        const auto s = setOf.find(id);
        const auto b = bindingOf.find(id);
        if (s == setOf.end() || b == bindingOf.end() || s->second != set) continue;
        Var v;
        if (samplerTypes.count(pointee)) {
            if (b->second < BindingOfSampler(0) || b->second >= BindingOfSampler(kMaxSamplers)) {
                SetWhy(why, "combine: a sampler at binding " + std::to_string(b->second));
                return false;
            }
            v.kind = VarKind::Sampler;
            v.reg = b->second - BindingOfSampler(0);
        } else if (imageDim.count(pointee) && imageSampled[pointee] == 1) {
            if (b->second < BindingOfTexture(0) || b->second >= BindingOfTexture(kMaxTextures)) {
                SetWhy(why, "combine: a texture at binding " + std::to_string(b->second));
                return false;
            }
            if (imageDim[pointee] == kDimBuffer) {
                SetWhy(why, "combine: a buffer SRV (t" + std::to_string(b->second - BindingOfTexture(0)) +
                                ") cannot be a combined image sampler");
                return false;
            }
            v.kind = VarKind::Texture;
            v.imageType = pointee;
            v.reg = b->second - BindingOfTexture(0);
        } else if (imageDim.count(pointee)) {
            continue;   // a storage image (UAV) keeps its own descriptor
        } else {
            // A uniform-constant variable of another type (an array of descriptors): not handled.
            SetWhy(why, "combine: a descriptor array or an unknown resource at binding " + std::to_string(b->second));
            return false;
        }
        vars[id] = v;
    }
    if (vars.empty()) {
        refl->combined = true;
        return true;   // nothing to combine (no texture, no sampler)
    }

    // Loads of those variables, and the OpSampledImage joins.
    std::map<uint32_t, uint32_t> loadVar;   // load result id -> variable id
    for (const Inst& i : insts) {
        if (i.op != kOpLoad || i.words < 4) continue;
        const uint32_t* w = &m[i.at];
        if (vars.count(w[3])) loadVar[w[2]] = w[3];
    }
    std::set<std::pair<uint32_t, uint32_t>> pairs;   // (t, s)
    std::map<uint32_t, uint32_t> textureOfReg;       // t -> its texture variable
    for (const auto& [id, v] : vars) {
        if (v.kind == VarKind::Texture) textureOfReg[v.reg] = id;
    }
    std::set<uint32_t> samplerLoads;
    for (const auto& [load, var] : loadVar) {
        if (vars[var].kind == VarKind::Sampler) samplerLoads.insert(load);
    }
    for (const Inst& i : insts) {
        const uint32_t* w = &m[i.at];
        if (i.op == kOpSampledImage) {
            if (i.words != 5) {
                SetWhy(why, "combine: an OpSampledImage of an unexpected size");
                return false;
            }
            const auto img = loadVar.find(w[3]);
            const auto smp = loadVar.find(w[4]);
            if (img == loadVar.end() || smp == loadVar.end() || vars[img->second].kind != VarKind::Texture ||
                vars[smp->second].kind != VarKind::Sampler) {
                SetWhy(why, "combine: an OpSampledImage that does not join a texture load with a sampler load");
                return false;
            }
            pairs.insert(std::make_pair(vars[img->second].reg, vars[smp->second].reg));
            continue;
        }
        // A sampler value read by anything but OpSampledImage cannot be rewritten. The instructions
        // that can carry an opaque value elsewhere, at their id operands (an operand scan of every
        // instruction would mistake literals for ids): OpCopyObject, OpCompositeConstruct, OpSelect,
        // OpPhi, OpFunctionCall, OpStore, OpReturnValue. tools/vulkan/shader_check.cpp's spirv-val run
        // over every retail permutation is the proof for the rest.
        uint32_t from = 0;
        uint32_t step = 1;
        switch (i.op) {
        case 83:    // OpCopyObject
        case 80:    // OpCompositeConstruct
            from = 3;
            break;
        case 169:   // OpSelect
            from = 4;
            break;
        case 245:   // OpPhi: value, parent pairs
            from = 3;
            step = 2;
            break;
        case 57:    // OpFunctionCall
            from = 4;
            break;
        case 62:    // OpStore
            from = 2;
            break;
        case 254:   // OpReturnValue
            from = 1;
            break;
        default:
            break;
        }
        for (uint32_t k = from; from != 0 && k < i.words; k += step) {
            if (samplerLoads.count(w[k])) {
                SetWhy(why, "combine: a sampler is used outside OpSampledImage (opcode " + std::to_string(i.op) + ")");
                return false;
            }
        }
    }

    // ---- 2. the bindings ----
    struct Combined {
        uint32_t texture = 0;
        uint32_t sampler = kNoSampler;
        uint32_t binding = 0;
        uint32_t variable = 0;
        uint32_t sampledType = 0;
        uint32_t pointerType = 0;
    };
    std::map<std::pair<uint32_t, uint32_t>, size_t> pairIndex;   // (t, s) -> combined index
    std::map<uint32_t, size_t> primaryOf;                        // t -> combined index
    std::vector<Combined> combined;
    refl->textureSampler = NoSamplers();
    refl->extraPairs.clear();
    uint32_t extra = 0;
    for (const auto& [t, var] : textureOfReg) {
        Combined c;
        c.texture = t;
        c.binding = BindingOfTexture(t);
        for (const auto& p : pairs) {
            if (p.first == t) {
                c.sampler = p.second;   // the set is ordered: the lowest sampler register first
                break;
            }
        }
        primaryOf[t] = combined.size();
        if (c.sampler != kNoSampler) pairIndex[std::make_pair(t, c.sampler)] = combined.size();
        refl->textureSampler[t] = static_cast<uint8_t>(c.sampler);
        combined.push_back(c);
    }
    for (const auto& p : pairs) {
        if (pairIndex.count(p)) continue;
        if (extra >= kMaxSamplers) {
            SetWhy(why, "combine: more than " + std::to_string(kMaxSamplers) + " further (texture, sampler) pairs");
            return false;
        }
        Combined c;
        c.texture = p.first;
        c.sampler = p.second;
        c.binding = BindingOfSampler(extra++);
        pairIndex[p] = combined.size();
        CombinedPair e;
        e.binding = static_cast<uint8_t>(c.binding);
        e.texture = static_cast<uint8_t>(c.texture);
        e.sampler = static_cast<uint8_t>(c.sampler);
        refl->extraPairs.push_back(e);
        combined.push_back(c);
    }

    // ---- 3. emit ----
    std::vector<uint32_t> newTypes;   // declarations inserted before the first function
    for (Combined& c : combined) {
        const uint32_t imageType = vars[textureOfReg[c.texture]].imageType;
        auto si = sampledImageOf.find(imageType);
        if (si == sampledImageOf.end()) {
            const uint32_t id = bound++;
            newTypes.insert(newTypes.end(), { Head(kOpTypeSampledImage, 3), id, imageType });
            si = sampledImageOf.emplace(imageType, id).first;
        }
        c.sampledType = si->second;
        auto pt = pointerTypes.find(std::make_pair(kStorageUniformConstant, c.sampledType));
        if (pt == pointerTypes.end()) {
            const uint32_t id = bound++;
            newTypes.insert(newTypes.end(), { Head(kOpTypePointer, 4), id, kStorageUniformConstant, c.sampledType });
            pt = pointerTypes.emplace(std::make_pair(kStorageUniformConstant, c.sampledType), id).first;
        }
        c.pointerType = pt->second;
        c.variable = bound++;
        newTypes.insert(newTypes.end(), { Head(kOpVariable, 4), c.pointerType, c.variable, kStorageUniformConstant });
    }
    std::vector<uint32_t> newDecorations;
    for (const Combined& c : combined) {
        newDecorations.insert(newDecorations.end(), { Head(kOpDecorate, 4), c.variable, kDecorationDescriptorSet, set });
        newDecorations.insert(newDecorations.end(), { Head(kOpDecorate, 4), c.variable, kDecorationBinding, c.binding });
    }

    std::vector<uint32_t> out(m.begin(), m.begin() + 5);
    out.reserve(m.size() + newTypes.size() + newDecorations.size() + 64);
    for (const Inst& i : insts) {
        const uint32_t* w = &m[i.at];
        if (i.at == firstGlobal) out.insert(out.end(), newDecorations.begin(), newDecorations.end());
        if (i.at == firstFunction) out.insert(out.end(), newTypes.begin(), newTypes.end());
        switch (i.op) {
        case kOpEntryPoint: {
            // Operands: model, id, a literal string (nul-terminated, padded), then the interface ids.
            size_t k = 3;
            while (k < i.words) {
                const uint32_t word = w[k++];
                if ((word >> 24) == 0) break;   // the word holding the terminating nul
            }
            std::vector<uint32_t> inst(w, w + k);
            for (; k < i.words; ++k) {
                if (!vars.count(w[k])) inst.push_back(w[k]);
            }
            for (const Combined& c : combined)
                inst.push_back(c.variable);
            inst[0] = Head(kOpEntryPoint, static_cast<uint32_t>(inst.size()));
            out.insert(out.end(), inst.begin(), inst.end());
            continue;
        }
        case kOpName:
        case kOpDecorate:
            if (i.words >= 2 && (vars.count(w[1]) || samplerLoads.count(w[1]))) continue;
            break;
        case kOpMemberName:
            break;
        case kOpVariable:
            if (i.words >= 4 && vars.count(w[2])) continue;
            break;
        case kOpLoad: {
            const auto lv = loadVar.find(w[2]);
            if (lv == loadVar.end()) break;
            const Var& v = vars[lv->second];
            if (v.kind == VarKind::Sampler) continue;   // its only readers were the joins
            // A texture load: the primary combined descriptor, then the image out of it.
            const Combined& c = combined[primaryOf[v.reg]];
            const uint32_t loaded = bound++;
            out.insert(out.end(), { Head(kOpLoad, 4), c.sampledType, loaded, c.variable });
            out.insert(out.end(), { Head(kOpImage, 4), w[1], w[2], loaded });
            continue;
        }
        case kOpSampledImage: {
            const uint32_t t = vars[loadVar[w[3]]].reg;
            const uint32_t s = vars[loadVar[w[4]]].reg;
            const Combined& c = combined[pairIndex[std::make_pair(t, s)]];
            out.insert(out.end(), { Head(kOpLoad, 4), c.sampledType, w[2], c.variable });
            continue;
        }
        default:
            break;
        }
        out.insert(out.end(), w, w + i.words);
    }
    out[3] = bound;
    *spirv = std::move(out);
    refl->combined = true;
    return true;
}

}  // namespace Shaders
}  // namespace VideoVulkan
