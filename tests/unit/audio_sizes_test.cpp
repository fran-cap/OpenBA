// ============================================================================
// tests/unit/audio_sizes_test.cpp -- malformed SoundBuffer / xWMA inputs
// (review 2026-11-03, findings F-031, F-048, F-071, F-072, F-073)
// ============================================================================
// Each test carries the fuzz input that reached its finding, inline. None of
// these bytes is game data: they are fuzzer output and hand-made headers.
//   * F-048: BinaryReader::SerializeBool stored the raw stream byte in a C++
//     bool (a byte of 2 is not a valid bool value: UB on every later load).
//   * F-072: SoundBuffer::PlatformSerialize allocated _length / _wmaLength
//     bytes straight from the stream (0x7fffffff in a 34-byte record).
//   * F-073: XwmaDecoder::TotalSamples took the seek table's last entry as is,
//     and decode-at-load sized its PCM allocation from it (4 GiB for a
//     330-byte resource).
//   * F-071: a WAVEFORMATEX nSamplesPerSec >= 0x80000000 reached ff_wma_init
//     as a negative int (signed overflow at wma.c:140, negative shift at :246).
//   * F-031: wmadec was built with FFmpeg's UNCHECKED bitstream reader, so a
//     packet that never reaches EOB read past its padded copy.
// ============================================================================
#include "unit.h"

#include "audio/SoundBuffer.h"
#include "audio/xwma_decode.h"
#include "io/Serializer.h"
#include "io/Stream.h"
#include "system/string_internal.h"
#include "collection/Collection.h"

#include <cstdint>
#include <cstring>
#include <new>
#include <string>
#include <vector>

namespace {

std::vector<uint8_t> Hex(const char* s) {
    std::vector<uint8_t> out;
    auto nib = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        return c - 'A' + 10;
    };
    for (size_t i = 0; s[i] != 0 && s[i + 1] != 0; i += 2)
        out.push_back(static_cast<uint8_t>((nib(s[i]) << 4) | nib(s[i + 1])));
    return out;
}

uint8_t RawByte(const bool& b) {
    uint8_t v;
    std::memcpy(&v, &b, 1);
    return v;
}

// A reading stream over a byte vector, with the binary encoding a MemoryStream
// installs (so IO::Serializer builds a BinaryReader over it).
struct ByteStream final : IO::Stream {
    std::vector<uint8_t> bytes;
    int32_t pos = 0;

    explicit ByteStream(std::vector<uint8_t> b) : bytes(std::move(b)) {
        void* encMem = RtSystem::RtMemAlloc(8, 4);
        _encoding = ::new (encMem) IO::BinaryEncoding();
    }
    bool Write(int, const void*) override { return false; }
    bool Read(int n, void* dst) override {
        if (n < 0 || static_cast<size_t>(pos) + static_cast<size_t>(n) > bytes.size())
            return false;
        if (n > 0)
            std::memcpy(dst, bytes.data() + pos, static_cast<size_t>(n));
        pos += n;
        return true;
    }
    bool SetCurrentPos(int p) override {
        pos = p;
        return true;
    }
    uint32_t GetCurrentPos() const override { return static_cast<uint32_t>(pos); }
    uint32_t GetSize() const override { return static_cast<uint32_t>(bytes.size()); }
};

// A SoundBuffer the test never destroys: ~SoundBuffer tells the audio Device,
// which a host test has none of. The buffers it allocated are freed here.
struct LoadedBuffer {
    alignas(Audio::SoundBuffer) unsigned char mem[sizeof(Audio::SoundBuffer)];
    Audio::SoundBuffer* buf;
    LoadedBuffer() : buf(::new (static_cast<void*>(mem)) Audio::SoundBuffer()) {}
    ~LoadedBuffer() {
        if (buf->_data != nullptr) rtcoll::MemFree(buf->_data);
        if (buf->_wmaData != nullptr) rtcoll::MemFree(buf->_wmaData);
    }
};

// The xwma fuzz layout: a 0x2c-byte format window, the seek table's byte count
// (u32 at 0x2c, clamped to what follows), the table, then the packets.
struct XwmaInput {
    std::vector<uint8_t> raw;
    std::vector<uint32_t> table;
    std::vector<uint8_t> packets;
    uint32_t packetCount = 0;

    explicit XwmaInput(const char* hex) : raw(Hex(hex)) {
        uint32_t tb = 0;
        std::memcpy(&tb, raw.data() + 0x2c, 4);
        if (tb > raw.size() - 0x30) tb = static_cast<uint32_t>(raw.size() - 0x30);
        table.assign(tb / 4 + 1, 0u);
        std::memcpy(table.data(), raw.data() + 0x30, tb);
        packets.assign(raw.begin() + 0x30 + tb, raw.end());
        packetCount = tb >> 2;
    }
    bool Open(Audio::XwmaDecoder& dec) const {
        // No packet bytes: open as the Stream worker does (format and table only).
        return dec.Open(raw.data(), packets.empty() ? nullptr : packets.data(), packets.size(),
                        table.data(), packetCount);
    }
};

// scratchpad/crashes/rsc_serialize/soundbuffer-bool-byte, minus the fuzzer's
// type byte: the record header, _loopForever = _streamed = 0x02, two empty
// strings, 1.0f, _length 4, _wmaLength 4, the table, a zero format, the data.
const char* const kSoundBufferBoolByte =
    "aaaaaaaa0000000000000000020200000080000000800000803f0400000004000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000";

// scratchpad/crashes/rsc_serialize/soundbuffer-length-alloc, minus the type
// byte: _length 0x7fffffff, _wmaLength 0, and the record ends there.
const char* const kSoundBufferLengthAlloc =
    "aaaaaaaa0000000000000000000000000080000000800000803fffffff7f00000000";

// scratchpad/crashes/xwma/huge_total.bin (330 B): stereo 44.1 kHz, one
// 278-byte packet of zeros, seek table {0xffffffff}.
const std::string kHugeTotal = std::string(
    "6101020044ac0000701700001601100000000000000000000000000000000000000000000000"
    "00000000000004000000ffffffff") + std::string(278 * 2, '0');

// scratchpad/crashes/xwma/ovf140 and shl246 (52 B each): nSamplesPerSec
// 0xaf0bbb80 / 0xfe00bb80, negative as the int the decoder takes.
const char* const kOvf140 =
    "6101020080bb0baf4e4e4e4e4e4e4e4e4e4e4e4e4e4eff4e4e4e4e4e4e4e4e4e4e4e4e4e4eff"
    "4e4e4e4e4e4e4e4e4e4e4e4e4e4e";
const char* const kShl246 =
    "6101020080bb00fea00f0000aa00ffffffff00000000004e4e4e4e0000900900eaeaeafd104e"
    "4ef2c0ff4000000ff4004e4e4e4e";

// scratchpad/crashes/xwma/unsafe_reader_min (364 B) and getbits492 (278 B):
// packets whose run-level decode never reaches EOB.
const char* const kUnsafeReaderMin =
    "6101020080bb00007017000000011000000000000100000000000000000000000000000000000000"
    "070301003c0000000020000000400000006000000080000000a0000000c0000000e000000000010000"
    "20010000400100005ce53d37d20e5570b7d27e16f9ac804e4e4e4e4e4e4e4e4e4e4e4e4e4e4e4e4e4e"
    "4e4e4e4e4e4e4e4e4e4e4e4e4e4e4e4e4e4e4e4e4e4effffffffffffffff4e4e4e4e4e4e4e4e4e4e4e"
    "4e4e4e4e4e4e4e4e4e4e4e4e4e4e4e4e4e4e4e4e4e4e4e4e4e4e4e4e4e4e4e4e4e4e4e4e4e4e4e4e4e"
    "4e4e4e4e4ef1ffd0000003fd000000341ddcd0fb5dc1e3e2732fd14e8964e6b9bc4e4e4e4e4e4e4e4e"
    "4e4e4e4e4ef1ffd0000003fd000000341ddcd0fb5dc1e3e2732fd14e8964e6b9bc4e4a4ebc4003abf2"
    "34ce7bc5f2bd03e9a93e4b9ba96a567a9bded7686cfb3d25ea6cae4e4e4e4e4e4e4e4e4e4e4e4e4e4e"
    "4e4e4e4e4e4e4e4e4e4e4e4e4e4e4e4e4e4e4e4e4e4e4e4e4e4e4e4e4e4e4e4e4e4e4e4e4e4e4e4e4e"
    "4e4e4e4e4e4e4e4e4e4e4e";
const char* const kGetbits492 =
    "6101020080bb0000a00f0000aa0010000000000000000f0000000000000000000000000000000000"
    "070301003c00000000200066aeac2bd46ae174c5fa204e4e4e4e4e4e4e4eaffc535c9998ede634b0"
    "56001f31a58b2000258f1000a586d80029bd94c46d073f393eb9b34e4e4e4ef2c0aeac2bd46ae174"
    "c5fa204e4e4e4e4e4e4e4ed5d0f800000066aeac2bd46ae174c5fa204e4e4e4e4e4e4e4e74c5fa20"
    "4e4e4e4e4e4e4e4eaffc535c9998ede634b056001f31a58b2000258f1000a586d80029bd94c46d07"
    "3f393eb9b34e4e4e4ef2c0aeac2bd46ae174c5fa204e4ed5d0f800000066aeac2bd46ae174c5fa20"
    "4e4e4e4e4e4e4e4e4e4e4e4ed5d0f800000066aeac2bd46ae174c5fa204e4e4e4e4e4e4e4e4e";

}  // namespace

// F-048: the reader's scalar bool slot.
UNIT_TEST(audio_sizes_serializebool_stores_a_valid_bool) {
    alignas(8) uint8_t serMem[16] = {};
    IO::Serializer& ser = *reinterpret_cast<IO::Serializer*>(serMem);
    ByteStream s(Hex("aaaaaaaa0000000000000000" "02ff0001"));
    ser._stream = &s;
    {
        IO::BinaryReader r(ser, 0);
        bool v[4] = {};
        for (bool& b : v)
            r.SerializeBool(b, nullptr);
        CHECK_EQ(RawByte(v[0]), 1);
        CHECK_EQ(RawByte(v[1]), 1);
        CHECK_EQ(RawByte(v[2]), 0);
        CHECK_EQ(RawByte(v[3]), 1);
        bool arr[2] = {};
        s.bytes.push_back(0x40);
        s.bytes.push_back(0x00);
        r.SerializeBoolArray(2, arr, nullptr);
        CHECK_EQ(RawByte(arr[0]), 1);
        CHECK_EQ(RawByte(arr[1]), 0);
    }
}

// SoundBuffer::Serialize's read (vtbatch7_soundbuffer_soundeffect.cpp, retail
// 0x1800e0480) up to and including PlatformSerialize, over a fuzz record. The
// slot itself is a marker in the unit build (audio_abi_test.cpp); the census and
// the decode at load that follow it are not reached by these zero formats. The
// encoding's CreateReader is a stub in the unit build (b39_io_classes_test.cpp),
// so the Serializer is assembled by hand around a real BinaryReader.
static void LoadSoundBuffer(Audio::SoundBuffer& buf, ByteStream& s) {
    alignas(8) uint8_t serMem[16] = {};
    IO::Serializer& ser = *reinterpret_cast<IO::Serializer*>(serMem);
    ser._stream = &s;
    IO::BinaryReader r(ser, 0);
    ser._readerWriter = &r;
    r.SerializeBool(buf._loopForever, nullptr);
    r.SerializeBool(buf._streamed, nullptr);
    buf._wave.Serialize(ser);
    buf._streamPath.Serialize(ser);
    r.SerializeFloat(buf._durationSeconds, nullptr);
    buf.PlatformSerialize(ser);
}

// F-048 at SoundBuffer's two flags; the record is otherwise well formed, and its
// two 4-byte buffers load as they always did.
UNIT_TEST(audio_sizes_soundbuffer_flag_bytes_load_as_bools) {
    ByteStream s(Hex(kSoundBufferBoolByte));
    LoadedBuffer lb;
    Audio::SoundBuffer* const buf = lb.buf;
    LoadSoundBuffer(*buf, s);
    CHECK_EQ(RawByte(buf->_loopForever), 1);
    CHECK_EQ(RawByte(buf->_streamed), 1);
    CHECK_EQ(buf->_length, 4);
    CHECK_EQ(buf->_wmaLength, 4);
    CHECK(buf->_data != nullptr);
    CHECK(buf->_wmaData != nullptr);
    CHECK_EQ(s.pos, static_cast<int32_t>(s.bytes.size()));
}

// F-072: a length the record does not hold is not allocated.
UNIT_TEST(audio_sizes_soundbuffer_length_past_the_record_is_rejected) {
    ByteStream s(Hex(kSoundBufferLengthAlloc));
    LoadedBuffer lb;
    Audio::SoundBuffer* const buf = lb.buf;
    LoadSoundBuffer(*buf, s);
    CHECK_EQ(buf->_length, 0);
    CHECK(buf->_data == nullptr);
    CHECK_EQ(buf->_wmaLength, 0);
    CHECK(buf->_wmaData == nullptr);
}

// F-073: TotalSamples is bounded by what the packets can decode to.
UNIT_TEST(audio_sizes_xwma_total_is_bounded_by_the_packets) {
    XwmaInput in(kHugeTotal.c_str());
    Audio::XwmaDecoder dec;
    CHECK(in.Open(dec));
    CHECK_EQ(dec.PacketCount(), 1u);
    CHECK(dec.TotalSamples() <= dec.MaxSamplesFor(dec.PacketCount()));
    CHECK(dec.TotalSamples() * dec.Channels() * 2u < (1ull << 20));
}

// F-071: a sample rate that is negative as an int is refused before ff_wma_init.
UNIT_TEST(audio_sizes_xwma_negative_rate_is_refused) {
    for (const char* hex : {kOvf140, kShl246}) {
        XwmaInput in(hex);
        Audio::XwmaDecoder dec;
        CHECK(!in.Open(dec));
        CHECK(!dec.IsOpen());
    }
}

// F-031: the decoder is built with the checked bitstream reader, and the two
// inputs that read past the packet copy on the unchecked one decode to an end.
#include "../../third_party/wmadec/shim/config.h"
#if defined(CONFIG_SAFE_BITSTREAM_READER) && CONFIG_SAFE_BITSTREAM_READER
static constexpr bool kWmadecSafeReader = true;
#else
static constexpr bool kWmadecSafeReader = false;
#endif

UNIT_TEST(audio_sizes_wmadec_uses_the_checked_bitstream_reader) {
    CHECK(kWmadecSafeReader);
    for (const char* hex : {kUnsafeReaderMin, kGetbits492}) {
        XwmaInput in(hex);
        Audio::XwmaDecoder dec;
        if (!in.Open(dec))
            continue;
        const size_t cap = dec.MaxSamplesFor(dec.PacketCount());
        std::vector<int16_t> pcm(cap * dec.Channels() + 1);
        const size_t got = dec.DecodePackets(0, dec.PacketCount(), pcm.data(), cap);
        CHECK(got <= cap);
    }
}
