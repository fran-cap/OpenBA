// ============================================================================
// tests/fuzz/text_codec_fuzz.cpp -- IO::TextCodec (src/engine/io/text_codec.cpp) over arbitrary text
// ============================================================================
// P0_MAP input surface rank 6. Build: `~/fz.sh build text_codec src/engine/io/text_codec.cpp`
// (text_codec.cpp is not in the unit tree archive).
//
// Input layout:
//   byte 0   source format id  (b % 8: 0..5 the codecs, 6 = "detect", 7 = an unknown id)
//   byte 1   destination format id (b % 8)
//   byte 2   bit 0 forceDetect, bit 1 appendTerminator
//   rest     the text, copied into a heap block of EXACTLY its size so ASan sees a read past it
//
// Checks:
//   1. DetectFormat over the text.
//   2. ConvertText's documented two-pass use: a sizing call (dst == nullptr) then a call into a
//      heap block of exactly that many bytes; both must return the same count.
//   3. Differential a -> b -> a: y = a->b(x), z = b->a(y), y2 = a->b(z); y2 must equal y.
//      Every leg pads its source with four zero bytes so the "one byte stricter" end rules
//      (text_codec.cpp banner: a final multi-byte character is rejected) do not drop the last
//      character, and trailing encoded NULs are stripped from each result.
//
// MASKS (findings already recorded, so the fuzzer can go deeper):
//   M1  Format 1 (raw 16-bit) with an ODD source length: DecodeOne's case 1 reads src[1] one byte
//       past the end (heap-buffer-overflow READ, recorded as finding TC-1). The target rounds an
//       odd format-1 source down to an even length unless FUZZ_NO_MASK_M1 is defined.
// ============================================================================
#include "io/Serializer.h"
#include "io/Stream.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

// Link stub: error.cpp (pulled in by the allocator) references the platform message box.
namespace Platform::Window {
void ShowErrorMessage(const uint16_t*, const uint16_t*) {}
}  // namespace Platform::Window

namespace {

int TermWidth(int fmt) {
    return (fmt == 1 || fmt == 4 || fmt == 5) ? 2 : 1;
}

// One ConvertText leg in the two-pass shape, into an exactly sized heap block.
std::vector<uint8_t> Leg(int srcFmt, uint8_t force, const uint8_t* src, int32_t len, int dstFmt,
                         uint8_t term) {
    const int32_t need = RtIo_TextCodec_ConvertText(srcFmt, force, src, len, dstFmt, 0, term,
                                                    nullptr, 0);
    if (need < 0)
        __builtin_trap();
    uint8_t* dst = static_cast<uint8_t*>(std::malloc(static_cast<size_t>(need)));
    const int32_t got = RtIo_TextCodec_ConvertText(srcFmt, force, src, len, dstFmt, 0, term, dst,
                                                   need);
    if (got != need)
        __builtin_trap();
    // The terminator check adds termLen twice (transcribed retail quirk), so an exactly sized
    // block never receives the terminator: only need - termLen bytes are written then.
    int32_t written = need;
    if (term != 0 && dstFmt >= 0 && dstFmt <= 5)
        written -= TermWidth(dstFmt);
    std::vector<uint8_t> out(dst, dst + (written > 0 ? written : 0));
    std::free(dst);
    return out;
}

void StripTrailingNul(std::vector<uint8_t>& v, int fmt) {
    const size_t w = static_cast<size_t>(TermWidth(fmt));
    while (v.size() >= w) {
        bool zero = true;
        for (size_t i = v.size() - w; i < v.size(); ++i)
            zero = zero && v[i] == 0;
        if (!zero)
            break;
        v.resize(v.size() - w);
    }
}

std::vector<uint8_t> PaddedLeg(int a, const std::vector<uint8_t>& in, int b) {
    std::vector<uint8_t> src(in);
    src.insert(src.end(), 4, 0);
    uint8_t* heap = static_cast<uint8_t*>(std::malloc(src.size()));
    std::memcpy(heap, src.data(), src.size());
    std::vector<uint8_t> out = Leg(a, 0, heap, static_cast<int32_t>(src.size()), b, 0);
    std::free(heap);
    StripTrailingNul(out, b);
    return out;
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size < 3 || size > 0x10000)
        return 0;
    const int srcFmt = data[0] % 8;
    const int dstFmt = data[1] % 8;
    const uint8_t force = data[2] & 1;
    const uint8_t term = (data[2] >> 1) & 1;
    int32_t len = static_cast<int32_t>(size - 3);
#ifndef FUZZ_NO_MASK_M1
    // M1: see the banner.
    if (srcFmt == 1 && force == 0 && (len & 1))
        len -= 1;
#endif

    uint8_t* text = static_cast<uint8_t*>(std::malloc(static_cast<size_t>(len)));
    std::memcpy(text, data + 3, static_cast<size_t>(len));

    const int32_t detected = RtIo_TextCodec_DetectFormat(text, len);
    if (detected != 0 && detected != 2 && detected != 3 && detected != 4 && detected != 5)
        __builtin_trap();

    // 2. the documented two-pass use, exactly as given.
    (void)Leg(srcFmt, force, text, len, dstFmt, term);

    // 3. the differential round trip, over the real codecs only.
    const int a = (srcFmt <= 5) ? srcFmt : detected;
    const int b = dstFmt % 6;
    bool skipRound = false;
#ifndef FUZZ_NO_MASK_M1
    // M1 again: an odd-length raw-16 source would only re-find TC-1.
    skipRound = (a == 1 && (len & 1));
#endif
    if (!skipRound) {
        std::vector<uint8_t> x(text, text + len);
        // a BOM is a source-side marker; feed the bytes after it
        const int bom = (srcFmt <= 5) ? 0 : (a == 3 ? 3 : (a == 4 || a == 5) ? 2 : 0);
        x.erase(x.begin(), x.begin() + (bom <= len ? bom : len));
        const std::vector<uint8_t> y = PaddedLeg(a, x, b);
        const std::vector<uint8_t> z = PaddedLeg(b, y, a);
        const std::vector<uint8_t> y2 = PaddedLeg(a, z, b);
        if (y2 != y)
            __builtin_trap();
    }

    std::free(text);
    return 0;
}
