// ============================================================================
// tests/fuzz/text_codec2_fuzz.cpp -- IO::TextCodec, UNMASKED and at the per-character API
// ============================================================================
// The companion of text_codec_fuzz.cpp with no masks: every exported codec entry point
// (src/engine/io/text_codec.cpp) on exactly sized heap blocks, so the first crash it finds is
// a real site. Build: `~/fz.sh build text_codec2 src/engine/io/text_codec.cpp`.
//
// Input layout:
//   byte 0      mode (b % 3)
//   mode 0      ConvertText(srcFmt = byte1 % 8, dst = byte2 % 8, flags byte3) over the rest
//   mode 1      DecodeOne(fmtEcx = byte1 % 8, fmtArg5 = byte2 % 8) once over the rest
//   mode 2      EncodeOne(cp = int32 LE bytes 1..4, fmt = byte5 % 8) into a 4-byte heap block
// ============================================================================
#include "io/Serializer.h"
#include "io/Stream.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>

// Link stub: error.cpp (pulled in by the allocator) references the platform message box.
namespace Platform::Window {
void ShowErrorMessage(const uint16_t*, const uint16_t*) {}
}  // namespace Platform::Window

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size < 4 || size > 0x10000)
        return 0;
    const int mode = data[0] % 3;
    if (mode == 0) {
        const int32_t len = static_cast<int32_t>(size - 4);
        uint8_t* text = static_cast<uint8_t*>(std::malloc(static_cast<size_t>(len)));
        std::memcpy(text, data + 4, static_cast<size_t>(len));
        const int s = data[1] % 8;
        const int d = data[2] % 8;
        const int32_t need =
            RtIo_TextCodec_ConvertText(s, data[3] & 1, text, len, d, 0, (data[3] >> 1) & 1, nullptr, 0);
        uint8_t* dst = static_cast<uint8_t*>(std::malloc(static_cast<size_t>(need)));
        RtIo_TextCodec_ConvertText(s, data[3] & 1, text, len, d, 0, (data[3] >> 1) & 1, dst, need);
        std::free(dst);
        std::free(text);
    } else if (mode == 1) {
        const size_t len = size - 3;
        uint8_t* text = static_cast<uint8_t*>(std::malloc(len));
        std::memcpy(text, data + 3, len);
        int32_t cp = 0;
        RtIo_TextCodec_DecodeOne(data[1] % 8, text, text + len, &cp, data[2] % 8);
        std::free(text);
    } else {
        if (size < 6)
            return 0;
        int32_t cp;
        std::memcpy(&cp, data + 1, 4);
        uint8_t* dst = static_cast<uint8_t*>(std::malloc(4));
        RtIo_TextCodec_EncodeOne(cp, dst, data[5] % 8);
        std::free(dst);
    }
    return 0;
}
