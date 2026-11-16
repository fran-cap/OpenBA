// ============================================================================
// tools/audio_tail/audio_tail.cpp -- offline: the ends of the shipped one-shots
// ============================================================================
// Decodes a SoundBuffer's PlatformSerialize run (int32 _length, int32 _wmaLength,
// the seek table, the 0x28-byte format window, the xWMA packets; the layout
// tests/unit/data/README.md documents and tools/audio_tail/pkg_sounds.py cuts out
// of the game's packages) with the same Audio::XwmaDecoder the game uses at load,
// and prints what the END of the PCM looks like: the last frames, the peak of the
// final millisecond, and the largest sample-to-sample step at the end against the
// largest step anywhere in the sound. A non-zero final sample or a tail step that
// stands out from the body is a click retail's XAudio2 plays too (the PCM is the
// decoder's, measured within 1 LSB of XAudio2: handins/XWMA.md).
//
// Build (from the repo root, after `tools/build.sh build` built the
// decoder library):
//   g++ -std=c++17 -O1 -I. -Isrc/engine -Isrc/platform/common -Ithird_party/wmadec/include \
//       tools/audio_tail/audio_tail.cpp src/engine/audio/xwma_decode.cpp \
//       build/cmake-debug/third_party/wmadec/libwmadec.a -o build/audio_tail.exe
// Run:  build/audio_tail.exe <run.bin>...        (one line per file)
//       build/audio_tail.exe --pcm out.raw <run.bin>   (also writes the S16 PCM)
// ============================================================================
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "audio/xwma_decode.h"

namespace {

std::vector<uint8_t> ReadFile(const char* path) {
    std::vector<uint8_t> v;
    FILE* f = std::fopen(path, "rb");
    if (!f) return v;
    std::fseek(f, 0, SEEK_END);
    const long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    v.resize(n > 0 ? static_cast<size_t>(n) : 0);
    if (!v.empty() && std::fread(v.data(), 1, v.size(), f) != v.size()) v.clear();
    std::fclose(f);
    return v;
}

int32_t I32(const uint8_t* p) {
    int32_t v;
    std::memcpy(&v, p, 4);
    return v;
}

const char* BaseName(const char* path) {
    const char* b = path;
    for (const char* p = path; *p; ++p)
        if (*p == '/' || *p == '\\') b = p + 1;
    return b;
}

}  // namespace

int main(int argc, char** argv) {
    const char* pcmOut = nullptr;
    int first = 1;
    if (argc > 2 && std::strcmp(argv[1], "--pcm") == 0) {
        pcmOut = argv[2];
        first = 3;
    }
    if (first >= argc) {
        std::fprintf(stderr, "usage: audio_tail [--pcm out.raw] run.bin...\n");
        return 2;
    }
    std::printf("%-40s %2s %5s %7s %7s %4s | %-30s | %6s %6s %6s %6s\n", "sound", "ch", "rate",
                "table", "frames", "fail", "last 5 frames (ch 0)", "tailPk", "tailSt", "bodySt",
                "seam");
    for (int a = first; a < argc; ++a) {
        const std::vector<uint8_t> run = ReadFile(argv[a]);
        if (run.size() < 8 + 0x28) {
            std::printf("%-40s unreadable\n", BaseName(argv[a]));
            continue;
        }
        const int32_t length = I32(run.data());
        const int32_t wmaLength = I32(run.data() + 4);
        const uint32_t packetCount = static_cast<uint32_t>(wmaLength) / 4u;
        const uint8_t* table = run.data() + 8;
        const uint8_t* format = table + wmaLength;
        const uint8_t* packets = format + 0x28;
        if (packets + length > run.data() + run.size()) {
            std::printf("%-40s short run (%d packet bytes, %zu present)\n", BaseName(argv[a]),
                        length, run.size() - (packets - run.data()));
            continue;
        }
        std::vector<uint32_t> seek(packetCount);
        std::memcpy(seek.data(), table, static_cast<size_t>(wmaLength));

        Audio::XwmaDecoder dec;
        if (!dec.Open(format, packets, static_cast<size_t>(length), seek.data(), packetCount)) {
            std::printf("%-40s decoder refused the format\n", BaseName(argv[a]));
            continue;
        }
        const uint32_t ch = dec.Channels();
        const size_t total = static_cast<size_t>(dec.TotalSamples());
        std::vector<int16_t> pcm(total * ch);
        const size_t frames = dec.DecodePackets(0, packetCount, pcm.data(), total);
        pcm.resize(frames * ch);

        // Steps: the largest |x[n] - x[n-1]| over the last 64 frames and over the body.
        auto stepMax = [&](size_t from, size_t to) {
            int best = 0;
            for (size_t n = from + 1; n < to; ++n)
                for (uint32_t c = 0; c < ch; ++c) {
                    const int d = std::abs(int(pcm[n * ch + c]) - int(pcm[(n - 1) * ch + c]));
                    if (d > best) best = d;
                }
            return best;
        };
        const size_t tailFrom = frames > 64 ? frames - 64 : 0;
        int tailPeak = 0;
        for (size_t n = tailFrom; n < frames; ++n)
            for (uint32_t c = 0; c < ch; ++c)
                tailPeak = std::max(tailPeak, std::abs(int(pcm[n * ch + c])));
        const int tailStep = stepMax(tailFrom, frames);
        const int bodyStep = stepMax(0, tailFrom);
        // The loop seam: last frame to first frame (a looping buffer replays from 0).
        int seam = 0;
        if (frames > 0)
            for (uint32_t c = 0; c < ch; ++c)
                seam = std::max(seam, std::abs(int(pcm[(frames - 1) * ch + c]) - int(pcm[c])));

        std::string last;
        for (size_t n = frames >= 5 ? frames - 5 : 0; n < frames; ++n) {
            char buf[16];
            std::snprintf(buf, sizeof buf, "%d ", pcm[n * ch]);
            last += buf;
        }
        std::printf("%-40s %2u %5u %7zu %7zu %4s | %-30s | %6d %6d %6d %6d\n", BaseName(argv[a]), ch,
                    dec.SampleRate(), total, frames, dec.Failed() ? "FAIL" : "ok", last.c_str(),
                    tailPeak, tailStep, bodyStep, seam);

        if (pcmOut) {
            FILE* f = std::fopen(pcmOut, "wb");
            if (f) {
                std::fwrite(pcm.data(), 2, pcm.size(), f);
                std::fclose(f);
            }
        }
    }
    return 0;
}
