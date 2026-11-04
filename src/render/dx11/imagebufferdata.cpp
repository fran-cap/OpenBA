// VideoDX11 `ImageBufferData` — all seven vtable slots (0x180018790).
//
// Lane L3. This is the largest of the eleven resource classes (0x38 bytes, five
// COM interfaces) and the only one that touches texture creation, formats, mip
// levels, cube maps, compressed data, render targets and depth buffers.
//
// Contents
// --------
//   0x180002400  slot 0  scalar deleting dtor
//   0x180002430  (private) ~ImageBufferData — untrack + release five interfaces
//   0x180002580  slot 1  alias the DeviceData color buffer, build an RTV
//   0x180002650  slot 2  alias the DeviceData depth buffer, build a DSV
//   0x180002870  slot 3  create the texture and all its views
//   0x180002720  (private) create the sampler state — called by slot 3
//   0x180002da0  slot 4  bind sampler + SRV to a pixel-shader slot
//   0x180002e10  slot 5  upload pixels (dynamic Map, or UpdateSubresource)
//   0x180002ef0  slot 6  read pixels back through a STAGING texture
//   0x180002300  (shared with DeviceData) TrackImageBuffer — see below
//
// 0x180002300 is DeviceData's, not ours — but it lives here
// ---------------------------------------------------------
// `DeviceData::TrackImageBuffer(Video::ImageBuffer*)` appends (add-if-absent)
// to the array at DeviceData+0x60 `{void* data; int count; int capacity}`,
// stride 8, x3 growth. That array is the registry of Video::ImageBuffers that
// must be released and recreated across a device / swap-chain reset.
//
// It is called from ImageBufferData slots 1, 2 and 3, and the matching *remove*
// lives inside ~ImageBufferData (0x180002430) — i.e. three of the four call
// sites are in this file. So it is implemented here to keep these slots
// self-contained. **Lane L6 owns DeviceData and will need exactly this helper**
// (DeviceData slot 0 frees the array at teardown); when L6 lands, hoist
// TrackImageBuffer into the DeviceData translation unit and have this file call
// it instead. Until then there is one copy and it is here.
//
// Note that the *remove* in ~ImageBufferData is a **stable shift-erase**, not a
// swap-remove — src/render/dx11/README.md and decomp/videodx11.md both say "swap-remove",
// which is wrong. 0x1800024c8-0x1800024ee shifts every later element down by one
// (`MOV RCX,[R8+8]; MOV [R8],RCX`) and only then decrements the count. Corrected
// here; the two docs still carry the old wording.
//
// Membership differs between the two sites and that asymmetry is real:
//   ~ImageBufferData (remove):  IsGenerated() || (flags & 0x70) || IsRendered()
//   slot 3           (add):     IsGenerated() || IsRendered() || (flags & 0x40)
// The dtor also honours bits 4 and 5 (set by slots 1 and 2, which add
// unconditionally); slot 3 only honours bit 6. Reproduced verbatim.
//
// Reaching D3D11
// --------------
// ImageBufferData never caches the device. Every slot walks:
//
//     Video::Device::_singleton  ->  Video::Device*
//        [+0x100]                ->  active Video::DeviceData*
//        [+0x20]                 ->  ID3D11Device*
//        [+0x28]                 ->  ID3D11DeviceContext*
//
// The sampler helper additionally reads `Video::Configuration::_singleton`
// (a *different* import — 0x180013480 vs 0x1800134b8; Ghidra renders both as
// `_singleton_exref`, which is how the anisotropy field got misattributed at
// first glance). Configuration+0x28 is the requested anisotropy, Device+0x98 is
// the hardware cap.
//
// COM vtable indices
// ------------------
// Every call below goes through the mingw-w64 <d3d11.h> C++ interface rather
// than a hand-rolled offset, and every one was checked against the byte offset
// in the shipped code:
//
//   ID3D11Device        +0x28 = index  5 = CreateTexture2D          (0x180002c46)
//                       +0x38 = index  7 = CreateShaderResourceView (0x180002cf6)
//                       +0x48 = index  9 = CreateRenderTargetView   (0x18000262a)
//                       +0x50 = index 10 = CreateDepthStencilView   (0x180002d47)
//                       +0xb8 = index 23 = CreateSamplerState       (0x18000285b)
//   ID3D11DeviceContext +0x40 = index  8 = PSSetShaderResources     (0x180002e00)
//                       +0x50 = index 10 = PSSetSamplers            (0x180002ddc)
//                       +0x70 = index 14 = Map                      (0x180002e79)
//                       +0x78 = index 15 = Unmap                    (0x180002ea1)
//                       +0x178= index 47 = CopyResource             (0x180002f9a)
//                       +0x180= index 48 = UpdateSubresource        (0x180002ec7)
//   IUnknown            +0x08 = index  1 = AddRef                   (0x180002612)
//                       +0x10 = index  2 = Release                  (0x180002513)
//
// MinGW GCC lays out a single-inheritance pure-abstract COM interface exactly
// as MSVC does on Win64, so `dev->CreateTexture2D(...)` emits `call [rax+0x28]`.
//
// Video::ImageBuffer field map (read directly, offsets from the shipped code)
// --------------------------------------------------------------------------
//   +0x20 int    width          (also the mip-chain restart width in slot 3)
//   +0x24 int    height
//   +0x28 int    dimension      0 = 2D, non-zero (1) = cube  -> ArraySize 6,
//                               MiscFlags TEXTURECUBE, SRV dimension TEXTURECUBE
//   +0x2c int    kind           drives BindFlags / Usage / CPUAccessFlags:
//                                 1     -> BIND_DEPTH_STENCIL
//                                 0,4,5 -> BIND_RENDER_TARGET
//                                 3     -> USAGE_DYNAMIC + CPU_ACCESS_WRITE
//                                 2 (and anything with a bit outside 0x5 set)
//                                       -> no extra bind flag
//                               The names of these five values are NOT
//                               recoverable from this DLL — the arithmetic is
//                               reproduced literally rather than guessed at.
//   +0x30 int    format         index into the two 8-entry tables below
//   +0x34 uint   flags          bit0 = mipmapped, bit4/bit5 = set by slots 1/2,
//                               bit6 = comparison/shadow sampler
//   +0x38 int    addressU       index into {WRAP, CLAMP}
//   +0x3c int    addressV
//   +0x40 int    addressW
//   +0x44 int    filter         index into {POINT, LINEAR, ANISOTROPIC}
//
// Engine format enum -> DXGI_FORMAT (0x1800029e0..0x180002a11, and again at
// 0x180002c89 for the cube SRV; both tables are byte-identical), with the
// companion bytes-per-pixel-or-block table at 0x180002adc..0x180002b07:
//
//   idx | DXGI                                    | bytes/px-or-block
//   ----+-----------------------------------------+------------------
//    0  | 0x47 = 71 DXGI_FORMAT_BC1_UNORM         |  8   (block)
//    1  | 0x4d = 77 DXGI_FORMAT_BC3_UNORM         | 16   (block)
//    2  | 0x4d = 77 DXGI_FORMAT_BC3_UNORM         | 16   (block)
//    3  | 0x41 = 65 DXGI_FORMAT_A8_UNORM          |  1
//    4  | 0x3d = 61 DXGI_FORMAT_R8_UNORM          |  1
//    5  | 0x57 = 87 DXGI_FORMAT_B8G8R8A8_UNORM    |  4
//    6  | 0x29 = 41 DXGI_FORMAT_R32_TYPELESS      |  4
//    7  | 0x2c = 44 DXGI_FORMAT_R24G8_TYPELESS    |  4
//
// Index 5 is the backbuffer format (slot 1 stamps format=5) and index 7 is the
// depth format (slot 2 stamps format=7) — those two are confirmed against the
// DeviceData buffer creators. Indices 1 and 2 BOTH map to BC3; index 1 was very
// likely DXT3/BC2 in the engine enum and got folded onto BC3 in this backend.
// That is an observation about the shipped table, not a guess: the two entries
// really are the same immediate. Reproduced as-is.
//
// Standard: observable-accurate
// ----------------------------
// Identical D3D11/DXGI calls, identical arguments, identical order, identical
// struct layout. Where the original omits an error check, so does this. Where
// it releases in an odd order, so does this. Two things are deliberately NOT
// invented and are called out at their use sites:
//   * the second parameter of slot 4 (never read by the original),
//   * the meaning of the 16-byte level descriptors slot 3 receives (only the
//     leading pointer is read).
//
// See ../README.md for the seam, and ../types/VideoDX11Data.h for the layout.
//
// B5 RES (the dx11 seam): `VideoDX11::ImageBufferData` is a real C++ subclass
// of `Video::ImageBufferData` (../types/VideoDX11Data.h); the seven slots are
// members declared `override` and the compiler builds the vtable. The field map
// above is read through the NAMED members of the real `Video::ImageBuffer`
// (`_owner`, typed) — the `IbI32` / `IbU32` / `IbSet` raw-offset accessors are
// gone — and the Device / Configuration reads through `Video::Device::
// backBufferWidth / backBufferHeight / maxAnisotropy` and `Video::Configuration::
// textureFilter`. The two "not invented" items above are now named by their
// callers: slot 4's second parameter is the `Video::Context` (CompanionData.h),
// and slot 3's level descriptors are `Video::ImageBuffer::LevelArray`, a
// `Collection::Array<Collection::Array<uint8_t>>` (only each element's leading
// `_data` pointer is read here).

#include "dx11_backend_access.h"   // Dx11DeviceData / Dx11Device / Dx11Context, <d3d11.h>
#include "VideoDX11Data.h"
#include "ao/ao_backend.h"       // NOT RETAIL: ext.ambient_occlusion (AttachDepthBuffer's readable arm)
#include "ao/ao_depth_views.h"
#include "system/MemoryAllocator.h"   // B.3R DX: MemAlloc / MemFree, direct
#include "async_gpu_create/async_create.h"   // NOT RETAIL: ext.async_gpu_create
#include "compress_ao/bc4_create.h"          // NOT RETAIL: ext.compress_ao
#include "compress_ao/bc4_chain.h"           // NOT RETAIL: ext.compress_ao (the encode / the cache)
#include "bc4/bc4.h"                         // NOT RETAIL: ext.compress_ao (the BC4 block-row pitch)
#include "share_gpu_data/share_create.h"     // NOT RETAIL: ext.share_gpu_data
#include "create_on_demand/lazy_create.h"    // NOT RETAIL: ext.create_on_demand
#include "texture_quality/texture_quality.h" // NOT RETAIL: ext.texture_quality

// The real `Video::ImageBufferData` abstract base. B5 RES: `VideoDX11::
// ImageBufferData` derives from it in C++, so the compiler chains its destructor
// and builds its vtable.
#include "video/CompanionData.h"
// ENDGAME B.3.5 block J, RULE B5-14: the real `Video::ImageBuffer`, whose nine
// query members replace nine of this file's table-driven Runtime imports, and
// (B5 RES) whose named fields replace the raw-offset accessors.
#include "video/ImageBuffer.h"
#include "video/Device.h"   // ENDGAME B.3.9 VID: Video::Device / Configuration::_singleton

#include <windows.h>
#include <d3d11.h>

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <vector>

namespace VideoDX11 {
namespace {

// ============================================================
// Runtime-x64.dll imports — DIRECT REFERENCES (ENDGAME B.3R, block DX)
//
// This was an `Imports` struct of two pointers resolved at first use by
// `ResolveExport`, with an all-or-nothing readiness test; every slot forwarded
// to `Dx11OriginalSlot` when it failed. Both names bound to a body in this
// build, so the test was always true; the struct, the test and the forwarding
// arms are GONE (B3R_RULES R2, R8):
//   ?MemAlloc@MemoryAllocator@System@@SAPEAX_KH@Z -> System::MemoryAllocator::MemAlloc
//   ?MemFree@MemoryAllocator@System@@SAXPEAX@Z    -> System::MemoryAllocator::MemFree
// The nine `Video::ImageBuffer` rows went in ENDGAME B.3.5 block J (RULE B5-14)
// and the base dtor in block C (RULE B5-3); since B5 RES the base dtor is the
// compiler's chain.
//
// B5 RES: the `IbI32` / `IbU32` / `IbSet` raw-offset accessors, the `ImgOf`
// bridge cast and the local `VideoDevice` / `ActiveDeviceData` / `D3DDevice` /
// `D3DContext` walk are GONE. The ImageBuffer is the base's typed `_owner`; the
// walk is dx11_backend_access.h's `Dx11DeviceData()` / `Dx11Device()` /
// `Dx11Context()` — the same Device::_singleton -> +0x100 -> +0x20 / +0x28 loads
// (never cached across calls, as before).
// ============================================================

// Engine format -> DXGI_FORMAT. See the header comment for the derivation.
const int kFormatToDxgi[8] = { 0x47, 0x4d, 0x4d, 0x41, 0x3d, 0x57, 0x29, 0x2c };

// Engine format -> bytes per pixel (uncompressed) or per 4x4 block (BC).
const int kFormatBytes[8]  = {    8,   16,   16,    1,    1,    4,    4,    4 };

// ============================================================
// 0x180002300 — DeviceData::TrackImageBuffer(Video::ImageBuffer*)
//
// Add-if-absent into DeviceData+0x60 {data, count, capacity}, stride 8, x3
// growth. NOTE FOR LANE L6: this belongs to DeviceData; hoist it when
// DeviceData lands (see the header comment).
// ============================================================
void TrackImageBuffer(DeviceData* dd, ::Video::ImageBuffer* imageBuffer) {
    // B5 DEV-A: DeviceData::trackedImages is typed `Video::ImageBuffer**` now
    // (VideoDX11Data.h), so the array is walked as what it holds.
    const int                    oldCount = dd->trackedCount;
    ::Video::ImageBuffer** const oldData  = dd->trackedImages;

    // Linear scan; present -> nothing to do. (The original also has an
    // `if (index > -1)` guard on the hit, which is unconditionally true.)
    if (oldCount > 0) {
        ::Video::ImageBuffer** p = oldData;
        for (int i = 0; i < oldCount; ++i)
            if (p[i] == imageBuffer) return;
    }

    // Array::SetLength(oldCount + 1), inlined by the original with stride 8.
    const int newCount = oldCount + 1;
    dd->trackedCount = newCount;
    if (dd->trackedCapacity < newCount) {
        dd->trackedCapacity = newCount;
        if (oldCount != 0) dd->trackedCapacity = newCount * 3;
        dd->trackedImages = static_cast< ::Video::ImageBuffer**>(
            System::MemoryAllocator::MemAlloc(static_cast<uint64_t>(dd->trackedCapacity) * 8, 8));
    }

    // If the block was reused, only the tail needs filling; if it moved, the
    // whole range is copied across and the new element lands at the end.
    const int start = (oldData == dd->trackedImages) ? oldCount : 0;
    for (int i = start; i < newCount; ++i) {
        ::Video::ImageBuffer** dst = dd->trackedImages + i;
        if (dst != nullptr)
            *dst = (i < oldCount) ? oldData[i] : static_cast< ::Video::ImageBuffer*>(imageBuffer);
    }

    if (oldData != dd->trackedImages) System::MemoryAllocator::MemFree(oldData);
}

// The matching stable shift-erase, inlined into ~ImageBufferData at
// 0x1800024b2-0x180002505. Removes every occurrence (there is at most one).
void UntrackImageBuffer(DeviceData* dd, ::Video::ImageBuffer* imageBuffer) {
    for (int i = 0; i < dd->trackedCount; ++i) {
        if (dd->trackedImages[i] != imageBuffer) continue;
        for (int j = i + 1; j < dd->trackedCount; ++j) {
            ::Video::ImageBuffer** slot = dd->trackedImages + (j - 1);
            if (slot != nullptr) *slot = slot[1];
        }
        dd->trackedCount -= 1;
        --i;
    }
}

// D3D11_SUBRESOURCE_DATA is the 0x10-byte element the original builds; the
// growable array it builds them in uses the same Array::SetLength shape.
static_assert(sizeof(D3D11_SUBRESOURCE_DATA) == 0x10, "D3D11_SUBRESOURCE_DATA stride");

struct SubresArray {
    D3D11_SUBRESOURCE_DATA* data;
    int32_t                 count;
    int32_t                 capacity;
};

// Array::SetLength(count+1) then fill, inlined by the original at
// 0x180002b18-0x180002bbe with stride 0x10. x3 growth, first alloc exactly 1.
void PushSubresource(SubresArray* arr, const D3D11_SUBRESOURCE_DATA& value) {
    D3D11_SUBRESOURCE_DATA* const oldData = arr->data;
    const int oldCount = arr->count;
    const int newCount = oldCount + 1;

    arr->count = newCount;
    if (arr->capacity < newCount) {
        arr->capacity = newCount;
        if (oldCount != 0) arr->capacity = newCount * 3;
        arr->data = static_cast<D3D11_SUBRESOURCE_DATA*>(
            System::MemoryAllocator::MemAlloc(static_cast<uint64_t>(arr->capacity) * 0x10, 8));
    }

    const int start = (oldData == arr->data) ? oldCount : 0;
    for (int i = start; i < newCount; ++i) {
        D3D11_SUBRESOURCE_DATA* dst = arr->data + i;
        if (dst != nullptr)
            *dst = (i < oldCount) ? oldData[i] : value;
    }

    if (oldData != arr->data) System::MemoryAllocator::MemFree(oldData);
}

}  // namespace

// ============================================================
// Release the five interfaces in the shipped order and null them.
// Shared verbatim by ~ImageBufferData (0x180002507) and slot 3 (0x18000292a).
// The order is +0x28, +0x20, +0x30, +0x10, +0x18 — NOT declaration order.
// ============================================================
void ImageBufferData::ReleaseInterfaces() {
    // NOT RETAIL (ext.share_gpu_data): a shared texture's user is dropped before its releases below.
    ShareGpuData::DropTexture(texture);
    if (depthStencilView)   depthStencilView->Release();
    if (renderTargetView)   renderTargetView->Release();
    if (shaderResourceView) shaderResourceView->Release();
    if (sampler)            sampler->Release();
    if (texture)            texture->Release();

    depthStencilView   = nullptr;
    renderTargetView   = nullptr;
    shaderResourceView = nullptr;
    sampler            = nullptr;
    texture            = nullptr;
}

// ============================================================
// 0x180002720 — create the sampler state (private; called by slot 3 only,
// including on the CreateTexture2D failure path).
// ============================================================
// NOT RETAIL (shape only): the descriptor half of 0x180002720 is a free function so that
// ext.async_gpu_create can build it on the loader and hand it to a worker; the body is unchanged.
// `droppedLevels` (NOT RETAIL, ext.texture_quality, docs/extensions/handins/TEXQUALITY.md): the top
// levels the texture was created without; MaxLOD counts the levels it has. 0 = retail's descriptor.
static D3D11_SAMPLER_DESC SamplerDescFor(const ::Video::ImageBuffer* ib, int droppedLevels = 0) {
    // Both tables are stack locals in the original and are *patched* before
    // being indexed, which is why they are locals here too.
    int addressTable[2] = { D3D11_TEXTURE_ADDRESS_WRAP,       // 1
                            D3D11_TEXTURE_ADDRESS_CLAMP };    // 3
    int filterTable[3]  = { D3D11_FILTER_MIN_MAG_MIP_POINT,   // 0x00
                            D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT, // 0x14
                            D3D11_FILTER_ANISOTROPIC };       // 0x55

    // Configuration+0x28 (B.3.9 VID: the class static; B5 RES: the named member).
    int aniso = ::Video::Configuration::_singleton->textureFilter;
    const uint32_t flags = ib->_flags;
    const bool comparison = ((flags >> 6) & 1) != 0;

    if (comparison) {
        // Shadow sampler: the LINEAR entry becomes the comparison filter and
        // anisotropy is forced off. The ANISOTROPIC entry is left at 0x55.
        filterTable[1] = D3D11_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR; // 0x95
        aniso = 0;
    } else if (aniso == -1) {
        filterTable[2] = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;      // 0x14
        aniso = 0;
    } else if (aniso == 0) {
        filterTable[2] = D3D11_FILTER_MIN_MAG_MIP_LINEAR;            // 0x15
        aniso = 0;
    } else if (aniso < 0) {
        aniso = 0;                       // aniso < -1: nothing patched, just off
    }
    // aniso > 0: table stays ANISOTROPIC and the value is kept.

    ::Video::Device* const device = ::Video::Device::_singleton;
    const int maxAniso = device->maxAnisotropy;   // Device+0x98
    if (aniso > maxAniso) aniso = maxAniso;

    // NOT RETAIL (UB, out-of-bounds index): the four serialized fields index the stack tables
    // through ImageBuffer's clamps; retail indexes them raw (a mod .rsc with _filter 3 read past
    // filterTable). Valid values 0..1 / 0..2 index themselves.
    using IB = ::Video::ImageBuffer;
    D3D11_SAMPLER_DESC desc;
    desc.Filter         = static_cast<D3D11_FILTER>(filterTable[IB::FilterTableIndex(ib->_filter)]);                    // +0x44
    desc.AddressU       = static_cast<D3D11_TEXTURE_ADDRESS_MODE>(addressTable[IB::AddressTableIndex(ib->_addressU)]);  // +0x38
    desc.AddressV       = static_cast<D3D11_TEXTURE_ADDRESS_MODE>(addressTable[IB::AddressTableIndex(ib->_addressV)]);  // +0x3c
    desc.AddressW       = static_cast<D3D11_TEXTURE_ADDRESS_MODE>(addressTable[IB::AddressTableIndex(ib->_addressW)]);  // +0x40
    desc.MipLODBias     = 0.0f;
    desc.MaxAnisotropy  = static_cast<UINT>(aniso);
    // D3D11_COMPARISON_NEVER (1) normally, D3D11_COMPARISON_LESS (2) for shadow.
    desc.ComparisonFunc = static_cast<D3D11_COMPARISON_FUNC>((comparison ? 1 : 0) + 1);
    desc.BorderColor[0] = 0.0f;
    desc.BorderColor[1] = 0.0f;
    desc.BorderColor[2] = 0.0f;
    desc.BorderColor[3] = 0.0f;
    desc.MinLOD         = 0.0f;
    // 0x180018c50 = 1.0f (verified: bytes 00 00 80 3f).
    desc.MaxLOD         = (flags & 1) ? (static_cast<float>(ib->GetLevelCount() - droppedLevels) - 1.0f) : 0.0f;

    return desc;
}

void ImageBufferData::CreateSamplerState(int droppedLevels) {
    const D3D11_SAMPLER_DESC desc = SamplerDescFor(_owner, droppedLevels);
    Dx11Device()->CreateSamplerState(&desc, &sampler);
}

// ============================================================
// slot 0 — 0x180002400 — scalar deleting destructor
// 0x180002430 — ~ImageBufferData (tail-called by slot 0)
//
// B5 RES: slot 0's `if (flags & 1) MemFree` is the compiler's D0 calling the
// base's class-scope `operator delete` (B5 W0, companion_operators.cpp); the
// private destructor's "restore this class's vptr" store and its tail call into
// `??1ImageBufferData@Video@@UEAA@XZ` (JMP [0x1800133e0]) are the compiler's
// destructor chain. The body is what is left: the untrack, then the five
// releases in the shipped order (+0x28, +0x20, +0x30, +0x10, +0x18) and the
// five null stores.
// ============================================================
ImageBufferData::~ImageBufferData() {
    AsyncCreate::Await(this);   // NOT RETAIL (ext.async_gpu_create): a queued create lands first
    CreateOnDemand::Forget(this);   // NOT RETAIL (ext.create_on_demand): nor a first-use create after this
    ::Video::ImageBuffer* const ib = _owner;

    // Untrack. Note bits 4|5|6 = 0x70 here, unlike slot 3 which only tests bit 6.
    if (ib->IsGenerated() || (ib->_flags & 0x70) != 0 || ib->IsRendered())
        UntrackImageBuffer(Dx11DeviceData(), ib);

    ReleaseInterfaces();
}

// ============================================================
// slot 1 — 0x180002580 — alias the DeviceData color buffer
//
// Re-points the owning Video::ImageBuffer at the swap chain's render target:
// stamps the ImageBuffer's descriptor from the Video::Device resolution,
// AddRefs DeviceData::colorBuffer into +0x18, builds a default RTV, and joins
// the reset registry. No sampler and no SRV are created.
// ============================================================
void ImageBufferData::AttachColorBuffer() {
    AsyncCreate::Await(this);   // NOT RETAIL (ext.async_gpu_create): a queued create lands first
    CreateOnDemand::Forget(this);   // NOT RETAIL (ext.create_on_demand): this replaces a deferred one
    ::Video::Device* const device = ::Video::Device::_singleton;
    DeviceData* const dd = Dx11DeviceData();
    ::Video::ImageBuffer* const ib = _owner;

    // Field write order copied exactly (0x1800025ab..0x180002600) — note 0x40
    // before 0x3c before 0x38, and flags (0x34) last.
    ib->_width     = device->backBufferWidth;    // +0x20 <- Device+0x04
    ib->_height    = device->backBufferHeight;   // +0x24 <- Device+0x08
    ib->_dimension = 0;      // +0x28 dimension: 2D
    ib->_kind      = 0;      // +0x2c kind: render target
    ib->_format    = 5;      // +0x30 format 5 = DXGI_FORMAT_B8G8R8A8_UNORM (the backbuffer)
    ib->_addressW  = 1;      // +0x40 addressW = CLAMP
    ib->_addressV  = 1;      // +0x3c addressV = CLAMP
    ib->_addressU  = 1;      // +0x38 addressU = CLAMP
    ib->_filter    = 0;      // +0x44 filter   = POINT
    ib->_flags     = 0x10;   // +0x34 flags: bit4 -> tracked for device reset

    ID3D11Device* const dev = Dx11Device();
    // DeviceData::colorBuffer is the `ID3D11Texture2D*` at +0x38.
    ID3D11Texture2D* const color = dd->colorBuffer;
    color->AddRef();
    texture = color;
    dev->CreateRenderTargetView(texture, nullptr, &renderTargetView);

    TrackImageBuffer(dd, ib);   // tail call in the original
}

// ============================================================
// slot 2 — 0x180002650 — alias the DeviceData depth buffer
// Mirror of slot 1 with the depth format, kind 1, flag bit 5, and a DSV.
// ============================================================
void ImageBufferData::AttachDepthBuffer() {
    AsyncCreate::Await(this);   // NOT RETAIL (ext.async_gpu_create): a queued create lands first
    CreateOnDemand::Forget(this);   // NOT RETAIL (ext.create_on_demand): this replaces a deferred one
    ::Video::Device* const device = ::Video::Device::_singleton;
    DeviceData* const dd = Dx11DeviceData();
    ::Video::ImageBuffer* const ib = _owner;

    ib->_width     = device->backBufferWidth;
    ib->_height    = device->backBufferHeight;
    ib->_dimension = 0;      // dimension: 2D
    ib->_kind      = 1;      // kind: depth buffer
    ib->_format    = 7;      // format 7 = DXGI_FORMAT_R24G8_TYPELESS (D24S8)
    ib->_addressW  = 1;
    ib->_addressV  = 1;
    ib->_addressU  = 1;
    ib->_filter    = 0;
    ib->_flags     = 0x20;   // flags: bit5 -> tracked for device reset

    ID3D11Device* const dev = Dx11Device();
    // DeviceData::depthBuffer is the `ID3D11Texture2D*` at +0x30.
    ID3D11Texture2D* const depth = dd->depthBuffer;
    depth->AddRef();
    texture = depth;
    // Null desc: the DSV inherits the resource format. The resource here is the
    // DeviceData depth buffer, created as D24_UNORM_S8_UINT (typed, not
    // typeless), so a default view is legal.
    // EXT SEAM (NOT RETAIL, docs/extensions/handins/AOCORE.md): ext.ambient_occlusion (AO.md §6.1): when
    // CreateDepthBuffer built the typeless readable depth, the DSV needs an
    // explicit desc, and the SRV goes into the slot retail leaves null.
    if (AoBackend::DepthReadable())
        AoBackend::CreateMainDepthViews(dev, depth, &depthStencilView, &shaderResourceView);
    else
        dev->CreateDepthStencilView(texture, nullptr, &depthStencilView);

    TrackImageBuffer(dd, ib);
}

// ============================================================
// slot 3 — 0x180002870 — create the texture and all its views
//
// The level descriptors: an engine `Collection::Array` of 0x10-byte elements
// (the loop's `ADD RCX,0x10`), of which ONLY the leading pointer is ever read
// (0x180002ab4). B5 RES: the callers name the type —
// `Video::ImageBuffer::LevelArray`, i.e. `Collection::Array<Collection::Array<
// uint8_t>>` (CompanionData.h, at the slot) — so each element is a byte array
// and the leading pointer is its `_data`. The former `ImageLevel` /
// `ImageLevelArray` views of the same bytes are gone.
// ============================================================
// NOT RETAIL (ext.async_gpu_create, docs/extensions/handins/ASYNCGPUCREATE.md): slot 3's tail for a
// plain seeded texture, with the three creates on a worker. Everything engine-visible (the registry,
// the nulled views, the descriptor, the subresource pitches) was done by slot 3 on this thread; this
// copies the level bytes the subresources point at (the caller's LevelArray dies when it returns),
// re-points the pitches into the copy and hands the job over. The worker makes the texture, the SRV
// only if the texture succeeded and the sampler always: slot 3's order and failure state. The
// sampler's descriptor is built HERE, from the Configuration / Device values slot 3 would have read.
// `encodeBc4` (NOT RETAIL, ext.compress_ao): the R8 levels are copied as they are and the job turns
// them into BC4 (the cache lookup or the encode) on its worker before the create, so neither runs on
// the loader.
// `drop` (NOT RETAIL, ext.texture_quality): the levels Create left out; subresource i is level i + drop.
static void CreateTextureDeferred(ImageBufferData* self,
                                  const Collection::Array<Collection::Array<uint8_t> >* levels,
                                  const D3D11_TEXTURE2D_DESC& desc, SubresArray* subres,
                                  bool encodeBc4, int drop) {
    ::Video::ImageBuffer* const ib = self->_owner;

    uint64_t total = 0;
    for (int i = drop; i < levels->GetSize(); ++i)
        total += static_cast<uint64_t>(levels->GetData()[i].GetSize());

    uint8_t* const bytes = new uint8_t[total > 0 ? total : 1];
    D3D11_SUBRESOURCE_DATA* copy = new D3D11_SUBRESOURCE_DATA[subres->count > 0 ? subres->count : 1];
    uint64_t at = 0;
    for (int i = 0; i < subres->count; ++i) {
        const Collection::Array<uint8_t>& level = levels->GetData()[i + drop];
        const uint64_t n = static_cast<uint64_t>(level.GetSize());
        if (n != 0) std::memcpy(bytes + at, subres->data[i].pSysMem, n);
        copy[i]         = subres->data[i];
        copy[i].pSysMem = bytes + at;
        at += n;
    }
    System::MemoryAllocator::MemFree(subres->data);
    subres->data = nullptr;
    if (subres->count == 0) {   // slot 3 hands CreateTexture2D a null array then
        delete[] copy;
        copy = nullptr;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
    const D3D11_SHADER_RESOURCE_VIEW_DESC* srvDescPtr = nullptr;
    if (ib->_dimension == 1) {   // slot 3's cube arm (a usage-2 texture is never a depth buffer)
        srvDesc.Format                      = static_cast<DXGI_FORMAT>(kFormatToDxgi[ib->_format]);
        srvDesc.ViewDimension               = D3D11_SRV_DIMENSION_TEXTURECUBE;
        srvDesc.TextureCube.MipLevels       = static_cast<UINT>(ib->GetLevelCount());
        srvDesc.TextureCube.MostDetailedMip = 0;
        srvDescPtr = &srvDesc;
    }

    AsyncCreate::EnqueueTexture(self, Dx11Device(), desc, copy, subres->count, bytes, total, srvDescPtr,
                                SamplerDescFor(ib, drop), encodeBc4 ? static_cast<uint32_t>(ib->_format) : 0,
                                &self->texture, &self->shaderResourceView,
                                &self->sampler);
}

bool ImageBufferData::Create(const Collection::Array<Collection::Array<uint8_t> >* levels) {
    AsyncCreate::Await(this);   // NOT RETAIL (ext.async_gpu_create): a queued create lands first
    CreateOnDemand::Forget(this);   // NOT RETAIL (ext.create_on_demand): this create replaces a deferred one
    ::Video::ImageBuffer* const ib = _owner;
    const int width  = ib->Width();
    const int height = ib->Height();

    // Degenerate, non-generated, non-rendered buffer: succeed and do nothing.
    if (!ib->IsGenerated() && !ib->IsRendered() && (width == 0 || height == 0))
        return true;

    DeviceData* const dd = Dx11DeviceData();

    // Add to the reset registry. Only bit 6 here (the dtor also honours 4 and 5).
    if (ib->IsGenerated() || ib->IsRendered() || ((ib->_flags >> 6) & 1) != 0)
        TrackImageBuffer(dd, ib);

    ReleaseInterfaces();

    // NOT RETAIL (UB, out-of-bounds index): every create below indexes the 8-entry
    // kFormatToDxgi / kFormatBytes with `_format`, which a .rsc carries unchecked; retail has no
    // check and reads past the tables. Out of range fails here as a failed CreateTexture2D would
    // (no texture, no view), as the Metal backend's create does. Formats 0..7 are unchanged.
    if (!::Video::ImageBuffer::FormatInRange(ib->_format))
        return false;

    // --- D3D11_TEXTURE2D_DESC ---------------------------------------------
    const uint32_t kind = static_cast<uint32_t>(ib->_kind);   // +0x2c, read unsigned

    // 0x180002992-0x1800029b6, reproduced literally. `kind & ~5` selects the
    // {0,1,4,5} group; 1 is then split off as the depth case.
    UINT bindFlags = 0;
    if ((kind & 0xfffffffau) == 0 && kind != 1) bindFlags = D3D11_BIND_RENDER_TARGET;  // 0x20
    if (kind == 1)                              bindFlags |= D3D11_BIND_DEPTH_STENCIL; // 0x40

    // EXT SEAM (NOT RETAIL, docs/extensions/handins/TEXQUALITY.md): ext.texture_quality -- a qualifying
    // texture seeded from a resource file is made without its `drop` largest levels (the engine's level
    // reader skipped their bytes, or the array still holds them and they are left out here): the
    // descriptor at the first kept level's size, the subresources from it (after the faithful loop
    // below), the sampler's MaxLOD lowered by the same count. 0 for every other image and while the
    // row is off: retail's create.
    const int drop = (levels != nullptr && kind == 2) ? TextureQuality::DropFor(*ib, levels->GetSize()) : 0;

    D3D11_TEXTURE2D_DESC desc;
    desc.Width  = static_cast<UINT>(width >> drop);
    desc.Height = static_cast<UINT>(height >> drop);
    // flags bit0 set (mipmapped) -> 0 = "generate the full chain".
    desc.MipLevels = static_cast<UINT>((~static_cast<uint8_t>(ib->_flags)) & 1);
    desc.ArraySize = (ib->_dimension == 0) ? 1 : 6;   // 6 = cube faces
    desc.Format = static_cast<DXGI_FORMAT>(kFormatToDxgi[ib->_format]);
    desc.SampleDesc.Count   = 1;   // textures are never multisampled here
    desc.SampleDesc.Quality = 0;
    desc.Usage = (kind == 3) ? D3D11_USAGE_DYNAMIC
                             : (levels != nullptr ? D3D11_USAGE_IMMUTABLE : D3D11_USAGE_DEFAULT);
    bindFlags |= D3D11_BIND_SHADER_RESOURCE;  // 0x8, always
    desc.BindFlags      = bindFlags;
    desc.CPUAccessFlags = (kind == 3) ? D3D11_CPU_ACCESS_WRITE : 0;   // 0x10000
    desc.MiscFlags      = (ib->_dimension != 0) ? D3D11_RESOURCE_MISC_TEXTURECUBE : 0;  // 0x4

    // --- initial data ------------------------------------------------------
    SubresArray subres = { nullptr, 0, 0 };
    if (levels != nullptr) {
        int levelWidth = ib->_width;   // NOT the Width() accessor — the raw field
        int levelIndex = 0;

        for (int i = 0; i < levels->GetSize(); ++i) {
            D3D11_SUBRESOURCE_DATA entry;
            entry.pSysMem = levels->GetData()[i].GetData();   // element +0x00, the only read

            // Row length in pixels, or in 4x4 blocks for a BC format. The
            // original is `(w + (w<0 ? 3 : 0)) >> 2`, i.e. C truncating /4.
            int rowUnits = levelWidth;
            if (ib->IsCompressed()) rowUnits = levelWidth / 4;
            entry.SysMemPitch      = static_cast<UINT>(kFormatBytes[ib->_format] * rowUnits);
            entry.SysMemSlicePitch = 0;   // hoisted out of the loop by the original

            PushSubresource(&subres, entry);

            // Next mip; BC formats floor at a 4-pixel (one-block) row. When the
            // mip chain is exhausted, restart it — that is how the six faces of
            // a cube map are walked with one flat counter.
            levelWidth >>= 1;
            if (ib->IsCompressed() && levelWidth < 4) levelWidth = 4;
            if (++levelIndex == ib->GetLevelCount()) {
                levelWidth = ib->_width;
                levelIndex = 0;
            }
        }
        // EXT SEAM (NOT RETAIL, docs/extensions/handins/TEXQUALITY.md): ext.texture_quality -- the
        // dropped levels' entries go; the rest keep the pitches the loop gave them, which are the
        // smaller texture's own (DropFor requires both sides a multiple of 4 << drop). 2D only: the
        // restart above never fires before the last level.
        subres.count = TextureQuality::DropLeading(subres.data, subres.count, drop);
    }

    // EXT SEAM (NOT RETAIL, docs/extensions/handins/COMPRESSAO.md): ext.compress_ao -- a 2D R8
    // texture seeded from a resource file (the baked AO maps), every level present and the top level
    // a multiple of 4 each way, is created as BC4_UNORM from its levels encoded (or found in the
    // derived cache). ImageBuffer keeps format 4; only the D3D format and the pitches change.
    // Inside an armed loader bracket the conversion moves to the deferred create's worker (below).
    std::vector<uint8_t> bc4;
    std::vector<size_t> bc4Offsets;
    bool asBc4 = false;
    // ext.texture_quality: the shape of the texture made (the first kept level's size). A dropped
    // texture's levels are a new chain for the cache (its key hashes the levels it is given).
    const int topWidth  = width >> drop;
    const int topHeight = height >> drop;
    const bool bc4Candidate = CompressAo::Requested() && levels != nullptr && kind == 2 && ib->_format == 4 &&
                              ib->_dimension == 0 && (topWidth & 3) == 0 && (topHeight & 3) == 0 &&
                              levels->GetSize() == ib->GetLevelCount();
    const bool deferred = AsyncCreate::Deferring() && levels != nullptr && kind == 2;

    // EXT SEAM (NOT RETAIL, docs/extensions/handins/CREATEONDEMAND.md): ext.create_on_demand -- the
    // engine's level reader skipped this image's level bytes (Video::ImageBuffer::_extLevelSource):
    // everything above ran as for any texture, and the sampler is made below as retail makes it; the
    // texture and its SRV (after compress_ao's conversion, through share_gpu_data's create-or-share)
    // wait for the first use. The level arrays are empty: nothing here reads them.
    if (levels != nullptr && kind == 2 && CreateOnDemand::HasPendingLevels(ib)) {
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
        const D3D11_SHADER_RESOURCE_VIEW_DESC* srvDescPtr = nullptr;
        if (ib->_dimension == 1) {   // slot 3's cube arm (a usage-2 texture is never a depth buffer)
            srvDesc.Format                      = static_cast<DXGI_FORMAT>(kFormatToDxgi[ib->_format]);
            srvDesc.ViewDimension               = D3D11_SRV_DIMENSION_TEXTURECUBE;
            srvDesc.TextureCube.MipLevels       = static_cast<UINT>(ib->GetLevelCount());
            srvDesc.TextureCube.MostDetailedMip = 0;
            srvDescPtr = &srvDesc;
        }
        std::vector<UINT> pitches(static_cast<size_t>(subres.count));
        for (int i = 0; i < subres.count; ++i)
            pitches[static_cast<size_t>(i)] = subres.data[i].SysMemPitch;
        CreateOnDemand::DeferTexture(this, ib, desc, pitches.data(), subres.count, srvDescPtr,
                                     bc4Candidate ? static_cast<uint32_t>(ib->_format) : 0, &texture,
                                     &shaderResourceView);
        CreateSamplerState(drop);
        System::MemoryAllocator::MemFree(subres.data);
        return true;
    }

    if (bc4Candidate && !deferred) {
        // ext.texture_quality: the kept levels only (level i + drop of the file is subresource i).
        const int kept = levels->GetSize() - drop;
        std::vector<::CompressAo::Level> in(static_cast<size_t>(kept));
        for (int i = 0; i < kept; ++i) {
            in[i].data = levels->GetData()[i + drop].GetData();
            in[i].size = static_cast<int64_t>(levels->GetData()[i + drop].GetSize());
        }
        asBc4 = ::CompressAo::Encode(in.data(), kept, topWidth, topHeight, ib->_format, &bc4, &bc4Offsets);
        if (asBc4) {
            desc.Format = DXGI_FORMAT_BC4_UNORM;
            for (int i = 0; i < subres.count; ++i) {
                const int levelWidth = (topWidth >> i) > 0 ? (topWidth >> i) : 1;
                subres.data[i].pSysMem     = bc4.data() + bc4Offsets[i];
                subres.data[i].SysMemPitch = static_cast<UINT>(Bc4::BlockRowPitch(levelWidth));
            }
        }
    }

    // EXT SEAM (NOT RETAIL, docs/extensions/handins/ASYNCGPUCREATE.md): inside an armed loader
    // bracket a plain texture seeded from a resource file (usage 2: no render target, no depth, not
    // dynamic, so no RTV / DSV) is created on a worker, from a copy of its level bytes.
    if (deferred) {
        CreateTextureDeferred(this, levels, desc, &subres, bc4Candidate, drop);
        return true;
    }

    // EXT SEAM (NOT RETAIL, docs/extensions/handins/SHAREGPUDATA.md): ext.share_gpu_data -- outside
    // the async bracket, a texture seeded from a resource file (usage 2: the texture and its SRV only,
    // no RTV / DSV) is made through the create-or-share; the sampler and the scratch array as below.
    if (ShareGpuData::Requested() && levels != nullptr && kind == 2) {
        std::vector<uint64_t> sizes(static_cast<size_t>(subres.count));
        for (int i = 0; i < subres.count; ++i)
            sizes[static_cast<size_t>(i)] = asBc4 ? bc4Offsets[i + 1] - bc4Offsets[i]
                                                  : static_cast<uint64_t>(levels->GetData()[i + drop].GetSize());
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
        const D3D11_SHADER_RESOURCE_VIEW_DESC* srvDescPtr = nullptr;
        if (ib->_dimension == 1) {   // slot 3's cube arm (a usage-2 texture is never a depth buffer)
            srvDesc.Format                      = static_cast<DXGI_FORMAT>(kFormatToDxgi[ib->_format]);
            srvDesc.ViewDimension               = D3D11_SRV_DIMENSION_TEXTURECUBE;
            srvDesc.TextureCube.MipLevels       = static_cast<UINT>(ib->GetLevelCount());
            srvDesc.TextureCube.MostDetailedMip = 0;
            srvDescPtr = &srvDesc;
        }
        const HRESULT shared = ShareGpuData::CreateTexture(Dx11Device(), desc, subres.data, subres.count,
                                                           sizes.data(), srvDescPtr, &texture,
                                                           &shaderResourceView);
        CreateSamplerState(drop);
        System::MemoryAllocator::MemFree(subres.data);
        return shared == 0;
    }

    ID3D11Device* const dev = Dx11Device();
    HRESULT hr = dev->CreateTexture2D(&desc, subres.data, &texture);

    if (hr == 0) {
        // --- shader resource view -----------------------------------------
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
        D3D11_SHADER_RESOURCE_VIEW_DESC* srvDescPtr = nullptr;

        if (ib->IsDepthBuffer()) {
            // Hard-coded: a depth texture is always read as R24_UNORM_X8.
            // GAP: format index 6 (R32_TYPELESS) would need R32_FLOAT here, so
            // either index 6 is unused or IsDepthBuffer implies index 7. The
            // shipped code makes no distinction and neither does this.
            srvDesc.Format                    = static_cast<DXGI_FORMAT>(0x2e); // R24_UNORM_X8_TYPELESS
            srvDesc.ViewDimension             = D3D11_SRV_DIMENSION_TEXTURE2D;  // 4
            srvDesc.Texture2D.MostDetailedMip = 0;
            srvDesc.Texture2D.MipLevels       = 1;
            srvDescPtr = &srvDesc;
        } else if (ib->_dimension == 1) {
            srvDesc.Format                      = static_cast<DXGI_FORMAT>(kFormatToDxgi[ib->_format]);
            srvDesc.ViewDimension               = D3D11_SRV_DIMENSION_TEXTURECUBE; // 9
            srvDesc.TextureCube.MipLevels       = static_cast<UINT>(ib->GetLevelCount());
            srvDesc.TextureCube.MostDetailedMip = 0;
            srvDescPtr = &srvDesc;
        }
        // else: null desc — the default view over the whole 2D texture.

        hr = dev->CreateShaderResourceView(texture, srvDescPtr, &shaderResourceView);

        // --- render-target / depth-stencil view ----------------------------
        // Only for non-cube render buffers.
        if (hr == 0 && ib->_dimension == 0 && ib->IsRenderBuffer()) {
            if (ib->IsDepthBuffer()) {
                D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc;
                dsvDesc.Format             = static_cast<DXGI_FORMAT>(0x2d); // D24_UNORM_S8_UINT
                dsvDesc.ViewDimension      = D3D11_DSV_DIMENSION_TEXTURE2D;  // 3
                dsvDesc.Flags              = 0;
                dsvDesc.Texture2D.MipSlice = 0;
                hr = dev->CreateDepthStencilView(texture, &dsvDesc, &depthStencilView);
            } else {
                hr = dev->CreateRenderTargetView(texture, nullptr, &renderTargetView);
            }
        }
    }

    // Reached from every path, including a failed CreateTexture2D: the sampler
    // is created regardless, and the scratch array is always freed (the
    // original calls MemFree even when the pointer is null).
    CreateSamplerState(drop);
    System::MemoryAllocator::MemFree(subres.data);
    return hr == 0;
}

// ============================================================
// slot 4 — 0x180002da0 — bind to a pixel-shader slot
//
// The second parameter (RDX) is never read by the original. It is the
// `Video::Context` (CompanionData.h quotes `?BindTextures@Context@Video@@`'s
// `MOV RDX,RSI` before the call); the body walks to the global context instead,
// like every other Bind in this DLL. Sampler first, then SRV — that order is
// observable.
// ============================================================
// NOT RETAIL (ext.sharp_ui). The point-sampled twin of the sampler built at
// creation: CreateSamplerState's descriptor with the filter forced to
// MIN_MAG_MIP_POINT and anisotropy off, the image's own address modes and LOD
// range. D3D11 hands back the SAME state object for an identical descriptor
// (it keeps one per unique descriptor), so building it per bind is a lookup,
// not an allocation, and nothing device-scoped is cached here (a device-lost
// round trip has nothing of ours to rebuild). The context holds its own
// reference once bound, so ours is released at once. Null on failure: the
// caller then binds the creation-time sampler.
// ext.texture_quality (docs/extensions/handins/TEXQUALITY.md): the LOD range is the creation-time
// sampler's when there is one, so a texture made without its top levels keeps its lowered MaxLOD.
static ID3D11SamplerState* AcquirePointSampler(const ::Video::ImageBuffer* ib, ID3D11SamplerState* made) {
    const int addressTable[2] = { D3D11_TEXTURE_ADDRESS_WRAP, D3D11_TEXTURE_ADDRESS_CLAMP };
    D3D11_SAMPLER_DESC desc;
    desc.Filter         = D3D11_FILTER_MIN_MAG_MIP_POINT;
    // Through the same clamps as SamplerDescFor (an out-of-range field indexed past the table).
    using IB = ::Video::ImageBuffer;
    desc.AddressU       = static_cast<D3D11_TEXTURE_ADDRESS_MODE>(addressTable[IB::AddressTableIndex(ib->_addressU)]);
    desc.AddressV       = static_cast<D3D11_TEXTURE_ADDRESS_MODE>(addressTable[IB::AddressTableIndex(ib->_addressV)]);
    desc.AddressW       = static_cast<D3D11_TEXTURE_ADDRESS_MODE>(addressTable[IB::AddressTableIndex(ib->_addressW)]);
    desc.MipLODBias     = 0.0f;
    desc.MaxAnisotropy  = 0;
    desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    desc.BorderColor[0] = 0.0f;
    desc.BorderColor[1] = 0.0f;
    desc.BorderColor[2] = 0.0f;
    desc.BorderColor[3] = 0.0f;
    desc.MinLOD         = 0.0f;
    desc.MaxLOD         = (ib->_flags & 1) ? (static_cast<float>(ib->GetLevelCount()) - 1.0f) : 0.0f;
    if (made != nullptr) {
        D3D11_SAMPLER_DESC creation;
        made->GetDesc(&creation);
        desc.MaxLOD = creation.MaxLOD;
    }
    ID3D11SamplerState* state = nullptr;
    if (Dx11Device()->CreateSamplerState(&desc, &state) < 0) return nullptr;
    return state;
}

void ImageBufferData::Bind(void* context, unsigned slot) {
    AsyncCreate::Await(this);   // NOT RETAIL (ext.async_gpu_create): a queued create lands first
    // NOT RETAIL (ext.create_on_demand): the first bind of a deferred texture makes it; a made one
    // pays the null test. The scope stamps the use and keeps an eviction away until the bind is done.
    CreateOnDemand::UseScope use(this);
    if (::CreateOnDemand::LoadPublished(shaderResourceView) == nullptr) CreateOnDemand::Ensure(this);
    (void)context;   // never read by the original either

    ID3D11DeviceContext* const ctx = Dx11Context();
    // EXT SEAM (NOT RETAIL, docs/extensions/handins/SHARPUI.md): ext.sharp_ui -- ::Video::ImageBufferData::
    // _extPointSampling is false by default and in the faithful profile, which
    // takes retail's two lines below untouched. Set only inside a UI pass drawn
    // at an integer ratio >= 2 (Ext::SharpUI::Draw); a comparison (shadow) image
    // keeps its own sampler regardless.
    if (::Video::ImageBufferData::_extPointSampling && ((_owner->_flags >> 6) & 1) == 0) {
        if (ID3D11SamplerState* point = AcquirePointSampler(_owner, sampler)) {
            ctx->PSSetSamplers(slot, 1, &point);
            point->Release();
            ctx->PSSetShaderResources(slot, 1, &shaderResourceView);
            return;
        }
    }
    ctx->PSSetSamplers(slot, 1, &sampler);
    ctx->PSSetShaderResources(slot, 1, &shaderResourceView);
}

// ============================================================
// slot 5 — 0x180002e10 — upload pixels
//
// Always returns true; the original ignores both HRESULTs. Both paths assume
// 4 bytes per pixel and a tightly packed source, so this is the 32-bit upload
// path only (dynamic UI/terrain textures), not the compressed one.
// ============================================================
bool ImageBufferData::Update(void* data, int width, int height) {
    AsyncCreate::Await(this);   // NOT RETAIL (ext.async_gpu_create): a queued create lands first
    CreateOnDemand::UseScope use(this);   // NOT RETAIL (ext.create_on_demand): stamp, no eviction meanwhile
    // NOT RETAIL (ext.create_on_demand): an acquire load, a worker may publish the handle meanwhile
    if (::CreateOnDemand::LoadPublished(texture) == nullptr) CreateOnDemand::Ensure(this);
    // NOT RETAIL: the Present lock (dx11_backend_access.h). Callers hold the
    // device lock and may be off the presenting thread (the loader's image
    // uploads, the save-info thumbnails).
    std::lock_guard<std::recursive_mutex> presentLock(Dx11PresentMutex());
    ID3D11DeviceContext* const ctx = Dx11Context();

    if (_owner->IsDynamic()) {
        D3D11_MAPPED_SUBRESOURCE mapped;
        ctx->Map(texture, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        // Note: copies width*height*4 into the mapped block *without* honouring
        // mapped.RowPitch. That is what the shipped code does.
        std::memcpy(mapped.pData, data, static_cast<size_t>(width * height) * 4);
        ctx->Unmap(texture, 0);
    } else {
        ctx->UpdateSubresource(texture, 0, nullptr,
                               data, static_cast<UINT>(width * 4), 0);
    }
    return true;
}

// ============================================================
// slot 6 — 0x180002ef0 — read pixels back
//
// Creates a STAGING B8G8R8A8 texture of the requested size, CopyResources into
// it, maps it for read and de-pitches into the caller's buffer. No HRESULT is
// checked anywhere in the original — if CreateTexture2D fails, `staging` stays
// null and CopyResource is called with it. Reproduced rather than "fixed":
// silently diverging here would hide a real failure during A/B capture.
// ============================================================
bool ImageBufferData::Read(void* dst, int width, int height) {
    AsyncCreate::Await(this);   // NOT RETAIL (ext.async_gpu_create): a queued create lands first
    CreateOnDemand::UseScope use(this);   // NOT RETAIL (ext.create_on_demand): stamp, no eviction meanwhile
    // NOT RETAIL (ext.create_on_demand): an acquire load, a worker may publish the handle meanwhile
    if (::CreateOnDemand::LoadPublished(texture) == nullptr) CreateOnDemand::Ensure(this);
    D3D11_TEXTURE2D_DESC desc;
    desc.Width              = static_cast<UINT>(width);
    desc.Height             = static_cast<UINT>(height);
    desc.MipLevels          = 1;
    desc.ArraySize          = 1;
    desc.Format             = static_cast<DXGI_FORMAT>(0x57);   // B8G8R8A8_UNORM
    desc.SampleDesc.Count   = 1;
    desc.SampleDesc.Quality = 0;
    desc.Usage              = D3D11_USAGE_STAGING;              // 3
    desc.BindFlags          = 0;
    desc.CPUAccessFlags     = D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE;  // 0x30000
    desc.MiscFlags          = 0;

    // NOT RETAIL: the Present lock (dx11_backend_access.h), for the staging
    // round trip through the immediate context below.
    std::lock_guard<std::recursive_mutex> presentLock(Dx11PresentMutex());
    ID3D11DeviceContext* const ctx = Dx11Context();

    ID3D11Texture2D* staging = nullptr;
    Dx11Device()->CreateTexture2D(&desc, nullptr, &staging);

    ctx->CopyResource(staging, texture);

    // MapType is 3 = D3D11_MAP_READ_WRITE, not READ (`LEA R9D,[RBX+3]` with
    // RBX zeroed, 0x180002fb0) — which is why the staging texture above asks
    // for CPU_ACCESS_READ **and** WRITE. A read-only map would be rejected
    // against that description, so this is load-bearing, not incidental.
    D3D11_MAPPED_SUBRESOURCE mapped;
    ctx->Map(staging, 0, D3D11_MAP_READ_WRITE, 0, &mapped);

    // The caller's buffer and the mapped rows are plain bytes; name them as such
    // once (B5 RES: was a cast + offset inside each memcpy argument).
    uint8_t* const       out = static_cast<uint8_t*>(dst);   // CAST-OK: byte_io (the caller's pixel buffer)
    const uint8_t* const in  = static_cast<const uint8_t*>(mapped.pData);   // CAST-OK: gpu_map
    const int dstPitch = width * 4;
    int srcOffset = 0;
    int dstOffset = 0;
    for (int y = 0; y < height; ++y) {
        std::memcpy(out + dstOffset, in + srcOffset, static_cast<size_t>(dstPitch));
        srcOffset += static_cast<int>(mapped.RowPitch);
        dstOffset += dstPitch;
    }

    ctx->Unmap(staging, 0);
    staging->Release();
    return true;
}

}  // namespace VideoDX11

// All seven live. Verified in-game and RenderDoc-diffed against the shipped
// DLL — see src/render/dx11/README.md §"Per-draw pipeline-state equality".
