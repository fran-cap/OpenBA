#pragma once
// ============================================================================
// src/render/vulkan/instance_bank.h -- the Vulkan backend's instance-transform bank (issue #253)
// ============================================================================
// NOT RETAIL. src/render/common/draw_batch/instance_bank.h has the contract. Retail's bank is the
// Context's _transforms[52] behind the vertex programs' TransformConstants buffer (declared
// `transforms[52]`, 208 float4, dynamically indexed by SV_InstanceID). The shader side needs no
// rewrite: dxbc-spirv declares every dynamically indexed constant buffer at D3D's full 4096 float4
// (dxbc_resources.cpp handleDclConstantBuffer), so an index past 207 reads the bound range. What
// bounds a draw is the descriptor range (ConstantBufferRange, every constant-buffer descriptor) and
// the bank's storage: slots 52.. live in ContextData::wideBank and BindTransforms uploads
// Context::_transforms[0..51] and them as one bank.
//
// kVulkanInstanceCap is the ONE line a bench variant changes. It is a multiple of 52 so every wide
// batch ends where a retail batch ends (batch_fill.h WideBatchCap), and at most 1024 (4096 float4,
// 64 KiB: D3D's constant-buffer limit and the Adreno 8xx / desktop maxUniformBufferRange). 52 is
// today's path exactly: Cap() is 52, the descriptor range stays 16 KiB and no wide slot is written.
// ============================================================================

#include <cstdint>

namespace VideoVulkan {

// 208 (four retail batches, 13 KiB): the S25 forest bench winner (docs/android/I253_RENDER.md §3).
constexpr int32_t kVulkanInstanceCap = 208;

constexpr int32_t kRetailTransformSlots = 52;      // Context::_transforms[52] (Context.h kTransformSlots)
constexpr int32_t kTransformBytes       = 64;      // one float4x4
constexpr uint32_t kBaseConstantRange   = 16384;   // the range before issue #253 (pipelines.h)
static_assert(kVulkanInstanceCap >= kRetailTransformSlots && kVulkanInstanceCap % kRetailTransformSlots == 0,
              "a wide batch must end where a retail batch ends: a multiple of 52");
static_assert(kVulkanInstanceCap * kTransformBytes <= 65536, "4096 float4 is a D3D constant buffer's limit");

// The cap on this device: kVulkanInstanceCap, cut to the largest multiple of 52 the device's
// maxUniformBufferRange holds (never below 52). 52 before the device exists.
int32_t LiveInstanceCap();

// The range of every constant-buffer descriptor: kBaseConstantRange, or the live cap's bank when that
// is larger.
uint32_t ConstantBufferRange();

}  // namespace VideoVulkan
