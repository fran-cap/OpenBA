// ============================================================================
// tests/unit/string_codec_hardening_test.cpp -- review 2026-11-03, group
//   string-text-codec: malformed length words and odd tails in System::String's
//   serializer, RtIo_SerializeStringArray and the IO::TextCodec bodies
// ============================================================================
// Each input is the hostile record a fuzz harness reached the defect with,
// inline as hex bytes after the 12-byte BinaryReader record header:
//   F-023  03 00 00 80 'a' 'b' 'c'    plain length 3, payload with no NUL
//   F-079  00 00 00 81 ...            plain length 0x01000000 over a 7-byte stream
//   F-080  00 00 10 00 ...            legacy UTF-16 length 0x00100000 (2 MB) likewise
//   F-034  41 | 99                    a one-byte format-1 source (the 99 is past the end)
//   F-057  EncodeOne(INT_MIN, fmt 4 / 5)
// The stream below fails a read past its end without touching the destination, as
// MemoryStream::Read does, and reports its size the way every reading stream does.
// ============================================================================
#include "unit.h"

#include "io/Serializer.h"
#include "io/Stream.h"
#include "system/MemoryAllocator.h"
#include "system/String.h"
#include "system/string_internal.h"

#include <climits>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {

struct VecStream final : IO::Stream {
    std::vector<uint8_t> bytes;
    uint32_t pos = 0;

    VecStream() { _direction = 0; }   // reading; _encoding stays null
    ~VecStream() override = default;

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

// RtIo_SerializeStringArray's temporary goes through System::MemoryAllocator, which has
// no singleton on the host: the instrument seat routes it to malloc for the test's span.
void* SeatAlloc(uint64_t size, int) { return std::malloc(static_cast<size_t>(size)); }
void SeatFree(void* p) { std::free(p); }
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

// The record header BinaryReader's constructor consumes: magic, length, version.
const uint8_t kHeader[12] = {0xaa, 0xaa, 0xaa, 0xaa, 0x08, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00};

// A reading Serializer over `s` (its 16 bytes, as the Encoding factory would leave them).
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

void Load(VecStream& s, std::initializer_list<uint8_t> body) {
    s.bytes.assign(kHeader, kHeader + sizeof kHeader);
    s.bytes.insert(s.bytes.end(), body.begin(), body.end());
    s.pos = 0;
}

void ReleaseString(System::String& str) {
    System::StringPrivateAccess::Release(str);
    str._data = nullptr;
}

}  // namespace

// F-023: the plain arm reads n bytes into a block of n-1 characters and never writes
// the terminator, so a payload that does not end in NUL leaves an unterminated String.
UNIT_TEST(string_codec_serialize_plain_payload_is_terminated) {
    VecStream s;
    Load(s, {0x03, 0x00, 0x00, 0x80, 0x61, 0x62, 0x63});
    System::String str;
    {
        Reader r(s);
        str.Serialize(r.ser);
    }
    CHECK(str._data != nullptr);
    if (str._data != nullptr) {
        CHECK_EQ(str._data[2], '\0');
        CHECK_EQ(str.GetLength(), 2);
    }
    ReleaseString(str);
}

// A well-formed record is unchanged: "ab" + NUL.
UNIT_TEST(string_codec_serialize_plain_valid_record_unchanged) {
    VecStream s;
    Load(s, {0x03, 0x00, 0x00, 0x80, 0x61, 0x62, 0x00});
    System::String str;
    {
        Reader r(s);
        str.Serialize(r.ser);
    }
    CHECK_STREQ(str._data, "ab");
    CHECK_EQ(s.pos, 12u + 4u + 3u);
    ReleaseString(str);
}

// F-079: a plain length word of 0x01000000 over a stream with three bytes left must
// not size a 16 MB String; the record is rejected and the String is left empty.
UNIT_TEST(string_codec_serialize_plain_length_past_stream_rejected) {
    VecStream s;
    Load(s, {0x00, 0x00, 0x00, 0x81, 0x61, 0x62, 0x63});
    System::String str;
    {
        Reader r(s);
        str.Serialize(r.ser);
    }
    CHECK(str._data == nullptr);
    ReleaseString(str);
}

// F-080: the legacy UTF-16 arm (bit 31 clear) asked for n*2 bytes with no bound.
UNIT_TEST(string_codec_serialize_wide_length_past_stream_rejected) {
    VecStream s;
    Load(s, {0x00, 0x00, 0x10, 0x00, 0x61, 0x00, 0x00, 0x00});
    System::String str;
    {
        Reader r(s);
        str.Serialize(r.ser);
    }
    CHECK(str._data == nullptr);
    ReleaseString(str);
}

// The legacy UTF-16 arm on a well-formed record still converts: L"ab" + NUL.
UNIT_TEST(string_codec_serialize_wide_valid_record_unchanged) {
    VecStream s;
    Load(s, {0x03, 0x00, 0x00, 0x00, 0x61, 0x00, 0x62, 0x00, 0x00, 0x00});
    System::String str;
    {
        Reader r(s);
        str.Serialize(r.ser);
    }
    CHECK_STREQ(str._data, "ab");
    ReleaseString(str);
}

// F-023 / F-079 / F-080 through RtIo_SerializeStringArray (package string tables).
UNIT_TEST(string_codec_string_array_plain_payload_is_terminated) {
    VecStream s;
    Load(s, {0x03, 0x00, 0x00, 0x80, 0x61, 0x62, 0x63});
    System::String str;
    {
        Reader r(s);
        RtIo_SerializeStringArray(&r.ser, 1, &str, nullptr);
    }
    CHECK(str._data != nullptr);
    if (str._data != nullptr)
        CHECK_EQ(str._data[2], '\0');
    ReleaseString(str);
}

UNIT_TEST(string_codec_string_array_length_past_stream_rejected) {
    VecStream s;
    Load(s, {0x00, 0x00, 0x00, 0x81, 0x61, 0x62, 0x63});
    System::String a;
    {
        Reader r(s);
        RtIo_SerializeStringArray(&r.ser, 1, &a, nullptr);
    }
    CHECK(a._data == nullptr);
    ReleaseString(a);

    Load(s, {0x00, 0x00, 0x10, 0x00, 0x61, 0x00, 0x00, 0x00});
    System::String b;
    {
        Reader r(s);
        RtIo_SerializeStringArray(&r.ser, 1, &b, nullptr);
    }
    CHECK(b._data == nullptr);
    ReleaseString(b);
}

UNIT_TEST(string_codec_string_array_valid_records_unchanged) {
    VecStream s;
    // "ab" plain, then L"cd" legacy UTF-16.
    Load(s, {0x03, 0x00, 0x00, 0x80, 0x61, 0x62, 0x00, 0x03, 0x00, 0x00, 0x00, 0x63, 0x00, 0x64, 0x00, 0x00, 0x00});
    System::String arr[2];
    {
        Reader r(s);
        RtIo_SerializeStringArray(&r.ser, 2, arr, nullptr);
    }
    CHECK_STREQ(arr[0]._data, "ab");
    CHECK_STREQ(arr[1]._data, "cd");
    ReleaseString(arr[0]);
    ReleaseString(arr[1]);
}

// F-034: format 1 read its second byte with no bound; an odd tail is malformed, not a
// read of the byte past `end`.
UNIT_TEST(string_codec_decode_fmt1_odd_tail_is_malformed) {
    const uint8_t buf[2] = {0x41, 0x99};
    int32_t cp = 0;
    const int32_t used = RtIo_TextCodec_DecodeOne(1, buf, buf + 1, &cp, 1);
    CHECK_EQ(cp, -1);
    CHECK_EQ(used, 1);

    // A whole unit still decodes.
    cp = 0;
    CHECK_EQ(RtIo_TextCodec_DecodeOne(1, buf, buf + 2, &cp, 1), 2);
    CHECK_EQ(cp, 0x9941);

    // ConvertText over the one-byte source (the fuzz input 0001020041) produces nothing.
    uint8_t out[8] = {};
    CHECK_EQ(RtIo_TextCodec_ConvertText(1, 0, buf, 1, 2, 0, 0, out, sizeof out), 0);
}

// F-057: the surrogate test keeps retail's 32-bit wrap for a negative codepoint
// (INT_MIN - 0xD800 is not a surrogate, so retail stores the low 16 bits).
UNIT_TEST(string_codec_encode_utf16_negative_codepoint_wraps) {
    uint8_t d[4] = {0xee, 0xee, 0xee, 0xee};
    CHECK_EQ(RtIo_TextCodec_EncodeOne(INT_MIN, d, 4), 2);
    CHECK_EQ(d[0], 0x00);
    CHECK_EQ(d[1], 0x00);
    CHECK_EQ(RtIo_TextCodec_EncodeOne(INT_MIN, d, 5), 2);
    CHECK_EQ(RtIo_TextCodec_EncodeOne(0xD800, d, 4), 0);
    CHECK_EQ(RtIo_TextCodec_EncodeOne(0xDFFF, d, 5), 0);
    CHECK_EQ(RtIo_TextCodec_EncodeOne(0xE000, d, 4), 2);
}
