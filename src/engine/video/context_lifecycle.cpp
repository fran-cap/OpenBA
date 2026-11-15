// ============================================================================
// src/engine/video/context_lifecycle.cpp — Wave 9 lane VID2
//   Video::Context::ClearResources, Video::Context::~Context and the five
//   destruction helpers the dtor reaches that nothing else in the tree owned.
// ============================================================================
//
// WHY THIS FILE EXISTS
// --------------------
// `Engine::MainLoop::CreateVideoContext` (context_ctor.cpp) destroys and frees
// the old Context through `RtVideo_Context_Dtor` and
// `Video_Context_OperatorDelete` before it allocates the new one. This file
// ports the DESTRUCTION half of that lifecycle. The construction half (the
// 0x1800f1820 ctor, ~400 instructions building seven sub-objects) is
// `RtVideo_Context_Ctor` in context_ctor.cpp.
//
// `ClearResources` is a pure state transformation on the Context with no
// allocation and no call.
//
// The DESTRUCTORS' correctness rests on transcription from disassembly, which
// is stated here rather than buried.
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "system/MemoryAllocator.h"
#include "video/Buffers.h"
#include "video/Context.h"
#include "video/Device.h"
// ENDGAME B.3.5 block VC — `Video::{Context,ConstantBuffer,ImageBuffer}Data`'s
// slot names, and `Video::ImageBuffer` for the two default images' slot 0.
#include "video/CompanionData.h"
#include "video/ImageBuffer.h"

// ENDGAME B.3.5 block D1. `?ClearResources@` is a member of `Video::Context`.
// THE TWO DESTRUCTORS ARE NOT MEMBERS and that is a rule, not a shortcut:
// `??1Context@Video@@` and `??1ConstantBuffer@Video@@` stay free functions, because
// a user-provided destructor on either class would destroy
// `std::is_trivially_copyable` — `Video::Context` holds seven ConstantBuffers by
// value and nine files treat the 0x2168 bytes as a flat block (RULE B5-4, and
// B4b-2 for the ctor side). Both asserts are in src/engine/video/Context.h
// and ConstantBuffer.h.
using Video::Context;
using ConstantInfo = Video::ConstantBuffer::ConstantInfo;

// The three `Platform*` siblings live in buffers.cpp (Wave 6 lane C / Wave 9
// lanes W and GFX2). Retail's dtor reaches them by direct near CALL; ours reaches
// the C++ MEMBERS — ENDGAME B.3.5 block VC — so no resolver and no free-function
// door sits on the path at all.
//
// (Wave 20 Phase 0c's warning is kept because it is still the reason there is
// exactly one body: this file used to carry its own copy of
// `RenderBuffer::PlatformDestruct`, which shadowed lane C's. The two were
// behaviourally equivalent, so nothing was ever observably wrong; it was one
// edit away from being wrong.) B.4 (R-VIDEO): the two `void**` Device-lock
// declarations this file still carried had no caller here and are gone.

namespace {

// ---------------------------------------------------------------------------
// The scalar-deleting-dtor call every one of these bodies makes.
//
//     MOV RAX,[RCX] / MOV EDX,1 / CALL [RAX]
//
// i.e. vtable slot 0 with flag 1 — the MSVC "deleting destructor". Every
// companion object (`ContextData`, `ImageBuffer`, `ConstantBufferData`) is
// released this way, as a virtual `DeletingDtor(1)` / `delete` on the typed
// field. The deref is UNGUARDED past the null test retail already makes.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// The System::String release the ConstantBuffer dtor and 0x18001cfc0 both
// carry inline, four times between them. Retail, at 0x1800eef14:
//
//     MOV RAX,[RBX+0x8]        ; the string's data pointer
//     TEST RAX,RAX / JZ done
//     DEC dword ptr [RAX-0x4]  ; refcount lives 4 bytes BEFORE the data
//     CMP dword ptr [RAX-0x4],0
//     JNZ .clear
//     ADD qword ptr [RBX+0x8],-0x8   ; back the FIELD up to the block start
//     MOV RCX,[RBX+0x8] / CALL MemFree
//     JMP done
//   .clear:
//     MOV qword ptr [RBX+0x8],0
//
// TWO quirks are deliberate and both are reproduced:
//   * the LAST-REFERENCE branch does NOT null the field. It rewinds it by 8 and
//     frees; the field is left pointing at freed memory. Retail relies on the
//     object being dead.
//   * the SHARED branch (refcount still non-zero) DOES null it.
// Anything that "tidies" either of those changes what the next reader sees.
//
// The fields are `void*` String data pointers (ConstantBuffer.h, H5), so the
// header at `data - 4` / `data - 8` is reached through marked views.
// ---------------------------------------------------------------------------
void ReleaseString(void*& field) {
    uint8_t* data = static_cast<uint8_t*>(field);                  // CAST-OK: string_header
    if (!data) return;
    int32_t* rc = reinterpret_cast<int32_t*>(data - 4);            // CAST-OK: string_header
    *rc -= 1;
    if (*rc != 0) { field = nullptr; return; }
    field = data - 8;
    System::MemoryAllocator::MemFree(field);
}

// The 0x18001cfc0 spelling of the same release differs in ONE respect and it is
// not cosmetic: after rewinding the field by 8 it re-reads it and SKIPS the
// free when the result is null (`ADD [RBX+8],-8 ; MOV RSI,[RBX+8] ; JZ`, the ZF
// coming from the ADD). It also does not null the field in that branch. The
// 0x1800c7ad0 spelling used by ReleaseString has the null test INSIDE MemFree,
// so the two agree on behaviour; they are written apart because the flag
// dependency above is the kind of thing a later reader would "simplify".
void ReleaseStringNoTidy(void*& field) {
    uint8_t* data = static_cast<uint8_t*>(field);                  // CAST-OK: string_header
    if (!data) return;
    int32_t* rc = reinterpret_cast<int32_t*>(data - 4);            // CAST-OK: string_header
    *rc -= 1;
    if (*rc != 0) { field = nullptr; return; }
    field = data - 8;
    if (field) System::MemoryAllocator::MemFree(field);
}

}  // namespace

// ---------------------------------------------------------------------------
// 0x18001cfc0 — the per-element destroy loop for `Array<ConstantInfo>`, whose
// first two qwords are refcounted System::String data pointers. Retail releases
// +0x08 (`typeName`) FIRST and +0x00 (`name`) second; order kept.
//
// Retail's first parameter is the array object. The callee spills it at
// [RSP+8] and NEVER READS IT (0x18001cfc9 is the only mention), so it is not
// passed here. `?BeginConstants@` (0x1800eed10) TAIL-JUMPS to 0x18001cab0 and
// the resize at 0x180053510 calls 0x18001cfc0 — both in constant_buffer.cpp.
// ---------------------------------------------------------------------------
void RtVideo_ConstEntryArrayDestroy(ConstantInfo* data, int32_t count) {
    if (count <= 0) return;
    ConstantInfo* e = data;
    for (int64_t i = count; i != 0; --i) {
        ReleaseStringNoTidy(e->typeName);
        ReleaseStringNoTidy(e->name);
        ++e;
    }
}

// ---------------------------------------------------------------------------
// 0x18001cab0 — Collection::Array<ConstantInfo>::~Array.
//   destroy the elements, free the block, then zero {_data} and {_size,_capacity}
//   as two qword stores (0x18001cb18 / 0x18001cb1b) — the second written as the
//   two named fields.
// Retail's `XOR EAX,EAX` at 0x18001cb16 supplies BOTH stores and the return
// value, so the function returns 0, not the array. No caller reads it, so this
// is written `void`.
// ---------------------------------------------------------------------------
void RtVideo_ConstEntryArrayDtor(ConstantInfo*& data, int32_t& size, int32_t& capacity) {
    ConstantInfo* d = data;
    RtVideo_ConstEntryArrayDestroy(d, size);
    if (d) System::MemoryAllocator::MemFree(d);
    data     = nullptr;   // _data
    size     = 0;         // _size \ one qword store
    capacity = 0;         // _capacity /
}

// ============================================================================
// ??1ConstantBuffer@Video@@QEAA@XZ — retail 0x1800eeee0, ordinal 558
// ============================================================================
// The seven `Video::ConstantBuffer` members embedded in a Context are destroyed
// through this. Layout touched (src/engine/video/ConstantBuffer.h):
//   _prefix (+0x00), _typeName (+0x08)   refcounted System::String data pointers
//   _constantData.. (+0x28)              Array<ConstantInfo>, stride 0x18
//   _platformData (+0x40)                the ConstantBufferData companion
//                                        (deleting dtor, slot 0)
// ORDER: companion, then the array, then _typeName, then _prefix. Kept.
//
// STILL A FREE FUNCTION, NOT `~ConstantBuffer()` (ENDGAME B.3.5 block D1). A real
// destructor here would make `Video::ConstantBuffer` non-trivially-copyable and
// `Video::Context` — which holds seven by value — with it; RULE B5-4 and the
// `static_assert`s in both headers forbid exactly that.
// ============================================================================
void Video_ConstantBuffer_Dtor(Video::ConstantBuffer* self) {
    // B5 BUF (B5_RULES D10): retail's slot-0 call with flags 1 is `delete` -- the
    // virtual destructor, then the base's class-scope operator delete (MemFree).
    if (Video::ConstantBufferData* companion = self->_platformData)
        delete companion;

    RtVideo_ConstEntryArrayDtor(self->_constantData, self->_constantCount,
                                self->_constantCapacity);

    ReleaseString(self->_typeName);
    ReleaseString(self->_prefix);
}

// ============================================================================
// ??3RenderBuffer@Video@@SAXPEAX@Z — retail 0x18001f360, ordinal 995
// ============================================================================
// A class `operator delete`: `if (p) { Enter(alloc CS); Free(_singleton, p);
// Leave; }`. Byte-for-byte the same body as 0x180018500 (Video::Context's own
// operator delete) and 0x180006a10. Our 0x1800c7ad0 MemFree is that composition,
// with the null test inside it, which is why the pointer is handed over
// unguarded exactly as retail's `TEST RCX,RCX; JZ ret` allows.
// ============================================================================
void Video::RenderBuffer::operator delete(void* p) {
    System::MemoryAllocator::MemFree(p);
}

// `??3Context@Video@@SAXPEAX@Z` (0x180018500) stays a FREE FUNCTION and not
// `Video::Context::operator delete`: `Context.h` declares none of the eight
// lifecycle spellings, on purpose. It is a distinct function in the image from
// the one above.
extern "C" void Video_Context_OperatorDelete(void* p) { System::MemoryAllocator::MemFree(p); }

// ============================================================================
// 0x180006a10 — the tree's shared guarded object free (UNEXPORTED; not a
// `Video::` member). `~Context` frees its three buffer sub-objects through it,
// which is retail — retail's ~Context calls 0x180006a10 and NOT 0x180018500 for
// them — and Save / Input / Engine call it by name.
// ============================================================================
extern "C" void Rt_GuardedObjectFree(void* p) { System::MemoryAllocator::MemFree(p); }

// ============================================================================
// ?ClearResources@Context@Video@@QEAAXXZ — retail 0x1800f2090, ordinal 1712
// ============================================================================
// Two sixteen-iteration sweeps that drop every bound texture and every bound
// vertex stream, re-dirtying only the slots that actually held something.
//
//   0x1800f209f  EAX = 1                      ; the rotating image-slot bit
//   0x1800f20a7  for slot i in 0..15:
//                  if (_imageBuffers[i]) {
//                      _dirty           |= 0x2   (kDirtyImageBuffers)
//                      _imageBufferDirty|= bit
//                      _imageBuffers[i]  = nullptr
//                  }
//                  ROL EAX,1
//   0x1800f20d0  for slot i in 0..15:
//                  if (_vertexBuffers[i]) {
//                      _dirty            |= 0x8  (kDirtyVertexBuffers)
//                      _vertexBufferDirty|= bit
//                      _vertexBuffers[i]  = nullptr
//                      _vertexOffsets[i]  = 0     ; [R8-0x40], R8 walking +0x178
//                      _vertexStrides[i]  = 0     ; [R8]
//                  }
//                  ROL EDX,1
//
// The two rotating masks are SEPARATE REGISTERS (EAX and EDX) seeded from the
// same 1 at 0x1800f2090/0x1800f209f, and EDX survives the first loop untouched.
// This function touches NOTHING ELSE.
// ============================================================================
void Video::Context::ClearResources() {

    uint32_t imageBit = 1;
    for (int i = 0; i < RtVideo::kImageBufferSlots; ++i) {
        if (_imageBuffers[i] != nullptr) {
            _dirty            |= RtVideo::kDirtyImageBuffers;
            _imageBufferDirty |= imageBit;
            _imageBuffers[i]   = nullptr;
        }
        imageBit = (imageBit << 1) | (imageBit >> 31);   // ROL EAX,1
    }

    uint32_t vertexBit = 1;
    for (int i = 0; i < RtVideo::kVertexBufferSlots; ++i) {
        if (_vertexBuffers[i] != nullptr) {
            _dirty             |= RtVideo::kDirtyVertexBuffers;
            _vertexBufferDirty |= vertexBit;
            _vertexBuffers[i]   = nullptr;
            _vertexOffsets[i]   = 0;
            _vertexStrides[i]   = 0;
        }
        vertexBit = (vertexBit << 1) | (vertexBit >> 31);   // ROL EDX,1
    }

}

// ============================================================================
// ??1Context@Video@@QEAA@XZ — retail 0x1800f16e0, ordinal 562
// ============================================================================
// Seven sub-objects and seven embedded constant buffers, in retail's order.
// The order is not arbitrary and is not normalised: the backend companion at
// +0x2160 goes FIRST, so that every buffer released afterwards is released
// against a backend that has already torn its own state down.
//
//   +0x2160 _contextData            deleting dtor (slot 0)
//   +0x20f8 _dynamicVertexBuffer    VertexBuffer::PlatformDestruct, then delete
//   +0x2118 _dynamicIndexBuffer     IndexBuffer::PlatformDestruct,  then delete
//   +0x2140 _defaultColorImage      deleting dtor (slot 0)
//   +0x2148 _defaultDepthImage      deleting dtor (slot 0)
//   +0x2138 _defaultRenderBuffer    RenderBuffer::PlatformDestruct, then ??3RenderBuffer
//   +0x2130 _dynamicInstanceBuffer  VertexBuffer::PlatformDestruct, then delete
//   +0x2098, +0x2050, +0x2008, +0x1fc0, +0x1f78, +0x2e0, +0x298
//                                   ~ConstantBuffer, in THAT order (reverse
//                                   declaration order, as MSVC emits)
//
// NOTE WHAT IS ABSENT. The dtor does NOT null any of the seven pointers after
// releasing them, and it does not touch the bindings ClearResources clears.
//
// The three buffer sub-objects are freed through 0x180006a10, NOT through the
// same `operator delete` the Context itself uses (0x180018500).
// ============================================================================
extern "C" void RtVideo_Context_Dtor(Context* self) {
    if (self->_contextData)
        delete self->_contextData;   // B5 CTX (C1): retail's slot-0 call with flags 1

    if (Video::VertexBuffer* p = self->_dynamicVertexBuffer) {
        p->Video::VertexBuffer::PlatformDestruct();
        Rt_GuardedObjectFree(p);
    }
    if (Video::IndexBuffer* p = self->_dynamicIndexBuffer) {
        p->Video::IndexBuffer::PlatformDestruct();
        Rt_GuardedObjectFree(p);
    }
    if (Video::ImageBuffer* p = self->_defaultColorImage)
        delete p;
    if (Video::ImageBuffer* p = self->_defaultDepthImage)
        delete p;
    if (Video::RenderBuffer* p = self->_defaultRenderBuffer) {
        p->Video::RenderBuffer::PlatformDestruct();
        Video::RenderBuffer::operator delete(p);
    }
    if (Video::VertexBuffer* p = self->_dynamicInstanceBuffer) {
        p->Video::VertexBuffer::PlatformDestruct();
        Rt_GuardedObjectFree(p);
    }

    Video_ConstantBuffer_Dtor(&self->_lightConstants);
    Video_ConstantBuffer_Dtor(&self->_transformConstants);
    Video_ConstantBuffer_Dtor(&self->_boneConstants);
    Video_ConstantBuffer_Dtor(&self->_instanceConstants);
    Video_ConstantBuffer_Dtor(&self->_globalConstants);
    Video_ConstantBuffer_Dtor(&self->_pixelConstants);
    Video_ConstantBuffer_Dtor(&self->_materialConstants);   // retail tail-jumps
}

// ============================================================================
// B.4 (R-VIDEO): the ClearResources probe helpers that followed here
// (`ClearSnapshot`, `Capture`, `SameSnapshot`, `BuildCase`) had no caller —
// -Wunused-function said so — and are deleted with their casts.
// ============================================================================
