// ============================================================================
// tests/unit/redzone_canary_test.cpp -- the redzone shim's pad and canary dump (review F-281,
// F-285, F-300)
// ============================================================================
// src/diag/win/redzone_canary.h, used by src/diag/win/redzone_alloc.cpp:
//   * PadBytes rounds BANISHED_REDZONE_PAD up to 8 without int overflow: INT_MAX (what
//     ClampedAtoi returns for any larger number) gives 2^31, not a negative int cast to size_t;
//   * FormatHead dumps only the canary bytes the shim allocated (at most 16): with an 8-byte
//     pad the canary is placed last before a PAGE_NOACCESS page (Windows only), where a fixed
//     16-byte dump reads 8 bytes into the guard page and faults.
// ============================================================================
#include "unit.h"

#include "win/redzone_canary.h"

#include <climits>
#include <cstdint>
#include <cstring>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace {

int CountBytes(const char* line) {
    const char* p = std::strstr(line, "canary head:");
    if (!p) return -1;
    p += std::strlen("canary head:");
    int n = 0;
    while (*p == ' ') {
        ++n;
        p += 3;
    }
    return n;
}

}  // namespace

UNIT_TEST(redzone_pad_bytes_round_without_overflow) {
    CHECK_EQ(RedzoneCanary::PadBytes(0), (size_t)0);
    CHECK_EQ(RedzoneCanary::PadBytes(-5), (size_t)0);
    CHECK_EQ(RedzoneCanary::PadBytes(1), (size_t)8);
    CHECK_EQ(RedzoneCanary::PadBytes(8), (size_t)8);
    CHECK_EQ(RedzoneCanary::PadBytes(9), (size_t)16);
    CHECK_EQ(RedzoneCanary::PadBytes(INT_MAX - 7), (size_t)0x7FFFFFF8u);
    CHECK_EQ(RedzoneCanary::PadBytes(INT_MAX - 6), (size_t)0x80000000u);
    CHECK_EQ(RedzoneCanary::PadBytes(INT_MAX), (size_t)0x80000000u);
}

UNIT_TEST(redzone_canary_head_dumps_at_most_the_pad) {
    uint8_t buf[32];
    std::memset(buf, 0xAB, sizeof(buf));
    char line[160];
    CHECK(!RedzoneCanary::FormatHead(line, sizeof(line), buf, 0));
    CHECK(RedzoneCanary::FormatHead(line, sizeof(line), buf, 8));
    CHECK_EQ(CountBytes(line), 8);
    CHECK(RedzoneCanary::FormatHead(line, sizeof(line), buf, 24));
    CHECK_EQ(CountBytes(line), 16);
    CHECK(std::strstr(line, "ab ab") != nullptr);
}

#if defined(_WIN32)
UNIT_TEST(redzone_canary_head_stays_inside_an_8_byte_pad) {
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    const size_t page = si.dwPageSize;
    uint8_t* mem = static_cast<uint8_t*>(VirtualAlloc(nullptr, 2 * page, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    CHECK(mem != nullptr);
    if (!mem) return;
    DWORD old = 0;
    CHECK(VirtualProtect(mem + page, page, PAGE_NOACCESS, &old) != 0);
    uint8_t* canary = mem + page - 8;   // an 8-byte pad ending at the guard page
    std::memset(canary, 0xAB, 8);
    canary[3] = 0x41;
    char line[160];
    CHECK(RedzoneCanary::FormatHead(line, sizeof(line), canary, 8));
    CHECK_EQ(CountBytes(line), 8);
    CHECK(std::strstr(line, "ab ab ab 41 ab") != nullptr);
    VirtualFree(mem, 0, MEM_RELEASE);
}
#endif
