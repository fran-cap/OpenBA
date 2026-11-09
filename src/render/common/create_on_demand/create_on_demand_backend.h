#pragma once

#include "render_impl.h"   // BANISHED_RENDER_IMPL_BEGIN / _END (several backends in one exe)
// ============================================================================
// src/render/common/create_on_demand/create_on_demand_backend.h -- ext.create_on_demand's render
// side, as the extension sees it (docs/extensions/handins/CREATEONDEMAND.md, issue #13)
// ============================================================================
// NOT RETAIL. No graphics types (the share_gpu_data_backend.h precedent). The DX11 body is
// src/render/dx11/create_on_demand/, the Metal body src/render/metal/create_on_demand/; the
// backend-neutral parts are beside this file (payload.h, lazy_table.h, stream_locate.h, pending_levels.h).
//
// Retail creates every static GPU buffer and texture while its resource file loads: a MegaMod
// set makes ~9.8 GB of them, of which well under 1 % is ever drawn in a session. With the row on, a
// backend that supports it records, instead of creating it, each immutable vertex / index buffer
// and file-seeded texture whose bytes it can read again from the mapped package, skips those bytes
// in the load, and creates the object at its first use (or earlier, when the extension prefetches
// the meshes the world places).
// ============================================================================

namespace CreateOnDemandBackend {
BANISHED_RENDER_IMPL_BEGIN

// The extension's one call (Ext::CreateOnDemand::Boot, main thread, once at boot, before the first
// load: the row takes effect at the next start). Off: every object is created at load, retail's.
// `idleMinutes` > 0: a made object unused that long is given back and re-made at its next use.
void Request(bool on, int idleMinutes);

// False when the backend has no deferred path (none today: DX11 and Metal both have one): the request is
// then ignored.
bool Supported();

// Queue the deferred create of an engine companion (a Video::VertexBuffer / IndexBuffer's platform
// data, a Video::ImageBuffer's) for a prefetch worker. A no-op for any object that has none.
void Prefetch(const void* companion);

// Prefetch, and the object is never given back by the idle eviction: what is drawn many at a time
// when a window opens (the UI's sprite sheets, the cursors).
void Keep(const void* companion);

// True when a vertex buffer's companion holds its GPU object (made at load, by a prefetch or a use,
// and not given back since). One load, no lock: the draw gather asks it for every mesh it draws, and
// a stale answer costs one extra queueing at most.
bool VertexBufferMade(const void* companion);
// The same for an image's companion (its shader view).
bool ImageMade(const void* companion);

BANISHED_RENDER_IMPL_END
}  // namespace CreateOnDemandBackend
