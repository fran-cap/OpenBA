#pragma once
// The twelve `VideoDX11::*Data` classes: VideoDX11-x64.dll's concrete companions,
// as real C++ subclasses of the runtime's abstract `Video::*Data` bases
// (src/engine/video/CompanionData.h) -- ENDGAME B5, the dx11 seam (2026-10-05).
//
// The base contributes the first 0x10 bytes (the compiler's vptr at +0x00 and the
// typed `_owner` at +0x08); the private tail follows at +0x10, laid out and sized
// exactly as the shipped plugin's MemAlloc(size, 4) in each DeviceData factory
// slot (every field and the sizeof are static_asserted). Every slot is an
// `override` in retail order with the retail address in its trailing comment; the
// compiler builds the vtable (two destructor slots, so retail slot N >= 1 is
// compiler slot N + 1 -- nothing indexes these tables). See ../README.md for the
// status board and ../../decomp/videodx11.md for the derivation.
//
// Fields are named only where a decompiled function proves the meaning;
// everything else stays a raw slot so the layout stays honest. Do not guess.

#include <cstdint>
#include <cstddef>

// B5 (the dx11 seam): the abstract `Video::XData` bases the converted classes
// below derive from.
#include "video/CompanionData.h"

// B5 (D4): the COM pointers are typed with FORWARD declarations, so this header
// stays free of <d3d11.h> (mingw-w64's `interface` is `struct`, so these name
// the same types).
struct ID3D11Buffer;                 // B5 BUF: Constant/Index/VertexBufferData
struct ID3D11DeviceContext;          // B5 CTX: ContextData
struct ID3D11InputLayout;            // B5 RES: VertexLayoutData
struct ID3D11VertexShader;           // B5 RES: VertexProgramData
struct ID3D11PixelShader;            // B5 RES: PixelProgramData
struct ID3D11BlendState;             // B5 RES: RenderStateData
struct ID3D11DepthStencilState;      // B5 RES: RenderStateData
struct ID3D11RasterizerState;        // B5 RES: RenderStateData
struct ID3D11SamplerState;           // B5 RES: ImageBufferData
struct ID3D11Texture2D;              // B5 RES: ImageBufferData
struct ID3D11RenderTargetView;       // B5 RES: ImageBufferData
struct ID3D11DepthStencilView;       // B5 RES: ImageBufferData
struct ID3D11ShaderResourceView;     // B5 RES: ImageBufferData

namespace VideoDX11 {

// (The `{vptr, owner}` prefix every class shares is the base `Video::XData` itself;
// each class below asserts `offsetof(X, _owner) == 0x08`.)

// ============================================================
// DeviceData — 0x70 bytes, vtable 0x180018bb0 (20 slots)
// Created by the CreateDeviceData export (0x1800011a0).
//
// REIMPLEMENTED — src/render/dx11/devicedata.cpp (slots 0/1/2/3/8 + the 11 factories,
// lane L6) and src/render/dx11/devicedata_stubs.cpp (slots 4-7).
//
// Field meanings recovered from:
//   slot 0  dtor      0x180004270  (frees +0x60 array)
//   slot 2  create    0x180004e10  (writes +0x10/+0x18/+0x20/+0x28, then +0x30/+0x38)
//   slot 3  shutdown  0x1800051c0  (releases +0x18/+0x20/+0x28/+0x30/+0x38,
//                                   DestroyCursor(+0x48), reads +0x40)
//   slot 8  cursor    0x180005450  (writes +0x48/+0x50/+0x58/+0x5c)
//   0x180004340        resize      (releases and rebuilds +0x30/+0x38, reads +0x40)
//   0x1800042d0        WM_ACTIVATE (the only writer of +0x40)
//
// B5 DEV-A (the dx11 seam): a real C++ subclass of `Video::DeviceData`; the
// compiler builds its vtable (retail slot N >= 1 is compiler slot N + 1, which
// nothing indexes: Runtime and contextdata.cpp call every slot BY NAME through the
// base). `_owner` (+0x08, `Video::Device*`) is the base's and replaces the old
// `device` field; the old `vptr` field is the compiler's. The COM pointers are
// typed through the forward declarations below (D4), so this header still never
// includes <windows.h> / <d3d11.h>. The slot names: Runtime's
// `Video::DeviceData` (src/engine/video/CompanionData.h), whose comments carry
// the retail evidence for each; the four `SlotN` stubs have no recoverable name.
// ============================================================
}  // namespace VideoDX11

struct IDXGIFactory1;
struct IDXGISwapChain;
struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11Texture2D;
// Phase D.4 RENDER-3: DeviceData::cursor is Platform::CursorHandle, an opaque 8-byte
// handle (retail's HCURSOR; a slot holding an SDL_Cursor* since SDL step W5,
// src/platform/sdl/cursor_sdl.cpp).
#include "platform_window.h"

namespace VideoDX11 {

class DeviceData final : public ::Video::DeviceData {
public:
    // CreateDeviceData 0x1800011a0: MemAlloc(0x70, 4) + ??0DeviceData(device) + the
    // vptr store, then EXACTLY these stores. +0x40 is a single BYTE
    // (`MOV byte [RBX+0x40],AL`): pad41 keeps what MemAlloc left. +0x58 is ONE
    // qword -1 (`MOV qword [RBX+0x58],-1`): both hotspots read -1.
    explicit DeviceData(::Video::Device* owner);
    ~DeviceData() override;                                      // [ 0] 0x180004270  frees +0x60, chains ??1DeviceData@Video@@UEAA@XZ

    bool  EnumerateInterfaces() override;                        // [ 1] 0x180004690  DXGI adapter/output/mode walk
    bool  CreateDevice() override;                               // [ 2] 0x180004e10  D3D11 device + swap chain + buffers
    void  Shutdown() override;                                   // [ 3] 0x1800051c0  unregister, DestroyCursor, release
    void  Slot4(bool loading) override;                          // [ 4] 0x180001190  `ret` — shared with VideoProgramData[2]
    float Slot5() override;                                      // [ 5] 0x180005430  `xorps xmm0,xmm0; ret`
    bool  Slot6() override;                                      // [ 6] 0x180005440  `xor al,al; ret`
    bool  Slot7() override;                                      // [ 7] 0x180001060  `mov al,1; ret` — shared with RenderBufferData[1], VideoProgramData[1]
    void  SetImageBuffer(::Video::ImageBuffer* imageBuffer, int hotX, int hotY) override;  // [ 8] 0x180005450  HCURSOR from a 32x32 ImageBuffer

    // [9]..[19] The eleven resource factories. The address <-> class pairing was
    // verified independently of the slot ORDER: each factory's base-ctor CALL
    // resolves through the VideoDX11 Runtime-import IAT (base 0x180013218) to the
    // matching ??0<Class>Data@Video@@ mangled name (the table in devicedata.cpp).
    // Each returns the BASE type, as the slot declares it (not covariant).
    ::Video::ContextData*        CreateContextData(::Video::Context* owner) override;               // [ 9] 0x180005650  0x20 bytes, vt 0x1800186f0
    ::Video::ImageBufferData*    CreateImageBufferData(::Video::ImageBuffer* owner) override;       // [10] 0x1800056c0  0x38 bytes, vt 0x180018790
    ::Video::ConstantBufferData* CreateConstantBufferData(::Video::ConstantBuffer* owner) override; // [11] 0x180005730  0x28 bytes, vt 0x1800185d0
    ::Video::IndexBufferData*    CreateIndexBufferData(::Video::IndexBuffer* owner) override;       // [12] 0x1800057a0  0x20 bytes, vt 0x1800187c8
    ::Video::VertexBufferData*   CreateVertexBufferData(::Video::VertexBuffer* owner) override;     // [13] 0x180005810  0x20 bytes, vt 0x180018830
    ::Video::VertexLayoutData*   CreateVertexLayoutData(::Video::VertexLayout* owner) override;     // [14] 0x180005880  0x18 bytes, vt 0x180018878
    ::Video::VertexProgramData*  CreateVertexProgramData(::Video::VertexProgram* owner) override;   // [15] 0x1800058f0  0x30 bytes, vt 0x180018890
    ::Video::PixelProgramData*   CreatePixelProgramData(::Video::PixelProgram* owner) override;     // [16] 0x180005960  0x30 bytes, vt 0x180018800
    ::Video::RenderStateData*    CreateRenderStateData(::Video::RenderState* owner) override;       // [17] 0x1800059d0  0x28 bytes, vt 0x180018818
    ::Video::VideoProgramData*   CreateVideoProgramData(::Video::VideoProgram* owner) override;     // [18] 0x180005a40  0x10 bytes, vt 0x1800185a8
    ::Video::RenderBufferData*   CreateRenderBufferData(::Video::RenderBuffer* owner) override;     // [19] 0x180005aa0  0x10 bytes, vt 0x180018588

    // +0x10 IDXGIFactory1* — CONFIRMED (L6), no longer TBD. Created by slot 2's
    // CreateDXGIFactory1(IID at 0x180018b98 = IID_IDXGIFactory1, &this[0x10]),
    // and used for EnumAdapters1 (vtable +0x60 = index 12), MakeWindowAssociation
    // (+0x40 = 8) and CreateSwapChain (+0x50 = 10).
    //
    // It is NOT released on shutdown, contrary to the earlier note: slot 2
    // releases it at 0x1800050c2 as soon as the swap chain exists and never
    // nulls the field, so this dangles for the rest of the session. Slot 3 does
    // not touch it, which is correct — releasing it again would be a double
    // release.
    IDXGIFactory1*       unk10;          // +0x10  IDXGIFactory1*, dangling after slot 2
    IDXGISwapChain*      swapChain;      // +0x18  (SetFullscreenState(FALSE) before release)
    ID3D11Device*        d3dDevice;      // +0x20  (CreateTexture2D callee in 0x1800052b0)
    ID3D11DeviceContext* d3dContext;     // +0x28  (every resource Bind reads this)
    ID3D11Texture2D*     depthBuffer;    // +0x30  D24_UNORM_S8_UINT, BIND_DEPTH_STENCIL
    ID3D11Texture2D*     colorBuffer;    // +0x38  B8G8R8A8_UNORM,    BIND_RENDER_TARGET

    // +0x40 "was fullscreen, restore it when focus returns" — NOT "is
    // fullscreen". The live fullscreen flag is Video::Configuration+0x40; this
    // is the backup the WM_ACTIVATE handler (0x1800042d0) parks it in:
    //
    //   WA_INACTIVE : wasFullscreen = 1; Configuration.fullscreen = 0; resize
    //   WA_ACTIVE   : if (wasFullscreen) { wasFullscreen = 0;
    //                                      Configuration.fullscreen = 1; resize }
    //
    // Slot 3 mirrors it back into **Video::Configuration+0x40** on shutdown
    // (0x18000520f loads IAT 0x180013480 = ?_singleton@Configuration@Video@@),
    // so quitting while alt-tabbed still saves VideoFullscreen=true. An earlier
    // revision of this comment and of decomp/videodx11.md said
    // Application::_singleton+0x40 — that was one of the three
    // `_singleton_exref` mix-ups L6 corrected.
    uint8_t   wasFullscreen;     // +0x40  restore-fullscreen-on-refocus latch
    uint8_t   pad41[7];          // +0x41

    // Slot 3 DestroyCursor()s +0x48 without nulling it, so it dangles after
    // shutdown; slot 8's identity check then compares against a stale handle.
    Platform::CursorHandle cursor;         // +0x48  HCURSOR built by slot 8 from a 32x32 ImageBuffer
    ::Video::ImageBuffer* cursorImage;     // +0x50  the ImageBuffer the cursor was built from
    int32_t               cursorHotspotX;  // +0x58  CreateCursor xHotSpot
    int32_t               cursorHotspotY;  // +0x5c  CreateCursor yHotSpot

    // Registry of the Video::ImageBuffer*s that must be released and recreated
    // across a device / swap-chain reset. Membership test (from ~ImageBufferData):
    //   IsGenerated() || (flags & 0x70) || IsRendered()
    // Appended by 0x180002300 (add-if-absent); removed by ~ImageBufferData with a
    // stable SHIFT-ERASE (`*p = p[1]` walking forward), not a swap-remove — order
    // is preserved and a port must preserve it.
    // NOT the Device::GraphicsInterface array — the older note had that wrong;
    // the stride here is 8, not 0x18.
    ::Video::ImageBuffer** trackedImages;  // +0x60  Video::ImageBuffer*[]
    int32_t                trackedCount;   // +0x68
    int32_t                trackedCapacity;// +0x6c
};
static_assert(sizeof(DeviceData) == 0x70, "DeviceData size (MemAlloc 0x70 @ 0x1800011a0)");
static_assert(offsetof(DeviceData, _owner) == 0x8, "DeviceData::_owner (the base's)");
static_assert(offsetof(DeviceData, unk10) == 0x10, "DeviceData::unk10");
static_assert(offsetof(DeviceData, pad41) == 0x41, "DeviceData::pad41");
static_assert(offsetof(DeviceData, cursorHotspotY) == 0x5C, "DeviceData::cursorHotspotY");
static_assert(offsetof(DeviceData, swapChain)       == 0x18, "DeviceData::swapChain");
static_assert(offsetof(DeviceData, d3dDevice)       == 0x20, "DeviceData::d3dDevice");
static_assert(offsetof(DeviceData, d3dContext)      == 0x28, "DeviceData::d3dContext");
static_assert(offsetof(DeviceData, depthBuffer)     == 0x30, "DeviceData::depthBuffer");
static_assert(offsetof(DeviceData, colorBuffer)     == 0x38, "DeviceData::colorBuffer");
static_assert(offsetof(DeviceData, wasFullscreen)   == 0x40, "DeviceData::wasFullscreen");
static_assert(offsetof(DeviceData, cursor)          == 0x48, "DeviceData::cursor");
static_assert(offsetof(DeviceData, cursorImage)     == 0x50, "DeviceData::cursorImage");
static_assert(offsetof(DeviceData, cursorHotspotX)  == 0x58, "DeviceData::cursorHotspotX");
static_assert(offsetof(DeviceData, trackedImages)   == 0x60, "DeviceData::trackedImages");
static_assert(offsetof(DeviceData, trackedCount)    == 0x68, "DeviceData::trackedCount");
static_assert(offsetof(DeviceData, trackedCapacity) == 0x6c, "DeviceData::trackedCapacity");

// (B5 DEV-A: the `DeviceDataSlot` index enum that stood here had no reader and
// its numbers stopped naming compiler slots with the flip; the retail address of
// every slot is on its override declaration above.)

// ============================================================
// Resource *Data subclasses.
//
// All twelve vtables were walked 2026-07-24, so each class's slot count is
// exact (the override lists below and the status board in ../README.md). Fields named
// here are proven by a decompiled slot; the rest stay raw. Do not guess.
//
// Every resource class except ContextData reaches D3D11 the same way — it does
// NOT cache the device or context, it walks to them on each call:
//
//     g_Device (0x1800134b8) -> Video::Device* -> [+0x100] DeviceData
//                            -> [+0x28] ID3D11DeviceContext*
//
// ContextData is the exception: it caches the context at +0x18.
// ============================================================

// 0x20, vtable 0x1800186f0 (20 slots), factory 0x180005650
//
// REIMPLEMENTED — src/render/dx11/contextdata.cpp (all 20 slots, lane L5).
//
// The one class in the DLL that CACHES the immediate context instead of
// re-walking g_Device->[0x100]->[0x28] on every call. The cache is filled by
// slot 1 (BeginCommands, 0x18000170f) from exactly that chain, and every other
// slot reads `this->d3dContext`. It is a borrowed pointer: nothing AddRefs it
// and slot 0 does not Release it.
//
// +0x10 is the *last primitive topology handed to IASetPrimitiveTopology*, a
// redundant-call filter. The factory (0x180005650) writes 0 there, which is
// D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED and therefore never equal to a value the
// class would set — so the first draw always issues the call. Proven by
// 0x180001de4 (`CMP [RBX+0x10],4` guarding `[ctxvt+0xc0]` = index 24 =
// IASetPrimitiveTopology) and by slots 15/19 which do the same for topology 2.
//
// B5 CTX: a real C++ subclass of `Video::ContextData`; the compiler builds its
// vtable (retail slot N >= 1 is compiler slot N + 1, which nothing indexes).
// `_owner` (+0x08, `Video::Context*`) is the base's and replaces the old
// `context` field. Owner type confirmed by
// ??0ContextData@Video@@QEAA@PEAVContext@1@@Z.
class ContextData final : public ::Video::ContextData {
public:
    // Factory 0x180005650: MemAlloc(0x20, 4) + ??0ContextData(owner) + the vptr
    // store, then exactly these three: +0x10 and +0x14 as DWORDS (0 =
    // D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED), +0x18 as a qword. Nothing else.
    explicit ContextData(::Video::Context* owner)
        : ::Video::ContextData(owner) {
        topology   = 0;
        pad14      = 0;
        d3dContext = nullptr;
    }
    ~ContextData() override;                                                // [ 0] 0x180001600, chains ??1ContextData@Video@@UEAA@XZ
    bool BeginCommands() override;                                          // [ 1] 0x180001650
    void EndCommands() override;                                            // [ 2] 0x180001720
    void Finish() override;                                                 // [ 3] 0x1800017c0  Present + device-lost
    void Clear(::Video::ClearFlags flags, unsigned color,
               float depth, int stencil) override;                          // [ 4] 0x180001920
    void BindRenderBuffer() override;                                       // [ 5] 0x180001ad0
    void BindViewport() override;                                           // [ 6] 0x180001b60
    void BindScissor() override;                                            // [ 7] 0x180001be0
    void BindTransforms() override;                                         // [ 8] 0x180001c40
    void BindTextureNone(int slot) override;                                // [ 9] 0x180001cb0
    void BindIndexNone() override;                                          // [10] 0x180001d10  (a tail)
    void BindVertexNone(int slot) override;                                 // [11] 0x180001d30
    void DrawIndexed(int primitiveType, int baseVertex, int vertexCount,
                     int startIndex, int indexCount,
                     int instanceCount) override;                           // [12] 0x180001d80
    int  GetMaxDynamicInstances(int a, int b, int c) override;              // [13] 0x180001ef0  (a tail) -> 52
    bool BeginVertices(int vertexCount, int vertexStride) override;         // [14] 0x180001e60
    void EndVertices(int primitiveType) override;                           // [15] 0x180001f00
    bool BeginIndexedVertices(int vertexCount, int vertexStride,
                              int indexCount) override;                     // [16] 0x180002000
    void EndIndexedVertices(int primitiveType) override;                    // [17] 0x180002020  `jmp` slot 19
    bool BeginInstancedIndexedVertices(int vertexCount, int vertexStride,
                                       int indexCount,
                                       int instanceCount) override;         // [18] 0x180002030
    void EndInstancedIndexedVertices(int primitiveType) override;           // [19] 0x180002150

    int32_t              topology;   // +0x10  last D3D11_PRIMITIVE_TOPOLOGY set; factory writes 0
    int32_t              pad14;      // +0x14  factory writes 0; NOT RETAIL: last Dx11ClearStateGeneration() seen (contextdata.cpp)
    ID3D11DeviceContext* d3dContext; // +0x18  cached by slot 1; BORROWED (never AddRef'd / Released)

private:
    // Slots 4 and 7 share this (inlined at 0x180001920, standalone at 0x180001be0).
    void SetScissorFromContext();
    // Slots 15, 17 and 19's identical topology filter.
    void ApplyPrimitiveType(int primitiveType);
};
static_assert(sizeof(ContextData) == 0x20, "MemAlloc 0x20 @ 0x180005650");
static_assert(offsetof(ContextData, _owner)     == 0x08, "ContextData::_owner (the base's)");
static_assert(offsetof(ContextData, topology)   == 0x10, "ContextData::topology");
static_assert(offsetof(ContextData, pad14)      == 0x14, "ContextData::pad14");
static_assert(offsetof(ContextData, d3dContext) == 0x18, "ContextData::d3dContext");

// ContextData vtable slot indices (0x1800186f0, 20 slots).
//
// These names are NOT inferred from the DX11 code — every one is the Runtime
// method that dispatches to the slot. Runtime's Video::Context keeps its
// Video::ContextData* at Context+0x2160 and each public method is a two-load
// tail-jump through it, so the (name -> byte offset) pairing is read straight
// out of the thunk. Examples:
//     Video::Context::BindTransforms          0x1800f22a0  jmp [platform+0x40]
//     Video::Context::GetMaxDynamicInstances  0x1800f21d0  jmp [platform+0x68]
//     Video::Context::EndInstancedIndexedVertices 0x1800f2110 jmp [platform+0x98]
// All twenty were checked this way; see src/render/dx11/contextdata.cpp.
// (B5: the `ContextDataSlot` index enum is gone -- retail's slot numbers and
// addresses are on the override declarations above; GCC's table has two
// destructor slots, so the numbers never named compiler slots after the flip.)
// * = not a Ghidra function; a tail inside the preceding function's body.
//     Slot 17 (0x180002020) is `jmp 0x180002150`, i.e. it IS slot 19 — the DX11
//     backend implements EndIndexedVertices and EndInstancedIndexedVertices
//     with one body, which works because slot 18 stores the instance count on
//     the Context and slot 16 passes 1.

// 0x38, vtable 0x180018790, factory 0x1800056c0
//
// All five COM interfaces identified 2026-07-24 from the seven slots (lane L3).
// B5 RES: the fields are the typed D3D11 interfaces (D4), forward-declared at
// the top of this file so it still does not force <d3d11.h> on its includers.
//
//   +0x10 ID3D11SamplerState*       created by 0x180002720 via CreateSamplerState
//                                   (ID3D11Device vtable +0xb8 = index 23)
//   +0x18 ID3D11Texture2D*          created by slot 3 via CreateTexture2D (+0x28,
//                                   index 5); slots 1/2 instead AddRef the
//                                   DeviceData color/depth buffer into it
//   +0x20 ID3D11RenderTargetView*   CreateRenderTargetView (+0x48, index 9)
//   +0x28 ID3D11DepthStencilView*   CreateDepthStencilView (+0x50, index 10)
//   +0x30 ID3D11ShaderResourceView* CreateShaderResourceView (+0x38, index 7)
//
// The dtor (0x180002430) and slot 3 (0x180002870) both release these in the
// order +0x28, +0x20, +0x30, +0x10, +0x18 and then null all five. That order is
// reproduced verbatim in the port — it is not the declaration order.
//
// B5 RES: a real C++ subclass of `Video::ImageBufferData`; the compiler builds
// its vtable (retail slot N >= 1 is compiler slot N + 1, which nothing indexes).
// `_owner` (+0x08, `Video::ImageBuffer*`) is the base's and replaces the old
// `imageBuffer` field.
class ImageBufferData final : public ::Video::ImageBufferData {
public:
    // Factory 0x1800056c0: MemAlloc(0x38, 4) + ??0ImageBufferData(owner) + the
    // vptr store + the five null stores. Nothing else is stored.
    explicit ImageBufferData(::Video::ImageBuffer* owner)
        : ::Video::ImageBufferData(owner),
          sampler(nullptr), texture(nullptr), renderTargetView(nullptr),
          depthStencilView(nullptr), shaderResourceView(nullptr) {}
    ~ImageBufferData() override;                                     // [0] 0x180002400 -> 0x180002430, chains ??1ImageBufferData@Video@@UEAA@XZ
    void AttachColorBuffer() override;                               // [1] 0x180002580  alias DeviceData::colorBuffer + RTV
    void AttachDepthBuffer() override;                               // [2] 0x180002650  alias DeviceData::depthBuffer + DSV
    bool Create(const Collection::Array<Collection::Array<uint8_t> >* levels) override;  // [3] 0x180002870  texture + SRV + RTV/DSV + sampler
    void Bind(void* context, unsigned slot) override;                // [4] 0x180002da0  PSSetSamplers + PSSetShaderResources
    bool Update(void* data, int width, int height) override;         // [5] 0x180002e10  Map/WRITE_DISCARD or UpdateSubresource
    bool Read(void* dst, int width, int height) override;            // [6] 0x180002ef0  staging copy + Map/READ_WRITE readback

    ID3D11SamplerState*       sampler;            // +0x10
    ID3D11Texture2D*          texture;            // +0x18
    ID3D11RenderTargetView*   renderTargetView;   // +0x20
    ID3D11DepthStencilView*   depthStencilView;   // +0x28
    ID3D11ShaderResourceView* shaderResourceView; // +0x30

private:
    // The helpers slot 3 and the destructor share: the five releases inlined at
    // 0x180002507 / 0x18000292a, and the sampler builder 0x180002720.
    void ReleaseInterfaces();
    // NOT RETAIL (ext.texture_quality, docs/extensions/handins/TEXQUALITY.md): `droppedLevels` lowers
    // MaxLOD by the top levels the texture was created without; 0 = retail's sampler.
    void CreateSamplerState(int droppedLevels = 0);
};
static_assert(sizeof(ImageBufferData) == 0x38, "MemAlloc 0x38 @ 0x1800056c0");
static_assert(offsetof(ImageBufferData, _owner) == 0x08, "ImageBufferData::_owner (the base's)");
static_assert(offsetof(ImageBufferData, sampler)            == 0x10, "ImageBufferData::sampler");
static_assert(offsetof(ImageBufferData, texture)            == 0x18, "ImageBufferData::texture");
static_assert(offsetof(ImageBufferData, renderTargetView)   == 0x20, "ImageBufferData::renderTargetView");
static_assert(offsetof(ImageBufferData, depthStencilView)   == 0x28, "ImageBufferData::depthStencilView");
static_assert(offsetof(ImageBufferData, shaderResourceView) == 0x30, "ImageBufferData::shaderResourceView");

// ImageBufferData vtable slot indices (0x180018790, 7 slots) — recovered by
// lane L3. Names describe what the code does; the Runtime-side Video::ImageBufferData
// interface names are not recoverable from this DLL alone.
// (B5: the `ImageBufferDataSlot` index enum is gone; see the overrides above.)

// 0x28, vtable 0x1800185d0 (4 slots), factory 0x180005730
//
// +0x18 is a growable array of ID3D11Buffer* — the dtor (0x180001270) walks
// [0, count) calling Release (COM vtable +0x10) on each, then MemFrees the
// block; slot 1 (0x180001300) sizes it through the generic Array::SetLength at
// 0x180001550. Slot 3 (0x180001450) returns owner->[0x14] + 1.
//
// The array is a LADDER when the owner is "banked" (Video::ConstantBuffer+0x19):
// buffers[i] is created with ByteWidth = owner->bankStride * (i + 1), so slot 2
// can bind the smallest buffer that still holds the payload. +0x10 is the index
// of the rung currently bound; slot 2 only re-issues VS/PSSetConstantBuffers
// when it changes. (Proven by 0x180001300 and 0x180001460 — lane L1.)
//
// REIMPLEMENTED — src/render/dx11/buffers.cpp (all 4 slots).
//
// B5 BUF: a real C++ subclass of `Video::ConstantBufferData`; the compiler builds
// its vtable (retail slot N >= 1 is compiler slot N + 1, which nothing indexes).
// `_owner` (+0x08, `Video::ConstantBuffer*`) is the base's and replaces the old
// `constantBuffer` field. Owner type confirmed by
// ??0ConstantBufferData@Video@@QEAA@PEAVConstantBuffer@1@@Z.
class ConstantBufferData final : public ::Video::ConstantBufferData {
public:
    // Factory 0x180005730: MemAlloc(0x28, 4) + ??0ConstantBufferData(owner) + the
    // vptr store + activeIndex = -1, pad14 = 0, buffers = null, count = capacity = 0;
    // then (OURS, not retail) Dx11TrackConstantBuffer(this) — buffers.cpp.
    explicit ConstantBufferData(::Video::ConstantBuffer* owner);
    ~ConstantBufferData() override;                      // [0] 0x180001240 -> ~ 0x180001270, chains ??1ConstantBufferData@Video@@UEAA@XZ
    bool Create() override;                              // [1] 0x180001300  offsets + the ID3D11Buffer ladder
    void Bind(void* context, const void* srcData, int32_t first,
              int32_t sizeBytes) override;               // [2] 0x180001460  VS/PSSetConstantBuffers + UpdateSubresource
    int32_t SlotIndexPlusOne() override;                 // [3] 0x180001450  owner->_uploadIndex + 1

    int32_t        activeIndex;    // +0x10  index into buffers[] currently bound; factory inits to -1
    int32_t        pad14;
    ID3D11Buffer** buffers;        // +0x18  ID3D11Buffer*[]
    int32_t        bufferCount;    // +0x20
    int32_t        bufferCapacity; // +0x24
};
static_assert(sizeof(ConstantBufferData) == 0x28, "MemAlloc 0x28 @ 0x180005730");
static_assert(offsetof(ConstantBufferData, _owner) == 0x8, "ConstantBufferData::_owner (the base's)");
static_assert(offsetof(ConstantBufferData, pad14) == 0x14, "ConstantBufferData::pad14");
static_assert(offsetof(ConstantBufferData, activeIndex)    == 0x10, "ConstantBufferData::activeIndex");
static_assert(offsetof(ConstantBufferData, buffers)        == 0x18, "ConstantBufferData::buffers");
static_assert(offsetof(ConstantBufferData, bufferCount)    == 0x20, "ConstantBufferData::bufferCount");
static_assert(offsetof(ConstantBufferData, bufferCapacity) == 0x24, "ConstantBufferData::bufferCapacity");

// 0x20, vtable 0x1800187c8 (7 slots), factory 0x1800057a0
//
// Slot 5 (0x180003380) is IASetIndexBuffer(this->buffer, DXGI_FORMAT_R16_UINT,
// 0) — so indices are always 16-bit, and +0x10 is the ID3D11Buffer.
// Slots 3 and 4 are shared verbatim with VertexBufferData (0x180003290 /
// 0x180003340): the two classes have identical Lock/Unlock code.
//
// +0x18 / +0x1c recovered from the shared Lock/Unlock (lane L1):
//   +0x18  byte offset the NEXT Lock will hand out. Lock maps
//          WRITE_NO_OVERWRITE and appends while it fits, WRITE_DISCARD and
//          rewinds to 0 when it does not — a classic dynamic ring buffer. The
//          cursor advances by the request rounded up to an even byte count.
//   +0x1c  bool, 1 between Lock and Unlock. Nothing in the DLL reads it; it
//          exists for the Runtime-side base class / debug assertions.
//
// REIMPLEMENTED — src/render/dx11/buffers.cpp (all 7 slots).
//
// B5 BUF: a real C++ subclass of `Video::IndexBufferData`; the compiler builds its
// vtable. `_owner` (+0x08, `Video::IndexBuffer*`) is the base's and replaces the
// old `indexBuffer` field. Owner type confirmed by
// ??0IndexBufferData@Video@@QEAA@PEAVIndexBuffer@1@@Z.
class IndexBufferData final : public ::Video::IndexBufferData {
public:
    // Factory 0x1800057a0: MemAlloc(0x20, 4) + ??0IndexBufferData(owner) + the vptr
    // store + buffer = null, +0x18 as a DWORD 0, +0x1c as a single BYTE 0.
    // +0x1d..+0x1f are left as MemAlloc left them, so `pad1d` is deliberately NOT
    // in the initialiser list.
    explicit IndexBufferData(::Video::IndexBuffer* owner)
        : ::Video::IndexBufferData(owner) {
        buffer      = nullptr;
        writeCursor = 0;
        locked      = 0;
    }
    ~IndexBufferData() override;                                         // [0] 0x180003080, chains ??1IndexBufferData@Video@@UEAA@XZ
    bool  Create(uint32_t sizeBytes, uint8_t dynamic, const void* data) override;   // [1] 0x1800030f0
    bool  SetData(int32_t sizeBytes, int32_t offsetBytes, const void* src) override; // [2] 0x1800031c0
    void* Lock(int32_t sizeBytes, int32_t* outByteOffset) override;     // [3] 0x180003290  SHARED with VertexBufferData[3]
    void  Unlock() override;                                             // [4] 0x180003340  SHARED with VertexBufferData[4]
    void  Bind(void* context) override;                                  // [5] 0x180003380  IASetIndexBuffer(R16_UINT)
    void  Serialize(::IO::Serializer* serializer) override;              // [6] 0x1800033b0

    ID3D11Buffer* buffer;      // +0x10  ID3D11Buffer*
    int32_t       writeCursor; // +0x18  next Lock offset in bytes
    uint8_t       locked;      // +0x1c  (factory writes a byte here, not a dword)
    uint8_t       pad1d[3];
};
static_assert(sizeof(IndexBufferData) == 0x20, "MemAlloc 0x20 @ 0x1800057a0");
static_assert(offsetof(IndexBufferData, _owner) == 0x8, "IndexBufferData::_owner (the base's)");
static_assert(offsetof(IndexBufferData, pad1d) == 0x1D, "IndexBufferData::pad1d");
static_assert(offsetof(IndexBufferData, buffer)      == 0x10, "IndexBufferData::buffer");
static_assert(offsetof(IndexBufferData, writeCursor) == 0x18, "IndexBufferData::writeCursor");
static_assert(offsetof(IndexBufferData, locked)      == 0x1c, "IndexBufferData::locked");

// 0x20, vtable 0x180018830 (7 slots), factory 0x180005810
// Same shape as IndexBufferData, and slots 3/4 are literally the same code.
//
// REIMPLEMENTED — src/render/dx11/buffers.cpp (all 7 slots).
//
// B5 BUF: a real C++ subclass of `Video::VertexBufferData`; the compiler builds
// its vtable. `_owner` (+0x08, `Video::VertexBuffer*`) is the base's and replaces
// the old `vertexBuffer` field. Owner type confirmed by
// ??0VertexBufferData@Video@@QEAA@PEAVVertexBuffer@1@@Z.
class VertexBufferData final : public ::Video::VertexBufferData {
public:
    // Factory 0x180005810: the same stores as IndexBufferData's (0x1800057a0) —
    // buffer = null, +0x18 DWORD 0, +0x1c BYTE 0, +0x1d..+0x1f untouched.
    explicit VertexBufferData(::Video::VertexBuffer* owner)
        : ::Video::VertexBufferData(owner) {
        buffer      = nullptr;
        writeCursor = 0;
        locked      = 0;
    }
    ~VertexBufferData() override;                                        // [0] 0x180003b20, chains ??1VertexBufferData@Video@@UEAA@XZ
    bool  Create(uint32_t sizeBytes, uint8_t dynamic, const void* data) override;   // [1] 0x180003b90
    bool  SetData(int32_t sizeBytes, int32_t offsetBytes, const void* src) override; // [2] 0x180003c60
    void* Lock(int32_t sizeBytes, int32_t* outByteOffset) override;     // [3] 0x180003290  SHARED with IndexBufferData[3]
    void  Unlock() override;                                             // [4] 0x180003340  SHARED with IndexBufferData[4]
    void  Bind(void* context, uint32_t startSlot, uint32_t offsetBytes,
               uint32_t strideBytes) override;                           // [5] 0x180003d30  IASetVertexBuffers
    void  Serialize(::IO::Serializer* serializer) override;              // [6] 0x180003d90

    ID3D11Buffer* buffer;      // +0x10  ID3D11Buffer*
    int32_t       writeCursor; // +0x18  next Lock offset in bytes
    uint8_t       locked;      // +0x1c
    uint8_t       pad1d[3];
};
static_assert(sizeof(VertexBufferData) == 0x20, "MemAlloc 0x20 @ 0x180005810");
static_assert(offsetof(VertexBufferData, _owner) == 0x8, "VertexBufferData::_owner (the base's)");
static_assert(offsetof(VertexBufferData, pad1d) == 0x1D, "VertexBufferData::pad1d");
static_assert(offsetof(VertexBufferData, buffer)      == 0x10, "VertexBufferData::buffer");
static_assert(offsetof(VertexBufferData, writeCursor) == 0x18, "VertexBufferData::writeCursor");
static_assert(offsetof(VertexBufferData, locked)      == 0x1c, "VertexBufferData::locked");

// Slots 3 (0x180003290, Lock) and 4 (0x180003340, Unlock) are the SAME two
// addresses in both vtables — the shipped DLL has exactly one copy of the code
// and both classes dispatch to it. src/render/dx11/buffers.cpp likewise has exactly
// one implementation of each — B5 BUF: a template helper over the three shared
// fields, called from both classes' `Lock` / `Unlock` members (no cast between
// the two companion types). These asserts are what keeps that one body honest:
// if a future edit moves a field in one class and not the other, the build
// breaks here instead of the two members silently diverging from retail's one.
static_assert(sizeof(VertexBufferData) == sizeof(IndexBufferData),
              "VertexBufferData/IndexBufferData must stay layout-identical (shared slots 3/4)");
static_assert(offsetof(VertexBufferData, buffer) == offsetof(IndexBufferData, buffer),
              "shared slot 3/4: buffer offset must match");
static_assert(offsetof(VertexBufferData, writeCursor) == offsetof(IndexBufferData, writeCursor),
              "shared slot 3/4: writeCursor offset must match");
static_assert(offsetof(VertexBufferData, locked) == offsetof(IndexBufferData, locked),
              "shared slot 3/4: locked offset must match");

// 0x18, vtable 0x180018878 (3 slots), factory 0x180005880
//
// REIMPLEMENTED — src/render/dx11/vertexlayoutdata.cpp (all 3 slots).
//
//   slot 0  0x180003f60  scalar deleting dtor: releases inputLayout, chains
//                        ??1VertexLayoutData@Video@@UEAA@XZ
//   slot 1  0x180003fc0  Bind(Context&)   -> IASetInputLayout(this->inputLayout)
//   slot 2  0x180003ff0  PlatformCreate() -> CreateInputLayout; this is the DLL's
//                        only user of the "TEXCOORD" string at 0x180018868, and
//                        it names element i "TEXCOORD" with SemanticIndex i,
//                        ignoring the Usage/usageIndex the layout carries.
//
// Owner type confirmed by the Runtime ctor export
// ??0VertexLayoutData@Video@@QEAA@PEAVVertexLayout@1@@Z. The Video::VertexLayout
// fields slot 2 reads (element array @ +0x20/+0x28, input signature @
// +0x38/+0x40) and the 0x20-byte Element layout are pinned with static_asserts
// in src/render/dx11/dx11_backend_access.h.
//
// B5 RES: a real C++ subclass of `Video::VertexLayoutData`; the compiler builds
// its vtable (retail slot N >= 1 is compiler slot N + 1, which nothing indexes).
// `_owner` (+0x08, `Video::VertexLayout*`) is the base's and replaces the old
// `vertexLayout` field; `inputLayout` is the typed COM pointer (B5_RULES D4).
class VertexLayoutData final : public ::Video::VertexLayoutData {
public:
    // Factory 0x180005880: MemAlloc(0x18, 4) + ??0VertexLayoutData(owner) + the
    // vptr store + `inputLayout = nullptr`. Nothing else is stored.
    explicit VertexLayoutData(::Video::VertexLayout* owner)
        : ::Video::VertexLayoutData(owner), inputLayout(nullptr) {}
    ~VertexLayoutData() override;                        // [0] 0x180003f60, chains ??1VertexLayoutData@Video@@UEAA@XZ
    void Bind(void* context) override;                   // [1] 0x180003fc0  IASetInputLayout
    void PlatformCreate() override;                      // [2] 0x180003ff0  CreateInputLayout

    ID3D11InputLayout* inputLayout;  // +0x10  built with the "TEXCOORD" semantic string
};
static_assert(sizeof(VertexLayoutData) == 0x18, "MemAlloc 0x18 @ 0x180005880");
static_assert(offsetof(VertexLayoutData, _owner) == 0x08, "VertexLayoutData::_owner (the base's)");
static_assert(offsetof(VertexLayoutData, inputLayout) == 0x10, "VertexLayoutData::inputLayout");

// 0x30, vtable 0x180018890 (3 slots), factory 0x1800058f0
//
// REIMPLEMENTED — src/render/dx11/shaderprograms.cpp (all 3 slots).
//
//   slot 0  0x180004110  scalar deleting dtor: releases all 4 variants, chains
//                        ??1VertexProgramData@Video@@UEAA@XZ
//   slot 1  0x1800041a0  PlatformCreate() -> bool. Loops i in [0,4), skipping
//                        i >= owner->byteCode.length, and calls
//                        CreateVertexShader(blob[i].data, blob[i].length,
//                        nullptr, &variants[i]). Any non-zero HRESULT returns
//                        false immediately.
//   slot 2  0x180004230  Bind(Context&, int permutation):
//                        VSSetShader(variants[variants[p] ? p : 0], nullptr, 0)
//
// So +0x10 is a 4-entry array of shader-permutation objects with index 0 as the
// fallback when the requested permutation was never compiled. The permutation
// byte code lives on the owner: Video::ShaderProgram+0x40 is a
// Collection::Array<Collection::Array<unsigned char>>, one blob per
// permutation, pinned with static_asserts in src/render/dx11/dx11_backend_access.h.
// Owner type confirmed by the Runtime ctor export
// ??0VertexProgramData@Video@@QEAA@PEAVVertexProgram@1@@Z.
//
// B5 RES: a real C++ subclass of `Video::VertexProgramData`; the compiler builds
// its vtable. `_owner` (+0x08, `Video::VertexProgram*`) is the base's and
// replaces the old `vertexProgram` field; the variants are typed (D4).
class VertexProgramData final : public ::Video::VertexProgramData {
public:
    // Factory 0x1800058f0: MemAlloc(0x30, 4) + ??0VertexProgramData(owner) + the
    // vptr store + the four `variants[i] = nullptr`. Nothing else is stored.
    explicit VertexProgramData(::Video::VertexProgram* owner)
        : ::Video::VertexProgramData(owner) {
        for (int i = 0; i < 4; ++i) variants[i] = nullptr;
    }
    ~VertexProgramData() override;                       // [0] 0x180004110, chains ??1VertexProgramData@Video@@UEAA@XZ
    bool PlatformCreate() override;                      // [1] 0x1800041a0  CreateVertexShader x4
    void Bind(void* context, int permutation) override;  // [2] 0x180004230  VSSetShader

    ID3D11VertexShader* variants[4];  // +0x10  one per shader permutation
};
static_assert(sizeof(VertexProgramData) == 0x30, "MemAlloc 0x30 @ 0x1800058f0");
static_assert(offsetof(VertexProgramData, _owner) == 0x08, "VertexProgramData::_owner (the base's)");
static_assert(offsetof(VertexProgramData, variants) == 0x10, "VertexProgramData::variants");
static_assert(sizeof(VertexProgramData::variants) == 0x20, "4 shader permutations");

// 0x30, vtable 0x180018800 (3 slots), factory 0x180005960
//
// REIMPLEMENTED — src/render/dx11/shaderprograms.cpp (all 3 slots).
//
// Mirror of VertexProgramData in every respect; the three functions differ only
// in the vtable they store, the Runtime base dtor they chain
// (??1PixelProgramData@Video@@UEAA@XZ), and CreatePixelShader (device vtable
// index 15) / PSSetShader (context vtable index 9) in place of the vertex
// equivalents. Owner type confirmed by
// ??0PixelProgramData@Video@@QEAA@PEAVPixelProgram@1@@Z; it derives from the
// same Video::ShaderProgram, and slot 1 reads the identical +0x40/+0x48 fields.
//
// B5 RES: a real C++ subclass of `Video::PixelProgramData`, the mirror of the
// vertex class above; `_owner` (+0x08, `Video::PixelProgram*`) replaces the old
// `pixelProgram` field.
class PixelProgramData final : public ::Video::PixelProgramData {
public:
    // Factory 0x180005960: MemAlloc(0x30, 4) + ??0PixelProgramData(owner) + the
    // vptr store + the four `variants[i] = nullptr`. Nothing else is stored.
    explicit PixelProgramData(::Video::PixelProgram* owner)
        : ::Video::PixelProgramData(owner) {
        for (int i = 0; i < 4; ++i) variants[i] = nullptr;
    }
    ~PixelProgramData() override;                        // [0] 0x180003580, chains ??1PixelProgramData@Video@@UEAA@XZ
    bool PlatformCreate() override;                      // [1] 0x180003610  CreatePixelShader x4
    void Bind(void* context, int permutation) override;  // [2] 0x1800036a0  PSSetShader

    ID3D11PixelShader* variants[4];   // +0x10  one per shader permutation
};
static_assert(sizeof(PixelProgramData) == 0x30, "MemAlloc 0x30 @ 0x180005960");
static_assert(offsetof(PixelProgramData, _owner) == 0x08, "PixelProgramData::_owner (the base's)");
static_assert(offsetof(PixelProgramData, variants) == 0x10, "PixelProgramData::variants");
static_assert(sizeof(PixelProgramData::variants) == 0x20, "4 shader permutations");

// 0x28, vtable 0x180018818 (3 slots), factory 0x1800059d0
//
// The three COM slots are pinned in both directions, so there is no ambiguity
// about which is which:
//   slot 1 "Create" (0x180003760) writes them via
//     CreateBlendState        -> &this[0x10]   (device vtable +0xa0, @0x180003a3f)
//     CreateRasterizerState   -> &this[0x20]   (device vtable +0xb0, @0x180003a53)
//     CreateDepthStencilState -> &this[0x18]   (device vtable +0xa8, @0x180003a68)
//   slot 2 "Bind"   (0x180003a90) reads them back through the matching setters
//     OMSetBlendState(this[0x10], {0,0,0,0}, 0xFFFFFFFF)   (ctx +0x118)
//     OMSetDepthStencilState(this[0x18], renderState[0x4c])(ctx +0x120)
//     RSSetState(this[0x20])                               (ctx +0x158)
//
// slot 0 (0x1800036e0) releases them in *field* order — blend, depth-stencil,
// rasterizer — which is deliberately not the creation order.
//
// The +0x08 owner is a Video::RenderState; its field map (the source of every
// state-desc value) is the real class's (src/engine/video/RenderState.h),
// read by name in src/render/dx11/renderstatedata.cpp, and is described in the
// "RenderState -> D3D11 state objects" section of ../../decomp/videodx11.md.
//
// B5 RES: a real C++ subclass of `Video::RenderStateData`; the compiler builds
// its vtable. `_owner` (+0x08, `Video::RenderState*`) is the base's and replaces
// the old `renderState` field; the three state objects are typed (D4).
class RenderStateData final : public ::Video::RenderStateData {
public:
    // Factory 0x1800059d0: MemAlloc(0x28, 4) + ??0RenderStateData(owner) + the
    // vptr store + the three null stores. Nothing else is stored.
    explicit RenderStateData(::Video::RenderState* owner)
        : ::Video::RenderStateData(owner),
          blendState(nullptr), depthStencilState(nullptr), rasterizerState(nullptr) {}
    ~RenderStateData() override;                         // [0] 0x1800036e0, chains ??1RenderStateData@Video@@UEAA@XZ
    bool Create() override;                              // [1] 0x180003760  the three CreateXxxState
    void Bind(void* context) override;                   // [2] 0x180003a90  OMSet* / RSSetState

    ID3D11BlendState*        blendState;        // +0x10
    ID3D11DepthStencilState* depthStencilState; // +0x18
    ID3D11RasterizerState*   rasterizerState;   // +0x20
};
static_assert(sizeof(RenderStateData) == 0x28, "MemAlloc 0x28 @ 0x1800059d0");
static_assert(offsetof(RenderStateData, _owner) == 0x08, "RenderStateData::_owner (the base's)");
static_assert(offsetof(RenderStateData, blendState)        == 0x10, "RenderStateData::blendState");
static_assert(offsetof(RenderStateData, depthStencilState) == 0x18, "RenderStateData::depthStencilState");
static_assert(offsetof(RenderStateData, rasterizerState)   == 0x20, "RenderStateData::rasterizerState");

// RenderStateData vtable slot indices (vtable 0x180018818).
// Roles confirmed from the Runtime side, not inferred:
//   Video::RenderState::PlatformDestruct 0x1800f4e80 -> vtable[0](this, 1)
//   Video::RenderState::PlatformCreate   0x1800f4ea0 -> vtable[1]()
//   Video::RenderState::Bind(Context&)   0x1800f4ec0 -> vtable[2]()
// (B5: the `RenderStateDataSlot` index enum is gone; see the overrides above.)

// 0x10, vtable 0x1800185a8 (3 slots), factory 0x180005a40 — no private fields.
// Slots 1 and 2 are the shared `return true` / `ret` stubs, so only the dtor is
// real: this class exists purely to satisfy the Runtime-side interface.
//
// REIMPLEMENTED — src/render/dx11/smallclasses.cpp (all 3 slots).
//
//   slot 0  0x180001140  scalar deleting dtor, chains
//                        ??1VideoProgramData@Video@@UEAA@XZ
//   slot 1  0x180001060  `mov al,1; ret` — shared with RenderBufferData[1] and
//                        DeviceData[7]
//   slot 2  0x180001190  `ret`           — shared with DeviceData[4]
//
// B5 PILOT: a real C++ subclass of `Video::VideoProgramData`; the compiler builds
// its vtable (retail slot N >= 1 is compiler slot N + 1, which nothing indexes).
// `_owner` (+0x08, `Video::VideoProgram*`) is the base's and replaces the old
// `videoProgram` field. Owner type confirmed by
// ??0VideoProgramData@Video@@QEAA@PEAVVideoProgram@1@@Z.
class VideoProgramData final : public ::Video::VideoProgramData {
public:
    // Factory 0x180005a40: MemAlloc(0x10, 4) + ??0VideoProgramData(owner) + the
    // vptr store. Nothing else is stored.
    explicit VideoProgramData(::Video::VideoProgram* owner)
        : ::Video::VideoProgramData(owner) {}
    ~VideoProgramData() override;                        // [0] 0x180001140, chains ??1VideoProgramData@Video@@UEAA@XZ
    bool Slot1() override;                               // [1] 0x180001060  `mov al,1; ret` — shared with RenderBufferData[1], DeviceData[7]
    void Bind(void* context, int detail) override;       // [2] 0x180001190  `ret` — shared with DeviceData[4]
};
static_assert(sizeof(VideoProgramData) == 0x10, "MemAlloc 0x10 @ 0x180005a40");
static_assert(offsetof(VideoProgramData, _owner) == 0x08, "VideoProgramData::_owner (the base's)");

// 0x10, vtable 0x180018588 (4 slots), factory 0x180005aa0 — no private fields.
//
// REIMPLEMENTED — src/render/dx11/smallclasses.cpp (all 4 slots).
//
//   slot 0  0x180001010  scalar deleting dtor, chains
//                        ??1RenderBufferData@Video@@UEAA@XZ. Releases nothing:
//                        the views belong to the ImageBufferData of each
//                        Video::ImageBuffer, not to this object.
//   slot 1  0x180001060  `mov al,1; ret` (shared address, separate slot)
//   slot 2  0x180001070  Bind(Context&) -> OMSetRenderTargets(1, &rtv, dsv)
//   slot 3  0x1800010e0  Resolve(Context&, RenderBuffer*, bool) ->
//                        ResolveSubresource(this.colour, 0, other.colour, 0,
//                                           B8G8R8A8_UNORM)
//
// "0 private bytes" is true of the class but NOT of the code: slots 2 and 3
// reach real D3D11 views through the owner. Video::RenderBuffer is a
// (colour, depth) Video::ImageBuffer pair at +0x00/+0x08 and each ImageBuffer
// carries its VideoDX11::ImageBufferData at +0x60 — all pinned with
// static_asserts in src/render/dx11/dx11_backend_access.h. Owner type confirmed by
// ??0RenderBufferData@Video@@QEAA@PEAVRenderBuffer@1@@Z, and the pair shape by
// ?Create@RenderBuffer@Video@@QEAA_NPEBVImageBuffer@2@0@Z.
//
// B5 PILOT: a real C++ subclass of `Video::RenderBufferData`; the compiler builds
// its vtable. `_owner` (+0x08, `Video::RenderBuffer*`) is the base's and replaces
// the old `renderBuffer` field.
class RenderBufferData final : public ::Video::RenderBufferData {
public:
    // Factory 0x180005aa0: MemAlloc(0x10, 4) + ??0RenderBufferData(owner) + the
    // vptr store. Nothing else is stored.
    explicit RenderBufferData(::Video::RenderBuffer* owner)
        : ::Video::RenderBufferData(owner) {}
    ~RenderBufferData() override;                                      // [0] 0x180001010, chains ??1RenderBufferData@Video@@UEAA@XZ
    bool Slot1() override;                                             // [1] 0x180001060  `mov al,1; ret`
    void Bind(void* context) override;                                 // [2] 0x180001070  OMSetRenderTargets
    void Resolve(void* context, ::Video::RenderBuffer* other, bool flag) override;   // [3] 0x1800010e0  ResolveSubresource
};
static_assert(sizeof(RenderBufferData) == 0x10, "MemAlloc 0x10 @ 0x180005aa0");
static_assert(offsetof(RenderBufferData, _owner) == 0x08, "RenderBufferData::_owner (the base's)");

// Retail's vtable slot counts (walked from .rdata 0x180018580-0x180018c60 on
// 2026-07-24: DeviceData 20, ContextData 20, ImageBufferData 7, Index / Vertex /
// BufferData 7, ConstantBufferData 4, RenderBufferData 4, the five others 3; 84
// in all) are now the number of `override` declarations in each class above plus
// its destructor; tests/unit/b39_vid_data_test.cpp pins every slot by retail
// number through slot_check.h. The `kSlots` table and the seam that re-verified
// it (src/render/dx11/funcs/dx11_seam.cpp) went with B5.

} // namespace VideoDX11
