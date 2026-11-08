// src/game/Game/serialize_struct60b.cpp
//
// The 0x60-byte GeometryBatch record (WorldGeometryElements.h): its two
// constructors, the owning arrays' element-spec row and the resize entry.
// WorldGeometry::_decals (+0x1F0), WorldGeometry::_roadBatches (+0x200) and
// RebuildRoadMesh's working array are GeometryBatchArray.
//
// Retail map:
//   0x14001f250  Array<GeometryBatch>::SetSize(n)        — GeometryBatch_Resize / SetSize
//   0x14001fa80  Array<GeometryBatch>::SetSize(n, fill)  — SetSize
//   0x14001f5e0  GeometryBatch copy constructor
//   0x14001fb80  destroy a range of batches              — the row's destroy
//
// 0x60-byte element layout (from 0x14001f5e0):
//   +0x00 : qword                    — copied raw (the MaterialInstance ref; only
//                                       the serializer 0x140007140 resolves it)
//   +0x08 : Array<GeometryBatchVertex, stride 0x14> — deep-copied (0x14001fdb0)
//   +0x18 : Array<uint16_t>                          — deep-copied (0x14001fe60)
//   +0x28..+0x44                     — 5 dwords + 1 byte + 1 dword + 1 byte
//   +0x48..+0x5d                     — 5 floats + 2 bytes
//
// B.4 H5-P2 (behaviour:): the two inner arrays are real Collection::Array
// members; the copy / default constructors and the destructor are the class's,
// and the hand-written resize body (step for step rtcoll::SetSizeCore) is the
// one erased core through kSpec_GeometryBatch.

#include "system/MemoryAllocator.h"   // System::MemoryAllocator::_singleton (the scaffold guard)
#include <cstdint>
#include <cstdio>
#include <new>
#include "Game/exe_bodies.h"
#include "Game/WorldGeometryElements.h"

// ============================================================
// GeometryBatch's constructors (WorldGeometryElements.h).
// ============================================================
// The new-element arm of 0x14001f250 (0x14001f318..0x14001f36a; AddDecal's
// template 0x1400b0e01..0x1400b0e47 is the same sequence): +0x00, the two array
// headers, +0x2C..+0x3B and +0x48..+0x57 zeroed, slot = -1, the bytes +0x3C /
// +0x44 / +0x5D and the dwords +0x40 / +0x58 zeroed. +0x5C and the padding
// bytes are not written.
GeometryBatch::GeometryBatch()
    : material(nullptr), vertices(), indices(), slot(-1),
      minX(0), minY(0), maxX(0), maxY(0),
      alpha(0), rotation(0), tiled(false),
      uvParams{0.0f, 0.0f, 0.0f, 0.0f, 0.0f}, alternateKey(false) {}

// 0x14001f5e0: material copied; each array header zeroed (its member ctor) and
// deep-assigned (0x14001fdb0 / 0x14001fe60 = Collection::Array<T>::operator=:
// self-test, clear, size, MemAlloc(size * stride, 2), element copy); then the
// scalars field by field in retail's order (the padding is never written).
GeometryBatch::GeometryBatch(const GeometryBatch& s)
    : material(s.material), vertices(), indices() {
    vertices = s.vertices;                             // 0x14001fdb0
    indices  = s.indices;                              // 0x14001fe60
    slot       = s.slot;                               // +0x28
    minX       = s.minX;                               // +0x2c
    minY       = s.minY;                               // +0x30
    maxX       = s.maxX;                               // +0x34
    maxY       = s.maxY;                               // +0x38
    alpha = s.alpha;                         // +0x3c byte
    rotation = s.rotation;                         // +0x40
    tiled = s.tiled;                         // +0x44 byte
    for (int i = 0; i < 5; ++i)                        // +0x48 .. +0x58
        uvParams[i] = s.uvParams[i];
    isDecal   = s.isDecal;                       // +0x5c byte
    alternateKey = s.alternateKey;                     // +0x5d byte
}

// ============================================================
// kSpec_GeometryBatch — the owning arrays' row.
// ============================================================
// 0x14001f250 (SetSize(n)) and 0x14001fa80 (SetSize(n, fill)) compared with
// rtcoll::SetSizeCore step for step: exact-equal early return (MOVSXD old size),
// the size stored before the capacity test, capacity = n and treble on a
// one-element grow of a non-empty array, MemAlloc(capacity * 0x60, 8); the loop
// starts at the old size when the block did not move, else 0, tests the COMPUTED
// element address, copy-constructs (0x14001f5e0) below the old size and above it
// runs the new-element arm (f250) / the copy constructor from the fill (fa80);
// then on a move 0x14001fb80 over the whole old block and MemFree, else
// 0x14001fb80 over the cut tail. The row is that: assign = copy-construct, init =
// default-construct, destroy = 0x14001fb80.
namespace {

void CopyConstructBatch(GeometryBatch* d, const GeometryBatch* s) {
    new (d) GeometryBatch(*s);                         // 0x14001f5e0
}
void ConstructBatch(GeometryBatch* d) {
    new (d) GeometryBatch();                           // 0x14001f250's init arm
}
// 0x14001fb80 — destroy `count` batches (retail returns at once for count <= 0):
// each one's index array (+0x18), then its vertex array (+0x08), each MemFree then
// its fields zeroed (~GeometryBatch). The `!first` test is the tree's.
void DestroyBatches(GeometryBatch* first, int count) {
    if (count <= 0 || !first) return;
    for (int i = 0; i < count; ++i)
        first[i].~GeometryBatch();
}

void AssignBatch(uint8_t* d, const uint8_t* s) {
    rtcoll::AssignAs<GeometryBatch, &CopyConstructBatch>(d, s);
}
void InitBatch(uint8_t* d) {
    rtcoll::InitAs<GeometryBatch, &ConstructBatch>(d);
}

}  // namespace

namespace GeometryBatchRows {
const rtcoll::ArraySpec kSpec_GeometryBatch = {          // exe 0x14001f250 / 0x14001fa80
    "Array<GeometryBatch>", 0x60, 8,
    &AssignBatch, &InitBatch, &rtcoll::DestroyAs<GeometryBatch, &DestroyBatches>, false,
    /*assign unwritten*/ 0, 0,
    /*init   unwritten*/ 0, 0,
    /*seed zero       */ 0, 0 };
}  // namespace GeometryBatchRows

// ============================================================
// 0x14001f250 — GeometryBatchArray::SetSize(n), behind the tree's two scaffold
// guards (retail has neither: a negative count and a missing allocator).
// ============================================================
void GeometryBatch_Resize(GeometryBatchArray& hdr, unsigned int newCount) {
    // Defensive: when the deserialize stream is misaligned, we may receive a
    // garbage count (typically a negative int reinterpreted as a huge unsigned).
    // Without this, oldCount - newCount underflows to ~65k and DtorRange iterates
    // freeing wild pointers.
    if (static_cast<int32_t>(newCount) < 0) {
        static bool s_warned = false;
        if (!s_warned) {
            printf("[Struct60B::Resize] negative-as-unsigned newCount=%u (signed=%d) "
                   "for header=%p — stream misaligned upstream; leaving array unchanged\n",
                   newCount, static_cast<int32_t>(newCount), (void*)&hdr);
            fflush(stdout);
            s_warned = true;
        }
        return;
    }

    if (!System::MemoryAllocator::_singleton) {
        static bool s_warned = false;
        if (!s_warned) {
            printf("[Struct60B::Resize] MemAlloc unavailable — leaving array unchanged "
                   "(header=%p newCount=%u)\n", (void*)&hdr, newCount);
            fflush(stdout);
            s_warned = true;
        }
        return;
    }

    hdr.SetSize(static_cast<int>(newCount));           // 0x14001f250
}
