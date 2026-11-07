// ============================================================================
// src/render/metal/better_clouds/cloud_shadow.cpp -- ext.better_clouds Fancy's shadow pass: not supported on Metal
// ============================================================================
// NOT RETAIL. The contract is src/render/common/better_clouds/cloud_shadow_backend.h
// (docs/extensions/handins/BETTERCLOUDS.md section 2.6). The pass is DX11's and Vulkan's
// (src/render/{dx11,vulkan}/better_clouds/cloud_shadow.cpp): here Supported() answers false and Draw is a no-op, so
// Fancy draws the sky's clouds and no shadows.
// ============================================================================
#include "better_clouds/cloud_shadow_backend.h"

namespace CloudShadowBackend {

bool Supported() { return false; }

void Request(bool /*on*/) {}

void Draw(Video::Context& /*context*/, const Video::ImageBuffer* /*noise*/, const Frame& /*frame*/) {}

}  // namespace CloudShadowBackend
