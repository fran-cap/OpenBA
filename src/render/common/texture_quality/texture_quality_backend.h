#pragma once

#include "render_impl.h"   // BANISHED_RENDER_IMPL_BEGIN / _END (several backends in one exe)
// ============================================================================
// src/render/common/texture_quality/texture_quality_backend.h -- ext.texture_quality's render side,
// as the extension sees it (docs/extensions/handins/TEXQUALITY.md)
// ============================================================================
// NOT RETAIL. No graphics types (the create_on_demand_backend.h precedent). A backend that supports
// the drop creates a qualifying texture (texture_quality.h DropFor) without its dropped levels: the
// descriptor halved per level, the subresources and pitches from the first kept level, the sampler's
// MaxLOD lowered by the same count. The DX11 body is src/render/dx11/texture_quality/; the Metal
// backend answers "not supported" (its uploads still plan every level of the file), and the
// extension then leaves the drop at 0, so the level readers keep every level.
// ============================================================================

namespace TextureQualityBackend {
BANISHED_RENDER_IMPL_BEGIN

// False: the backend creates every texture from all its levels; the request is ignored.
bool Supported();

BANISHED_RENDER_IMPL_END
}  // namespace TextureQualityBackend
