#pragma once
// Shared plumbing for the ported VideoDX11 resource classes.
//
// This header holds the two things every ported *Data slot needs and that are
// NOT class-specific:
//
//   1. The walk from the Video::Device singleton down to ID3D11Device /
//      ID3D11DeviceContext, reproduced exactly as the shipped DLL does it.
//   2. The *Runtime-side* owner types the ported slots read fields out of
//      (Video::ShaderProgram, Video::RenderBuffer, Video::VertexLayout,
//      Video::ImageBuffer): all four are real classes in `src/engine/video/`,
//      and this file includes those headers instead of restating offsets.
//
// It deliberately does NOT live in src/render/dx11/VideoDX11Data.h: that file is the
// home of the twelve VideoDX11-defined *Data layouts. Everything here describes
// Runtime-side types instead. Include this header rather than duplicating it.
//
// Every offset below is proven by a decompiled VideoDX11 function; the proof is
// cited inline. Nothing here is guessed.

#include <windows.h>
#include <d3d11.h>

#include <cstdint>
#include <cstddef>
#include <mutex>

#include "collection/Collection.h"   // Collection::Array<T> (src/engine)
// ENDGAME B.3.5 block A — the real `Video::VertexLayout` (src/engine/video/), which
// replaces this file's former `VertexLayoutOwner` / `VertexLayoutElement`
// views. B35_RULES 1.2: `src/render/dx11/` compiles with the shared include set
// (BANISHED_INCLUDES in CMakeLists.txt, which has `src/engine`).
#include "video/VertexLayout.h"
// ENDGAME B.3.5 block B — the real `Video::ShaderProgram` / `VertexProgram` /
// `PixelProgram`, which replace this file's former `ShaderProgramOwner` view.
#include "video/ShaderProgram.h"
// ENDGAME B.3.5 block C, RULE B5-2.1 — the real `Video::ImageBuffer`, which
// replaces this file's former `ImageBufferOwner` view.
#include "video/ImageBuffer.h"
// ENDGAME B.3.5 block D2, RULE B5-2.3 — the real `Video::RenderBuffer`
// (0x30 bytes), which replaces this file's former `RenderBufferOwner` view.
#include "video/Device.h"
#include "VideoDX11Data.h"
#include "VideoDX11Plugin.h"   // B5 DEV-A: Dx11SwapChain (D7)
// ENDGAME B.3R block DX: `System::MemoryAllocator::MemAlloc / MemFree`, the
// direct replacement for the import rows the files below resolved by name.
// (`resolve.h` went with `Dx11ResolveRuntimeImport`, its last user here.)
#include "system/MemoryAllocator.h"

namespace VideoDX11 {

// ============================================================
// Reaching D3D11
// ============================================================
//
// Every resource class in the DLL except ContextData refuses to cache the
// device or the context. It walks to them on every single call, through the
// one global the DLL has (VideoDX11 .rdata 0x1800134b8, an IAT slot holding the
// address of Runtime's Video::Device::_singleton at Runtime 0x1801fe7a8):
//
//     mov rax, [rip+...]      ; 0x1800134b8 -> Video::Device**
//     mov rdx, [rax]          ; Video::Device*
//     mov rax, [rdx+0x100]    ; active Video::DeviceData*   (our DeviceData)
//     mov rcx, [rax+0x28]     ; ID3D11DeviceContext*
//     mov rbx, [rax+0x20]     ; ID3D11Device*
//
// (See decomp/videodx11.md §"The one global". The +0x20 / +0x28 identification
// is from src/render/dx11/VideoDX11Data.h, itself proven by the CreateTexture2D
// calls in 0x1800052b0 / 0x180005350.)
//
// We take the singleton's *address* (the class static) and re-read its contents on every
// call, exactly like the original: Video::Device::_singleton can in principle
// be re-pointed, and DeviceData+0x20/+0x28 are definitely replaced across a
// device reset, so caching either would be a behavioural change.
//
// NOTE ON NULL CHECKS: there are none, in the original or here. The shipped
// code dereferences this chain unconditionally; a null anywhere in it means the
// renderer was never brought up, which is already unrecoverable. Adding guards
// would hide that instead of reporting it, and would be a divergence from the
// original for no gain.

// Ghidra address of the VideoDX11 IAT slot the original reads it through.
// Retained as documentation of the shipped import; nothing reads it now.
constexpr uintptr_t kDx11Iat_VideoDeviceSingleton = 0x1800134b8;

// Runtime's Video::Device::_singleton (0x1801fe7a8) — the class static since
// ENDGAME B.3.9 VID; dx11 is compiled into the exe, so it is read directly.
inline ::Video::Device** Dx11VideoDeviceSingletonSlot() {
    return &::Video::Device::_singleton;
}

// Video::Device* — the live singleton.
inline ::Video::Device* Dx11VideoDevice() {
    return *Dx11VideoDeviceSingletonSlot();
}

// A Video::Device's DeviceData: `Video::Device::deviceData` (+0x100,
// decomp/videodx11.md "How Runtime loads it").
//
// B5 DEV-A (B5_RULES D7): the ONE downcast dx11 makes. The field is the base
// `Video::DeviceData*`; dx11 built the object (Dx11_CreateDeviceData,
// exports.cpp), so it is a `VideoDX11::DeviceData`. The DX9 placeholder is the
// only other producer, and every body that reaches this runs on a device DX11
// created (the placeholder never creates one). NO NULL CHECKS, as above.
// ContextData and ConstantBufferData reach it through their own owner's
// `_device` (as the original does), everything else through the singleton.
inline DeviceData* Dx11DeviceDataOf(const ::Video::Device* device) {
    ::Video::DeviceData* const base = device->deviceData;   // the typed base pointer
    return static_cast<DeviceData*>(base);                  // the downcast dx11 may make: it built the object
}

// The active DeviceData: the singleton's.
inline DeviceData* Dx11DeviceData() {
    return Dx11DeviceDataOf(Dx11VideoDevice());
}

inline ID3D11Device* Dx11Device() {
    return Dx11DeviceData()->d3dDevice;
}

inline ID3D11DeviceContext* Dx11Context() {
    return Dx11DeviceData()->d3dContext;
}

// ============================================================
// The Present lock (NOT RETAIL: backend lock scope, issue #13 DEVLOCK)
//
// Retail's ContextData::Finish (0x1800017c0) presents and THEN drops
// Video::Device's lock, so the lock is held from BeginCommands through the
// Present. That lock serves two unrelated purposes: it keeps the D3D11
// immediate context single-threaded (the device is created without
// D3D11_CREATE_DEVICE_SINGLETHREADED, so ID3D11Device is free-threaded but the
// context is not), and it guards the engine's intrusive resource lists
// (`ListLink` / `ListUnlink` in every buffer's PlatformConstruct /
// PlatformDestruct, src/engine/video/buffers.cpp). A loader thread creating a
// buffer therefore waited out a whole Present, which is slow while the loader
// keeps the GPU busy.
//
// This backend's Finish drops the device lock BEFORE the Present and presents
// under this mutex instead. Present still never overlaps another thread's use
// of the immediate context: every backend body a thread other than the one
// recording the frame can reach that touches the context takes this mutex
// (inside the device lock its caller holds, so the order is always device lock
// -> this mutex; nothing takes the device lock while holding it):
//   GpuBuffer_SetData (Vertex/IndexBufferData::SetData), GpuBuffer_Serialize's
//   save arm, ImageBufferData::Update (CopyToTexture) and ::Read
//   (CopyFromTexture). The rest of the context users (Bind / Draw / Clear /
//   Map / Unmap of the Context's dynamic buffers / Resize) run only on the
//   thread recording the frame, which is the thread that presents.
// The resource-list links no longer wait for the Present. Same D3D11 calls,
// same order on each thread, same frames; see docs/extensions/handins/DEVLOCK.md.
// ============================================================
std::recursive_mutex& Dx11PresentMutex();

// D7: `IDXGISwapChain* Dx11SwapChain()` — the swap chain (+0x18) by name, for
// swap_probe_dx11.cpp's two probes, which do not include this header (it pulls
// <d3d11.h> and the runtime type headers). Declared in VideoDX11Plugin.h
// (included above), defined in dx11_backend_access.cpp as
// `Dx11DeviceData()->swapChain`.

// Returns true when the address of Video::Device::_singleton is non-null.
// VideoDX11-x64.dll is never mapped, so there is no IAT-held copy to compare with.
bool Dx11VerifyDeviceSingleton();

// ============================================================
// Runtime-side container shape
// ============================================================
//
// Collection::Array<T>. The name comes from the Runtime export
//   ?GetByteCode@ShaderProgram@Video@@QEBAAEBV?$Array@E@Collection@@H@Z
//     -> const Collection::Array<unsigned char>& ShaderProgram::GetByteCode(int)
// and the layout from how VideoDX11 walks these arrays: a base pointer at +0x00
// and an int32 element count at +0x08, with the whole header 0x10 bytes (that
// last part proven by the 0x10 stride the shader-program loop at 0x1800041a0
// uses to step from one Array<unsigned char> to the next).
//
// +0x0C is inferred to be the capacity from the universal {data,count,capacity}
// shape used elsewhere in Runtime (e.g. DeviceData+0x60/+0x68/+0x6c). Nothing
// in this lane reads it.
//
// ENDGAME B.3.2 step 2 — this WAS a private `RtArray` POD spelling the same
// three fields. It is now the real `Collection::Array<T>`
// (src/engine/collection/Collection.h), which is where that layout has been
// pinned since Wave 2 and which the three static_asserts below still check
// field for field. The two shader-program readers are pure READ views over
// memory Runtime owns: nothing here constructs, resizes or destroys one, so the
// class's SetSize/~Array (and therefore the allocator) are never reached from
// src/render/dx11/.
using RtByteCodeBlob = Collection::Array<uint8_t>;
using RtByteCodeList = Collection::Array<RtByteCodeBlob>;
static_assert(sizeof(RtByteCodeBlob) == 0x10, "Collection::Array<T> header size");
static_assert(offsetof(RtByteCodeBlob, _data)     == 0x00, "Array::_data @ +0x00");
static_assert(offsetof(RtByteCodeBlob, _size)     == 0x08, "Array::_size @ +0x08");
static_assert(offsetof(RtByteCodeBlob, _capacity) == 0x0C, "Array::_capacity @ +0x0C");

// ============================================================
// Video::ShaderProgram — the owner of VertexProgramData / PixelProgramData
// ============================================================
//
// VertexProgramData+0x08 is a Video::VertexProgram* and PixelProgramData+0x08 a
// Video::PixelProgram* (Runtime ctor exports
// ??0VertexProgramData@Video@@QEAA@PEAVVertexProgram@1@@Z and
// ??0PixelProgramData@Video@@QEAA@PEAVPixelProgram@1@@Z). Both derive from
// Video::ShaderProgram, and 0x1800041a0 / 0x180003610 read *identical* offsets
// out of them — which is what makes the fields below base-class fields.
//
// Proven by 0x1800041a0 (VertexProgramData::PlatformCreate):
//     CMP  EBX, dword ptr [RAX + 0x48]     ; permutation count
//     MOV  RAX, qword ptr [RAX + 0x40]     ; base of the byte-code blobs
//     MOV  RDX, qword ptr [RAX + RDI]      ; blob[i].data     (RDI = i*0x10)
//     MOVSXD R8, dword ptr [RAX + RDI + 8] ; blob[i].length
//
// i.e. ShaderProgram+0x40 is a Collection::Array<Collection::Array<uint8>> —
// one compiled byte-code blob per shader permutation, at most 4 of which the
// DX11 backend ever creates.
// ENDGAME B.3.5 block B, RULE B5-2.1 — `ShaderProgramOwner` IS GONE. It was a
// partial, offset-pinned view (`opaque00[0x40]` + `byteCode` @ +0x40) of a
// Runtime-side class that is now real: `Video::ShaderProgram`
// (src/engine/video/ShaderProgram.h), where the same two offsets are pinned
// by the same 0x1800041a0 reads plus the Runtime-side evidence, and where
// `byteCode` is spelled `_byteCode` as retail's own accessors name it. Its two
// users in `src/render/dx11/shaderprograms.cpp` name `Video::ShaderProgram` directly —
// exactly the way B.3.2 step 2 replaced this file's private `RtArray` POD with
// the real `Collection::Array<T>` (see the note above `RtByteCodeBlob`) and the
// way block A retired `VertexLayoutOwner`.
//
// The view's two `static_assert`s are not lost: they are
// `offsetof(ShaderProgram, _byteCode) == 0x40` and the `_size` sum == 0x48 in
// that header, alongside the retail `sizeof` of 0x50 the view could not state.

// The DX11 backend creates at most this many shader permutations, regardless of
// how many byte-code blobs the program actually carries: the loop in
// 0x1800041a0 / 0x180003610 is `for (i = 0; i < 4; ++i)`, and the variant array
// on VertexProgramData / PixelProgramData is exactly 4 pointers wide.
constexpr int kShaderVariantCount = 4;
static_assert(sizeof(VertexProgramData::variants) / sizeof(void*) == kShaderVariantCount,
              "VertexProgramData variant array width");
static_assert(sizeof(PixelProgramData::variants) / sizeof(void*) == kShaderVariantCount,
              "PixelProgramData variant array width");

// ============================================================
// Video::VertexLayout — the owner of VertexLayoutData
// ============================================================
//
// VertexLayoutData+0x08 is a Video::VertexLayout* (Runtime ctor export
// ??0VertexLayoutData@Video@@QEAA@PEAVVertexLayout@1@@Z). Runtime also exports
// ?GetElementCount@VertexLayout@Video@@QEBAHXZ and
// ?GetElement@VertexLayout@Video@@QEBAAEBUElement@12@H@Z, which is where the
// `Element` struct name comes from.
//
// Proven by 0x180003ff0 (VertexLayoutData::PlatformCreate):
//     CMP  dword ptr [R9 + 0x40], 0x0   ; guard: input-signature length
//     MOV  R8D, dword ptr [R9 + 0x28]   ; element count
//     MOV  RAX, qword ptr [R9 + 0x20]   ; element array base (stride 0x20)
//     MOV  R9,  qword ptr [R9 + 0x38]   ; input-signature byte code
//     MOVSXD RDX, dword ptr [R9 + 0x40] ; input-signature length
// ENDGAME B.3.2 step 2 — both members are the real `Collection::Array<T>`, with
// the element type spelled out instead of a `void*` the reader had to cast.
//
// ENDGAME B.3.5 block A, RULE B5-2.2 — `VertexLayoutOwner` and
// `VertexLayoutElement` ARE GONE. Both were partial, offset-pinned views of a
// Runtime-side class that had no header anywhere in the tree; that header now
// exists, with the four offsets above folded in and their proofs kept:
// `src/engine/video/VertexLayout.h`. The two users in
// `src/render/dx11/vertexlayoutdata.cpp` name `Video::VertexLayout` and
// `Video::VertexLayout::Element` directly, exactly the way B.3.2 step 2
// replaced this file's private `RtArray` POD with the real
// `Collection::Array<T>` (see the note above `RtByteCodeBlob`).
//
// ONE FIELD NAME CHANGED, AND IT WAS A CORRECTION, NOT A RENAME: this view's
// `byteOffset` @ +0x0C is `Element::offset`, and this view was RIGHT about it
// (as it was about `stream` @ +0x08 and `format` @ +0x18). The banner in
// VertexLayout.h records the retail reads that settle it, and which of the two
// pre-existing views of this class was wrong about the other three fields.
//
// The narrowing this file's loop performs is retail's, not a consequence of
// the view: 0x180003ff0 reads +0x08 and +0x0C with `MOVZX ...,word`, i.e. the
// low 16 bits of two int32 fields. `vertexlayoutdata.cpp` now spells that
// truncation out.

// ============================================================
// Video::RenderBuffer / Video::ImageBuffer — owners reached by RenderBufferData
// ============================================================
//
// RenderBufferData+0x08 is a Video::RenderBuffer* (Runtime ctor export
// ??0RenderBufferData@Video@@QEAA@PEAVRenderBuffer@1@@Z), and Runtime exports
//     ?ColorBuffer@RenderBuffer@Video@@QEBAPEBVImageBuffer@2@XZ
//     ?DepthBuffer@RenderBuffer@Video@@QEBAPEBVImageBuffer@2@XZ
//     ?Create@RenderBuffer@Video@@QEAA_NPEBVImageBuffer@2@0@Z
// i.e. a RenderBuffer is exactly a (color, depth) ImageBuffer pair. There is no
// ??_7RenderBuffer@Video@@6B@ export, so the class is non-polymorphic and can
// legitimately start with a data member at +0x00.
//
// Proven by 0x180001070 (RenderBufferData::Bind):
//     MOV RDX, qword ptr [RCX + 0x8]   ; this->renderBuffer
//     MOV RAX, qword ptr [RDX]         ; +0x00 -> color ImageBuffer*
//     MOV RAX, qword ptr [RDX + 0x8]   ; +0x08 -> depth ImageBuffer*
// ============================================================
// Constant-buffer binding registry (device-reset bookkeeping)
//
// DeviceData::Resize calls ID3D11DeviceContext::ClearState(), which unbinds
// every constant buffer from its VS/PS slot. ConstantBufferData::Bind only
// issues VS/PSSetConstantBuffers when its cached rung index changes, so after a
// ClearState nothing ever re-binds them: every shader then reads a null
// constant buffer, all transforms come back zero, no geometry rasterises, and
// the window is black for the rest of the run. See the note on
// Dx11InvalidateConstantBufferBindings in buffers.cpp.
//
// The registry is only touched on create/destroy and once per device reset,
// never per draw.
// ============================================================
void Dx11TrackConstantBuffer(ConstantBufferData* p);
void Dx11UntrackConstantBuffer(ConstantBufferData* p);
void Dx11InvalidateConstantBufferBindings();

// ClearState() also drops the primitive topology, which ContextData caches in
// `topology` and would otherwise never re-issue. DeviceData_Resize bumps this
// counter; ContextData::BeginCommands compares it with its own copy (the
// otherwise unused `pad14`) and clears the cache when they differ.
void     Dx11NoteClearState();
unsigned Dx11ClearStateGeneration();

// ENDGAME B.3.5, RULE B5-2.3 — COLLAPSED. Block A deleted the
// `sizeof(RenderBufferOwner) == 0x10` assertion (a size assert on a prefix view
// of a 0x30-byte class: it could not fail, because it described the view rather
// than the class, and what it said about the class was untrue) and left the view
// itself for "the block that converts RenderBuffer". This is that block: the
// duplicate two-pointer POD is GONE and the name is now an ALIAS of the real
// `Video::RenderBuffer` (src/engine/video/Device.h), which carries the true
// 0x30 and all seven offsets. Two of the three views of this one class are now
// one definition; the third, `Context.h`'s `RtVideo::RenderBufferPrefix`, is
// b35-D1's file and is theirs to collapse.
//
// The FIELD NAMES change with the type: `color`/`depth` become the class's
// `colorBuffer`/`depthBuffer`. The two use sites in `smallclasses.cpp` and the
// four in `contextdata.cpp` are re-spelled; nothing else in dx11 named the view.
using RenderBufferOwner = ::Video::RenderBuffer;
static_assert(offsetof(RenderBufferOwner, colorBuffer) == 0x00, "RenderBuffer::colorBuffer");
static_assert(offsetof(RenderBufferOwner, depthBuffer) == 0x08, "RenderBuffer::depthBuffer");
static_assert(sizeof(RenderBufferOwner) == 0x30,
              "RULE B5-2.3: the real class, not a 0x10-byte prefix view");

// Video::ImageBuffer+0x60 is the backend companion object, i.e. the
// VideoDX11::ImageBufferData whose layout is in src/render/dx11/VideoDX11Data.h.
//
// Proven by 0x180001070 / 0x1800010e0, which read
//     imageBuffer->[0x60]->[0x18]   the D3D texture
//     imageBuffer->[0x60]->[0x20]   the render-target view
//     imageBuffer->[0x60]->[0x28]   the depth-stencil view
// and cross-checked against ImageBufferData's own creator 0x180002870, which
// stores into exactly those three offsets of `this`:
//     CALL [dev+0x38] (index  7 = CreateShaderResourceView) -> this+0x30
//     CALL [dev+0x48] (index  9 = CreateRenderTargetView)   -> this+0x20
//     CALL [dev+0x50] (index 10 = CreateDepthStencilView)   -> this+0x28
// and 0x180002580, which stores the swap-chain colour texture into this+0x18.
// ENDGAME B.3.5 block C, RULE B5-2.1 — THE `ImageBufferOwner` VIEW IS GONE.
// It was `uint8_t opaque00[0x60]` plus `void* data` at +0x60, a partial,
// offset-pinned duplicate of a Runtime-side layout. `Video::ImageBuffer` is now a
// real class (src/engine/video/ImageBuffer.h, included above) whose `_data`
// member is that same +0x60, asserted against the retail ctor there rather than
// against this file's one instruction. The three helpers below lose their cast
// through the view (and, B5 RES, their `const void*` parameter: every caller
// holds a typed `const Video::ImageBuffer*` out of a `Video::RenderBuffer`).
//
// ImageBufferData fields this lane reads. L2 derived these offsets from
// RenderBufferData slots 2/3 (0x180001070, 0x1800010e0) while L3 derived them
// independently from ImageBufferData slots 1/2/3 — the two lanes agreed
// exactly, so +0x18/+0x20/+0x28 are texture/RTV/DSV on two separate proofs.
// L3 owns the struct and has now named them; these helpers just spell out which
// view each caller wants.
//
// `_data` is a `Video::ImageBufferData*` — Runtime's ABSTRACT base (block A).
// B5 RES: `VideoDX11::ImageBufferData` is a real subclass of it, and every live
// companion was built by dx11's own factory (`DeviceData` slot 10,
// `new ImageBufferData(owner)`), so the step down is a `static_cast` — the one
// downcast dx11 may make (B5_RULES D7: dx11 built the object). The helpers take
// the typed `Video::ImageBuffer*` their callers already hold.
inline ImageBufferData* Dx11ImageBackend(const Video::ImageBuffer* img) {
    Video::ImageBufferData* const base = img->_data;   // the typed base pointer
    return static_cast<ImageBufferData*>(base);
}
inline ID3D11Resource* Dx11ImageTexture(const Video::ImageBuffer* img) {
    return Dx11ImageBackend(img)->texture;
}
inline ID3D11RenderTargetView* Dx11ImageRtv(const Video::ImageBuffer* img) {
    return Dx11ImageBackend(img)->renderTargetView;
}
inline ID3D11DepthStencilView* Dx11ImageDsv(const Video::ImageBuffer* img) {
    return Dx11ImageBackend(img)->depthStencilView;
}

// ============================================================
// Runtime imports used by the ported destructors
// ============================================================
//
// ENDGAME B.3R block DX: GONE — `Dx11ResolveRuntimeImport` (a by-name lookup
// in the port registry), the `Dx11BaseDtor_t` / `Dx11MemFree_t` typedefs and
// the `Dx11MemFree` wrapper with its lazy function-local cache. Every Runtime
// import dx11 used is now a direct reference to the body its mangled name
// bound (B3R_RULES R2, R8). Since B5 the destructors are `~XData() override`:
// the compiler chains to the base destructor, and `delete` frees through the
// base's class-scope `operator delete` (retail's MemFree, VideoDX11 IAT slot
// 0x1800134d0; src/engine/video/companion_operators.cpp).

// L6's port of 0x180004340, the DeviceData swap-chain resize/recreate helper.
// Declared here because it is the one cross-lane call in the port: L5's
// ContextData::BeginCommands reaches it with a near CALL rather than a vtable
// slot, so it needs a declaration with external linkage.
// B5 DEV-A: takes the dx11 DeviceData itself (it was `void*` while the class
// was L6's internal POD); the caller reaches it with Dx11DeviceDataOf.
int Dx11_DeviceData_Resize(DeviceData* deviceData);

// COM Release() through the IUnknown vtable (index 2, byte offset 0x10) —
// exactly the `MOV RAX,[RCX]; CALL [RAX+0x10]` the destructors emit.
inline void Dx11Release(IUnknown* comObject) {
    if (comObject) comObject->Release();
}

}  // namespace VideoDX11
