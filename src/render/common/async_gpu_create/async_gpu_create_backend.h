#pragma once

#include "render_impl.h"   // BANISHED_RENDER_IMPL_BEGIN / _END (several backends in one exe)
// ============================================================================
// src/render/common/async_gpu_create/async_gpu_create_backend.h -- ext.async_gpu_create's render side,
// as the extension sees it (docs/extensions/handins/ASYNCGPUCREATE.md, issue #13)
// ============================================================================
// NOT RETAIL. No graphics types: src/ext/async_gpu_create/async_gpu_create.cpp includes it (the
// billboard_shadow_backend.h precedent). The DX11 body is src/render/dx11/async_gpu_create/.
//
// Retail creates every GPU resource synchronously inside its Serialize, on the loader thread. With
// the row on, a backend that supports it hands the loader's data-seeded, immutable creates (vertex /
// index buffers and plain textures read from a resource file) to worker threads while the loader
// keeps deserialising. The same resources with the same contents and the same failure state; every
// one of them exists before the load ends (the loader bracket's close waits for them), and any use
// of one before that waits for its own create first.
// ============================================================================

namespace AsyncGpuCreateBackend {
BANISHED_RENDER_IMPL_BEGIN

// The extension's one call (Ext::AsyncGpuCreate::Sync, main thread). Idempotent. Takes effect at the
// next load the loader starts; a load already running keeps the mode it started with.
void Request(bool on);

// False when the backend has no worker path, or the device's driver reports no concurrent
// creates (D3D11_FEATURE_THREADING.DriverConcurrentCreates = 0): the creates then stay on the
// loader, retail's path. Known only once a device exists; false before.
bool Supported();

BANISHED_RENDER_IMPL_END
}  // namespace AsyncGpuCreateBackend
