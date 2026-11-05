// Pure-logic test of the Metal backend's draw ranges (src/render/metal/draw_bounds.h): the int4 widening
// reads only bytes inside the bound vertex stream, and an indexed draw only reaches Metal when its index
// range lies inside the index buffer. The values a test feeds are the ones a crafted VertexLayout or
// mesh resource can carry.
#include "unit.h"

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>

#include "../../src/render/metal/draw_bounds.h"

namespace {
// The last byte index (relative to the buffer start) the widening loop reads for `n` vertices,
// as backend.mm's `src[v * stride + offset + k]` with src = contents + begin.
size_t LastByteRead(size_t n, size_t begin, size_t stride, size_t offset) {
    return begin + (n - 1) * stride + offset + 3;
}
}  // namespace

UNIT_TEST(metal_widen_count_sane_layouts) {
    CHECK_EQ(VideoMetal::WidenCount(64, 0, 4, 0), size_t(16));
    CHECK_EQ(VideoMetal::WidenCount(64, 0, 16, 12), size_t(4));
    CHECK_EQ(VideoMetal::WidenCount(64, 16, 16, 12), size_t(3));
    CHECK_EQ(VideoMetal::WidenCount(64, 0, 0, 0), size_t(0));
    CHECK_EQ(VideoMetal::WidenCount(64, 64, 4, 0), size_t(0));
    CHECK_EQ(VideoMetal::WidenCount(64, 100, 4, 0), size_t(0));
}

// Every byte a non-zero count lets the loop read lies inside the buffer.
UNIT_TEST(metal_widen_count_never_reads_past_the_stream) {
    const size_t length = 64;
    for (size_t stride : { size_t(4), size_t(8), size_t(16), size_t(20) }) {
        for (size_t begin : { size_t(0), size_t(4), size_t(17) }) {
            for (size_t offset = 0; offset < 3 * stride; ++offset) {
                const size_t n = VideoMetal::WidenCount(length, begin, stride, offset);
                if (n) {
                    CHECK(LastByteRead(n, begin, stride, offset) < length);
                }
            }
        }
    }
}

// The finding's cases (review F-320 / F-322): a UChar4 at offset 8 in a 4-byte vertex, at offset 16 in
// a 16-byte one, and a negative layout offset (stored into an NSUInteger, so it wraps).
UNIT_TEST(metal_widen_count_rejects_an_offset_outside_the_vertex) {
    CHECK_EQ(VideoMetal::WidenCount(64, 0, 4, 8), size_t(0));
    CHECK_EQ(VideoMetal::WidenCount(64, 0, 16, 16), size_t(0));
    CHECK_EQ(VideoMetal::WidenCount(64, 0, 16, 13), size_t(0));
    CHECK_EQ(VideoMetal::WidenCount(64, 0, 16, size_t(int64_t(-5000))), size_t(0));
    CHECK_EQ(VideoMetal::WidenCount(64, 0, 2, 0), size_t(0));   // a 2-byte vertex cannot hold a UChar4
}

UNIT_TEST(metal_index_range_sane_draws) {
    size_t offset = 0;
    CHECK(VideoMetal::IndexRange(0, 0, 3, 6, &offset));
    CHECK_EQ(offset, size_t(0));
    CHECK(VideoMetal::IndexRange(4, 3, 3, 16, &offset));
    CHECK_EQ(offset, size_t(10));
    CHECK(!VideoMetal::IndexRange(4, 3, 4, 16, &offset));
    CHECK(!VideoMetal::IndexRange(0, 0, 4, 6, &offset));
}

// The finding's cases (review F-321): a negative start index or index-buffer offset, or a negative
// count, wrapped the unsigned sum back inside the buffer and passed the old check.
UNIT_TEST(metal_index_range_rejects_negative_values) {
    size_t offset = 12345;
    CHECK(!VideoMetal::IndexRange(0, -1, 3, 64, &offset));
    CHECK(!VideoMetal::IndexRange(-2, 0, 3, 64, &offset));
    CHECK(!VideoMetal::IndexRange(8, 0, -1, 64, &offset));
    CHECK(!VideoMetal::IndexRange(0, std::numeric_limits<int>::min(), 3, 64, &offset));
    CHECK(!VideoMetal::IndexRange(std::numeric_limits<int>::max(), std::numeric_limits<int>::max(), 3, 64, &offset));
    CHECK_EQ(offset, size_t(12345));   // untouched when refused
}
