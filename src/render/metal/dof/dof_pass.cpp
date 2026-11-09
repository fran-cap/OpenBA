// ============================================================================
// src/render/metal/dof/dof_pass.cpp -- ext.depth_of_field's depth blur: not supported on Metal
// ============================================================================
// NOT RETAIL. The contract is src/render/common/dof/dof_backend.h (docs/extensions/DOF.md). The pass is
// DX11's and Vulkan's (src/render/{dx11,vulkan}/dof/dof_pass.cpp): here Supported() answers false and Draw
// is a no-op, so the scene stays sharp.
// ============================================================================
#include "dof/dof_backend.h"

namespace DofBackend {

bool Supported() { return false; }

void Request(bool /*on*/) {}

void Draw(Video::Context& /*context*/, const Frame& /*frame*/) {}

}  // namespace DofBackend
