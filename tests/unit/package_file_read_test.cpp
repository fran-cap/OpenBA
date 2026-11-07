// ============================================================================
// tests/unit/package_file_read_test.cpp -- IO::PackageFile::ReadPackfile over
// crafted .pkg / .pkm images (review 2026-11-03, findings F-017..F-021, F-033,
// F-054..F-056, F-076, F-077)
// ============================================================================
// The REAL ReadPackfile (src/engine/io/package_file_read.cpp) with the real
// CompressedStream::Open / Read and MemoryStream underneath. Only FileSystem's
// OpenFile / ReadFile / CloseFile are host fakes over one in-memory file: the
// real ones (filesystem_openfile.cpp, filesystem_createfile.cpp) stand on the
// package index and the OS and do not link here. OpenFile answers a PACKED
// handle (0), so ReadPackfile's loose-file seek is not taken.
//
// Every image carries its index as ONE stored 32 KB block, zero-padded: a read
// past the meaningful bytes reads zeros, and a read past the block fails at
// once (a short last block would park a read past its end in CompressedStream's
// own zero-chunk loop, which is not what these tests are about).
//
// Two groups:
//  * valid images (UTF-16 metadata, raw UTF-8 metadata, the longest name the
//    raw arm can hold): read exactly as before the hardening;
//  * malformed images: each one names the finding it reproduces. An over-long
//    string, a negative blob length, an icon or preview larger than the index,
//    and a file count the index cannot hold are REJECTED: ReadPackfile returns
//    false, closes the file and keeps no entry (the state of a package whose
//    file never opened, which both callers already handle).
//
// The indeterminate-stack cases (F-017, F-018, F-054) fill the stack with 0x42
// first, so a read of bytes ReadPackfile never wrote shows up as 'B's in the
// name instead of depending on what an earlier test left there.
// ============================================================================
#include "unit.h"

#include "io/FileSystem.h"
#include "io/PackageFile.h"
#include "system/String.h"
#include "system/Filename.h"
#include "system/MemoryAllocator.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

// ---- the in-memory FileSystem ---------------------------------------------
namespace {
std::vector<uint8_t> g_file;
int g_closeCalls = 0;
alignas(16) unsigned char g_fsMem[sizeof(IO::FileSystem)];
}  // namespace

IO::FileSystem* IO::FileSystem::_singleton = reinterpret_cast<IO::FileSystem*>(g_fsMem);

RtIO::FileInfo IO::FileSystem::OpenFile(const System::Filename&) {
    RtIO::FileInfo fi;
    fi.handle = 0;
    fi.size = static_cast<int32_t>(g_file.size());
    fi.cursor = 0;
    return fi;
}

bool IO::FileSystem::ReadFile(RtIO::FileInfo& fi, int32_t n, void* dst) {
    if (n < 0 || fi.cursor < 0)
        return false;
    if (static_cast<size_t>(fi.cursor) + static_cast<size_t>(n) > g_file.size())
        return false;
    std::memcpy(dst, g_file.data() + fi.cursor, static_cast<size_t>(n));
    fi.cursor += n;
    return true;
}

void IO::FileSystem::CloseFile(RtIO::FileInfo& info) {
    ++g_closeCalls;
    info = RtIO::FileInfo();
}

namespace {

constexpr size_t kBlock = 0x8000;

void Put32(std::vector<uint8_t>& v, uint32_t x) {
    for (int i = 0; i < 4; ++i)
        v.push_back(static_cast<uint8_t>(x >> (8 * i)));
}

void PutBytes(std::vector<uint8_t>& v, const char* s, size_t n) {
    v.insert(v.end(), reinterpret_cast<const uint8_t*>(s), reinterpret_cast<const uint8_t*>(s) + n);
}

// A length-prefixed raw string (formatVersion > 2), NUL included.
void PutRaw(std::vector<uint8_t>& v, const char* s) {
    const size_t n = std::strlen(s) + 1;
    Put32(v, static_cast<uint32_t>(n));
    PutBytes(v, s, n);
}

// A length-prefixed UTF-16LE string (formatVersion <= 2), NUL unit included;
// the length is in BYTES.
void PutUtf16(std::vector<uint8_t>& v, const char* s) {
    const size_t n = std::strlen(s) + 1;
    Put32(v, static_cast<uint32_t>(2 * n));
    for (size_t i = 0; i < n; ++i) {
        v.push_back(static_cast<uint8_t>(s[i]));
        v.push_back(0);
    }
}

// The metadata block up to and including the preview (formatVersion >= 2).
void PutMeta(std::vector<uint8_t>& p, int32_t formatVersion, uint32_t iconW, uint32_t iconH,
             uint32_t previewBytes) {
    Put32(p, static_cast<uint32_t>(formatVersion));
    Put32(p, 7);            // version
    Put32(p, 3);            // userVersion
    Put32(p, 0x2710);       // priority
    Put32(p, 0);            // platform
    p.push_back(1);         // overridable
    Put32(p, 0x12345678);   // crc
    Put32(p, iconW);
    Put32(p, iconH);
    // Pixels only for a small, valid icon; a malformed size is rejected first.
    const uint64_t pixels = static_cast<uint64_t>(iconW) * iconH;
    for (uint64_t i = 0; pixels <= 16 && i < pixels; ++i)
        Put32(p, 0xff00ff00u);
    if (formatVersion >= 2) {
        Put32(p, previewBytes);
        for (uint32_t i = 0; previewBytes <= 16 && i < previewBytes; ++i)
            p.push_back(static_cast<uint8_t>(i));
    }
}

// The file: header word (bit 31 = metadata, low bits = the index offset 4),
// blob length, then the blob = one stored block of exactly 32 KB.
void SetImage(std::vector<uint8_t> payload, bool metadata) {
    payload.resize(kBlock, 0);
    std::vector<uint8_t> blob;
    Put32(blob, 1);                 // blockCount
    Put32(blob, 12);                // sourceOffset
    blob.push_back(0);              // compressedSize 0 = stored
    blob.push_back(0);
    blob.push_back(0x00);           // uncompressedSize 0x8000
    blob.push_back(0x80);
    blob.insert(blob.end(), payload.begin(), payload.end());
    g_file.clear();
    Put32(g_file, metadata ? 0x80000004u : 0x00000004u);
    Put32(g_file, static_cast<uint32_t>(blob.size()));
    g_file.insert(g_file.end(), blob.begin(), blob.end());
}

// Fill the stack below this frame with 0x42 so bytes ReadPackfile never wrote
// read as 'B' (the ReadPackfile frame is laid over this one).
__attribute__((noinline)) void PoisonStack() {
    volatile uint8_t junk[0x10000];
    for (size_t i = 0; i < sizeof(junk); ++i)
        junk[i] = 0x42;
}

// The host has no seated MemoryAllocator: allocate through the instrument seat,
// armed with the CRT heap for each run (ext_package_cache_test.cpp does the same).
void* SeatAlloc(uint64_t size, int) {
    return std::malloc(static_cast<size_t>(size));
}
void SeatFree(void* p) {
    std::free(p);
}
struct HeapSeat {
    HeapSeat() {
        System::g_memoryAllocatorInstrument.memAlloc = &SeatAlloc;
        System::g_memoryAllocatorInstrument.memFree = &SeatFree;
    }
    ~HeapSeat() {
        System::g_memoryAllocatorInstrument.memAlloc = nullptr;
        System::g_memoryAllocatorInstrument.memFree = nullptr;
    }
};

struct Result {
    bool ok = false;
    std::string name;
    std::string author;
    std::string description;
    std::vector<std::string> entryNames;
    std::vector<uint32_t> entryOffsets;
    std::vector<uint32_t> entrySizes;
    int32_t iconW = 0;
    int32_t iconH = 0;
    int32_t iconPixels = 0;
    int32_t previewBytes = 0;
    int32_t handle = 0;
};

std::string Text(const System::String& s) {
    return s._data != nullptr ? std::string(s._data) : std::string();
}

__attribute__((noinline)) Result Run() {
    g_closeCalls = 0;
    HeapSeat seat;
    IO::PackageFile pkg;
    System::Filename fn;
    PoisonStack();
    Result r;
    r.ok = pkg.IO::PackageFile::ReadPackfile(fn);
    r.name = Text(pkg._name);
    r.author = Text(pkg._author);
    r.description = Text(pkg._description);
    for (int32_t i = 0; i < pkg._entries._size; ++i) {
        r.entryNames.push_back(Text(pkg._entries._data[i].name.AsString()));
        r.entryOffsets.push_back(pkg._entries._data[i].offset);
        r.entrySizes.push_back(pkg._entries._data[i].size);
    }
    r.iconW = pkg._iconWidth;
    r.iconH = pkg._iconHeight;
    r.iconPixels = pkg._iconData._size;
    r.previewBytes = pkg._previewData._size;
    r.handle = pkg._file.handle;
    return r;
}

// A rejected package: false, the file closed, no entry kept.
void CheckRejected(const Result& r) {
    CHECK(!r.ok);
    CHECK_EQ(r.handle, -1);
    CHECK_EQ(g_closeCalls, 1);
    CHECK(r.entryNames.empty());
}

}  // namespace

// ---- valid images: unchanged -----------------------------------------------

UNIT_TEST(packfile_read_valid_raw_metadata) {
    std::vector<uint8_t> p;
    Put32(p, 2);                              // fileCount
    PutMeta(p, 3, 2, 2, 4);
    PutRaw(p, "Mod name");
    PutRaw(p, "An author");
    PutRaw(p, "");
    PutRaw(p, "first.txt");
    Put32(p, 0x10);
    Put32(p, 0x20);
    PutRaw(p, "second.rsc");
    Put32(p, 0x30);
    Put32(p, 0x40);
    SetImage(p, true);
    const Result r = Run();
    CHECK(r.ok);
    CHECK_EQ(r.handle, 0);
    CHECK_EQ(g_closeCalls, 0);
    CHECK_STREQ(r.name.c_str(), "Mod name");
    CHECK_STREQ(r.author.c_str(), "An author");
    CHECK_STREQ(r.description.c_str(), "");
    CHECK_EQ(r.iconW, 2);
    CHECK_EQ(r.iconH, 2);
    CHECK_EQ(r.iconPixels, 4);
    CHECK_EQ(r.previewBytes, 4);
    CHECK_EQ(r.entryNames.size(), static_cast<size_t>(2));
    if (r.entryNames.size() == 2) {
        CHECK_STREQ(r.entryNames[0].c_str(), "first.txt");
        CHECK_STREQ(r.entryNames[1].c_str(), "second.rsc");
        CHECK_EQ(r.entryOffsets[0], 0x10u);
        CHECK_EQ(r.entrySizes[0], 0x20u);
        CHECK_EQ(r.entryOffsets[1], 0x30u);
        CHECK_EQ(r.entrySizes[1], 0x40u);
    }
}

UNIT_TEST(packfile_read_valid_utf16_metadata) {
    std::vector<uint8_t> p;
    Put32(p, 1);
    PutMeta(p, 1, 1, 1, 0);
    PutUtf16(p, "Old mod");
    PutUtf16(p, "Someone");
    PutUtf16(p, "Text");
    PutUtf16(p, "file.dat");
    Put32(p, 0x100);
    Put32(p, 0x200);
    SetImage(p, true);
    const Result r = Run();
    CHECK(r.ok);
    CHECK_STREQ(r.name.c_str(), "Old mod");
    CHECK_STREQ(r.author.c_str(), "Someone");
    CHECK_STREQ(r.description.c_str(), "Text");
    CHECK_EQ(r.entryNames.size(), static_cast<size_t>(1));
    if (r.entryNames.size() == 1) {
        CHECK_STREQ(r.entryNames[0].c_str(), "file.dat");
        CHECK_EQ(r.entryOffsets[0], 0x100u);
        CHECK_EQ(r.entrySizes[0], 0x200u);
    }
}

UNIT_TEST(packfile_read_valid_no_metadata) {
    std::vector<uint8_t> p;
    Put32(p, 1);
    PutUtf16(p, "plain.txt");
    Put32(p, 8);
    Put32(p, 9);
    SetImage(p, false);
    const Result r = Run();
    CHECK(r.ok);
    CHECK_EQ(r.entryNames.size(), static_cast<size_t>(1));
    if (r.entryNames.size() == 1)
        CHECK_STREQ(r.entryNames[0].c_str(), "plain.txt");
}

// The longest raw name the frame holds with its terminator: 0x183f bytes.
UNIT_TEST(packfile_read_valid_longest_raw_name) {
    std::vector<uint8_t> p;
    Put32(p, 1);
    PutMeta(p, 3, 0, 0, 0);
    PutRaw(p, "n");
    PutRaw(p, "a");
    PutRaw(p, "d");
    const std::string longName(0x183e, 'L');
    PutRaw(p, longName.c_str());
    Put32(p, 1);
    Put32(p, 2);
    SetImage(p, true);
    const Result r = Run();
    CHECK(r.ok);
    CHECK_EQ(r.entryNames.size(), static_cast<size_t>(1));
    if (r.entryNames.size() == 1)
        CHECK(r.entryNames[0] == longName);
}

// ---- indeterminate stack reads (UB, fixed) ---------------------------------

// F-054: formatVersion 3 and a first string of length 0 read nothing into the
// never-written frame, then strlen'd it.
UNIT_TEST(packfile_read_raw_zero_length_first_string) {
    std::vector<uint8_t> p;
    Put32(p, 0);
    PutMeta(p, 3, 0, 0, 0);
    Put32(p, 0);              // name: length 0
    PutRaw(p, "x");
    PutRaw(p, "y");
    SetImage(p, true);
    const Result r = Run();
    CHECK(r.ok);
    CHECK_STREQ(r.name.c_str(), "");
    CHECK_STREQ(r.author.c_str(), "x");
}

// F-018: a raw string with no NUL inside its bytes: 61 62 63, length 3.
UNIT_TEST(packfile_read_raw_string_without_terminator) {
    std::vector<uint8_t> p;
    Put32(p, 1);
    PutMeta(p, 3, 0, 0, 0);
    const uint8_t name[] = {0x03, 0x00, 0x00, 0x00, 0x61, 0x62, 0x63};
    p.insert(p.end(), name, name + sizeof(name));
    PutRaw(p, "x");
    PutRaw(p, "y");
    const uint8_t entry[] = {0x02, 0x00, 0x00, 0x00, 0x65, 0x66, 0x01, 0, 0, 0, 0x02, 0, 0, 0};
    p.insert(p.end(), entry, entry + sizeof(entry));
    SetImage(p, true);
    const Result r = Run();
    CHECK(r.ok);
    CHECK_STREQ(r.name.c_str(), "abc");
    CHECK_EQ(r.entryNames.size(), static_cast<size_t>(1));
    if (r.entryNames.size() == 1)
        CHECK_STREQ(r.entryNames[0].c_str(), "ef");
}

// F-054 on the UTF-16 arm (and F-017's input): formatVersion 1, a UTF-16 string
// of 0x900 bytes ('A' 00 repeated, no terminator unit). The decode walks
// 2 * 0x900 bytes from buf860, so it reads buf860[0x900..) that the read never
// wrote (the poisoned 'B's before the frame was zeroed). What this test pins is
// the zeroed frame: with it alone, buf860[0x900] decodes to cp 0 and ends the
// name. The F-017 clamp (the walk stops at the frame's end, 0x1c0 short of
// retail's) cannot be observed on this host: the bytes past the frame are not
// checked here, and the MinGW build has no ASan. It is proven by reading and by
// the fuzz ASan reports pkC1 / pkC2.
UNIT_TEST(packfile_read_utf16_overread_past_frame) {
    std::vector<uint8_t> p;
    Put32(p, 0);
    PutMeta(p, 1, 0, 0, 0);
    Put32(p, 0x900);
    for (int i = 0; i < 0x480; ++i) {
        p.push_back(0x41);
        p.push_back(0x00);
    }
    PutUtf16(p, "x");
    PutUtf16(p, "y");
    SetImage(p, true);
    const Result r = Run();
    CHECK(r.ok);
    CHECK(r.name == std::string(0x480, 'A'));
}

// ---- malformed images: rejected --------------------------------------------

// F-020: formatVersion 3, a metadata string of length 0x2000 into the
// 0x1840-byte frame.
UNIT_TEST(packfile_read_rejects_raw_string_longer_than_frame) {
    std::vector<uint8_t> p;
    Put32(p, 1);
    PutMeta(p, 3, 0, 0, 0);
    Put32(p, 0x2000);
    p.resize(p.size() + 0x2000, 'M');
    SetImage(p, true);
    CheckRejected(Run());
}

// F-021: formatVersion 3, an entry name of length 0x2000 (and the boundary,
// 0x1840: the whole frame with no room for the terminator).
UNIT_TEST(packfile_read_rejects_raw_name_longer_than_frame) {
    const uint32_t lens[] = {0x2000, 0x1840};
    for (uint32_t len : lens) {
        std::vector<uint8_t> p;
        Put32(p, 1);
        PutMeta(p, 3, 0, 0, 0);
        PutRaw(p, "n");
        PutRaw(p, "a");
        PutRaw(p, "d");
        Put32(p, len);
        p.resize(p.size() + len, 'N');
        Put32(p, 0);
        Put32(p, 0);
        SetImage(p, true);
        CheckRejected(Run());
    }
}

// F-019: formatVersion 1 (or no metadata), a UTF-16 string or name of 0x1100
// bytes into buf860, which has 0x1040.
UNIT_TEST(packfile_read_rejects_utf16_longer_than_buffer) {
    {
        std::vector<uint8_t> p;
        Put32(p, 0);
        PutMeta(p, 1, 0, 0, 0);
        Put32(p, 0x1100);
        p.resize(p.size() + 0x1100, 0x41);
        SetImage(p, true);
        CheckRejected(Run());
    }
    {
        // pkB4_entry_buf860: no metadata, nameLen 0x1100.
        std::vector<uint8_t> p;
        Put32(p, 1);
        Put32(p, 0x1100);
        p.resize(p.size() + 0x1100, 0x41);
        Put32(p, 0);
        Put32(p, 0);
        SetImage(p, false);
        CheckRejected(Run());
    }
}

// F-019 boundary: a UTF-16 string or name of 0x1040 non-zero bytes fills buf860
// to the frame's last byte. The clamped walk's output fills buf60 (total 0x800,
// so no terminator is written), and strlen ran through buf860 and read past the
// frame. Rejected; 0x103f is the longest length kept.
UNIT_TEST(packfile_read_rejects_utf16_filling_buffer) {
    {
        std::vector<uint8_t> p;
        Put32(p, 0);
        PutMeta(p, 1, 0, 0, 0);
        Put32(p, 0x1040);
        p.resize(p.size() + 0x1040, 0x01);
        PutUtf16(p, "x");
        PutUtf16(p, "y");
        SetImage(p, true);
        CheckRejected(Run());
    }
    {
        std::vector<uint8_t> p;
        Put32(p, 1);
        Put32(p, 0x1040);
        p.resize(p.size() + 0x1040, 0x01);
        Put32(p, 0);
        Put32(p, 0);
        SetImage(p, false);
        CheckRejected(Run());
    }
}

// The longest UTF-16 length kept, 0x103f bytes of 0x01: the frame's last byte
// (buf860[0x103f]) is never written and ends the string. The name is buf60's
// 0x400 units of cp 0x0101 (C4 81), then buf860's 0x103f bytes, as retail
// builds it from the same frame.
UNIT_TEST(packfile_read_utf16_longest_kept) {
    std::string expected;
    for (int i = 0; i < 0x400; ++i)
        expected += "\xC4\x81";
    expected.append(0x103f, '\x01');
    {
        std::vector<uint8_t> p;
        Put32(p, 0);
        PutMeta(p, 1, 0, 0, 0);
        Put32(p, 0x103f);
        p.resize(p.size() + 0x103f, 0x01);
        PutUtf16(p, "x");
        PutUtf16(p, "y");
        SetImage(p, true);
        const Result r = Run();
        CHECK(r.ok);
        CHECK(r.name == expected);
        CHECK_STREQ(r.author.c_str(), "x");
    }
    {
        std::vector<uint8_t> p;
        Put32(p, 1);
        Put32(p, 0x103f);
        p.resize(p.size() + 0x103f, 0x01);
        Put32(p, 5);
        Put32(p, 6);
        SetImage(p, false);
        const Result r = Run();
        CHECK(r.ok);
        CHECK_EQ(r.entryNames.size(), static_cast<size_t>(1));
        if (r.entryNames.size() == 1) {
            CHECK(r.entryNames[0] == expected);
            CHECK_EQ(r.entryOffsets[0], 5u);
        }
    }
}

// F-033: pkH_negative_bloblen, the whole file: 04000000 ffffffff. blobLen -1
// was sign-extended into a 16 EB MemAlloc.
UNIT_TEST(packfile_read_rejects_negative_blob_length) {
    const uint8_t file[] = {0x04, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff};
    g_file.assign(file, file + sizeof(file));
    CheckRejected(Run());
}

// F-055: icon 0x10000 x 0x10000, an int32 product that overflows (to 0).
UNIT_TEST(packfile_read_rejects_icon_product_overflow) {
    std::vector<uint8_t> p;
    Put32(p, 0);
    PutMeta(p, 1, 0x10000, 0x10000, 0);
    SetImage(p, true);
    CheckRejected(Run());
}

// F-056: icon width -1, height 0x30000000 (pkD2_icon_size4_overflow: the
// size * 4 of the read overflowed int32). A negative size is a broken icon, not
// a broken package: no pixels are read and the mod loads with the 48x48 "?".
UNIT_TEST(packfile_read_negative_icon_size_loads_placeholder) {
    std::vector<uint8_t> p;
    Put32(p, 1);
    PutMeta(p, 3, 0xffffffffu, 0x30000000u, 0);
    PutRaw(p, "Mod name");
    PutRaw(p, "An author");
    PutRaw(p, "");
    PutRaw(p, "file.dat");
    Put32(p, 0x100);
    Put32(p, 0x200);
    SetImage(p, true);
    const Result r = Run();
    CHECK(r.ok);
    CHECK_EQ(g_closeCalls, 0);
    CHECK_STREQ(r.name.c_str(), "Mod name");
    CHECK_EQ(r.iconW, 48);
    CHECK_EQ(r.iconH, 48);
    CHECK_EQ(r.iconPixels, 48 * 48);
    CHECK_EQ(r.entryNames.size(), static_cast<size_t>(1));
}

// F-076: an icon (0x4000 x 0x4000, 1 GiB) or a preview (0x7fffffff bytes)
// larger than the whole index.
UNIT_TEST(packfile_read_rejects_icon_or_preview_larger_than_index) {
    {
        std::vector<uint8_t> p;
        Put32(p, 0);
        PutMeta(p, 1, 0x4000, 0x4000, 0);
        SetImage(p, true);
        CheckRejected(Run());
    }
    {
        std::vector<uint8_t> p;
        Put32(p, 0);
        PutMeta(p, 2, 0, 0, 0x7fffffffu);
        SetImage(p, true);
        CheckRejected(Run());
    }
}

// A NEGATIVE preview count is read as retail reads it: no preview, the rest of the
// index as written. Shipped mods carry one (North7.pkm stores 0xE1E1E1E1, its
// packer's uninitialised fill); rejecting it hid the mod from the Mods page.
UNIT_TEST(packfile_read_negative_preview_count_is_no_preview) {
    std::vector<uint8_t> p;
    Put32(p, 1);
    PutMeta(p, 3, 1, 1, 0xE1E1E1E1u);
    PutRaw(p, "NORTH 7");
    PutRaw(p, "An author");
    PutRaw(p, "");
    PutRaw(p, "file.dat");
    Put32(p, 0x100);
    Put32(p, 0x200);
    SetImage(p, true);
    const Result r = Run();
    CHECK(r.ok);
    CHECK_EQ(g_closeCalls, 0);
    CHECK_STREQ(r.name.c_str(), "NORTH 7");
    CHECK_EQ(r.previewBytes, 0);
    CHECK_EQ(r.entryNames.size(), static_cast<size_t>(1));
}

// F-077: a file count the index cannot hold (each entry is at least 12 bytes:
// name length, offset, size). 5000 entries need 60000 bytes; the index has
// 32764 after the count. (The fuzz input used 0x7fffffff, a 2^31-iteration
// loop; 5000 keeps the pre-hardening run short.)
UNIT_TEST(packfile_read_rejects_file_count_larger_than_index) {
    std::vector<uint8_t> p;
    Put32(p, 5000);
    SetImage(p, false);
    CheckRejected(Run());
}
