// P3 proof: a CompressedStream block header whose u16 size exceeds the 0x8000 window.
// Mirrors PackageFile::ReadPackfile (package_file_read.cpp:478-501): blob -> embedded
// MemoryStream -> CompressedStream::Open(ms, 1) -> CompressedStream::Read(4, &fileCount).
// argv[1] = "compressed" (scratch stack arm) or "stored" (window heap arm).
#include "io/Stream.h"
#include "system/MemoryAllocator.h"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

// The unit tree links MemoryStream as ABI stubs (io_stream_abi_stubs.cpp), so the source is a
// Stream whose Read / SetCurrentPos / GetSize are MemoryStream's (memory_stream.cpp:64-124)
// transcribed: 32-bit end check, then memcpy.
struct BlobStream final : IO::Stream {
    const uint8_t* data = nullptr;
    uint32_t size = 0;
    uint32_t cursor = 0;
    bool Write(int, const void*) override { return false; }
    bool Read(int count, void* dst) override {
        const int64_t c = static_cast<int32_t>(cursor);
        const int32_t end = static_cast<int32_t>(c) + count;
        if (end > static_cast<int32_t>(size))
            return false;
        std::memcpy(dst, data + c, static_cast<size_t>(count));
        cursor += static_cast<uint32_t>(count);
        return true;
    }
    bool SetCurrentPos(int p) override {
        cursor = static_cast<uint32_t>(p);
        return true;
    }
    uint32_t GetCurrentPos() const override { return cursor; }
    uint32_t GetSize() const override { return size; }
};

int main(int argc, char** argv) {
    const bool stored = argc > 1 && std::strcmp(argv[1], "stored") == 0;
    if (argc > 1 && std::strcmp(argv[1], "count") == 0) {
        // Open's `blockCount * 8` (compressed_stream_open.cpp:117) on blockCount 0x10000001.
        static const uint8_t hdr[16] = {0x01, 0x00, 0x00, 0x10};
        BlobStream s;
        s.data = hdr;
        s.size = sizeof(hdr);
        IO::CompressedMemoryStream c;
        std::printf("Open -> %d\n", c.IO::CompressedStream::Open(s, 1) ? 1 : 0);
        return 0;
    }
    const uint16_t big = 0x9000;                      // > RtIo::kWin (0x8000)
    std::vector<uint8_t> file;
    auto put32 = [&](uint32_t v) { for (int i = 0; i < 4; ++i) file.push_back(uint8_t(v >> (8 * i))); };
    auto put16 = [&](uint16_t v) { file.push_back(uint8_t(v)); file.push_back(uint8_t(v >> 8)); };
    put32(1);                                         // blockCount
    put32(12);                                        // header[0].sourceOffset
    put16(stored ? 0 : big);                          // compressedSize (0 = stored)
    put16(big);                                       // uncompressedSize
    file.resize(12 + big, 0x00);                      // total == len, Open returns true
    void* blob = System::MemoryAllocator::MemAlloc(file.size(), 4);
    std::memcpy(blob, file.data(), file.size());

    IO::CompressedMemoryStream cms;
    BlobStream src;
    src.data = static_cast<const uint8_t*>(blob);
    src.size = static_cast<uint32_t>(file.size());
    const bool ok = cms.IO::CompressedStream::Open(src, 1);
    std::printf("Open -> %d\n", ok ? 1 : 0);
    int32_t fileCount = 0;
    cms.IO::CompressedStream::Read(4, &fileCount);
    std::printf("fileCount %d (no overflow reported)\n", fileCount);
    return 0;
}
