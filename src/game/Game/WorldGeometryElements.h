#pragma once
// ============================================================================
// src/game/Game/WorldGeometryElements.h — ENDGAME B.4 GAME-b
//
// The element records of WorldGeometry's nine {data, size|capacity} headers
// (WorldGeometry::_arrays, +0x1A0..+0x22F) and of the terrain vertex buffer.
// WorldGeometry.h (GAME-c) keeps the nine headers untyped (`Header`); until it
// splits them into typed arrays, they are read through nine named accessors
// (WorldGeometry::ContourVertices() .. BatchCounters(), geometry_draw.cpp), whose
// bodies are the only casts (LEFT: owner GAME-c).
//
// RETAIL (Application-x64.exe), the receivers settled from the callers, not the
// Ghidra labels: 0x1400b04d0 (Serialize, "TradeWork_impl_04d0"), 0x1400af1a0
// (SerializeLegacyBatches, "TradeWork_impl_f1a0"), 0x1400ac1e0 (BuildTerrainMesh,
// "TradeWork_impl_sub_c1e0") and 0x1400ac780 (UploadHeightImage,
// "TradeWork_impl_sub_c780") are WorldGeometry members: MenuScene's Serialize
// (0x1400a3630) builds +0x28 with MemAlloc(0x230) + 0x1400b00e0 (the
// WorldGeometry ctor) and calls 04d0 / f1a0 on it; the game's serialize
// (0x140095c72) passes GameUpdateLoop+0x110 (m_worldGeometry); c1e0 / c780 are
// called from 04d0 / 0x1400b0c50 / 0x1400ac4b0 on the same RCX.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "Action/PathSearch.h"   // RawArray<T>, Collection::Array (collection/Collection.h)

namespace Object { class External; }
namespace Math { class Vector2; }
namespace Graphics { class MaterialInstance; }

// One terrain vertex (stride 0x10): the master VertexBuffer at +0x100 and the
// contour vertices at +0x1A0 (0x1400ac880 writes it; Serialize: 4 shorts, 2 shorts,
// 4 bytes).
struct TerrainVertex {
    int16_t position[4];   // +0x00 x (col - grid/2), y (row - grid/2), height, 0x00FF
    int16_t uv[2];         // +0x08 col / grid * 16384, row / grid * 16384
    uint8_t normal[4];     // +0x0C (n + 1) * 127.5 per component, then 0
};
static_assert(sizeof(TerrainVertex) == 0x10, "TerrainVertex stride 0x10");
static_assert(offsetof(TerrainVertex, position) == 0x0, "TerrainVertex::position");
static_assert(offsetof(TerrainVertex, uv) == 0x08, "TerrainVertex::uv @ +0x08");
static_assert(offsetof(TerrainVertex, normal) == 0x0C, "TerrainVertex::normal @ +0x0C");

// One batch vertex (stride 0x14): a GeometryBatch's vertices (0x14001fdb0 copies
// it, 0x14001f9c0 serializes 4 shorts, 4 shorts, 4 bytes). ACT-a's RoadVertex is the
// same record.
struct GeometryBatchVertex {
    int16_t position[4];   // +0x00 x, y, height, blend
    int16_t uv[4];         // +0x08
    uint8_t normal[4];     // +0x10
};
static_assert(sizeof(GeometryBatchVertex) == 0x14, "GeometryBatchVertex stride 0x14");
static_assert(offsetof(GeometryBatchVertex, position) == 0x0, "GeometryBatchVertex::position");
static_assert(offsetof(GeometryBatchVertex, uv) == 0x08, "GeometryBatchVertex::uv @ +0x08");
static_assert(offsetof(GeometryBatchVertex, normal) == 0x10, "GeometryBatchVertex::normal @ +0x10");

// One mesh batch (stride 0x60): the decals at +0x1F0 (draw mode 0x2000) and the
// road meshes at +0x200 (mode 0x4000). Serialize 0x14001ffa0. ACT-a's RoadBatch is
// the same record.
//
// B.4 H5-P2 (behaviour:): the two inner arrays are real Collection::Array members
// and the record's constructors / destructor are retail's:
//   GeometryBatch()        the new-element arm of SetSize 0x14001f250 (0x14001f318..
//                          0x14001f36a) and AddDecal's template (0x1400b0e01..
//                          0x1400b0e47): every field zeroed, slot = -1; +0x5C and
//                          the padding are NOT written (retail leaves them as found)
//   GeometryBatch(const&)  0x14001f5e0: material, vertices zeroed + Array<Vertex>::
//                          operator= (0x14001fdb0), indices zeroed + Array<uint16>::
//                          operator= (0x14001fe60), then the scalars field by field
//   ~GeometryBatch         the per-element body of 0x14001fb80: indices, then
//                          vertices (each MemFree, then its fields zeroed): the
//                          reverse declaration order of the two members
// The owning arrays are GeometryBatchArray (below): their SetSize / ~Array are the
// one erased core driven by kSpec_GeometryBatch (serialize_struct60b.cpp).
struct GeometryBatch {
    GeometryBatch();                               // 0x14001f250's init arm
    GeometryBatch(const GeometryBatch& src);       // 0x14001f5e0
    GeometryBatch& operator=(const GeometryBatch&) = delete;   // retail has none for the record

    Graphics::MaterialInstance*            material;   // +0x00 (0x140007140 serializes it; null = unused slot)
    Collection::Array<GeometryBatchVertex> vertices;   // +0x08 (SetSize 0x14001ee40 / 0x14001f8d0)
    Collection::Array<uint16_t>            indices;    // +0x18 (SetSize 0x14001c890)
    int32_t  slot;                                 // +0x28 -1 = free
    int32_t  minX;                                 // +0x2C culling box, tile coords
    int32_t  minY;                                 // +0x30
    int32_t  maxX;                                 // +0x34
    int32_t  maxY;                                 // +0x38
    uint8_t  alpha;                           // +0x3C (Serialize uchar)
    uint8_t  _pad3D[3];                            // +0x3D
    int32_t  rotation;                           // +0x40 (Serialize int)
    bool     tiled;                           // +0x44 (Serialize bool)
    uint8_t  _pad45[3];                            // +0x45
    float    uvParams[5];                        // +0x48 .. +0x58 (Serialize float x5)
    // NOT RETAIL (type only, review F-008): retail's +0x5C is a raw byte. The default
    // constructor leaves it as found and RebuildRoadMesh never sets it, so the copy
    // constructor 0x14001f5e0 copies whatever byte the heap held (UBSan saw 64); typed
    // `bool`, that copy was a load of an invalid bool value (UB). A uint8_t copies the
    // byte exactly as retail does; Serialize still goes through the bool slot (+0x78).
    uint8_t  isDecal;                           // +0x5C (Serialize bool, raw byte)
    bool     alternateKey;                         // +0x5D (version >= 2) decal draw key 3 instead of 1
    uint8_t  _pad5E[2];                            // +0x5E
};
static_assert(sizeof(GeometryBatch) == 0x60, "GeometryBatch stride 0x60");
static_assert(offsetof(GeometryBatch, material) == 0x0, "GeometryBatch::material");
static_assert(offsetof(GeometryBatch, minY) == 0x30, "GeometryBatch::minY");
static_assert(offsetof(GeometryBatch, maxX) == 0x34, "GeometryBatch::maxX");
static_assert(offsetof(GeometryBatch, _pad3D) == 0x3D, "GeometryBatch::_pad3D");
static_assert(offsetof(GeometryBatch, _pad45) == 0x45, "GeometryBatch::_pad45");
static_assert(offsetof(GeometryBatch, _pad5E) == 0x5E, "GeometryBatch::_pad5E");
static_assert(offsetof(GeometryBatch, vertices) == 0x08, "GeometryBatch::vertices @ +0x08");
static_assert(offsetof(GeometryBatch, indices) == 0x18, "GeometryBatch::indices @ +0x18");
static_assert(offsetof(GeometryBatch, slot) == 0x28, "GeometryBatch::slot @ +0x28");
static_assert(offsetof(GeometryBatch, minX) == 0x2C, "GeometryBatch::minX @ +0x2C");
static_assert(offsetof(GeometryBatch, maxY) == 0x38, "GeometryBatch::maxY @ +0x38");
static_assert(offsetof(GeometryBatch, alpha) == 0x3C, "GeometryBatch +0x3C");
static_assert(offsetof(GeometryBatch, rotation) == 0x40, "GeometryBatch +0x40");
static_assert(offsetof(GeometryBatch, tiled) == 0x44, "GeometryBatch +0x44");
static_assert(offsetof(GeometryBatch, uvParams) == 0x48, "GeometryBatch +0x48");
static_assert(offsetof(GeometryBatch, isDecal) == 0x5C, "GeometryBatch +0x5C");
static_assert(offsetof(GeometryBatch, alternateKey) == 0x5D, "GeometryBatch +0x5D");
static_assert(offsetof(GeometryBatch, vertices) + offsetof(Collection::Array<GeometryBatchVertex>, _size) == 0x10,
              "GeometryBatch::vertices._size @ +0x10 (the road draw's vertex-count test)");
static_assert(offsetof(GeometryBatch, indices) + offsetof(Collection::Array<uint16_t>, _size) == 0x20,
              "GeometryBatch::indices._size @ +0x20");
static_assert(alignof(GeometryBatch) == 8, "MemAlloc(cap * 0x60, 8) (0x14001f2b3)");

// The owning arrays' hand-derived row (serialize_struct60b.cpp): assign = the copy
// constructor 0x14001f5e0, init = the default constructor (0x14001f250's new-element
// arm), destroy = 0x14001fb80. Retail's SetSize 0x14001f250 (2-arg) / 0x14001fa80
// (fill form) and the inlined ~Array (0x1400aedd3, 0x1400b005b) ARE rtcoll::SetSizeCore
// / ClearCallback over this row, arm for arm.
namespace GeometryBatchRows {
extern const rtcoll::ArraySpec kSpec_GeometryBatch;   // exe 0x14001f250 / 0x14001fa80
}
using GeometryBatchArray = Collection::Array<GeometryBatch, Collection::DefaultAllocator,
                                             Collection::ExplicitSpec<&GeometryBatchRows::kSpec_GeometryBatch>>;
static_assert(sizeof(GeometryBatchArray) == 0x10, "the Array header {data, size, capacity}");

// WorldGeometry +0x1E0 (_contourOutlines, Array<Array<Math::Vector2>>): the row of retail SetSize
// 0x14001f0a0 (serialize_resize_helpers.cpp), used through RawArraySetSize(a, n, row). B8 RT2.
extern const rtcoll::ArraySpec kSpec_ContourOutline;   // exe 0x14001f0a0

// One contour draw range (stride 0x10): +0x1C0 (mode 0x6000) and +0x1D0 (mode
// 0x8000). Draw passes DrawIndexed(0, [1], [2], [0], [3], 1).
struct GeometryDrawRange {
    int32_t values[4];
};
static_assert(sizeof(GeometryDrawRange) == 0x10, "GeometryDrawRange stride 0x10");
static_assert(offsetof(GeometryDrawRange, values) == 0x0, "GeometryDrawRange::values");

namespace IO { class Serializer; }

// The batch helpers (serialize_struct60b.cpp / serialize_struct60b_serialize.cpp).
// GeometryBatch_Resize is GeometryBatchArray::SetSize (0x14001f250) behind the
// tree's two scaffold guards (a negative count, no allocator yet).
void GeometryBatch_Resize(GeometryBatchArray& array, unsigned int newCount);           // 0x14001f250
void GeometryBatch_Serialize(IO::Serializer& serializer, int count, GeometryBatch* batches);  // 0x14001ffa0
