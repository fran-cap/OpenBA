// src/game/Game/geometry_serialize.cpp
//
// M3.5 Path B session 8 — final assembly.
//
// B.4 GAME-b: the receiver is WorldGeometry, settled from retail's callers (see
// WorldGeometryElements.h), so the two bodies are WorldGeometry::Serialize
// (0x1400b04d0) and WorldGeometry::SerializeLegacyBatches (0x1400af1a0), reading
// the class's fields and the nine _arrays through their named accessors.
//
// Two top-level reimpls that drive `Geometry::Serialize` end-to-end so that
// the standalone-exe MenuScene 3D milestone can deserialize terrain without
// the original exe code page being mapped:
//
//   `Reimpl_TradeWork_impl_04d0`  — exe 0x1400b04d0 (~700 bytes, 15 steps)
//                                    Geometry::Serialize body (master schema, version 6).
//   `Reimpl_TradeWork_impl_f1a0`  — exe 0x1400af1a0 (~196 bytes, 4 steps)
//                                    Version-gated extra serialize (parallel schema, version 4).
//
// Both walk the spec in `docs/history/notes/M35_geom_serialize_findings.md`. Each step
// delegates to a previously-implemented session helper:
//
//   session 2 — the GeometryBatch resize / copy / destroy (now GeometryBatchArray, B.4 H5-P2)
//   session 3 — Reimpl_ExternalPtr_Serialize_Typed_7140 (MaterialInstance)
//   session 4 — Reimpl_Struct60B_Serialize, Reimpl_Array_Serialize_External_MaterialInstance
//   session 5 — Reimpl_Array_Resize_Stride02/16A/16B/04/_ArrayOfArrays
//   session 6 — Reimpl_TradeWork_sub_c1e0 (heightmap mesh build)
//   session 7 — Reimpl_TradeWork_sub_c780 (heightmap → texture upload)
//   prior     — Reimpl_Array_Resize_Stride08 (stride 8) from serialize_arrays.cpp
//
// Cascade safety: every Runtime primitive (Serializer ctor/dtor, dispatch slot
// for Serialize_Int/Bool/Float/Byte/SerializeBytes, Vector2::Serialize, IndexBuffer/
// VertexBuffer::Create) is null-guarded with one-shot `printf` warnings; on
// missing primitives we soft-fail per-step rather than crashing.
//
// On first run in standalone we expect cascades on the 4 typed-ExternalPtr
// serialize call sites (MaterialInstance via FUN_140007140 at +0xF8, +0x150,
// +0x158 inside 04d0; FUN_140020150 at +0x210; FUN_14001ffa0's interior call
// for each 0x60B element). Session 3 + session 4 reimpls already log
// one-shot warnings in those branches and return early — so the menu
// shouldn't crash even with partial type registration. Document expected
// failure points inline so the user can iterate.
//
// Dispatch pattern: try the original exe address first (usable when
// 0x1400b04d0 / 0x1400af1a0 is RegisterExeAddr-able). If null, fall
// through to the cleaned C body. Both functions self-resolve each Runtime
// dispatch slot lazily on first use.

// ENDGAME B.3.5 batch K1: the runtime type header comes FIRST. video/Buffers.h reaches
// no Volume.h, but the house rule (a runtime type header ahead of resolve.h's
// <windows.h>, whose <wingdi.h> `Rectangle()` hides Volume.h's global
// `struct Rectangle`) is kept so a later include change cannot break it silently.
#include "video/Buffers.h"   // Video::{Vertex,Index}Buffer
#include "Component/still_terrain.h"   // STILLTERRAIN: StillTerrain::Bump (the terrain epoch)
#include "math/Math.h"
#include "system/MemoryAllocator.h"   // System::MemoryAllocator::MemAlloc / MemFree
#include "io/Serializer.h"   // ENDGAME B.3.4 caller batch B
#include <new>               // ENDGAME B.3.4a closing: placement-new of IO::Serializer
#include <cstdint>
#include <cstdio>
#include "system/Error.h"   // NOT RETAIL (review F-112): a damaged terrain record is retail's fatal error box

// Heap-overflow checkpoint (standalone diagnostic; no-op when
// BANISHED_REDZONE is unset). Localizes which Geometry::Serialize step trips a
// tail-canary so the displaced free-list crash can be root-caused.
#include "redzone_alloc.h"
#include "Game/exe_bodies.h"
#include "Game/WorldGeometry.h"
#include "Game/WorldGeometryElements.h"
#include "io/Stream.h"
#include "graphics/Material.h"   // GeometryBatch::material -> the External* table
// Use the free-list LINK validator (not the tail-canary ValidateAll, which is a
// no-op at PAD=0) so a checkpoint catches the step that clobbers a freed block's
// forward link — the menu heap-crash signature. Aborts (0x124) naming the tag.
#define RZ_CHECK(tag) RedzoneAlloc_CheckLinks(tag)

// The reimpl entry points of sibling files are declared in Game/exe_bodies.h.

namespace {

// The serializer's load/save flag: the stream's direction (+0x08 of the
// Stream; 0 = loading, 1 = saving).
inline bool IsLoading(IO::Serializer& serializer) {
    return static_cast<int>(serializer.GetDirection()) == 0;   // IO::Serializer::GetDirection, retail 0x180006210
}

// One contour draw-range array (+0x1C0 / +0x1D0): count, on load resize (stride
// 0x10, align 4), then four ints per record.
void SerializeDrawRanges(IO::Serializer& serializer, RawArray<GeometryDrawRange>& ranges) {
    int32_t c = ranges.size;
    serializer.Serialize(c, nullptr);
    if (IsLoading(serializer)) {
        RawArraySetSize(ranges, c);   // Array<GeometryDrawRange>::SetSize 0x14001efd0
    }
    if (c > 0 && ranges.data) {
        for (int i = 0; i < c; ++i) {
            for (int k = 0; k < 4; ++k)
                serializer.Serialize(ranges.data[i].values[k], nullptr);
        }
    }
}

// One batch array (+0x1F0 / +0x200): count, on load resize (0x14001f250), then
// the batches in place (0x14001ffa0).
void SerializeBatches(IO::Serializer& serializer, GeometryBatchArray& batches) {
    int32_t c = batches._size;
    serializer.Serialize(c, nullptr);
    if (IsLoading(serializer)) {
        GeometryBatch_Resize(batches, static_cast<unsigned>(c));
    }
    GeometryBatch_Serialize(serializer, c, batches._data);
}

// The pre-v5 (Serialize) / pre-v3 (SerializeLegacyBatches) backfill, on load: the
// material table +0x210 sized to the road batches, each entry the batch's
// material, and each batch's box reset to {0, 0, grid, grid}.
void BackfillBatchMaterials(WorldGeometry& geometry) {
    GeometryBatchArray& roads = geometry._roadBatches;
    RawArray<Graphics::MaterialInstance*>& materials = geometry._batchMaterials;
    Reimpl_Array_Resize_Stride08(&materials, roads._size);   // FUN_140001bb0
    int32_t dstCount = materials.size;
    int32_t grid = geometry._gridSize;
    if (dstCount > 0 && roads._data && materials.data) {
        for (int i = 0; i < dstCount; ++i) {
            materials.data[i] = roads._data[i].material;
            roads._data[i].minX = 0;
            roads._data[i].minY = 0;
            roads._data[i].maxX = grid;
            roads._data[i].maxY = grid;
        }
    }
}

// The material table +0x210: count, on load resize (stride 8), then the
// references (0x140020150).
void SerializeBatchMaterials(IO::Serializer& serializer, RawArray<Graphics::MaterialInstance*>& materials) {
    int32_t c = materials.size;
    serializer.Serialize(c, nullptr);
    if (IsLoading(serializer)) {
        Reimpl_Array_Resize_Stride08(&materials, c);
    }
    ::Object::SerializeExternalArray<Graphics::MaterialInstance>(serializer, c, materials.data);   // 0x140020150 (B8 RT2)
}

} // namespace

// NOT RETAIL (ext.create_on_demand, docs/extensions/handins/CREATEONDEMAND.md): each loaded
// batch's material through WorldGeometry::_extMaterialTaken (the caller tests the seam).
static void ExtTakeBatchMaterials(const GeometryBatchArray& batches) {
    for (int32_t i = 0; i < batches._size; ++i) {
        WorldGeometry::_extMaterialTaken(batches._data[i].material);
    }
}

// ============================================================
// 0x1400b04d0 — WorldGeometry::Serialize (version 6).
// ============================================================
//
// Spec: docs/history/notes/M35_geom_serialize_findings.md lines 42-112.
void WorldGeometry::Serialize(IO::Stream& stream) {
    RZ_CHECK("geom 04d0 entry");

    // ---- Step 1: the local Serializer, version 6. ----
    IO::Serializer serializer(stream, 6);

    // ---- Step 2: six ints at +0xC0 .. +0xD4. ----
    // Disassembly @ 0x1400b051f .. 0x1400b059e: 6 calls to slot 0x140152ac8.
    serializer.Serialize(_gridSize, nullptr);           // +0xC0
    serializer.Serialize(_fieldC4, nullptr);            // +0xC4
    serializer.Serialize(_chunkSize, nullptr);          // +0xC8
    serializer.Serialize(_fieldCC, nullptr);            // +0xCC
    serializer.Serialize(_fieldD0, nullptr);            // +0xD0
    serializer.Serialize(_chunkIndexStride, nullptr);   // +0xD4

    // ---- Step 3: the height words at +0xD8 (count = the header's low dword). ----
    // Verified original disasm @ 0x1400b0589..0x1400b05ce: local_count is the
    // ELEMENT count (no shift). Both resize and serialize use it directly.
    int32_t local_count = static_cast<int32_t>(_heights.size);
    serializer.Serialize(local_count, nullptr);
    if (IsLoading(serializer)) {
        RawArraySetSize(_heights, local_count);   // Array<int16>::SetSize 0x14001c890
    }
    // Retail 0x1400b05c0..0x1400b05ce: R8 = [R14+0xd8], EDX = the count, CALL [0x1401523f8] =
    // Serialize(int, short*) -- the field's own int16 type, with no null test of the buffer
    // (B6 row 61: the tree used the ushort overload behind an `if (_heights.data)`).
    serializer.Serialize(local_count, _heights.data, nullptr);
    StillTerrain::Bump();   // [STILLTERRAIN] the heightmap may have been streamed in

    RZ_CHECK("geom 04d0 after step3 (+0xD8 uint16 heightmap arr)");

    // NOT RETAIL (review F-112, F-111): a terrain record whose grid fields disagree with
    // each other or with the height count (WorldGeometry::LoadedTerrainIsConsistent) is
    // refused here, before BuildTerrainMesh and the +0x140 allocation size anything from
    // them, through retail's own fatal error box (SetText(.., true) + Display: the box,
    // then exit), as Map::Serialize refuses a damaged map. Retail builds the mesh from
    // the raw fields and overruns its vertex scratch and the height array.
    if (IsLoading(serializer) &&
        !LoadedTerrainIsConsistent(_gridSize, _chunkSize, _fieldCC, _fieldD0, _chunkIndexStride,
                                   local_count)) {
        fprintf(stderr,
                "[WorldGeometry::Serialize] FATAL: terrain grid %d chunk %d %d stride %d %d, %d heights\n",
                _gridSize, _chunkSize, _fieldCC, _fieldD0, _chunkIndexStride, local_count);
        fflush(stderr);
        System::Error::SetText("This save's terrain is damaged and cannot be loaded.", true);
        System::Error::Display();   // the box; _stopApplication then exits the process
        return;
    }

    // ---- Step 4: the terrain material at +0xF8 (0x140007140). ----
    ::Object::SerializeExternal<Graphics::MaterialInstance>(serializer, _terrainMaterial);

    // ---- Step 5: the raw heightmap bytes at +0x140 (4 bytes per cell). ----
    // On loading: build the mesh first (0x1400ac1e0), then allocate the buffer,
    // then read the bytes. On saving: write from the existing pointer. The test is
    // the stream's own direction (`*(int*)(stream + 8) == 0`).
    // Retail sizes the allocation in 64 bits (0x1400b05f2..0x1400b05fc: IMUL EAX,EAX /
    // MOVSXD RCX,EAX / SHL RCX,2) and the byte count it serializes in 32 bits
    // (0x1400b061e..0x1400b0628: IMUL EDX,EDX / SHL EDX,2), and stores the allocation at
    // +0x140 unconditionally (0x1400b060b). NOT RETAIL (review F-111): both products are
    // formed without int32 overflow (UB); the load check above keeps g*g*4 in range.
    int gridSize  = _gridSize;
    int byteCount = static_cast<int>(static_cast<uint32_t>(HeightBytesAllocSize(gridSize)));
    uint8_t* heightBuf = nullptr;
    const bool streamLoading = (static_cast<int>(stream.GetDirection()) == 0);

    if (streamLoading) {
        BuildTerrainMesh();   // 0x1400ac1e0
        if (System::MemoryAllocator::_singleton) {
            const uint64_t allocSize = static_cast<uint64_t>(HeightBytesAllocSize(gridSize));
            heightBuf = static_cast<uint8_t*>(System::MemoryAllocator::MemAlloc(allocSize, 4));
        }
        _heightBytes = heightBuf;   // 0x1400b060b: stored whatever the size
    } else {
        heightBuf = _heightBytes;
    }
    if (heightBuf && byteCount > 0) {
        serializer.SerializeBytes(byteCount, heightBuf, nullptr);
    }

    RZ_CHECK("geom 04d0 after step5 (sub_c1e0 mesh build + heightBuf)");

    // ---- Step 6: version-gated contour vertex / index arrays. ----
    RawArray<TerrainVertex>& contourVertices = _contourVertices;   // +0x1A0
    RawArray<uint16_t>&      contourIndices  = _contourIndices;    // +0x1B0
    int version = serializer.GetVersion();
    if (version < 3) {
        // OLD format: bool + byte count + raw bytes for both arrays.
        bool flag = false;
        int32_t cnt = 0;
        serializer.Serialize(flag, nullptr);
        serializer.Serialize(cnt, nullptr);
        RawArraySetSize(contourVertices, cnt >> 4);   // Array<TerrainVertex>::SetSize 0x14001ef10
        if (contourVertices.data) serializer.SerializeBytes(cnt, contourVertices.data, nullptr);
        serializer.Serialize(flag, nullptr);
        serializer.Serialize(cnt, nullptr);
        RawArraySetSize(contourIndices, cnt >> 1);     // Array<uint16>::SetSize 0x14001c890
        if (contourIndices.data) serializer.SerializeBytes(cnt, contourIndices.data, nullptr);
    } else {
        // NEW format: per-element loops (0x1400b073f .. 0x1400b0768):
        //   foreach: Serialize(4, position); Serialize(2, uv); Serialize(4, normal)
        int32_t vcount = contourVertices.size;
        serializer.Serialize(vcount, nullptr);
        if (IsLoading(serializer)) {
            RawArraySetSize(contourVertices, vcount);   // 0x14001ef10
        }
        if (vcount > 0 && contourVertices.data) {
            for (int i = 0; i < vcount; ++i) {
                TerrainVertex& v = contourVertices.data[i];
                serializer.Serialize(4, v.position, nullptr);
                serializer.Serialize(2, v.uv, nullptr);
                serializer.Serialize(4, v.normal, nullptr);
            }
        }
        int32_t icount = contourIndices.size;
        serializer.Serialize(icount, nullptr);
        if (IsLoading(serializer)) {
            RawArraySetSize(contourIndices, icount);   // 0x14001c890
        }
        if (contourIndices.data) serializer.Serialize(icount, contourIndices.data, nullptr);
    }

    RZ_CHECK("geom 04d0 after step6 (vertex/index arrays)");

    // ---- Step 7: the two water materials at +0x150 / +0x158 (0x140007140). ----
    ::Object::SerializeExternal<Graphics::MaterialInstance>(serializer, _waterMaterialA);
    ::Object::SerializeExternal<Graphics::MaterialInstance>(serializer, _waterMaterialB);

    // ---- Step 8: the two draw-range arrays at +0x1C0 and +0x1D0. ----
    SerializeDrawRanges(serializer, _contourRangesA);
    SerializeDrawRanges(serializer, _contourRangesB);

    RZ_CHECK("geom 04d0 after step8 (int4 arrays +0x1C0/+0x1D0)");

    // ---- Step 9: the outlines at +0x1E0 (an array of Vector2 arrays). ----
    {
        RawArray<RawArray<Math::Vector2>>& outlines = _contourOutlines;
        int32_t outerCount = outlines.size;
        serializer.Serialize(outerCount, nullptr);
        if (IsLoading(serializer)) {
            RawArraySetSize(outlines, outerCount, &kSpec_ContourOutline);   // 0x14001f0a0
        }
        if (outerCount > 0 && outlines.data) {
            for (int i = 0; i < outerCount; ++i) {
                RawArray<Math::Vector2>& outline = outlines.data[i];
                int32_t innerCount = outline.size;
                serializer.Serialize(innerCount, nullptr);
                if (IsLoading(serializer)) {
                    Reimpl_Array_Resize_Stride08(&outline, innerCount);   // FUN_1400203a0
                }
                if (innerCount > 0 && outline.data) {
                    for (int j = 0; j < innerCount; ++j)
                        outline.data[j].Serialize(serializer);   // slot 0x1401526b8
                }
            }
        }
    }

    RZ_CHECK("geom 04d0 after step9 (array-of-arrays +0x1E0)");

    // ---- Step 10: the road batches at +0x200. ----
    SerializeBatches(serializer, _roadBatches);

    RZ_CHECK("geom 04d0 after step10 (Struct60B array +0x200)");

    // ---- Step 11: version 4+: the batch counters at +0x220 (stride 4). ----
    // Original at 0x1400b0a55 calls slot 0x140151278: Serialize(int count, int* data, name).
    if (version >= 4) {
        Collection::Array<int32_t>& counters = _batchCounters;
        int32_t c = counters._size;
        serializer.Serialize(c, nullptr);
        if (IsLoading(serializer)) {
            counters.SetSize(c);   // 0x1400021d0
        }
        if (counters._data) serializer.Serialize(c, counters._data, nullptr);
    }

    // ---- Step 12: version < 5, on load: backfill +0x210 from the road batches. ----
    if (version < 5 && IsLoading(serializer)) {
        BackfillBatchMaterials(*this);
    }

    // ---- Step 13: version 6+: the material table at +0x210. ----
    if (version >= 6) {
        SerializeBatchMaterials(serializer, _batchMaterials);
    }

    // ---- Step 14: on load only. ----
    if (streamLoading) {
        // 14a: the contour IndexBuffer (+0x180) with idxCount*2 bytes.
        // 14b: the contour VertexBuffer (+0x160) with vertCount*16 bytes.
        // 14c: 0x1400ac780 — the height image upload.
        // 14d: if the material table and the counters disagree, resize the table.
        _waterIndexBuffer.Create(contourIndices.size * 2, false, contourIndices.data);
        _waterVertexBuffer.Create(contourVertices.size * 0x10, false, contourVertices.data);
        UploadHeightImage();   // 0x1400ac780

        int32_t materialCount = _batchMaterials.size;
        int32_t counterCount  = _batchCounters._size;
        if (materialCount != counterCount) {
            Reimpl_Array_Resize_Stride08(&_batchMaterials, counterCount);
        }

        // EXT SEAM (NOT RETAIL, docs/extensions/handins/CREATEONDEMAND.md): the loaded terrain's
        // materials are queued for their first-use creates (WorldGeometry.h has the contract).
        if (_extMaterialTaken != nullptr) {
            _extMaterialTaken(_terrainMaterial);
            _extMaterialTaken(_waterMaterialA);
            _extMaterialTaken(_waterMaterialB);
            ExtTakeBatchMaterials(_roadBatches);
            for (int32_t i = 0; i < _batchMaterials.size; ++i) {
                _extMaterialTaken(_batchMaterials.data[i]);
            }
        }
    }

    RZ_CHECK("geom 04d0 after step14 (VB/IB create + image upload)");
    // ---- Step 15: the local Serializer's dtor (the compiler's). ----
}

// ============================================================
// 0x1400af1a0 — WorldGeometry::SerializeLegacyBatches (version 4).
// ============================================================
//
// Spec: docs/history/notes/M35_geom_serialize_findings.md lines 114-127. A separate
// IO::Serializer with version 4 (NOT 6): the decals (+0x1F0) and road batches
// (+0x200), then the material table (+0x210).
void WorldGeometry::SerializeLegacyBatches(IO::Stream& stream) {
    RZ_CHECK("geom f1a0 entry");

    IO::Serializer serializer(stream, 4);

    SerializeBatches(serializer, _decals);        // +0x1F0
    SerializeBatches(serializer, _roadBatches);   // +0x200

    // Version 0-2, on load: backfill +0x210 from the road batches.
    int version = serializer.GetVersion();
    if (version < 3 && IsLoading(serializer)) {
        BackfillBatchMaterials(*this);
    }

    // Version 3+: the material table at +0x210.
    if (version >= 3) {
        SerializeBatchMaterials(serializer, _batchMaterials);
    }

    // EXT SEAM (NOT RETAIL, docs/extensions/handins/CREATEONDEMAND.md): the loaded decals' and roads'
    // materials are queued for their first-use creates (WorldGeometry.h has the contract).
    if (IsLoading(serializer) && _extMaterialTaken != nullptr) {
        ExtTakeBatchMaterials(_decals);
        ExtTakeBatchMaterials(_roadBatches);
    }

    RZ_CHECK("geom f1a0 before dtor");
}

