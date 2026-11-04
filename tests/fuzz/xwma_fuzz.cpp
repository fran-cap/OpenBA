// ============================================================================
// tests/fuzz/xwma_fuzz.cpp -- Audio::XwmaDecoder (and the wmav2 decoder under it) over arbitrary bytes
// ============================================================================
// P0_MAP input surface rank 4: a SoundBuffer resource as SoundBuffer::PlatformSerialize reads it
// (vtbatch7_soundbuffer_soundeffect.cpp:205: the seek table, the 0x28-byte format window, the
// packets), then decoded the two ways the engine decodes it:
//   * whole, at load (DecodeXwmaJob, vtbatch7:324): Open(format, packets, bytes, table, n),
//     pcm = TotalSamples() * channels * 2 bytes, DecodePackets(0, n, pcm, TotalSamples());
//   * streamed (Stream::Run, audio_stream_run.cpp): Open(format, nullptr, 0, table, n), then
//     DecodeChunk over chunks of up to 16 packets, out = MaxSamplesFor(chunk), with a seek
//     (a new pass at another packet) part way.
//
// INPUT
//   [0x00, 0x28)  the format window (WAVEFORMATEX + trailing bytes)
//   0x28 u8       mode: bit0 streamed, bit1 a small `out` (the pending path), bit2 seek
//   0x29 u8       packets per chunk - 1 (low 4 bits)
//   0x2a u16      the packet a seek restarts at
//   0x2c u32      the seek table's byte length (_wmaLength), clipped to what follows
//   ...           the table, then the packets (_length = the rest)
// Every buffer is a heap copy of exactly its stored size, as MemAlloc(_wmaLength) /
// MemAlloc(_length) are, so ASan sees a read past either.
//
// CAP (not a mask of a finding in the decoder): the whole-decode PCM buffer is sized by the
// seek table's last entry, an attacker-chosen u32 (up to 4 GiB; recorded as its own finding,
// the int-overflow-alloc / DoS row of the xwma report). Here the buffer is min(TotalSamples(),
// MaxSamplesFor(n)) samples and outSamples is the same number, so the decoder writes exactly
// what the engine's call would write into its first that-many samples (the rest of a pass that
// long would go to _pending, which is still exercised).
// ============================================================================
#include "audio/xwma_decode.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

// DecodeChunk gained a chunk byte count (PR #140, review F-096 / F-097). Call whichever
// signature the tree has, so this target builds before and after that change merges.
template <typename D, typename = void>
struct HasChunkBytes : std::false_type {};
template <typename D>
struct HasChunkBytes<D, std::void_t<decltype(std::declval<D&>().DecodeChunk(
                             uint32_t{}, uint32_t{}, static_cast<const uint8_t*>(nullptr), size_t{},
                             static_cast<int16_t*>(nullptr), size_t{}))>> : std::true_type {};

template <typename D>
size_t DecodeChunkAny(D& dec, uint32_t first, uint32_t count, const uint8_t* block, size_t blockBytes,
                      int16_t* out, size_t outSamples) {
    if constexpr (HasChunkBytes<D>::value)
        return dec.DecodeChunk(first, count, block, blockBytes, out, outSamples);
    else
        return dec.DecodeChunk(first, count, block, out, outSamples);
}

uint32_t ReadU32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

constexpr size_t kHeader = 0x30;
constexpr size_t kPcmCapSamples = size_t(1) << 22;   // keeps one run under the rss limit

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size < kHeader)
        return 0;
    // MASK (xwma finding "negative sample rate"): nSamplesPerSec >= 0x80000000 reaches
    // ff_wma_init as a negative int (wmav2_open skips avcodec_open2's `sample_rate < 0` check)
    // and stops the run on UBSan at wma.c:140 / wma.c:246 before any packet is read.
    if (data[7] & 0x80)
        return 0;
    // MASK (xwma finding "unchecked bit reader"): the campaign build links wmadec with
    // -DCONFIG_SAFE_BITSTREAM_READER=1 (scratchpad xwma/build_wmadec.sh) after the over-read in
    // get_vlc2 / get_bits was recorded; the shipped build has the reader unchecked.
    // The format window: SoundBuffer::_format is 0x2c bytes in the object, 0x28 serialized.
    uint8_t* format = static_cast<uint8_t*>(std::malloc(0x28));
    std::memcpy(format, data, 0x28);
    const uint8_t mode = data[0x28];
    const uint32_t chunk = (data[0x29] & 0x0f) + 1u;
    const uint32_t seekTo = uint32_t(data[0x2a]) | (uint32_t(data[0x2b]) << 8);
    uint32_t tableBytes = ReadU32(data + 0x2c);
    const size_t rest = size - kHeader;
    if (tableBytes > rest)
        tableBytes = static_cast<uint32_t>(rest);
    const size_t packetBytes = rest - tableBytes;

    // MemAlloc(_wmaLength, 4) / MemAlloc(_length, 4): exact sizes, no padding.
    uint32_t* table = tableBytes > 0 ? static_cast<uint32_t*>(std::malloc(tableBytes)) : nullptr;
    if (table != nullptr)
        std::memcpy(table, data + kHeader, tableBytes);
    uint8_t* packets = packetBytes > 0 ? static_cast<uint8_t*>(std::malloc(packetBytes)) : nullptr;
    if (packets != nullptr)
        std::memcpy(packets, data + kHeader + tableBytes, packetBytes);
    const uint32_t packetCount = tableBytes >> 2;   // DecodeXwmaJob's `_wmaLength >> 2`

    Audio::XwmaDecoder dec;
    if ((mode & 1) == 0) {
        // DecodeXwmaJob.
        if (dec.Open(format, packets, packetBytes, table, packetCount)) {
            const uint64_t total = dec.TotalSamples();
            size_t cap = dec.MaxSamplesFor(dec.PacketCount());
            if (cap > kPcmCapSamples)
                cap = kPcmCapSamples;
            size_t outSamples = total < cap ? static_cast<size_t>(total) : cap;
            if ((mode & 2) != 0 && outSamples > 7)
                outSamples = 7;
            if (outSamples > 0) {
                std::vector<int16_t> pcm(outSamples * dec.Channels());
                size_t got = dec.DecodePackets(0, dec.PacketCount(), pcm.data(), outSamples);
                // Drain the pending samples a short `out` left behind.
                for (int guard = 0; guard < 64 && got > 0; ++guard)
                    got = dec.DecodePackets(dec.NextPacket(), 0, pcm.data(), outSamples);
            }
            if ((mode & 4) != 0 && seekTo < dec.PacketCount()) {
                std::vector<int16_t> pcm(dec.MaxSamplesFor(dec.PacketCount() - seekTo) * dec.Channels());
                dec.DecodePackets(seekTo, dec.PacketCount() - seekTo, pcm.data(), pcm.size() / dec.Channels());
            }
        }
    } else if (dec.Open(format, nullptr, 0, table, packetCount)) {
        // Stream::Run: chunks of `chunk` packets, each copied to its own exact-size buffer.
        const uint32_t align = dec.BlockAlign();
        const size_t have = align != 0 ? packetBytes / align : 0;
        uint32_t next = 0;
        bool sought = false;
        for (int step = 0; step < 4096; ++step) {
            if ((mode & 4) != 0 && !sought && step == 1) {
                next = seekTo;
                sought = true;
            }
            if (next >= dec.PacketCount() || next >= have)
                break;
            uint32_t n = chunk;
            if (n > dec.PacketCount() - next)
                n = dec.PacketCount() - next;
            if (n > have - next)
                n = static_cast<uint32_t>(have - next);
            std::vector<uint8_t> block(static_cast<size_t>(n) * align);
            std::memcpy(block.data(), packets + static_cast<size_t>(next) * align, block.size());
            size_t outSamples = dec.MaxSamplesFor(n);
            if ((mode & 2) != 0 && outSamples > 5)
                outSamples = 5;
            std::vector<int16_t> pcm(outSamples * dec.Channels());
            DecodeChunkAny(dec, next, n, block.data(), block.size(), pcm.data(), outSamples);
            next = dec.NextPacket();
        }
        std::vector<int16_t> tail(64 * dec.Channels());
        DecodeChunkAny(dec, dec.NextPacket(), 0, nullptr, 0, tail.data(), 64);
    }

    std::free(packets);
    std::free(table);
    std::free(format);
    return 0;
}
