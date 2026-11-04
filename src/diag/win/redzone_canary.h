// src/diag/win/redzone_canary.h -- the redzone shim's tail-pad arithmetic and canary dump line
// (redzone_alloc.cpp), pure so tests/unit/redzone_canary_test.cpp reaches it.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace RedzoneCanary {

// BANISHED_REDZONE_PAD bytes rounded up to an 8-byte multiple; 0 = no tail pad. Rounded in
// size_t: `requested + 7` in int overflows for the top 7 values (ClampedAtoi's INT_MAX).
inline size_t PadBytes(int requested) {
    if (requested <= 0) return 0;
    return ((size_t)requested + 7) & ~(size_t)7;
}

// "[REDZONE]   canary head: ab ab ..." for the first bytes of the canary at `c`: at most 16,
// and never more than the `pad` bytes the shim allocated (an 8-byte pad has 8; reading on
// reaches the next block's tag, or unmapped memory at the end of a range). The line stays
// empty (returns false) when there is no canary.
inline bool FormatHead(char* out, size_t outLen, const uint8_t* c, size_t pad) {
    if (pad == 0 || outLen == 0) return false;
    const size_t n = pad < 16 ? pad : 16;
    int at = std::snprintf(out, outLen, "[REDZONE]   canary head:");
    for (size_t i = 0; i < n && at > 0 && (size_t)at < outLen; ++i) {
        at += std::snprintf(out + at, outLen - at, " %02x", c[i]);
    }
    if (at > 0 && (size_t)at < outLen) std::snprintf(out + at, outLen - at, "\n");
    return true;
}

}  // namespace RedzoneCanary
