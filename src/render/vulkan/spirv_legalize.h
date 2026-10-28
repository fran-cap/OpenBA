#pragma once
// ============================================================================
// src/render/vulkan/spirv_legalize.h -- OpBitFieldInsert for the Adreno driver; SPIR-V 1.3 for the 1.1 path
// ============================================================================
// NOT RETAIL (P8 round 2, docs/android/P8_VULKAN.md §6). The Adreno 830 driver (1.3.284, shader compiler
// E031.47.18.56) fails vkCreateGraphicsPipelines with VK_ERROR_UNKNOWN ("AdrenoVK: Shader compilation
// failed for shaderType: 0") on a vertex shader holding OpBitFieldInsert: ext.ambient_occlusion's
// VSFullscreen (dxbc-spirv's form of SM5 `bfi`), so the AO pass never ran on the S25. No retail program
// has one (vulkan-shader-check). This pass rewrites each OpBitFieldInsert whose offset and count are
// 32-bit constants as the same bit operation in shifts and masks:
//   result = ((insert << offset) & mask) | (base & ~mask),   mask = ((1 << count) - 1) << offset
// An insert with a non-constant offset or count, or on a vector, is left as it is.
//
// No Vulkan header, no device (tests/unit/vulkan_ao_convert_test.cpp, vulkan_shader_convert_test.cpp).
// ============================================================================
#include <cstdint>
#include <string>
#include <vector>

namespace VideoVulkan {
namespace Shaders {

// Rewrites `spirv` in place. *changed counts the instructions rewritten. False with *why on a module
// the pass cannot read (left unchanged then).
bool LowerBitFieldInsert(std::vector<uint32_t>* spirv, uint32_t* changed, std::string* why);

// THE 1.1 PATH (src/render/vulkan/vulkan11.h). dxbc-spirv writes SPIR-V 1.6 (and glslc the present
// pass's 1.6), which a Vulkan 1.1 device does not accept (1.3 at most; 1.4 with VK_KHR_spirv_1_4).
// This pass rewrites a module to SPIR-V 1.3 with the same meaning, as far as the backend's modules go:
//   - the version word becomes 1.3;
//   - OpEntryPoint lists only its Input / Output variables (1.4 widened the list to every global);
//   - the VulkanMemoryModel and PhysicalStorageBufferAddresses capabilities get their OpExtension
//     (core in 1.5; the device enables VK_KHR_vulkan_memory_model, vulkan11.h);
//   - OpDemoteToHelperInvocation (D3D's discard, core in 1.6) becomes OpKill, which ends the block, so
//     only a demote followed by nothing but the block's OpBranch / OpReturn is lowered; any other
//     shape fails the module. An OpPhi naming the killed block as a parent drops that pair. The
//     difference: after a demote a pixel keeps running as a helper (its derivatives stay defined for
//     its quad neighbours); after OpKill it stops. The 1.3 path keeps the demote;
//   - OpTerminateInvocation (1.6) becomes OpKill;
//   - the debug names (OpName, OpMemberName) are dropped. Not a version rule: the Galaxy S10's driver
//     (Adreno 640, 1.1.128) fails vkCreateGraphicsPipelines with VK_ERROR_UNKNOWN ("Failed to link
//     shaders. Assertion failed: (*mSymbolObjectMap)[gv] == NULL") on a vertex module whose input and
//     output share a name (dxbc-spirv names both after their semantic: TEXCOORD in, TEXCOORD out).
// False with *why (left unchanged) for a module it cannot read or lower.
bool DowngradeSpirv(std::vector<uint32_t>* spirv, std::string* why);

// THE 1.1 PATH, the pair's linked pixel module (pipelines.h). D3D links a vertex output wider than the
// pixel input it feeds (float4 written, float3 read); Vulkan allows it only with maintenance4, which the
// 1.3 path enables and a 1.1 device does not have (VUID-RuntimeSpirv-maintenance4-06817). This pass
// widens each plain pixel input at component 0 to the width of the vertex output at its Location
// (`widths[location]`, 0 for none): the variable's type becomes the wider vector, each whole OpLoad
// loads it and takes the old components back (OpVectorShuffle / OpCompositeExtract), access chains
// stay as they are. An input used any other way is left as it is. *changed counts the inputs widened.
bool WidenInputs(std::vector<uint32_t>* spirv, const uint32_t widths[32], uint32_t* changed, std::string* why);

}  // namespace Shaders
}  // namespace VideoVulkan
