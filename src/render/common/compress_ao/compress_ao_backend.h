#pragma once

#include "render_impl.h"   // BANISHED_RENDER_IMPL_BEGIN / _END (several backends in one exe)
// ============================================================================
// src/render/common/compress_ao/compress_ao_backend.h -- ext.compress_ao's render side, as the
// extension sees it (docs/extensions/handins/COMPRESSAO.md, issue #13)
// ============================================================================
// NOT RETAIL. No graphics types: src/ext/compress_ao/compress_ao.cpp includes it (the
// async_gpu_create_backend.h precedent). The DX11 body is src/render/dx11/compress_ao/bc4_create.cpp,
// the Metal body src/render/metal/compress_ao/bc4_create.mm; the backend-neutral encode (and its use of
// the derived cache) is bc4_chain.h beside this file.
//
// Retail loads the baked per-material ambient-occlusion maps (WinData/Models/**/MaterialInstance/
// *AO_resource.crs) as R8_UNORM textures. With the row on, a backend that supports it creates every
// mipmapped 2D R8 texture seeded from a resource file as BC4_UNORM instead: the same single channel
// in half the GPU and process memory, encoded at load (bc4_chain.h) or found in the derived-data
// cache. The engine's ImageBuffer still says R8 (nothing reads these textures back).
// ============================================================================

namespace Ext {
namespace DerivedCache {
class Store;
}
}  // namespace Ext

namespace CompressAoBackend {
BANISHED_RENDER_IMPL_BEGIN

// The extension's one call (Ext::CompressAo::Boot, main thread, once at boot: the row takes effect
// at the next start). `cache` may be null (no cache: every texture is encoded). Off: retail's R8.
void Request(bool on, Ext::DerivedCache::Store* cache);

// False when the backend cannot create BC4 textures from encoded levels (Metal on a device without
// BC texture compression): the request is then ignored and the textures stay R8.
bool Supported();

BANISHED_RENDER_IMPL_END
}  // namespace CompressAoBackend
