// ============================================================================
// tests/unit/review_imagebuffer_fields_test.cpp -- the serialized ImageBuffer enum fields
// (review 2026-11-03, F-028 / F-062 / F-068).
//
// `Video::ImageBuffer::Serialize` reads `_kind`, `_format`, `_addressU/V/W` and `_filter` with
// `SerializeUnsigned` and never range-checks them, so a .rsc in a mod .pkm hands any uint32 to:
//   * PlatformSerialize's generated / rendered test, `kind - 4` in signed int (F-062: signed
//     overflow for kind 0x80000000);
//   * the DX11 create's `kFormatToDxgi[_format]` / `kFormatBytes[_format]`, 8 entries (F-028);
//   * the DX11 sampler's `addressTable[2]` / `filterTable[3]` stack tables (F-068).
// The DX11 bodies link only against D3D11, so the predicates and table indexes they now go
// through (ImageBuffer.h) are what is tested here, on the values the fuzz inputs carry.
// ============================================================================
#include "unit.h"

#include "video/ImageBuffer.h"

#include <cstdint>

namespace {

using Video::ImageBuffer;

uint32_t ReadLe32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

// rsc_serialize/imagebuffer-format-8 (61 bytes), offset 0x19: the `_kind` and `_format` words
// (kind 2 = a texture seeded from the file, format 8 = one past the DX11 tables).
const uint8_t kFormat8Fields[8] = { 0x02, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00 };

// rsc_serialize/imagebuffer-kind-intmin (57 bytes), offset 0x19: the `_kind` word, 0x80000000.
const uint8_t kKindIntMin[4] = { 0x00, 0x00, 0x00, 0x80 };

// The sampler fields of a hostile file: _addressU/V/W = 2, 3, 0xffffffff; _filter = 3, 0xffffffff.
const uint8_t kSamplerFields[20] = {
    0x02, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff,
    0x03, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff,
};

// F-062: evaluated as a constant expression, so a signed overflow in the predicate is a
// compile error rather than a silent wrap.
constexpr bool kIntMinIsGenerated = ImageBuffer::KindIsGeneratedOrRendered(INT32_MIN);
static_assert(!kIntMinIsGenerated, "kind 0x80000000 is neither generated nor rendered");
static_assert(ImageBuffer::KindIsGeneratedOrRendered(4), "kind 4 = generated");
static_assert(ImageBuffer::KindIsGeneratedOrRendered(5), "kind 5 = rendered");
static_assert(!ImageBuffer::KindIsGeneratedOrRendered(3), "kind 3 = dynamic");
static_assert(!ImageBuffer::KindIsGeneratedOrRendered(6), "kind 6");
static_assert(!ImageBuffer::KindIsGeneratedOrRendered(INT32_MAX), "kind 0x7fffffff");

}  // namespace

UNIT_TEST(review_imagebuffer_kind_generated_or_rendered) {
    const auto kind = static_cast<ImageBuffer::Usage>(ReadLe32(kKindIntMin));
    CHECK(!ImageBuffer::KindIsGeneratedOrRendered(kind));
    for (int32_t k = 0; k < 8; ++k)
        CHECK_EQ(ImageBuffer::KindIsGeneratedOrRendered(k), k == 4 || k == 5);
}

UNIT_TEST(review_imagebuffer_format_in_range) {
    const auto format = static_cast<ImageBuffer::Format>(ReadLe32(kFormat8Fields + 4));
    CHECK(!ImageBuffer::FormatInRange(format));
    CHECK(!ImageBuffer::FormatInRange(-1));
    CHECK(!ImageBuffer::FormatInRange(INT32_MIN));
    // Every retail format keeps its table entry.
    for (int32_t f = 0; f < 8; ++f)
        CHECK(ImageBuffer::FormatInRange(f));
}

UNIT_TEST(review_imagebuffer_sampler_table_indexes) {
    for (int i = 0; i < 3; ++i) {
        const auto mode = static_cast<ImageBuffer::AddressMode>(ReadLe32(kSamplerFields + 4 * i));
        const int32_t index = ImageBuffer::AddressTableIndex(mode);
        CHECK(index >= 0);
        CHECK(static_cast<uint32_t>(index) < ImageBuffer::kAddressModeCount);
    }
    for (int i = 3; i < 5; ++i) {
        const auto filter = static_cast<ImageBuffer::FilterMode>(ReadLe32(kSamplerFields + 4 * i));
        const int32_t index = ImageBuffer::FilterTableIndex(filter);
        CHECK(index >= 0);
        CHECK(static_cast<uint32_t>(index) < ImageBuffer::kFilterModeCount);
    }
    // Valid values index their own entry: retail's sampler on every valid file.
    CHECK_EQ(ImageBuffer::AddressTableIndex(0), 0);
    CHECK_EQ(ImageBuffer::AddressTableIndex(1), 1);
    CHECK_EQ(ImageBuffer::FilterTableIndex(0), 0);
    CHECK_EQ(ImageBuffer::FilterTableIndex(1), 1);
    CHECK_EQ(ImageBuffer::FilterTableIndex(2), 2);
}
