// ============================================================================
// tests/fuzz/text_codec3_fuzz.cpp -- a one-shot repro, not a fuzz loop: ConvertText's int32
//   `produced` counter over a source whose UTF-8 form passes 2 GB
// ============================================================================
// Any input runs the same case: 0x2B000000 raw 16-bit units of U+FFFF (1.34 GB, format 1), sized
// into UTF-8 (format 2, 3 bytes each): 0x81000000 bytes, past INT32_MAX. Run it with
// -rss_limit_mb=4096 -malloc_limit_mb=2048 on one input file.
// Build: `~/fz.sh build text_codec3 src/engine/io/text_codec.cpp`.
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

extern "C" int LLVMFuzzerTestOneInput(const uint8_t*, size_t) {
    const int32_t len = 0x56000000;
    uint8_t* src = static_cast<uint8_t*>(std::malloc(static_cast<size_t>(len)));
    if (src == nullptr)
        return 0;
    std::memset(src, 0xFF, static_cast<size_t>(len));
    const int32_t need = RtIo_TextCodec_ConvertText(1, 0, src, len, 2, 0, 0, nullptr, 0);
    std::free(src);
    return need < 0 ? 1 : 0;
}
