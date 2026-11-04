// ============================================================================
// tests/fuzz/compressed_stream_fuzz.cpp -- IO::CompressedStream over arbitrary package bytes
// ============================================================================
// P0_MAP input surface rank 1 (every package entry, every save block). The target builds the
// stream exactly as ReadPackfile does (package_file_read.cpp:477-482): a CompressedMemoryStream
// whose embedded MemoryStream holds the blob, then CompressedStream::Open(memoryStream, windows).
// It then drives a sequence of SetCurrentPos / Read / ReadArray / GetSize calls taken from the
// input, which reaches SetWindow, GetAvailableWindow, DecompressFromSource and the LZF decoder.
//
// Real bodies linked in addition to the unit archive (it carries host stubs for them):
//   src/engine/io/compressed_stream_open.cpp   Open / SetWindowCount
//   src/engine/io/memory_stream.cpp            MemoryStream::Read / SetCurrentPos / GetSize
//   src/engine/io/encoding.cpp                 BinaryEncoding (the stream constructors install one)
// The link uses --allow-multiple-definition so these objects win over io_stream_abi_stubs.cpp.
// compressed_stream_write.cpp (CompressedStream::SetCurrentPos / GetSize) drags in the
// FileSystem singleton and Release and does not link here; SetCurrentPos's whole retail body
// (0x1800aa830: `CALL SetWindow; MOV AL,1`) is therefore called as SetWindow(pos) directly.
//
// Input layout:
//   [0]       cfg: windows = 1 + (cfg & 3); bit 2 = keep going when Open returns false
//             (ReadPackfile discards Open's result, package_file_read.cpp:482)
//   [1..4]    u32 L, the blob length (clamped to what is left)
//   [5..5+L)  the blob, copied into a heap buffer of exactly L bytes (ASan sees over-reads)
//   [...]     ops, one byte each plus operands:
//             0: SetCurrentPos(i32)   1: Read(u16)   2: Read(u8)   3: ReadArray(u8 count, 1..8)
//             4: no-op (GetSize is not linkable here)
//
// MASKS (compile-time, so the crash sites can be recorded first and then stepped over to
// reach deeper code). Each names the finding it hides; they are off unless defined.
//   FZ_MASK_STACK   (finding: DecompressFromSource compressedSize > 0x8000 into the 0x8000
//                    stack scratch): reject blobs whose directory has compressedSize > 0x8000.
//   FZ_MASK_STORED  (finding: stored arm / window end with uncompressedSize > 0x8000):
//                    reject blobs whose directory has uncompressedSize > 0x8000.
//   FZ_MASK_SRCOFF  (finding: negative / huge sourceOffset into MemoryStream::Read):
//                    reject blobs whose sourceOffset is > 0x7fff0000.
//   FZ_MASK_NEGPOS  (finding: negative position indexes _headerData[negative]): skip seeks to
//                    a negative position.
//   FZ_MASK_SPIN    (finding: Read past a short block's end spins / copies a negative chunk):
//                    only issue a Read whose every byte lies inside a block's decoded extent.
//   FZ_MASK_BIGCOUNT (findings: Open's blockCount * 8 signed overflow; a blockCount <= 0
//                    makes Open read headerSize * 8 bytes into a null directory): reject
//                    blockCount > 0x0fffffff or <= 0 before Open.
//   F-002 (known, compression.cpp:335 pointer overflow) is masked in the BUILD: the script
//                    links its own copy of compression.cpp built -fno-sanitize=pointer-overflow.
// ============================================================================
#include "io/Stream.h"
#include "io/PackageFile.h"
#include "object/External.h"
#include "platform/common/platform_window.h"
#include "system/Compression.h"
#include "system/MemoryAllocator.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <vector>

// Link-only stubs: static initialisers in the archive's type-record objects (pulled in with
// io_stream_abi_stubs.cpp) and error.cpp name these; the target never calls them.
Object::Base* Object::ExternalList::CreateNewObject() { std::abort(); }
Object::Base* Object::ExternalList::CreateInPlaceObject(void*) { std::abort(); }
Object::Base* IO::PackageFile::CreateNewObject() { std::abort(); }
Object::Base* IO::PackageFile::CreateInPlaceObject(void*) { std::abort(); }
void Platform::Window::ShowErrorMessage(const uint16_t*, const uint16_t*) { std::abort(); }

namespace {

struct Cursor {
    const uint8_t* p;
    size_t n;
    bool Take(void* out, size_t k) {
        if (n < k)
            return false;
        std::memcpy(out, p, k);
        p += k;
        n -= k;
        return true;
    }
};

#if defined(FZ_MASK_STACK) || defined(FZ_MASK_STORED) || defined(FZ_MASK_SRCOFF) || \
    defined(FZ_MASK_BIGCOUNT)
bool DirectoryRejected(const uint8_t* blob, uint32_t len) {
    if (len < 4)
        return false;
    int32_t count = 0;
    std::memcpy(&count, blob, 4);
#ifdef FZ_MASK_BIGCOUNT
    // count > 0x0fffffff: Open's blockCount * 8 overflows. count <= 0: SetSize leaves the
    // directory null (and its size negative), and Open reads size * 8 bytes into it.
    if (count > 0x0fffffff || count <= 0)
        return true;
#endif
    if (count <= 0)
        return false;
    for (int64_t i = 0; i < count && 4 + (i + 1) * 8 <= len; ++i) {
        RtIo::CompressedStreamHeader h;
        std::memcpy(&h, blob + 4 + i * 8, 8);
#ifdef FZ_MASK_STACK
        // 0x8000 - 64, not 0x8000: F-001 (known, Decompress has no source-end bound) over-reads
        // up to ~33 bytes past compressedSize, which past 0x7fc0 leaves the 0x8000 stack scratch
        // and reads stale stack bytes (a non-reproducible stack-buffer-overflow READ).
        if (h.compressedSize > 0x8000 - 64)
            return true;
#endif
#ifdef FZ_MASK_STORED
        if (h.uncompressedSize > 0x8000)
            return true;
#endif
#ifdef FZ_MASK_SRCOFF
        if (h.sourceOffset > 0x7fff0000u)
            return true;
#endif
    }
    return false;
}
#endif

#ifdef FZ_MASK_SPIN
// True when every byte of [pos, pos + n) lies inside a block's decoded extent [b<<15,
// (b<<15) + uncompressedSize), or the first byte that does not lies in a block past the
// directory (SetWindow then fails cleanly before any copy).
bool ReadIsInside(const IO::CompressedStream& cs, int64_t pos, int64_t n) {
    while (n > 0) {
        if (pos < 0)
            return false;
        const int64_t b = pos >> 15;
        if (b >= cs._headerSize)
            return true;
        const int64_t start = b << 15;
        const int64_t end   = start + cs._headerData[b].uncompressedSize;
        if (pos >= end)
            return false;
        const int64_t chunk = (pos + n > end) ? end - pos : n;
        pos += chunk;
        n -= chunk;
    }
    return true;
}
#endif

void Teardown(IO::CompressedMemoryStream* cs) {
    for (int32_t i = 0; i < cs->_windowSize; ++i)
        ::System::MemoryAllocator::MemFree(cs->_windowData[i].buffer);
    if (cs->_windowData != nullptr)
        ::System::MemoryAllocator::MemFree(cs->_windowData);
    if (cs->_headerData != nullptr)
        ::System::MemoryAllocator::MemFree(cs->_headerData);
    cs->_windowData = nullptr;
    cs->_headerData = nullptr;
    cs->_windowSize = 0;
    cs->_headerSize = 0;
    cs->_source = nullptr;
    cs->_memoryStream._data = nullptr;
    cs->_memoryStream._ownsData = 0;
    cs->IO::CompressedMemoryStream::~CompressedMemoryStream();
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    Cursor in{data, size};
    uint8_t cfg = 0;
    uint32_t len = 0;
    if (!in.Take(&cfg, 1) || !in.Take(&len, 4))
        return 0;
    if (len > in.n)
        len = static_cast<uint32_t>(in.n);
    uint8_t* const blob = static_cast<uint8_t*>(std::malloc(len ? len : 1));
    std::memcpy(blob, in.p, len);
    in.p += len;
    in.n -= len;

#if defined(FZ_MASK_STACK) || defined(FZ_MASK_STORED) || defined(FZ_MASK_SRCOFF) || \
    defined(FZ_MASK_BIGCOUNT)
    if (DirectoryRejected(blob, len)) {
        std::free(blob);
        return 0;
    }
#endif

    alignas(IO::CompressedMemoryStream) unsigned char mem[sizeof(IO::CompressedMemoryStream)];
    IO::CompressedMemoryStream* const cs = ::new (static_cast<void*>(mem)) IO::CompressedMemoryStream();
    cs->_memoryStream._data      = blob;                     // package_file_read.cpp:478
    cs->_memoryStream._size      = len;
    cs->_memoryStream._cursor    = 0;
    cs->_memoryStream._direction = 0;
    const int windows = 1 + (cfg & 3);
    const bool ok = cs->IO::CompressedStream::Open(cs->_memoryStream, windows);

    if (ok || (cfg & 4)) {
        std::vector<uint8_t> dst;
        for (int op = 0; op < 64 && in.n > 0; ++op) {
            uint8_t kind = 0;
            in.Take(&kind, 1);
            switch (kind % 5) {
            case 0: {
                int32_t pos = 0;
                if (!in.Take(&pos, 4))
                    break;
#ifdef FZ_MASK_NEGPOS
                if (pos < 0)
                    break;
#endif
                cs->SetWindow(pos);   // == CompressedStream::SetCurrentPos 0x1800aa830, result dropped
                break;
            }
            case 1:
            case 2: {
                uint16_t n = 0;
                if (kind % 5 == 1) {
                    if (!in.Take(&n, 2))
                        break;
                } else {
                    uint8_t b = 0;
                    if (!in.Take(&b, 1))
                        break;
                    n = b;
                }
#ifdef FZ_MASK_SPIN
                if (!ReadIsInside(*cs, cs->_position, n))
                    break;
#endif
                dst.assign(n ? n : 1, 0);
                cs->Read(n, dst.data());
                break;
            }
            case 3: {
                uint8_t c = 0, e = 0;
                if (!in.Take(&c, 1) || !in.Take(&e, 1))
                    break;
                const int elem = 1 + (e & 7);
#ifdef FZ_MASK_SPIN
                if (!ReadIsInside(*cs, cs->_position, static_cast<int64_t>(c) * elem))
                    break;
#endif
                dst.assign(static_cast<size_t>(c) * elem + 1, 0);
                cs->ReadArray(c, elem, dst.data());
                break;
            }
            default:
                break;
            }
        }
    }

    Teardown(cs);
    std::free(blob);
    return 0;
}

#ifdef FUZZ_SEEDGEN
// ----------------------------------------------------------------------------
// Seed generator: packages assembled with OUR encoder (System::Compression::Compress), in the
// layout CompressedStream::Commit writes ([count][headers][blocks], absolute offsets).
// Build with -DFUZZ_SEEDGEN -fsanitize=fuzzer-no-link and run with the output directory.
// ----------------------------------------------------------------------------
#include <random>
#include <string>

namespace {

std::vector<uint8_t> Encode(const std::vector<uint8_t>& v) {
    std::vector<uint8_t> out(v.size() + v.size() / 16 + 64);
    System::Compression c;
    const int n = c.Compress(v.data(), static_cast<uint32_t>(v.size()), out.data(),
                             static_cast<uint32_t>(out.size()));
    RtSystem::RtMemFree(c._table);
    out.resize(n > 0 ? static_cast<size_t>(n) : 0);
    return out;
}

std::vector<uint8_t> Package(const std::vector<uint8_t>& plain, bool forceStored) {
    std::vector<RtIo::CompressedStreamHeader> hdr;
    std::vector<uint8_t> body;
    const size_t blocks = (plain.size() + 0x7fff) / 0x8000;
    const uint32_t base = static_cast<uint32_t>(4 + blocks * 8);
    for (size_t off = 0; off < plain.size(); off += 0x8000) {
        const size_t n = std::min<size_t>(0x8000, plain.size() - off);
        std::vector<uint8_t> blk(plain.begin() + off, plain.begin() + off + n);
        std::vector<uint8_t> enc = forceStored ? std::vector<uint8_t>() : Encode(blk);
        RtIo::CompressedStreamHeader h{};
        h.sourceOffset = base + static_cast<uint32_t>(body.size());
        h.uncompressedSize = static_cast<uint16_t>(n);
        if (!enc.empty() && enc.size() < n) {
            h.compressedSize = static_cast<uint16_t>(enc.size());
            body.insert(body.end(), enc.begin(), enc.end());
        } else {
            h.compressedSize = 0;
            body.insert(body.end(), blk.begin(), blk.end());
        }
        hdr.push_back(h);
    }
    std::vector<uint8_t> out(4);
    const int32_t count = static_cast<int32_t>(hdr.size());
    std::memcpy(out.data(), &count, 4);
    for (const auto& h : hdr) {
        const uint8_t* p = reinterpret_cast<const uint8_t*>(&h);
        out.insert(out.end(), p, p + 8);
    }
    out.insert(out.end(), body.begin(), body.end());
    return out;
}

void Emit(const std::string& dir, int idx, uint8_t cfg, const std::vector<uint8_t>& pkg,
          const std::vector<uint8_t>& ops) {
    std::vector<uint8_t> f;
    f.push_back(cfg);
    const uint32_t len = static_cast<uint32_t>(pkg.size());
    const uint8_t* lp = reinterpret_cast<const uint8_t*>(&len);
    f.insert(f.end(), lp, lp + 4);
    f.insert(f.end(), pkg.begin(), pkg.end());
    f.insert(f.end(), ops.begin(), ops.end());
    const std::string path = dir + "/seed_" + std::to_string(idx);
    FILE* fp = std::fopen(path.c_str(), "wb");
    std::fwrite(f.data(), 1, f.size(), fp);
    std::fclose(fp);
}

void Seek(std::vector<uint8_t>& ops, int32_t p) {
    ops.push_back(0);
    const uint8_t* b = reinterpret_cast<const uint8_t*>(&p);
    ops.insert(ops.end(), b, b + 4);
}
void Rd(std::vector<uint8_t>& ops, uint16_t n) {
    ops.push_back(1);
    const uint8_t* b = reinterpret_cast<const uint8_t*>(&n);
    ops.insert(ops.end(), b, b + 2);
}

}  // namespace

int main(int argc, char** argv) {
    const std::string dir = argc > 1 ? argv[1] : ".";
    std::mt19937 rng(1234);
    int idx = 0;
    const size_t sizes[] = {16, 300, 0x8000, 0x8000 + 777, 0x10000 + 5, 3 * 0x8000};
    for (size_t sz : sizes) {
        for (int kind = 0; kind < 3; ++kind) {
            std::vector<uint8_t> plain(sz);
            for (size_t i = 0; i < sz; ++i)
                plain[i] = kind == 0 ? static_cast<uint8_t>(rng())
                         : kind == 1 ? static_cast<uint8_t>(rng() & 3)
                                     : static_cast<uint8_t>("the quick brown fox "[i % 20]);
            const bool stored = (kind == 0);
            std::vector<uint8_t> pkg = Package(plain, stored);
            std::vector<uint8_t> ops;
            Rd(ops, 4);
            Rd(ops, static_cast<uint16_t>(std::min<size_t>(sz - 4, 2000)));
            if (sz > 0x8000) {
                Seek(ops, 0x7ff0);
                Rd(ops, 0x40);
                Seek(ops, 0);
                ops.push_back(3);
                ops.push_back(50);
                ops.push_back(3);
            }
            ops.push_back(4);
            Emit(dir, idx++, static_cast<uint8_t>(kind), pkg, ops);
        }
    }
    std::printf("wrote %d seeds to %s\n", idx, dir.c_str());
    return 0;
}
#endif
