// ============================================================================
// src/engine/audio/xwma_decode.cpp — Audio::XwmaDecoder (Phase D.4 SDL A3)
// ============================================================================
// The xWMA layer over third_party/wmadec's C API. xwma_decode.h states the
// contract; third_party/wmadec/EXTRACT.md the XAudio2 2.7 measurements behind
// the two numbers this file applies:
//   * the frames dropped at a pass start: 2 x frame_len from packet 0,
//     1 x frame_len from any later packet (the decoder's warm-up, and at packet
//     0 the encoder's priming frame too);
//   * the samples a pass yields: the seek table's total from packet 0; from
//     packet k, the total minus the table's position before k minus frame_len.
// Everything else is the decoder's own output, converted to S16.
// ============================================================================
#include "audio/xwma_decode.h"

#include <cmath>
#include <cstring>
#if defined(__SSE2__)
#include <emmintrin.h>
#endif

#include "wmav2_decoder.h"

namespace Audio {

namespace {

uint16_t ReadU16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
uint32_t ReadU32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

}  // namespace

// ---------------------------------------------------------------------------
// float -> S16 (LOADPLAN block AUDIODEC). The original body was, per sample,
//     v = std::nearbyint(x * 32768.0f); v >= 32767 -> 32767; v <= -32768 -> -32768;
//     NaN -> 0; else (int16_t)v
// and MinGW's nearbyintf is a libm call that saves and restores the x87
// environment: 79% of a new game's resource load. This is the same function:
//   * y = x * 32768.0f is the same single IEEE multiply (mulss / mulps lane).
//   * NaN: y != y -> 0, as before (the old comparisons are all false on NaN).
//   * Clamp-then-round == round-then-clamp: R (round to integer in the current
//     mode) is monotone and leaves integers fixed, and both bounds are integers,
//     so y > 32767 gives R(y) >= 32767 (old: 32767) and R(32767) = 32767; the
//     same below -32768; inside, R(y) is the old v. +-inf saturate in both.
//   * R: nearbyintf rounds by the x87 control word, cvtss2si / cvtps2dq by MXCSR.
//     Both are round-to-nearest-even at thread start (Win64 and SysV ABIs) and
//     nothing in the tree changes either (no fesetround / _controlfp /
//     _mm_setcsr / ldmxcsr). DAZ / FTZ would touch only the multiply, which is
//     the same instruction in both bodies.
//   * The clamped y is an integer-or-not in [-32768, 32767]: the int32 convert
//     never overflows, and packssdw's saturation is then a no-op.
// tests/unit/xwma_tos16_test.cpp compares both against the old body (kept there
// only) on every float class and on every shipped sound's decoder output.
// ---------------------------------------------------------------------------
int16_t FloatToS16(float x) {
    float y = x * 32768.0f;
    if (y != y) return 0;
    y = y < -32768.0f ? -32768.0f : y;
    y = y > 32767.0f ? 32767.0f : y;
#if defined(__SSE2__)
    return static_cast<int16_t>(_mm_cvtss_si32(_mm_set_ss(y)));
#else
    return static_cast<int16_t>(std::lrintf(y));   // the current mode, as nearbyintf
#endif
}

#if defined(__SSE2__)
namespace {
// Four lanes of FloatToS16 as int32: NaN -> +0 (cmpord mask), clamp, cvtps2dq.
inline __m128i FourToS32(const float* src) {
    __m128 y = _mm_mul_ps(_mm_loadu_ps(src), _mm_set1_ps(32768.0f));
    y = _mm_and_ps(y, _mm_cmpord_ps(y, y));
    y = _mm_max_ps(y, _mm_set1_ps(-32768.0f));
    y = _mm_min_ps(y, _mm_set1_ps(32767.0f));
    return _mm_cvtps_epi32(y);
}
inline __m128i EightToS16(const float* src) {
    return _mm_packs_epi32(FourToS32(src), FourToS32(src + 4));
}
// An unaligned 16-byte store (movdqu; memcpy keeps it free of a pointer cast).
inline void Store8(int16_t* dst, __m128i v) { std::memcpy(dst, &v, sizeof(v)); }
}  // namespace
#endif

void PlanesToS16(const float* const* planes, size_t from, size_t frames, uint32_t channels,
                 int16_t* dst) {
    size_t s = 0;
#if defined(__SSE2__)
    if (channels == 1) {
        const float* m = planes[0] + from;
        for (; s + 8 <= frames; s += 8)
            Store8(dst + s, EightToS16(m + s));
        for (; s < frames; ++s) dst[s] = FloatToS16(m[s]);
        return;
    }
    if (channels == 2) {
        const float* l = planes[0] + from;
        const float* r = planes[1] + from;
        for (; s + 8 <= frames; s += 8) {
            const __m128i lv = EightToS16(l + s);
            const __m128i rv = EightToS16(r + s);
            Store8(dst + 2 * s, _mm_unpacklo_epi16(lv, rv));
            Store8(dst + 2 * s + 8, _mm_unpackhi_epi16(lv, rv));
        }
    }
#endif
    int16_t* o = dst + s * channels;
    for (; s < frames; ++s)
        for (uint32_t c = 0; c < channels; ++c)
            *o++ = FloatToS16(planes[c][from + s]);
}

XwmaDecoder::~XwmaDecoder() { Close(); }

void XwmaDecoder::Close() {
    if (_decoder != nullptr) {
        wmav2_close(_decoder);
        _decoder = nullptr;
    }
    _packets = nullptr;
    _bytes = 0;
    _seekTable = nullptr;
    _packetCount = 0;
    _channels = _sampleRate = _bitRate = _blockAlign = _frameLength = 0;
    _decoderFresh = false;
    _passActive = false;
    _failed = false;
    _nextPacket = 0;
    _skip = _remaining = 0;
    _pending.clear();
    _pendingRead = 0;
}

bool XwmaDecoder::OpenDecoder() {
    if (_decoder != nullptr) {
        wmav2_close(_decoder);
        _decoder = nullptr;
    }
    uint8_t extradata[6];
    wmav2_xwma_extradata(extradata);
    Wmav2Params params;
    params.channels = static_cast<int>(_channels);
    params.sample_rate = static_cast<int>(_sampleRate);
    params.bit_rate = static_cast<int>(_bitRate);
    params.block_align = static_cast<int>(_blockAlign);
    params.extradata = extradata;
    params.extradata_size = static_cast<int>(sizeof(extradata));
    if (wmav2_open(&params, &_decoder) < 0) {
        _decoder = nullptr;
        return false;
    }
    _frameLength = static_cast<uint32_t>(wmav2_frame_len(_decoder));
    _decoderFresh = true;
    return true;
}

bool XwmaDecoder::Open(const uint8_t* format, const uint8_t* packets, size_t bytes,
                       const uint32_t* seekTable, uint32_t packetCount) {
    Close();
    if (format == nullptr || seekTable == nullptr || packetCount == 0) return false;
    if (packets == nullptr && bytes != 0) return false;   // stream use: no packets, no bytes

    // WAVEFORMATEX: wFormatTag +0, nChannels +2, nSamplesPerSec +4,
    // nAvgBytesPerSec +8, nBlockAlign +12.
    const uint16_t tag        = ReadU16(format + 0);
    const uint16_t channels   = ReadU16(format + 2);
    const uint32_t rate       = ReadU32(format + 4);
    const uint32_t avgBytes   = ReadU32(format + 8);
    const uint16_t blockAlign = ReadU16(format + 12);
    if (tag != kFormatTagWma2 || channels < 1 || channels > 2 || rate == 0 || avgBytes == 0 ||
        blockAlign == 0)
        return false;
    if (packets != nullptr && bytes / blockAlign < packetCount) return false;

    _packets = packets;
    _bytes = bytes;
    _seekTable = seekTable;
    _packetCount = packetCount;
    _channels = channels;
    _sampleRate = rate;
    _bitRate = avgBytes * 8;
    _blockAlign = blockAlign;
    // FFmpeg's own init is the format check that matters (rate, bit rate and
    // block size have to give a frame layout it can decode).
    if (!OpenDecoder()) {
        Close();
        return false;
    }
    return true;
}

uint64_t XwmaDecoder::TotalSamples() const {
    if (!IsOpen()) return 0;
    const uint64_t table = _seekTable[_packetCount - 1] / (2u * _channels);
    // NOT RETAIL (hardening, review F-073): the seek table is resource data and
    // its last entry is not checked against the packets; decode at load sizes its
    // PCM allocation from this value, so {0xffffffff} asked for 4 GiB for one
    // 278-byte packet. A pass from packet 0 cannot yield more than its packets
    // decode to (a superframe's 15 frames per packet plus the two drained
    // frames, MaxSamplesFor's bound), so the total is capped there. A shipped
    // table is far below the cap.
    const uint64_t bound = (static_cast<uint64_t>(_packetCount) * 15u + 2u) * _frameLength;
    return table < bound ? table : bound;
}

uint64_t XwmaDecoder::SeekSamples(uint32_t packet) const {
    if (!IsOpen() || packet == 0) return 0;
    if (packet > _packetCount) packet = _packetCount;
    return _seekTable[packet - 1] / (2u * _channels);
}

size_t XwmaDecoder::MaxSamplesFor(uint32_t packetCount) const {
    return PendingSamples() + (static_cast<size_t>(packetCount) * 15u + 2u) * _frameLength;
}

bool XwmaDecoder::StartPass(uint32_t firstPacket) {
    _passActive = false;
    _failed = false;
    _pending.clear();
    _pendingRead = 0;
    // A pass starts from a decoder that has seen no packet: the one Open made,
    // or a new one (the MDCT overlap and the bit reservoir start empty).
    if (!_decoderFresh && !OpenDecoder()) {
        _failed = true;
        return false;
    }
    _decoderFresh = false;

    _skip = static_cast<uint64_t>(_frameLength) * (firstPacket == 0 ? 2u : 1u);
    const uint64_t total = TotalSamples();
    const uint64_t done = SeekSamples(firstPacket) + (firstPacket == 0 ? 0u : _frameLength);
    _remaining = total > done ? total - done : 0;
    _nextPacket = firstPacket;
    _passActive = true;
    return true;
}

void XwmaDecoder::Append(const float* const* planes, int frames) {
    uint64_t n = frames > 0 ? static_cast<uint64_t>(frames) : 0;
    uint64_t from = 0;
    if (_skip > 0) {
        const uint64_t drop = n < _skip ? n : _skip;
        _skip -= drop;
        from = drop;
    }
    uint64_t keep = n - from;
    if (keep > _remaining) keep = _remaining;
    _remaining -= keep;
    if (keep == 0) return;

    // Straight into the caller's buffer while it has room (DecodeRange decides
    // when; the samples and their order are what the copy out of _pending gave).
    if (_directOut != nullptr && _directUsed < _directCap) {
        const size_t room = _directCap - _directUsed;
        const size_t direct = keep < room ? static_cast<size_t>(keep) : room;
        PlanesToS16(planes, static_cast<size_t>(from), direct, _channels,
                    _directOut + _directUsed * _channels);
        _directUsed += direct;
        from += direct;
        keep -= direct;
        if (keep == 0) return;
    }

    // Compact what has already been returned before growing.
    if (_pendingRead > 0) {
        _pending.erase(_pending.begin(), _pending.begin() + static_cast<std::ptrdiff_t>(_pendingRead));
        _pendingRead = 0;
    }
    const size_t base = _pending.size();
    _pending.resize(base + static_cast<size_t>(keep) * _channels);
    PlanesToS16(planes, static_cast<size_t>(from), static_cast<size_t>(keep), _channels,
                _pending.data() + base);
}

size_t XwmaDecoder::DecodePackets(uint32_t firstPacket, uint32_t packetCount,
                                  int16_t* out, size_t outSamples) {
    // Opened for the stream (no packets): only a pure continuation, which hands
    // back what is pending, is meaningful here.
    if (_packets == nullptr && packetCount != 0) return 0;
    const uint8_t* first =
        _packets ? _packets + static_cast<size_t>(firstPacket) * _blockAlign : nullptr;
    return DecodeRange(firstPacket, packetCount, first, out, outSamples);
}

size_t XwmaDecoder::DecodeChunk(uint32_t firstPacket, uint32_t packetCount, const uint8_t* chunk,
                                size_t chunkBytes, int16_t* out, size_t outSamples) {
    if (chunk == nullptr && packetCount != 0) return 0;
    // Hardening (review F-096 / F-097): the packets are read at the format's
    // nBlockAlign, the caller's chunk holds chunkBytes. The Stream worker's ring
    // slot is 16 fixed 0x8b6-byte packets; a track with a larger nBlockAlign read
    // past it. The packets DecodeRange would read (packetCount clipped to the
    // table, as there) must fit, or nothing is decoded.
    if (IsOpen() && firstPacket < _packetCount) {
        const uint32_t left = _packetCount - firstPacket;
        const uint32_t n = packetCount < left ? packetCount : left;
        if (static_cast<uint64_t>(n) * _blockAlign > chunkBytes) return 0;
    }
    return DecodeRange(firstPacket, packetCount, chunk, out, outSamples);
}

size_t XwmaDecoder::DecodeRange(uint32_t firstPacket, uint32_t packetCount, const uint8_t* first,
                                int16_t* out, size_t outSamples) {
    if (!IsOpen() || firstPacket >= _packetCount) {
        // Only a continuation with nothing left to decode may still hand back
        // what is pending.
        if (!(IsOpen() && _passActive && firstPacket == _nextPacket)) return 0;
        packetCount = 0;
    } else if (!_passActive || firstPacket != _nextPacket) {
        if (!StartPass(firstPacket)) return 0;
    }
    if (packetCount > _packetCount - firstPacket) packetCount = _packetCount - firstPacket;

    // Nothing pending: what this call decodes goes straight to `out` (Append),
    // up to outSamples; the rest is pending, as before.
    if (out != nullptr && PendingSamples() == 0) {
        _directOut = out;
        _directCap = outSamples;
        _directUsed = 0;
    }

    for (uint32_t i = 0; i < packetCount; ++i) {
        const uint32_t packet = firstPacket + i;
        const float* const* planes = nullptr;
        int frames = 0;
        const uint8_t* data = first + static_cast<size_t>(i) * _blockAlign;
        if (wmav2_decode_packet(_decoder, data, static_cast<int>(_blockAlign), &planes, &frames) < 0)
            _failed = true;
        else
            Append(planes, frames);
        _nextPacket = packet + 1;

        if (_nextPacket == _packetCount) {
            // The pass's last packet: the frame left in the bit reservoir, then
            // the MDCT overlap (wmav2_drain returns 0 frames once both are out).
            for (int step = 0; step < 2; ++step) {
                if (wmav2_drain(_decoder, &planes, &frames) < 0) { _failed = true; break; }
                if (frames == 0) break;
                Append(planes, frames);
            }
        }
    }

    const size_t direct = _directUsed;
    _directOut = nullptr;
    _directCap = _directUsed = 0;

    const size_t available = PendingSamples();
    const size_t room = outSamples - direct;
    const size_t n = available < room ? available : room;
    if (n > 0 && out != nullptr) {
        std::memcpy(out + direct * _channels, _pending.data() + _pendingRead,
                    n * _channels * sizeof(int16_t));
        _pendingRead += n * _channels;
        if (_pendingRead == _pending.size()) {
            _pending.clear();
            _pendingRead = 0;
        }
    }
    return direct + (out != nullptr ? n : 0);
}

}  // namespace Audio
