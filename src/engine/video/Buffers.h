#pragma once
// ============================================================================
// src/engine/video/Buffers.h — Video::IndexBuffer / Video::VertexBuffer,
// promoted to real C++ classes by ENDGAME B.3.5 block D2.
// ============================================================================
//
// Runtime-x64.dll classes, 0x20 bytes each, identical layout. NON-POLYMORPHIC:
// neither has a `??_7` export or a `?_typeInfo@` row (B35_RULES §2.1), and
// neither carries a vtable of its own — Create/Set/Bind/Lock/Unlock are thin
// dispatches through the platform buffer object at +0x08 (dx11's
// IndexBufferData / VertexBufferData, see src/render/dx11/VideoDX11Data.h and the
// abstract bases in src/engine/video/CompanionData.h).
//
// ---------------------------------------------------------------------------
// B.3.5 BLOCK D2 — B4b-1 AND THE NAMESPACE
// ---------------------------------------------------------------------------
// This header used to declare `struct IndexBuffer` / `struct VertexBuffer` at
// GLOBAL SCOPE with no namespace at all (B35_FILEMAP §3d's fourth spelling,
// "none"). The retail names are `Video::IndexBuffer` / `Video::VertexBuffer`, so
// B4b-1 applies: the real classes take the retail names in `namespace Video`,
// and the old GLOBAL spelling becomes a type ALIAS of each. The two were
// field-identical, so there is ONE definition and no duplicate layout — the
// move the B.3.5 brief prefers over a second copy. `buffers.cpp`,
// `companion_data.cpp`, `context_lifecycle.cpp` and `dynamic_resources.cpp`
// spell these types unqualified today and keep compiling unchanged.
//
// NO REAL CONSTRUCTOR (the destructor IS real since B.4 wave Y: retail's `??1`
// is one `JMP PlatformDestruct`, declared below), and the reason is the intrusive list:
// `PlatformConstruct`/`PlatformDestruct` (0x1800f3680/0x1800f3760 for
// IndexBuffer, 0x1800f57e0/0x1800f58c0 for VertexBuffer) link and unlink `this`
// in a process-global circular doubly linked list of every live buffer of that
// type, rooted at `?_firstBuffer@IndexBuffer@Video@@` (0x1801fe788) /
// `?_firstBuffer@VertexBuffer@Video@@` (0x1801fe798) and guarded by
// `Video::Device::_singleton`'s critical section at +0x60 (RULE B5-6). The two
// heads are the class statics `_firstBuffer` (defined in dynamic_resources.cpp),
// and the ctors stay the free functions `Video_IndexBuffer_Ctor` /
// `Video_VertexBuffer_Ctor` in `src/engine/video/buffers.cpp`, which publish
// `this` into the list through PlatformConstruct.
//
// Every exported member of both classes is declared below whether or not it is
// ported; `NOT PORTED` means declared here and defined nowhere, so a caller gets
// a link error rather than a silent null call.
//
// Decompiled from (Wave 6 lane C):
//   IndexBuffer ctor:   0x1800f3490 (??0IndexBuffer@Video@@QEAA@XZ)
//   VertexBuffer ctor:  0x1800f55f0 (??0VertexBuffer@Video@@QEAA@XZ)
//   IndexBuffer::Create:  0x1800f3880 (?Create@IndexBuffer@Video@@QEAA_NH_NPEAX@Z)
//   VertexBuffer::Create: 0x1800f59e0 (?Create@VertexBuffer@Video@@QEAA_NH_NPEAX@Z)
//   IndexBuffer::Set:     0x1800f38a0 (?Set@IndexBuffer@Video@@QEAA_NHHPEAX@Z)
//   VertexBuffer::Set:    0x1800f5a00 (?Set@VertexBuffer@Video@@QEAA_NHHPEAX@Z)
//   IndexBuffer::GetSize (used only to confirm +0x00):  0x180011e00
//   VertexBuffer::GetSize (same confirmation):          0x180011db0
// ============================================================================

#include <cstdint>
#include <cstddef>
#include <type_traits>

namespace IO { class Serializer; }

namespace Video {

class Context;             // src/engine/video/Context.h       (b35-D1)
class IndexBufferData;     // src/engine/video/CompanionData.h (b35-A)
class VertexBufferData;    // src/engine/video/CompanionData.h (b35-A)

// ----------------------------------------------------------------------------
// Video::IndexBuffer — 0x20 bytes.
//
// LIFECYCLE EXPORTS — DOORS, NOT MEMBERS (see the header banner):
//   ??0IndexBuffer@Video@@QEAA@XZ                0x1800f3490  Video_IndexBuffer_Ctor
//   ??1IndexBuffer@Video@@QEAA@XZ                0x1800f3480  ~IndexBuffer (member)
//   ??4IndexBuffer@Video@@QEAAAEAV01@AEBV01@@Z   0x180011e10  UNPORTED (operator=)
// ----------------------------------------------------------------------------
class IndexBuffer {
public:
    // ??1IndexBuffer@Video@@QEAA@XZ  0x1800f3480  `JMP PlatformDestruct` (buffers.cpp)
    ~IndexBuffer();
    // ?Create@IndexBuffer@Video@@QEAA_NH_NPEAX@Z  0x1800f3880  buffers.cpp
    //   A trampoline into IndexBufferData slot 1 (+0x08).
    bool Create(int count, bool dynamic, void* data);
    // ?Set@IndexBuffer@Video@@QEAA_NHHPEAX@Z      0x1800f38a0  buffers.cpp
    //   A trampoline into IndexBufferData slot 2 (+0x10).
    //
    // THE TWO INTS ARE (sizeBytes, offsetBytes), NOT (offset, count). This header
    // named them `(offset, count)` until ENDGAME B.3.5's closing pass while every
    // game-side caller passed `(byteSize, byteOffset)` positionally. THE CALLERS
    // WERE RIGHT; the names here were wrong, and only the names changed -- no call
    // site, no argument order and no byte moved.
    //
    // Settled from the retail bytes, two independent ways:
    //  1. The TRAMPOLINE. `0x1800f38a0` disassembles to exactly three
    //     instructions -- `MOV RCX,[RCX+0x8] ; MOV RAX,[RCX] ; JMP [RAX+0x10]`
    //     (and `0x1800f5a00`, VertexBuffer::Set, is byte-for-byte the same shape).
    //     RDX, R8 and R9 are never touched, so argument 1 stays argument 1: this
    //     function's parameters ARE slot 2's parameters.
    //  2. THE CONSUMER. Slot 2 of `{Index,Vertex}BufferData` is `SetData`, whose
    //     parameters `CompanionData.h` already spells `(int32_t sizeBytes,
    //     int32_t offsetBytes, const void* src)` -- and dx11's port of the retail
    //     body (`GpuBuffer_SetData`, src/render/dx11/buffers.cpp) proves the reading from
    //     the D3D11 call it makes: `box.left = offsetBytes`,
    //     `box.right = sizeBytes + offsetBytes`, and `srcRowPitch = sizeBytes`.
    //     Swap the two and `box.left` would be a byte COUNT and the row pitch an
    //     OFFSET, which no caller could survive.
    // So a caller that believed the header would have uploaded to the wrong place
    // with the wrong length. Nothing did, because every caller was written against
    // the door's real behaviour.
    bool Set(int sizeBytes, int offsetBytes, void* data);
    // ?Lock@IndexBuffer@Video@@QEAAPEAXHAEAH@Z    0x1800f34e0  companion_data.cpp
    //   (IndexBufferData slot 3, +0x18). ENDGAME B.3.5 block VC: the body had been
    //   in block A's companion_data.cpp since Wave 6 lane C while this header said
    //   NOT PORTED; neither block owned the edge, so it is moved here now.
    void* Lock(int sizeBytes, int& outByteOffset);
    // ?Unlock@IndexBuffer@Video@@QEAAXXZ          0x1800f34c0  companion_data.cpp
    //   (IndexBufferData slot 4, +0x20). Block VC, as above.
    void  Unlock();
    // ?Bind@IndexBuffer@Video@@QEBAXAEAVContext@2@@Z 0x1800f3860  NOT PORTED (slot 5)
    void  Bind(Context& context) const;
    // ?Serialize@IndexBuffer@Video@@QEAAXAEAVSerializer@IO@@@Z 0x1800f38c0 NOT PORTED (slot 6)
    void  Serialize(IO::Serializer& serializer);
    // ?GetSize@IndexBuffer@Video@@QEBAHXZ         0x180011e00  NOT PORTED
    int   GetSize() const;

    // ---- the two class-static list sweeps (under the Device lock) -----------
    // ?RecreateDynamic@IndexBuffer@Video@@SAXXZ   0x1800f3500  dynamic_resources.cpp
    static void RecreateDynamic();
    // ?ReleaseDynamic@IndexBuffer@Video@@SAXXZ    0x1800f35d0  dynamic_resources.cpp
    static void ReleaseDynamic();
    // ?_firstBuffer@IndexBuffer@Video@@0PEAV12@EA (0x1801fe788) — the list head
    // (ENDGAME B.3.9 VID class static, B39_RULES D7; dynamic_resources.cpp).
    static IndexBuffer* _firstBuffer;

    // ---- the platform half; members in buffers.cpp -------------------------
    // NOTE both are PUBLIC in retail (`QEAA`), unlike ImageBuffer's and
    // RenderBuffer's private (`AEAA`) twins.
    // ?PlatformConstruct@IndexBuffer@Video@@QEAAXXZ 0x1800f3680  buffers.cpp
    void PlatformConstruct();
    // ?PlatformDestruct@IndexBuffer@Video@@QEAAXXZ  0x1800f3760  buffers.cpp
    void PlatformDestruct();

    // ---- data ---------------------------------------------------------------
    int32_t  size;          // +0x00  index count (Create's first param); zeroed in ctor
    bool     dynamic;       // +0x04  zeroed in ctor
    uint8_t  pad05[3];
    // B.4 (R-VIDEO), H4: the pointer members carry their retail types.
    IndexBufferData* platformData;  // +0x08  set by PlatformConstruct
    IndexBuffer*     listPrev;      // +0x10  intrusive list (Device's live-buffer registry); not touched by Create/Set
    IndexBuffer*     listNext;      // +0x18  the field the four retail walkers step through
};

// ----------------------------------------------------------------------------
// Video::VertexBuffer — layout-identical to IndexBuffer (confirmed: both ctors
// write the same byte pattern, both Create/Set dispatch through the same
// +0x08/vtable+0x08 and +0x08/vtable+0x10 shape). The ONE slot where the two
// companion shapes differ is `Bind`, which takes four arguments here.
//
// LIFECYCLE EXPORTS — DOORS, NOT MEMBERS:
//   ??0VertexBuffer@Video@@QEAA@XZ               0x1800f55f0  Video_VertexBuffer_Ctor
//   ??1VertexBuffer@Video@@QEAA@XZ               0x1800f55e0  ~VertexBuffer (member)
//   ??4VertexBuffer@Video@@QEAAAEAV01@AEBV01@@Z  0x180011dd0  UNPORTED (operator=)
// ----------------------------------------------------------------------------
class VertexBuffer {
public:
    // ??1VertexBuffer@Video@@QEAA@XZ  0x1800f55e0  `JMP PlatformDestruct` (buffers.cpp)
    ~VertexBuffer();
    // ?Create@VertexBuffer@Video@@QEAA_NH_NPEAX@Z  0x1800f59e0  buffers.cpp
    bool Create(int count, bool dynamic, void* data);
    // ?Set@VertexBuffer@Video@@QEAA_NHHPEAX@Z      0x1800f5a00  buffers.cpp
    //   A trampoline into VertexBufferData slot 2 (+0x10). Same three-instruction
    //   shape and the same `(sizeBytes, offsetBytes)` reading as IndexBuffer::Set
    //   above -- see that member's note for the retail evidence.
    bool Set(int sizeBytes, int offsetBytes, void* data);
    // ?Lock@VertexBuffer@Video@@QEAAPEAXHAEAH@Z    0x1800f5640  companion_data.cpp
    //   (VertexBufferData slot 3, +0x18). Block VC, as above.
    void* Lock(int sizeBytes, int& outByteOffset);
    // ?Unlock@VertexBuffer@Video@@QEAAXXZ          0x1800f5620  companion_data.cpp
    //   (VertexBufferData slot 4, +0x20). Block VC, as above.
    void  Unlock();
    // ?Bind@VertexBuffer@Video@@QEBAXAEAVContext@2@HII@Z 0x1800f59c0 NOT PORTED (slot 5)
    void  Bind(Context& context, int slot, unsigned offsetBytes, unsigned strideBytes) const;
    // ?Serialize@VertexBuffer@Video@@QEAAXAEAVSerializer@IO@@@Z 0x1800f5a20 NOT PORTED (slot 6)
    void  Serialize(IO::Serializer& serializer);
    // ?GetSize@VertexBuffer@Video@@QEBAHXZ         0x180011db0  NOT PORTED
    int   GetSize() const;
    // ?GetData@VertexBuffer@Video@@QEBAPEBVVertexBufferData@2@XZ 0x180011dc0  NOT PORTED
    //   (IndexBuffer has no exported twin of this one.)
    const VertexBufferData* GetData() const;

    // ?RecreateDynamic@VertexBuffer@Video@@SAXXZ   0x1800f5660  dynamic_resources.cpp
    static void RecreateDynamic();
    // ?ReleaseDynamic@VertexBuffer@Video@@SAXXZ    0x1800f5730  dynamic_resources.cpp
    static void ReleaseDynamic();
    // ?_firstBuffer@VertexBuffer@Video@@0PEAV12@EA (0x1801fe798) — the list head
    // (ENDGAME B.3.9 VID class static, B39_RULES D7; dynamic_resources.cpp).
    static VertexBuffer* _firstBuffer;

    // ?PlatformConstruct@VertexBuffer@Video@@QEAAXXZ 0x1800f57e0  buffers.cpp
    void PlatformConstruct();
    // ?PlatformDestruct@VertexBuffer@Video@@QEAAXXZ  0x1800f58c0  buffers.cpp
    void PlatformDestruct();

    // ---- data ---------------------------------------------------------------
    int32_t  size;
    bool     dynamic;
    uint8_t  pad05[3];
    VertexBufferData* platformData;  // +0x08
    VertexBuffer*     listPrev;      // +0x10
    VertexBuffer*     listNext;      // +0x18
};

}  // namespace Video

// ---- layout assertions, after the classes ----------------------------------
static_assert(sizeof(Video::IndexBuffer) == 0x20, "IndexBuffer must be 0x20 bytes");
static_assert(offsetof(Video::IndexBuffer, pad05) == 0x5, "IndexBuffer::pad05");
static_assert(alignof(Video::IndexBuffer) == 8, "IndexBuffer aligns to 8");
static_assert(std::is_standard_layout<Video::IndexBuffer>::value, "IndexBuffer stays standard-layout");
static_assert(!std::is_trivially_destructible<Video::IndexBuffer>::value,
              "IndexBuffer's destructor is retail's ??1 (PlatformDestruct); its ctor stays a door");
static_assert(offsetof(Video::IndexBuffer, size) == 0x00, "size at +0x00");
static_assert(offsetof(Video::IndexBuffer, dynamic) == 0x04, "dynamic at +0x04");
static_assert(offsetof(Video::IndexBuffer, platformData) == 0x08, "platformData at +0x08");
static_assert(offsetof(Video::IndexBuffer, listPrev) == 0x10, "listPrev at +0x10");
static_assert(offsetof(Video::IndexBuffer, listNext) == 0x18, "listNext at +0x18");

static_assert(sizeof(Video::VertexBuffer) == 0x20, "VertexBuffer must be 0x20 bytes");
static_assert(offsetof(Video::VertexBuffer, pad05) == 0x5, "VertexBuffer::pad05");
static_assert(alignof(Video::VertexBuffer) == 8, "VertexBuffer aligns to 8");
static_assert(std::is_standard_layout<Video::VertexBuffer>::value, "VertexBuffer stays standard-layout");
static_assert(!std::is_trivially_destructible<Video::VertexBuffer>::value,
              "VertexBuffer's destructor is retail's ??1 (PlatformDestruct); its ctor stays a door");
static_assert(offsetof(Video::VertexBuffer, size) == 0x00, "size at +0x00");
static_assert(offsetof(Video::VertexBuffer, dynamic) == 0x04, "dynamic at +0x04");
static_assert(offsetof(Video::VertexBuffer, platformData) == 0x08, "platformData at +0x08");
static_assert(offsetof(Video::VertexBuffer, listPrev) == 0x10, "listPrev at +0x10");
static_assert(offsetof(Video::VertexBuffer, listNext) == 0x18, "listNext at +0x18");

// ============================================================================
// The old GLOBAL-SCOPE spelling, now ALIASES of the real classes (B4b-1 —
// see the header banner). ONE definition, two names.
// ============================================================================
using IndexBuffer  = ::Video::IndexBuffer;
using VertexBuffer = ::Video::VertexBuffer;

namespace RtVideo {
// `RtVideo::` for symmetry with Device.h, so a caller batch can spell either.
using IndexBuffer  = ::Video::IndexBuffer;
using VertexBuffer = ::Video::VertexBuffer;
}  // namespace RtVideo

// ==== B3R W0-P0c: Runtime bodies, called by name ====
// Each body below is declared here, copied from its definition (linkage,
// namespace, types), so its callers can call it by name. The retail address
// is in its trailing comment.

// ---- video/buffers.cpp
void Video_IndexBuffer_Ctor(Video::IndexBuffer* self);  // 0x1800f3490
void Video_VertexBuffer_Ctor(Video::VertexBuffer* self);  // 0x1800f55f0
// ==== end B3R W0-P0c ====
