// ============================================================================
// src/render/dx11/ao/ao_depth_views.h -- the readable main depth: the two backend arms
// ============================================================================
// NOT RETAIL. ext.ambient_occlusion (docs/extensions/AO.md §6.1). Included by
// devicedata.cpp (CreateDepthBuffer) and imagebufferdata.cpp (AttachDepthBuffer)
// only; the extension talks to ao_backend.h. Bodies: ao_pass.cpp.
// ============================================================================
#pragma once

#include <d3d11.h>

#include "render_impl.h"   // BANISHED_RENDER_IMPL_BEGIN / _END (several backends in one exe)

namespace AoBackend {
BANISHED_RENDER_IMPL_BEGIN

// CreateDepthBuffer: whether THIS build of the main depth is the readable one.
// Reads the extension's request (ao_backend.h) and records it as what exists.
bool BuildReadableDepth();

// AttachDepthBuffer's readable arm, on the typeless texture: the DSV with an
// explicit D24_UNORM_S8_UINT desc (TEXTURE2DMS / TEXTURE2D; a null desc is
// illegal on a typeless resource) and the R24_UNORM_X8_TYPELESS SRV, which goes
// into ImageBufferData::shaderResourceView (retail leaves it null for the depth
// alias), so the tracked-image registry releases and rebuilds it with the
// depth: ~ImageBufferData / ReleaseDynamic / RecreateDynamic.
void CreateMainDepthViews(ID3D11Device* device, ID3D11Texture2D* depth,
                          ID3D11DepthStencilView** dsv, ID3D11ShaderResourceView** srv);

BANISHED_RENDER_IMPL_END
}  // namespace AoBackend
