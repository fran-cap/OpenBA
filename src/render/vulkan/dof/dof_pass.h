#pragma once
// ============================================================================
// src/render/vulkan/dof/dof_pass.h -- ext.depth_of_field's depth blur on this backend: the device hook
// ============================================================================
// NOT RETAIL. The contract is src/render/common/dof/dof_backend.h; the body dof_pass.cpp.
// ============================================================================

namespace VideoVulkan {
namespace Dof {

// DeviceData::Shutdown, before the shared depth copy's: the pass's images, pipelines, pools and ring.
void Shutdown();

}  // namespace Dof
}  // namespace VideoVulkan
