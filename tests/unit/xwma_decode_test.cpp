// ============================================================================
// tests/unit/xwma_decode_test.cpp — Audio::XwmaDecoder over one shipped sound
// (Phase D.4 SDL A3; the fixture moved out of the tree 2026-10-17)
// ============================================================================
// THE SOUND. `Audio\Animal\Sheep1.wav` exactly as the game stores it: the 13,452-byte
// run SoundBuffer::PlatformSerialize reads out of WinData/data0.pkg at 0x2d802 -- int32
// length, int32 table bytes, the seek table, the 0x28-byte format window, the packets.
// Mono, 44.1 kHz, 48 kbit/s, 6 packets of 2230 bytes, 65536 samples (layout table in
// data/README.md). No game bytes are stored in the tree (CLAUDE.md): the run is read AT
// TEST TIME from the user's install -- $BANISHED_DIR, C:/GOG Games/Banished or
// /mnt/c/GOG Games/Banished -- first at the known offset (verified by its sha256), else by
// the scan that found it (a 0x0161 WAVEFORMATEX preceded by its two counts). Without an
// install every test here SKIPS (UNIT_SKIP: visible in the summary, never `ok`).
//
// NO REFERENCE VALUES are stored (user ruling 2026-10-17: numbers derived from the retail
// samples are game content, however few). The decoder was proven against XAudio2 2.7 on
// 2026-10-10 (max |diff| 1 LSB, RMS 0.025 LSB, 99.94 % identical; method in
// third_party/wmadec/EXTRACT.md), but that capture and the per-block fingerprint that
// replaced it are gone. What the decode test asserts now is structural: the sample count
// from the xWMA header, a non-silent RMS, no clipping at the int16 rails, the chunked
// decode equal to the whole decode sample for sample, and a repeat decode identical.
// ============================================================================
#include "unit.h"
#include "audio/xwma_decode.h"
#include "audio/load_decode_batch.h"   // block SNDPAR: the batch test at the end
#include "system/parallel_for.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

// --------------------------------------------------------------- the install
constexpr const char* kPackage      = "/WinData/data0.pkg";
constexpr int32_t     kKnownOffset  = 0x2d802;
constexpr size_t      kRunBytes     = 13452;
constexpr const char* kRunSha256    = "30da65356d18a1e0c9c0c92a208da2bfd03fc60c61946003de3d8980496fea4c";

uint32_t ReadU32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
uint16_t ReadU16(const uint8_t* p) { return uint16_t(p[0] | (p[1] << 8)); }

// A plain SHA-256 (FIPS 180-4), enough to verify one 13 KB run.
std::string Sha256Hex(const uint8_t* data, size_t len) {
    static const uint32_t k[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
    uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                     0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    std::vector<uint8_t> msg(data, data + len);
    msg.push_back(0x80);
    while (msg.size() % 64 != 56) msg.push_back(0);
    const uint64_t bits = uint64_t(len) * 8;
    for (int i = 7; i >= 0; --i) msg.push_back(uint8_t(bits >> (8 * i)));
    auto rotr = [](uint32_t x, int n) { return (x >> n) | (x << (32 - n)); };
    for (size_t off = 0; off < msg.size(); off += 64) {
        uint32_t w[64];
        for (int i = 0; i < 16; ++i)
            w[i] = (uint32_t(msg[off + 4 * i]) << 24) | (uint32_t(msg[off + 4 * i + 1]) << 16) |
                   (uint32_t(msg[off + 4 * i + 2]) << 8) | uint32_t(msg[off + 4 * i + 3]);
        for (int i = 16; i < 64; ++i) {
            const uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; ++i) {
            const uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            const uint32_t ch = (e & f) ^ (~e & g);
            const uint32_t t1 = hh + S1 + ch + k[i] + w[i];
            const uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            const uint32_t t2 = S0 + maj;
            hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }
    char hex[65];
    for (int i = 0; i < 8; ++i) std::snprintf(hex + 8 * i, 9, "%08x", h[i]);
    return std::string(hex, 64);
}

std::vector<uint8_t> ReadRange(FILE* f, int32_t offset, size_t bytes) {
    std::vector<uint8_t> out;
    if (std::fseek(f, offset, SEEK_SET) != 0) return out;
    out.resize(bytes);
    if (std::fread(out.data(), 1, bytes, f) != bytes) out.clear();
    return out;
}

// The scan fallback only: the whole package in memory, bounded by the file's size
// (WinData/data0.pkg is 132,041,595 bytes, 126 MB, on the GOG install). The
// known-offset path reads 13 KB and never calls this.
std::vector<uint8_t> ReadWhole(FILE* f) {
    std::vector<uint8_t> out;
    if (std::fseek(f, 0, SEEK_SET) != 0) return out;
    std::vector<uint8_t> chunk(size_t(1) << 22);
    size_t n;
    while ((n = std::fread(chunk.data(), 1, chunk.size(), f)) > 0)
        out.insert(out.end(), chunk.begin(), chunk.begin() + std::ptrdiff_t(n));
    return out;
}

// Is this a well-formed PlatformSerialize run with the sound's shape (mono, 44.1 kHz,
// six 2230-byte packets, the table ending at 0x20000 decoded bytes)?
bool LooksLikeTheSound(const uint8_t* d, size_t n) {
    if (n < 8) return false;
    const uint32_t length = ReadU32(d), tableBytes = ReadU32(d + 4);
    if (length != 13380 || tableBytes != 24) return false;
    if (n < 8 + tableBytes + 0x28 + length) return false;
    const uint8_t* fmt = d + 8 + tableBytes;
    return ReadU16(fmt) == 0x0161 && ReadU16(fmt + 2) == 1 && ReadU32(fmt + 4) == 44100 &&
           ReadU16(fmt + 12) == 2230 && ReadU32(d + 8 + 20) == 0x20000;
}

// The scan data/README.md describes: a 0x0161 format window whose two counts sit in
// front of a strictly rising table. Returns the run's offset or -1.
std::ptrdiff_t ScanForTheSound(const std::vector<uint8_t>& pkg) {
    const uint8_t* d = pkg.data();
    const size_t n = pkg.size();
    for (size_t i = 32; i + 0x28 <= n; ++i) {
        if (d[i] != 0x61 || d[i + 1] != 0x01) continue;
        const size_t start = i - 32;   // 8 bytes of counts + a 24-byte table
        if (LooksLikeTheSound(d + start, n - start)) return std::ptrdiff_t(start);
    }
    return -1;
}

// The PlatformSerialize run, split into the four pieces SoundBuffer holds. Loaded once
// from the install; `why` says what was missing when `ok` is false.
struct Fixture {
    std::vector<uint8_t>  raw;
    std::vector<uint32_t> table;
    const uint8_t* format = nullptr;
    const uint8_t* packets = nullptr;
    size_t bytes = 0;
    bool ok = false;
    std::string why;
    std::string source;

    static const Fixture& Get() { static const Fixture fx; return fx; }

    bool Open(Audio::XwmaDecoder& dec) const {
        return ok && dec.Open(format, packets, bytes, table.data(), uint32_t(table.size()));
    }

private:
    Fixture() {
        std::vector<std::string> dirs;
        if (const char* env = std::getenv("BANISHED_DIR"); env && *env) dirs.push_back(env);
        dirs.push_back("C:/GOG Games/Banished");
        dirs.push_back("/mnt/c/GOG Games/Banished");
        FILE* f = nullptr;
        std::string path;
        for (const std::string& dir : dirs) {
            path = dir + kPackage;
            f = std::fopen(path.c_str(), "rb");
            if (f) break;
        }
        if (!f) { why = "no game install (WinData/data0.pkg not found)"; return; }

        raw = ReadRange(f, kKnownOffset, kRunBytes);
        if (!raw.empty() && Sha256Hex(raw.data(), raw.size()) == kRunSha256) {
            source = path + " @0x2d802 (sha256 verified)";
        } else {
            raw.clear();
            const std::vector<uint8_t> pkg = ReadWhole(f);
            const std::ptrdiff_t at = ScanForTheSound(pkg);
            if (at >= 0) {
                raw.assign(pkg.begin() + at, pkg.begin() + at + std::ptrdiff_t(kRunBytes));
                char where[64];
                std::snprintf(where, sizeof(where), " @0x%tx (found by scan)", at);
                source = path + where;
            }
        }
        std::fclose(f);
        if (raw.empty()) { why = "Sheep1 run not found in " + path; return; }
        Split();
    }

    void Split() {
        const int32_t length = int32_t(ReadU32(&raw[0]));
        const int32_t tableBytes = int32_t(ReadU32(&raw[4]));
        if (length <= 0 || tableBytes <= 0 || tableBytes % 4 != 0) { why = "malformed run"; return; }
        if (raw.size() != 8u + size_t(tableBytes) + 0x28u + size_t(length)) { why = "malformed run"; return; }
        table.resize(size_t(tableBytes) / 4);
        for (size_t i = 0; i < table.size(); ++i) table[i] = ReadU32(&raw[8 + 4 * i]);
        format = raw.data() + 8 + tableBytes;
        packets = format + 0x28;
        bytes = size_t(length);
        ok = true;
    }
};

// Every test starts here: the fixture, or a visible skip.
#define XWMA_FIXTURE_OR_SKIP(fx) \
    const Fixture& fx = Fixture::Get(); \
    if (!fx.ok) UNIT_SKIP(fx.why.c_str())

constexpr size_t kTotal = 65536;

std::vector<int16_t> DecodeWhole(Audio::XwmaDecoder& dec) {
    std::vector<int16_t> pcm(size_t(dec.TotalSamples()) * dec.Channels());
    const size_t n = dec.DecodePackets(0, dec.PacketCount(), pcm.data(), size_t(dec.TotalSamples()));
    pcm.resize(n * dec.Channels());
    return pcm;
}

struct Diff { int maxAbs = 0; double rms = 0.0; double identical = 0.0; };
Diff Compare(const int16_t* a, const int16_t* b, size_t n) {
    Diff d;
    if (n == 0) return d;
    double sq = 0.0;
    size_t same = 0;
    for (size_t i = 0; i < n; ++i) {
        const int e = int(a[i]) - int(b[i]);
        const int ae = e < 0 ? -e : e;
        if (ae > d.maxAbs) d.maxAbs = ae;
        sq += double(e) * e;
        if (e == 0) ++same;
    }
    d.rms = std::sqrt(sq / double(n));
    d.identical = double(same) / double(n);
    return d;
}

}  // namespace

UNIT_TEST(xwma_fixture_opens_as_the_game_stores_it) {
    XWMA_FIXTURE_OR_SKIP(fx);
    std::printf("  fixture: %s\n", fx.source.c_str());
    CHECK_EQ(fx.raw.size(), kRunBytes);
    Audio::XwmaDecoder dec;
    CHECK(fx.Open(dec));
    CHECK(dec.IsOpen());
    CHECK_EQ(dec.Channels(), 1u);
    CHECK_EQ(dec.SampleRate(), 44100u);
    CHECK_EQ(dec.BlockAlign(), 2230u);
    CHECK_EQ(dec.PacketCount(), 6u);
    CHECK_EQ(dec.FrameLength(), 2048u);
    CHECK_EQ(dec.TotalSamples(), uint64_t(65536));   // seek table 0x20000 bytes / 2
    CHECK_EQ(dec.SeekSamples(0), uint64_t(0));
    CHECK_EQ(dec.SeekSamples(3), uint64_t(30720));   // 0xf000 / 2
    CHECK_EQ(dec.NextPacket(), 0u);
}

UNIT_TEST(xwma_decode_at_load_is_sane_chunk_exact_and_deterministic) {
    XWMA_FIXTURE_OR_SKIP(fx);
    Audio::XwmaDecoder dec;
    CHECK(fx.Open(dec));
    const std::vector<int16_t> pcm = DecodeWhole(dec);
    CHECK_EQ(pcm.size(), kTotal);                    // the count the xWMA header promises
    CHECK_EQ(dec.NextPacket(), 6u);
    CHECK_EQ(dec.PendingSamples(), size_t(0));
    CHECK(!dec.Failed());
    if (pcm.size() != kTotal) return;

    // Not silent, not clipped: a dead decoder gives zeros, a broken one noise at the rails.
    double sq = 0.0;
    size_t rails = 0;
    for (int16_t v : pcm) {
        sq += double(v) * v;
        if (v >= 32767 || v <= -32768) ++rails;
    }
    const double rms = std::sqrt(sq / double(pcm.size()));
    std::printf("  xwma decode: RMS %.1f LSB, %zu samples at the rails\n", rms, rails);
    CHECK(rms > 10.0);
    CHECK(rails * 1000 < pcm.size());                // under 0.1 %

    // Feeding the packets in pieces equals the whole decode exactly.
    for (uint32_t chunk : {1u, 4u}) {
        Audio::XwmaDecoder cd;
        CHECK(fx.Open(cd));
        std::vector<int16_t> got;
        std::vector<int16_t> buf(1500);
        uint32_t packet = 0;
        while (packet < cd.PacketCount()) {
            const uint32_t n = std::min(chunk, cd.PacketCount() - packet);
            size_t w = cd.DecodePackets(packet, n, buf.data(), buf.size());
            got.insert(got.end(), buf.begin(), buf.begin() + std::ptrdiff_t(w));
            packet += n;
            while (cd.PendingSamples() > 0) {
                w = cd.DecodePackets(cd.NextPacket(), 0, buf.data(), buf.size());
                got.insert(got.end(), buf.begin(), buf.begin() + std::ptrdiff_t(w));
            }
        }
        CHECK(got == pcm);
    }

    // Determinism: a second decoder, and a second pass of the first, give the same samples.
    Audio::XwmaDecoder other;
    CHECK(fx.Open(other));
    CHECK(DecodeWhole(other) == pcm);
    CHECK(DecodeWhole(dec) == pcm);
}


// The Stream worker's shape: the pass continues across calls, and output that does
// not fit is carried. Same samples, same order, as the one-call decode.
UNIT_TEST(xwma_chunks_continue_the_pass) {
    XWMA_FIXTURE_OR_SKIP(fx);
    Audio::XwmaDecoder whole;
    CHECK(fx.Open(whole));
    const std::vector<int16_t> one = DecodeWhole(whole);

    for (uint32_t chunk : {1u, 2u, 4u}) {
        Audio::XwmaDecoder dec;
        CHECK(fx.Open(dec));
        std::vector<int16_t> got;
        std::vector<int16_t> buf(1500);           // smaller than a chunk: forces the carry
        uint32_t packet = 0;
        while (packet < dec.PacketCount()) {
            const uint32_t n = std::min(chunk, dec.PacketCount() - packet);
            size_t w = dec.DecodePackets(packet, n, buf.data(), buf.size());
            got.insert(got.end(), buf.begin(), buf.begin() + std::ptrdiff_t(w));
            packet += n;
            CHECK_EQ(dec.NextPacket(), packet);
            while (dec.PendingSamples() > 0) {
                w = dec.DecodePackets(dec.NextPacket(), 0, buf.data(), buf.size());
                got.insert(got.end(), buf.begin(), buf.begin() + std::ptrdiff_t(w));
            }
        }
        CHECK_EQ(got.size(), one.size());
        CHECK(got == one);
        CHECK_EQ(dec.DecodePackets(dec.NextPacket(), 1, buf.data(), buf.size()), size_t(0));
    }
}

// The Stream worker's real shape: no whole track in memory. Open with the table and
// format only; each chunk's packets arrive in a reused buffer (Stream::Block's ring),
// which is overwritten as soon as DecodeChunk returns.
UNIT_TEST(xwma_stream_chunks_from_a_reused_buffer) {
    XWMA_FIXTURE_OR_SKIP(fx);
    Audio::XwmaDecoder whole;
    CHECK(fx.Open(whole));
    const std::vector<int16_t> one = DecodeWhole(whole);

    for (uint32_t chunk : {2u, 16u}) {
        Audio::XwmaDecoder dec;
        CHECK(dec.Open(fx.format, nullptr, 0, fx.table.data(), uint32_t(fx.table.size())));
        CHECK_EQ(dec.TotalSamples(), uint64_t(65536));
        std::vector<uint8_t> block(size_t(chunk) * dec.BlockAlign());
        std::vector<int16_t> buf(dec.MaxSamplesFor(chunk));
        std::vector<int16_t> got;
        uint32_t packet = 0;
        while (packet < dec.PacketCount()) {
            const uint32_t n = std::min(chunk, dec.PacketCount() - packet);
            std::copy(fx.packets + size_t(packet) * dec.BlockAlign(),
                      fx.packets + size_t(packet + n) * dec.BlockAlign(), block.begin());
            const size_t w = dec.DecodeChunk(packet, n, block.data(), block.size(), buf.data(), buf.size());
            got.insert(got.end(), buf.begin(), buf.begin() + std::ptrdiff_t(w));
            std::fill(block.begin(), block.end(), uint8_t(0xA5));   // the ring slot is reused
            packet += n;
        }
        CHECK_EQ(dec.PendingSamples(), size_t(0));
        CHECK(got == one);
        // Without packets, the contiguous entry point has nothing to read.
        CHECK_EQ(dec.DecodePackets(0, 1, buf.data(), buf.size()), size_t(0));
    }
}

// A pass started at packet k > 0 is a fresh decoder there, as a fresh XAudio2 voice
// would be: TotalSamples - SeekSamples(k) - FrameLength samples, the tail of the
// packet-0 pass (XAudio2 2.7 measured the same, third_party/wmadec/EXTRACT.md rule 4).
UNIT_TEST(xwma_pass_from_a_later_packet_is_the_tail) {
    XWMA_FIXTURE_OR_SKIP(fx);
    Audio::XwmaDecoder dec;
    CHECK(fx.Open(dec));
    const std::vector<int16_t> one = DecodeWhole(dec);

    const uint32_t k = 3;
    const size_t expect = size_t(dec.TotalSamples() - dec.SeekSamples(k) - dec.FrameLength());
    CHECK_EQ(expect, size_t(32768));
    std::vector<int16_t> tail(expect + 4096);
    const size_t n = dec.DecodePackets(k, dec.PacketCount() - k, tail.data(), tail.size());
    CHECK_EQ(n, expect);
    if (n != expect || one.size() < n) return;
    const Diff d = Compare(tail.data(), one.data() + (one.size() - n), n);
    CHECK(d.maxAbs <= 2);
    CHECK(d.identical >= 0.995);

    // Back to packet 0: a new pass, the same samples as the first.
    const std::vector<int16_t> again = DecodeWhole(dec);
    CHECK(again == one);
}

UNIT_TEST(xwma_open_refuses_what_it_cannot_decode) {
    XWMA_FIXTURE_OR_SKIP(fx);
    Audio::XwmaDecoder dec;
    std::vector<uint8_t> fmt(fx.format, fx.format + 0x28);

    std::vector<uint8_t> pcmTag = fmt;                 // WAVE_FORMAT_PCM
    pcmTag[0] = 0x01; pcmTag[1] = 0x00;
    CHECK(!dec.Open(pcmTag.data(), fx.packets, fx.bytes, fx.table.data(), uint32_t(fx.table.size())));
    CHECK(!dec.IsOpen());

    std::vector<uint8_t> six = fmt;                    // 6 channels
    six[2] = 6;
    CHECK(!dec.Open(six.data(), fx.packets, fx.bytes, fx.table.data(), uint32_t(fx.table.size())));

    CHECK(!dec.Open(fmt.data(), fx.packets, fx.bytes - 1, fx.table.data(), uint32_t(fx.table.size())));
    CHECK(!dec.Open(fmt.data(), fx.packets, fx.bytes, fx.table.data(), 0));
    CHECK(!dec.IsOpen());
    CHECK_EQ(dec.DecodePackets(0, 1, nullptr, 0), size_t(0));

    CHECK(dec.Open(fmt.data(), fx.packets, fx.bytes, fx.table.data(), uint32_t(fx.table.size())));
    dec.Close();
    CHECK(!dec.IsOpen());
    CHECK_EQ(dec.TotalSamples(), uint64_t(0));
}

// Block SNDPAR: the real WMA v2 decode through Audio::LoadDecode's batch (the path a load's
// SoundBuffers take, src/engine/audio/load_decode_batch.h) on 4 pool threads, 24 jobs over
// the fixture at once: every job's PCM is the inline decode's, byte for byte, and the
// finishes run in Defer order. tests/unit/load_decode_batch_test.cpp pins the batch itself
// without an install.
namespace {
struct BatchJob {
    const Fixture* fx = nullptr;
    std::vector<int16_t> pcm;      // decode's output
    std::vector<int16_t> published;
    int order = -1;
};
int g_batchFinishes = 0;           // finishes run on the scope's thread only
void BatchDecode(void* item, void*) {
    BatchJob* j = static_cast<BatchJob*>(item);
    Audio::XwmaDecoder dec;
    if (j->fx->Open(dec)) j->pcm = DecodeWhole(dec);
}
void BatchFinish(void* item, void*) {
    BatchJob* j = static_cast<BatchJob*>(item);
    j->published.swap(j->pcm);
    j->order = g_batchFinishes++;
}
}  // namespace

UNIT_TEST(xwma_decode_through_the_load_batch_is_byte_identical) {
    XWMA_FIXTURE_OR_SKIP(fx);
    Audio::XwmaDecoder dec;
    CHECK(fx.Open(dec));
    const std::vector<int16_t> one = DecodeWhole(dec);
    CHECK_EQ(one.size(), kTotal);

    System::Parallel::SetWorkers(4);
    std::vector<BatchJob> jobs(24);
    g_batchFinishes = 0;
    {
        Audio::LoadDecode::Scope batch;
        for (BatchJob& j : jobs) {
            j.fx = &fx;
            CHECK(Audio::LoadDecode::Defer(&j, nullptr, fx.bytes, &BatchDecode, &BatchFinish));
        }
    }
    int same = 0, inOrder = 0;
    for (size_t i = 0; i < jobs.size(); ++i) {
        if (jobs[i].published == one) ++same;
        if (jobs[i].order == int(i)) ++inOrder;
    }
    std::printf("  load batch: %d/24 byte-identical to the inline decode, %d in Defer order\n",
                same, inOrder);
    CHECK_EQ(same, 24);
    CHECK_EQ(inOrder, 24);
    System::Parallel::Shutdown();
}
