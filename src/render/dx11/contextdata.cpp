// VideoDX11 ContextData — vtable 0x1800186f0, all 20 slots (lane L5).
//
// The largest vtable in the DLL and the last resource class. It is also the one
// class that CACHES the D3D11 immediate context (ContextData+0x18) instead of
// re-walking Video::Device::_singleton -> [+0x100] -> [+0x28] on every call.
//
// B5 CTX (the dx11 seam): `VideoDX11::ContextData` is a real C++ subclass of
// the abstract `Video::ContextData` (src/render/dx11/VideoDX11Data.h); every slot
// below is a member declared `override`, and the compiler builds the vtable
// (retail slot N >= 1 is compiler slot N + 1, which nothing indexes any more).
// Every Video::Context / Video::Device / Video::Configuration field is read by
// its NAMED member (B5_RULES D8); the raw-offset field accessors are gone.
//
// ---------------------------------------------------------------------------
// The slot names are recovered, not invented
// ---------------------------------------------------------------------------
// Runtime's Video::Context keeps its Video::ContextData* at Context+0x2160, and
// every public Context method that reaches the backend is a two-load tail-jump
// through it:
//
//     Video::Context::BindTransforms   0x1800f22a0
//         MOV RCX,[RCX+0x2160] / MOV RAX,[RCX] / JMP [RAX+0x40]
//
// So the mapping from Runtime method name to vtable byte offset is read
// straight out of the thunk, with no inference at all. All twenty were checked
// individually in Runtime-x64.dll:
//
//   slot off   Runtime thunk   exported signature (from the Runtime export table)
//   ---- ----  --------------  -----------------------------------------------
//    0   0x00  0x1800f2240 PlatformDestruct  -> vtable[0](this, 1)
//    1   0x08  0x1800f2340   ?BeginCommands@Context@Video@@QEAA_NXZ
//    2   0x10  0x1800f2360   ?EndCommands@Context@Video@@QEAAXXZ
//    3   0x18  0x1800f2380   ?Finish@Context@Video@@QEAAXXZ
//    4   0x20  0x1800f23f0   ?Clear@Context@Video@@QEAAXIIMH@Z
//    5   0x28  0x1800f2280   ?BindRenderBuffer@Context@Video@@AEAAXXZ
//    6   0x30  0x1800f2300   ?BindViewport@Context@Video@@AEAAXXZ
//    7   0x38  0x1800f2320   ?BindScissor@Context@Video@@AEAAXXZ
//    8   0x40  0x1800f22a0   ?BindTransforms@Context@Video@@AEAAXXZ
//    9   0x48  0x1800f2260   ?BindTextureNone@Context@Video@@AEAAXH@Z
//   10   0x50  0x1800f22e0   ?BindIndexNone@Context@Video@@AEAAXXZ
//   11   0x58  0x1800f22c0   ?BindVertexNone@Context@Video@@AEAAXH@Z
//   12   0x60  0x1800f2410   ?DrawIndexed@Context@Video@@QEAAXW4PrimitiveType@12@HHHHH@Z
//   13   0x68  0x1800f21d0   ?GetMaxDynamicInstances@Context@Video@@QEBAHHHH@Z
//   14   0x70  0x1800f21b0   ?BeginVertices@Context@Video@@QEAA_NHH@Z
//   15   0x78  0x1800f2190   ?EndVertices@Context@Video@@QEAAXW4PrimitiveType@12@@Z
//   16   0x80  0x1800f2170   ?BeginIndexedVertices@Context@Video@@QEAA_NHHH@Z
//   17   0x88  0x1800f2150   ?EndIndexedVertices@Context@Video@@QEAAXW4PrimitiveType@12@@Z
//   18   0x90  0x1800f2130   ?BeginInstancedIndexedVertices@Context@Video@@QEAA_NHHHH@Z
//   19   0x98  0x1800f2110   ?EndInstancedIndexedVertices@Context@Video@@QEAAXW4PrimitiveType@12@@Z
//
// The Runtime thunks pass `this` = the ContextData and forward RDX/R8/R9 and
// the stack arguments untouched, so each slot's parameter list is exactly the
// Context method's parameter list. That is where the signatures below come
// from — none of them is guessed from register usage alone.
//
// ---------------------------------------------------------------------------
// Slot 17 and slot 19 are the same function
// ---------------------------------------------------------------------------
// 0x180002020 is five bytes: `e9 2b 01 00 00` = `jmp 0x180002150`. So
// EndIndexedVertices and EndInstancedIndexedVertices share one body. That is
// consistent because the instance count is carried on the Context
// (Context+0x2110), and the non-instanced entry point (slot 16) sets it to 1
// before locking. Slot 17 is a one-line member that calls slot 19's body.
//
// Three slots are not Ghidra functions at all — they are tails inside a
// neighbour's body. All three were decoded by hand from `read_memory`:
//
//   0x180001d10  48 8b 49 18            MOV RCX,[RCX+0x18]      this->d3dContext
//                45 33 c9               XOR R9D,R9D             Offset = 0
//                33 d2                  XOR EDX,EDX             pIndexBuffer = NULL
//                48 8b 01               MOV RAX,[RCX]
//                45 8d 41 39            LEA R8D,[R9+0x39]       Format = 57 = R16_UINT
//                48 ff a0 98 00 00 00   JMP [RAX+0x98]          IASetIndexBuffer
//   0x180001ef0  b8 34 00 00 00 / c3    MOV EAX,0x34; RET       -> 52
//   0x180002020  e9 2b 01 00 00         JMP 0x180002150
//
// (src/render/dx11/README.md already listed all three; each is re-verified here rather
// than trusted.)
//
// ---------------------------------------------------------------------------
// Video::Context field map used by this file
// ---------------------------------------------------------------------------
// Every offset is proven either by a VideoDX11 instruction cited at its use
// site below, or by a one-line Runtime accessor, which is the stronger proof
// because it also gives the field its name:
//
//   Runtime accessor                              offset  field
//   --------------------------------------------  ------  ---------------------
//   ?GetRenderBuffer@         0x180019ec0          +0x0028 current RenderBuffer*
//   ?GetViewport@             0x18001a0c0          +0x01d0 Viewport{x,y,w,h}
//   ?GetVertexBufferStride@   0x180019f90          (array) dynamic stride kin
//   ?GetDefaultRenderBuffer@  0x18001ae40          +0x2138 default RenderBuffer*
//   ?GetTrianglesSubmitted@   0x18001ae60          +0x2150 int
//   ?GetVerticesSubmitted@    0x18001ae70          +0x2154 int
//   ?GetDrawsSubmitted@       0x18001ae80          +0x2158 int
//   ?GetObjectsSubmitted@     0x18001ae90          +0x215c int
//   ?AddVertex@               0x18001ad40          +0x20e0 write cursor,
//                                                  +0x20e8 stride
//   (Context+0x2160 = the ContextData*, from every thunk above.)
//
// B5 CTX: the full table, offset -> the `Video::Context` member (Context.h,
// which asserts every one) -> the proof this file carries:
//
//   +0x0000 _device              a Video::Device*, not a vptr: slot 1 loads it
//                                with `MOV RAX,[RCX+8] / MOV RDI,[RAX]`
//                                (0x18000168c) and hands it to Device::Lock.
//   +0x0010 _dirty               dirty-flag word. Bits this DLL touches:
//                                  0x0001 render buffer (slot 4 -> BindRenderBuffer)
//                                  0x0002 textures      (slot 5 -> Context::BindTextures)
//                                  0x0010 index buffer  (set by slot 19)
//                                  0x0200 viewport      (slot 4 -> BindViewport)
//                                  0x0400 scissor       (slot 4 -> RSSetScissorRects)
//                                  0x4000 transforms    (slot 8)
//                                Slot 4 clears 0x0601 in one AND (0x1800019c6,
//                                mask 0xfffff9fe).
//   +0x0020 _transformCount      slot 8: payload size is `[0x20] << 6`
//                                (0x180001c67, 64-byte matrices).
//   +0x0024 _unknown0024         slot 8 binds the bone buffer only when > 0.
//   +0x0028 _renderBuffer        ?GetRenderBuffer@ = `MOV RAX,[RCX+0x28]; RET`.
//   +0x0030 _imageBuffers[16]    slot 5 walks i in [0,0x10) stride 8 from 0x30
//                                (0x180001ae4 seeds RDI with 0x30, 0x180001b18
//                                bounds the loop at 0x10).
//   +0x01b8 _indexBuffer         last index buffer + byte offset bound through
//   +0x01c0 _indexOffset         the ordinary path; slot 19 compares and raises
//                                dirty bit 0x10 (0x1800021d4-0x1800021f0).
//   +0x01d0 _viewport            ?GetViewport@ copies exactly these four dwords.
//   +0x01e0 _scissor             same shape, read by slots 4 and 7.
//   +0x0418 _boneAndProjectionStaging  the bone payload, exactly 0xcc0 bytes
//                                (slot 8's literal at 0x180001c92); 0x418 + 0xcc0
//                                == 0x10d8, the transform payload: adjacent.
//   +0x10d8 _transforms          the transform payload.
//   +0x2008 _boneConstants       two Video::ConstantBuffer objects EMBEDDED in
//   +0x2050 _transformConstants  the Context (slot 8 takes their addresses
//                                with LEA, it does not load a pointer).
//   +0x20e0 _dynamicVertexCursor moving write pointer (AddVertex memcpy's here)
//   +0x20e8 _dynamicVertexStride bytes per vertex (AddVertex advances by it)
//   +0x20f0 _dynamicIndexCursor  moving write pointer
//   +0x20f8 _dynamicVertexBuffer Video::VertexBuffer*
//   +0x2100 _unknown2100         int, the byte offset VertexBuffer::Lock returns
//   +0x2104 _unknown2104         int, the vertex count (Draw's count, slot 15)
//   +0x2108 _unknown2108         void*, a copy of the vertex Lock result
//   +0x2110 _instanceCount       int
//   +0x2118 _dynamicIndexBuffer  Video::IndexBuffer*
//   +0x2120 _unknown2120         int, the byte offset IndexBuffer::Lock returns
//   +0x2124 _unknown2124         int, the index count
//   +0x2128 _unknown2128         void*, a copy of the index Lock result
//   +0x2138 _defaultRenderBuffer ?GetDefaultRenderBuffer@ = `MOV RAX,[RCX+0x2138]`.
//   +0x2150.. the four per-frame counters, each named by its one-instruction
//             Runtime getter (0x18001ae60 / 70 / 80 / 90).
//
// Video::Device members read here (Device.h, asserted there):
//   +0x04 / +0x08  backBufferWidth / backBufferHeight. Slot 1 builds the
//                  full-screen viewport from them (0x1800016d4 / 0x1800016df);
//                  the same two fields fill DXGI_SWAP_CHAIN_DESC.BufferDesc in
//                  0x180004590 (see decomp/videodx11.md), an independent proof.
//   +0x50 / +0x58  onVideoError / onVideoRestart, two `void(*)()` hooks called by
//                  slot 3 either side of a device reset (0x18000183c /
//                  0x18000187d). Both are null-checked by the original.
//   +0x9c          settingsChanged: when non-zero slot 1 refuses to begin the
//                  frame and runs the swap-chain recreate helper instead
//                  (0x180001693). NOT the same field as Device+0x98.
//   +0x100         deviceData, the active Video::DeviceData*.
// Video::Configuration+0x41 `vsync` — a byte, used as Present's SyncInterval
// (`MOVZX EAX,byte [RDX+0x41]` at 0x1800017e7, then `SETNZ DL`). Read through
// ?_singleton@Configuration@Video@@, which is the OTHER of the two globals
// Ghidra renders as `_singleton_exref`; conflating it with
// ?_singleton@Device@Video@@ (0x1800134b8) would read the wrong object.
//
// ---------------------------------------------------------------------------
// COM vtable indices
// ---------------------------------------------------------------------------
// Index = position counted from QueryInterface = 0, i.e. INCLUDING every
// inherited method (ID3D11DeviceContext derives ID3D11DeviceChild, which
// contributes GetDevice/GetPrivateData/SetPrivateData/SetPrivateDataInterface
// at 3..6). Byte offset = index * 8. Each one below was checked against the
// disassembly of the call site, not assumed:
//
//   interface               idx  byte   method                     call site
//   ----------------------  ---  -----  -------------------------  -----------
//   IUnknown                  1  0x008  AddRef                     0x180001747
//   IUnknown                  2  0x010  Release                    0x180001794
//   IDXGISwapChain            8  0x040  Present                    0x1800017fc
//   IDXGISwapChain            9  0x048  GetBuffer                  0x180001763
//   ID3D11Device             39  0x138  GetDeviceRemovedReason     0x180001828
//   ID3D11DeviceContext       8  0x040  PSSetShaderResources       0x180001cf4
//   ID3D11DeviceContext      10  0x050  PSSetSamplers              0x180001cdd
//   ID3D11DeviceContext      13  0x068  Draw                       0x180001ff0
//   ID3D11DeviceContext      12  0x060  DrawIndexed                0x180001e2a
//   ID3D11DeviceContext      18  0x090  IASetVertexBuffers         0x180001d69
//   ID3D11DeviceContext      19  0x098  IASetIndexBuffer           0x180001d1d
//   ID3D11DeviceContext      20  0x0a0  DrawIndexedInstanced       0x180001e51
//   ID3D11DeviceContext      24  0x0c0  IASetPrimitiveTopology     0x180001dfd
//   ID3D11DeviceContext      44  0x160  RSSetViewports             0x180001bd4
//   ID3D11DeviceContext      45  0x168  RSSetScissorRects          0x1800019bc
//   ID3D11DeviceContext      50  0x190  ClearRenderTargetView      0x180001a5e
//   ID3D11DeviceContext      53  0x1a8  ClearDepthStencilView      0x180001aa9
//   ID3D11DeviceContext      57  0x1c8  ResolveSubresource         0x180001788
//
// These agree with the indices L3 and L4 recovered independently for
// CreateTexture2D (5), CreateShaderResourceView (7), CreateRenderTargetView
// (9), CreateDepthStencilView (10), CreateBlendState (20), CreateSamplerState
// (23), OMSetBlendState (35), OMSetDepthStencilState (36) and RSSetState (43).
//
// ---------------------------------------------------------------------------
// What this lane deliberately does NOT do
// ---------------------------------------------------------------------------
// Slot 1 calls 0x180004340, the DeviceData swap-chain resize/recreate helper.
// That function belongs to lane L6 and is NOT ported here — it is called
// through L6's port, Dx11_DeviceData_Resize. It is a near CALL in the original
// (0x1800016a3), not a vtable dispatch. ENDGAME B.3.5 block J: it used to be a
// function POINTER FIELD in this file's import struct, set to
// `&Dx11_DeviceData_Resize` unconditionally — never a resolved import, so never
// anything `g_imOk` could fail on. Slot 1 names the function directly now.
//
// Slot 3 calls DeviceData slots 3 (shutdown) and 2 (create device) VIRTUALLY,
// by name through the abstract `Video::DeviceData`, as the original dispatches.

#include "device_lost_drive.h"   // BANISHED_FORCE_DEVICE_LOST=bgsave (src/diag)
#include "options.h"
#include "d3d_debug.h"
#include "clamped_atoi.h"     // review F-087: atoi on BANISHED_* text
#include "dx11_backend_access.h"
#include "async_gpu_create/async_create.h"   // NOT RETAIL: ext.async_gpu_create
#include "boot_device_lost/boot_device_lost.h"   // NOT RETAIL: fix.boot_device_lost
#include "platform_window.h"   // NOT RETAIL: IsIconic, fix.boot_device_lost's minimised-window wait
#include "system/Error.h"   // ENDGAME B.3.4b System callers: System::Error (-Iruntime/types)
// ENDGAME B.3.5 block VC: `Video::Viewport` and `Video::Context`.
#include "video/Context.h"
#include "video/CompanionData.h"
#include "video/Device.h"   // Video::Device, Video::Configuration::_singleton
// `Video::{Vertex,Index}Buffer::Lock/Unlock` (block VC's members).
#include "video/Buffers.h"
#if BANISHED_TWEAK_OVERLAY
#include "tweak_overlay.h"   // the debug tweak overlay (debug flavour only, src/diag)
#endif

#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cstdio>
#include <cstdlib>

namespace VideoDX11 {
namespace {

// ============================================================
// Runtime-x64.dll imports — ENDGAME B.3.5 block J, RULE B5-14 ("spelling 9")
//
// SEVENTEEN OF THE EIGHTEEN ROWS ARE GONE. They were mangled Runtime symbol
// names held as `const char*` in an initialised table and resolved positionally.
// Every class they named is a real C++ class in `src/engine/video/` now, so
// each one is a DIRECT MEMBER CALL at its call site:
//
//   ??1ContextData@Video@@UEAA@XZ                     -> ~ContextData()  (the compiler's base chain, B5)
//   ?Lock@Device@Video@@                              -> Video::Device::Lock
//   ?Unlock@Device@Video@@                            -> Video::Device::Unlock
//   ?ReleaseDynamic@Device@Video@@                    -> Video::Device::ReleaseDynamic
//   ?RecreateDynamic@Device@Video@@                   -> Video::Device::RecreateDynamic
//   ?SetViewport@Context@Video@@                      -> Video::Context::SetViewport
//   ?SetRenderBuffer@Context@Video@@                  -> Video::Context::SetRenderBuffer
//   ?SetImageBuffer@Context@Video@@                   -> Video::Context::SetImageBuffer
//   ?SetVertexBuffer@Context@Video@@                  -> Video::Context::SetVertexBuffer
//   ?BindTextures@Context@Video@@   (AEAA)            -> Video::Context::BindTextures
//   ?BindResources@Context@Video@@  (AEAA)            -> Video::Context::BindResources
//   ?Bind@RenderBuffer@Video@@                        -> Video::RenderBuffer::Bind
//   ?Bind@ConstantBuffer@Video@@                      -> Video::ConstantBuffer::Bind
//   ?Lock@VertexBuffer@Video@@   / ?Unlock@…          -> Video::VertexBuffer::Lock / Unlock
//   ?Lock@IndexBuffer@Video@@    / ?Unlock@…          -> Video::IndexBuffer::Lock / Unlock
//
// The two `AEAA` names really are private in retail and really are exported;
// `Video::Context` declares both as public members (Context.h), which is what
// makes the direct call legal on this side of the seam.
//
// ENDGAME B.3.9 VID: the one binding that was still resolved at run time,
// `?_singleton@Configuration@Video@@0PEAV12@EA`, is the class static
// `Video::Configuration::_singleton` now (dx11 is compiled into the exe).

// Slot 5 walks this many texture slots (0x180001b18 bounds the loop at 0x10).
constexpr int kTextureSlotCount = 16;
static_assert(sizeof(::Video::Context::_imageBuffers) ==
                  kTextureSlotCount * sizeof(const ::Video::ImageBuffer*),
              "Context::_imageBuffers is the 16 texture slots slot 5 walks");

// Slot 8's bone payload size, the literal 0xcc0 at 0x180001c92 — exactly the
// staging block, which ends where the transform payload starts.
constexpr int kBoneDataBytes = 0x0cc0;
static_assert(sizeof(::Video::Context::_boneAndProjectionStaging) == kBoneDataBytes,
              "the bone block is 0xcc0 bytes");
static_assert(offsetof(::Video::Context, _boneAndProjectionStaging) + kBoneDataBytes ==
                  offsetof(::Video::Context, _transforms),
              "bone block is adjacent to the transform block");

// The active DeviceData of the Context's own device. ContextData never uses the
// Video::Device singleton: it walks this->_owner->_device instead. Both routes
// reach the same object, but the original only ever takes this one, so the port
// takes it too.
//
// `Video::Device::deviceData` is typed `Video::DeviceData*`; the object is
// dx11's DeviceData (B5 DEV-A: a real subclass), reached through the one
// downcast dx11 makes (Dx11DeviceDataOf, dx11_backend_access.h).
inline DeviceData* OwnerDeviceData(const ::Video::Context* ctx) {
    return Dx11DeviceDataOf(ctx->_device);
}

// Video::Viewport — 16 bytes, four int32. Shape proven by Runtime's
// ?GetViewport@ (0x18001a0c0) copying Context+0x1d0..0x1dc into it, and used
// here to call ?SetViewport@Context@Video@@QEAAXAEBVViewport@2@@Z.
//
// ENDGAME B.3.5 block VC, RULE B5-2's duplicate-view collapse: an ALIAS of the
// REAL `Video::Viewport` (src/engine/video/Context.h).
using Viewport = ::Video::Viewport;

// IID_ID3D11Texture2D, transcribed from the 16 bytes at VideoDX11 0x1800186d8
// that slot 2 passes to IDXGISwapChain::GetBuffer:
//   f2 aa 15 6f  08 d2  89 4e  9a b4 48 95 35 d3 4f 9c
// Spelled out here rather than using IID_ID3D11Texture2D so this file does not
// depend on dxguid.lib being on the link line, and so the bytes in the shipped
// DLL are visible next to the call that uses them.
const GUID kIID_ID3D11Texture2D =
    { 0x6f15aaf2, 0xd208, 0x4e89, { 0x9a, 0xb4, 0x48, 0x95, 0x35, 0xd3, 0x4f, 0x9c } };

// ============================================================
// BANISHED_FORCE_DEVICE_LOST=<frame>  -- Wave 9 lane ENGIO
//
// PLAN 5.5 wants a forced device-lost event exercised before cutover and it had
// never happened: nothing in the harness can make Present fail, and waiting for
// a real driver reset is not a test. This injects ONE synthetic
// DXGI_ERROR_DEVICE_RESET into the Present result on the Nth call to Finish,
// and then never again.
//
// WHY IT IS SOUND AS A DRIVE RATHER THAN A SIMULATION. It substitutes the
// RETURN VALUE of Present and nothing else, at the exact place a real reset
// would deliver it, so every line after it -- the two DXGI error comparisons,
// the onLost notify, ReleaseDynamic, the d3dDevice null, DeviceData slots 3 and
// 2, RecreateDynamic, the onRestored notify -- runs for real on the real
// device. In particular the device really IS torn down and recreated; this is
// not a mock of the recovery, it is the recovery.
//
// The one thing it does NOT reproduce is the D3D device being genuinely
// removed, so `GetDeviceRemovedReason` is never consulted by the recovery --
// and it cannot be, because DEVICE_RESET is one of the two values that SKIP
// that query (0x180001820's two comparisons). Stated rather than implied: this
// drives the NOTIFY-AND-RECREATE half of the path, which is the half PLAN 5.5
// is about and the half MainLoop::OnVideoError / OnVideoRestart live on.
//
// Off unless the env var is set, and `getenv` is process-cached
// (src/app/options.h), so the read costs nothing per frame.
// ============================================================

// Returns true exactly once, on the Nth ContextData::Finish call.
bool ForceDeviceLostThisFrame() {
    static int  wanted = -2;      // -2 = not yet read, -1 = disabled, -3 = on request (`bgsave`)
    static long frame  = 0;
    static bool fired  = false;
    if (wanted == -2) {
        const char* v = Opt().forceDeviceLost;
        wanted = DeviceLostDrive::Armed() ? -3 : (v && *v) ? ClampedAtoi(v) : -1;
        if (wanted > 0)
            std::fprintf(stderr,
                "[DeviceLost] ARMED: a synthetic DXGI_ERROR_DEVICE_RESET will be "
                "injected on Present #%d (PLAN 5.5 forced device-lost drive)\n",
                wanted);
        if (wanted == -3)
            std::fprintf(stderr, "[DeviceLost] ARMED: a synthetic DXGI_ERROR_DEVICE_RESET will be "
                                 "injected on the first Present after a background save starts\n");
    }
    ++frame;
    // BANISHED_FORCE_DEVICE_LOST=bgsave (src/diag/device_lost_drive.h): once, on request.
    if (wanted == -3) {
        if (fired || !DeviceLostDrive::TakeRequest()) return false;
        fired = true;
        return true;
    }
    if (wanted <= 0 || fired || frame < wanted) return false;
    fired = true;
    return true;
}

}  // namespace

// NOT RETAIL: the Present lock (dx11_backend_access.h). One per process; the
// immediate context it guards is re-created on a device-lost round trip, the
// mutex is not.
std::recursive_mutex& Dx11PresentMutex() {
    static std::recursive_mutex m;
    return m;
}

// ============================================================
// Slot 0 — scalar deleting destructor  (0x180001600)
//
//     18000161d  LEA  RAX,[0x1800186f0] / MOV [RCX],RAX
//     180001627  CALL [0x180013470]   ; Video::ContextData::~ContextData
//     180001635  CALL [0x1800134d0]   ; MemFree, only when (flags & 1)
//                returns `this`
//
// B5 CTX: those three instructions are the compiler's now — the vptr re-seat
// and the base-destructor chain are what `~ContextData()` does by itself, and
// the conditional MemFree is the compiler's D0 calling the base's class-scope
// `operator delete` (B5 W0, src/engine/video/companion_operators.cpp).
//
// Note what is NOT here: no Release of this->d3dContext. The cached context is
// borrowed — slot 1 copies it out of the DeviceData without an AddRef, so
// releasing it here would be an over-release. Preserved by doing nothing.
// ============================================================
ContextData::~ContextData() {
}

// ============================================================
// Slot 1 — bool BeginCommands()  (0x180001650)
//
// Start-of-frame. Zero the four statistics counters, bail out if the device
// wants a swap-chain rebuild, otherwise take the device lock, reset the render
// target to the default one and the viewport to the whole backbuffer, and
// refresh the cached ID3D11DeviceContext.
//
// The counters are zeroed BEFORE the reset check, so a frame that is skipped
// still reports zeros. Reproduced.
//
// The original re-loads `this->context` before every one of those four stores
// (0x180001668, 0x18000166e, ...). Using one local is equivalent: nothing on
// the path writes ContextData+0x08.
// ============================================================
// ENDGAME B.3.5 block J: the `if (!GetImports())` forward-to-original readiness
// guard is GONE with the four imports this slot used (and B5 D9 deleted the
// forwarding template itself: it was never instantiated).
// Finish's device-lost recovery (below Finish's neighbour EndCommands).
static void RecoverLostDevice(::Video::Device* device, DeviceData* dd,
                              ID3D11Device* d3dDevice, HRESULT hr);

bool ContextData::BeginCommands() {
    ::Video::Context* const ctx = _owner;
    ctx->_trianglesSubmitted = 0;
    ctx->_verticesSubmitted  = 0;
    ctx->_drawsSubmitted     = 0;
    ctx->_objectsSubmitted   = 0;

    ::Video::Device* const device = ctx->_device;
    if (device->settingsChanged != 0) {
        // 0x180004340 — DeviceData swap-chain resize/recreate. Lane L6's
        // function; retail discards its result (`XOR AL,AL`).
        DeviceData* const dd = Dx11DeviceDataOf(device);   // B5 DEV-A: typed
        const int resized = Dx11_DeviceData_Resize(dd);

        // FIX SEAM (NOT RETAIL, docs/retail_bugs.md section 10, known_port_issues row
        // 1334): with fix.boot_device_lost on, a reset that failed on a REMOVED
        // device enters Finish's device-lost recovery here. Retail reaches that
        // recovery only from Present, which a failed reset never gets to: once the
        // device is gone ResizeBuffers answers DXGI_ERROR_DEVICE_REMOVED on every
        // retry, so the frame loop spun in the reset forever, or the first read-back
        // (the cursor's staging Map) crashed on the dead device. A reset refused for
        // any other reason (SetFullscreenState without the foreground, a hidden or
        // minimised window) leaves the device alive and is retried as retail retries.
        // Row off (and under the faithful profile): retail's retry, spin or crash.
        // A minimised window waits for its restore (review S-019): the recovery's
        // CreateDevice / AdjustDisplay would latch its 0x0 client rect, the 0x0 colour
        // buffer fails, and the recovery ends in the fatal D3D11 Error box. Resize keeps
        // settingsChanged set, so the first frame after the restore fails the reset again
        // and recovers with the real client size.
        if (resized == 0 && BootDeviceLost::On() && !Platform::Window::IsIconic(device->windowHandle)) {
            ID3D11Device* const d3dDevice = dd->d3dDevice;
            const HRESULT removed = d3dDevice->GetDeviceRemovedReason();
            if (removed != 0) {
                std::fprintf(stderr, "[DeviceLost] reset failed on a removed device "
                                     "(reason 0x%08lx): device-lost recovery\n",
                             static_cast<unsigned long>(removed));
                std::fflush(stderr);
                RecoverLostDevice(device, dd, d3dDevice, removed);
            }
        }
        return false;
    }

    device->Lock();
    ctx->SetRenderBuffer(ctx->_defaultRenderBuffer);

    Viewport vp;
    vp.x = 0;
    vp.y = 0;
    vp.width  = device->backBufferWidth;
    vp.height = device->backBufferHeight;
    ctx->SetViewport(vp);

    // DeviceData+0x28 (typed ID3D11DeviceContext* by B5 DEV-A, D4).
    d3dContext = OwnerDeviceData(ctx)->d3dContext;

    // NOT RETAIL: a resize ran ClearState(), which reset the topology to
    // UNDEFINED behind the `topology` cache; drop the cache so the next draw
    // sets it again. `pad14` (unused in retail) holds the generation seen.
    const unsigned gen = Dx11ClearStateGeneration();
    if (static_cast<unsigned>(pad14) != gen) {
        pad14    = static_cast<int32_t>(gen);
        topology = 0;
    }
    return true;
}

// ============================================================
// Slot 2 — void EndCommands()  (0x180001720)
//
// Resolve the (possibly multisampled) colour buffer into the swap chain's back
// buffer. Nothing is presented here; slot 3 does that.
//
//     180001747  AddRef(deviceData->colorBuffer)        [+0x08]
//     180001763  swapChain->GetBuffer(0, IID_Texture2D, &backBuffer)  [+0x48]
//     180001788  ResolveSubresource(backBuffer, 0, colorBuffer, 0, 0x57)
//     180001794  Release(colorBuffer)
//     18000179f  Release(backBuffer)
//
// The AddRef/Release pair around the colour buffer is a no-op in net terms and
// is kept because it is observable in a PIX/RenderDoc reference-count view.
//
// The original never checks GetBuffer's HRESULT and never initialises the
// out-pointer — `LEA R9,[RSP+0x40]` points at its own argument home slot. It
// then Releases whatever came back unconditionally. This port initialises the
// local to nullptr (reading an uninitialised object would be UB in C++) and
// still Releases unconditionally, so a GetBuffer failure faults here just as it
// faults in the shipped DLL rather than being silently swallowed.
// ============================================================
void ContextData::EndCommands() {
    DeviceData* dd = OwnerDeviceData(_owner);

    IUnknown* color = dd->colorBuffer;
    color->AddRef();

    ID3D11Resource* backBuffer = nullptr;
    dd->swapChain->GetBuffer(
        0, kIID_ID3D11Texture2D, reinterpret_cast<void**>(&backBuffer));   // CAST-OK: com_iid

    d3dContext->ResolveSubresource(backBuffer, 0,
                                   dd->colorBuffer, 0,
                                   DXGI_FORMAT_B8G8R8A8_UNORM);

    color->Release();
    backBuffer->Release();
}
static_assert(static_cast<int>(DXGI_FORMAT_B8G8R8A8_UNORM) == 0x57,
              "the literal 0x57 stored at 0x180001778");

// ============================================================
// Finish's device-lost recovery from step 5 on (0x18000184f..), unchanged. NOT RETAIL
// (structure only): lifted out of Finish so BeginCommands can run it too (see there);
// Finish calls it exactly where these lines used to be.
// ============================================================
static void RecoverLostDevice(::Video::Device* device, DeviceData* dd,
                              ID3D11Device* d3dDevice, HRESULT hr) {
    // B5 W0: the ONLY place dx11 read its own hand table by INDEX. Retail
    // (0x18000184f / 0x18000185c) dispatches slots 3 and 2 of the DeviceData;
    // the abstract base `Video::DeviceData` declares those slots by name in
    // retail order, so the by-name call reaches retail slots 3 and 2 of the
    // compiler's DeviceData table (B5 DEV-A: DeviceData is a real subclass).
    // B5 CTX: `Video::Device::deviceData` is already that typed base pointer,
    // so the base needs no cast (B5 DEV-A: and OwnerDeviceData's bridge is the
    // single dx11 downcast Dx11DeviceDataOf now; DeviceData wears the compiler's table).
    ::Video::DeviceData* const ddBase = device->deviceData;
    ::Video::Device*     const owner  = ddBase->_owner;   // DeviceData+0x08

    void (*const onLost)() = owner->onVideoError;          // Device+0x50
    std::fprintf(stderr, "[DeviceLost] hr=0x%08lx onLost=%p -> %s\n",
                 static_cast<unsigned long>(hr), (void*)onLost,
                 onLost ? "calling" : "*** NULL, notification SKIPPED ***");
    // B5 CTX (B5_RULES §6 a): diagnostic only — DEVLOST_FINDING noted the
    // removed reason is never logged. A query, not a state change.
    std::fprintf(stderr, "[DeviceLost] removed reason=0x%08lx\n",
                 static_cast<unsigned long>(d3dDevice->GetDeviceRemovedReason()));
    std::fflush(stderr);
    if (onLost) onLost();

    // NOT RETAIL (ext.async_gpu_create): creates queued against the lost device finish first.
    AsyncCreate::Drain();
    owner->ReleaseDynamic();

    dd->d3dDevice = nullptr;

    ddBase->Shutdown();                           // retail slot 3
    const bool created = ddBase->CreateDevice();  // retail slot 2

    if (!created) {
        // Same five strings, same fallback, same order of tests as
        // 0x18000189a-0x1800018f6. They are our own literals rather than the
        // .rdata copies in the shipped DLL; System::Error::SetText copies the
        // text, so the observable result is identical.
        const char* msg;
        if      (hr == DXGI_ERROR_INVALID_CALL)           msg = "D3D11 Error: DXGI_ERROR_INVALID_CALL!";
        else if (hr == DXGI_ERROR_DEVICE_REMOVED)         msg = "D3D11 Error: DXGI_ERROR_DEVICE_REMOVED!";
        else if (hr == DXGI_ERROR_DEVICE_HUNG)            msg = "D3D11 Error: DXGI_ERROR_DEVICE_HUNG!";
        else if (hr == DXGI_ERROR_DEVICE_RESET)           msg = "D3D11 Error: DXGI_ERROR_DEVICE_RESET!";
        else if (hr == DXGI_ERROR_DRIVER_INTERNAL_ERROR)  msg = "D3D11 Error: DXGI_ERROR_DRIVER_INTERNAL_ERROR!";
        else                                              msg = "D3D11 Error: UNKNOWN!";
        System::Error::SetText(msg, true);
        System::Error::Display();
        return;
    }

    owner->RecreateDynamic();

    void (*const onRestored)() = owner->onVideoRestart;    // Device+0x58
    std::fprintf(stderr, "[DeviceLost] recreate OK; onRestored=%p -> %s\n",
                 (void*)onRestored,
                 onRestored ? "calling" : "*** NULL, notification SKIPPED ***");
    std::fflush(stderr);
    if (onRestored) onRestored();
    std::fprintf(stderr, "[DeviceLost] device-lost round trip COMPLETE\n");
    std::fflush(stderr);
}

// ============================================================
// Slot 3 — void Finish()  (0x1800017c0)
//
// Present, then the whole device-lost recovery path.
//
//   1. Present(Configuration+0x41 ? 1 : 0, 0) and drop the device lock
//      (this port drops it first and presents under Dx11PresentMutex; see below).
//   2. Success -> done.
//   3. If the failure is not already DEVICE_REMOVED / DEVICE_RESET, ask
//      ID3D11Device::GetDeviceRemovedReason() for the real one. The original
//      writes this as `(hr + 0x7785fffb) & 0xfffffffd`, which is zero for
//      exactly hr == 0x887a0005 and hr == 0x887a0007 and nothing else; the two
//      explicit comparisons below are the same predicate.
//   4. A zero reason means the Present error was transient -> done.
//   5. Otherwise: notify (Device+0x50), Video::Device::ReleaseDynamic, NULL the
//      ID3D11Device, run DeviceData slot 3 (shutdown) and slot 2 (create).
//      On success: RecreateDynamic + notify (Device+0x58), tail-called.
//      On failure: System::Error::SetText(msg, true) then ::Display().
//
// The `dd->d3dDevice = nullptr` at step 5 happens BEFORE shutdown
// (0x180001857), i.e. it deliberately stops the shutdown path from Releasing a
// device that is already gone. That ordering is load-bearing, not incidental.
//
// The DeviceData slots are invoked VIRTUALLY, as the original dispatches them.
// ============================================================
void ContextData::Finish() {
    ::Video::Device*     const device = _owner->_device;
    DeviceData*          const dd     = OwnerDeviceData(_owner);

    const UINT sync = ::Video::Configuration::_singleton->vsync != 0 ? 1u : 0u;

#if BANISHED_TWEAK_OVERLAY
    // The debug tweak overlay draws into the back buffer EndCommands resolved (one atomic test closed).
    Diag::TweakOverlay::BeforePresent(dd->d3dDevice, d3dContext, dd->swapChain);
#endif
    // NOT RETAIL (lock scope only): retail presents, then drops the device lock.
    // Here the device lock goes first and the Present runs under the Present
    // mutex, which every off-frame immediate-context user also takes (the note
    // above Dx11PresentMutex, dx11_backend_access.h). A loader thread linking a
    // new buffer into the engine's resource lists no longer waits out the
    // Present; nothing else can touch the immediate context while it runs.
    device->Unlock();
    HRESULT hr;
    {
        std::lock_guard<std::recursive_mutex> presentLock(Dx11PresentMutex());
        hr = dd->swapChain->Present(sync, 0);
    }
    D3dDebug::Drain();   // NOT RETAIL: BANISHED_D3D_DEBUG's info queue (no-op without it)

    // The PLAN 5.5 drive. Substitutes the Present result and nothing else; see
    // the block comment above ForceDeviceLostThisFrame.
    if (ForceDeviceLostThisFrame()) {
        std::fprintf(stderr, "[DeviceLost] INJECTING DXGI_ERROR_DEVICE_RESET "
                             "(real Present returned 0x%08lx)\n",
                     static_cast<unsigned long>(hr));
        std::fflush(stderr);
        hr = DXGI_ERROR_DEVICE_RESET;
    }

    if (hr == 0) return;

    // DeviceData+0x20 (typed by B5 DEV-A, D4). Nothing above writes it, so one
    // read serves both queries below.
    ID3D11Device* const d3dDevice = dd->d3dDevice;
    if (hr != DXGI_ERROR_DEVICE_REMOVED && hr != DXGI_ERROR_DEVICE_RESET)
        hr = d3dDevice->GetDeviceRemovedReason();
    if (hr == 0) return;

    RecoverLostDevice(device, dd, d3dDevice, hr);
}
static_assert(static_cast<unsigned>(DXGI_ERROR_DEVICE_REMOVED) == 0x887a0005u, "hr constant");
static_assert(static_cast<unsigned>(DXGI_ERROR_DEVICE_RESET)   == 0x887a0007u, "hr constant");
static_assert(static_cast<unsigned>(DXGI_ERROR_INVALID_CALL)   == 0x887a0001u, "hr constant");
static_assert(static_cast<unsigned>(DXGI_ERROR_DEVICE_HUNG)    == 0x887a0006u, "hr constant");
static_assert(static_cast<unsigned>(DXGI_ERROR_DRIVER_INTERNAL_ERROR) == 0x887a0020u, "hr constant");

// Build the D3D11_RECT slots 4 and 7 both use: {x, y, x + w, y + h} from
// Context+0x1e0..0x1ec. The shipped DLL has two copies of this (inlined into
// 0x180001920 and standing alone at 0x180001be0); one helper emits the same
// RSSetScissorRects call from both.
void ContextData::SetScissorFromContext() {
    const Viewport& s = _owner->_scissor;
    D3D11_RECT r;
    r.left   = s.x;
    r.top    = s.y;
    r.right  = s.width  + s.x;
    r.bottom = s.height + s.y;
    d3dContext->RSSetScissorRects(1, &r);
}

// ============================================================
// Slot 4 — void Clear(unsigned flags, unsigned color, float depth, int stencil)
//          (0x180001920)
//
// Signature is Runtime's: ?Clear@Context@Video@@QEAAXIIMH@Z. The register
// assignment matches exactly — EDX flags, R8D colour, XMM3 depth, and the
// stencil in the fifth argument slot, which the backend reads as a BYTE
// (`MOVZX EAX, byte ptr [RSP+0x80]` at 0x180001a99).
//
// Clearing forces the three lazily-bound pieces of state to be flushed first,
// because a clear targets whatever is currently bound:
//     flags & 0x0001 -> BindRenderBuffer()
//     flags & 0x0200 -> BindViewport()
//     flags & 0x0400 -> RSSetScissorRects(...)
// then all three dirty bits are cleared in one AND.
//
// Slots 5 and 6 are reached by near CALLs (0x18000194e and 0x180001963), not
// through the vtable; the QUALIFIED calls below are the same non-virtual call.
//
// The colour word is 0xAARRGGBB: the four components are extracted as
// (c>>16)&0xff, (c>>8)&0xff, c&0xff, c>>24 and each is DIVIDED by 255.0f
// (0x180018c54 = 0x437f0000). Division, not multiplication by a reciprocal —
// the two do not give bit-identical results, and this value ends up in a clear
// colour that a RenderDoc capture shows to the last bit.
//
// The clear flags are the engine's own bit 1 = depth, bit 2 = stencil, mapped
// onto D3D11_CLEAR_DEPTH / D3D11_CLEAR_STENCIL; ClearDepthStencilView is
// skipped entirely when neither is asked for.
// ============================================================
void ContextData::Clear(unsigned flags, unsigned color, float depth, int stencil) {
    // The original tests bit 0x01 with a BYTE load of the dirty word; the low
    // byte's bit 0 is the word's bit 0.
    if (static_cast<uint8_t>(_owner->_dirty) & 0x01)
        ContextData::BindRenderBuffer();

    if (_owner->_dirty & 0x200)
        ContextData::BindViewport();

    if (_owner->_dirty & 0x400)
        SetScissorFromContext();

    _owner->_dirty &= 0xfffff9feu;

    if (flags & 1) {
        const float rgba[4] = {
            static_cast<float>((color >> 16) & 0xff) / 255.0f,
            static_cast<float>((color >>  8) & 0xff) / 255.0f,
            static_cast<float>( color        & 0xff) / 255.0f,
            static_cast<float>( color >> 24)         / 255.0f,
        };
        const RenderBufferOwner* rb = _owner->_renderBuffer;
        d3dContext->ClearRenderTargetView(Dx11ImageRtv(rb->colorBuffer), rgba);
    }

    UINT clearFlags = (flags & 2) ? D3D11_CLEAR_DEPTH : 0;
    if (flags & 4) clearFlags |= D3D11_CLEAR_STENCIL;

    if (clearFlags) {
        const RenderBufferOwner* rb = _owner->_renderBuffer;
        d3dContext->ClearDepthStencilView(Dx11ImageDsv(rb->depthBuffer),
                                          clearFlags, depth, static_cast<UINT8>(stencil));
    }
}
static_assert(D3D11_CLEAR_DEPTH == 1 && D3D11_CLEAR_STENCIL == 2,
              "the literals 1 and 2 at 0x180001a6c / 0x180001a76");

// ============================================================
// Slot 5 — void BindRenderBuffer()  (0x180001ad0)
//
// Break any read-after-write hazard, flush the texture bindings if they are
// dirty, then let Runtime bind the render buffer (which reaches
// RenderBufferData::Bind -> OMSetRenderTargets, lane L2's slot).
//
// The hazard loop walks all 16 texture slots and unbinds any that currently
// hold the colour or depth ImageBuffer of the render buffer about to be bound
// as a target. It calls Video::Context::SetImageBuffer(nullptr, i) rather than
// touching D3D11 directly, so the unbind is recorded on the Context and flushed
// with the rest.
//
// The original re-reads `this->context` on every iteration (0x180001af0 is
// inside the loop) and re-reads Context+0x28 each time too. Kept, because
// SetImageBuffer is a Runtime call and this file cannot prove it leaves those
// two fields alone.
//
// This is a near CALL from slot 4 as well as a vtable slot; both paths run the
// identical body.
// ============================================================
void ContextData::BindRenderBuffer() {
    for (int i = 0; i < kTextureSlotCount; ++i) {
        ::Video::Context* const ctx = _owner;
        const ::Video::ImageBuffer* bound = ctx->_imageBuffers[i];
        const RenderBufferOwner* rb = ctx->_renderBuffer;
        if (bound == rb->colorBuffer || bound == rb->depthBuffer)
            ctx->SetImageBuffer(nullptr, i);
    }

    // A byte test in the original, as in slot 4.
    if (static_cast<uint8_t>(_owner->_dirty) & 0x02) {
        _owner->BindTextures();
        _owner->_dirty &= 0xfffffffdu;
    }

    _owner->_renderBuffer->Bind(*_owner);
}

// ============================================================
// Slot 6 — void BindViewport()  (0x180001b60)
//
// RSSetViewports(1, {(float)x, (float)y, (float)w, (float)h, 0.0f, 1.0f}) from
// the int32 viewport at Context+0x1d0. MaxDepth comes from the 1.0f constant at
// 0x180018c50; MinDepth is an `xorps`'d zero.
// ============================================================
void ContextData::BindViewport() {
    const Viewport& v = _owner->_viewport;

    D3D11_VIEWPORT vp;
    vp.TopLeftX = static_cast<float>(v.x);
    vp.TopLeftY = static_cast<float>(v.y);
    vp.Width    = static_cast<float>(v.width);
    vp.Height   = static_cast<float>(v.height);
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;

    d3dContext->RSSetViewports(1, &vp);
}

// ============================================================
// Slot 7 — void BindScissor()  (0x180001be0)
// ============================================================
void ContextData::BindScissor() {
    SetScissorFromContext();
}

// ============================================================
// Slot 8 — void BindTransforms()  (0x180001c40)
//
// Upload the two constant buffers that live INSIDE the Video::Context object:
//
//   Context+0x2050  transforms  <- Context+0x10d8, (Context+0x20 << 6) bytes
//                                  guarded by dirty bit 0x4000
//   Context+0x2008  bones       <- Context+0x0418, exactly 0xcc0 bytes
//                                  guarded by Context+0x24 > 0
//
// The <<6 says a transform is a 64-byte 4x4 matrix, and 0x0418 + 0xcc0 ==
// 0x10d8 says the bone block ends exactly where the transform block starts.
//
// Two asymmetries worth noting, both preserved: the transform upload is gated
// on a dirty bit while the bone upload is gated on a count, and the bone size
// is a constant rather than derived from that count.
//
// Signature of the callee (from the import table):
//     ?Bind@ConstantBuffer@Video@@QEAAXAEAVContext@2@PEAXHH@Z
//       void ConstantBuffer::Bind(Context& context, void* data, int offset, int size)
// The `offset` argument is the literal 0 in both calls. The two receivers are
// the ConstantBuffers EMBEDDED in the Context (`LEA` in retail): named members
// now (B5 CTX), so the B.3.5-era bridge casts are gone.
// ============================================================
void ContextData::BindTransforms() {
    ::Video::Context* ctx = _owner;
    if (ctx->_dirty & 0x4000) {
        ctx->_transformConstants.Bind(*ctx,
                                      ctx->_transforms,
                                      0,
                                      ctx->_transformCount << 6);
    }

    ctx = _owner;
    if (ctx->_unknown0024 > 0) {
        ctx->_boneConstants.Bind(*ctx,
                                 ctx->_boneAndProjectionStaging,
                                 0,
                                 kBoneDataBytes);
    }
}

// ============================================================
// Slot 9 — void BindTextureNone(int slot)  (0x180001cb0)
//
// PSSetSamplers(slot, 1, {NULL}) then PSSetShaderResources(slot, 1, {NULL}).
// That order is the original's (index 10 first, then index 8) and is kept: the
// two calls appear in that sequence in any capture.
//
// Pixel stage only — the vertex stage's sampler/SRV slots are never unbound.
// ============================================================
void ContextData::BindTextureNone(int slot) {
    ID3D11SamplerState*       nullSampler = nullptr;
    ID3D11ShaderResourceView* nullSrv     = nullptr;

    d3dContext->PSSetSamplers(static_cast<UINT>(slot), 1, &nullSampler);
    d3dContext->PSSetShaderResources(static_cast<UINT>(slot), 1, &nullSrv);
}

// ============================================================
// Slot 10 — void BindIndexNone()  (0x180001d10, a 22-byte tail)
//
// IASetIndexBuffer(NULL, DXGI_FORMAT_R16_UINT, 0). The format is the literal
// 0x39 built with `LEA R8D,[R9+0x39]` off an already-zeroed R9 — a null index
// buffer still gets a format, and it is the same 16-bit format
// IndexBufferData::Bind uses (lane L1, 0x180003380).
//
// This slot is the proof that ContextData+0x18 is the cached context: it loads
// [RCX+0x18] and calls straight through it with no singleton walk at all.
// ============================================================
void ContextData::BindIndexNone() {
    d3dContext->IASetIndexBuffer(nullptr, DXGI_FORMAT_R16_UINT, 0);
}
static_assert(static_cast<int>(DXGI_FORMAT_R16_UINT) == 0x39,
              "the literal 0x39 encoded at 0x180001d19");

// ============================================================
// Slot 11 — void BindVertexNone(int slot)  (0x180001d30)
//
// IASetVertexBuffers(slot, 1, {NULL}, {0}, {0}). The stride and offset arrays
// are separate zeroed stack dwords in the original; both are passed by address.
// ============================================================
void ContextData::BindVertexNone(int slot) {
    ID3D11Buffer* nullBuffer = nullptr;
    UINT          stride     = 0;
    UINT          offset     = 0;

    d3dContext->IASetVertexBuffers(static_cast<UINT>(slot), 1,
                                   &nullBuffer, &stride, &offset);
}

// ============================================================
// Slot 12 — void DrawIndexed(PrimitiveType, int baseVertex, int vertexCount,
//                            int startIndex, int indexCount, int instanceCount)
//          (0x180001d80)
//
// Signature from ?DrawIndexed@Context@Video@@QEAAXW4PrimitiveType@12@HHHHH@Z.
// The parameter roles come from where each register lands in the D3D11 call:
//
//     DrawIndexed(indexCount, startIndex, baseVertex)              instanceCount == 1
//     DrawIndexedInstanced(indexCount, instanceCount,
//                          startIndex, baseVertex, 0)              otherwise
//
// `vertexCount` never reaches D3D11 — it exists only to feed the
// verticesSubmitted counter.
//
// *** THE PrimitiveType ARGUMENT IS IGNORED. *** This slot unconditionally
// forces D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, whatever it was handed
// (0x180001de4 compares this->topology against the literal 4 and never looks at
// RDX; RDX is in fact destroyed by the div-by-3 IMUL at 0x180001d9c before it
// could be read). Slots 15/19 by contrast do honour their PrimitiveType. So on
// this backend a non-triangle DrawIndexed silently draws triangles. That is a
// bug in the shipped DLL; reproducing it is the whole point of the exercise.
//
// The triangle counter uses `indexCount / 3` — the assembly is the canonical
// signed divide-by-3 (magic 0x55555556, `MOV EAX,EDX; SHR EAX,31; ADD EDX,EAX`),
// which is exactly C's truncating `/ 3`, negatives included.
// ============================================================
void ContextData::DrawIndexed(int primitiveType, int baseVertex, int vertexCount,
                              int startIndex, int indexCount, int instanceCount) {
    (void)primitiveType;   // deliberately unused past this point — see above

    _owner->_trianglesSubmitted += (indexCount / 3) * instanceCount;
    _owner->_verticesSubmitted  += vertexCount * instanceCount;
    _owner->_drawsSubmitted     += 1;
    _owner->_objectsSubmitted   += instanceCount;

    _owner->BindResources();

    if (topology != D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST) {
        topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        d3dContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    }

    if (instanceCount == 1) {
        d3dContext->DrawIndexed(static_cast<UINT>(indexCount),
                                static_cast<UINT>(startIndex),
                                baseVertex);
    } else {
        d3dContext->DrawIndexedInstanced(static_cast<UINT>(indexCount),
                                         static_cast<UINT>(instanceCount),
                                         static_cast<UINT>(startIndex),
                                         baseVertex,
                                         0);
    }
}
static_assert(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST == 4, "topology literal 4");
static_assert(D3D11_PRIMITIVE_TOPOLOGY_LINELIST == 2, "topology literal 2");

// ============================================================
// Slot 13 — int GetMaxDynamicInstances(int, int, int) const
//          (0x180001ef0: `b8 34 00 00 00 / c3`)
//
// Returns 52 and ignores all three arguments. What the 52 *means* is not
// determinable from this DLL: it is a bare immediate with no relationship to
// any other constant here. It is not the bone-block capacity (0xcc0 / 64 = 51)
// and not the transform-block size. Left as the literal.
// ============================================================
int ContextData::GetMaxDynamicInstances(int /*a*/, int /*b*/, int /*c*/) {
    return 0x34;
}

// ============================================================
// Slot 14 — bool BeginVertices(int vertexCount, int vertexStride)
//          (0x180001e60)
//
// Sub-allocate `vertexCount * vertexStride` bytes out of the Context's shared
// dynamic vertex buffer (Context+0x20f8) and leave the mapped pointer at
// Context+0x20e0 for Video::Context::AddVertex to fill.
//
// The capacity test reads the FIRST DWORD of the Video::VertexBuffer
// (`MOV ECX,[RAX]`, `Video::VertexBuffer::size`) and is a strict `<`: a request
// equal to the capacity is rejected.
//
// Which argument is which is settled by slot 15, which passes Context+0x2104 to
// ID3D11DeviceContext::Draw as the vertex count, and by Runtime's AddVertex
// advancing the cursor by Context+0x20e8. So EDX is the count and R8D the
// stride, matching ?BeginVertices@Context@Video@@QEAA_NHH@Z.
//
// Note the return is `Context+0x20e0 != 0` re-read from memory, not the local
// the Lock returned. Same value; kept literal.
// ============================================================
bool ContextData::BeginVertices(int vertexCount, int vertexStride) {
    ::Video::Context* ctx = _owner;
    if (ctx->_dynamicVertexBuffer->size <= vertexCount * vertexStride)
        return false;

    _owner->_dynamicVertexStride = vertexStride;
    _owner->_unknown2104         = vertexCount;            // +0x2104 vertex count

    ctx = _owner;
    void* mapped = ctx->_dynamicVertexBuffer->Lock(vertexCount * vertexStride,
                                                   ctx->_unknown2100);   // +0x2100 byte offset

    _owner->_dynamicVertexCursor = static_cast<uint8_t*>(mapped);   // CAST-OK: gpu_map (the Lock result)
    _owner->_unknown2108         = _owner->_dynamicVertexCursor;          // +0x2108 Lock result

    return _owner->_dynamicVertexCursor != nullptr;
}

// Topology filter shared by slots 15, 17 and 19 (identical code in each). The
// engine's PrimitiveType 0 maps to TRIANGLELIST and 2 to LINELIST; every other
// value does NOTHING AT ALL — no call, and no update of the cache — so the
// draw inherits whatever topology was last set. Preserved verbatim: the values
// 1 and 3 are presumably strips that this backend never implemented.
void ContextData::ApplyPrimitiveType(int primitiveType) {
    int newTopology;
    if (primitiveType == 0) {
        if (topology == D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST) return;
        newTopology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    } else if (primitiveType == 2) {
        if (topology == D3D11_PRIMITIVE_TOPOLOGY_LINELIST) return;
        newTopology = D3D11_PRIMITIVE_TOPOLOGY_LINELIST;
    } else {
        return;
    }
    topology = newTopology;
    d3dContext->IASetPrimitiveTopology(
        static_cast<D3D11_PRIMITIVE_TOPOLOGY>(newTopology));
}

// ============================================================
// Slot 15 — void EndVertices(PrimitiveType)  (0x180001f00)
//
// Unlock, rebind the dynamic vertex buffer at the offset the Lock handed out,
// account, flush the resource bindings, apply the topology, and draw.
//
// The cursor is nulled BEFORE the Unlock (0x180001f13), which is the reverse of
// the obvious order and is kept.
//
// Statistics here count one object and one draw; the triangle count is
// `vertexCount / 3` (no index buffer involved).
//
// The final Draw is a tail jump: Draw(Context+0x2104, 0).
// ============================================================
void ContextData::EndVertices(int primitiveType) {
    _owner->_dynamicVertexCursor = nullptr;
    _owner->_dynamicVertexBuffer->Unlock();

    ::Video::Context* ctx = _owner;
    ctx->SetVertexBuffer(ctx->_dynamicVertexBuffer,
                         0,
                         ctx->_unknown2100,
                         ctx->_dynamicVertexStride);

    ctx = _owner;
    const int vertexCount = ctx->_unknown2104;
    ctx->_trianglesSubmitted += vertexCount / 3;
    _owner->_verticesSubmitted += _owner->_unknown2104;
    _owner->_drawsSubmitted    += 1;
    _owner->_objectsSubmitted  += 1;

    _owner->BindResources();

    ApplyPrimitiveType(primitiveType);

    d3dContext->Draw(static_cast<UINT>(_owner->_unknown2104), 0);
}

// ============================================================
// Slot 16 — bool BeginIndexedVertices(int vertexCount, int vertexStride,
//                                     int indexCount)  (0x180002000)
//
//     180002004  MOV dword ptr [RSP+0x20],0x1   ; instanceCount = 1
//     18000200c  CALL 0x180002030               ; slot 18
//
// A literal near CALL, so the port makes the same NON-virtual call (qualified).
// ============================================================
bool ContextData::BeginIndexedVertices(int vertexCount, int vertexStride, int indexCount) {
    return ContextData::BeginInstancedIndexedVertices(vertexCount, vertexStride,
                                                      indexCount, 1);
}

// ============================================================
// Slot 18 — bool BeginInstancedIndexedVertices(int vertexCount,
//               int vertexStride, int indexCount, int instanceCount)
//          (0x180002030)
//
// Same sub-allocation as slot 14, plus an index allocation out of
// Context+0x2118. Indices are 16-bit, hence `indexCount * 2` bytes.
//
// *** ORIGINAL BUG, REPRODUCED. *** The second capacity test compares the index
// byte count against the VERTEX buffer's capacity:
//
//     18000204e  MOV ECX,[RAX]        ; RAX = Context+0x20f8, the VertexBuffer
//     18000206a  LEA EDI,[RSI+RSI]    ; indexCount * 2
//     18000206d  CMP EDI,ECX
//     18000206f  JGE fail
//
// ECX is never reloaded from the index buffer at Context+0x2118. Whether that
// is harmless depends on the two buffers' relative sizes, which this DLL does
// not reveal. Fixing it would change which draws are rejected, so it stands.
//
// Note also the two tests use different comparisons: the vertex test rejects
// `capacity <= bytes` and the index test rejects `bytes >= capacity`. Those are
// the same predicate, written differently in the original; no asymmetry.
// ============================================================
bool ContextData::BeginInstancedIndexedVertices(int vertexCount, int vertexStride,
                                                int indexCount, int instanceCount) {
    ::Video::Context* ctx = _owner;
    const int32_t vertexCapacity = ctx->_dynamicVertexBuffer->size;

    if (vertexCapacity <= vertexCount * vertexStride) return false;
    if (indexCount * 2 >= vertexCapacity) return false;   // sic — see above

    ctx->_dynamicVertexStride = vertexStride;
    _owner->_unknown2104      = vertexCount;               // +0x2104 vertex count
    _owner->_instanceCount    = instanceCount;

    ctx = _owner;
    void* mappedVertices = ctx->_dynamicVertexBuffer->Lock(vertexCount * vertexStride,
                                                           ctx->_unknown2100);   // +0x2100

    _owner->_dynamicVertexCursor = static_cast<uint8_t*>(mappedVertices);   // CAST-OK: gpu_map
    _owner->_unknown2108         = _owner->_dynamicVertexCursor;

    if (_owner->_dynamicVertexCursor == nullptr) return false;

    _owner->_unknown2124 = indexCount;                     // +0x2124 index count

    ctx = _owner;
    void* mappedIndices = ctx->_dynamicIndexBuffer->Lock(indexCount * 2,
                                                         ctx->_unknown2120);     // +0x2120

    _owner->_dynamicIndexCursor = static_cast<uint16_t*>(mappedIndices);   // CAST-OK: gpu_map
    _owner->_unknown2128        = _owner->_dynamicIndexCursor;                   // +0x2128

    return _owner->_dynamicIndexCursor != nullptr;
}

// ============================================================
// Slot 17 — void EndIndexedVertices(PrimitiveType)  (0x180002020)
//
// Retail's slot 17 is the five bytes `e9 2b 01 00 00` = `jmp 0x180002150`:
// slot 19's body. One body serves both because the instance count lives on the
// Context (+0x2110) and the non-instanced Begin (slot 16) stores 1 there, so
// slot 19's `instanceCount == 1` arm is exactly the non-instanced draw. The
// qualified call is the jmp's non-virtual target.
// ============================================================
void ContextData::EndIndexedVertices(int primitiveType) {
    ContextData::EndInstancedIndexedVertices(primitiveType);
}

// ============================================================
// Slot 19 — void EndInstancedIndexedVertices(PrimitiveType)  (0x180002150;
//           slot 17 jumps here)
//
// Unlock both dynamic buffers, rebind the vertex stream, mark the index binding
// dirty on the Context if it moved, account, flush, apply the topology and draw.
//
// The index-buffer dirty check is the one place this slot writes a Context
// dirty bit (0x10). It compares BOTH the buffer pointer and the byte offset
// against Context+0x1b8 / +0x1c0, which is what the ordinary (non-dynamic)
// SetIndexBuffer path records.
//
// StartIndexLocation is `Context+0x2120 >> 1`: a byte offset converted to a
// 16-bit index count. The original sign-extends the dword to 64 bits and then
// shifts LOGICALLY right (`MOVSXD R8,...` + `SHR R8,1`), so a negative offset
// would produce an enormous index — an impossible input in practice, but the
// cast chain below reproduces it exactly rather than writing `/ 2`.
//
// Statistics use the INSTANCE count as a multiplier on both triangles and
// vertices, and add the instance count (not 1) to objectsSubmitted.
// ============================================================
void ContextData::EndInstancedIndexedVertices(int primitiveType) {
    _owner->_dynamicVertexCursor = nullptr;
    _owner->_dynamicVertexBuffer->Unlock();

    _owner->_dynamicIndexCursor = nullptr;
    _owner->_dynamicIndexBuffer->Unlock();

    ::Video::Context* ctx = _owner;
    ctx->SetVertexBuffer(ctx->_dynamicVertexBuffer,
                         0,
                         ctx->_unknown2100,
                         ctx->_dynamicVertexStride);

    ctx = _owner;
    ::Video::IndexBuffer* const dynIb     = ctx->_dynamicIndexBuffer;
    const int32_t               dynOffset = ctx->_unknown2120;
    if (dynIb     != ctx->_indexBuffer ||
        dynOffset != ctx->_indexOffset) {
        ctx->_dirty       |= 0x10u;
        ctx->_indexBuffer  = dynIb;
        ctx->_indexOffset  = dynOffset;
    }

    ctx = _owner;
    const int indexCount    = ctx->_unknown2124;
    const int instanceCount = ctx->_instanceCount;
    ctx->_trianglesSubmitted += (indexCount / 3) * instanceCount;

    ctx = _owner;
    ctx->_verticesSubmitted += ctx->_instanceCount * ctx->_unknown2104;
    _owner->_drawsSubmitted   += 1;
    _owner->_objectsSubmitted += _owner->_instanceCount;

    _owner->BindResources();

    ApplyPrimitiveType(primitiveType);

    ctx = _owner;
    const UINT startIndex = static_cast<UINT>(
        static_cast<uint64_t>(static_cast<int64_t>(ctx->_unknown2120)) >> 1);

    if (ctx->_instanceCount == 1) {
        d3dContext->DrawIndexed(static_cast<UINT>(ctx->_unknown2124), startIndex, 0);
    } else {
        d3dContext->DrawIndexedInstanced(static_cast<UINT>(ctx->_unknown2124),
                                         static_cast<UINT>(ctx->_instanceCount),
                                         startIndex, 0, 0);
    }
}

}  // namespace VideoDX11
