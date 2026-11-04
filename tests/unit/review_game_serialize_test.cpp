// ============================================================================
// tests/unit/review_game_serialize_test.cpp — review 2026-11-03, group
// game-serialize (findings F-111, F-112, F-187)
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
//   F-187  UpdateCamera's focus clamp is Math::Vector3::Clamp (Runtime 0x18007cbe0):
//          per component `COMISS max,v / JA keep / MOVAPS v<-max`, then
//          `COMISS min,t / JA min / MOVAPS min<-t`. An unordered compare falls through
//          both JAs, so a NaN component comes out as max.
//
// WorldGeometry::Serialize itself is not in the unit target (geometry_serialize.cpp
// also loads the materials, the GPU buffers and the batches; the refusal exits the
// process through System::Error::Display), so these tests drive the two helpers it
// calls on its read arm.
// ============================================================================
#include "unit.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <limits>

#include "Game/WorldGeometry.h"
#include "math/Math.h"

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
    // the port wrapped to 0x02010000.
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

namespace {

uint32_t Bits(float f) {
    uint32_t u;
    std::memcpy(&u, &f, 4);
    return u;
}

}  // namespace

UNIT_TEST(review_gser_f187_vector3_clamp_nan_takes_max) {
    // The camera focus bounds for a half extent of 256: (-256,-256,0) .. (256,256,0).
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const Math::Vector3 lo(-256.0f, -256.0f, 0.0f);
    const Math::Vector3 hi(256.0f, 256.0f, 0.0f);
    // A NaN on every axis: each falls through `JA keep` to max, which then clears the
    // min test, so the result is max on every axis (z: +0.0).
    const Math::Vector3 r = Math::Vector3(nan, nan, nan).Clamp(lo, hi);
    CHECK_EQ(Bits(r.x), Bits(256.0f));
    CHECK_EQ(Bits(r.y), Bits(256.0f));
    CHECK_EQ(Bits(r.z), Bits(0.0f));
    // Mixed: only the NaN component is replaced.
    const Math::Vector3 m = Math::Vector3(10.0f, nan, -3.0f).Clamp(lo, hi);
    CHECK_EQ(Bits(m.x), Bits(10.0f));
    CHECK_EQ(Bits(m.y), Bits(256.0f));
    CHECK_EQ(Bits(m.z), Bits(0.0f));
    // A NaN bound: max = NaN is unordered, so t = max (NaN); min > NaN is unordered too,
    // so the result is that NaN. min = NaN keeps t.
    const Math::Vector3 nanMax(nan, 256.0f, 0.0f);
    CHECK(std::isnan(Math::Vector3(5.0f, 5.0f, 0.0f).Clamp(lo, nanMax).x));
    const Math::Vector3 nanMin(nan, -256.0f, 0.0f);
    CHECK_EQ(Bits(Math::Vector3(5.0f, 5.0f, 0.0f).Clamp(nanMin, hi).x), Bits(5.0f));
}

UNIT_TEST(review_gser_f187_vector3_clamp_bounds_and_equal_cases) {
    const Math::Vector3 lo(-256.0f, -256.0f, 0.0f);
    const Math::Vector3 hi(256.0f, 256.0f, 0.0f);
    // Inside, below and above.
    const Math::Vector3 a = Math::Vector3(12.5f, -300.0f, 7.0f).Clamp(lo, hi);
    CHECK_EQ(Bits(a.x), Bits(12.5f));
    CHECK_EQ(Bits(a.y), Bits(-256.0f));
    CHECK_EQ(Bits(a.z), Bits(0.0f));
    const Math::Vector3 b = Math::Vector3(300.0f, 1000.0f, -7.0f).Clamp(lo, hi);
    CHECK_EQ(Bits(b.x), Bits(256.0f));
    CHECK_EQ(Bits(b.y), Bits(256.0f));
    CHECK_EQ(Bits(b.z), Bits(0.0f));
    // Equal to a bound: the value is the bound either way.
    const Math::Vector3 e = Math::Vector3(256.0f, -256.0f, 0.0f).Clamp(lo, hi);
    CHECK_EQ(Bits(e.x), Bits(256.0f));
    CHECK_EQ(Bits(e.y), Bits(-256.0f));
    CHECK_EQ(Bits(e.z), Bits(0.0f));
    // Signed zeros: -0.0 against max +0.0 is not `max > v`, so t = max = +0.0.
    // +0.0 against min -0.0 is not `min > t`, so the result keeps t = +0.0.
    const Math::Vector3 z = Math::Vector3(0.0f, 0.0f, -0.0f).Clamp(Math::Vector3(-1.0f, -0.0f, 0.0f), hi);
    CHECK_EQ(Bits(z.y), Bits(0.0f));
    CHECK_EQ(Bits(z.z), Bits(0.0f));
    // A collapsed range (min > max), which the camera never builds: retail takes min.
    const Math::Vector3 c = Math::Vector3(0.0f, 0.0f, 0.0f).Clamp(hi, lo);
    CHECK_EQ(Bits(c.x), Bits(256.0f));
}
