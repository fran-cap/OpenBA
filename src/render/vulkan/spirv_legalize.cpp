// ============================================================================
// src/render/vulkan/spirv_legalize.cpp -- lowering of OpBitFieldInsert (spirv_legalize.h)
// ============================================================================
// NOT RETAIL. Two word-level SPIR-V passes. LowerBitFieldInsert:
//   1. index the 32-bit integer scalar types and the 32-bit OpConstant values;
//   2. replace each OpBitFieldInsert on such a type with constant offset and count by
//      OpShiftLeftLogical / OpBitwiseAnd / OpBitwiseAnd / OpBitwiseOr (the last keeps the result id);
//   3. declare the two mask constants of each before the first function.
// DowngradeSpirv (the 1.1 path): index the variables' storage classes and the capabilities, rewrite
// (version, extensions, entry-point interface, demote / terminate to OpKill), then fix the OpPhis.
// ============================================================================
#include "spirv_legalize.h"

#include <cstddef>
#include <map>
#include <utility>

namespace VideoVulkan {
namespace Shaders {

namespace {

constexpr uint32_t kOpTypeInt = 21;
constexpr uint32_t kOpConstant = 43;
constexpr uint32_t kOpFunction = 54;
constexpr uint32_t kOpShiftLeftLogical = 196;
constexpr uint32_t kOpBitwiseOr = 197;
constexpr uint32_t kOpBitwiseAnd = 199;
constexpr uint32_t kOpBitFieldInsert = 201;

uint32_t Head(uint32_t op, uint32_t words) {
    return (words << 16) | op;
}

void SetWhy(std::string* why, const std::string& text) {
    if (why) *why = text;
}

}  // namespace

bool LowerBitFieldInsert(std::vector<uint32_t>* spirv, uint32_t* changed, std::string* why) {
    *changed = 0;
    const std::vector<uint32_t>& m = *spirv;
    if (m.size() < 5) {
        SetWhy(why, "legalize: module too short");
        return false;
    }
    uint32_t bound = m[3];

    // ---- 1. index ----
    std::map<uint32_t, uint32_t> intWidth;    // OpTypeInt id -> width
    std::map<uint32_t, uint32_t> constants;   // OpConstant id (one value word) -> value
    size_t firstFunction = m.size();
    bool any = false;
    for (size_t at = 5; at < m.size();) {
        const uint32_t words = m[at] >> 16;
        const uint32_t op = m[at] & 0xFFFFu;
        if (words == 0 || at + words > m.size()) {
            SetWhy(why, "legalize: malformed instruction stream");
            return false;
        }
        if (op == kOpTypeInt && words == 4) intWidth[m[at + 1]] = m[at + 2];
        if (op == kOpConstant && words == 4) constants[m[at + 2]] = m[at + 3];
        if (op == kOpFunction && firstFunction == m.size()) firstFunction = at;
        if (op == kOpBitFieldInsert) any = true;
        at += words;
    }
    if (!any || firstFunction == m.size()) return true;

    // ---- 2. rewrite ----
    std::vector<uint32_t> out(m.begin(), m.begin() + 5);
    out.reserve(m.size() + 64);
    std::vector<uint32_t> newConstants;
    size_t constantsAt = 0;
    for (size_t at = 5; at < m.size();) {
        const uint32_t words = m[at] >> 16;
        const uint32_t op = m[at] & 0xFFFFu;
        const uint32_t* w = &m[at];
        if (at == firstFunction) constantsAt = out.size();
        // OpBitFieldInsert: result type, result, base, insert, offset, count.
        const bool lower = op == kOpBitFieldInsert && words == 7 && intWidth.count(w[1]) != 0 &&
                           intWidth[w[1]] == 32 && constants.count(w[5]) != 0 && constants.count(w[6]) != 0 &&
                           constants[w[5]] < 32 && constants[w[6]] <= 32 - constants[w[5]];
        if (lower) {
            const uint32_t offset = constants[w[5]];
            const uint32_t count = constants[w[6]];
            const uint32_t mask = (count == 32 ? 0xFFFFFFFFu : ((1u << count) - 1u)) << offset;
            const uint32_t type = w[1];
            const uint32_t maskId = bound++;
            const uint32_t notMaskId = bound++;
            newConstants.insert(newConstants.end(), { Head(kOpConstant, 4), type, maskId, mask });
            newConstants.insert(newConstants.end(), { Head(kOpConstant, 4), type, notMaskId, ~mask });
            const uint32_t shifted = bound++;
            const uint32_t inserted = bound++;
            const uint32_t kept = bound++;
            out.insert(out.end(), { Head(kOpShiftLeftLogical, 5), type, shifted, w[4], w[5] });
            out.insert(out.end(), { Head(kOpBitwiseAnd, 5), type, inserted, shifted, maskId });
            out.insert(out.end(), { Head(kOpBitwiseAnd, 5), type, kept, w[3], notMaskId });
            out.insert(out.end(), { Head(kOpBitwiseOr, 5), type, w[2], inserted, kept });
            ++*changed;
        } else {
            out.insert(out.end(), w, w + words);
        }
        at += words;
    }
    if (*changed == 0) return true;
    out.insert(out.begin() + static_cast<std::ptrdiff_t>(constantsAt), newConstants.begin(), newConstants.end());
    out[3] = bound;
    *spirv = std::move(out);
    return true;
}

// ---- DowngradeSpirv (spirv_legalize.h, THE 1.1 PATH) ----------------------------------------------

namespace {

constexpr uint32_t kOpExtension = 10;
constexpr uint32_t kOpEntryPoint = 15;
constexpr uint32_t kOpCapability = 17;
constexpr uint32_t kOpVariable = 59;
constexpr uint32_t kOpPhi = 245;
constexpr uint32_t kOpLabel = 248;
constexpr uint32_t kOpBranch = 249;
constexpr uint32_t kOpKill = 252;
constexpr uint32_t kOpReturn = 253;
constexpr uint32_t kOpTerminateInvocation = 4416;
constexpr uint32_t kOpDemoteToHelperInvocation = 5380;
constexpr uint32_t kCapabilityVulkanMemoryModel = 5345;
constexpr uint32_t kCapabilityPhysicalStorageBufferAddresses = 5347;
constexpr uint32_t kCapabilityDemoteToHelperInvocation = 5379;
constexpr uint32_t kStorageInput = 1;
constexpr uint32_t kStorageOutput = 3;
constexpr uint32_t kSpirv13 = 0x00010300u;

// A literal string operand (nul-terminated, padded to whole words) starting at word `at`.
std::string LiteralAt(const uint32_t* w, size_t at, size_t words) {
    std::string s;
    for (size_t i = at; i < words; ++i) {
        for (int b = 0; b < 4; ++b) {
            const char c = static_cast<char>((w[i] >> (8 * b)) & 0xFFu);
            if (c == '\0') return s;
            s.push_back(c);
        }
    }
    return s;
}

// The words of `OpExtension "name"`.
std::vector<uint32_t> ExtensionInstruction(const std::string& name) {
    std::vector<uint32_t> literal((name.size() + 4) / 4, 0u);
    for (size_t i = 0; i < name.size(); ++i)
        literal[i / 4] |= static_cast<uint32_t>(static_cast<uint8_t>(name[i])) << (8 * (i % 4));
    std::vector<uint32_t> out{ Head(kOpExtension, static_cast<uint32_t>(1 + literal.size())) };
    out.insert(out.end(), literal.begin(), literal.end());
    return out;
}

}  // namespace

bool DowngradeSpirv(std::vector<uint32_t>* spirv, std::string* why) {
    const std::vector<uint32_t>& m = *spirv;
    if (m.size() < 5) {
        SetWhy(why, "downgrade: module too short");
        return false;
    }
    if (m[1] <= kSpirv13) return true;

    // ---- 1. index: storage classes, capabilities, extensions ----
    std::map<uint32_t, uint32_t> storage;   // OpVariable id -> storage class
    bool memoryModel = false;
    bool physicalStorage = false;
    std::vector<std::string> extensions;
    for (size_t at = 5; at < m.size();) {
        const uint32_t words = m[at] >> 16;
        const uint32_t op = m[at] & 0xFFFFu;
        if (words == 0 || at + words > m.size()) {
            SetWhy(why, "downgrade: malformed instruction stream");
            return false;
        }
        if (op == kOpVariable && words >= 4) storage[m[at + 2]] = m[at + 3];
        if (op == kOpCapability && words == 2 && m[at + 1] == kCapabilityVulkanMemoryModel) memoryModel = true;
        if (op == kOpCapability && words == 2 && m[at + 1] == kCapabilityPhysicalStorageBufferAddresses) {
            physicalStorage = true;
        }
        if (op == kOpExtension) extensions.push_back(LiteralAt(&m[at], 1, words));
        at += words;
    }
    std::vector<std::string> needed;
    if (memoryModel) needed.push_back("SPV_KHR_vulkan_memory_model");
    if (physicalStorage) needed.push_back("SPV_KHR_physical_storage_buffer");

    // ---- 2. rewrite ----
    std::vector<uint32_t> out(m.begin(), m.begin() + 5);
    out[1] = kSpirv13;
    out.reserve(m.size() + 16);
    bool extensionsAdded = false;
    uint32_t block = 0;                // the label of the block being copied
    std::vector<uint32_t> killed;      // blocks whose demote became OpKill
    for (size_t at = 5; at < m.size();) {
        const uint32_t words = m[at] >> 16;
        const uint32_t op = m[at] & 0xFFFFu;
        const uint32_t* w = &m[at];
        at += words;
        if (!extensionsAdded && op != kOpCapability) {
            for (const std::string& name : needed) {
                bool present = false;
                for (const std::string& e : extensions)
                    present = present || e == name;
                if (present) continue;
                const std::vector<uint32_t> ext = ExtensionInstruction(name);
                out.insert(out.end(), ext.begin(), ext.end());
            }
            extensionsAdded = true;
        }
        if (op == kOpCapability && words == 2 && w[1] == kCapabilityDemoteToHelperInvocation) continue;
        if (op == kOpLabel && words == 2) block = w[1];
        if (op == kOpEntryPoint && words >= 4) {
            // Execution model, entry id, the name (to its last word), then the interface ids.
            size_t name = 3;
            while (name < words && (w[name] >> 24) != 0)
                ++name;
            const size_t first = name + 1;
            if (first > words) {
                SetWhy(why, "downgrade: malformed OpEntryPoint");
                return false;
            }
            std::vector<uint32_t> entry(w, w + first);
            for (size_t i = first; i < words; ++i) {
                auto it = storage.find(w[i]);
                if (it != storage.end() && (it->second == kStorageInput || it->second == kStorageOutput)) {
                    entry.push_back(w[i]);
                }
            }
            entry[0] = Head(kOpEntryPoint, static_cast<uint32_t>(entry.size()));
            out.insert(out.end(), entry.begin(), entry.end());
            continue;
        }
        if (op == kOpTerminateInvocation) {
            out.push_back(Head(kOpKill, 1));
            continue;
        }
        if (op == kOpDemoteToHelperInvocation) {
            // Lowered only when the block ends right after it with a plain branch or return.
            const uint32_t next = at < m.size() ? (m[at] & 0xFFFFu) : 0;
            if (next != kOpBranch && next != kOpReturn) {
                SetWhy(why, "downgrade: an OpDemoteToHelperInvocation not followed by the end of its block");
                return false;
            }
            out.push_back(Head(kOpKill, 1));
            at += m[at] >> 16;   // the branch / return: the block ends with the OpKill
            killed.push_back(block);
            continue;
        }
        out.insert(out.end(), w, w + words);
    }

    // ---- 3. an OpPhi naming a killed block as a parent drops that (value, parent) pair ----
    if (!killed.empty()) {
        std::vector<uint32_t> fixed(out.begin(), out.begin() + 5);
        fixed.reserve(out.size());
        for (size_t at = 5; at < out.size();) {
            const uint32_t words = out[at] >> 16;
            const uint32_t op = out[at] & 0xFFFFu;
            const uint32_t* w = &out[at];
            at += words;
            if (op != kOpPhi) {
                fixed.insert(fixed.end(), w, w + words);
                continue;
            }
            std::vector<uint32_t> phi(w, w + 3);
            for (size_t i = 3; i + 1 < words; i += 2) {
                bool dead = false;
                for (uint32_t k : killed)
                    dead = dead || w[i + 1] == k;
                if (dead) continue;
                phi.push_back(w[i]);
                phi.push_back(w[i + 1]);
            }
            if (phi.size() == 3) {
                SetWhy(why, "downgrade: an OpPhi whose every parent was killed");
                return false;
            }
            phi[0] = Head(kOpPhi, static_cast<uint32_t>(phi.size()));
            fixed.insert(fixed.end(), phi.begin(), phi.end());
        }
        out = std::move(fixed);
    }
    *spirv = std::move(out);
    return true;
}

// ---- WidenInputs (spirv_legalize.h, THE 1.1 PATH) -------------------------------------------------

namespace {

constexpr uint32_t kOpDecorate = 71;
constexpr uint32_t kOpTypeFloat = 22;
constexpr uint32_t kOpTypeVector = 23;
constexpr uint32_t kOpTypePointer = 32;
constexpr uint32_t kOpLoad = 61;
constexpr uint32_t kOpAccessChain = 65;
constexpr uint32_t kOpInBoundsAccessChain = 66;
constexpr uint32_t kOpVectorShuffle = 79;
constexpr uint32_t kOpCompositeExtract = 81;
constexpr uint32_t kDecorationBuiltIn = 11;
constexpr uint32_t kDecorationLocation = 30;
constexpr uint32_t kDecorationComponent = 31;

struct Widened {
    uint32_t component = 0;   // the scalar type
    uint32_t from = 0;        // the old pointee type (a scalar or a vector) and its width
    uint32_t fromWidth = 0;
    uint32_t to = 0;          // the new vector type, its pointer, and its width
    uint32_t pointer = 0;
    uint32_t toWidth = 0;
};

}  // namespace

bool WidenInputs(std::vector<uint32_t>* spirv, const uint32_t widths[32], uint32_t* changed,
                 std::string* why) {
    *changed = 0;
    const std::vector<uint32_t>& m = *spirv;
    if (m.size() < 5) {
        SetWhy(why, "widen: module too short");
        return false;
    }
    uint32_t bound = m[3];

    // ---- 1. index ----
    std::map<uint32_t, uint32_t> location;
    std::map<uint32_t, uint32_t> component;
    std::map<uint32_t, bool> builtIn;
    std::map<uint32_t, std::pair<uint32_t, uint32_t>> vectors;    // id -> (scalar type, width)
    std::map<uint32_t, bool> scalars;                             // OpTypeInt / OpTypeFloat ids
    std::map<uint32_t, std::pair<uint32_t, uint32_t>> pointers;   // id -> (storage class, pointee)
    std::map<uint32_t, uint32_t> inputs;                          // Input OpVariable -> its pointer type
    size_t firstFunction = m.size();
    for (size_t at = 5; at < m.size();) {
        const uint32_t words = m[at] >> 16;
        const uint32_t op = m[at] & 0xFFFFu;
        const uint32_t* w = &m[at];
        if (words == 0 || at + words > m.size()) {
            SetWhy(why, "widen: malformed instruction stream");
            return false;
        }
        if (op == kOpFunction && firstFunction == m.size()) firstFunction = at;
        if (op == kOpDecorate && words >= 3) {
            if (w[2] == kDecorationLocation && words == 4) location[w[1]] = w[3];
            if (w[2] == kDecorationComponent && words == 4) component[w[1]] = w[3];
            if (w[2] == kDecorationBuiltIn) builtIn[w[1]] = true;
        }
        if ((op == kOpTypeInt || op == kOpTypeFloat) && words >= 3 && w[2] == 32) scalars[w[1]] = true;
        if (op == kOpTypeVector && words == 4) vectors[w[1]] = { w[2], w[3] };
        if (op == kOpTypePointer && words == 4) pointers[w[1]] = { w[2], w[3] };
        const bool global = at < firstFunction;
        if (op == kOpVariable && words >= 4 && w[3] == kStorageInput && global) inputs[w[2]] = w[1];
        at += words;
    }

    // ---- 2. the inputs to widen ----
    std::map<uint32_t, Widened> widen;   // variable id -> what it becomes
    for (const auto& in : inputs) {
        const uint32_t var = in.first;
        if (builtIn.count(var) || !location.count(var) || location[var] >= 32) continue;
        if (component.count(var) && component[var] != 0) continue;
        auto ptr = pointers.find(in.second);
        if (ptr == pointers.end()) continue;
        Widened wv;
        wv.from = ptr->second.second;
        if (vectors.count(wv.from)) {
            wv.component = vectors[wv.from].first;
            wv.fromWidth = vectors[wv.from].second;
        } else if (scalars.count(wv.from)) {
            wv.component = wv.from;
            wv.fromWidth = 1;
        } else {
            continue;
        }
        if (!scalars.count(wv.component)) continue;
        wv.toWidth = widths[location[var]];
        if (wv.toWidth <= wv.fromWidth || wv.toWidth > 4) continue;
        widen[var] = wv;
    }
    if (widen.empty()) return true;

    // An input used other than by a whole OpLoad or as an access chain's base stays as it is.
    for (size_t at = firstFunction; at < m.size();) {
        const uint32_t words = m[at] >> 16;
        const uint32_t op = m[at] & 0xFFFFu;
        const uint32_t* w = &m[at];
        at += words;
        const bool load = op == kOpLoad && words >= 4;
        const bool chain = (op == kOpAccessChain || op == kOpInBoundsAccessChain) && words >= 4;
        for (size_t i = 1; i < words; ++i) {
            if ((load || chain) && i == 3) continue;
            widen.erase(w[i]);
        }
    }
    if (widen.empty()) return true;

    // ---- 3. the wider types: an existing vector / pointer type, or a new one ----
    std::vector<uint32_t> declarations;   // new types and the moved variables, before the first function
    for (auto& entry : widen) {
        Widened& wv = entry.second;
        for (const auto& v : vectors) {
            if (v.second.first == wv.component && v.second.second == wv.toWidth) wv.to = v.first;
        }
        if (!wv.to) {
            wv.to = bound++;
            declarations.insert(declarations.end(),
                                { Head(kOpTypeVector, 4), wv.to, wv.component, wv.toWidth });
            vectors[wv.to] = { wv.component, wv.toWidth };
        }
        for (const auto& p : pointers) {
            if (p.second.first == kStorageInput && p.second.second == wv.to) wv.pointer = p.first;
        }
        if (!wv.pointer) {
            wv.pointer = bound++;
            declarations.insert(declarations.end(),
                                { Head(kOpTypePointer, 4), wv.pointer, kStorageInput, wv.to });
            pointers[wv.pointer] = { kStorageInput, wv.to };
        }
    }
    // The variables move after every type they may name (globals can sit anywhere in that section, and
    // nothing global but decorations, names and the entry point refers to an input).
    for (const auto& entry : widen) {
        declarations.insert(declarations.end(),
                            { Head(kOpVariable, 4), entry.second.pointer, entry.first, kStorageInput });
    }

    // ---- 4. rewrite ----
    std::vector<uint32_t> out(m.begin(), m.begin() + 5);
    out.reserve(m.size() + declarations.size() + 16 * widen.size());
    for (size_t at = 5; at < m.size();) {
        const uint32_t words = m[at] >> 16;
        const uint32_t op = m[at] & 0xFFFFu;
        const uint32_t* w = &m[at];
        if (at == firstFunction) out.insert(out.end(), declarations.begin(), declarations.end());
        at += words;
        if (op == kOpVariable && words >= 4 && widen.count(w[2])) continue;   // moved
        if (op == kOpLoad && words >= 4 && widen.count(w[3])) {
            const Widened& wv = widen[w[3]];
            const uint32_t wide = bound++;
            // OpLoad <wide> %wide %var [memory operands], then the old components out of it.
            out.push_back(Head(kOpLoad, words));
            out.push_back(wv.to);
            out.push_back(wide);
            out.insert(out.end(), w + 3, w + words);
            if (wv.fromWidth == 1) {
                out.insert(out.end(), { Head(kOpCompositeExtract, 5), wv.from, w[2], wide, 0u });
            } else {
                out.insert(out.end(), { Head(kOpVectorShuffle, 5 + wv.fromWidth), wv.from, w[2], wide, wide });
                for (uint32_t c = 0; c < wv.fromWidth; ++c)
                    out.push_back(c);
            }
            continue;
        }
        out.insert(out.end(), w, w + words);
    }
    out[3] = bound;
    *changed = static_cast<uint32_t>(widen.size());
    *spirv = std::move(out);
    return true;
}

}  // namespace Shaders
}  // namespace VideoVulkan
