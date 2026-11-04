// ============================================================================
// tests/unit/mull_gaps_string_test.cpp -- System::String cases the Mull run found unchecked
// ============================================================================
// Each test kills mutants of src/engine/system/string.cpp that survived the first
// tools/mull/run.sh run (tools/mull/README.md): the expectations are the retail
// behaviour the body's comments already document (ToFloat / ToInteger's digit and
// exponent loops, the stored length and refcount every concatenation leaves, the
// shared-buffer no-op of Set, FormatParamList's '9' and its trailing literal,
// FormatIndexedList reading only the arguments passed, and the legacy UTF-16 decode).
// ============================================================================
#include "unit.h"

#include "io/Serializer.h"
#include "io/Stream.h"
#include "system/MemoryAllocator.h"
#include "system/String.h"
#include "system/string_internal.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <vector>

using System::String;

namespace {

int RefCount(const String& s) {
    return s._data ? reinterpret_cast<const int32_t*>(s._data)[-1] : 0;
}

int StoredLength(const String& s) {
    return s._data ? reinterpret_cast<const int32_t*>(s._data)[-2] : 0;
}

}  // namespace

// The digit loops accept exactly '0'..'9', and every digit scales what came before.
UNIT_TEST(mull_string_to_float_multi_digit_and_digit_bounds) {
    CHECK_EQ(String::ToFloat("12"), 12.0f);
    CHECK_EQ(String::ToFloat("305.25"), 305.25f);
    CHECK_EQ(String::ToFloat("9"), 9.0f);
    CHECK_EQ(String::ToFloat("1/"), 1.0f);       // '/' (0x2f) ends the integer part
    CHECK_EQ(String::ToFloat("1:"), 1.0f);       // ':' (0x3a) ends it too
    CHECK_EQ(String::ToFloat("1.5/"), 1.5f);     // and the fraction
    CHECK_EQ(String::ToFloat("1.5:"), 1.5f);
}

// The exponent: '/' is no exponent digit, a '/' after digits ends it, and 19 is the last
// exponent the 1e8 / 10 chunks handle alone (20 and up take the 1e20 chunk first).
UNIT_TEST(mull_string_to_float_exponent_digits_and_chunks) {
    CHECK_EQ(String::ToFloat("1e/"), 1.0f);
    CHECK_EQ(String::ToFloat("1e2/"), 100.0f);
    CHECK_EQ(String::ToFloat("1e19"), 1.0e19f);
    CHECK_EQ(String::ToFloat("1e20"), 1.0e20f);
    CHECK_EQ(String::ToFloat("1e7"), 1.0e7f);
    CHECK_EQ(String::ToFloat("1e8"), 1.0e8f);
}

UNIT_TEST(mull_string_to_integer_zero_and_nine_are_digits) {
    CHECK_EQ(String::ToInteger("105"), 105);
    CHECK_EQ(String::ToInteger("90"), 90);
    CHECK_EQ(String::ToInteger("-09"), -9);
}

// Every concatenation leaves a buffer whose header holds the real length and refcount 1.
UNIT_TEST(mull_string_concat_headers) {
    String cs = String("ab") + "cde";
    CHECK_EQ(StoredLength(cs), 5);
    CHECK_EQ(RefCount(cs), 1);

    String ch = String("ab") + 'c';
    CHECK_EQ(StoredLength(ch), 3);
    CHECK_EQ(RefCount(ch), 1);
    CHECK(ch == "abc");
}

// RemoveAt(0) is SubString(1, len - 1): the result is exactly len - 1 characters long.
UNIT_TEST(mull_string_remove_at_front_stored_length) {
    String s("abcd");
    String r = s.RemoveAt(0);
    CHECK(r == "bcd");
    CHECK_EQ(StoredLength(r), 3);
}

// Set to text equal to a SHARED buffer's keeps the share (retail does not un-share).
UNIT_TEST(mull_string_set_equal_text_keeps_the_share) {
    String a("same");
    String b(a);
    a.Set("same");
    CHECK(a._data == b._data);
    CHECK_EQ(RefCount(a), 2);
}

// '@9' is a parameter: the digit range is inclusive at both ends.
UNIT_TEST(mull_string_format_param_nine) {
    const char* argv[10] = {"0", "1", "2", "3", "4", "5", "6", "7", "8", "N"};
    String r = String::FormatIndexedList("x@9y", argv, 10);
    CHECK(r == "xNy");
    CHECK_EQ(StoredLength(r), 3);
}

// The trailing literal run is SubString(segStart, len - 1): no terminator copied into it.
UNIT_TEST(mull_string_format_trailing_literal_length) {
    const char* argv[1] = {"A"};
    String r = String::FormatIndexedList("@0bc", argv, 1);
    CHECK(r == "Abc");
    CHECK_EQ(StoredLength(r), 3);
    String plain = String::FormatIndexedList("ab", argv, 1);
    CHECK_EQ(StoredLength(plain), 2);
    String p = String::FormatIndexedList("@0", argv, 1);
    CHECK_EQ(StoredLength(p), 1);
}

// Only the `argc` arguments passed are read; a parameter past them stays empty (F-125).
UNIT_TEST(mull_string_format_indexed_reads_only_argc) {
    const char* argv[2] = {"A", "B"};
    String r = String::FormatIndexedList("@0@1", argv, 1);
    CHECK(r == "A");
    CHECK_EQ(StoredLength(r), 1);
}

// ---- the legacy UTF-16 branch of String::Serialize -------------------------------------

namespace {

IO::BinaryEncoding g_binaryEncoding;

struct VecStream final : IO::Stream {
    std::vector<uint8_t> bytes;
    uint32_t pos = 0;

    VecStream() {
        _direction = 0;
        _encoding = &g_binaryEncoding;
    }
    ~VecStream() override { _encoding = nullptr; }

    bool Write(int, const void*) override { return false; }
    bool Read(int n, void* dst) override {
        if (n < 0 || static_cast<uint64_t>(pos) + static_cast<uint32_t>(n) > bytes.size())
            return false;
        std::memcpy(dst, bytes.data() + pos, static_cast<size_t>(n));
        pos += static_cast<uint32_t>(n);
        return true;
    }
    bool SetCurrentPos(int p) override {
        pos = static_cast<uint32_t>(p);
        return true;
    }
    uint32_t GetCurrentPos() const override { return pos; }
    uint32_t GetSize() const override { return static_cast<uint32_t>(bytes.size()); }
};

void* SeatAlloc(uint64_t size, int) {
    return std::malloc(static_cast<size_t>(size));
}

void SeatFree(void* p) {
    std::free(p);
}

struct HeapSeat {
    HeapSeat() {
        System::g_memoryAllocatorInstrument.memAlloc = &SeatAlloc;
        System::g_memoryAllocatorInstrument.memFree = &SeatFree;
    }
    ~HeapSeat() {
        System::g_memoryAllocatorInstrument.memAlloc = nullptr;
        System::g_memoryAllocatorInstrument.memFree = nullptr;
    }
};

const uint8_t kHeader[12] = {0xaa, 0xaa, 0xaa, 0xaa, 0x08, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00};

struct Reader {
    HeapSeat seat;
    alignas(8) uint8_t serBytes[16] = {};
    IO::Serializer& ser;
    IO::BinaryReader rw;

    explicit Reader(VecStream& s) : ser(Bind(serBytes, s)), rw(ser, 0) {
        ser._readerWriter = &rw;
    }
    static IO::Serializer& Bind(uint8_t* bytes, VecStream& s) {
        IO::Serializer& out = *reinterpret_cast<IO::Serializer*>(bytes);
        out._stream = &s;
        return out;
    }
};

// A legacy record: the length word (bit 31 clear) counts UTF-16 units including the NUL.
void LoadLegacy(VecStream& s, std::initializer_list<uint16_t> units) {
    s.bytes.assign(kHeader, kHeader + sizeof kHeader);
    const uint32_t n = static_cast<uint32_t>(units.size());
    for (int i = 0; i < 4; ++i)
        s.bytes.push_back(static_cast<uint8_t>(n >> (8 * i)));
    for (uint16_t u : units) {
        s.bytes.push_back(static_cast<uint8_t>(u & 0xff));
        s.bytes.push_back(static_cast<uint8_t>(u >> 8));
    }
    s.pos = 0;
}

String DecodeLegacy(std::initializer_list<uint16_t> units) {
    VecStream s;
    LoadLegacy(s, units);
    Reader r(s);
    String str;
    str.Serialize(r.ser);
    return str;
}

}  // namespace

// U+0080 is the first two-byte code point; D800 DC00 is the first surrogate pair (U+10000).
UNIT_TEST(mull_string_legacy_utf16_boundaries) {
    String two = DecodeLegacy({0x0080, 0x0000});
    CHECK_EQ(StoredLength(two), 2);
    CHECK(two._data && static_cast<uint8_t>(two._data[0]) == 0xC2 && static_cast<uint8_t>(two._data[1]) == 0x80);

    String pair = DecodeLegacy({0xD800, 0xDC00, 0x0000});
    CHECK_EQ(StoredLength(pair), 4);
    CHECK(pair._data && std::memcmp(pair._data, "\xF0\x90\x80\x80", 4) == 0);

    // A pair past the first: U+10401.
    String pair2 = DecodeLegacy({0xD801, 0xDC01, 0x0000});
    CHECK_EQ(StoredLength(pair2), 4);
    CHECK(pair2._data && std::memcmp(pair2._data, "\xF0\x90\x90\x81", 4) == 0);

    // U+0800 is the first three-byte code point.
    String three = DecodeLegacy({0x0800, 0x0000});
    CHECK_EQ(StoredLength(three), 3);
    CHECK(three._data && std::memcmp(three._data, "\xE0\xA0\x80", 3) == 0);

    // A low surrogate on its own is a lone unit: three bytes, never paired with what precedes it.
    String lone = DecodeLegacy({0x0041, 0xDC00, 0x0000});
    CHECK_EQ(StoredLength(lone), 4);
    CHECK(lone._data && lone._data[0] == 'A');

    // DC00 is not a high surrogate, and E000 is not a low one: no pair, three bytes each.
    String lows = DecodeLegacy({0xDC00, 0xDC00, 0x0000});
    CHECK_EQ(StoredLength(lows), 6);
    String highThenPrivate = DecodeLegacy({0xD800, 0xE000, 0x0000});
    CHECK_EQ(StoredLength(highThenPrivate), 6);
}

// The trailing NUL unit is not decoded, an embedded U+0000 is (one byte).
UNIT_TEST(mull_string_legacy_utf16_nul_units) {
    String plain = DecodeLegacy({0x0061, 0x0062, 0x0000});
    CHECK_EQ(StoredLength(plain), 2);
    CHECK(plain == "ab");

    String embedded = DecodeLegacy({0x0061, 0x0000, 0x0062, 0x0000});
    CHECK_EQ(StoredLength(embedded), 3);
    CHECK(embedded._data && embedded._data[0] == 'a' && embedded._data[1] == '\0' && embedded._data[2] == 'b');
}

// The F-080 cap sizes a legacy record at two bytes per unit: a length whose units fit the
// stream only at one byte each is rejected and leaves the String empty.
UNIT_TEST(mull_string_legacy_utf16_cap_counts_two_bytes_per_unit) {
    VecStream s;
    LoadLegacy(s, {0x0061, 0x0062, 0x0063, 0x0064, 0x0000});
    // Keep 5 of the 10 payload bytes: enough at one byte per unit, too few at two.
    s.bytes.resize(sizeof kHeader + 4 + 5);
    Reader r(s);
    String str;
    str.Serialize(r.ser);
    CHECK(str._data == nullptr);
}
