// ============================================================================
// tests/unit/io_streams_abi_test.cpp — the REAL `IO` stream classes' vtable
// order and layout.   ENDGAME B.3.4 block 4 (STREAMS).
// ============================================================================
//
// WHY THIS IS ITS OWN FILE. `io_vtable_abi_test.cpp` is the shared IO ABI suite
// and it already carries the Encoding / ReaderWriter / Serializer pins from
// blocks 1-3, plus the first two STREAMS blocks' probes. Two parallel agents
// appending to the END of that one file produced a merge conflict whose marker
// split the single shared closing brace, so from block 4's third commit onward
// the stream probes live here. Registration is automatic (CMakeLists.txt globs
// `tests/unit/*.cpp`); the helper channel below is this file's own copy,
// deliberately, because the one in io_vtable_abi_test.cpp has internal linkage.
//
// WHAT IT PINS, AND WHY IT HAS TO EXIST
// -------------------------------------
// Nothing else in the build compiles a real `IO` stream vtable at all. Every
// live stream wears a MINTED table out of `standalone/rt_data_mint_tables.inc`
// and every registered door calls QUALIFIED, so a slot declared one line out of
// place in src/engine/io/Stream.h or FileStream.h would stay invisible right
// through B.3.4 and surface only when B.3 steps 5-8 retire the minted rows — as
// a stream that reads through `Close` or seeks through `Open`, with no recent
// change to blame.
//
// The shape is block 3's section B.7 shape: a CONCRETE probe subclass of each
// real class, every slot overridden with its own marker, then a raw slot call
// at every retail byte offset asserting the marker equals offset/8.
//
// HOW SLOT 1 IS PINNED WITHOUT CALLING IT. `GetSource` returns a
// `System::String` BY VALUE, so it carries a hidden return pointer, and the
// registered door (`RtIo_Stream_GetSource`, iobase_vtables.cpp) spells that
// slot in MSVC's argument order while a C++ member compiled here uses the
// compiler's. Raw-calling it would be asserting a calling convention, not a
// slot order. So slot 1 is pinned BY ELIMINATION: every OTHER slot is
// raw-called and lands on its own marker, and all the table's entries are
// asserted distinct. With n distinct entries and n-1 of them pinned, the
// remaining one is at the remaining index — a complete proof that depends on
// no convention at all.
//
// The out-of-line bodies these probes need at link time are in
// tests/unit/io_stream_abi_stubs.cpp; that file explains why.
// ============================================================================

#include "unit.h"
#include "slot_check.h"
#include "dtor_check.h"

#include "io/Stream.h"
#include "io/FileStream.h"

#include <cstdint>
#include <cstddef>
#include <type_traits>

namespace {

// ---------------------------------------------------------------------------
// Observation channel — this file's own copy (the one in io_vtable_abi_test.cpp
// has internal linkage and is not reachable from here).
// ---------------------------------------------------------------------------
const void* g_lastSelf = nullptr;
int         g_lastMark = 0;

void Note(const void* self, int mark) { g_lastSelf = self; g_lastMark = mark; }
void Reset() { g_lastSelf = nullptr; g_lastMark = 0; }

// The one instruction every raw slot caller in runtime/src compiles to.
// `byteOffset` is RETAIL's; T (the object's class) decides the compiler's.
// Every object here is read through its `IO::Stream` base, so T is IO::Stream
// (a virtual destructor is inherited: the whole family shifts alike).
template <typename Fn, class T>
Fn SlotAt(const void* obj, int byteOffset) {
    const uint8_t* const* vt = *reinterpret_cast<const uint8_t* const* const*>(obj);
    return reinterpret_cast<Fn>(*reinterpret_cast<void* const*>(
        reinterpret_cast<const uint8_t*>(vt) + RetailByteOffset<T>(byteOffset)));
}

// ---------------------------------------------------------------------------
// Byte offsets, spelled once. `IO::Stream` is 7 slots; the concrete classes
// append theirs (B34_RULES section 1.3).
// ---------------------------------------------------------------------------
constexpr int kDtor          = 0x00;   // B3X W1: the D1/D0 pair (CheckStreamDtorPair)
constexpr int kGetSource     = 0x08;
constexpr int kWrite         = 0x10;
constexpr int kRead          = 0x18;
constexpr int kSetCurrentPos = 0x20;
constexpr int kGetCurrentPos = 0x28;
constexpr int kGetSize       = 0x30;
constexpr int kSlot7         = 0x38;   // Close on the memory/compressed branch,
                                       // Open on the FileStream branch
constexpr int kSlot8         = 0x40;   // PreCommit; the compressed branch only

// The six slots every Stream in the family shares, raw-called at their retail
// byte offsets. Each CHECK says the entry at that offset is the override
// declared at offset/8, with `this` unadjusted.
void CheckSharedStreamSlots(void* raw) {
    uint8_t scratch[8] = {0};

    Reset();
    using WriteFn = bool (*)(void*, int, const void*);
    CHECK((SlotAt<WriteFn, IO::Stream>(raw, kWrite)(raw, 8, scratch)) == true);
    CHECK_EQ(g_lastMark, kWrite / 8);

    Reset();
    using ReadFn = bool (*)(void*, int, void*);
    CHECK((SlotAt<ReadFn, IO::Stream>(raw, kRead)(raw, 8, scratch)) == true);
    CHECK_EQ(g_lastMark, kRead / 8);

    Reset();
    using SeekFn = bool (*)(void*, int);
    CHECK((SlotAt<SeekFn, IO::Stream>(raw, kSetCurrentPos)(raw, 3)) == true);
    CHECK_EQ(g_lastMark, kSetCurrentPos / 8);

    Reset();
    using U32Fn = uint32_t (*)(void*);
    SlotAt<U32Fn, IO::Stream>(raw, kGetCurrentPos)(raw);
    CHECK_EQ(g_lastMark, kGetCurrentPos / 8);

    Reset();
    SlotAt<U32Fn, IO::Stream>(raw, kGetSize)(raw);
    CHECK_EQ(g_lastMark, kGetSize / 8);
}

// Slots 7 and 8 on the compressed branch.
void CheckCompressedTail(void* raw) {
    using VoidFn = void (*)(void*);
    Reset();
    SlotAt<VoidFn, IO::Stream>(raw, kSlot7)(raw);
    CHECK_EQ(g_lastMark, kSlot7 / 8);
    CHECK_EQ(g_lastMark, 7);
    Reset();
    SlotAt<VoidFn, IO::Stream>(raw, kSlot8)(raw);
    CHECK_EQ(g_lastMark, kSlot8 / 8);
    CHECK_EQ(g_lastMark, 8);
}

// n distinct RETAIL slots — which is what turns "every slot but one is pinned"
// into "slot 1 is `GetSource`". B3X W1: retail's n slots are n + 1 compiler
// entries (retail slot 0 is the D1/D0 pair), all distinct, D1 != D0 included.
void CheckDistinctSlots(void* raw, int n) {
    void* const* vt = *reinterpret_cast<void* const* const*>(raw);
    const int entries = static_cast<int>(RetailSlot<IO::Stream>(n - 1)) + 1;
    CHECK_EQ(entries, n + 1);
    CHECK(vt[0] != vt[1]);
    for (int a = 0; a < entries; ++a)
        for (int b = a + 1; b < entries; ++b)
            CHECK(vt[a] != vt[b]);
}

// Retail slot 0 (kDtor) is the compiler's destructor pair, read raw: [0] D1
// runs ~P once and frees nothing (retail flags 0); [1] D0 runs it and hands the
// block to the class-scope operator delete (flags 1). D1 is called on a probe
// placement-new'd into a buffer and never destroyed again (a stack probe would
// be destroyed twice).
template <class P>
void CheckStreamDtorPair() {
    static_assert(kDtor == 0, "retail slot 0");
    alignas(P) unsigned char buf[sizeof(P)];
    P* p = ::new (static_cast<void*>(buf)) P();
    void* raw = static_cast<void*>(static_cast<IO::Stream*>(p));
    Reset(); dtor_check::ResetFrees();
    dtor_check::CallD1(raw);
    CHECK_EQ(g_lastMark, kDtor / 8);
    CHECK(g_lastSelf == raw);
    CHECK_EQ(dtor_check::g_frees, 0);
    P* h = dtor_check::NewOnMalloc<P>();
    void* rawH = static_cast<void*>(static_cast<IO::Stream*>(h));
    Reset(); dtor_check::ResetFrees();
    dtor_check::CallD0(rawH);
    CHECK_EQ(g_lastMark, kDtor / 8);
    CHECK(g_lastSelf == rawH);
    CHECK_EQ(dtor_check::g_frees, 1);
    CHECK(dtor_check::g_lastFree == rawH);
}

// ---------------------------------------------------------------------------
// The probes. Nine slots is the widest table in the family; the narrower
// classes simply declare fewer. B3X W1: retail slot 0 is each probe's own
// `~Probe() override { Note(this, 0); }` (written per probe), and the macro
// carries a recording class-scope operator delete (retail flags 1's free).
// ---------------------------------------------------------------------------
#define STREAM_PROBE_SEVEN                                                     \
    DTOR_CHECK_RECORDING_DELETE                                                \
    System::String GetSource() const override { Note(this, 1); return System::String(); } \
    bool     Write(int, const void*) override  { Note(this, 2); return true; } \
    bool     Read(int, void*) override         { Note(this, 3); return true; } \
    bool     SetCurrentPos(int) override       { Note(this, 4); return true; } \
    uint32_t GetCurrentPos() const override    { Note(this, 5); return 0; }    \
    uint32_t GetSize() const override          { Note(this, 6); return 0; }

struct CompressedStreamProbe final : IO::CompressedStream {
    STREAM_PROBE_SEVEN
    void Close() override     { Note(this, 7); }
    void PreCommit() override { Note(this, 8); }
    ~CompressedStreamProbe() override { Note(this, 0); }
};

struct CompressedMemoryStreamProbe final : IO::CompressedMemoryStream {
    STREAM_PROBE_SEVEN
    void Close() override     { Note(this, 7); }
    void PreCommit() override { Note(this, 8); }
    ~CompressedMemoryStreamProbe() override { Note(this, 0); }
};

struct CompressedFileStreamProbe final : IO::CompressedFileStream {
    STREAM_PROBE_SEVEN
    void Close() override     { Note(this, 7); }
    void PreCommit() override { Note(this, 8); }
    ~CompressedFileStreamProbe() override { Note(this, 0); }
};

UNIT_TEST(the_real_CompressedStream_has_9_slots_Close_at_7_PreCommit_at_8) {
    CompressedStreamProbe p;
    void* raw = static_cast<void*>(static_cast<IO::Stream*>(&p));

    CheckSharedStreamSlots(raw);
    CheckCompressedTail(raw);
    CheckDistinctSlots(raw, 9);
    CheckStreamDtorPair<CompressedStreamProbe>();

    // Slot 1 by elimination, and confirmed to be a virtual at all by an
    // ordinary C++ call through the base.
    Reset();
    { const IO::Stream& s = p; (void)s.GetSource(); }
    CHECK_EQ(g_lastMark, kGetSource / 8);

    // Nine, not eight: this is the branch that adds PreCommit. The FileStream
    // branch stops at eight and gives slot 7 a DIFFERENT meaning. B3X W1: the
    // family's retail slot 0 is a virtual destructor (D1 + D0).
    static_assert(std::has_virtual_destructor<IO::CompressedStream>::value,
                  "IO::CompressedStream: retail slot 0 is a virtual destructor");
    static_assert(std::has_virtual_destructor<IO::CompressedMemoryStream>::value,
                  "IO::CompressedMemoryStream: retail slot 0 is a virtual destructor");
    static_assert(std::has_virtual_destructor<IO::CompressedFileStream>::value,
                  "IO::CompressedFileStream: retail slot 0 is a virtual destructor");
}

UNIT_TEST(the_two_compressed_subclasses_keep_the_9_slot_order) {
    CompressedMemoryStreamProbe cms;
    void* rawCms = static_cast<void*>(static_cast<IO::Stream*>(&cms));
    CheckSharedStreamSlots(rawCms);
    CheckCompressedTail(rawCms);
    CheckDistinctSlots(rawCms, 9);
    CheckStreamDtorPair<CompressedMemoryStreamProbe>();

    CompressedFileStreamProbe cfs;
    void* rawCfs = static_cast<void*>(static_cast<IO::Stream*>(&cfs));
    CheckSharedStreamSlots(rawCfs);
    CheckCompressedTail(rawCfs);
    CheckDistinctSlots(rawCfs, 9);
    CheckStreamDtorPair<CompressedFileStreamProbe>();

    // `this` is unadjusted at every level of the chain, which every raw slot
    // caller in runtime/src assumes when it hands the same pointer to any of
    // the three types.
    CHECK(static_cast<void*>(static_cast<IO::Stream*>(&cms)) ==
          static_cast<void*>(static_cast<IO::CompressedStream*>(&cms)));
    CHECK(static_cast<void*>(static_cast<IO::CompressedStream*>(&cfs)) ==
          static_cast<void*>(static_cast<IO::CompressedFileStream*>(&cfs)));
}

UNIT_TEST(the_embedded_stream_at_0x68_carries_its_own_vtable) {
    // B34_RULES section 2 finding 6, now expressed on the real classes: a
    // Compressed*Stream holds a CONCRETE stream BY VALUE at +0x68, and that
    // member is a separate sub-object with its OWN vptr. A raw slot call on the
    // member dispatches through MemoryStream's eight-slot table, not through
    // the enclosing object's nine-slot one — which is exactly what
    // ?Close@CompressedMemoryStream does at 0x1800a9647.
    CompressedMemoryStreamProbe cms;
    void* const outer  = static_cast<void*>(static_cast<IO::Stream*>(&cms));
    void* const member = static_cast<void*>(
        static_cast<IO::Stream*>(&cms._memoryStream));

    CHECK_EQ((int)(static_cast<char*>(member) - static_cast<char*>(outer)), 0x68);
    // TWO DIFFERENT TABLES IN ONE OBJECT. If these ever compared equal the
    // member would dispatch through the enclosing class's nine slots and
    // `_memoryStream.Close()` would reach CompressedMemoryStream::Close —
    // unbounded recursion on the save path.
    CHECK(*reinterpret_cast<void**>(outer) != *reinterpret_cast<void**>(member));
    CHECK(*reinterpret_cast<void**>(member) != nullptr);

    CompressedFileStreamProbe cfs;
    void* const fOuter  = static_cast<void*>(static_cast<IO::Stream*>(&cfs));
    void* const fMember = static_cast<void*>(
        static_cast<IO::Stream*>(&cfs._file));
    CHECK_EQ((int)(static_cast<char*>(fMember) - static_cast<char*>(fOuter)), 0x68);
    CHECK(*reinterpret_cast<void**>(fOuter) != *reinterpret_cast<void**>(fMember));

    // ?Close@CompressedFileStream reads the FileInfo at this+0x88, which is
    // +0x68 (the member) + 0x20 (its FileInfo). The member body now spells that
    // as `_file._file`; this is the arithmetic it replaced.
    CHECK_EQ((int)(offsetof(IO::CompressedFileStream, _file) +
                   offsetof(IO::FileStream, _file)), 0x88);
}

UNIT_TEST(the_compressed_classes_are_layout_identical_to_their_PODs) {
    static_assert(sizeof(IO::CompressedStream) == sizeof(RtIo::CompressedStream), "POD");
    static_assert(sizeof(IO::CompressedMemoryStream) ==
                  sizeof(RtIo::CompressedMemoryStream), "POD");
    static_assert(sizeof(IO::CompressedFileStream) ==
                  sizeof(RtIo::CompressedFileStream), "POD");

    // A field write through the CLASS is visible through the POD at the same
    // bytes — the property every reinterpret_cast in src/engine/io relies on.
    CompressedStreamProbe p;
    p._position   = 0x111;
    p._headerSize = 0x222;
    p._direction  = 1;
    const RtIo::CompressedStream* pod =
        reinterpret_cast<const RtIo::CompressedStream*>(
            static_cast<IO::CompressedStream*>(&p));
    CHECK_EQ((int)pod->position,   0x111);
    CHECK_EQ((int)pod->headerSize, 0x222);
    CHECK_EQ((int)pod->base.direction, 1);
}

// ===========================================================================
// ENDGAME B.3.4 STREAMS (4/6) — IO::TextFileStream and IO::VisitorStream.
// ===========================================================================

// The ten-slot table. Slot 7 is `Open(const Filename&)` — the FileStream branch
// meaning — and slot 8 is `OpenWithFormat`, TextFileStream's own overload of it.
// The probe overrides BOTH with distinct markers, which is the whole point:
// if the two ever swapped, a caller asking for the formatted open would get the
// plain one and silently read the file with the wrong codec.
struct TextFileStreamProbe final : IO::TextFileStream {
    // ENDGAME B.3.9 IO: the class's one constructor is retail's (type, name,
    // version); the host stub (io_stream_abi_stubs.cpp) makes its own stores.
    TextFileStreamProbe() : IO::TextFileStream(System::String(), System::String(), 0) {}
    STREAM_PROBE_SEVEN
    bool Open(const System::Filename&) override { Note(this, 7); return true; }
    bool OpenWithFormat(const System::Filename&, IO::Text::Format) override {
        Note(this, 8); return true;
    }
    bool Create(const System::Filename&, IO::Text::Format, IO::Text::Format) override {
        Note(this, 9); return true;
    }
    ~TextFileStreamProbe() override { Note(this, 0); }
};

struct VisitorStreamProbe final : IO::VisitorStream {
    STREAM_PROBE_SEVEN
    ~VisitorStreamProbe() override { Note(this, 0); }
};

UNIT_TEST(the_real_TextFileStream_has_10_slots_with_Open_at_7_and_OpenWithFormat_at_8) {
    TextFileStreamProbe p;
    void* raw = static_cast<void*>(static_cast<IO::Stream*>(&p));

    CheckSharedStreamSlots(raw);

    // Slot 7 is `Open`, NOT `Close`. This is the one place the family disagrees
    // with itself about what +0x38 means: on the memory / compressed branch it
    // is `Close()`, here it is `Open(const Filename&)`. Same offset, same table
    // shape, completely different behaviour, and nothing else diagnoses it.
    using OpenFn = bool (*)(void*, const void*);
    Reset();
    CHECK((SlotAt<OpenFn, IO::Stream>(raw, kSlot7)(raw, nullptr)) == true);
    CHECK_EQ(g_lastMark, 7);

    // Slot 8 is the class's OWN overload of that same name. On the compressed
    // branch this offset is `PreCommit()`.
    using OpenFmtFn = bool (*)(void*, const void*, int32_t);
    Reset();
    CHECK((SlotAt<OpenFmtFn, IO::Stream>(raw, kSlot8)(raw, nullptr, 3)) == true);
    CHECK_EQ(g_lastMark, 8);

    // Slot 9 — `Create(Filename const&, Format, Format)`. THREE arguments after
    // `this`, because the last two are both formats (the mangled name's
    // trailing `1` back-references `W4Format@Text@4@`, and retail stores both
    // as DWORDs at +0x68 and +0x6c).
    constexpr int kSlot9 = 0x48;
    using CreateFn = bool (*)(void*, const void*, int32_t, int32_t);
    Reset();
    CHECK((SlotAt<CreateFn, IO::Stream>(raw, kSlot9)(raw, nullptr, 6, 3)) == true);
    CHECK_EQ(g_lastMark, 9);

    CheckDistinctSlots(raw, 10);
    CheckStreamDtorPair<TextFileStreamProbe>();

    // Slot 1 by elimination (the file's banner explains why GetSource is never
    // raw-called), plus the ordinary virtual call that proves it is a virtual.
    Reset();
    { const IO::Stream& s = p; (void)s.GetSource(); }
    CHECK_EQ(g_lastMark, kGetSource / 8);

    static_assert(std::has_virtual_destructor<IO::TextFileStream>::value,
                  "IO::TextFileStream: retail slot 0 is a virtual destructor (B3X W1)");

    // `this` is unadjusted through the three-level chain, which every raw slot
    // caller in runtime/src assumes.
    CHECK(static_cast<void*>(static_cast<IO::Stream*>(&p)) ==
          static_cast<void*>(static_cast<IO::FileStream*>(&p)));
    CHECK(static_cast<void*>(static_cast<IO::FileStream*>(&p)) ==
          static_cast<void*>(static_cast<IO::TextFileStream*>(&p)));
}

UNIT_TEST(the_real_VisitorStream_has_exactly_7_slots_and_no_fields) {
    VisitorStreamProbe p;
    void* raw = static_cast<void*>(static_cast<IO::Stream*>(&p));

    CheckSharedStreamSlots(raw);
    CheckDistinctSlots(raw, 7);
    CheckStreamDtorPair<VisitorStreamProbe>();

    Reset();
    { const IO::Stream& s = p; (void)s.GetSource(); }
    CHECK_EQ(g_lastMark, kGetSource / 8);

    // SEVEN, not eight. VisitorStream is the bare IO::Stream shape: no Open, no
    // Close, no PreCommit. It adds no field either, so it is exactly IO::Stream
    // with five overrides — which is what makes it a NULL SINK rather than a
    // stream with a degenerate buffer.
    static_assert(sizeof(IO::VisitorStream) == sizeof(IO::Stream),
                  "IO::VisitorStream adds no field to IO::Stream");
    static_assert(std::has_virtual_destructor<IO::VisitorStream>::value,
                  "IO::VisitorStream: retail slot 0 is a virtual destructor (B3X W1)");

    CHECK(static_cast<void*>(static_cast<IO::Stream*>(&p)) ==
          static_cast<void*>(static_cast<IO::VisitorStream*>(&p)));
}

UNIT_TEST(TextFileStream_and_VisitorStream_are_layout_identical_to_their_PODs) {
    static_assert(sizeof(IO::TextFileStream) == sizeof(RtIo::TextFileStream), "POD");
    static_assert(sizeof(IO::VisitorStream)  == sizeof(RtIo::VisitorStream),  "POD");

    // THE NAME/TYPE PAIR IS THE OTHER WAY ROUND FROM THE OBVIOUS READING and it
    // is a coin flip nothing but a body that NAMES one of them settles:
    // ?GetObjectName@ is `LEA RAX,[RCX+0x58]` and ?GetObjectType@ is
    // `LEA RAX,[RCX+0x60]`, so the LOWER offset is the NAME.
    CHECK((int)offsetof(IO::TextFileStream, _objectName) <
          (int)offsetof(IO::TextFileStream, _objectType));

    // THE CLASS SAYS MORE THAN THE POD AT +0x78, and this is the pin for it:
    // the POD's single `void* bufferSizeAndCursor` is the class's `_bufSize` + `_bufCursor`
    // int32 pair, occupying the same eight bytes. `GetSize` returns +0x78 in
    // its buffer arm and the inherited FileInfo::size in its file arm, which is
    // what identifies +0x78 as a SIZE and not a pointer.
    CHECK_EQ((int)offsetof(IO::TextFileStream, _bufSize),
             (int)offsetof(RtIo::TextFileStream, bufferSizeAndCursor));
    CHECK_EQ((int)(sizeof(TextFileStreamProbe::_bufSize) +
                   sizeof(TextFileStreamProbe::_bufCursor)),
             (int)sizeof(RtIo::TextFileStream::bufferSizeAndCursor));

    // A field write through the CLASS is visible through the POD at the same
    // bytes — the property every reinterpret_cast in text_file_stream.cpp
    // now relies on, including the two `RtIO::TextFileStream*` bridges the
    // `Create` and `OpenWithFormat` members use to reach the FileStream doors.
    TextFileStreamProbe p;
    p._objectVersion   = 0x11;
    p._requestedFormat = IO::Text::Format::Detect;
    p._activeEncoding  = IO::Text::Format::Utf8Bom;
    p._bufSize         = 0x333;
    const RtIo::TextFileStream* pod =
        reinterpret_cast<const RtIo::TextFileStream*>(
            static_cast<IO::TextFileStream*>(&p));
    CHECK_EQ((int)pod->objectVersion,   0x11);
    CHECK_EQ((int)pod->requestedFormat, 6);
    CHECK_EQ((int)pod->activeEncoding,  3);
    CHECK_EQ((int)*reinterpret_cast<const int32_t*>(
                 reinterpret_cast<const char*>(pod) + 0x78), 0x333);
}

}  // namespace
