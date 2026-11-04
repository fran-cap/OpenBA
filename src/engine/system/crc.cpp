// ============================================================================
// src/engine/system/crc.cpp — System::CRC::Compute
// ============================================================================
//
// ?Compute@CRC@System@@SAIPEBXHI@Z  (retail 0x1800c5490, ordinal 1752;
// profile build: same name). Static, three args: RCX = data, EDX = byte count,
// R8D = running crc. Read out of the retail body:
//
//     1800c5490  TEST EDX,EDX / JLE done          ; 0 < count
//     loop:      MOVZX EAX,byte [RCX] / INC RCX
//                MOVZX ECX,R8B / XOR ECX,EAX       ; (crc & 0xff) ^ byte
//                MOV EAX,[_crcTable + RCX*4]
//                SHR R8D,8 / XOR R8D,EAX           ; table[...] ^ (crc >> 8)
//                DEC RDX / JNZ loop
//     done:      MOV EAX,R8D / NOT EAX / RET       ; ~crc
//
// Note what it does NOT do: it never complements the seed on entry. A caller
// that wants a standard CRC-32 passes 0xFFFFFFFF itself; a caller chaining
// chunks passes ~previousResult (which is what the persistence jobs do — see
// io/persistence_worker.cpp's Crc32Chunked).
//
// The table is System::CRC::_crcTable (exported DATA, retail 0x1801f9080),
// verified byte-for-byte against the textbook reflected CRC-32 table in
// object/type_system.cpp, so it is generated here rather than read from the
// DLL, for the same reason that file gives: the body has to survive cutover.
// Generated locally rather than shared, on the census rule the persistence
// files record (each file's own dependencies are what the census greps).
//
// Opened as an address door by io/persistence_saveinfo_job.cpp (batch 8b) and
// closed here.
// ============================================================================
// ENDGAME B.3.4b SYSTEM (4): `System::CRC` is a real class with one static
// member (src/engine/system/Crc.h — the mangling `?Compute@CRC@System@@SA…`
// is the evidence, and Crc.h's banner has the argument). The body moved onto
// `System::CRC::Compute`; the registered door keeps its address and its name
// and delegates. Defined at GLOBAL scope with a qualified name rather than
// inside a `namespace System { ... }` block, for the reason B.3.4b SYSTEM (3)
// gives: this file names no POD that the block could capture, and keeping the
// two files consistent is worth more than two lines of nesting.
#include <cstdint>

#include "system/Crc.h"

namespace {

// NOT RETAIL (review F-148): retail's table is static data (0x1801f9080) with no
// build step. The port used to fill a function-local array on first use behind a
// plain `static bool built`, which two threads making their first CRC call raced on
// (the Persistence save-info job and the ext.resources sprite load): a data race,
// and on ARM64 a reader could see `built` before the table stores. The table is
// now built at compile time, as retail's is: the same 256 values, no flag.
struct CrcTableData {
    uint32_t v[256];
};

constexpr CrcTableData BuildCrcTable() {
    CrcTableData t{};
    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t c = i;
        for (int k = 0; k < 8; ++k) {
            c = (c & 1u) ? ((c >> 1) ^ 0xEDB88320u) : (c >> 1);
        }
        t.v[i] = c;
    }
    return t;
}

constexpr CrcTableData kCrcTable = BuildCrcTable();
static_assert(kCrcTable.v[1] == 0x77073096u && kCrcTable.v[255] == 0x2D02EF8Du,
              "the reflected CRC-32 table retail exports at 0x1801f9080");

const uint32_t* CrcTable() {
    return kCrcTable.v;
}

}  // namespace

// ?Compute@CRC@System@@SAIPEBXHI@Z  retail 0x1800c5490 — the member body.
uint32_t System::CRC::Compute(const void* data, int count, uint32_t crc) {
    if (count > 0) {
        const uint32_t* const table = CrcTable();
        const uint8_t* p = static_cast<const uint8_t*>(data);  // CAST-OK: byte_io
        uint64_t n = static_cast<uint32_t>(count);
        do {
            const uint8_t b = *p++;
            crc = table[(crc & 0xffu) ^ b] ^ (crc >> 8);
        } while (--n != 0);
    }
    return ~crc;
}
