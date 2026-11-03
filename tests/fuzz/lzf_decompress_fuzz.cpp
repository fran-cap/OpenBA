// ============================================================================
// tests/fuzz/lzf_decompress_fuzz.cpp -- System::Compression::Decompress over arbitrary bytes
// ============================================================================
// The block codec under every package entry and every save block (P0_MAP input surface rank 1):
// one CompressedStream block is at most a 32 KB window, so the destination is the window size.
// The input is the compressed block exactly as stored: libFuzzer hands it in a heap buffer of
// exactly `size` bytes, so ASan reports a read past the block's end.
// ============================================================================
#include "system/Compression.h"

#include <cstddef>
#include <cstdint>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size == 0 || size > 0x10000)
        return 0;
    std::vector<uint8_t> dst(0x8000);
    System::Compression::Decompress(data, static_cast<uint32_t>(size), dst.data(),
                                    static_cast<uint32_t>(dst.size()));
    return 0;
}
