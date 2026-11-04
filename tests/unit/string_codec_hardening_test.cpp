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
// MemoryStream::Read does, and wears the binary encoding every BinaryReader stream
// wears, so its size and position are byte counts.
//
// The text-resource path (src/ext/resources/text_reader.cpp) is the other reading
// route: a stream whose encoding is not binary and whose GetSize / GetCurrentPos
// answer 0, under a ReaderWriter that answers a String's length slot with
// (strlen + 1) | 0x80000000 and then fills the chars slot itself. The cap must not
// apply there (review of PR #107): the string_codec_text_stream_* tests pin it.
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

IO::BinaryEncoding g_binaryEncoding;

struct VecStream final : IO::Stream {
    std::vector<uint8_t> bytes;
    uint32_t pos = 0;

    VecStream() {
        _direction = 0;                 // reading
        _encoding = &g_binaryEncoding;  // shared, not owned
    }
    ~VecStream() override { _encoding = nullptr; }   // ~Stream must not free it

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

// The shape of the ext .rsc text reader's IO side (text_reader.cpp TextEncoding /
// TextStream): IsBinary false, GetKind 1, a reading stream that holds nothing and
// answers 0 for both its size and its position.
class RscTextEncoding final : public IO::Encoding {
public:
    bool IsBinary() const override { return false; }
    bool IsText() const override { return true; }
    int GetKind() const override { return 1; }
    IO::ReaderWriter* CreateReader(IO::Serializer&, int) override { return nullptr; }
    IO::ReaderWriter* CreateWriter(IO::Serializer&, int) override { return nullptr; }
};
RscTextEncoding g_rscTextEncoding;

struct RscTextStream final : IO::Stream {
    RscTextStream() {
        _encoding = &g_rscTextEncoding;
        _direction = IO::kReading;
    }
    ~RscTextStream() override { _encoding = nullptr; }
    bool Write(int, const void*) override { return false; }
    bool Read(int, void*) override { return false; }
    bool SetCurrentPos(int) override { return true; }
    uint32_t GetCurrentPos() const override { return 0; }
    uint32_t GetSize() const override { return 0; }
};

// The text reader's answers for one String field: the length slot gets
// (strlen + 1) | 0x80000000, the chars slot gets the text and its NUL.
class RscTextAnswer final : public IO::ReaderWriter {
public:
    RscTextAnswer(IO::Serializer& ser, const char* text) : ReaderWriter(ser), _text(text) {}
    ~RscTextAnswer() override {}
    int GetVersion() const override { return 0; }
    void Skip() override {}
    void SerializeCharArray(int n, char* a, const char*) override {
        ++charCalls;
        lastCharCount = n;
        if (a != nullptr && n > 0)
            std::memcpy(a, _text, static_cast<size_t>(n));
    }
    void SerializeUInt(uint32_t& v, const char*) override {
        ++uintCalls;
        v = static_cast<uint32_t>(std::strlen(_text) + 1) | 0x80000000u;
    }
    void SerializeBoolArray(int, bool*, const char*) override { ++otherCalls; }
    void SerializeFloatArray(int, float*, const char*) override { ++otherCalls; }
    void SerializeUInt64Array(int, uint64_t*, const char*) override { ++otherCalls; }
    void SerializeInt64Array(int, int64_t*, const char*) override { ++otherCalls; }
    void SerializeUIntArray(int, uint32_t*, const char*) override { ++otherCalls; }
    void SerializeIntArray(int, int32_t*, const char*) override { ++otherCalls; }
    void SerializeUShortArray(int, uint16_t*, const char*) override { ++otherCalls; }
    void SerializeShortArray(int, int16_t*, const char*) override { ++otherCalls; }
    void SerializeUCharArray(int, unsigned char*, const char*) override { ++otherCalls; }
    void SerializeSCharArray(int, signed char*, const char*) override { ++otherCalls; }
    void SerializeChar(char&, const char*) override { ++otherCalls; }
    void SerializeBool(bool&, const char*) override { ++otherCalls; }
    void SerializeFloat(float&, const char*) override { ++otherCalls; }
    void SerializeUInt64(uint64_t&, const char*) override { ++otherCalls; }
    void SerializeInt64(int64_t&, const char*) override { ++otherCalls; }
    void SerializeInt(int32_t&, const char*) override { ++otherCalls; }
    void SerializeUShort(uint16_t&, const char*) override { ++otherCalls; }
    void SerializeShort(int16_t&, const char*) override { ++otherCalls; }
    void SerializeUChar(unsigned char&, const char*) override { ++otherCalls; }
    void SerializeSChar(signed char&, const char*) override { ++otherCalls; }
    void SerializeBytes(int, void*, const char*) override { ++otherCalls; }

    int charCalls = 0;
    int lastCharCount = 0;
    int uintCalls = 0;
    int otherCalls = 0;

private:
    const char* _text;
};

// A reading Serializer over an RscTextStream with the text reader's answers bound.
struct RscTextReader {
    HeapSeat seat;
    alignas(8) uint8_t serBytes[16] = {};
    IO::Serializer& ser;
    RscTextAnswer rw;

    RscTextReader(RscTextStream& s, const char* text) : ser(Bind(serBytes, s)), rw(ser, text) {
        ser._readerWriter = &rw;
    }
    static IO::Serializer& Bind(uint8_t* bytes, RscTextStream& s) {
        IO::Serializer& out = *reinterpret_cast<IO::Serializer*>(bytes);
        out._stream = &s;
        return out;
    }
};

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

// Review of PR #107 (blocker): the F-079 / F-080 cap must not touch the ext .rsc text
// reader. Its stream answers 0 for size and position, so a cap on GetSize() -
// GetCurrentPos() rejected every String it set, the chars slot was never asked for,
// and RscText::BuildObject failed for every embedded object with a String field.
UNIT_TEST(string_codec_text_stream_string_still_loads) {
    RscTextStream s;
    System::String str;
    int charCalls = 0;
    int lastCharCount = 0;
    {
        RscTextReader r(s, "Extensions");
        str.Serialize(r.ser);
        charCalls = r.rw.charCalls;
        lastCharCount = r.rw.lastCharCount;
        CHECK_EQ(r.rw.uintCalls, 1);
        CHECK_EQ(r.rw.otherCalls, 0);
    }
    CHECK_EQ(charCalls, 1);
    CHECK_EQ(lastCharCount, 11);
    CHECK_STREQ(str._data, "Extensions");
    ReleaseString(str);
}

// The same through RtIo_SerializeStringArray: every element still asks for its chars.
UNIT_TEST(string_codec_text_stream_string_array_still_loads) {
    RscTextStream s;
    System::String arr[2];
    int charCalls = 0;
    {
        RscTextReader r(s, "Sprites/ext");
        RtIo_SerializeStringArray(&r.ser, 2, arr, nullptr);
        charCalls = r.rw.charCalls;
        CHECK_EQ(r.rw.otherCalls, 0);
    }
    CHECK_EQ(charCalls, 2);
    CHECK_STREQ(arr[0]._data, "Sprites/ext");
    CHECK_STREQ(arr[1]._data, "Sprites/ext");
    ReleaseString(arr[0]);
    ReleaseString(arr[1]);
}
