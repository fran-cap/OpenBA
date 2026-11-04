// ============================================================================
// tests/fuzz/large_packages_fuzz.cpp -- Ext::LargePackages::Unwrap over arbitrary package indexes
// ============================================================================
// fix.large_packages rebuilds 64-bit entry offsets from a > 2 GiB package's uint32 index (a mod
// .pkm: outside bytes). P0_MAP §9. The caller (large_packages.cpp OnMounted) passes the file's
// size (> kRetailLimit), the index blob's extent read from the header (begin: a uint32, end =
// begin + 4 + a non-negative int32) and the entries' { offset, size } pairs; the target builds the
// same ranges from the input.
// Input: [u32 fileSize in 64 KB units][u32 index begin][u32 index blob length] then { u32 offset,
// u32 size } per entry. Oracle (trap): an Ok result describes entries inside the file, after the
// header word, in order, not overlapping each other.
// ============================================================================
#include "ext/large_packages/large_packages_unwrap.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

using namespace Ext::LargePackages;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size < 12 || size > 0x40000)
        return 0;
    uint32_t units, ib, blob;
    std::memcpy(&units, data, 4);
    std::memcpy(&ib, data + 4, 4);
    std::memcpy(&blob, data + 8, 4);
    const int64_t fileSize = kRetailLimit + 1 + static_cast<int64_t>(units) * 65536;
    const int64_t indexBegin = ib;
    const int64_t indexEnd = indexBegin + 4 + static_cast<int64_t>(blob & 0x7FFFFFFFu);
    const size_t n = (size - 12) / 8;
    std::vector<uint32_t> offs(n), sizes(n);
    for (size_t i = 0; i < n; ++i) {
        std::memcpy(&offs[i], data + 12 + i * 8, 4);
        std::memcpy(&sizes[i], data + 12 + i * 8 + 4, 4);
    }
    std::vector<int64_t> wide;
    const Result r = Unwrap(offs.data(), sizes.data(), static_cast<int32_t>(n), fileSize, indexBegin, indexEnd, wide);
    if (r.verdict == Verdict::Ok) {
        if (wide.size() != n)
            __builtin_trap();
        int64_t prevEnd = 4;
        for (size_t i = 0; i < n; ++i) {
            const int64_t b = wide[i];
            const int64_t e = b + sizes[i];
            if (b < prevEnd || e > fileSize || static_cast<uint32_t>(b) != offs[i])
                __builtin_trap();
            prevEnd = e;
        }
    } else if (!wide.empty()) {
        __builtin_trap();
    }
    VerdictText(r.verdict);
    return 0;
}
