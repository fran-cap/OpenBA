// ============================================================================
// src/render/vulkan/decal_fog/decal_fog.cpp -- fix.decal_fog_order, the Vulkan side
// ============================================================================
// NOT RETAIL (src/render/common/decal_fog/decal_fog_backend.h, docs/retail_bugs.md section 32):
// the DXBC -> SPIR-V conversion takes the corrected copy (src/render/vulkan/companions.cpp,
// CreateProgram), so the registry entry, its hash and its cache record are the copy's.
// ============================================================================

#include "decal_fog/decal_fog_backend.h"

namespace DecalFogBackend {
BANISHED_RENDER_IMPL_BEGIN

bool Supported() {
    return true;
}

BANISHED_RENDER_IMPL_END
}  // namespace DecalFogBackend
