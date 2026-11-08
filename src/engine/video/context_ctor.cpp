// ============================================================================
// src/engine/video/context_ctor.cpp — Wave 9 lane VID2 phase 4
//   ??0Context@Video@@, its four zero-fill sub-object constructors, and
//   ?CreateVideoContext@MainLoop@Engine@@ — the last two links of the chain
//   ConstantBuffer -> AssignConstants -> Context ctor -> CreateVideoContext.
// ============================================================================
//
// THE ORACLE WAS CHOSEN BEFORE ANY BODY WAS WRITTEN, AND MY EARLIER REASON FOR
// THINKING IT IMPOSSIBLE WAS WRONG
// ---------------------------------------------------------------------------
// Phase 3's lane doc said the ctor "makes seven CALL [RAX+0x58] virtual calls on
// a real Video::Device, so the synthetic subject that verified AssignConstants
// does not carry over". **That is wrong, and reading the bodies is what showed
// it.** Not one of those calls is on the Device. Every one of them is
//
//     factory = device->+0x100;                       // the backend DeviceData
//     buf->+0x40 = factory ? factory->vtbl[11](factory, buf) : nullptr;
//
// — a companion factory hanging off the Device, and it is NULL-CHECKED. The
// same is true of the one `[RAX+0x48]` (slot 9, the Context's own companion).
// The only UNGUARDED virtual calls in the whole function are the three
// `buf->+0x08->vtbl[1](…)` after each dynamic buffer is constructed and the two
// `img->+0x60->vtbl[1|2]()` in the default-render-buffer branch — and every one
// of those sits behind an ARGUMENT-VALUE guard the caller controls
// (`size > 0`, `renderBuffer == nullptr`).
//
// So the subject is synthetic, in TWO configurations, and neither needs a
// device:
//
//   A. `Context(zeroedDevice, 0, 0, 0, aNonNullRenderBuffer)`. Every guarded
//      virtual takes its null branch and every unguarded one is skipped
//      entirely. This covers the seven inlined ConstantBuffer constructors, the
//      four zero-fills, the four memsets, AssignConstants and the whole field
//      map — with no allocation and no backend at all.
//   B. The LIVE shape, `(device, 0x800000, 0x400000, 0x100000, nullptr)`, which
//      is literally what CreateVideoContext passes, driven against a
//      DEVICE-LEVEL FAKE: one object with a vtable whose slot 11 and slot 9
//      hand back fake companions, installed in the `?_singleton@Device@Video@@`
//      slot for the duration (borrow-and-restore, the trick lane VID2 already
//      used for Video::Configuration) so that the ported PlatformConstructs
//      find it too.
//
// A live `RT_VIDCONSTBUF` A/B was the alternative and was REJECTED: it
// can only report "did it boot", which is precisely the executed-but-undiffed
// state, and it cannot compare a single field of the object under construction.
//
// THE FOUR "ARRAY FILLS" ARE NOT ARRAY FILLS
// ------------------------------------------
// Phase 3 called 0x180053730/840/8b0/920 "four Array-family fills, siblings of
// 0x180053510". Checked by BODY, they are nothing of the kind — no data/size/
// capacity, no allocator, no element type. They are four plain zero-fill
// constructors for four differently-shaped sub-objects:
//
//   0x180053730  0xd0 bytes at +0x328   two 4x4 float matrices + 10 pointers
//   0x180053840  51 x 0x40 at +0x418    -> bones[51]
//   0x1800538b0  52 x 0x40 at +0x10d8   -> transform[52]
//   0x180053920  0x1a0 bytes at +0x1dd8
//
// The middle two differ ONLY in one immediate — `MOV EDX,0x32` against
// `MOV EDX,0x33` — which is exactly the lookalike trap that family has sprung
// before. And the counts are 51 and 52, NOT 50 and 51: the loop is
// `DEC EDX … JNS`, so it runs until EDX goes negative and executes imm+1 times.
// Both numbers are confirmed twice over: the fill sizes tile the Context
// exactly (0x418 + 51*0x40 == 0x10d8, and 0x10d8 + 52*0x40 == 0x1dd8), and they
// match `bones[51]` and `Format("transform[%i]", 52)` in AssignConstants.
//
// THE ALLOCATOR CALLS, ALSO CHECKED BY BODY
// -----------------------------------------
// The ctor's `operator new` is 0x180011640 and CreateVideoContext inlines its
// own allocation. Disassembled, both are
// `Enter(alloc+0x640); Allocate(alloc, n, align); Leave;` — byte-for-byte the
// shape of `?MemAlloc@MemoryAllocator@System@@` (0x1800c7a50), with align 4 for
// `operator new` and align 8 for the Context block. So both are expressed here
// as System::MemoryAllocator::MemAlloc, called directly, and
// engine_lifecycle.cpp:1111 records the same equivalence and the same reason
// for not registering 0x180011640 itself.
//
// FIELDS THE CTOR NEVER WRITES — an oracle must not compare them
// ---------------------------------------------------------------
// The inlined ConstantBuffer constructor writes +0x18 as a BYTE and +0x1c as an
// UNALIGNED QWORD, so **+0x19, +0x1a and +0x1b are never written**. In a fresh
// heap block they hold whatever was there. The probe compares fields, not
// bytes, and does not look at them
// (reference_oracle_must_not_compare_unwritten_bytes.md).
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>

#include "system/MemoryAllocator.h"
#include "video/Device.h"   // ENDGAME B.3.9 VID: Video::Device::_singleton
#include "video/Context.h"
#include "engine/MainLoop.h"   // ENDGAME B.3.7: Engine::MainLoop::CreateVideoContext
// ENDGAME B.3.5 block VC — the three headers the converted call sites need:
// `Video::{Index,Vertex}Buffer` and their `*Data` companions, `Video::ImageBuffer`
// and `Video::{Device,ImageBuffer,ConstantBuffer}Data`'s slot names.
#include "video/Buffers.h"
#include "video/CompanionData.h"
#include "video/ImageBuffer.h"

// ============================================================================
// ENDGAME B.3.5 block D1 — WHAT DID AND DID NOT CHANGE HERE
// ============================================================================
// NOTHING IN THIS FILE BECOMES A MEMBER. `??0Context@Video@@` stays a registered
// ctor DOOR (B4b-2 / B35_RULES 11): it runs on 0x2168 raw bytes from
// `System::MemoryAllocator` whose first field it then writes, it is inlined
// seven times over `Video::ConstantBuffer`'s own constructor, and a real
// constructor would additionally destroy `std::is_trivially_copyable<Context>`,
// which nine files depend on. The four sub-object zero-fills stay free functions
// for the same reason. What the conversion bought here is TYPES: `Context*` and
// `Video::ConstantBuffer*` instead of `void*` plus raw offsets, and one
// RT_EXPORT_ONCE retired.
//
// THE FOUR BY-ADDRESS-ONLY ROWS ARE CONFIRMED AS `Video::Context`'s, not
// inferred from their names (B35_FILEMAP flags them as INFERRED). Ghidra
// `getReferencesTo` on each of 0x180053730 / 0x180053840 / 0x1800538b0 /
// 0x180053920 in the retail DLL (15290 functions) returns exactly TWO callers
// each, and both are `Video::Context` constructors:
//
//   0x1800f19cc / 0x1800f19f4 / 0x1800f1a00 / 0x1800f1a0c
//        inside ??0Context@Video@@QEAA@PEAVDevice@1@HHHPEAVRenderBuffer@1@@Z
//        (0x1800f1820) — the five-argument ctor this file ports
//   0x1800f1422 / 0x1800f144a / 0x1800f1456 / 0x1800f1462
//        inside ??0Context@Video@@QEAA@PEAVDevice@1@@Z (0x1800f1280, body
//        0x1800f1280..0x1800f16d5) — the ONE-ARGUMENT ctor, which is NOT ported
//
// No other referencer exists, so the attribution is proved rather than guessed,
// and the second caller is new information: the four zero-fills are SHARED by
// both Context constructors, which is why they are registered separately
// instead of being folded into the ported ctor.
//
// The two `operator new`s this file ports — `??2ImageBuffer@Video@@` and
// `??2RenderBuffer@Video@@` — are `Video::ImageBuffer`'s and
// `Video::RenderBuffer`'s members (proved by their mangled names, not by a
// prefix), i.e. block C's and block D2's classes. ENDGAME B.3.5 block VC (the
// fragment pass) MOVED BOTH BODIES onto those members; the two doors keep their
// names and both rows each.
//
// BLOCK VC ALSO CONVERTED EVERY BINDING IN THIS FILE. Nine `RT_EXPORT_ONCE` name
// doors (spelling 3) and five hand-inlined `(*(void***)p)[N]` dispatches
// (spelling 5) are gone: the `*Data` receivers are block A's real abstract
// classes with named slots, and `Video::{Vertex,Index,Render}Buffer::
// PlatformConstruct`, `RenderBuffer::Create` and the two `operator new`s are
// members. `??0Context@Video@@`, the four sub-object zero-fills and
// `??0ImageBuffer@Video@@` remain DOORS for B4b-2's reason, and the last is now
// CALLED as a door by direct near call instead of through a resolver.
// ============================================================================

extern "C" void RtVideo_Context_Dtor(Video::Context* self);    // context_lifecycle.cpp
// `??3Context@Video@@SAXPEAX@Z`'s ported body, also context_lifecycle.cpp. This
// used to be an `RT_EXPORT_ONCE(…, "??3Context@Video@@SAXPEAX@Z")` indirect call
// — binding spelling (c) — on a symbol whose only implementation in the process
// is the one three lines away in this block's own file set. B.3.5 makes it a
// direct call; the registration in context_lifecycle.cpp is untouched, so an
// outside caller that still asks by name or by address gets the same body.
extern "C" void Video_Context_OperatorDelete(void* p);         // context_lifecycle.cpp
// `??0ImageBuffer@Video@@QEAA@XZ`'s ported body — a ctor DOOR, not a constructor
// (B4b-2: it runs on raw `operator new` bytes and stamps the MINTED vtable).
// ENDGAME B.3.5 block VC: this used to be an
// `RT_EXPORT_ONCE(VoidThisFn, "??0ImageBuffer@Video@@QEAA@XZ")` indirect call
// (spelling 3) on a symbol whose only implementation in the process is
// buffers.cpp's. Now a direct call; the registration there is untouched, so an
// outside caller that still asks by name or address gets the same body.
// ENDGAME B.3.9 VS: the door is gone; the two default images below are built by
// the real constructor (`::new (p) Video::ImageBuffer()`).

namespace {

// ENDGAME B.3.5 block VC: `VoidThisFn`, `NewFn`, `RbCreateFn`, `CompanionMk`,
// `BufSetSizeFn` and `ImgSlotFn` are all GONE. B.4 (R-VIDEO): so are the
// file-local `Field<T>` / `At` offset helpers and the `void**` singleton-slot
// accessor; every store below is a named field of a typed receiver, and a
// retail qword store that covers two fields is written as the two named
// fields in address order (H7).

// ---------------------------------------------------------------------------
// The INLINED ConstantBuffer constructor, emitted seven times by MSVC.
//
//   +0x00 = 0        +0x08 = 0        +0x10 = -1 as a QWORD (so +0x10 and
//   +0x14 both become -1)             +0x18 = 0 as a BYTE
//   +0x1c = 0 as an UNALIGNED QWORD (covers +0x1c..+0x23: _registerCount AND
//   _bankStride)
//   +0x28 = 0        +0x30 = 0        +0x38 = the Device
//   +0x40 = device->+0x100 ? deviceData->vtbl[11](deviceData, this) : 0
//
// +0x19..+0x1b and +0x24..+0x27 are deliberately left alone; retail writes
// neither here.
// ---------------------------------------------------------------------------
void InitConstantBuffer(Video::ConstantBuffer* buf, Video::Device* device) {
    buf->_prefix           = nullptr;                   // +0x00
    buf->_typeName         = nullptr;                   // +0x08
    buf->_ordinalIndex     = -1;                        // +0x10 \ one qword -1
    buf->_uploadIndex      = -1;                        // +0x14 /
    buf->_bindToVertex     = 0;                         // +0x18, a BYTE
    buf->_registerCount    = 0;                         // +0x1c \ one unaligned
    buf->_bankStride       = 0;                         // +0x20 / qword zero
    buf->_constantData     = nullptr;                   // +0x28
    buf->_constantCount    = 0;                         // +0x30 \ one qword zero
    buf->_constantCapacity = 0;                         // +0x34 /
    buf->_device           = device;                    // +0x38

    // `vt[11]` (+0x58) is `Video::DeviceData::CreateConstantBufferData`.
    Video::DeviceData* factory = device ? device->deviceData : nullptr;
    buf->_platformData = factory ? factory->CreateConstantBufferData(buf) : nullptr;  // +0x40
}

// One dynamic buffer: `operator new(0x20)`, two field writes, PlatformConstruct,
// store, then an UNGUARDED `p->+0x08->vtbl[1](companion, bytes, true, 0)`.
//
// The unguarded deref is retail's and is kept: when the allocation fails retail
// stores null and dereferences it anyway. Do not add a guard here — the project
// rule is that an unguarded deref in a faithful body means the value is always
// there, and the fix for a null is upstream.
// `vt[1]` is `Video::{Vertex,Index}BufferData::Create(sizeBytes, dynamic,
// data)` — +0x08, with `dynamic` hardcoded true and no initial data, which is
// what retail's `(companion, bytes, 1, 0)` says.
template <class Buf>
void MakeDynamicBuffer(Buf*& slot, int32_t bytes) {
    Buf* p = static_cast<Buf*>(System::MemoryAllocator::MemAlloc(0x20, 4));   // 0x180011640
    if (p) {
        p->size    = 0;
        p->dynamic = false;
        p->Buf::PlatformConstruct();
    }
    slot = p;

    (void)p->platformData->Create(static_cast<uint32_t>(bytes), 1, nullptr);
}

}  // namespace

// ============================================================================
// ??2ImageBuffer@Video@@SAPEAX_K@Z    retail 0x18001e910, ord 801
// ??2RenderBuffer@Video@@SAPEAX_K@Z   retail 0x18001f2e0, ord 861
// ============================================================================
// **ALIGNMENT IS THE WHOLE DIFFERENCE, AND IT WAS CHECKED BY BODY.** These two
// and 0x180011640 are the same nineteen instructions —
// `Enter(alloc+0x640); Allocate(alloc, n, align); Leave; return` — and differ
// only in one immediate: `MOV R8D,0x8` here against `MOV R8D,0x4` in
// 0x180011640. Each was disassembled.
//
// ENDGAME B.3.5 block VC — THE BODIES ARE THE MEMBERS (`??2ImageBuffer@Video@@`,
// `??2RenderBuffer@Video@@` — proved by the mangling). `operator new` and
// `operator delete` are STATIC members and change no triviality trait.
void* Video::ImageBuffer::operator new(size_t size) {
    return System::MemoryAllocator::MemAlloc(size, 8);                           // MOV R8D,0x8
}
void* Video::RenderBuffer::operator new(size_t size) {
    return System::MemoryAllocator::MemAlloc(size, 8);                           // MOV R8D,0x8
}

// ============================================================================
// The four sub-object zero-fills. All four return their argument in RAX.
// Their parameter is the sub-object retail hands them (`LEA RCX,[RBX+off]`);
// both Context constructors call them.
// ============================================================================
extern "C" void* RtVideo_Ctx_ZeroTransformPair(void* p) {      // 0x180053730
    // Two 4x4 float matrices written COLUMN BY COLUMN (retail interleaves the
    // four rows of each), then ten zeroed qwords at +0x80..+0xd0: 0xd0 bytes
    // of zeroes in all, with no hole.
    std::memset(p, 0, 0xd0);
    return p;
}

extern "C" void* RtVideo_Ctx_ZeroBoneMatrices(void* p) {       // 0x180053840
    // `MOV EDX,0x32` with a DEC/JNS loop = 51 iterations, not 50.
    std::memset(p, 0, 51u * 0x40u);
    return p;
}

extern "C" void* RtVideo_Ctx_ZeroTransformMatrices(void* p) {  // 0x1800538b0
    // `MOV EDX,0x33` -> 52. The ONLY difference from the body above.
    std::memset(p, 0, 52u * 0x40u);
    return p;
}

extern "C" void* RtVideo_Ctx_ZeroLightBlock(void* p) {         // 0x180053920
    // Twelve qwords, then every dword from +0x60 to +0x19f. Coverage checked
    // exhaustively against the listing: 0x00..0x19f with no hole.
    std::memset(p, 0, 0x1a0);
    return p;
}

// ============================================================================
// ??0Context@Video@@QEAA@PEAVDevice@1@HHHPEAVRenderBuffer@1@@Z
//                                                   retail 0x1800f1820, ord 125
// ============================================================================
// The zeroing stores are retail's qwords, each written as the named fields it
// covers, in address order (H7).
extern "C" Video::Context* RtVideo_Context_Ctor(Video::Context* self, Video::Device* device,
                                                int32_t vertexBytes, int32_t indexBytes,
                                                int32_t instanceBytes,
                                                Video::RenderBuffer* renderBuffer) {
    self->_device            = device;          // +0x00
    self->_frameIndex        = 0;               // +0x08 \ qword
    self->_shaderDetail      = 0;               // +0x0c /
    self->_dirty             = 0;               // +0x10 \ qword
    self->_unreadDirtyCompanion       = 0;               // +0x14 /
    self->_imageBufferDirty  = 0;               // +0x18 \ qword
    self->_vertexBufferDirty = 0;               // +0x1c /
    self->_transformCount    = 0;               // +0x20 \ qword
    self->_boneCount       = 0;               // +0x24 /
    self->_renderBuffer      = nullptr;         // +0x28
    self->_vertexLayout      = nullptr;         // +0xb0
    self->_indexBuffer       = nullptr;         // +0x1b8
    self->_indexOffset       = 0;               // +0x1c0 (a dword; +0x1c4 is not written)
    self->_renderState       = nullptr;         // +0x1c8
    self->_viewport.x        = 0;               // +0x1d0 \ two qwords
    self->_viewport.y        = 0;               //         |
    self->_viewport.width    = 0;               //         |
    self->_viewport.height   = 0;               //         /
    self->_scissor.x         = 0;               // +0x1e0 \ two qwords
    self->_scissor.y         = 0;               //         |
    self->_scissor.width     = 0;               //         |
    self->_scissor.height    = 0;               //         /
    self->_vertexProgram     = nullptr;         // +0x1f0
    self->_pixelProgram      = nullptr;         // +0x1f8
    // +0x200..+0x297, nineteen qwords: `_videoProgram` and the pixel staging.
    // (void*: the block holds Math::Vector4 rows; the bytes are what is zeroed.)
    std::memset(static_cast<void*>(&self->_pixelConstantStaging), 0, sizeof(self->_pixelConstantStaging));

    InitConstantBuffer(&self->_materialConstants, device);   // mc_
    InitConstantBuffer(&self->_pixelConstants, device);      // pc_

    RtVideo_Ctx_ZeroTransformPair(&self->_viewProjection);             // +0x328
    self->_inverseScale = Math::Vector4();                             // +0x3f8 \ four qwords
    self->_color        = Math::Vector4();                             // +0x408 /
    RtVideo_Ctx_ZeroBoneMatrices(self->_boneAndProjectionStaging);     // 51 x 0x40 -> +0x10d8
    RtVideo_Ctx_ZeroTransformMatrices(self->_transforms);              // 52 x 0x40 -> +0x1dd8
    RtVideo_Ctx_ZeroLightBlock(&self->_lightStaging);                  // 0x1a0     -> +0x1f78

    InitConstantBuffer(&self->_globalConstants, device);     // gc_
    InitConstantBuffer(&self->_instanceConstants, device);   // ic_
    InitConstantBuffer(&self->_boneConstants, device);       // bc_
    InitConstantBuffer(&self->_transformConstants, device);  // tc_
    InitConstantBuffer(&self->_lightConstants, device);      // lc_

    self->_dynamicVertexCursor   = nullptr;     // +0x20e0
    self->_dynamicVertexStride   = 0;           // +0x20e8 (a dword)
    self->_dynamicIndexCursor    = nullptr;     // +0x20f0
    self->_dynamicVertexBuffer   = nullptr;     // +0x20f8
    // B5 CTX: +0x2100 / +0x2120 were one qword store each; the two dword
    // halves are stored separately now (same eight zero bytes).
    self->_dynamicVertexOffset           = 0;           // +0x2100
    self->_dynamicVertexCount           = 0;           // +0x2104
    self->_dynamicVertexBase           = nullptr;     // +0x2108
    self->_instanceCount         = 0;           // +0x2110 (a dword)
    self->_dynamicIndexBuffer    = nullptr;     // +0x2118
    self->_dynamicIndexOffset           = 0;           // +0x2120
    self->_dynamicIndexCount           = 0;           // +0x2124
    self->_dynamicIndexBase           = nullptr;     // +0x2128
    self->_dynamicInstanceBuffer = nullptr;     // +0x2130
    self->_defaultRenderBuffer   = nullptr;     // +0x2138
    self->_defaultColorImage     = nullptr;     // +0x2140
    self->_defaultDepthImage     = nullptr;     // +0x2148
    self->_trianglesSubmitted    = 0;           // +0x2150 \ qword
    self->_verticesSubmitted     = 0;           // +0x2154 /

    // 0x1800881b0 is the CRT memset (device_startup.cpp established this).
    std::memset(self->_imageBuffers,  0, sizeof(self->_imageBuffers));    // +0x30,  0x80
    std::memset(self->_vertexBuffers, 0, sizeof(self->_vertexBuffers));   // +0xb8,  0x80
    std::memset(self->_vertexOffsets, 0, sizeof(self->_vertexOffsets));   // +0x138, 0x40
    std::memset(self->_vertexStrides, 0, sizeof(self->_vertexStrides));   // +0x178, 0x40

    self->Video::Context::AssignConstants();

    // The Context's OWN backend companion, from the Device SINGLETON rather
    // than from the `device` argument — retail reads 0x1801fe7a8 here even
    // though it has a Device in RBP. Kept: the two are the same object in every
    // real call, and reading the argument instead would be a silent
    // normalisation. `vt[9]` (+0x48) is `Video::DeviceData::CreateContextData`.
    {
        Video::Device* singleton = Video::Device::_singleton;
        Video::DeviceData* factory = singleton ? singleton->deviceData : nullptr;
        self->_contextData = factory ? factory->CreateContextData(self) : nullptr;
    }

    if (vertexBytes > 0)
        MakeDynamicBuffer(self->_dynamicVertexBuffer, vertexBytes);
    if (indexBytes > 0)
        MakeDynamicBuffer(self->_dynamicIndexBuffer, indexBytes);
    if (instanceBytes > 0)
        MakeDynamicBuffer(self->_dynamicInstanceBuffer, instanceBytes);

    if (renderBuffer) {
        // Retail JUMPS STRAIGHT TO THE RETURN here: with a render buffer handed
        // in, the two default ImageBuffers are never built and +0x2140/+0x2148
        // stay null. That is why ~Context null-checks them.
        self->_defaultRenderBuffer = renderBuffer;
        return self;
    }

    // ENDGAME B.3.5 block VC: the whole default-render-buffer tail is direct
    // calls; ENDGAME B.3.9 VS: the two default images are built by the real
    // constructor. LEFT (B.4 R-VIDEO): the three class `operator new` results
    // are `void*`, viewed as the object each allocation is for (a C++ `new`
    // expression would assume a non-null result and drop retail's null tests).
    Video::RenderBuffer* rb = static_cast<Video::RenderBuffer*>(   // LEFT: the class operator new returns void*; retail allocates raw and constructs by hand (not the tier-I shape)
        Video::RenderBuffer::operator new(0x30));
    if (rb) {
        rb->colorBuffer = nullptr;   // +0x00
        rb->depthBuffer = nullptr;   // +0x08
        rb->width       = 0;         // +0x10 \ one qword store
        rb->height      = 0;         // +0x14 /
        rb->Video::RenderBuffer::PlatformConstruct();
    }
    self->_defaultRenderBuffer = rb;

    Video::ImageBuffer* color =
        static_cast<Video::ImageBuffer*>(Video::ImageBuffer::operator new(0x68));  // LEFT: class operator new returns void*; retail allocates raw and constructs by hand (not the tier-I shape)
    if (color) ::new (color) Video::ImageBuffer();
    self->_defaultColorImage = color;

    Video::ImageBuffer* depth =
        static_cast<Video::ImageBuffer*>(Video::ImageBuffer::operator new(0x68));  // LEFT: class operator new returns void*; retail allocates raw and constructs by hand (not the tier-I shape)
    if (depth) ::new (depth) Video::ImageBuffer();
    self->_defaultDepthImage = depth;

    // Two UNGUARDED companion calls on the images just built — slot 1 on the
    // colour image, slot 2 on the depth image. Different slots, and swapping them
    // would be silent: `Video::ImageBufferData::AttachColorBuffer` /
    // `AttachDepthBuffer`. Unguarded is retail's: when `operator new` failed,
    // `color`/`depth` is null and retail dereferences it anyway.
    color->_data->AttachColorBuffer();
    depth->_data->AttachDepthBuffer();

    rb->Video::RenderBuffer::Create(color, depth);
    return self;
}

// ============================================================================
// ?CreateVideoContext@MainLoop@Engine@@AEAAXXZ     retail 0x1800dd500, ord 2139
// ============================================================================
// Replace `MainLoop::+0x10`. The three sizes are hard-coded at the call site and
// the render buffer is NULL.
//
// The old Context is destroyed and freed BEFORE the new block is allocated, so
// the allocator may well hand back the same address; nothing here may assume
// otherwise.
// ============================================================================
// ENDGAME B.3.7 (block B1): the body is the member `Engine::MainLoop::
// CreateVideoContext` (`AEAA`, public here because the door is a free
// function); `MainLoop::+0x10` is the named field `_context` (typed
// `Video::Context*` since B.4 R-VIDEO).
void Engine::MainLoop::CreateVideoContext() {
    if (Video::Context* old = _context) {
        RtVideo_Context_Dtor(old);
        Video_Context_OperatorDelete(old);   // was RT_EXPORT_ONCE("??3Context@Video@@SAXPEAX@Z")
        _context = nullptr;
    }

    // Retail inlines `Enter ; Allocate(alloc, 0x2168, 8) ; Leave`, which is
    // MemAlloc(0x2168, 8) — see the banner.
    Video::Context* block = static_cast<Video::Context*>(
        System::MemoryAllocator::MemAlloc(sizeof(Video::Context), 8));
    if (!block) {
        _context = nullptr;
        return;
    }
    _context = RtVideo_Context_Ctor(block, Video::Device::_singleton,
                                    0x800000, 0x400000, 0x100000, nullptr);
}


// ---- ?SetDefaultRenderBuffer@Context@Video@@QEAAXPEAVRenderBuffer@2@@Z  0x1800ef3c0
// B.4 (R-VIDEO): MOVED here from context.cpp (unchanged), beside the ctor that
// first sets the field.
// One store, no compare, no dirty bit. Note this OVERWRITES the pointer the
// ctor may have allocated (+0x2138) without freeing it — retail leaks it
// deliberately, because the caller owns the buffer it hands in. Faithfully
// reproduced; do not add a delete here.
void Video::Context::SetDefaultRenderBuffer(Video::RenderBuffer* rb) {
    _defaultRenderBuffer = rb;
}
