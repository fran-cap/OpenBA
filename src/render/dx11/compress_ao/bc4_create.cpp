// ============================================================================
// src/render/dx11/compress_ao/bc4_create.cpp -- ext.compress_ao, the DX11 side (NOT RETAIL)
// ============================================================================
// bc4_create.h has the contract; docs/extensions/handins/COMPRESSAO.md.
// ============================================================================
#include "compress_ao/bc4_create.h"

#include "compress_ao/bc4_chain.h"
#include "bc4/bc4.h"
#include "compress_ao/compress_ao_backend.h"

#include <atomic>
#include <cstdio>

namespace VideoDX11 {
namespace CompressAo {

namespace {
std::atomic<bool> g_requested{false};
}  // namespace

bool Requested() {
    return g_requested.load(std::memory_order_relaxed);
}

bool ConvertForCreate(D3D11_TEXTURE2D_DESC* desc, D3D11_SUBRESOURCE_DATA* subres, int count,
                      std::vector<uint64_t>* sizes, uint32_t sourceFormat, std::vector<uint8_t>* bc4) {
    if (subres == nullptr || count <= 0) return false;
    std::vector<::CompressAo::Level> in(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) {
        in[i].data = static_cast<const uint8_t*>(subres[i].pSysMem);
        in[i].size = static_cast<int64_t>((*sizes)[i]);
    }
    const int width  = static_cast<int>(desc->Width);
    const int height = static_cast<int>(desc->Height);
    std::vector<size_t> offsets;
    if (!::CompressAo::Encode(in.data(), count, width, height, sourceFormat, bc4, &offsets)) return false;
    desc->Format = DXGI_FORMAT_BC4_UNORM;
    for (int i = 0; i < count; ++i) {
        const int levelWidth = (width >> i) > 0 ? (width >> i) : 1;
        subres[i].pSysMem     = bc4->data() + offsets[i];
        subres[i].SysMemPitch = static_cast<UINT>(Bc4::BlockRowPitch(levelWidth));
        (*sizes)[i]           = offsets[i + 1] - offsets[i];
    }
    return true;
}

}  // namespace CompressAo
}  // namespace VideoDX11

// ---- the backend-neutral interface (compress_ao_backend.h) ----------------------------------------
namespace CompressAoBackend {
BANISHED_RENDER_IMPL_BEGIN

// BC4_UNORM is required of every D3D11 device (feature level 10_0 and up), so DX11 always supports it.
void Request(bool on, Ext::DerivedCache::Store* cache) {
    ::CompressAo::SetCache(on ? cache : nullptr);
    VideoDX11::CompressAo::g_requested.store(on, std::memory_order_relaxed);
    std::fprintf(stderr, "[CompressAo] %s (derived cache %s)\n", on ? "on: R8 AO maps load as BC4" : "off",
                 on && cache != nullptr ? "on" : "off");
}

bool Supported() {
    return true;
}

BANISHED_RENDER_IMPL_END
}  // namespace CompressAoBackend
