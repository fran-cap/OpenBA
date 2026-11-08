#pragma once

#include "render_impl.h"   // BANISHED_RENDER_IMPL_BEGIN / _END (several backends in one exe)
// ============================================================================
// src/render/common/decal_fog/decal_fog_backend.h -- fix.decal_fog_order's render side, as the registry
// sees it (docs/retail_bugs.md section 32)
// ============================================================================
// NOT RETAIL. No graphics types: src/ext/registry_boot.cpp includes it. The rewrite is pure byte work
// (decal_fog_rewrite.h); a backend that compiles the installed DXBC (DX11: CreatePixelShader, Vulkan:
// the DXBC -> SPIR-V conversion) asks FixedCopy for each pixel permutation at the program's load and
// uses the copy instead of retail's bytes when it gets one. The row therefore takes effect for the
// programs loaded after a toggle: at the next start.
//
// Supported() is the backend's answer: DX11 and Vulkan true, Metal false (its shader path does not go
// through FixedCopy; the flag is accepted and has nothing to act on).
// ============================================================================

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace DecalFogBackend {

// The row's live atomic (Ext::LiveFix, set by registry_boot.cpp on the main thread once at boot; the
// programs load on the loader threads). Null, or false: retail's bytes.
void SetFixFlag(const std::atomic<bool>* live);

// The backend's answer (one body per backend).
BANISHED_RENDER_IMPL_BEGIN
bool Supported();
BANISHED_RENDER_IMPL_END

// A pixel permutation's corrected copy: true with `out` filled when the row is on and the program is
// one of the decal programs the rewrite matches; false (retail's bytes are used) otherwise. Logs the
// first few rewrites and every refusal of a program that carries the underwater tint (load time only).
bool FixedCopy(const uint8_t* blob, size_t size, std::vector<uint8_t>* out);

}  // namespace DecalFogBackend
