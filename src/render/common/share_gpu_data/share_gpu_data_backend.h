#pragma once

#include "render_impl.h"   // BANISHED_RENDER_IMPL_BEGIN / _END (several backends in one exe)
// ============================================================================
// src/render/common/share_gpu_data/share_gpu_data_backend.h -- ext.share_gpu_data's render side, as
// the extension sees it (docs/extensions/handins/SHAREGPUDATA.md, issue #13)
// ============================================================================
// NOT RETAIL. No graphics types: src/ext/share_gpu_data/share_gpu_data.cpp includes it (the
// compress_ao_backend.h precedent). The DX11 body is src/render/dx11/share_gpu_data/, the Metal body
// src/render/metal/share_gpu_data/; the backend-neutral table is share_table.h beside this file.
//
// Retail creates one GPU object per engine object, even when two resource files carry the very same
// mesh or texture. With the row on, a backend that supports it creates each immutable, data-seeded
// vertex buffer, index buffer and file-seeded texture once per distinct (descriptor, bytes) and
// hands the same object to every engine object that asks for an equal one. Nothing can write an
// immutable object, so no user sees another's change.
// ============================================================================

namespace ShareGpuDataBackend {
BANISHED_RENDER_IMPL_BEGIN

// The extension's one call (Ext::ShareGpuData::Boot, main thread, once at boot: the row takes effect
// at the next start). Off: one object per create, retail's.
void Request(bool on);

// False when the backend has no shared path (none today: DX11 and Metal both have one): the request is
// then ignored.
bool Supported();

BANISHED_RENDER_IMPL_END
}  // namespace ShareGpuDataBackend
