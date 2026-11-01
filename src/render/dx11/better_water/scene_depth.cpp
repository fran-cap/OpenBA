// ============================================================================
// src/render/dx11/better_water/scene_depth.cpp -- ext.better_water's scene depth copy on this backend
// ============================================================================
// NOT RETAIL. The contract is src/render/common/better_water/scene_depth_backend.h. Not supported on
// this backend yet: Fancy draws no contact foam here (the shore pass and the strip are unaffected).
// ============================================================================
#include "better_water/scene_depth_backend.h"

namespace SceneDepthBackend {

bool Supported() { return false; }

void Request(bool /*on*/) {}

void Copy(void* /*context*/) {}

const Video::ImageBuffer* Image() { return nullptr; }

}  // namespace SceneDepthBackend
