// ============================================================================
// src/render/dx11/decal_fog/decal_fog.cpp -- fix.decal_fog_order, the DX11 side
// ============================================================================
// NOT RETAIL (src/render/common/decal_fog/decal_fog_backend.h, docs/retail_bugs.md section 32):
// CreatePixelShader takes the corrected copy (src/render/dx11/shaderprograms.cpp,
// PixelProgramData::PlatformCreate).
// ============================================================================

#include "decal_fog/decal_fog_backend.h"

namespace DecalFogBackend {

bool Supported() {
    return true;
}

}  // namespace DecalFogBackend
