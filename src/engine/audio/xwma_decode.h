#pragma once
// ============================================================================
// src/engine/audio/xwma_decode.h — xWMA packets -> S16 PCM (Phase D.4 SDL A3)
// ============================================================================
//
// Every sound Banished ships is xWMA: WMA v2 packets (WAVEFORMATEX wFormatTag
// 0x0161) plus a seek table of cumulative DECODED bytes, one uint32 per packet.
// A SoundBuffer holds exactly that (SoundBuffer.h: `_format` +0x54, `_data` /
// `_length` the packets, `_wmaData` / `_wmaLength` the table), and the Stream
// worker reads the same pair out of a music file in 16-packet chunks
// (src/engine/audio/audio_stream_run.cpp). XAudio2 decodes it in retail;
// SDL3 cannot, so this class does, over the WMA v2 decoder extracted from FFmpeg
// (third_party/wmadec, LGPL-2.1; EXTRACT.md there).
//
// D4_RULES §3 Q5: decode AT LOAD, never on the audio callback. Two callers:
//   * a SoundBuffer decodes once, whole, when the resource loads:
//         dec.Open(...); dec.DecodePackets(0, dec.PacketCount(), pcm, dec.TotalSamples());
//   * the music stream decodes chunk-ahead on the Stream worker thread, N packets
//     per call, continuing the same pass (firstPacket == NextPacket()). The
//     worker never holds a whole track: it reads 16 packets at a time into a
//     Stream::Block, so it opens with the table and format only and hands each
//     chunk's bytes to DecodeChunk.
//
// WHAT IT MATCHES. The output is XAudio2 2.7's for the same packets to within
// half an S16 step (measured on three shipped sounds; the rules are in
// third_party/wmadec/EXTRACT.md):
//   * a pass from packet 0 yields exactly TotalSamples() frames;
//   * a pass from packet k > 0 (a fresh decoder, as a fresh XAudio2 voice
//     would start there) yields TotalSamples() - SeekSamples(k) - FrameLength()
//     frames, which are the LAST frames of the packet-0 pass;
//   * calls that continue a pass give the same frames, in order, as one call.
//
// UNITS. A "sample" is one sample FRAME (every channel at one instant), the unit
// of XAudio2's SamplesPlayed and of the seek table / (2 * channels). `out`
// receives interleaved int16 values, Channels() per frame.
//
// PORTABILITY: no OS header, no engine type, no allocator but std. Not
// thread-safe per object; separate decoders may run on separate threads.
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <vector>

struct Wmav2Decoder;   // third_party/wmadec/include/wmav2_decoder.h

namespace Audio {

// The decoder's float -> S16 conversion (public for tests/unit/xwma_tos16_test.cpp).
// y = x * 32768.0f, rounded to the nearest integer, ties to even (the thread's
// default rounding mode), saturated to [-32768, 32767]; NaN -> 0, +-inf saturate.
// Bit-identical to the original per-sample body
//     v = std::nearbyint(x * 32768.0f); v >= 32767 -> 32767; v <= -32768 -> -32768;
//     NaN -> 0; else (int16_t)v
// (the proof is in the .cpp and the unit test; handins/AUDIODEC.md).
int16_t FloatToS16(float x);
// planes[c][from + s] for s in [0, frames), c in [0, channels), interleaved into
// dst (frames * channels values), each through FloatToS16. SSE2 eight frames at a
// time on x86-64 (mono and stereo), FloatToS16 for the tail and other layouts.
void PlanesToS16(const float* const* planes, size_t from, size_t frames, uint32_t channels,
                 int16_t* dst);

class XwmaDecoder {
public:
    // WAVE_FORMAT_WMAUDIO2: the only tag xWMAEncode writes for the shipped audio.
    static constexpr uint16_t kFormatTagWma2 = 0x0161;

    XwmaDecoder() = default;
    ~XwmaDecoder();
    XwmaDecoder(const XwmaDecoder&) = delete;
    XwmaDecoder& operator=(const XwmaDecoder&) = delete;

    // format: at least the 18 bytes of a WAVEFORMATEX (SoundBuffer::Format()).
    // packets / bytes: the xWMA packets, nBlockAlign bytes each, back to back;
    // nullptr / 0 for a stream, whose chunks arrive through DecodeChunk.
    // seekTable / packetCount: one cumulative decoded-byte entry per packet.
    // The decoder keeps the three pointers (it copies nothing): the caller's
    // buffers must outlive it or the next Open. False, and closed, on a format
    // that is not mono / stereo WMA v2, on fewer than packetCount * nBlockAlign
    // bytes, on an empty table, or when the WMA v2 decoder refuses the format.
    bool Open(const uint8_t* format, const uint8_t* packets, size_t bytes,
              const uint32_t* seekTable, uint32_t packetCount);
    void Close();
    bool IsOpen() const { return _seekTable != nullptr; }

    uint32_t Channels() const    { return _channels; }
    uint32_t SampleRate() const  { return _sampleRate; }
    uint32_t BlockAlign() const  { return _blockAlign; }
    uint32_t PacketCount() const { return _packetCount; }
    // WMA frame length in samples (2048 at 44.1 / 48 kHz).
    uint32_t FrameLength() const { return _frameLength; }

    // seekTable[last] / (2 * channels): the samples a pass from packet 0 yields,
    // capped at what PacketCount() packets can decode to (an untrusted table).
    uint64_t TotalSamples() const;
    // The seek table's position before `packet` (0 for packet 0), in samples.
    uint64_t SeekSamples(uint32_t packet) const;

    // Decode packets [firstPacket, firstPacket + packetCount) as S16, writing
    // at most `outSamples` samples to `out`; returns the samples written.
    //   * firstPacket == NextPacket() continues the current pass;
    //   * anything else starts a new pass at firstPacket (a fresh decoder);
    //   * packetCount is clipped to the end of the table; the pass's last
    //     packet also drains the decoder (the frame in its bit reservoir and
    //     the MDCT overlap);
    //   * what does not fit in `outSamples` is kept and returned first by the
    //     next call (DecodePackets(NextPacket(), 0, ...) returns only that).
    // A packet the decoder rejects contributes no samples; Failed() then says
    // so until the next pass, and the pass returns fewer than its table says.
    // One shipped sound has such packets: Tornado.wav's last six, which XAudio2
    // turns into a noise burst (third_party/wmadec/EXTRACT.md).
    size_t DecodePackets(uint32_t firstPacket, uint32_t packetCount,
                         int16_t* out, size_t outSamples);

    // The same, for a decoder opened without packets: `chunk` holds packets
    // [firstPacket, firstPacket + packetCount), BlockAlign() bytes each (the
    // Stream worker's Block::packets). Only the call's own packets are read, and
    // only during the call: the buffer may be reused as soon as it returns.
    // chunkBytes is the chunk's size: a call whose packets do not fit in it
    // decodes nothing and returns 0 (review F-096 / F-097).
    size_t DecodeChunk(uint32_t firstPacket, uint32_t packetCount, const uint8_t* chunk,
                       size_t chunkBytes, int16_t* out, size_t outSamples);

    // The packet a call continuing the current pass starts at (0 before any).
    uint32_t NextPacket() const { return _nextPacket; }
    // Samples decoded and not yet returned.
    size_t PendingSamples() const { return (_pending.size() - _pendingRead) / (_channels ? _channels : 1); }
    // A capacity that holds everything one call over packetCount packets can
    // return (pending samples plus a WMA superframe's 15 frames per packet plus
    // the two drained frames).
    size_t MaxSamplesFor(uint32_t packetCount) const;
    bool Failed() const { return _failed; }

private:
    bool OpenDecoder();
    bool StartPass(uint32_t firstPacket);
    size_t DecodeRange(uint32_t firstPacket, uint32_t packetCount, const uint8_t* first,
                       int16_t* out, size_t outSamples);
    void Append(const float* const* planes, int frames);

    const uint8_t*  _packets     = nullptr;
    size_t          _bytes       = 0;
    const uint32_t* _seekTable   = nullptr;
    uint32_t        _packetCount = 0;
    uint32_t        _channels    = 0;
    uint32_t        _sampleRate  = 0;
    uint32_t        _bitRate     = 0;
    uint32_t        _blockAlign  = 0;
    uint32_t        _frameLength = 0;

    Wmav2Decoder*   _decoder     = nullptr;
    bool            _decoderFresh = false;  // opened and fed no packet yet
    bool            _passActive  = false;
    bool            _failed      = false;
    uint32_t        _nextPacket  = 0;
    uint64_t        _skip        = 0;   // decoder frames still to drop at the pass start
    uint64_t        _remaining   = 0;   // samples the pass may still emit
    std::vector<int16_t> _pending;      // interleaved, decoded and not yet returned
    size_t          _pendingRead = 0;   // int16 values of _pending already returned
    // During one DecodeRange that starts with nothing pending: the caller's `out`,
    // which Append fills directly (in order) until `outSamples` are written; only
    // what does not fit goes to _pending. The same samples, in the same order, as
    // converting everything into _pending and copying it out (AUDIODEC).
    int16_t*        _directOut   = nullptr;
    size_t          _directCap   = 0;   // samples
    size_t          _directUsed  = 0;   // samples
};

}  // namespace Audio
