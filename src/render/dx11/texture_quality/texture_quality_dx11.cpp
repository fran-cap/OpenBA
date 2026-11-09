// ============================================================================
// src/render/dx11/texture_quality/texture_quality_dx11.cpp -- ext.texture_quality, the DX11 answer
// (docs/extensions/handins/TEXQUALITY.md)
// ============================================================================
// NOT RETAIL. The drop itself is in ImageBufferData::Create (imagebufferdata.cpp, its EXT SEAM
// lines): the descriptor at the first kept level, the subresources from it, the sampler's MaxLOD.
// The deferred creates (ext.create_on_demand's records, ext.async_gpu_create's jobs) receive the
// already-dropped descriptor and levels, and ext.compress_ao / ext.share_gpu_data see the smaller
// texture as any other.
// ============================================================================

#include "texture_quality/texture_quality_backend.h"

namespace TextureQualityBackend {
BANISHED_RENDER_IMPL_BEGIN

bool Supported() {
    return true;
}

BANISHED_RENDER_IMPL_END
}  // namespace TextureQualityBackend
