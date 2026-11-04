// ============================================================================
// tests/unit/b39_io_classes_test.cpp -- ENDGAME B.3.9 block IO (B39_RULES D1,
// rule B9-16): the IO classes whose minted tables were retired, built through
// their REAL constructors.
// ============================================================================
//
// Each class gets: not abstract, the dtor shape, slot order via
// pointer-to-virtual-member (adjustment 0), the ctor's retail stores, and --
// where the class has a slot-0 deleting dtor -- both arms. B3X W1: retail
// slot 0 is a public `virtual ~T()` (D1 + D0): flags 0 is `p->~T()`, flags 1
// `delete p` (the free proven on a Recording<T> stand-in whose class operator
// delete records, where T is not final), and the retired vector arm (flags 3)
// is `delete[]` of a `new T[n]` (destructor order proven on the ArrLog<T>
// stand-in below where T is not final). The out-of-line member bodies live in
// src/engine/io/, which stands on the port registry and does not link here;
// the few this test needs to reach are stubbed below and say so.
// ============================================================================

#include "unit.h"
#include "slot_check.h"
#include "dtor_check.h"

#include "io/Serializer.h"
#include "io/FileStream.h"
#include "io/PackageFile.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <new>
#include <type_traits>
#include "platform_file.h"   // Platform::kInvalidFileHandle (FileInfo::winHandle is a Platform::FileHandle)

namespace {

const void* const* VTableOf(const void* obj) {
    return *reinterpret_cast<const void* const* const*>(obj);
}

// B3X W1: a stand-in whose destructor logs `this` once per object, so a
// `delete[]` of `new ArrLog<T>[n]` can be checked for running the n
// destructors downward (the order retail's vector arm destroyed in).
int         g_arrN = 0;
const void* g_arrOrder[8] = {};
template <class T>
struct ArrLog : T {
    using T::T;
    ~ArrLog() override {
        if (g_arrN < 8) g_arrOrder[g_arrN] = this;
        ++g_arrN;
    }
};

// Retail flags 1 on a non-final T: `delete` through T* of a Recording<T> runs
// the chain once and its class-scope operator delete receives the block.
template <class T>
void CheckRecordedDelete() {
    static_assert(dtor_check::HasClassDelete<T>::value, "D0 frees through a class-scope operator delete");
    T* r = dtor_check::NewOnMalloc<dtor_check::Recording<T>>();
    dtor_check::ResetFrees();
    delete r;
    CHECK_EQ(dtor_check::g_frees, 1);
    CHECK(dtor_check::g_lastFree == static_cast<const void*>(r));
}

}  // namespace

// ---------------------------------------------------------------------------
// Host stand-ins for the text and visitor encodings' factory bodies
// (src/engine/io/encoding_visitor_text.cpp). CreateReader is each class's key
// function, so defining the pair here is what emits the two vtables into the test
// binary. The tests never call them. BinaryEncoding's real pair
// (src/engine/io/encoding.cpp) is linked since save_metadata_hardening_test.cpp
// reads a record through it.
// ---------------------------------------------------------------------------
namespace IO {
ReaderWriter* TextEncoding::CreateReader(Serializer&, int)    { return nullptr; }
ReaderWriter* TextEncoding::CreateWriter(Serializer&, int)    { return nullptr; }
ReaderWriter* VisitorEncoding::CreateReader(Serializer&, int) { return nullptr; }
ReaderWriter* VisitorEncoding::CreateWriter(Serializer&, int) { return nullptr; }
}  // namespace IO

// ===========================================================================
// IO::BinaryEncoding / TextEncoding / VisitorEncoding -- the three unexported
// minted tables 0x1801554a8 / 0x1801559d0 / 0x1801559f8 (rows 0, 2, 3).
// ===========================================================================
// Every stamper now spells `::new (p) IO::XEncoding()` into the 8-byte
// MemAlloc(8,4) block. Retail's whole "constructor" is ONE vptr store, so the
// real class must write the first 8 bytes and nothing after them.
UNIT_TEST(b39_io_encoding_slot_order_by_pmf) {
    CHECK_SLOT(IO::Encoding, IsBinary,     0x00/8);
    CHECK_SLOT(IO::Encoding, IsText,       0x08/8);
    CHECK_SLOT(IO::Encoding, GetKind,      0x10/8);
    CHECK_SLOT(IO::Encoding, CreateReader, 0x18/8);
    CHECK_SLOT(IO::Encoding, CreateWriter, 0x20/8);
    CHECK(!std::is_abstract<IO::BinaryEncoding>::value);
    CHECK(!std::is_abstract<IO::TextEncoding>::value);
    CHECK(!std::is_abstract<IO::VisitorEncoding>::value);
    CHECK(!std::has_virtual_destructor<IO::Encoding>::value);
}

template <class E>
static bool PlacementWritesOnlyTheVptr(bool bin, bool txt, int kind) {
    alignas(8) uint8_t buf[16];
    std::memset(buf, 0xCD, sizeof buf);
    IO::Encoding* e = ::new (buf) E();
    bool ok = static_cast<void*>(e) == static_cast<void*>(buf);
    for (int i = 8; i < 16; ++i) ok = ok && buf[i] == 0xCD;   // one qword only
    ok = ok && VTableOf(buf) != nullptr;
    ok = ok && e->IsBinary() == bin && e->IsText() == txt && e->GetKind() == kind;
    alignas(8) uint8_t buf2[8];
    ::new (buf2) E();
    ok = ok && VTableOf(buf) == VTableOf(buf2);                // one table per class
    return ok;
}

UNIT_TEST(b39_io_encoding_ctor_is_one_vptr_store_with_retails_constants) {
    CHECK(PlacementWritesOnlyTheVptr<IO::BinaryEncoding>(true, false, 2));    // 0x1800a81e0/d0/c0
    CHECK(PlacementWritesOnlyTheVptr<IO::TextEncoding>(false, true, 1));      // 0x1800b9ab0/aa0/a90
    CHECK(PlacementWritesOnlyTheVptr<IO::VisitorEncoding>(false, false, 3));  // 0x1800bafe0/d0/c0
    alignas(8) uint8_t a[8], b[8], c[8];
    ::new (a) IO::BinaryEncoding();
    ::new (b) IO::TextEncoding();
    ::new (c) IO::VisitorEncoding();
    CHECK(VTableOf(a) != VTableOf(b));
    CHECK(VTableOf(b) != VTableOf(c));
    CHECK(VTableOf(a) != VTableOf(c));
}

// ===========================================================================
// IO::BinaryReader / BinaryWriter / Visitor, and their base IO::ReaderWriter
// -- rows 14 / 15 / 150 / 109.
// ===========================================================================

// The out-of-line virtual bodies are the real ones: src/engine/io/serialize_family.cpp
// is linked (tests/unit/sources.cmake, issue #13). The tests below never dispatch them.

namespace {

// A stream reached ONLY through the four slots the family uses (retail +0x10
// Write, +0x18 Read, +0x20 SetCurrentPos, +0x28 GetCurrentPos) over a 256-byte
// buffer, counting reads. B.4 TESTS: it IS an IO::Stream (the class is real
// since B3X W1), so the family's by-name member calls go through the
// compiler's own table (D1, D0, then retail slots 1..6 one index up) -- the
// table a live stream presents -- instead of a hand-built one.
static_assert(RetailSlot<IO::Stream>(0x10 / 8) == 3 && RetailSlot<IO::Stream>(0x28 / 8) == 6,
              "Write .. GetCurrentPos at compiler slots 3 .. 6");
struct FakeStream final : IO::Stream {
    uint8_t buf[256];
    int32_t pos;
    int32_t reads;

    FakeStream() : buf(), pos(0), reads(0) {}
    ~FakeStream() override = default;   // _encoding stays null: ~Stream frees nothing

    bool Write(int n, const void* src) override {
        std::memcpy(buf + pos, src, (size_t)n); pos += n; return true;
    }
    bool Read(int n, void* dst) override {
        std::memcpy(dst, buf + pos, (size_t)n); pos += n; ++reads; return true;
    }
    bool SetCurrentPos(int p) override { pos = p; return true; }
    uint32_t GetCurrentPos() const override { return (uint32_t)pos; }
    uint32_t GetSize() const override { return 0; }   // never reached by the family
};

void InitFake(FakeStream& f) {
    std::memset(f.buf, 0, sizeof f.buf);
    f.pos = 0;
    f.reads = 0;
}

// A Serializer's 16 bytes pointing at the fake stream (the Serializer ctor
// itself asks the stream's Encoding for a ReaderWriter; these tests build the
// ReaderWriter directly, as the Encoding factories do).
struct SerBuf { alignas(8) uint8_t b[16]; };
IO::Serializer& SerOver(SerBuf& sb, FakeStream& f) {
    std::memset(sb.b, 0, sizeof sb.b);
    IO::Serializer& s = *reinterpret_cast<IO::Serializer*>(sb.b);
    s._stream = &f;
    return s;
}

int32_t Dword(const FakeStream& f, int off) {
    int32_t v; std::memcpy(&v, f.buf + off, 4); return v;
}

}  // namespace

UNIT_TEST(b39_io_readerwriter_family_shape) {
    CHECK(std::is_abstract<IO::ReaderWriter>::value);          // 25 _purecall slots
    CHECK(!std::is_abstract<IO::BinaryReader>::value);
    CHECK(!std::is_abstract<IO::BinaryWriter>::value);
    CHECK(!std::is_abstract<IO::Visitor>::value);
    // B3X W1: public virtual destructors (retail slot 0 = D1 + D0).
    CHECK(std::is_destructible<IO::BinaryReader>::value);
    CHECK(std::is_destructible<IO::BinaryWriter>::value);
    CHECK(std::is_destructible<IO::Visitor>::value);
    CHECK(std::has_virtual_destructor<IO::BinaryReader>::value);
    CHECK(std::has_virtual_destructor<IO::BinaryWriter>::value);
    CHECK(std::has_virtual_destructor<IO::Visitor>::value);
    CHECK_DTOR_SLOT(IO::BinaryReader);
    CHECK_SLOT(IO::BinaryReader, GetVersion,         0x08/8);
    CHECK_SLOT(IO::BinaryReader, Skip,               0x10/8);
    CHECK_SLOT(IO::BinaryReader, SerializeCharArray, 0x18/8);
    CHECK_SLOT(IO::BinaryReader, SerializeChar,      0x70/8);
    CHECK_SLOT(IO::BinaryReader, SerializeInt,       0xA0/8);
    CHECK_SLOT(IO::BinaryReader, SerializeBytes,     0xC8/8);
    CHECK_DTOR_SLOT(IO::BinaryWriter);
    CHECK_SLOT(IO::BinaryWriter, SerializeUInt,  0x98/8);
    CHECK_SLOT(IO::BinaryWriter, SerializeBytes, 0xC8/8);
    CHECK_DTOR_SLOT(IO::Visitor);
    CHECK_SLOT(IO::Visitor, GetVersion,     0x08/8);
    CHECK_SLOT(IO::Visitor, SerializeBytes, 0xC8/8);
}

// 0x1800a8a30 then 0x1800a8980: magic, -1 placeholder, version; then the
// trailer and the BACK-PATCHED length (everything after the placeholder,
// version dword included) -- the 12 + 4 bytes the resave gate sees per record.
UNIT_TEST(b39_io_binarywriter_ctor_and_dtor_frame_the_record) {
    FakeStream f; InitFake(f);
    SerBuf sb; IO::Serializer& ser = SerOver(sb, f);
    alignas(8) uint8_t mem[0x30];
    std::memset(mem, 0xCD, sizeof mem);
    IO::BinaryWriter* w = ::new (mem) IO::BinaryWriter(ser, 7);
    CHECK_EQ((int)mem[0x08], 0);                         // allowCustomSerialize
    for (int i = 0x09; i < 0x10; ++i) CHECK_EQ((int)mem[i], 0xCD);   // not written
    CHECK(w->serializer == &ser);
    CHECK(w->stream == static_cast<void*>(&f));
    CHECK_EQ(w->word20, 4);                              // after the magic
    CHECK_EQ(w->word24, 8);                              // after the placeholder
    CHECK_EQ(w->version, 7);
    CHECK_EQ(w->GetVersion(), 7);
    CHECK_EQ((uint32_t)Dword(f, 0), 0xaaaaaaaau);
    CHECK_EQ(Dword(f, 4), -1);
    CHECK_EQ(Dword(f, 8), 7);
    int32_t payload = 0x1234;
    f.Write(4, &payload);                                // one field of the record
    w->~BinaryWriter();                                  // retail flags 0: no free
    CHECK_EQ((uint32_t)Dword(f, 16), 0xbbbbbbbbu);       // trailer
    CHECK_EQ(Dword(f, 4), 8);                            // len = 16 - 8, back-patched
    CHECK_EQ(f.pos, 20);                                 // and forward again
}

// 0x1800a8340 then 0x1800a82f0: reads magic / length / version (the argument is
// overwritten), remembers the position after the length, and the dtor swallows
// the trailer. Slot 0 with flags 1 frees through MemFree (malloc on the host):
// B3X W1, `delete r` — BinaryReader's D0 and ReaderWriter's class-scope
// operator delete (std::free here, matching the std::malloc block).
UNIT_TEST(b39_io_binaryreader_consumes_the_record_both_arms) {
    FakeStream f; InitFake(f);
    const int32_t rec[5] = { (int32_t)0xaaaaaaaau, 8, 9, 0x1234, (int32_t)0xbbbbbbbbu };
    std::memcpy(f.buf, rec, sizeof rec);
    SerBuf sb; IO::Serializer& ser = SerOver(sb, f);
    void* p = std::malloc(0x30);
    IO::BinaryReader* r = ::new (p) IO::BinaryReader(ser, 99);
    CHECK_EQ(r->word20, 8);
    CHECK_EQ(r->word24, 8);
    CHECK_EQ(r->version, 9);                             // 99 overwritten by the read
    CHECK_EQ(f.reads, 3);
    f.pos = 16;                                          // past the payload
    static_assert(dtor_check::HasClassDelete<IO::BinaryReader>::value, "ReaderWriter::operator delete");
    delete r;                                            // frees p
    CHECK_EQ(f.reads, 4);                                // the trailer read
    CHECK_EQ(f.pos, 20);

    // The retired VECTOR arm (0x180023a10: count at this-8, stride 0x30,
    // downward) becomes `delete[]` of a `new BinaryReader[2]` (the public
    // (serializer, version) ctor, one braced initialiser per element): both
    // destructors run, each swallowing its trailer. (BinaryReader is final, so
    // no logging stand-in: the two trailer reads are the observable.)
    InitFake(f);
    IO::BinaryReader* a = new IO::BinaryReader[2]{{ser, 0}, {ser, 0}};
    f.reads = 0;
    delete[] a;
    CHECK_EQ(f.reads, 2);                                // both dtors ran
}

// The Visitor factories' inlined ctor: base stores + version at +0x20; no
// stream traffic. Both slot-0 arms (stride 0x28).
UNIT_TEST(b39_io_visitor_ctor_stores_and_both_arms) {
    FakeStream f; InitFake(f);
    SerBuf sb; IO::Serializer& ser = SerOver(sb, f);
    alignas(8) uint8_t mem[0x28];
    std::memset(mem, 0xCD, sizeof mem);
    IO::Visitor* v = ::new (mem) IO::Visitor(ser, 5);
    CHECK_EQ((int)mem[0x08], 0);
    CHECK(v->serializer == &ser);
    CHECK(v->stream == static_cast<void*>(&f));
    CHECK_EQ(v->GetVersion(), 5);
    int32_t at20; std::memcpy(&at20, mem + 0x20, 4);
    CHECK_EQ(at20, 5);
    for (int i = 0x24; i < 0x28; ++i) CHECK_EQ((int)mem[i], 0xCD);  // _pad24 untouched
    CHECK_EQ(f.pos, 0);
    v->~Visitor();                                       // retail flags 0

    // Retail flags 1 (0x18002b710's free arm): `delete` of a heap Visitor;
    // ~Visitor is `= default`, so the observable is that it links, runs and
    // frees through ReaderWriter's class-scope operator delete.
    static_assert(dtor_check::HasClassDelete<IO::Visitor>::value, "ReaderWriter::operator delete");
    delete ::new (std::malloc(0x28)) IO::Visitor(ser, 1);
    // The retired vector arm (0x18002b710, stride 0x28) becomes `delete[]` of a
    // `new Visitor[3]`; the elements are the three (serializer, i) objects.
    IO::Visitor* a = new IO::Visitor[3]{{ser, 0}, {ser, 1}, {ser, 2}};
    CHECK_EQ(a[2].GetVersion(), 2);
    CHECK_EQ((int)(reinterpret_cast<uint8_t*>(&a[1]) - reinterpret_cast<uint8_t*>(&a[0])), 0x28);
    delete[] a;
    CHECK_EQ(f.pos, 0);                                  // no stream traffic
}

// ===========================================================================
// IO::VisitorStream -- row 151. `?Add@ExternalFactory@` stack-builds one.
// ===========================================================================
UNIT_TEST(b39_io_visitorstream_ctor_and_both_arms) {
    CHECK(!std::is_abstract<IO::VisitorStream>::value);
    CHECK(std::is_destructible<IO::VisitorStream>::value);        // B3X W1
    CHECK(std::has_virtual_destructor<IO::VisitorStream>::value);
    CHECK_DTOR_SLOT(IO::VisitorStream);
    CHECK_SLOT(IO::VisitorStream, GetSource, 0x08/8);
    CHECK_SLOT(IO::VisitorStream, Write,     0x10/8);
    CHECK_SLOT(IO::VisitorStream, GetSize,   0x30/8);

    // 0x1800bb090: +0x08 = 1 (WRITING), +0x10 = a fresh VISITOR encoding,
    // +0x0c never written.
    alignas(8) uint8_t mem[0x18];
    std::memset(mem, 0xCD, sizeof mem);
    IO::VisitorStream* vs = ::new (mem) IO::VisitorStream();
    CHECK_EQ(vs->_direction, 1);
    CHECK_EQ(vs->GetDirection(), IO::kWriting);
    for (int i = 0x0c; i < 0x10; ++i) CHECK_EQ((int)mem[i], 0xCD);
    CHECK(vs->_encoding != nullptr);
    CHECK_EQ(vs->_encoding->GetKind(), 3);
    CHECK(!vs->_encoding->IsBinary());
    vs->~VisitorStream();                      // flags 0: ~VisitorStream -> ~Stream frees the encoding

    // Flags 1, the free proven on a Recording<VisitorStream>.
    CheckRecordedDelete<IO::VisitorStream>();
    // The retired vector arm (0x18002bf80: stride 0x18) -> delete[].
    CHECK_ARRAY_DELETE(ArrLog<IO::VisitorStream>, g_arrN, g_arrOrder);
}

// ===========================================================================
// IO::FileStream / BinaryFileStream / TextFileStream -- rows 58 / 13 / 139.
// ===========================================================================
// ~FileStream / ~TextFileStream and the TextFileStream ctor are file_stream.cpp's
// (host stubs in io_stream_abi_stubs.cpp); the FileStream and BinaryFileStream
// constructors and all three slot-0 bodies are inline and REAL here.
UNIT_TEST(b39_io_filestream_family_shape) {
    CHECK(!std::is_abstract<IO::FileStream>::value);
    CHECK(!std::is_abstract<IO::BinaryFileStream>::value);
    CHECK(!std::is_abstract<IO::TextFileStream>::value);
    // B3X W1: public virtual destructors (retail slot 0 = D1 + D0).
    CHECK(std::is_destructible<IO::FileStream>::value);
    CHECK(std::is_destructible<IO::BinaryFileStream>::value);
    CHECK(std::is_destructible<IO::TextFileStream>::value);
    CHECK(std::has_virtual_destructor<IO::FileStream>::value);
    CHECK(std::has_virtual_destructor<IO::TextFileStream>::value);
    CHECK_DTOR_SLOT(IO::FileStream);
    CHECK_SLOT(IO::FileStream, GetSource, 0x08/8);
    CHECK_SLOT(IO::FileStream, Open,      0x38/8);   // NOT Close
    CHECK_DTOR_SLOT(IO::BinaryFileStream);
    CHECK_DTOR_SLOT(IO::TextFileStream);
    CHECK_SLOT(IO::TextFileStream, OpenWithFormat, 0x40/8);
    CHECK_SLOT(IO::TextFileStream, Create,         0x48/8);
}

// 0x1800aaa10: Stream base, `_filename` null, FileInfo handle -1, size/cursor
// 0, baseOffset 0, both names null, winHandle -1; +0x24 (_unk04), +0x34
// (_pad14) and +0x0c never written; NO encoding. 0x1800a8200 adds the BINARY
// encoding and nothing else.
UNIT_TEST(b39_io_filestream_and_binaryfilestream_ctor_stores) {
    alignas(8) uint8_t mem[0x50];
    std::memset(mem, 0xCD, sizeof mem);
    IO::FileStream* fs = ::new (mem) IO::FileStream();
    CHECK_EQ(fs->_direction, 0);
    CHECK(fs->_encoding == nullptr);
    CHECK(fs->_filename._data == nullptr);
    CHECK_EQ(fs->_file.handle, -1);
    CHECK_EQ(fs->_file.size, 0);
    CHECK_EQ(fs->_file.cursor, 0);
    CHECK_EQ(fs->_file.baseOffset, 0);
    CHECK_EQ(fs->_file.winHandle, Platform::kInvalidFileHandle);
    // INVALID_HANDLE_VALUE is all bits set: the value retail's inlined ctor stores.
    CHECK_EQ(reinterpret_cast<uintptr_t>(fs->_file.winHandle), ~uintptr_t{0});
    CHECK(fs->_file._name0._data == nullptr);
    CHECK(fs->_file._name1._data == nullptr);
    for (int i = 0x0c; i < 0x10; ++i) CHECK_EQ((int)mem[i], 0xCD);
    for (int i = 0x24; i < 0x28; ++i) CHECK_EQ((int)mem[i], 0xCD);   // FileInfo::_unk04
    for (int i = 0x34; i < 0x38; ++i) CHECK_EQ((int)mem[i], 0xCD);   // FileInfo padding at +0x14
    fs->~FileStream();                                  // retail flags 0

    alignas(8) uint8_t mem2[0x50];
    IO::BinaryFileStream* bfs = ::new (mem2) IO::BinaryFileStream();
    CHECK(bfs->_encoding != nullptr);
    CHECK(bfs->_encoding->IsBinary());
    CHECK_EQ(bfs->_encoding->GetKind(), 2);
    CHECK_EQ(bfs->_file.handle, -1);
    CHECK(VTableOf(bfs) != VTableOf(fs));               // slot 0 differs, one table per class
    bfs->~BinaryFileStream();                           // ~Stream frees the encoding
}

// Both arms of the three slot-0 bodies (0x18000c090 / 0x180023d80 stride 0x50,
// 0x18002ba90 stride 0x80): flags 1 frees `this`, flags 3 freed `this - 8` and
// returned it. B3X W1: flags 1 is `delete` (the free proven on Recording<T>);
// the vector arm has no Itanium counterpart and `delete[]` replaces it.
UNIT_TEST(b39_io_filestream_family_deletingdtor_arms) {
    IO::FileStream* fs = ::new (std::malloc(0x50)) IO::FileStream();
    delete fs;
    IO::BinaryFileStream* bfs = ::new (std::malloc(0x50)) IO::BinaryFileStream();
    delete bfs;
    CheckRecordedDelete<IO::FileStream>();
    CheckRecordedDelete<IO::BinaryFileStream>();

    CHECK_ARRAY_DELETE(ArrLog<IO::BinaryFileStream>, g_arrN, g_arrOrder);

    // TextFileStream has only retail's (type, name, version) ctor: one braced
    // initialiser per element; the destructors still run downward.
    using TLog = ArrLog<IO::TextFileStream>;
    TLog* t = new TLog[2]{{System::String(), System::String(), 0},
                          {System::String(), System::String(), 0}};
    CHECK_EQ(t[0]._requestedFormat, 6);
    const void* t0 = &t[0];
    const void* t1 = &t[1];
    g_arrN = 0;
    delete[] t;
    CHECK_EQ(g_arrN, 2);
    CHECK(g_arrOrder[0] == t1);
    CHECK(g_arrOrder[1] == t0);
}

// ===========================================================================
// IO::MemoryStream -- row 88. 0x1800ae710 / 0x1800ae890 / slot 0 0x1800242a0.
// ===========================================================================
UNIT_TEST(b39_io_memorystream_shape_ctor_and_both_arms) {
    CHECK(!std::is_abstract<IO::MemoryStream>::value);
    CHECK(std::is_destructible<IO::MemoryStream>::value);         // B3X W1
    CHECK(std::has_virtual_destructor<IO::MemoryStream>::value);
    CHECK_DTOR_SLOT(IO::MemoryStream);
    CHECK_SLOT(IO::MemoryStream, Write, 0x10/8);
    CHECK_SLOT(IO::MemoryStream, Close, 0x38/8);   // NOT Open

    // +0x08 = 0, +0x18 = null, +0x20 = 0 (one byte), +0x24..+0x2b = 0, +0x10 =
    // a fresh BINARY encoding; +0x0c, +0x21..+0x23 and +0x2c never written.
    alignas(8) uint8_t mem[0x30];
    std::memset(mem, 0xCD, sizeof mem);
    IO::MemoryStream* ms = ::new (mem) IO::MemoryStream();
    CHECK_EQ(ms->_direction, 0);
    CHECK(ms->_data == nullptr);
    CHECK_EQ((int)ms->_ownsData, 0);
    CHECK_EQ(ms->_cursor, 0u);
    CHECK_EQ(ms->_size, 0u);
    CHECK(ms->_encoding != nullptr);
    CHECK(ms->_encoding->IsBinary());
    for (int i = 0x0c; i < 0x10; ++i) CHECK_EQ((int)mem[i], 0xCD);
    for (int i = 0x21; i < 0x24; ++i) CHECK_EQ((int)mem[i], 0xCD);
    for (int i = 0x2c; i < 0x30; ++i) CHECK_EQ((int)mem[i], 0xCD);
    // An OWNED buffer is freed by ~MemoryStream, then ~Stream frees the encoding.
    ms->_data = std::malloc(16);
    ms->_ownsData = 1;
    ms->~MemoryStream();                                // retail flags 0

    // flags 1 frees `this` (`delete`, proven on Recording<MemoryStream>); the
    // retired vector arm of 0x1800242a0 (stride 0x30) becomes `delete[]`.
    IO::MemoryStream* heap = ::new (std::malloc(0x30)) IO::MemoryStream();
    delete heap;
    CheckRecordedDelete<IO::MemoryStream>();
    CHECK_ARRAY_DELETE(ArrLog<IO::MemoryStream>, g_arrN, g_arrOrder);
}

// ===========================================================================
// IO::CompressedFileStream -- row 31. 0x1800a93b0 / slot 0 0x180024100.
// ===========================================================================
// ~CompressedFileStream and ~CompressedStream are host stubs (release nothing);
// the constructors and slot 0 are inline and REAL here.
UNIT_TEST(b39_io_compressedfilestream_shape_ctor_and_both_arms) {
    CHECK(!std::is_abstract<IO::CompressedFileStream>::value);
    CHECK(std::is_destructible<IO::CompressedFileStream>::value);         // B3X W1
    CHECK(std::has_virtual_destructor<IO::CompressedFileStream>::value);
    CHECK_DTOR_SLOT(IO::CompressedFileStream);
    CHECK_SLOT(IO::CompressedFileStream, Close,     0x38/8);
    CHECK_SLOT(IO::CompressedFileStream, PreCommit, 0x40/8);

    // 0x1800aa3d0: +0x1c = 0, +0x20 = -1, +0x28..+0x67 = 0, a BINARY encoding;
    // +0x0c, +0x18, +0x24 never written. 0x1800a93b0 then builds the
    // BinaryFileStream member at +0x68 (its own Binary encoding, handle -1).
    alignas(8) uint8_t mem[0xb8];
    std::memset(mem, 0xCD, sizeof mem);
    IO::CompressedFileStream* cfs = ::new (mem) IO::CompressedFileStream();
    CHECK_EQ(cfs->_direction, 0);
    CHECK_EQ(cfs->_position, 0);
    CHECK_EQ(cfs->_activeWindow, -1);
    CHECK(cfs->_source == nullptr);
    CHECK(cfs->_windowData == nullptr);
    CHECK_EQ(cfs->_headerSize, 0);
    CHECK(cfs->_blockData == nullptr);
    CHECK(cfs->_compression == nullptr);
    CHECK(cfs->_encoding != nullptr);
    CHECK(cfs->_encoding->IsBinary());
    for (int i = 0x0c; i < 0x10; ++i) CHECK_EQ((int)mem[i], 0xCD);
    for (int i = 0x18; i < 0x1c; ++i) CHECK_EQ((int)mem[i], 0xCD);
    for (int i = 0x24; i < 0x28; ++i) CHECK_EQ((int)mem[i], 0xCD);
    CHECK(cfs->_file._encoding != nullptr);
    CHECK(cfs->_file._encoding != cfs->_encoding);
    CHECK_EQ(cfs->_file._file.handle, -1);
    CHECK(VTableOf(cfs) != VTableOf(&cfs->_file));
    cfs->~CompressedFileStream();                       // retail flags 0

    IO::CompressedFileStream* heap = ::new (std::malloc(0xb8)) IO::CompressedFileStream();
    delete heap;                                        // retail flags 1
    CheckRecordedDelete<IO::CompressedFileStream>();
    // The retired vector arm of 0x180024100 (stride 0xb8) -> delete[].
    CHECK_ARRAY_DELETE(ArrLog<IO::CompressedFileStream>, g_arrN, g_arrOrder);
}

// ===========================================================================
// IO::PackageFile -- row 94. ??0 0x1800b4440 / slot 0 0x1800259d0.
// ===========================================================================
// The slot bodies and ~PackageFile are host stubs (io_stream_abi_stubs.cpp);
// the constructor and slot 0 are inline and REAL here.
UNIT_TEST(b39_io_packagefile_shape_ctor_and_both_arms) {
    CHECK(!std::is_abstract<IO::PackageFile>::value);
    CHECK(std::is_destructible<IO::PackageFile>::value);          // B3X W1
    CHECK(std::has_virtual_destructor<IO::PackageFile>::value);
    CHECK_DTOR_SLOT(IO::PackageFile);
    CHECK_SLOT(IO::PackageFile, TypeName,  0x08/8);
    CHECK_SLOT(IO::PackageFile, Serialize, 0x28/8);
    CHECK_SLOT(IO::PackageFile, Destruct,  0x38/8);
    CHECK_SLOT(IO::PackageFile, Construct, 0x40/8);

    // 0x1800b4440's stores; +0x78..+0x7f, +0x90..+0x97, +0xad..+0xaf, +0xc4
    // and +0xd4 are never written.
    alignas(8) uint8_t mem[0xf0];
    std::memset(mem, 0xCD, sizeof mem);
    IO::PackageFile* pf = ::new (mem) IO::PackageFile();
    CHECK(pf->_path._data == nullptr);
    CHECK(pf->_stringArray20._data == nullptr);
    CHECK_EQ(pf->_stringArray30._size, 0);
    CHECK(pf->_name._data == nullptr);
    CHECK(pf->_string60._data == nullptr);
    CHECK_EQ(pf->_version, 0);
    CHECK_EQ(pf->_priority, 0x2710);
    CHECK_EQ(pf->_platform, 0);
    CHECK_EQ(pf->_iconData._size, 0);
    CHECK(pf->_previewData._data == nullptr);
    CHECK_EQ(pf->_crc, 0u);
    CHECK_EQ((int)pf->_overridable, 1);
    CHECK_EQ(pf->_entries._size, 0);
    CHECK_EQ(pf->_file.handle, -1);
    CHECK_EQ(pf->_file.size, 0);
    CHECK_EQ(pf->_file.baseOffset, 0);
    CHECK_EQ(pf->_file.winHandle, Platform::kInvalidFileHandle);
    CHECK(pf->_file._name1._data == nullptr);
    for (int i = 0x78; i < 0x80; ++i) CHECK_EQ((int)mem[i], 0xCD);
    for (int i = 0x90; i < 0x98; ++i) CHECK_EQ((int)mem[i], 0xCD);
    for (int i = 0xad; i < 0xb0; ++i) CHECK_EQ((int)mem[i], 0xCD);
    for (int i = 0xc4; i < 0xc8; ++i) CHECK_EQ((int)mem[i], 0xCD);
    for (int i = 0xd4; i < 0xd8; ++i) CHECK_EQ((int)mem[i], 0xCD);
    pf->~PackageFile();                                 // retail flags 0

    IO::PackageFile* heap = ::new (std::malloc(0xf0)) IO::PackageFile();
    delete heap;                                        // retail flags 1
    CheckRecordedDelete<IO::PackageFile>();
    // The retired vector arm of 0x1800259d0 (stride 0xf0) -> delete[].
    CHECK_ARRAY_DELETE(ArrLog<IO::PackageFile>, g_arrN, g_arrOrder);
}

// ===========================================================================
// IO::CompressedMemoryStream -- row 32 (W2). ??0 0x1800a9520 / slot 0 0x1800244a0.
// ===========================================================================
// ~CompressedMemoryStream / ~CompressedStream are host stubs (release nothing);
// the constructors and slot 0 are inline and REAL here.
UNIT_TEST(b39_io_compressedmemorystream_shape_ctor_and_both_arms) {
    CHECK(!std::is_abstract<IO::CompressedMemoryStream>::value);
    CHECK(std::is_destructible<IO::CompressedMemoryStream>::value);         // B3X W1
    CHECK(std::has_virtual_destructor<IO::CompressedMemoryStream>::value);
    CHECK_DTOR_SLOT(IO::CompressedMemoryStream);
    CHECK_SLOT(IO::CompressedMemoryStream, Close,     0x38/8);
    CHECK_SLOT(IO::CompressedMemoryStream, PreCommit, 0x40/8);

    // 0x1800a9520 = 0x1800aa3d0 (the base) + the vptr + 0x1800ae710 at +0x68.
    alignas(8) uint8_t mem[0x98];
    std::memset(mem, 0xCD, sizeof mem);
    IO::CompressedMemoryStream* cms = ::new (mem) IO::CompressedMemoryStream();
    CHECK_EQ(cms->_direction, 0);
    CHECK_EQ(cms->_position, 0);
    CHECK_EQ(cms->_activeWindow, -1);
    CHECK(cms->_source == nullptr);
    CHECK(cms->_headerData == nullptr);
    CHECK_EQ(cms->_headerSize, 0);
    CHECK(cms->_compression == nullptr);
    CHECK(cms->_encoding != nullptr);
    CHECK(cms->_encoding->IsBinary());
    for (int i = 0x18; i < 0x1c; ++i) CHECK_EQ((int)mem[i], 0xCD);
    for (int i = 0x24; i < 0x28; ++i) CHECK_EQ((int)mem[i], 0xCD);
    // the member: its own Binary encoding, data null, ownsData 0, cursor/size 0
    CHECK(cms->_memoryStream._encoding != nullptr);
    CHECK(cms->_memoryStream._encoding != cms->_encoding);
    CHECK(cms->_memoryStream._data == nullptr);
    CHECK_EQ((int)cms->_memoryStream._ownsData, 0);
    CHECK_EQ(cms->_memoryStream._size, 0u);
    CHECK(&cms->GetMemoryStream() == &cms->_memoryStream);
    CHECK_EQ((int)(reinterpret_cast<uint8_t*>(&cms->GetMemoryStream()) - mem), 0x68);
    CHECK(VTableOf(cms) != VTableOf(&cms->_memoryStream));
    cms->~CompressedMemoryStream();                     // retail flags 0

    IO::CompressedMemoryStream* heap = ::new (std::malloc(0x98)) IO::CompressedMemoryStream();
    delete heap;                                        // retail flags 1
    CheckRecordedDelete<IO::CompressedMemoryStream>();
    // The retired vector arm of 0x1800244a0 (stride 0x98) -> delete[].
    CHECK_ARRAY_DELETE(ArrLog<IO::CompressedMemoryStream>, g_arrN, g_arrOrder);
}


// ===========================================================================
// IO::PersistenceWorker -- unexported row 1 (0x180155860, W2).
// ===========================================================================
// Shape only: constructing one starts a real thread (??0Thread's
// _beginthreadex), which a host test must not do. The layout pins and the
// POD cross-asserts are static_asserts in the header.
#include "io/PersistenceWorker.h"
UNIT_TEST(b39_io_persistenceworker_shape) {
    CHECK(!std::is_abstract<IO::PersistenceWorker>::value);
    CHECK(!std::is_destructible<IO::PersistenceWorker>::value);
    CHECK(!std::has_virtual_destructor<IO::PersistenceWorker>::value);
    CHECK((std::is_base_of<System::Thread, IO::PersistenceWorker>::value));
    CHECK_SLOT(IO::PersistenceWorker, Run,         0x00/8);
    CHECK_SLOT(IO::PersistenceWorker, OnCompleted, 0x08/8);
    // sizeof 0x80 / _command +0x3c are Persistence.h's static_asserts.
}
