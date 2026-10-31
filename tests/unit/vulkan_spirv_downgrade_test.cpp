// ============================================================================
// tests/unit/vulkan_spirv_downgrade_test.cpp -- src/render/vulkan/spirv_legalize.h DowngradeSpirv and
// WidenInputs: the Vulkan backend's 1.1 path (src/render/vulkan/vulkan11.h)
// ============================================================================
// BUILT ONLY WITH THE VULKAN BACKEND (BANISHED_RENDER_BACKEND=vulkan): CMakeLists.txt filters
// tests/unit/vulkan_*_test.cpp out of every other configuration. Runs on any host (no device).
//
// The first module is SYNTHETIC, written here word by word: a fragment entry point listing an Input
// and a Uniform variable, the VulkanMemoryModel and DemoteToHelperInvocation capabilities, and a
// demote in the then-block of a selection whose merge block holds an OpPhi naming it. The second set
// is ext.ambient_occlusion's programs (OUR OWN bytes, src/render/common/ao/shaders) through the
// converter, which writes SPIR-V 1.6. `vulkan-shader-check --vulkan1.1` runs spirv-val over every
// retail program the same way (tools/vulkan/shader_check.cpp).
// ============================================================================
#include "unit.h"

#include "render/vulkan/shader_convert.h"
#include "render/vulkan/spirv_legalize.h"

#include "ao/shaders/PSApply.h"
#include "ao/shaders/PSPrepareDepthsAndNormals.h"
#include "ao/shaders/VSFullscreen.h"

#include <initializer_list>
#include <map>
#include <string>
#include <vector>

using namespace VideoVulkan::Shaders;

namespace {

uint32_t Head(uint32_t op, uint32_t words) {
    return (words << 16) | op;
}

// Ids of the synthetic module.
enum : uint32_t {
    kVoid = 1, kFn, kFloat, kPtrIn, kPtrUniform, kBool, kTrue, kIn, kUniform, kMain, kL0, kL1, kL2, kPhi, kBound
};

// `afterDemote`: what follows the demote in its block (the branch to the merge block, or for the refusal
// case an instruction before it).
std::vector<uint32_t> DemoteModule(const std::vector<uint32_t>& afterDemote) {
    std::vector<uint32_t> m = { 0x07230203u, 0x00010600u, 0, kBound, 0 };
    const auto add = [&m](std::initializer_list<uint32_t> words) {
        m.insert(m.end(), words);
    };
    add({ Head(17, 2), 1 });                                     // OpCapability Shader
    add({ Head(17, 2), 5345 });                                  // OpCapability VulkanMemoryModel
    add({ Head(17, 2), 5379 });                                  // OpCapability DemoteToHelperInvocation
    add({ Head(14, 3), 0, 3 });                                  // OpMemoryModel Logical Vulkan
    add({ Head(15, 7), 4, kMain, 0x6E69616Du, 0, kIn, kUniform });   // OpEntryPoint Fragment "main"
    add({ Head(16, 3), kMain, 7 });                              // OpExecutionMode OriginUpperLeft
    add({ Head(5, 3), kIn, 0x78u });                             // OpName %in "x"
    add({ Head(19, 2), kVoid });                                 // OpTypeVoid
    add({ Head(33, 3), kFn, kVoid });                            // OpTypeFunction
    add({ Head(22, 3), kFloat, 32 });                            // OpTypeFloat 32
    add({ Head(32, 4), kPtrIn, 1, kFloat });                     // OpTypePointer Input
    add({ Head(32, 4), kPtrUniform, 2, kFloat });                // OpTypePointer Uniform
    add({ Head(20, 2), kBool });                                 // OpTypeBool
    add({ Head(41, 3), kBool, kTrue });                          // OpConstantTrue
    add({ Head(59, 4), kPtrIn, kIn, 1 });                        // OpVariable Input
    add({ Head(59, 4), kPtrUniform, kUniform, 2 });              // OpVariable Uniform
    add({ Head(54, 5), kVoid, kMain, 0, kFn });                  // OpFunction
    add({ Head(248, 2), kL0 });                                  // OpLabel
    add({ Head(247, 3), kL2, 0 });                               // OpSelectionMerge
    add({ Head(250, 4), kTrue, kL1, kL2 });                      // OpBranchConditional
    add({ Head(248, 2), kL1 });                                  // OpLabel
    add({ Head(5380, 1) });                                      // OpDemoteToHelperInvocation
    m.insert(m.end(), afterDemote.begin(), afterDemote.end());
    add({ Head(248, 2), kL2 });                                  // OpLabel
    add({ Head(245, 7), kBool, kPhi, kTrue, kL0, kTrue, kL1 });  // OpPhi
    add({ Head(253, 1) });                                       // OpReturn
    add({ Head(56, 1) });                                        // OpFunctionEnd
    return m;
}

// Every instruction of `m` by opcode, with its words.
std::multimap<uint32_t, std::vector<uint32_t>> Instructions(const std::vector<uint32_t>& m) {
    std::multimap<uint32_t, std::vector<uint32_t>> out;
    for (size_t at = 5; at < m.size();) {
        const uint32_t words = m[at] >> 16;
        if (words == 0) break;
        out.emplace(m[at] & 0xFFFFu, std::vector<uint32_t>(m.begin() + at, m.begin() + at + words));
        at += words;
    }
    return out;
}

// The name an OpExtension's words carry.
std::string ExtensionName(const std::vector<uint32_t>& ext) {
    std::string name;
    for (size_t i = 1; i < ext.size(); ++i) {
        for (int b = 0; b < 4; ++b) {
            const char c = static_cast<char>((ext[i] >> (8 * b)) & 0xFF);
            if (c != '\0') name.push_back(c);
        }
    }
    return name;
}

}  // namespace

UNIT_TEST(vulkan_downgrade_synthetic_demote) {
    std::vector<uint32_t> m = DemoteModule({ Head(249, 2), kL2 });   // OpBranch to the merge block
    std::string why;
    CHECK(DowngradeSpirv(&m, &why));
    CHECK_EQ(m[1], 0x00010300u);
    const auto ops = Instructions(m);
    CHECK_EQ(ops.count(5380), (size_t)0);   // no demote
    CHECK_EQ(ops.count(252), (size_t)1);    // one OpKill
    CHECK_EQ(ops.count(5), (size_t)0);      // no debug name (the Adreno 640 linker, spirv_legalize.h)
    // The demote capability is gone; the memory model's extension is declared.
    for (auto it = ops.lower_bound(17); it != ops.upper_bound(17); ++it)
        CHECK(it->second[1] != 5379u);
    CHECK_EQ(ops.count(10), (size_t)1);
    CHECK(ExtensionName(ops.find(10)->second) == "SPV_KHR_vulkan_memory_model");
    // The entry point keeps its Input variable only.
    const std::vector<uint32_t>& entry = ops.find(15)->second;
    CHECK_EQ(entry.size(), (size_t)6);
    CHECK_EQ(entry.back(), (uint32_t)kIn);
    // The OpPhi lost the killed block's pair.
    const std::vector<uint32_t>& phi = ops.find(245)->second;
    CHECK_EQ(phi.size(), (size_t)5);
    CHECK_EQ(phi[4], (uint32_t)kL0);
    CHECK(CheckSpirvStructure(m, &why));
}

UNIT_TEST(vulkan_downgrade_refuses_a_demote_mid_block) {
    // An OpNop between the demote and the branch: the block does not end there, so it is not lowered.
    const std::vector<uint32_t> before = DemoteModule({ Head(0, 1), Head(249, 2), kL2 });
    std::vector<uint32_t> m = before;
    std::string why;
    CHECK(!DowngradeSpirv(&m, &why));
    CHECK(!why.empty());
    CHECK(m == before);
}

UNIT_TEST(vulkan_downgrade_leaves_1_3_alone) {
    std::vector<uint32_t> m = DemoteModule({ Head(249, 2), kL2 });
    m[1] = 0x00010300u;
    const std::vector<uint32_t> before = m;
    std::string why;
    CHECK(DowngradeSpirv(&m, &why));
    CHECK(m == before);
}

// WidenInputs: a float3 pixel input at Location 3 fed by a float4 vertex output. `use` is the
// instruction that reads it (a whole OpLoad, or a use the pass does not rewrite).
namespace {

enum : uint32_t { wVoid = 1, wFn, wFloat, wV3, wPtrIn, wIn, wMain, wL0, wVal, wBound };

std::vector<uint32_t> WidenModule(const std::vector<uint32_t>& use) {
    std::vector<uint32_t> m = { 0x07230203u, 0x00010600u, 0, wBound, 0 };
    const auto add = [&m](std::initializer_list<uint32_t> words) {
        m.insert(m.end(), words);
    };
    add({ Head(17, 2), 1 });                                 // OpCapability Shader
    add({ Head(14, 3), 0, 1 });                              // OpMemoryModel Logical GLSL450
    add({ Head(15, 6), 4, wMain, 0x6E69616Du, 0, wIn });     // OpEntryPoint Fragment "main"
    add({ Head(16, 3), wMain, 7 });                          // OpExecutionMode OriginUpperLeft
    add({ Head(71, 4), wIn, 30, 3 });                        // OpDecorate Location 3
    add({ Head(19, 2), wVoid });                             // OpTypeVoid
    add({ Head(33, 3), wFn, wVoid });                        // OpTypeFunction
    add({ Head(22, 3), wFloat, 32 });                        // OpTypeFloat 32
    add({ Head(23, 4), wV3, wFloat, 3 });                    // OpTypeVector 3
    add({ Head(32, 4), wPtrIn, 1, wV3 });                    // OpTypePointer Input
    add({ Head(59, 4), wPtrIn, wIn, 1 });                    // OpVariable Input
    add({ Head(54, 5), wVoid, wMain, 0, wFn });              // OpFunction
    add({ Head(248, 2), wL0 });                              // OpLabel
    m.insert(m.end(), use.begin(), use.end());
    add({ Head(253, 1) });                                   // OpReturn
    add({ Head(56, 1) });                                    // OpFunctionEnd
    return m;
}

}  // namespace

UNIT_TEST(vulkan_widen_pixel_input) {
    std::vector<uint32_t> m = WidenModule({ Head(61, 4), wV3, wVal, wIn });   // OpLoad
    uint32_t widths[32] = {};
    widths[3] = 4;
    uint32_t changed = 0;
    std::string why;
    CHECK(WidenInputs(&m, widths, &changed, &why));
    CHECK_EQ(changed, 1u);
    CHECK(CheckSpirvStructure(m, &why));
    const auto ops = Instructions(m);
    uint32_t v4 = 0;
    for (auto it = ops.lower_bound(23); it != ops.upper_bound(23); ++it) {
        if (it->second[3] == 4) v4 = it->second[1];
    }
    CHECK(v4 != 0);
    uint32_t pointer = 0;
    for (auto it = ops.lower_bound(32); it != ops.upper_bound(32); ++it) {
        if (it->second[2] == 1 && it->second[3] == v4) pointer = it->second[1];
    }
    CHECK(pointer != 0);
    CHECK_EQ(ops.count(59), (size_t)1);
    CHECK_EQ(ops.find(59)->second[1], pointer);   // the variable is the wider vector now
    const std::vector<uint32_t>& load = ops.find(61)->second;
    CHECK_EQ(load[1], v4);
    const std::vector<uint32_t>& shuffle = ops.find(79)->second;   // the old float3 out of it
    CHECK_EQ(shuffle.size(), (size_t)8);
    CHECK_EQ(shuffle[1], (uint32_t)wV3);
    CHECK_EQ(shuffle[2], (uint32_t)wVal);
    CHECK_EQ(shuffle[3], load[2]);
}

UNIT_TEST(vulkan_widen_leaves_other_uses_alone) {
    // OpCopyObject of the variable: not a use the pass rewrites, so nothing changes.
    const std::vector<uint32_t> before = WidenModule({ Head(83, 4), wPtrIn, wVal, wIn });
    std::vector<uint32_t> m = before;
    uint32_t widths[32] = {};
    widths[3] = 4;
    uint32_t changed = 0;
    std::string why;
    CHECK(WidenInputs(&m, widths, &changed, &why));
    CHECK_EQ(changed, 0u);
    CHECK(m == before);
    // A vertex output no wider than the input: nothing to do either.
    widths[3] = 3;
    CHECK(WidenInputs(&m, widths, &changed, &why));
    CHECK_EQ(changed, 0u);
}

UNIT_TEST(vulkan_downgrade_converted_programs) {
    struct Program {
        const uint8_t* bytes;
        size_t size;
        Stage stage;
    };
    const Program programs[] = {
        { g_ao_VSFullscreen, sizeof(g_ao_VSFullscreen), Stage::Vertex },
        { g_ao_PSApply, sizeof(g_ao_PSApply), Stage::Pixel },
        { g_ao_PSPrepareDepthsAndNormals, sizeof(g_ao_PSPrepareDepthsAndNormals), Stage::Pixel },
    };
    for (const Program& p : programs) {
        std::vector<uint32_t> spirv;
        ShaderReflection refl;
        std::string why;
        CHECK(ConvertDxbc(p.bytes, p.size, p.stage, &spirv, &refl, &why));
        CHECK_EQ(spirv[1], 0x00010600u);
        CHECK(DowngradeSpirv(&spirv, &why));
        CHECK_EQ(spirv[1], 0x00010300u);
        CHECK(CheckSpirvStructure(spirv, &why));
        const auto ops = Instructions(spirv);
        CHECK_EQ(ops.count(5380), (size_t)0);
        // dxbc-spirv always declares the Vulkan memory model (and these, PhysicalStorageBufferAddresses).
        bool memoryModel = false;
        for (auto it = ops.lower_bound(10); it != ops.upper_bound(10); ++it)
            memoryModel = memoryModel || ExtensionName(it->second) == "SPV_KHR_vulkan_memory_model";
        CHECK(memoryModel);
    }
}
