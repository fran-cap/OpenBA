// ============================================================================
// tests/unit/review_game_serialize_test.cpp — review 2026-11-03, group
// game-serialize (findings F-111, F-112)
//
// WorldGeometry::Serialize (0x1400b04d0) reads the terrain grid fields (+0xC0 ..
// +0xD4) and the height-word count (+0xD8) from the save, then BuildTerrainMesh
// (0x1400ac1e0) and the +0x140 height bytes are sized from them. The save values
// are inline here as little-endian hex, exactly as Serializer::Serialize(int&) reads
// them.
//
//   F-111  the +0x140 allocation size: retail's `MOVSXD RCX,EAX` (EAX = g*g) /
//          `SHL RCX,2`, in 64 bits (WorldGeometry::HeightBytesAllocSize).
//   F-112  the grid fields against each other and the height count
//          (WorldGeometry::LoadedTerrainIsConsistent): a scratch block of
//          _chunkIndexStride vertices that FillTerrainVertices writes _fieldCC^2
//          records into, and a height array shorter than _fieldD0^2.
//
// WorldGeometry::Serialize itself is not in the unit target (geometry_serialize.cpp
// also loads the materials, the GPU buffers and the batches; the refusal exits the
// process through System::Error::Display), so these tests drive the two helpers it
// calls on its read arm.
// ============================================================================
#include "unit.h"

#include <cstdint>
#include <cstring>
#include <initializer_list>

#include "Game/WorldGeometry.h"

namespace {

int32_t Le32(const uint8_t* p) {
    int32_t v;
    std::memcpy(&v, p, 4);
    return v;
}

// The six grid ints of a save, in Serialize's read order: +0xC0 gridSize, +0xC4,
// +0xC8 chunkSize, +0xCC vertsPerSide, +0xD0 rowStride, +0xD4 vertsPerChunk; then
// the +0xD8 height-word count.
struct GridRecord {
    int32_t gridSize;
    int32_t chunkSize;
    int32_t vertsPerSide;
    int32_t rowStride;
    int32_t vertsPerChunk;
    int32_t heightCount;
};

GridRecord ReadGrid(const uint8_t (&raw)[28]) {
    GridRecord r;
    r.gridSize      = Le32(raw + 0);
    r.chunkSize     = Le32(raw + 8);
    r.vertsPerSide  = Le32(raw + 12);
    r.rowStride     = Le32(raw + 16);
    r.vertsPerChunk = Le32(raw + 20);
    r.heightCount   = Le32(raw + 24);
    return r;
}

bool Consistent(const GridRecord& r) {
    return WorldGeometry::LoadedTerrainIsConsistent(r.gridSize, r.chunkSize, r.vertsPerSide, r.rowStride,
                                                    r.vertsPerChunk, r.heightCount);
}

}  // namespace

UNIT_TEST(review_gser_f111_height_bytes_size_is_retails_64bit_shift) {
    // gridSize 0x8000 (00 80 00 00): g*g = 0x40000000, retail allocates 0x100000000
    // bytes. The port's int32 g*g*4 overflowed (UB) and wrapped to 0, so _heightBytes
    // was left as heap garbage.
    const uint8_t g8000[4] = {0x00, 0x80, 0x00, 0x00};
    CHECK_EQ(WorldGeometry::HeightBytesAllocSize(Le32(g8000)), static_cast<int64_t>(0x100000000LL));
    // gridSize 0x8080 (80 80 00 00): g*g = 0x40804000; retail asks for 0x102010000,
    // the port wrapped to 0x03010000.
    const uint8_t g8080[4] = {0x80, 0x80, 0x00, 0x00};
    CHECK_EQ(WorldGeometry::HeightBytesAllocSize(Le32(g8080)), static_cast<int64_t>(0x102010000LL));
    // Valid sizes are unchanged: a 512-cell map (dim 256) is 1 MB, gridSize 0 is 0.
    CHECK_EQ(WorldGeometry::HeightBytesAllocSize(512), static_cast<int64_t>(512 * 512 * 4));
    CHECK_EQ(WorldGeometry::HeightBytesAllocSize(0), static_cast<int64_t>(0));
}

UNIT_TEST(review_gser_f112_valid_worldgen_grids_are_accepted) {
    // What WorldGeometry::BuildTerrain (0x1400b0230) stores for dim 256 / 384 / 512:
    // gridSize 2*dim, chunk 32, 33 verts per side, row stride 2*dim+1, 1089 per chunk,
    // and (2*dim+1)^2 height words.
    for (int dim : {256, 384, 512}) {
        const int32_t g = dim * 2;
        CHECK(WorldGeometry::LoadedTerrainIsConsistent(g, 32, 33, g + 1, 33 * 33, (g + 1) * (g + 1)));
    }
}

UNIT_TEST(review_gser_f112_scratch_smaller_than_the_chunk_block_is_refused) {
    // The ledger's ASan repro: gridSize 64, chunk 32, 33 verts per side, row stride 64,
    // _chunkIndexStride 1. BuildTerrainMesh's scratch holds 1 vertex and
    // FillTerrainVertices writes 33*33 into it.
    const uint8_t raw[28] = {0x40, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00,
                             0x21, 0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
                             0x00, 0x10, 0x00, 0x00};
    CHECK(!Consistent(ReadGrid(raw)));
}

UNIT_TEST(review_gser_f112_height_array_shorter_than_the_rows_is_refused) {
    // gridSize 64, chunk 32, 33, row stride 65, 1089 per chunk, but only 16 height
    // words (10 00 00 00): every sample past word 15 reads past _heights.
    const uint8_t raw[28] = {0x40, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00,
                             0x21, 0x00, 0x00, 0x00, 0x41, 0x00, 0x00, 0x00, 0x41, 0x04, 0x00, 0x00,
                             0x10, 0x00, 0x00, 0x00};
    CHECK(!Consistent(ReadGrid(raw)));
}

UNIT_TEST(review_gser_f112_degenerate_and_overflowing_grids_are_refused) {
    // chunkSize 0 (and so the divisions by it), a zero / negative row stride (the
    // sample clamp yields index -1), gridSize 0, and gridSize 0x8000, whose g*g*4 byte
    // count (Serialize's SerializeBytes size) does not fit an int32.
    CHECK(!WorldGeometry::LoadedTerrainIsConsistent(64, 0, 1, 65, 1, 65 * 65));
    CHECK(!WorldGeometry::LoadedTerrainIsConsistent(64, 32, 33, 0, 1089, 0));
    CHECK(!WorldGeometry::LoadedTerrainIsConsistent(64, 32, 33, -1, 1089, 1));
    CHECK(!WorldGeometry::LoadedTerrainIsConsistent(0, 32, 33, 1, 1089, 1));
    CHECK(!WorldGeometry::LoadedTerrainIsConsistent(0x8000, 32, 33, 0x8001, 1089, 0x7FFFFFFF));
    // vertsPerSide not chunkSize + 1: the chunk blocks would not meet, and a wider
    // block reads more vertices than the scratch holds.
    CHECK(!WorldGeometry::LoadedTerrainIsConsistent(64, 32, 40, 65, 1600, 65 * 65));
    // gridSize 8192 in 1-cell chunks: every other field agrees, but BuildTerrainMesh's
    // int32 vertex-buffer size 4 * 8192^2 * 16 overflows.
    CHECK(!WorldGeometry::LoadedTerrainIsConsistent(8192, 1, 2, 8193, 4, 8193 * 8193));
}
