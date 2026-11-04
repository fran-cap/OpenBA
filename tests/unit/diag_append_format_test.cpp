// tests/unit/diag_append_format_test.cpp -- Diag::AppendFormat (src/diag/append_format.h), the
// bounded line builder of scene_watch's LodCensus dump (review 2026-11-03, F-011).
//
// The buffer sits in a struct ahead of a guard block: a write past the buffer lands in the guard
// (no crash), and the test reads it back.

#include "unit.h"

#include "append_format.h"

#include <cstdint>
#include <cstring>

namespace {

template <size_t N>
struct Guarded {
    char    buf[N];
    uint8_t guard[256];
};

template <size_t N>
void Arm(Guarded<N>* g) {
    std::memset(g->buf, 'x', sizeof(g->buf));
    std::memset(g->guard, 0xA5, sizeof(g->guard));
}

template <size_t N>
bool GuardIntact(const Guarded<N>& g) {
    for (uint8_t b : g.guard) {
        if (b != 0xA5) return false;
    }
    return true;
}

}  // namespace

// Three 20-character fields into 32 bytes: the second is cut off, the third has no room.
UNIT_TEST(diag_append_format_stops_at_a_truncated_field) {
    Guarded<32> g;
    Arm(&g);
    int off = 0;
    for (int k = 0; k < 3; ++k) {
        off = Diag::AppendFormat(g.buf, sizeof(g.buf), off, "%s", "ABCDEFGHIJKLMNOPQRST");
    }
    CHECK(GuardIntact(g));
    CHECK_EQ(off, 31);
    CHECK_EQ(std::strlen(g.buf), size_t(31));
    CHECK_EQ(std::strncmp(g.buf, "ABCDEFGHIJKLMNOPQRSTABCDEFGHIJK", 31), 0);
}

// The census line itself (scene_watch.cpp, the frame-120 dump): 8 levels whose distance is 1e30,
// the input of the ledger's ASan replay (stack-buffer-overflow, WRITE of size 87 past buf[512]).
UNIT_TEST(diag_append_format_lod_census_line_with_absurd_distances) {
    Guarded<512> g;
    Arm(&g);
    int off = 0;
    for (int k = 0; k < 8; ++k) {
        off = Diag::AppendFormat(g.buf, sizeof(g.buf), off, "%sL%d{d=%.1f tris=%d vb=%p ib=%p}",
                                 k ? " " : "", k, 1.0e30, 123456,
                                 reinterpret_cast<void*>(uintptr_t(0x123456789abcull)),   // CAST-OK: test
                                 reinterpret_cast<void*>(uintptr_t(0x123456789abcull)));  // CAST-OK: test
    }
    CHECK(GuardIntact(g));
    CHECK_EQ(off, 511);
    CHECK_EQ(std::strlen(g.buf), size_t(511));
}

// A field that fits leaves the offset at its end; an empty format leaves it unchanged.
UNIT_TEST(diag_append_format_fits) {
    Guarded<16> g;
    Arm(&g);
    int off = Diag::AppendFormat(g.buf, sizeof(g.buf), 0, "%d", 42);
    off = Diag::AppendFormat(g.buf, sizeof(g.buf), off, "%s", "");
    off = Diag::AppendFormat(g.buf, sizeof(g.buf), off, ",%d", 7);
    CHECK(GuardIntact(g));
    CHECK_EQ(off, 4);
    CHECK_STREQ(g.buf, "42,7");
}
