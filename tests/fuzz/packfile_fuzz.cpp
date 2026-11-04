// ============================================================================
// tests/fuzz/packfile_fuzz.cpp -- IO::PackageFile::ReadPackfile over arbitrary bytes
// ============================================================================
// P0_MAP input surface rank 2: the .pkg / .pkm index (every downloaded mod is one). The body
// (src/engine/io/package_file_read.cpp) is the real one, linked as an extra source; so is
// CompressedStream::Open (compressed_stream_open.cpp). The FileSystem singleton is the shim:
//   * the singleton points at zeroed storage of sizeof(FileSystem) -- ReadPackfile only touches
//     `_platformExistsGate` (+0x19) on it;
//   * OpenFile opens the fuzz input, written to a temp file under ~/fz/<name>/tmp, as a LOOSE
//     file (handle -2, the Platform handle in winHandle), which is what LoadPackages /
//     Modification::RefreshList get for a .pkg / .pkm on disk;
//   * ReadFile is a copy of the real body's LOOSE arm (filesystem_openfile.cpp:492-498):
//     Platform::FileRead with the count cast to uint32_t, the cursor advanced by what was read.
// So the bytes flow exactly as they do for a real package: header word, Platform::FileSeek32,
// blobLen, MemAlloc(blobLen), the blob read, CompressedMemoryStream over the blob, the index.
//
// Two modes:
//   packfile   (this file): the input IS the file.
//   packfile2  (packfile2_fuzz.cpp, PACKFILE_STRUCTURED): the input is byte 0 = flags, then the
//              DECOMPRESSED index plaintext; the harness wraps it in a valid container (STORED
//              32 KB blocks, a correct block directory) so the fuzzer spends its time on the
//              index parser rather than on CompressedStream::Open's `total == len` check.
//
// Leak checking is off for the run (ASAN_OPTIONS_EXTRA=detect_leaks=0): the unit-test archive's
// ~CompressedStream is a host stub (tests/unit/io_stream_abi_stubs.cpp:119) that frees no window
// buffer, so every execution leaks 32 KB by construction. The harness frees what it can reach.
// ============================================================================
#include "io/FileSystem.h"
#include "io/PackageFile.h"
#include "io/Stream.h"
#include "system/Filename.h"
#include "system/MemoryAllocator.h"
#include "platform_file.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <climits>
#include <string>
#include <vector>
#include <unistd.h>
#include <sys/stat.h>
#include <pwd.h>

namespace {
alignas(64) unsigned char g_fsStorage[sizeof(IO::FileSystem)];
std::u16string g_tmpPath;
}  // namespace

// ---- the shim: the singleton and the two members ReadPackfile calls ---------------------------
IO::FileSystem* IO::FileSystem::_singleton = reinterpret_cast<IO::FileSystem*>(g_fsStorage);

RtIO::FileInfo IO::FileSystem::OpenFile(const System::Filename&) {
    RtIO::FileInfo fi;
    Platform::FileHandle h = Platform::FileOpenRead(g_tmpPath.c_str());
    if (h == nullptr || h == Platform::kInvalidFileHandle)
        return fi;   // handle -1: ReadPackfile returns false
    fi.handle     = -2;
    fi.winHandle  = h;
    fi.size       = static_cast<int32_t>(Platform::FileSize(h));
    fi.cursor     = 0;
    fi.baseOffset = 0;
    return fi;
}

// The LOOSE arm of filesystem_openfile.cpp:488, verbatim in behaviour.
bool IO::FileSystem::ReadFile(RtIO::FileInfo& fi, int32_t n, void* dst) {
    uint32_t got = 0;
    Platform::FileRead(fi.winHandle, dst, static_cast<uint32_t>(n), &got);
    fi.cursor += static_cast<int32_t>(got);
    return static_cast<int32_t>(got) == n;
}

// ---- link stubs: symbols the extra sources pull in that the unit archive does not define ----
// None is reached by ReadPackfile's read path; each aborts loudly if it ever is.
#include "io/Serializer.h"
#include "object/External.h"
#include "platform_window.h"
IO::ReaderWriter* IO::BinaryEncoding::CreateReader(Serializer&, int) { std::abort(); }
IO::ReaderWriter* IO::BinaryEncoding::CreateWriter(Serializer&, int) { std::abort(); }
// The Commit arm at package_file_read.cpp:540 is guarded by `_direction == 1`; Open sets 0.
void IO::CompressedStream::Commit() { std::abort(); }
::Object::Base* IO::PackageFile::CreateNewObject() { std::abort(); }
::Object::Base* IO::PackageFile::CreateInPlaceObject(void*) { std::abort(); }
::Object::Base* Object::ExternalList::CreateNewObject() { std::abort(); }
::Object::Base* Object::ExternalList::CreateInPlaceObject(void*) { std::abort(); }
void Platform::Window::ShowErrorMessage(const uint16_t*, const uint16_t*) {}

namespace {

void InitTmpPath() {
    if (!g_tmpPath.empty())
        return;
    const passwd* pw = ::getpwuid(::getuid());   // the home directory, no getenv
    const char* home = (pw != nullptr) ? pw->pw_dir : nullptr;
    std::string dir = std::string(home ? home : "/tmp") + "/fz/packfile/tmp";
    ::mkdir((std::string(home ? home : "/tmp") + "/fz").c_str(), 0755);
    ::mkdir((std::string(home ? home : "/tmp") + "/fz/packfile").c_str(), 0755);
    ::mkdir(dir.c_str(), 0755);
    const std::string p = dir + "/in-" + std::to_string(::getpid()) + ".pkm";
    g_tmpPath.assign(p.begin(), p.end());
}

bool WriteTmp(const uint8_t* data, size_t size) {
    const std::string p(g_tmpPath.begin(), g_tmpPath.end());
    FILE* f = std::fopen(p.c_str(), "wb");
    if (f == nullptr)
        return false;
    if (size != 0)
        std::fwrite(data, 1, size, f);
    std::fclose(f);
    return true;
}

void Put32(std::vector<uint8_t>& v, uint32_t x) {
    for (int i = 0; i < 4; ++i)
        v.push_back(static_cast<uint8_t>(x >> (8 * i)));
}

// The container CompressedStream::Open accepts: blockCount, blockCount x {sourceOffset,
// compressedSize = 0 (STORED), uncompressedSize}, then the blocks.
std::vector<uint8_t> WrapStored(const uint8_t* plain, size_t n) {
    const size_t blocks = (n + 0x7fff) / 0x8000;
    std::vector<uint8_t> blob;
    Put32(blob, static_cast<uint32_t>(blocks));
    uint32_t at = static_cast<uint32_t>(4 + 8 * blocks);
    for (size_t b = 0; b < blocks; ++b) {
        const size_t len = (n - b * 0x8000) < 0x8000 ? (n - b * 0x8000) : 0x8000;
        Put32(blob, at);
        blob.push_back(0);
        blob.push_back(0);
        blob.push_back(static_cast<uint8_t>(len));
        blob.push_back(static_cast<uint8_t>(len >> 8));
        at += static_cast<uint32_t>(len);
    }
    blob.insert(blob.end(), plain, plain + n);
    return blob;
}

#if defined(PACKFILE_STRUCTURED)
// ---- masks: input shapes whose crash site is already recorded -------------------------------
// Walks the plaintext the way ReadPackfile does and rejects an input that would hit a recorded
// site, so the fuzzer reaches what lies behind it. Each mask names its finding (summary of the
// P4 packfile run):
//   PK-A  CompressedStream::Read spins forever reading past the end of a short final block
//         (compressed_stream_read.cpp:447, chunk = end - pos = 0): masked by zero-padding the
//         plaintext to a whole number of 32 KB blocks in WrapStored's caller.
//   PK-B  stack-buffer-overflow, Read(len) into buf60 / buf860 with len only checked > 0
//         (package_file_read.cpp ReadStringRaw / ReadStringUtf16 / the entry loop).
//   PK-C  stack over-read, the UTF-16 transcode walks 2*len from buf860 (sec. 76).
//   PK-D  signed overflow _iconHeight * _iconWidth; PK-D2 the read size _iconData._size * 4.
//   PK-E  unbounded allocation from the icon size / preview count (OOM).
//   PK-F  fileCount drives an O(n^2) SetSizeFill loop (timeout).
struct Walk {
    const uint8_t* p;
    size_t n;
    size_t at;
    bool ok;
    int32_t I32() {
        int32_t v = 0;
        if (at + 4 > n) {
            ok = false;   // past the data the padded blocks read zeros
            at = n;
            return 0;
        }
        std::memcpy(&v, p + at, 4);
        at += 4;
        return v;
    }
    void Skip(int64_t k) {
        if (k <= 0)
            return;
        if (at + static_cast<uint64_t>(k) > n) {
            ok = false;   // the real Read runs to the end of the data, then every read fails
            at = n;
        } else
            at += static_cast<size_t>(k);
    }
};
bool MaskedShape(const uint8_t* p, size_t n, bool meta) {
    Walk w{p, n, 0, true};
    const int32_t fileCount = w.I32();
    if (fileCount > 20000)
        return true;                                            // PK-F
    int32_t fv = 0;
    if (meta) {
        fv = w.I32();
        w.Skip(16);
        w.Skip(1);
        w.Skip(4);
        const int32_t iw = w.I32();
        const int32_t ih = w.I32();
        const int64_t px = static_cast<int64_t>(iw) * ih;
        if (px > INT32_MAX || px < INT32_MIN)
            return true;                                        // PK-D
        if (px * 4 > INT32_MAX || px * 4 < INT32_MIN)
            return true;                                        // PK-D2: _iconData._size * 4
        if (px * 4 > (64 << 20))
            return true;                                        // PK-E
        if (px > 0)
            w.Skip(px * 4);
        if (fv >= 2) {
            const int32_t pv = w.I32();
            if (pv > (64 << 20))
                return true;                                    // PK-E
            w.Skip(pv);
        }
        for (int s = 0; s < 3 && w.ok; ++s) {
            const int32_t len = w.I32();
            if (fv <= 2 && (len > 0x1040 || len > 0x820))
                return true;                                    // PK-B (buf860) / PK-C
            if (fv > 2 && len > 0x1840)
                return true;                                    // PK-B (buf60)
            w.Skip(len);
        }
    }
    for (int32_t i = 0; i < fileCount && w.ok; ++i) {
        const int32_t len = w.I32();
        if (fv > 2 && len > 0x1840)
            return true;                                        // PK-B (buf60)
        if (fv <= 2 && len > 0x820)
            return true;                                        // PK-B (buf860) / PK-C
        w.Skip(len);
        w.Skip(8);
    }
    return false;
}
#endif

void FreeArrays(IO::PackageFile* pf) {
    // ~PackageFile is a host stub in the unit archive; release the two Arrays ReadPackfile grew.
    if (pf->_iconData._data != nullptr)
        System::MemoryAllocator::MemFree(pf->_iconData._data);
    if (pf->_previewData._data != nullptr)
        System::MemoryAllocator::MemFree(pf->_previewData._data);
    pf->_iconData._data    = nullptr;
    pf->_previewData._data = nullptr;
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    InitTmpPath();
#if defined(PACKFILE_STRUCTURED)
    // byte 0: bit 0 = the header's metadata bit (bit 31 of the header word).
    if (size < 1 || size > 0x40000)
        return 0;
    const bool meta = (data[0] & 1) != 0;
    // PK-A mask: pad to whole 32 KB blocks, so a read past the data ends at a block boundary
    // (DecompressFromSource returns false) instead of spinning on a short final block.
    std::vector<uint8_t> plain(data + 1, data + size);
    plain.resize(((plain.size() + 0x7fff) / 0x8000) * 0x8000 + (plain.empty() ? 0x8000 : 0), 0);
    if (MaskedShape(plain.data(), plain.size(), meta))   // walk what ReadPackfile will read
        return 0;
    std::vector<uint8_t> blob = WrapStored(plain.data(), plain.size());
    std::vector<uint8_t> file;
    Put32(file, (meta ? 0x80000000u : 0u) | 4u);   // the index starts right after the header word
    Put32(file, static_cast<uint32_t>(blob.size()));
    file.insert(file.end(), blob.begin(), blob.end());
    if (!WriteTmp(file.data(), file.size()))
        return 0;
#else
    if (size > 0x40000)
        return 0;
    if (!WriteTmp(data, size))
        return 0;
#endif
    // PackageFile's operator delete is the MemFree one (package_file.cpp), so build it the way
    // LoadPackages does: engine heap storage, placement new, explicit dtor, MemFree.
    void* const mem = System::MemoryAllocator::MemAlloc(sizeof(IO::PackageFile), 16);
    IO::PackageFile* pf = ::new (mem) IO::PackageFile();
    System::Filename name;
    pf->ReadPackfile(name);
    if (pf->_file.winHandle != nullptr && pf->_file.winHandle != Platform::kInvalidFileHandle) {
        Platform::FileClose(pf->_file.winHandle);
        pf->_file.winHandle = Platform::kInvalidFileHandle;
    }
    FreeArrays(pf);
    pf->~PackageFile();
    System::MemoryAllocator::MemFree(mem);
    return 0;
}
