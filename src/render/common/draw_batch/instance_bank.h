#pragma once
// ============================================================================
// src/render/common/draw_batch/instance_bank.h -- ext.draw_batch's wide instancing batches, as the
// extension sees the backend (docs/extensions/handins/DRAWBATCH.md, issue #253 "instance cap")
// ============================================================================
// NOT RETAIL. Retail's instancing batch takes at most the mesh level's _maxInstances items (52 in the
// game's data), which is Video::Context::_transforms[52] and the VS's TransformConstants bank. A backend
// whose transform bank can hold more lets ext.draw_batch's copy of DrawItemList join consecutive retail
// batches into one draw where that draws the same instances with the same state (batch_fill.h,
// ContinuesPastCut). Slots 0..51 stay the Context's own (Context::SetTransform); the slots past them
// live in the backend (SetWide), which uploads both as one bank when the batch is wider than 52.
//
// DX11 and Metal answer "not supported": Cap() is 52, so the extension never asks for a wide slot.
// The Vulkan backend's cap is kVulkanInstanceCap (src/render/vulkan/instance_bank.h), clamped to the
// device's maxUniformBufferRange. No D3D / Vulkan types here: src/ext/draw_batch/draw_batch.cpp
// includes it (the smoke_vs_backend.h precedent).
// ============================================================================

#include <cstdint>

namespace Math {
class Matrix;
}
namespace Video {
class Context;
}

namespace InstanceBank {

// The instance transforms one instanced draw can take on this backend: 52 (the Context's own bank)
// unless the backend holds more. Constant once the device exists. Any thread.
int32_t Cap();

// Slot `slot` (52 <= slot < Cap()) of the batch being built on `context`: stored in the backend's wide
// bank with SetTransform's tail (the transform-dirty bit, the high-water mark of _transformCount).
// Never called while Cap() is 52. The emitting thread only.
void SetWide(Video::Context& context, int32_t slot, const Math::Matrix& m);

}  // namespace InstanceBank
