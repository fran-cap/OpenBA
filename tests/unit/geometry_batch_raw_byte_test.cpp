// tests/unit/geometry_batch_raw_byte_test.cpp -- review 2026-11-03 F-008.
//
// GeometryBatch +0x5C is a raw byte in retail: the default constructor (0x14001f250's
// new-element arm, AddDecal's template 0x1400b0e01..0x1400b0e47) never writes it,
// RebuildRoadMesh's working batches never set it, and the copy constructor
// 0x14001f5e0 copies whatever byte is there. The Linux in-game UBSan run loaded
// the value 64 through the copy constructor at serialize_struct60b.cpp:66 while
// the field was typed `bool` (a load of a bool holding neither 0 nor 1 is UB).
// The field is a uint8_t; the byte must survive the copy unchanged.

#include "unit.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <type_traits>

#include "Game/WorldGeometryElements.h"

UNIT_TEST(GeometryBatchRawByte_FieldIsAByte) {
    // A bool member may only hold 0 or 1; retail stores any byte at +0x5C.
    CHECK((std::is_same<decltype(GeometryBatch::isDecal), uint8_t>::value));
    CHECK_EQ(offsetof(GeometryBatch, isDecal), static_cast<size_t>(0x5C));
}

UNIT_TEST(GeometryBatchRawByte_CopyKeepsTheByte) {
    // Storage as the heap hands it to SetSize: the default constructor leaves +0x5C
    // as found, here the byte 0x40 the UBSan report named.
    alignas(GeometryBatch) unsigned char src[sizeof(GeometryBatch)];
    std::memset(src, 0x40, sizeof(src));
    GeometryBatch* s = new (src) GeometryBatch();
    const unsigned char* sb = reinterpret_cast<const unsigned char*>(s);
    CHECK_EQ(static_cast<int>(sb[0x5C]), 0x40);

    alignas(GeometryBatch) unsigned char dst[sizeof(GeometryBatch)];
    std::memset(dst, 0, sizeof(dst));
    GeometryBatch* d = new (dst) GeometryBatch(*s);   // 0x14001f5e0
    const unsigned char* db = reinterpret_cast<const unsigned char*>(d);
    CHECK_EQ(static_cast<int>(db[0x5C]), 0x40);
    CHECK_EQ(static_cast<int>(db[0x5D]), 0);         // alternateKey: zeroed by the default ctor

    d->~GeometryBatch();
    s->~GeometryBatch();
}
