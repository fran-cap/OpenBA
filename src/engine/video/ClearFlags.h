#pragma once
// ============================================================================
// src/engine/video/ClearFlags.h — Video::ClearFlags, what Context::Clear clears
// ============================================================================
//
// The first argument of Video::Context::Clear (0x1800f23f0), which tail-jumps
// to the backend's ContextData::Clear (slot 4). The DX11 backend maps the bits
// to ClearRenderTargetView / D3D11_CLEAR_DEPTH / D3D11_CLEAR_STENCIL
// (render/dx11/contextdata.cpp); the Vulkan and Metal backends test the same
// bits.
// ============================================================================

#include <cstdint>
#include "../system/EnumFlags.h"

namespace Video {

enum class ClearFlags : uint32_t {
    Color   = 0x1,   // the colour target (ClearRenderTargetView)
    Depth   = 0x2,   // depth (D3D11_CLEAR_DEPTH)
    Stencil = 0x4,   // stencil (D3D11_CLEAR_STENCIL)
    All     = 0x7,   // Color | Depth | Stencil
};
OPENBA_ENUM_FLAGS(ClearFlags)

}  // namespace Video
