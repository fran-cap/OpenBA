// ============================================================================
// src/engine/collection/array_graphics_mesh.cpp
// The four Collection::Array<T> instantiations underneath Graphics::GraphicsMesh
// (vtable batch 6, 2026-09-12), plus the two element constructors one of them
// is parameterized by.
// ============================================================================
//
//   0x180013fb0  ~Array<GraphicsMesh::TriangleList>      stride 0xb0
//   0x180014910    its per-element destroy
//   0x180014160  ~Array<Collection::Array<T>>            stride 0x10
//   0x180014af0    its per-element destroy
//   0x18005d840   Array<GraphicsMesh::TriangleList>::SetSize(n)
//   0x18005cda0   Array<Collection::Array<T>>::SetSize(n)
//   0x1800fefc0   GraphicsMesh::TriangleList::TriangleList()
//   0x180013c50   GraphicsMesh::TriangleList::TriangleList(const TriangleList&)
//
// WHY A NEW FILE RATHER THAN ROWS IN array_destroy.cpp / array_graphics_anim.cpp
// -----------------------------------------------------------------------------
// Those two are APPEND-ONLY shared files that several agents are editing in the
// same session, and array_destroy.cpp's table is POSITIONAL (its banner records
// a Wave 27 merge that silently gave two lanes the same indices). A new file
// with no table of its own cannot collide. Nothing here is a second copy of a
// Collection algorithm: `SetSizeCore` and `MemAlloc` are array.cpp's bodies and
// `ArrayDtorImpl` and `MemFree` are header-inline in Collection.h, all reached
// through array_internal.h, exactly as array_graphics_anim.cpp reaches them.
//
// GREPPED BEFORE WRITING: all eight addresses were searched across code/,
// engine/, runtime/, standalone/ and src/render/dx11/. None is registered anywhere, and
// the only hits are the comments this batch's own graphics files carry.
//
// ---------------------------------------------------------------------------
// GraphicsMesh::TriangleList — 0xb0 bytes
// ---------------------------------------------------------------------------
// B.4 (X-RL): this element is `Graphics::GraphicsMesh::DetailLevel`
// (GraphicsMesh.h, every offset below asserted there); "TriangleList" is this
// file's old misnomer for it, kept in the function names only.
// Read from the three bodies that cover it end to end (the default ctor's
// stores, the copy ctor's moves, and the element destroy's releases); no field
// is named from a guess.
//
//   +0x00  System::String            refcounted; released by the destroy
//   +0x08  (one qword)               copied; the ctor zeroes it
//   +0x10  float = 10000.0f          0x461c4000, the same constant GraphicsMesh
//                                    itself puts at its +0x48
//   +0x18  Video::VertexBuffer  0x20   PlatformConstruct / PlatformDestruct
//   +0x38  Video::IndexBuffer   0x20   ditto
//   +0x58  Video::VertexBuffer  0x20   ditto
//   +0x78  Video::IndexBuffer   0x20   ditto
//   +0x98  Collection::Array<T>      the index/triangle array (16-byte header)
//   +0xa8  int32 = 1
//
// Each of the four buffer sub-objects gets `dword = 0; byte[+4] = 0` written
// before its PlatformConstruct, and the platform companion each one builds
// lands at ITS +0x08 — which is where `GraphicsMesh::TriangleList::Serialize`
// (graphicsmesh_vtable.cpp §T) reads +0x20/+0x40/+0x60/+0x80 from.
// ============================================================================

#include <cstdint>
#include <cstring>

#include "collection/Collection.h"
#include "video/Buffers.h"   // ENDGAME B.3.5: Video::{Vertex,Index}Buffer
#include "array_internal.h"
#include "array_destroy_internal.h"
#include "graphics/GraphicsMesh.h"   // B3R RT-VG: the Rt_* declarations

namespace rtcoll {
namespace gfxmesh {

constexpr uint32_t kTriStride   = 0xb0;
constexpr uint32_t kInnerStride = 0x10;

// B.4 (X-RL): the 0xb0 element this file calls "TriangleList" IS
// `Graphics::GraphicsMesh::DetailLevel` (GraphicsMesh.h, every field asserted,
// sizeof 0xb0 == kTriStride). The function names below keep the old misnomer so
// the addresses stay findable; the fields are read by their names.
using DetailLevel = Graphics::GraphicsMesh::DetailLevel;
static_assert(sizeof(DetailLevel) == kTriStride, "DetailLevel is the 0xb0 element");

// The four Video platform hooks. All four are members of the real
// `Video::VertexBuffer` / `Video::IndexBuffer` (B.3.5), so these four wrappers
// call them directly. They are the buffer's OWN members, not the element
// ctor/dtor helpers B.3.2 left alone: the sub-object at +0x18/+0x38/+0x58/+0x78
// IS a Video buffer, which is why the conversion applies here.
void VertexBufferPlatformConstruct(Video::VertexBuffer* p) {
    p->PlatformConstruct();
}
void IndexBufferPlatformConstruct(Video::IndexBuffer* p) {
    p->PlatformConstruct();
}
void VertexBufferPlatformDestruct(Video::VertexBuffer* p) {
    p->PlatformDestruct();
}
void IndexBufferPlatformDestruct(Video::IndexBuffer* p) {
    p->PlatformDestruct();
}

// `Collection::Array<T>::operator=` for the 0x98 index array — retail
// 0x180013640, the copy constructor's only call, ported in array_destroy.cpp as
// RtColl_ArrayAssign_180013640 and called by name. Named here so the next
// reader can find it rather than meeting a bare hex number.
void InnerIndexArrayAssign(void* dst, const void* src) {
    rtcoll::RtColl_ArrayAssign_180013640(dst, src);
}

// The same job one level up, for `Array<Array<T>>`'s element assign — retail
// 0x180014c70, ported in array_destroy.cpp as RtColl_ArrayAssign_180014c70.
void InnerArrayAssign(void* dst, const void* src) {
    rtcoll::RtColl_ArrayAssign_180014c70(dst, src);
}

// ---------------------------------------------------------------------------
// 0x1800fefc0 — GraphicsMesh::TriangleList::TriangleList()
// ---------------------------------------------------------------------------
// Transcribed store for store from 0x1800fefdd .. 0x1800ff039.
//
// *** THE `dword = 0; byte[+4] = 0` PAIR BEFORE EACH PlatformConstruct IS FIVE
// BYTES, NOT EIGHT. *** Retail writes `MOV dword [RCX],EDI` then
// `MOV byte [RCX+4],DIL` and leaves +0x05..+0x07 of each buffer untouched.
// Zeroing the whole qword would be unfaithful and would make any byte-wise
// oracle over those three bytes fail on every call
// (reference_oracle_must_not_compare_unwritten_bytes.md).
//
// Returns `this` (`MOV RAX,RBX`).
DetailLevel* TriangleListCtor(DetailLevel* d) {
    d->_name._data = nullptr;                            // the String slot
    d->_image._ptr = nullptr;
    d->_distance   = 10000.0f;                           // 0x461c4000

    // Retail's order: +0x18 Vertex, +0x38 Index, +0x58 Vertex, +0x78 Index.
    d->_vertexBuffer.size        = 0;
    d->_vertexBuffer.dynamic     = false;
    VertexBufferPlatformConstruct(&d->_vertexBuffer);
    d->_indexBuffer.size         = 0;
    d->_indexBuffer.dynamic      = false;
    IndexBufferPlatformConstruct(&d->_indexBuffer);
    d->_skinVertexBuffer.size    = 0;
    d->_skinVertexBuffer.dynamic = false;
    VertexBufferPlatformConstruct(&d->_skinVertexBuffer);
    d->_skinIndexBuffer.size     = 0;
    d->_skinIndexBuffer.dynamic  = false;
    IndexBufferPlatformConstruct(&d->_skinIndexBuffer);

    d->_triangleLists._data     = nullptr;               // the Array header
    d->_triangleLists._size     = 0;                     // one qword in retail (H7)
    d->_triangleLists._capacity = 0;
    d->_maxInstances = 1;
    return d;
}

// ---------------------------------------------------------------------------
// 0x180013c50 — GraphicsMesh::TriangleList::TriangleList(const TriangleList&)
// ---------------------------------------------------------------------------
// The String at +0x00 is REFCOUNTED and the copy takes a reference:
//     dst[0] = 0; if (src[0]) { dst[0] = src[0]; ++src[0][-1]; }
// The zero-first is not redundant — it is what makes the null-source case leave
// a null rather than whatever the raw storage held, which matters because this
// ctor writes into memory `SetSizeCore` has just allocated.
//
// Then +0x08 .. +0x90 are copied as SEVENTEEN QWORDS plus one dword at +0x10,
// verbatim — including the four buffer sub-objects, whose platform companion
// POINTERS are therefore SHARED with the source, not duplicated. That is
// retail's behaviour and it is why a copied TriangleList must not outlive its
// original; recorded, not corrected.
//
// The Array at +0x98 is zeroed and then assigned through 0x180013640, the
// zero-then-operator= order every nested Array copy in this DLL uses (the
// assignment starts by destroying what the destination holds).
//
// +0xa8 is copied last. +0xac..+0xaf are NEVER WRITTEN.
DetailLevel* TriangleListCopyCtor(DetailLevel* d, const DetailLevel* s) {
    d->_name._data = nullptr;
    char* const name = s->_name._data;
    if (name != nullptr) {
        d->_name._data = name;
        ++RtSystem::StringRefCount(name);
    }

    d->_image    = s->_image;                            // +0x08, the raw qword
    d->_distance = s->_distance;                         // +0x10 (retail: a dword MOV)
    // +0x18 .. +0x97: the four buffers, byte for byte (platform companions
    // SHARED, as retail's seventeen-qword copy leaves them).
    d->_vertexBuffer     = s->_vertexBuffer;
    d->_indexBuffer      = s->_indexBuffer;
    d->_skinVertexBuffer = s->_skinVertexBuffer;
    d->_skinIndexBuffer  = s->_skinIndexBuffer;

    d->_triangleLists._data     = nullptr;
    d->_triangleLists._size     = 0;                     // one qword in retail (H7)
    d->_triangleLists._capacity = 0;
    InnerIndexArrayAssign(&d->_triangleLists, &s->_triangleLists);

    d->_maxInstances = s->_maxInstances;
    return d;
}

// ---------------------------------------------------------------------------
// 0x180014910 — the per-element destroy of Array<TriangleList>
// ---------------------------------------------------------------------------
// `TEST R8D,R8D / JLE` first: a count of zero (or negative) does nothing at
// all, not even a virtual call. `MOVSXD R14,R8D` — the counter is the SIGN-
// extended 32-bit argument.
//
// PER ELEMENT, IN RETAIL'S ORDER (which is neither ascending nor descending by
// offset, and is transcribed rather than sorted):
//     +0x98  free the Array buffer, then zero its 16-byte header —
//            UNCONDITIONALLY, including when the buffer was already null
//     +0x78  IndexBuffer::PlatformDestruct
//     +0x58  VertexBuffer::PlatformDestruct
//     +0x38  IndexBuffer::PlatformDestruct
//     +0x18  VertexBuffer::PlatformDestruct
//     +0x00  release the String
//
// THE STRING RELEASE'S TWO ARMS LEAVE THE SLOT HOLDING DIFFERENT THINGS —
// null when the string survives, `str - 8` when it does not — which is the
// asymmetry array_destroy.cpp's Body B documents and the only part of this a
// comparator can see. Reproduced rather than reached through
// `ReleaseStringsCore`, because that helper walks its own loop over the whole
// range and would visit the strings in a separate pass, changing the order of
// allocator calls relative to the four PlatformDestructs.
void DestroyTriangleListRange(DetailLevel* base, int count) {
    if (count <= 0) return;                                  // TEST R8D,R8D / JLE
    const int64_t n = static_cast<int64_t>(count);           // MOVSXD

    for (int64_t i = 0; i < n; ++i) {
        DetailLevel* const e = base + i;                     // stride 0xb0

        if (e->_triangleLists._data != nullptr) MemFree(e->_triangleLists._data);
        e->_triangleLists._data     = nullptr;               // unconditional
        e->_triangleLists._size     = 0;                     // one qword in retail (H7)
        e->_triangleLists._capacity = 0;

        IndexBufferPlatformDestruct(&e->_skinIndexBuffer);    // +0x78
        VertexBufferPlatformDestruct(&e->_skinVertexBuffer);  // +0x58
        IndexBufferPlatformDestruct(&e->_indexBuffer);        // +0x38
        VertexBufferPlatformDestruct(&e->_vertexBuffer);      // +0x18

        char** const nameSlot = &e->_name._data;
        char* const name = *nameSlot;
        if (name == nullptr) continue;
        int32_t* const refs = &RtSystem::StringRefCount(name);
        --*refs;
        if (*refs != 0) {
            *nameSlot = nullptr;                             // MOV [RSI],R15 (== 0)
        } else {
            *nameSlot = name - 8;                            // ADD qword [RSI],-8
            if (*nameSlot != nullptr) MemFree(*nameSlot);    // the JZ on that ADD
        }
    }
}

// ---------------------------------------------------------------------------
// 0x180014af0 — the per-element destroy of Array<Array<T>>
// ---------------------------------------------------------------------------
// One inner Array per 0x10 element at offset 0: free the buffer, zero the
// header. That is array_destroy.cpp's BODY A with stride 0x10 and one offset,
// so it is reached through `FreeInnerArraysCore` and the row below rather than
// written out again.
const DestroySpec kD_InnerArr_10 = { "Array<Collection::Array<T>>", 0x10, 1, { 0x00, 0, 0 } };

void DestroyInnerArrayRange(void*, uint8_t* base, int count) {
    FreeInnerArraysCore(base, count, &kD_InnerArr_10);
}

// ---------------------------------------------------------------------------
// The two new-tail initialisers, i.e. the `else` limb of each fill loop.
// ---------------------------------------------------------------------------
void InitTriangleList(DetailLevel* p) { TriangleListCtor(p); }   // CALL 0x1800fefc0

// 0x18005cda0's arms BOTH open with `[rcx] = 0; [rcx+8] = 0`; the copy arm then
// calls 0x180014c70 over the top. So the init is the two zero stores alone and
// the assign is zero-then-assign — which is why `AssignInnerArray` below repeats
// them instead of delegating.
static void InnerArrayInit(ArrayHdr* a) {
    a->_data     = nullptr;
    a->_size     = 0;                    // one qword (H7)
    a->_capacity = 0;
}
void InitInnerArray(uint8_t* p) { InitAs<ArrayHdr, &InnerArrayInit>(p); }

void AssignTriangleList(DetailLevel* d, const DetailLevel* s) { TriangleListCopyCtor(d, s); }

static void InnerArrayCopy(ArrayHdr* d, const ArrayHdr* s) {
    d->_data     = nullptr;
    d->_size     = 0;                    // one qword (H7)
    d->_capacity = 0;
    InnerArrayAssign(d, s);
}
void AssignInnerArray(uint8_t* d, const uint8_t* s) { AssignAs<ArrayHdr, &InnerArrayCopy>(d, s); }

// ---------------------------------------------------------------------------
// The specs. Field order is array_internal.h's:
//   name, stride, align, assign, init, destroy, byteCopy,
//   assignUnwritten{Lo,Hi}, initUnwritten{Lo,Hi}, seedZero{Lo,Hi}
//
// Both alignments are 8: `MOV EDX,0x8` at 0x18005d8a3 and 0x18005ce02.
// ---------------------------------------------------------------------------
constexpr ArraySpec kSpec_TriangleList = {
    "Array<Graphics::GraphicsMesh::TriangleList>", kTriStride, 8,
    &AssignAs<DetailLevel, &AssignTriangleList>, &InitAs<DetailLevel, &InitTriangleList>,
    &DestroyAs<DetailLevel, &DestroyTriangleListRange>, false,
    0xac, 0xb0,    // the copy ctor's last store ends at +0xac
    0xac, 0xb0,    // ... and so does the default ctor's
    0x00, 0x08 };  // the name slot is refcounted — a probe must seed it null

constexpr ArraySpec kSpec_InnerArray = {
    "Array<Collection::Array<T>>", kInnerStride, 8,
    &AssignInnerArray, &InitInnerArray, &DestroyInnerArrayRange, false,
    0, 0,
    0, 0,
    0x00, 0x10 };  // the whole element is an Array header and IS dereferenced

static_assert(kSpec_TriangleList.initUnwrittenHi <= kSpec_TriangleList.stride,
              "TriangleList init hole must lie inside its own stride");
static_assert(kSpec_TriangleList.seedZeroHi <= kSpec_TriangleList.stride,
              "TriangleList seed window must lie inside its own stride");
static_assert(kSpec_InnerArray.seedZeroHi <= kSpec_InnerArray.stride,
              "inner-array seed window must lie inside its own stride");

}  // namespace gfxmesh
}  // namespace rtcoll

// ============================================================================
// The entry points. One free function per ADDRESS, with the address
// in the name so a crash log names it directly.
// ============================================================================
// B3R RT-VG: namespace rtcoll (external linkage), was an anonymous namespace --
// the graphics readers call these by name (src/engine/graphics/GraphicsMesh.h).
namespace rtcoll {

using namespace rtcoll::gfxmesh;

void* Rt_ArrayDtor_180013fb0(void* a) {       // ~Array<TriangleList>
    // ENDGAME B.3.2: the element destroy is kSpec_TriangleList's own
    // `destroy` (DestroyTriangleListRange), so ~Array runs exactly the two
    // steps this body used to spell out. The `this` retail passes the helper
    // is spilled and never read (the helper's first parameter is unnamed).
    rtcoll::AsArray<&kSpec_TriangleList>(a)->~Array();
    return nullptr;
}
void* Rt_ArrayDtor_180014160(void* a) {       // ~Array<Array<T>>
    rtcoll::AsArray<&kSpec_InnerArray>(a)->~Array();
    return nullptr;
}

void Rt_SetSize_18005d840(void* a, int n) {   // Array<TriangleList>
    rtcoll::AsArray<&kSpec_TriangleList>(a)->SetSize(n);
}
void Rt_SetSize_18005cda0(void* a, int n) {   // Array<Array<T>>
    rtcoll::AsArray<&kSpec_InnerArray>(a)->SetSize(n);
}

}  // namespace rtcoll

