// ============================================================================
// tests/unit/xwma_tos16_test.cpp — Audio::FloatToS16 / PlanesToS16 against the
// original conversion (LOADPLAN block AUDIODEC, docs/extensions/handins/AUDIODEC.md)
// ============================================================================
// OldToS16 below is the decoder's per-sample body before AUDIODEC, verbatim; it
// lives here only. Every check is
// bit-exact: the new conversion must return the identical int16 for every input.
//
//   * xwma_tos16_float_classes   specials (+-0, denormals, FLT_MIN, FLT_MAX, +-inf,
//     NaN payloads), every integer and half-integer y = x * 32768 in [-33100, 33100]
//     with 8 ulps either side of it, a 1 / 401 stride over all 2^32 bit patterns,
//     and 2M pseudo-random floats in [-1.01, 1.01]. Scalar and the SSE2 mono path.
//   * xwma_tos16_layouts         mono / stereo / 3 channels, every frame count 0..40
//     and plane offset 0..9 (the vector body, its tail, the generic loop).
//   * xwma_tos16_every_shipped_sound   the content proof: every xWMA SoundBuffer in
//     the game's packages decoded by XwmaDecoder against a reference pass over the
//     raw WMA v2 API converted with OldToS16 (whole, and one packet per call into a
//     small buffer: the carry path); one FNV-1a hash of all the PCM printed.
//     Skipped (and said so) when no game install is found. Only the sounds the
//     packages store as plain runs are reachable this way (23 of them); the rest
//     are covered by the game's [SoundDecode] pcmHash line (debug exe).
// ============================================================================
#include "unit.h"
#include "audio/xwma_decode.h"
#include "wmav2_decoder.h"

#include <chrono>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace {

// The original body (src/engine/audio/xwma_decode.cpp before AUDIODEC).
int16_t OldToS16(float x) {
    const float v = std::nearbyint(x * 32768.0f);
    if (v >= 32767.0f) return 32767;
    if (v <= -32768.0f) return -32768;
    if (v != v) return 0;
    return static_cast<int16_t>(v);
}

float FromBits(uint32_t b) { float f; std::memcpy(&f, &b, sizeof f); return f; }
uint32_t ToBits(float f) { uint32_t b; std::memcpy(&b, &f, sizeof b); return b; }

// Checks a batch through the scalar and the mono vector path; counts mismatches,
// prints the first few.
struct Checker {
    std::vector<float> batch;
    std::vector<int16_t> out;
    uint64_t checked = 0;
    uint64_t bad = 0;

    void Add(float x) {
        batch.push_back(x);
        if (batch.size() == 4096) Flush();
    }
    void Report(float x, int16_t got, int16_t want, const char* path) {
        if (++bad <= 8)
            std::printf("  mismatch (%s) x=%a bits=%08x got=%d want=%d\n", path, double(x), ToBits(x),
                        got, want);
    }
    void Flush() {
        if (batch.empty()) return;
        out.assign(batch.size(), int16_t(0x5a5a));
        const float* plane = batch.data();
        Audio::PlanesToS16(&plane, 0, batch.size(), 1, out.data());
        for (size_t i = 0; i < batch.size(); ++i) {
            const int16_t want = OldToS16(batch[i]);
            const int16_t one = Audio::FloatToS16(batch[i]);
            if (one != want) Report(batch[i], one, want, "scalar");
            if (out[i] != want) Report(batch[i], out[i], want, "planes");
        }
        checked += batch.size();
        batch.clear();
    }
};

}  // namespace

UNIT_TEST(xwma_tos16_float_classes) {
    Checker c;
    // Specials.
    const float specials[] = {
        0.0f, -0.0f, FLT_MIN, -FLT_MIN, FLT_MAX, -FLT_MAX, FLT_EPSILON, -FLT_EPSILON,
        FromBits(0x00000001u), FromBits(0x80000001u), FromBits(0x007fffffu), FromBits(0x807fffffu),
        std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(),
        std::numeric_limits<float>::quiet_NaN(), -std::numeric_limits<float>::quiet_NaN(),
        FromBits(0x7fc00001u), FromBits(0xffc12345u), FromBits(0x7f800001u), FromBits(0xff800001u),
        FromBits(0x7fffffffu), FromBits(0xffffffffu),
        1.0f, -1.0f, 1.01f, -1.01f, 0.5f / 32768.0f, -0.5f / 32768.0f, 1.5f / 32768.0f,
        -1.5f / 32768.0f, 2.5f / 32768.0f, -2.5f / 32768.0f,
        32766.5f / 32768.0f, 32767.0f / 32768.0f, 32767.5f / 32768.0f, 32768.5f / 32768.0f,
        -32767.5f / 32768.0f, -32768.0f / 32768.0f, -32768.5f / 32768.0f, -32769.5f / 32768.0f,
        2147483648.0f / 32768.0f, -2147483648.0f / 32768.0f, 4294967296.0f / 32768.0f, 1e20f, -1e20f};
    for (float f : specials) c.Add(f);
    // The boundary values themselves, pinned (not only "same as before").
    CHECK_EQ(Audio::FloatToS16(32767.5f / 32768.0f), int16_t(32767));    // tie -> 32768 -> saturates
    CHECK_EQ(Audio::FloatToS16(32766.5f / 32768.0f), int16_t(32766));    // tie -> even
    CHECK_EQ(Audio::FloatToS16(-32767.5f / 32768.0f), int16_t(-32768));  // tie -> even -32768
    CHECK_EQ(Audio::FloatToS16(-32768.5f / 32768.0f), int16_t(-32768));
    CHECK_EQ(Audio::FloatToS16(0.5f / 32768.0f), int16_t(0));
    CHECK_EQ(Audio::FloatToS16(1.5f / 32768.0f), int16_t(2));
    CHECK_EQ(Audio::FloatToS16(-2.5f / 32768.0f), int16_t(-2));
    CHECK_EQ(Audio::FloatToS16(std::numeric_limits<float>::quiet_NaN()), int16_t(0));
    CHECK_EQ(Audio::FloatToS16(std::numeric_limits<float>::infinity()), int16_t(32767));
    CHECK_EQ(Audio::FloatToS16(-std::numeric_limits<float>::infinity()), int16_t(-32768));

    // Every integer and half-integer y in [-33100, 33100] (|x| <= 1.0101), 8 ulps either side.
    for (int k = -33100; k <= 33100; ++k) {
        for (int half = 0; half < 2; ++half) {
            const float y = float(k) + (half ? 0.5f : 0.0f);
            float lo = y, hi = y;
            c.Add(y / 32768.0f);
            for (int u = 0; u < 8; ++u) {
                lo = std::nextafter(lo, -INFINITY);
                hi = std::nextafter(hi, INFINITY);
                c.Add(lo / 32768.0f);
                c.Add(hi / 32768.0f);
            }
        }
    }
    // A 1 / 401 stride over every bit pattern (all classes, both signs, NaN payloads).
    for (uint64_t b = 0; b <= 0xffffffffull; b += 401) c.Add(FromBits(uint32_t(b)));
    // Pseudo-random floats in [-1.01, 1.01] (the decoder's real range).
    uint64_t s = 0x9e3779b97f4a7c15ull;
    for (int i = 0; i < 2000000; ++i) {
        s = s * 6364136223846793005ull + 1442695040888963407ull;
        const float u = float(uint32_t(s >> 40)) / float(1u << 24);   // [0, 1)
        c.Add((u * 2.0f - 1.0f) * 1.01f);
    }
    c.Flush();
    std::printf("  xwma_tos16: %llu floats checked, %llu mismatches\n",
                static_cast<unsigned long long>(c.checked), static_cast<unsigned long long>(c.bad));
    CHECK_EQ(c.bad, uint64_t(0));
    CHECK(c.checked > 14000000u);
}

UNIT_TEST(xwma_tos16_layouts) {
    // Three planes of distinct values crossing every class the decoder can produce.
    std::vector<float> p[3];
    for (int c = 0; c < 3; ++c)
        for (int i = 0; i < 64; ++i)
            p[c].push_back(float((i * 7 + c * 13) % 41 - 20) * 0.0517f + (i % 3 == 0 ? 0.5f / 32768.0f : 0.0f));
    p[0][5] = 1.5f;  p[1][6] = -1.5f;  p[2][7] = std::numeric_limits<float>::quiet_NaN();
    p[0][9] = -0.0f; p[1][12] = 32767.5f / 32768.0f;
    const float* planes[3] = {p[0].data(), p[1].data(), p[2].data()};
    int bad = 0;
    for (uint32_t ch = 1; ch <= 3; ++ch)
        for (size_t from = 0; from < 10; ++from)
            for (size_t frames = 0; frames <= 40; ++frames) {
                std::vector<int16_t> got(frames * ch + 4, int16_t(0x7777));
                Audio::PlanesToS16(planes, from, frames, ch, got.data());
                size_t o = 0;
                for (size_t s = 0; s < frames; ++s)
                    for (uint32_t c = 0; c < ch; ++c, ++o)
                        if (got[o] != OldToS16(planes[c][from + s])) ++bad;
                for (size_t g = frames * ch; g < got.size(); ++g)   // nothing written past the end
                    if (got[g] != int16_t(0x7777)) ++bad;
            }
    CHECK_EQ(bad, 0);
}

// ---------------------------------------------------------------------------
// The content proof over the game's packages.
// ---------------------------------------------------------------------------
namespace {

uint16_t U16(const uint8_t* p) { return uint16_t(p[0] | (p[1] << 8)); }
uint32_t U32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

std::vector<uint8_t> ReadFile(const std::string& path) {
    std::vector<uint8_t> bytes;
    FILE* f = std::fopen(path.c_str(), "rb");
    if (f == nullptr) return bytes;
    std::vector<uint8_t> chunk(size_t(1) << 22);
    size_t n;
    while ((n = std::fread(chunk.data(), 1, chunk.size(), f)) > 0)
        bytes.insert(bytes.end(), chunk.begin(), chunk.begin() + std::ptrdiff_t(n));
    std::fclose(f);
    return bytes;
}

// One PlatformSerialize run (xwma_decode_test.cpp's fixture layout): int32 packet
// bytes, int32 table bytes, the table, the 0x28-byte format window, the packets.
struct Sound {
    const uint8_t* format;
    const uint8_t* packets;
    size_t bytes;
    std::vector<uint32_t> table;
};

// Finds every run whose format window is mono / stereo WMA v2 and whose two counts
// agree with it: table bytes == 4 * packets, packet bytes == packets * nBlockAlign,
// the table strictly increasing. It finds only the sounds stored uncompressed
// (tests/unit/data/README.md); this is the scan that cut the fixture.
void Scan(const std::vector<uint8_t>& pkg, std::vector<Sound>& out) {
    const size_t n = pkg.size();
    const uint8_t* d = pkg.data();
    for (size_t i = 12; i + 0x28 <= n; ++i) {
        if (d[i] != 0x61 || d[i + 1] != 0x01) continue;
        const uint16_t ch = U16(d + i + 2);
        const uint32_t rate = U32(d + i + 4);
        const uint32_t avg = U32(d + i + 8);
        const uint16_t align = U16(d + i + 12);
        if (ch < 1 || ch > 2 || rate == 0 || rate > 192000 || avg == 0 || align == 0) continue;
        uint32_t prev = 0xffffffffu;
        for (size_t j = 1; 4 * j + 8 <= i; ++j) {
            const uint32_t entry = U32(d + i - 4 * j);
            if (entry == 0 || entry >= prev) break;   // the table rises strictly
            prev = entry;
            if (U32(d + i - 4 * j - 4) == 4 * j && U32(d + i - 4 * j - 8) == uint32_t(j) * align) {
                const size_t bytes = size_t(j) * align;
                if (i + 0x28 + bytes > n) break;
                Sound s{d + i, d + i + 0x28, bytes, {}};
                for (size_t t = 0; t < j; ++t) s.table.push_back(U32(d + i - 4 * j + 4 * t));
                out.push_back(std::move(s));
                break;
            }
        }
    }
}

// The reference: one pass from packet 0 over the raw WMA v2 API, the decoder's
// frame rules (2 x frame_len dropped, the table's total kept, a refused packet
// contributes nothing, the last packet drains twice), converted with OldToS16.
std::vector<int16_t> ReferenceDecode(const Sound& s) {
    std::vector<int16_t> pcm;
    const uint32_t ch = U16(s.format + 2);
    Wmav2Params params;
    uint8_t extradata[6];
    wmav2_xwma_extradata(extradata);
    params.channels = int(ch);
    params.sample_rate = int(U32(s.format + 4));
    params.bit_rate = int(U32(s.format + 8) * 8);
    params.block_align = int(U16(s.format + 12));
    params.extradata = extradata;
    params.extradata_size = int(sizeof(extradata));
    Wmav2Decoder* dec = nullptr;
    if (wmav2_open(&params, &dec) < 0) return pcm;
    uint64_t skip = uint64_t(wmav2_frame_len(dec)) * 2;
    uint64_t remaining = s.table.back() / (2u * ch);
    auto append = [&](const float* const* planes, int frames) {
        uint64_t nf = frames > 0 ? uint64_t(frames) : 0, from = 0;
        if (skip > 0) { const uint64_t drop = nf < skip ? nf : skip; skip -= drop; from = drop; }
        uint64_t keep = nf - from;
        if (keep > remaining) keep = remaining;
        remaining -= keep;
        for (uint64_t k = 0; k < keep; ++k)
            for (uint32_t c = 0; c < ch; ++c) pcm.push_back(OldToS16(planes[c][from + k]));
    };
    const size_t count = s.table.size();
    for (size_t p = 0; p < count; ++p) {
        const float* const* planes = nullptr;
        int frames = 0;
        if (wmav2_decode_packet(dec, s.packets + p * size_t(params.block_align), params.block_align,
                                &planes, &frames) >= 0)
            append(planes, frames);
        if (p + 1 == count)
            for (int step = 0; step < 2; ++step) {
                if (wmav2_drain(dec, &planes, &frames) < 0 || frames == 0) break;
                append(planes, frames);
            }
    }
    wmav2_close(dec);
    return pcm;
}

}  // namespace

UNIT_TEST(xwma_tos16_every_shipped_sound) {
    // BANISHED_DIR (the one game-directory variable, as tools/build.sh) first, then the GOG default on
    // Windows and under WSL. getenv is allowed in the tests, never in the game.
    std::string envDir;
    if (const char* e = std::getenv("BANISHED_DIR"); e && *e) envDir = std::string(e) + "/WinData";
    const char* dirs[] = {envDir.c_str(), "C:/GOG Games/Banished/WinData", "/mnt/c/GOG Games/Banished/WinData"};
    const char* names[] = {"data0.pkg", "data1.pkg", "patch0.pkg", "patch1.pkg"};
    std::vector<std::vector<uint8_t>> pkgs;
    for (const char* dir : dirs) {
        if (!*dir) continue;
        for (const char* name : names) {
            std::vector<uint8_t> b = ReadFile(std::string(dir) + "/" + name);
            if (!b.empty()) pkgs.push_back(std::move(b));
        }
        if (!pkgs.empty()) break;
    }
    if (pkgs.empty()) UNIT_SKIP("no WinData/*.pkg found (no game install)");
    std::vector<Sound> sounds;
    for (const auto& p : pkgs) Scan(p, sounds);

    uint64_t hash = 0xcbf29ce484222325ull, samples = 0;
    size_t mismatched = 0, mismatchedChunked = 0, refused = 0, shortPasses = 0;
    double newMs = 0.0;
    for (const Sound& s : sounds) {
        Audio::XwmaDecoder dec;
        std::vector<int16_t> got;
        const auto t0 = std::chrono::steady_clock::now();
        if (dec.Open(s.format, s.packets, s.bytes, s.table.data(), uint32_t(s.table.size()))) {
            got.resize(size_t(dec.TotalSamples()) * dec.Channels());
            const size_t n = dec.DecodePackets(0, dec.PacketCount(), got.data(), size_t(dec.TotalSamples()));
            if (n < dec.TotalSamples()) ++shortPasses;
            got.resize(n * dec.Channels());
        } else {
            ++refused;
        }
        newMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        const std::vector<int16_t> ref = ReferenceDecode(s);
        if (got != ref) ++mismatched;
        // The Stream worker's shape over the same sound: one packet per call into a
        // buffer smaller than a packet's output, the rest carried (direct write,
        // then _pending, then the pending-first continuation).
        {
            Audio::XwmaDecoder chunked;
            std::vector<int16_t> carried, buf(1500 * 2);
            if (chunked.Open(s.format, s.packets, s.bytes, s.table.data(), uint32_t(s.table.size()))) {
                const size_t cap = buf.size() / chunked.Channels();
                for (uint32_t p = 0; p < chunked.PacketCount(); ++p) {
                    size_t w = chunked.DecodePackets(p, 1, buf.data(), cap);
                    carried.insert(carried.end(), buf.begin(), buf.begin() + std::ptrdiff_t(w * chunked.Channels()));
                    if (p % 2 == 1) continue;   // odd packets: decode on top of what is pending
                    while (chunked.PendingSamples() > 0) {
                        w = chunked.DecodePackets(chunked.NextPacket(), 0, buf.data(), cap);
                        carried.insert(carried.end(), buf.begin(),
                                       buf.begin() + std::ptrdiff_t(w * chunked.Channels()));
                    }
                }
                while (chunked.PendingSamples() > 0) {
                    const size_t w = chunked.DecodePackets(chunked.NextPacket(), 0, buf.data(), cap);
                    carried.insert(carried.end(), buf.begin(), buf.begin() + std::ptrdiff_t(w * chunked.Channels()));
                }
            }
            if (carried != ref) ++mismatchedChunked;
        }
        for (int16_t v : got) {
            hash = (hash ^ uint8_t(uint16_t(v) & 0xff)) * 0x100000001b3ull;
            hash = (hash ^ uint8_t(uint16_t(v) >> 8)) * 0x100000001b3ull;
        }
        samples += got.size();
    }
    std::printf("  xwma_tos16_every_shipped_sound: %zu sounds, %llu int16 values, refused=%zu short=%zu "
                "mismatched=%zu chunkedMismatched=%zu pcmHash=%016llx decodeMs=%.1f\n",
                sounds.size(), static_cast<unsigned long long>(samples), refused, shortPasses, mismatched,
                mismatchedChunked, static_cast<unsigned long long>(hash), newMs);
    // The packages store 23 sounds as plain runs (2026-10-14: 19,707,392 int16 values,
    // mono and stereo, 44.1 and 48 kHz); the rest sit inside compressed package blocks
    // this scan cannot read -- the game's own [SoundDecode] pcmHash covers those.
    CHECK(sounds.size() >= 20u);
    CHECK_EQ(refused, size_t(0));
    CHECK_EQ(mismatched, size_t(0));
    CHECK_EQ(mismatchedChunked, size_t(0));
}
