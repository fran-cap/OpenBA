// ============================================================================
// src/render/vulkan/texture_quality/texture_quality_vulkan.cpp -- ext.texture_quality, the Vulkan answer
// (docs/extensions/handins/TEXQUALITY.md)
// ============================================================================
// NOT RETAIL (P4). The drop itself is in ImageBufferData::Create (companions.cpp, its EXT SEAM lines):
// the image made at the first kept level's size with that many fewer mips, the level blobs from the
// first kept one, both samplers' maxLod lowered by the same count -- the DX11 body's three changes
// (src/render/dx11/imagebufferdata.cpp). The deferred creates (ext.create_on_demand's records,
// ext.async_gpu_create's jobs) receive the already-dropped shape and levels, and ext.compress_ao /
// ext.share_gpu_data see the smaller texture as any other.
// ============================================================================

#include "texture_quality/texture_quality_backend.h"

namespace TextureQualityBackend {
BANISHED_RENDER_IMPL_BEGIN

bool Supported() {
    return true;
}

BANISHED_RENDER_IMPL_END
}  // namespace TextureQualityBackend
