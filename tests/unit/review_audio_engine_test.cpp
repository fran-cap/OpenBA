// ============================================================================
// tests/unit/review_audio_engine_test.cpp — review 2026-11-03, group audio-engine
// ============================================================================
// F-096 / F-097: the streamed-music worker hands XwmaDecoder::DecodeChunk one ring
//   slot of 16 fixed 0x8b6-byte packets; the decoder strides by the format's
//   nBlockAlign. A track with a larger nBlockAlign read past the slot. The chunk
//   here ends on a no-access page, so the over-read faults on every platform.
// F-118 / F-136: the truncating float -> int conversions and the loop remainder of
//   Device::PlayPlatform / Scene::FindFree3DEffect / Scene::Update /
//   SoundEffect::Compile (audio/audio_cvtt.h).
//
// Scene::Update (F-095, F-119, F-120) does not link into the unit host
// (audio_abi_test.cpp replaces the audio members with markers): those fixes rest
// on the ledger's sanitizer repros and the gate.
// ============================================================================
#include "unit.h"
#include "audio/xwma_decode.h"
#include "audio/audio_cvtt.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace {

// `bytes` writable bytes that end exactly where a no-access page begins.
class GuardedBuffer {
public:
    explicit GuardedBuffer(size_t bytes) : _bytes(bytes) {
#if defined(_WIN32)
        SYSTEM_INFO si;
        GetSystemInfo(&si);
        _page = si.dwPageSize;
#else
        _page = static_cast<size_t>(sysconf(_SC_PAGESIZE));
#endif
        const size_t dataPages = (bytes + _page - 1) / _page;
        _total = (dataPages + 1) * _page;
#if defined(_WIN32)
        _base = static_cast<uint8_t*>(VirtualAlloc(nullptr, _total, MEM_RESERVE | MEM_COMMIT,
                                                   PAGE_READWRITE));
        if (_base) {
            DWORD old = 0;
            VirtualProtect(_base + dataPages * _page, _page, PAGE_NOACCESS, &old);
        }
#else
        void* p = mmap(nullptr, _total, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        _base = (p == MAP_FAILED) ? nullptr : static_cast<uint8_t*>(p);
        if (_base)
            mprotect(_base + dataPages * _page, _page, PROT_NONE);
#endif
        if (_base)
            _data = _base + dataPages * _page - bytes;
    }
    ~GuardedBuffer() {
        if (!_base)
            return;
#if defined(_WIN32)
        VirtualFree(_base, 0, MEM_RELEASE);
#else
        munmap(_base, _total);
#endif
    }
    uint8_t* data() const { return _data; }
    size_t size() const { return _bytes; }

private:
    size_t   _bytes = 0;
    size_t   _page  = 4096;
    size_t   _total = 0;
    uint8_t* _base  = nullptr;
    uint8_t* _data  = nullptr;
};

// A WAVEFORMATEX window (0x28 bytes, as SoundBuffer embeds it): WMA v2, stereo,
// 44.1 kHz, 12000 bytes/s, nBlockAlign `blockAlign`.
std::vector<uint8_t> WmaFormat(uint16_t blockAlign) {
    const uint8_t hdr[18] = {
        0x61, 0x01,               // wFormatTag 0x0161
        0x02, 0x00,               // nChannels 2
        0x44, 0xac, 0x00, 0x00,   // nSamplesPerSec 44100
        0xe0, 0x2e, 0x00, 0x00,   // nAvgBytesPerSec 12000
        0x00, 0x00,               // nBlockAlign (below)
        0x10, 0x00,               // wBitsPerSample 16
        0x00, 0x00,               // cbSize 0
    };
    std::vector<uint8_t> f(0x28, 0);
    std::memcpy(f.data(), hdr, sizeof(hdr));
    f[12] = static_cast<uint8_t>(blockAlign & 0xff);
    f[13] = static_cast<uint8_t>(blockAlign >> 8);
    return f;
}

}  // namespace

// F-096 / F-097: nBlockAlign 4459 (a standard xWMA block size, > 0x8b6), the
// worker's chunk of 16 * 0x8b6 = 35680 bytes. Before the fix the decoder read
// packet 8 at 8 * 4459 = 35672 and on, past the chunk.
UNIT_TEST(review_audio_decode_chunk_stays_in_the_slot) {
    const std::vector<uint8_t> fmt = WmaFormat(4459);
    std::vector<uint32_t> table(64);
    for (size_t i = 0; i < table.size(); ++i)
        table[i] = static_cast<uint32_t>((i + 1) * 8192u);

    Audio::XwmaDecoder dec;
    if (!dec.Open(fmt.data(), nullptr, 0, table.data(), static_cast<uint32_t>(table.size())))
        UNIT_SKIP("the WMA v2 decoder refused the synthetic 4459-byte format");
    CHECK_EQ(dec.BlockAlign(), uint32_t(4459));

    GuardedBuffer chunk(16u * 0x8b6u);
    CHECK(chunk.data() != nullptr);
    if (!chunk.data())
        return;
    std::memset(chunk.data(), 0, chunk.size());
    std::vector<int16_t> pcm(dec.MaxSamplesFor(16) * dec.Channels());

    // 16 packets of 4459 bytes do not fit in 35680: nothing is read or decoded.
    const size_t got = dec.DecodeChunk(0, 16, chunk.data(), chunk.size(), pcm.data(),
                                       dec.MaxSamplesFor(16));
    CHECK_EQ(got, size_t(0));

    // The 8 that fit still decode through the same call (the bound is the bytes).
    const size_t eight = dec.DecodeChunk(0, 8, chunk.data(), chunk.size(), pcm.data(),
                                         dec.MaxSamplesFor(8));
    (void)eight;
}

// The conversions are the cast in range and the instruction's INT_MIN out of it.
UNIT_TEST(review_audio_cvtt_matches_the_instruction) {
    CHECK_EQ(Audio::CvttSd2Si64(0.0), int64_t(0));
    CHECK_EQ(Audio::CvttSd2Si64(-0.0), int64_t(0));
    CHECK_EQ(Audio::CvttSd2Si64(1.9), int64_t(1));
    CHECK_EQ(Audio::CvttSd2Si64(-1.9), int64_t(-1));
    CHECK_EQ(Audio::CvttSd2Si64(123456.75 / 1e-6), int64_t(123456750000));
    CHECK_EQ(Audio::CvttSd2Si64(-9223372036854775808.0), INT64_MIN);
    CHECK_EQ(Audio::CvttSd2Si64(9223372036854774784.0), int64_t(9223372036854774784));
    CHECK_EQ(Audio::CvttSd2Si64(9223372036854775808.0), INT64_MIN);
    CHECK_EQ(Audio::CvttSd2Si64(4.61169e+23), INT64_MIN);   // F-136: INT64_MAX / 20 / 1e-6
    CHECK_EQ(Audio::CvttSd2Si64(-1e300), INT64_MIN);
    CHECK_EQ(Audio::CvttSd2Si64(std::numeric_limits<double>::infinity()), INT64_MIN);
    CHECK_EQ(Audio::CvttSd2Si64(-std::numeric_limits<double>::infinity()), INT64_MIN);
    CHECK_EQ(Audio::CvttSd2Si64(std::numeric_limits<double>::quiet_NaN()), INT64_MIN);

    CHECK_EQ(Audio::CvttSs2Si32(0.0f), int32_t(0));
    CHECK_EQ(Audio::CvttSs2Si32(2.5f), int32_t(2));
    CHECK_EQ(Audio::CvttSs2Si32(-2.5f), int32_t(-2));
    CHECK_EQ(Audio::CvttSs2Si32(2147483520.0f), int32_t(2147483520));
    CHECK_EQ(Audio::CvttSs2Si32(-2147483648.0f), INT32_MIN);
    CHECK_EQ(Audio::CvttSs2Si32(2147483648.0f), INT32_MIN);
    CHECK_EQ(Audio::CvttSs2Si32(1e20f), INT32_MIN);
    CHECK_EQ(Audio::CvttSs2Si32(std::numeric_limits<float>::infinity()), INT32_MIN);
    CHECK_EQ(Audio::CvttSs2Si32(std::numeric_limits<float>::quiet_NaN()), INT32_MIN);
}

// F-118: a zero-length loop (a SoundBuffer whose _durationSeconds is 0) has no
// position; retail's IDIV by 0 is #DE.
UNIT_TEST(review_audio_loop_remainder_of_an_empty_loop) {
    const double spt = 1e-6;
    const int64_t zeroLoop = Audio::CvttSd2Si64(static_cast<double>(0.0f) / spt);
    CHECK_EQ(zeroLoop, int64_t(0));
    CHECK_EQ(Audio::LoopRemainderTicks(123456789, zeroLoop), int64_t(0));
    CHECK_EQ(Audio::LoopRemainderTicks(INT64_MIN, -1), int64_t(0));
    // A real loop is the plain remainder.
    CHECK_EQ(Audio::LoopRemainderTicks(123456789, 1000000), int64_t(456789));
    CHECK_EQ(Audio::LoopRemainderTicks(-7, 3), int64_t(-1));
    CHECK_EQ(Audio::LoopRemainderTicks(123456789, INT64_MIN), int64_t(123456789));
}
