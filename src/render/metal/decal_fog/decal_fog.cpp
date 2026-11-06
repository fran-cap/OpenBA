// ============================================================================
// src/render/metal/decal_fog/decal_fog.cpp -- fix.decal_fog_order, the Metal side
// ============================================================================
// NOT RETAIL (src/render/common/decal_fog/decal_fog_backend.h, docs/retail_bugs.md section 32):
// not supported. This backend's pixel programs do not go through DecalFogBackend::FixedCopy, so the
// flag is accepted and has nothing to act on: the decal programs stay retail's.
// ============================================================================

#include "decal_fog/decal_fog_backend.h"

namespace DecalFogBackend {

bool Supported() {
    return false;
}

}  // namespace DecalFogBackend
