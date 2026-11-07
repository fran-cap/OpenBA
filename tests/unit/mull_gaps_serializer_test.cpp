// ============================================================================
// tests/unit/mull_gaps_serializer_test.cpp -- serializer cases the Mull run found unchecked
// ============================================================================
// The first tools/mull/run.sh run (tools/mull/README.md) found three bodies whose ordinary
// path no test checked: IO::MemoryStream::Read / Write inside and at the end of the
// buffer (only the F-016 / F-053 rejections were tested), RtIo_TextCodec_ConvertText on
// a UTF-16BE source and an embedded U+0000, and RtIo_SerializeStringArray's stored
// lengths and its legacy UTF-16 path past the "cd" record of string_codec_hardening.
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

// ---- IO::MemoryStream ------------------------------------------------------------------

UNIT_TEST(mull_memorystream_read_inside_and_to_the_end) {
    uint8_t blob[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    IO::MemoryStream ms;
    ms._data = blob;
    ms._size = 8;
    uint8_t out[8] = {};
    CHECK(ms.SetCurrentPos(2));
    CHECK(ms.Read(6, out));                  // ends exactly at _size
    CHECK(out[0] == 3 && out[5] == 8);
    CHECK_EQ(ms.GetCurrentPos(), 8u);
    CHECK(ms.SetCurrentPos(5));
    CHECK(!ms.Read(4, out));                 // one byte past the end
    CHECK_EQ(ms.GetCurrentPos(), 5u);
    ms._data = nullptr;
}

UNIT_TEST(mull_memorystream_write_inside_and_to_the_end) {
    uint8_t blob[8] = {};
    IO::MemoryStream ms;
    ms._data = blob;
    ms._size = 8;
    const uint8_t in[8] = {9, 8, 7, 6, 5, 4, 3, 2};
    CHECK(ms.SetCurrentPos(0));
    CHECK(ms.Write(3, in));                  // from cursor 0
    CHECK(blob[0] == 9 && blob[1] == 8 && blob[2] == 7);
    CHECK_EQ(ms.GetCurrentPos(), 3u);
    CHECK(ms.Write(5, in));                  // ends exactly at _size
    CHECK(blob[3] == 9 && blob[7] == 5);
    CHECK_EQ(ms.GetCurrentPos(), 8u);
    CHECK(ms.SetCurrentPos(6));
    CHECK(!ms.Write(3, in));                 // one byte past the end: no write, no move
    CHECK(blob[6] == 6);
    CHECK_EQ(ms.GetCurrentPos(), 6u);
    ms._data = nullptr;
}

// ---- RtIo_TextCodec_ConvertText -----------------------------------------------------------

// Format 5 (UTF-16BE) is a decodable source: `fmt <= 5` includes it.
UNIT_TEST(mull_convert_text_utf16be_source) {
    const uint8_t src[4] = {0x00, 0x41, 0x00, 0x42};
    uint8_t out[8] = {};
    const int32_t n = RtIo_TextCodec_ConvertText(IO::Text::Format::Utf16Be, 0, src, 4,
                                                 IO::Text::Format::Utf8, 0, 0, out, 8);
    CHECK_EQ(n, 2);
    CHECK(out[0] == 'A' && out[1] == 'B');
}

// U+0000 inside the source is a code point like any other (only cp < 0 is skipped).
UNIT_TEST(mull_convert_text_embedded_nul_is_converted) {
    const uint8_t src[3] = {'a', 0x00, 'b'};
    uint8_t out[8] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    const int32_t n = RtIo_TextCodec_ConvertText(IO::Text::Format::Latin1, 0, src, 3,
                                                 IO::Text::Format::Utf8, 0, 0, out, 8);
    CHECK_EQ(n, 3);
    CHECK(out[0] == 'a' && out[1] == 0x00 && out[2] == 'b');
}

// Format 5 is an encodable destination too, and output that fits the capacity exactly
// is copied in full (only `produced > dstCap` skips the copy).
UNIT_TEST(mull_convert_text_utf16be_destination_exact_fit) {
    const uint8_t src[2] = {'A', 'B'};
    uint8_t out[4] = {0xff, 0xff, 0xff, 0xff};
    const int32_t n = RtIo_TextCodec_ConvertText(IO::Text::Format::Latin1, 0, src, 2,
                                                 IO::Text::Format::Utf16Be, 0, 0, out, 4);
    CHECK_EQ(n, 4);
    CHECK(out[0] == 0x00 && out[1] == 'A' && out[2] == 0x00 && out[3] == 'B');
}

// ---- RtIo_SerializeStringArray --------------------------------------------------------------

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

void Load(VecStream& s, std::initializer_list<uint8_t> body) {
    s.bytes.assign(kHeader, kHeader + sizeof kHeader);
    s.bytes.insert(s.bytes.end(), body.begin(), body.end());
    s.pos = 0;
}

int StoredLength(const System::String& s) {
    return s._data ? reinterpret_cast<const int32_t*>(s._data)[-2] : 0;
}

void ReleaseString(System::String& str) {
    System::StringPrivateAccess::Release(str);
    str._data = nullptr;
}

}  // namespace

// The stored length of a loaded String is its character count, plain and legacy alike.
UNIT_TEST(mull_string_array_stored_lengths) {
    VecStream s;
    // "abc" plain, then L"de" legacy UTF-16 (length word 3, bit 31 clear).
    Load(s, {0x04, 0x00, 0x00, 0x80, 'a', 'b', 'c', 0x00,
             0x03, 0x00, 0x00, 0x00, 'd', 0x00, 'e', 0x00, 0x00, 0x00});
    System::String arr[2];
    {
        Reader r(s);
        RtIo_SerializeStringArray(&r.ser, 2, arr, nullptr);
    }
    CHECK_STREQ(arr[0]._data, "abc");
    CHECK_EQ(StoredLength(arr[0]), 3);
    CHECK_STREQ(arr[1]._data, "de");
    CHECK_EQ(StoredLength(arr[1]), 2);
    ReleaseString(arr[0]);
    ReleaseString(arr[1]);
}

// Legacy UTF-16: an embedded U+0000 is transcoded (one byte); only the trailing unit is not.
UNIT_TEST(mull_string_array_legacy_embedded_nul) {
    VecStream s;
    Load(s, {0x04, 0x00, 0x00, 0x00, 'x', 0x00, 0x00, 0x00, 'y', 0x00, 0x00, 0x00});
    System::String arr[1];
    {
        Reader r(s);
        RtIo_SerializeStringArray(&r.ser, 1, arr, nullptr);
    }
    CHECK_EQ(StoredLength(arr[0]), 3);
    CHECK(arr[0]._data && arr[0]._data[0] == 'x' && arr[0]._data[1] == '\0' && arr[0]._data[2] == 'y');
    ReleaseString(arr[0]);
}

// The F-079 cap counts two bytes per legacy unit: units that fit only at one byte each
// are rejected and the String is left empty.
UNIT_TEST(mull_string_array_legacy_cap_counts_two_bytes_per_unit) {
    VecStream s;
    Load(s, {0x05, 0x00, 0x00, 0x00, 'a', 0x00, 'b', 0x00, 'c'});   // 5 units, 5 bytes left
    System::String arr[1];
    {
        Reader r(s);
        RtIo_SerializeStringArray(&r.ser, 1, arr, nullptr);
    }
    CHECK(arr[0]._data == nullptr);
    ReleaseString(arr[0]);
}
