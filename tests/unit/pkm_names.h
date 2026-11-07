#pragma once
// ============================================================================
// tests/unit/pkm_names.h -- the entry names of an installed .pkm, read as
//   PackageFile::ReadPackfile reads its directory (package_file_read.cpp)
// ============================================================================
// Shared by path_index_test.cpp (PATHINDEX) and ext_package_cache_test.cpp
// (PKGCACHE). Reads only the header word and the directory blob.
// ============================================================================
#include "io/Stream.h"
#include "io/PackageFile.h"   // IO::PackageFile header word bits

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>
#include <string>
#include <vector>

namespace pkm_names {


inline int32_t ReadI32(const uint8_t* p) {
    int32_t v;
    std::memcpy(&v, p, 4);
    return v;
}

constexpr int32_t kWin = 0x8000;

struct BytesStream final : IO::Stream {
    const uint8_t* base = nullptr;
    int32_t size = 0;
    int32_t pos = 0;
    bool Write(int, const void*) override { return false; }
    bool Read(int n, void* dst) override {
        if (n < 0 || pos + n > size)
            return false;
        std::memcpy(dst, base + pos, static_cast<size_t>(n));
        pos += n;
        return true;
    }
    bool SetCurrentPos(int p) override {
        pos = p;
        return true;
    }
    uint32_t GetCurrentPos() const override { return static_cast<uint32_t>(pos); }
    uint32_t GetSize() const override { return static_cast<uint32_t>(size); }
};

// A CompressedMemoryStream over one compressed blob, as CompressedStream::Open
// leaves it (tests/unit/b39_io_classes_test.cpp drives the same class).
struct BlobReader {
    BytesStream src;
    std::vector<RtIo::CompressedStreamHeader> headers;
    RtIo::CompressedStreamWindow window{};
    std::vector<uint8_t> buffer;
    alignas(IO::CompressedMemoryStream) unsigned char mem[sizeof(IO::CompressedMemoryStream)];
    IO::CompressedMemoryStream* cs = nullptr;

    BlobReader(const uint8_t* base, int32_t size) {
        src.base = base;
        src.size = size;
        if (size < 4)
            return;
        const int32_t n = ReadI32(base);
        if (n < 0 || 4 + int64_t(n) * 8 > size)
            return;
        headers.resize(static_cast<size_t>(n));
        std::memcpy(headers.data(), base + 4, static_cast<size_t>(n) * 8);
        buffer.assign(kWin, 0);
        window.useCount = 0;
        window.start = -1;
        window.end = -1;
        window._pad0c = 0;
        window.buffer = buffer.data();
        cs = ::new (static_cast<void*>(mem)) IO::CompressedMemoryStream();
        cs->_direction = 0;
        cs->_position = 0;
        cs->_activeWindow = 0;
        cs->_source = &src;
        cs->_windowData = &window;
        cs->_windowSize = 1;
        cs->_windowCap = 1;
        cs->_headerData = headers.data();
        cs->_headerSize = n;
        cs->_headerCap = n;
    }
    ~BlobReader() {
        if (cs == nullptr)
            return;
        cs->_windowData = nullptr;
        cs->_headerData = nullptr;
        cs->_windowSize = 0;
        cs->_headerSize = 0;
        cs->_source = nullptr;
        cs->IO::CompressedMemoryStream::~CompressedMemoryStream();
    }
};

// The entry names of one package, in PackageFile::ReadPackfile's read order
// (package_file_read.cpp). Empty when the file is absent or unreadable.
inline std::vector<std::string> PackageNames(const std::string& path) {
    std::vector<std::string> names;
    FILE* f = std::fopen(path.c_str(), "rb");
    if (f == nullptr)
        return names;
    // Only the header word and the directory blob it points at (a package is
    // gigabytes; the directory sits at a 31-bit offset).
    uint8_t word[4] = {};
    const bool headOk = std::fread(word, 1, 4, f) == 4;
    const int32_t hdr = ReadI32(word);
    const bool compressed = (hdr & static_cast<int32_t>(IO::PackageFile::kHeaderCompressedBit)) != 0;
    const int32_t at = hdr & static_cast<int32_t>(IO::PackageFile::kHeaderOffsetMask);
    std::vector<uint8_t> blob;
    if (headOk && std::fseek(f, at, SEEK_SET) == 0 && std::fread(word, 1, 4, f) == 4) {
        const int32_t blobLen = ReadI32(word);
        if (blobLen > 0 && blobLen < (1 << 30)) {
            blob.resize(static_cast<size_t>(blobLen));
            if (std::fread(blob.data(), 1, blob.size(), f) != blob.size())
                blob.clear();
        }
    }
    std::fclose(f);
    if (blob.empty())
        return names;
    BlobReader dir(blob.data(), static_cast<int32_t>(blob.size()));
    if (dir.cs == nullptr)
        return names;
    IO::CompressedStream* const cs = dir.cs;
    auto rd = [&](int n, void* dst) { cs->IO::CompressedStream::Read(n, dst); };
    auto i32 = [&]() {
        int32_t v = 0;
        rd(4, &v);
        return v;
    };
    std::vector<uint8_t> tmp;
    auto skip = [&](int32_t n) {
        if (n <= 0)
            return;
        tmp.resize(static_cast<size_t>(n));
        rd(n, tmp.data());
    };
    const int32_t fileCount = i32();
    int32_t formatVersion = 0;
    if (compressed) {
        formatVersion = i32();
        for (int i = 0; i < 4; ++i)
            i32();   // version, userVersion, priority, platform
        skip(1);     // overridable
        i32();       // crc
        const int32_t w = i32();
        const int32_t h = i32();
        skip(w * h * 4);
        if (formatVersion >= 2)
            skip(i32());
        for (int s = 0; s < 3; ++s)
            skip(i32());   // name, author, description
    }
    for (int32_t i = 0; i < fileCount; ++i) {
        const int32_t nameLen = i32();
        std::string name;
        if (nameLen > 0) {
            tmp.resize(static_cast<size_t>(nameLen));
            rd(nameLen, tmp.data());
            if (formatVersion > 2) {
                name.assign(reinterpret_cast<const char*>(tmp.data()), static_cast<size_t>(nameLen));  // CAST-OK: byte_io
            } else {
                for (int32_t k = 0; k + 1 < nameLen; k += 2)
                    name.push_back(static_cast<char>(tmp[k]));
            }
        }
        i32();   // offset
        i32();   // size
        names.push_back(name);
    }
    return names;
}


// The seven MegaMod parts in mount order, or nothing when they are not installed.
inline std::vector<std::vector<std::string>> MegaModParts() {
    const std::string dir = "C:/GOG Games/Banished/MegaMod.X.Complete.Build.261018/";
    std::vector<std::vector<std::string>> parts;
    for (int i = 1; i <= 7; ++i) {
        std::vector<std::string> names = PackageNames(dir + "MegaMod" + std::to_string(i) + ".pkm");
        if (names.empty())
            return {};
        parts.push_back(std::move(names));
    }
    return parts;
}

}  // namespace pkm_names
