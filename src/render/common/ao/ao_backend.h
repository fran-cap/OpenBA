// ============================================================================
// src/render/common/ao/ao_backend.h -- what the AO extension asks of the render backend
// ============================================================================
// NOT RETAIL. ext.ambient_occlusion (docs/extensions/AO.md §6.1, §6.4;
// handins/AOCORE.md). No D3D types: src/ext/ambient_occlusion/ambient_occlusion.cpp includes it
// (only the backend TUs see a graphics API's headers). Bodies: src/render/dx11/ao/ao_pass.cpp,
// src/render/vulkan/integration.cpp, src/render/metal/integration.mm.
// ============================================================================
#pragma once

#include "render_impl.h"   // BANISHED_RENDER_IMPL_BEGIN / _END (several backends in one exe)

namespace AoBackend {
BANISHED_RENDER_IMPL_BEGIN

// True when the live device can run ASSAO (D3D11: feature level 11_0 or better, ASSAO's
// shaders are ps_5_0). False before the device exists.
bool Supported();

// The main depth's format for the NEXT (re)build. D3D11: readable (R24G8_TYPELESS,
// DEPTH_STENCIL | SHADER_RESOURCE, an explicit D24S8 DSV and an R24_UNORM_X8 SRV)
// or retail's (typed D24_UNORM_S8_UINT, DEPTH_STENCIL only); when the request
// differs from the depth that exists, raises Video::Device::settingsChanged so
// the next BeginCommands runs the rebuild path (DeviceData_Resize: the
// swap-chain resize, CreateDepthBuffer, RecreateDynamic -> AttachDepthBuffer):
// the same path a window resize or an Options Apply takes. Vulkan: the main
// targets are rebuilt at the next frame (draw.cpp EnsureMainTargets). Metal: the
// depth is created readable already. Main thread.
void RequestReadableDepth(bool on);

// The depth that exists (D3D11: devicedata.cpp CreateDepthBuffer records it).
bool DepthReadable();

BANISHED_RENDER_IMPL_END
}  // namespace AoBackend
