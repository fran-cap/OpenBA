// ============================================================================
// tests/unit/io_vtable_abi_test.cpp — ENDGAME B.3.4: the IO classes
// (`IO::ReaderWriter`, `IO::Encoding`, `IO::Serializer`, the Stream family,
// `IO::PackageFile`) standing on their own as real C++ classes.
//
// Every test here drives a REAL class, or a probe DERIVED from one with marker
// bodies: the real `IO::Serializer`'s lifetime (PART E), the real vtable
// orders pinned slot by slot (PARTS F, G), the PackageFile shape, and
// `Serializer::Serialize(int64_t&)` landing on retail's +0x90. The old
// transcription probes (`TestReaderWriter`, `TestEncoding`, `TestSerializer`,
// the `Test*Stream` family) are gone: a transcription can only ever test
// itself. What survives of them is the file-scope pins on the shipped
// constant tables below, and `TestStream`, the fake stream PART E's
// `CountingStream` is built on.
//
// B3X W1 — THE NORMAL-VIRTUAL CUTOVER. `IO::ReaderWriter` and `IO::Stream`
// declare `virtual ~T()` for retail slot 0, which GCC spends TWO slots on (D1,
// D0): retail slot N >= 1 is compiler slot N + 1, and every raw read below
// keeps its retail byte offset and translates it through RetailByteOffset<T>.
// `IO::Encoding` — retail slot 0 `IsBinary`, no destructor slot — stays.
//
// THE RETAIL SLOT LISTS BEING PINNED
// ----------------------------------
// Read byte-wise out of the shipped Runtime-x64.dll's .rdata (image base
// 0x180000000) in Ghidra on 2026-09-18. Cited here, not re-derived; the full
// tables with every body address are in
// docs/history/endgame_b/B34_RULES.md §1.
//
//   ??_7ReaderWriter@IO@@6B@  @ 0x1801553b0 — 26 slots: [0] 0x180023bd0
//     (the deleting dtor), [1..25] all `_purecall` (0x18007ec28). 26*8 = 0xd0,
//     so the last slot's byte offset is 0xc8 == `rw_vtbl::kSerializeBytes`,
//     which is what bounds the table at 26 independently of adjacency.
//   The Binary `IO::Encoding` table @ 0x1801554a8 — FIVE slots and NO dtor
//   slot: [0] 0x1800a81e0 `MOV AL,1; RET`, [1] 0x1800a81d0 `XOR AL,AL; RET`,
//   [2] 0x1800a81c0 `MOV EAX,2; RET`, [3] 0x1800a8120 CreateReader,
//   [4] 0x1800a8080 CreateWriter. So slot 3 is at byte +0x18 and slot 4 at
//   +0x20 — `rt::io::encoding_vtbl::kCreateReader/kCreateWriter`.
//   ??_7Stream@IO@@6B@ @ 0x1801549a0 — 7 slots, [2..6] `_purecall`.
//   ??_7MemoryStream@IO@@6B@ @ 0x1801555b0 — 8 slots ([7] Close 0x1800ae9e0).
//
// WHAT IS *NOT* PROVEN HERE
// -------------------------
// Nothing about MSVC. The retail DLL is gone; every caller and callee is this
// same GCC. The binding binary facts are OBJECT LAYOUT and RETAIL SLOT ORDER,
// and slot order binds only because our own tables were transcribed from
// retail.
// ============================================================================
#include "unit.h"
#include "slot_check.h"
#include "dtor_check.h"

// The shipped constant tables. The tests index through THESE, not through
// local copies, so a test failure means the header and the ABI disagree.
#include "io/Serializer.h"
#include "io/Stream.h"
#include "io/FileStream.h"
#include "object/Internal.h"
#include "io/PackageFile.h"   // B.3.4 PACKAGE+FS

#include <cstdint>
#include <cstddef>
#include <type_traits>

namespace {

namespace rw_vtbl = rt::io::rw_vtbl;
namespace enc_vtbl = rt::io::encoding_vtbl;

// ENDGAME B.3.9 IO: `IO::ReaderWriter`'s only constructor is retail's inlined
// base ctor, which takes the owning Serializer. The probes below hand it this
// zeroed one (stream null); no probe reads the two fields it sets.
alignas(8) unsigned char g_probeSerBytes[16] = {};
IO::Serializer& ProbeSer() { return *reinterpret_cast<IO::Serializer*>(g_probeSerBytes); }

// ---------------------------------------------------------------------------
// Observation channel: every body below records WHICH one ran and on what
// `this`, so a test can assert the identity of the callee, not merely that
// something returned.
// ---------------------------------------------------------------------------
const void* g_lastSelf = nullptr;
int         g_lastMark = 0;
long long   g_lastArg  = 0;

void Note(const void* self, int mark, long long arg = 0) {
    g_lastSelf = self;
    g_lastMark = mark;
    g_lastArg  = arg;
}
void Reset() { g_lastSelf = nullptr; g_lastMark = 0; g_lastArg = 0; }

// The one instruction all the raw-offset idioms compile to. `byteOffset` is
// RETAIL's; T (the object's class) decides the compiler's.
template <typename Fn, class T>
Fn SlotAt(const void* obj, int byteOffset) {
    const uint8_t* const* vt = *reinterpret_cast<const uint8_t* const* const*>(obj);
    return reinterpret_cast<Fn>(*reinterpret_cast<void* const*>(
        reinterpret_cast<const uint8_t*>(vt) + RetailByteOffset<T>(byteOffset)));
}

// ===========================================================================
// The shipped constant tables, pinned at file scope.
// ===========================================================================

// `rw_vtbl` is a contiguous 8-byte ladder over exactly 26 retail slots. The
// three scalar offsets are build-specific: the PROFILE build's ReaderWriter
// vtable puts them at +0x90 / +0xb0 / +0xb8.
static_assert(rw_vtbl::kDtor == 0x00,           "slot 0");
static_assert(rw_vtbl::kSerializeBytes == 0xC8, "slot 25 — the table's last");
static_assert(rw_vtbl::kSerializeBytes / 8 == 25, "26 slots, 0..25");
static_assert(rw_vtbl::kScalarBool  == 0x78, "RETAIL bool  (profile build: 0x90)");
static_assert(rw_vtbl::kScalarUInt  == 0x98, "RETAIL uint  (profile build: 0xb0)");
static_assert(rw_vtbl::kScalarInt   == 0xA0, "RETAIL int   (profile build: 0xb8)");
static_assert(rw_vtbl::kScalarBool - rw_vtbl::kScalarChar == 8, "14 -> 15");
static_assert(rw_vtbl::kScalarInt  - rw_vtbl::kScalarUInt == 8, "19 -> 20");
static_assert(rw_vtbl::kSerializeBytes - rw_vtbl::kScalarSChar == 8, "24 -> 25");

// `IO::Encoding`: CreateReader at +0x18 and CreateWriter at +0x20.
static_assert(enc_vtbl::kCreateReader == 0x18, "Encoding slot 3");
static_assert(enc_vtbl::kCreateWriter == 0x20, "Encoding slot 4");

// The base is abstract (all five slots pure) and the three concretes are
// not — which is exactly what says every one of the five slots is
// overridden, with no gap left for the compiler to fill.
static_assert(std::is_abstract<IO::Encoding>::value, "IO::Encoding is the interface");
static_assert(!std::is_abstract<IO::BinaryEncoding>::value, "all five slots overridden");
static_assert(!std::is_abstract<IO::TextEncoding>::value, "all five slots overridden");
static_assert(!std::is_abstract<IO::VisitorEncoding>::value, "all five slots overridden");

// THE GUARD THAT MATTERS: no virtual destructor anywhere in the hierarchy.
// Itanium would spend two slots on one, pushing CreateReader from +0x18 to
// +0x28 and CreateWriter from +0x20 to +0x30 — and `IO::Serializer`'s
// constructor, which reads those two byte offsets RAW, would then call
// `GetKind()` as a factory and store the int 2 as a ReaderWriter*.
static_assert(!std::has_virtual_destructor<IO::Encoding>::value, "B34_RULES §1.1");
static_assert(!std::has_virtual_destructor<IO::BinaryEncoding>::value, "B34_RULES §1.1");
static_assert(!std::has_virtual_destructor<IO::TextEncoding>::value, "B34_RULES §1.1");
static_assert(!std::has_virtual_destructor<IO::VisitorEncoding>::value, "B34_RULES §1.1");

// A concrete type destructible at namespace scope is what lets encoding.cpp
// and encoding_visitor_text.cpp hold one shared instance for their address
// doors to delegate through with a qualified, non-virtual call.
static_assert(std::is_destructible<IO::BinaryEncoding>::value, "door instance");
static_assert(std::is_destructible<IO::TextEncoding>::value, "door instance");
static_assert(std::is_destructible<IO::VisitorEncoding>::value, "door instance");

// ===========================================================================
// The fake stream PART E's `CountingStream` is built on: `IO::Stream`'s shape
// (B3X W1: a virtual destructor, compiler slots 0/1; slot 1 GetSource has a
// real body; slots 2..6 are `_purecall` in the base) with recording bodies.
// ===========================================================================
class TestStream {
public:
    // ??_7Stream@IO@@6B@ @ 0x1801549a0, 7 slots. Slot 0 is the deleting dtor
    // (0x18000c610, also RT_DELDTOR(..., "IO::Stream")) — B3X W1: a virtual
    // destructor, compiler slots 0/1; slot 1 GetSource has a real body
    // (0x18000c6a0); slots 2..6 are `_purecall` in the base.
    virtual ~TestStream() { Note(this, 400); }
    virtual const char* GetSource() const            { Note(this, 401); return "stream"; }
    virtual bool        Write(int n, const void*)    { Note(this, 402, n); return true; }
    virtual bool        Read(int n, void*)           { Note(this, 403, n); return true; }
    virtual void        SetCurrentPos(uint32_t p)    { Note(this, 404, p); }
    virtual uint32_t    GetCurrentPos() const        { Note(this, 405); return 0; }
    virtual uint32_t    GetSize() const              { Note(this, 406); return 0; }

    int32_t   direction;   // +0x08
    uint32_t  _pad0c;      // +0x0C  never written by any ctor
    void*     encoding;    // +0x10  IO::Encoding*, owned; freed with MemFree
};

// ===========================================================================
// PART E — ENDGAME B.3.4 block 3. The REAL `IO::Serializer`'s LIFETIME.
//
// THIS DRIVES THE REAL CLASS, NOT A MIRROR. The retired `TestSerializer` was a
// transcription, and a transcription can only ever test itself: it would keep
// passing while the shipped constructor drifted. `IO::Serializer`'s member
// bodies therefore live in a REGISTRY-FREE translation unit
// (src/engine/io/serializer_class.cpp, in tests/unit/sources.cmake) so that this
// file links the shipped code, exactly as B.3.3 did with type_reflection.cpp.
//
// WHAT IT PINS, and why each clause is load-bearing:
//
//   * Constructing consumes (reading) or emits (writing) a 12-BYTE STREAM
//     HEADER — `??0BinaryReader@IO@@` (0x1800a8340) calls the stream's Read
//     three times with count 4 and GetCurrentPos once; `??0BinaryWriter@IO@@`
//     (0x1800a8a30) calls Write three times with count 4 and GetCurrentPos
//     twice. EXACTLY ONCE each, on exactly one of the two factory slots.
//   * Destroying reads or back-patches FOUR MORE BYTES through the
//     ReaderWriter's slot 0 with flags == 1. EXACTLY ONCE.
//   * So `~Serializer` IS NOT IDEMPOTENT and construction is not free. The
//     double-construction hazard is asserted as a COUNTED FACT below rather
//     than left as a comment: two constructions into one buffer really do
//     consume two headers, and the file is desynced from that point on.
//
// The caller shape mirrored here is B34_RULES §7.3's: placement-new into an
// `alignas(8) uint8_t buf[24]` with an explicit destructor at the same
// position. That is the shape the 187 exe-side ctor sites convert to, the shape
// src/engine/object/internal_class.cpp's `ResetPersistState` now uses, and the
// shape the registered `RtSerializer_Ctor` door itself uses.
// ===========================================================================

namespace lifetime {

struct Counters {
    int readCalls, readBytes;
    int writeCalls, writeBytes;
    int getPosCalls, setPosCalls;
    int createReader, createWriter;
    int tailReads, tailWrites, deletingDtors;
    int lastVersion;
    int frees;                 // TailReaderWriter::operator delete (retail flags & 1)
    const void* lastFree;
};
Counters g_c{};
void ResetCounters() { g_c = Counters{}; }

// The stream. Slot order is `??_7Stream@IO@@6B@`'s: [2] Write +0x10,
// [3] Read +0x18, [4] SetCurrentPos +0x20, [5] GetCurrentPos +0x28. The
// Serializer's own constructor never calls a stream slot — it only reads the
// POD fields — so every count below is the ReaderWriter's doing, which is
// precisely the thing whose position must not move.
struct CountingStream final : TestStream {
    ~CountingStream() = default;
    bool Write(int n, const void*) override { ++g_c.writeCalls; g_c.writeBytes += n; return true; }
    bool Read (int n, void*)       override { ++g_c.readCalls;  g_c.readBytes  += n; return true; }
    void SetCurrentPos(uint32_t)   override { ++g_c.setPosCalls; }
    uint32_t GetCurrentPos() const override { ++g_c.getPosCalls; return 0; }
};

// A CONCRETE subclass of the REAL `IO::ReaderWriter` — every one of the 25
// non-destructor retail slots overridden, because the base declares them pure.
// Only retail slot 0 has behaviour here: it is the 4-byte tail that
// `~Serializer` triggers. B3X W1: `~Serializer` is `delete rw` (retail's
// slot-0 call with `MOV EDX,1`), so the tail is this destructor, and flags 1's
// meaning — "then free `this`" — is the class-scope operator delete receiving
// `this`. The objects live in HeaderEncoding's raw storage, so that operator
// delete records and frees nothing, and nothing else ever destroys them.
struct TailReaderWriter final : IO::ReaderWriter {
    TailReaderWriter(CountingStream* s, bool w) : IO::ReaderWriter(ProbeSer()), src(s), writing(w) {}
    CountingStream* src = nullptr;
    bool writing = false;

    ~TailReaderWriter() override {
        ++g_c.deletingDtors;
        if (writing) { src->Write(4, nullptr); ++g_c.tailWrites; }
        else         { src->Read (4, nullptr); ++g_c.tailReads;  }
    }
    static void operator delete(void* p) { ++g_c.frees; g_c.lastFree = p; }
    int  GetVersion() const override { return 0; }
    void Skip() override {}
    void SerializeCharArray  (int, char*,          const char*) override {}
    void SerializeBoolArray  (int, bool*,          const char*) override {}
    void SerializeFloatArray (int, float*,         const char*) override {}
    void SerializeUInt64Array(int, uint64_t*,      const char*) override {}
    void SerializeInt64Array (int, int64_t*,       const char*) override {}
    void SerializeUIntArray  (int, uint32_t*,      const char*) override {}
    void SerializeIntArray   (int, int32_t*,       const char*) override {}
    void SerializeUShortArray(int, uint16_t*,      const char*) override {}
    void SerializeShortArray (int, int16_t*,       const char*) override {}
    void SerializeUCharArray (int, unsigned char*, const char*) override {}
    void SerializeSCharArray (int, signed char*,   const char*) override {}
    void SerializeChar  (char&,          const char*) override {}
    void SerializeBool  (bool&,          const char*) override {}
    void SerializeFloat (float&,         const char*) override {}
    void SerializeUInt64(uint64_t&,      const char*) override {}
    void SerializeInt64 (int64_t&,       const char*) override {}
    void SerializeUInt  (uint32_t&,      const char*) override {}
    void SerializeInt   (int32_t&,       const char*) override {}
    void SerializeUShort(uint16_t&,      const char*) override {}
    void SerializeShort (int16_t&,       const char*) override {}
    void SerializeUChar (unsigned char&, const char*) override {}
    void SerializeSChar (signed char&,   const char*) override {}
    void SerializeBytes(int, void*, const char*) override {}
};

// A CONCRETE subclass of the REAL `IO::Encoding`, whose two factory bodies
// reproduce the retail codec constructors' STREAM TRAFFIC and nothing else.
// Three fixed ReaderWriter slots so a second construction gets its own. They
// are RAW storage: the factory placement-news a TailReaderWriter into one and
// `~Serializer`'s `delete` destroys it, so nothing is ever destroyed twice.
struct HeaderEncoding final : IO::Encoding {
    CountingStream* src = nullptr;
    alignas(TailReaderWriter) unsigned char rw[3][sizeof(TailReaderWriter)];
    int handed = 0;
    void* Slot(int i) { return static_cast<void*>(rw[i]); }

    bool IsBinary() const override { return true; }
    bool IsText() const override { return false; }
    int  GetKind() const override { return 2; }

    IO::ReaderWriter* CreateReader(IO::Serializer&, int version) override {
        ++g_c.createReader; g_c.lastVersion = version;
        src->Read(4, nullptr); src->Read(4, nullptr); src->Read(4, nullptr);  // 12 bytes
        src->GetCurrentPos();
        return ::new (Slot(handed++ % 3)) TailReaderWriter(src, false);
    }
    IO::ReaderWriter* CreateWriter(IO::Serializer&, int version) override {
        ++g_c.createWriter; g_c.lastVersion = version;
        src->Write(4, nullptr); src->Write(4, nullptr); src->Write(4, nullptr); // 12 bytes
        src->GetCurrentPos(); src->GetCurrentPos();
        return ::new (Slot(handed++ % 3)) TailReaderWriter(src, true);
    }
    ~HeaderEncoding() = default;
};

// Wire a `CountingStream` up as the POD the (still unconverted) `IO::Stream`
// is today, and hand it out as the class the constructor's signature names.
// This pair of casts is precisely what block 4 deletes.
IO::Stream& AsIoStream(CountingStream& cs, HeaderEncoding& enc, int32_t direction) {
    TestStream* base = static_cast<TestStream*>(&cs);
    base->direction = direction;
    base->encoding  = static_cast<void*>(static_cast<IO::Encoding*>(&enc));
    enc.src = &cs;
    return *reinterpret_cast<IO::Stream*>(base);
}

}  // namespace lifetime

UNIT_TEST(the_real_Serializer_consumes_one_12_byte_header_and_one_4_byte_tail) {
    using namespace lifetime;

    // ---- READING: direction 0 takes CreateReader (encoding slot 3, +0x18) --
    {
        CountingStream cs;
        HeaderEncoding enc;
        IO::Stream& s = AsIoStream(cs, enc, rt::io::kDirectionReading);

        ResetCounters();
        alignas(8) uint8_t buf[24];      // the exe-side caller shape, §7.3
        IO::Serializer* ser = ::new (static_cast<void*>(buf)) IO::Serializer(s, 17);

        // Exactly ONE factory call, and it was the READ one.
        CHECK_EQ(g_c.createReader, 1);
        CHECK_EQ(g_c.createWriter, 0);
        CHECK_EQ(g_c.lastVersion, 17);   // arg 3 reached the factory
        // Exactly TWELVE bytes, in three 4-byte reads, and nothing written.
        CHECK_EQ(g_c.readCalls, 3);
        CHECK_EQ(g_c.readBytes, 12);
        CHECK_EQ(g_c.getPosCalls, 1);
        CHECK_EQ(g_c.writeCalls, 0);
        // The object is wired the way retail wires it.
        CHECK(ser->_stream == &s);
        CHECK(static_cast<void*>(ser->_readerWriter) == enc.Slot(0));
        CHECK(static_cast<void*>(ser) == static_cast<void*>(buf));

        // ---- the tail, on destruction, exactly once --------------------
        ser->~Serializer();
        CHECK_EQ(g_c.deletingDtors, 1);
        CHECK_EQ(g_c.frees, 1);          // retail's flags 1: then free `this`
        CHECK(g_c.lastFree == enc.Slot(0));
        CHECK_EQ(g_c.tailReads, 1);
        CHECK_EQ(g_c.readCalls, 4);      // 3 header + 1 tail
        CHECK_EQ(g_c.readBytes, 16);     // 12 + 4
        CHECK_EQ(g_c.tailWrites, 0);
    }

    // ---- WRITING: any non-zero direction takes CreateWriter (+0x20) --------
    {
        CountingStream cs;
        HeaderEncoding enc;
        IO::Stream& s = AsIoStream(cs, enc, 1);

        ResetCounters();
        alignas(8) uint8_t buf[24];
        IO::Serializer* ser = ::new (static_cast<void*>(buf)) IO::Serializer(s, 18);

        CHECK_EQ(g_c.createWriter, 1);
        CHECK_EQ(g_c.createReader, 0);
        CHECK_EQ(g_c.lastVersion, 18);
        CHECK_EQ(g_c.writeCalls, 3);
        CHECK_EQ(g_c.writeBytes, 12);
        CHECK_EQ(g_c.getPosCalls, 2);    // the writer records a back-patch pos
        CHECK_EQ(g_c.readCalls, 0);

        ser->~Serializer();
        CHECK_EQ(g_c.deletingDtors, 1);
        CHECK_EQ(g_c.frees, 1);
        CHECK_EQ(g_c.tailWrites, 1);
        CHECK_EQ(g_c.writeCalls, 4);
        CHECK_EQ(g_c.writeBytes, 16);
    }
}

UNIT_TEST(constructing_a_Serializer_twice_consumes_two_headers) {
    using namespace lifetime;

    // THE HAZARD, AS A COUNTED FACT rather than a comment in a rule sheet.
    // This is why §7.3 forbids `x.~Serializer()` on a TYPED local (the implicit
    // destructor at scope exit would be a second one) and why the caller
    // conversion is placement-new into a RAW buffer, which has no implicit
    // destructor of its own. Nothing here is a bug being demonstrated — it is
    // the measurement that says a second construction is not free, so a
    // conversion that accidentally doubles one shifts every record after it.
    CountingStream cs;
    HeaderEncoding enc;
    IO::Stream& s = AsIoStream(cs, enc, rt::io::kDirectionReading);

    ResetCounters();
    alignas(8) uint8_t buf[24];

    IO::Serializer* a = ::new (static_cast<void*>(buf)) IO::Serializer(s, 3);
    CHECK_EQ(g_c.readBytes, 12);

    // Same bytes, second construction. The stream has now given up 24.
    IO::Serializer* b = ::new (static_cast<void*>(buf)) IO::Serializer(s, 3);
    CHECK_EQ(g_c.createReader, 2);
    CHECK_EQ(g_c.readCalls, 6);
    CHECK_EQ(g_c.readBytes, 24);      // <- the desync, measured
    CHECK(a == b);                    // into the same storage, at one address

    // The first ReaderWriter is now unreachable and its 4-byte tail will never
    // be consumed: one destruction cannot undo two constructions. Faithful, and
    // exactly why the destructor is not idempotent.
    b->~Serializer();
    CHECK_EQ(g_c.deletingDtors, 1);
    CHECK_EQ(g_c.frees, 1);
    CHECK(g_c.lastFree == enc.Slot(1));   // the second construction's reader
    CHECK_EQ(g_c.readBytes, 28);      // 24 + one tail, NOT 24 + two
}

// ===========================================================================
// PART F — ENDGAME B.3.4 block 3. PIN THE REAL CLASSES' VTABLE ORDER.
//
// WHY THIS TEST HAS TO EXIST. Nothing else in the tree exercises the real
// `IO::ReaderWriter`/`IO::Encoding` vtable ORDER: every registered door calls
// qualified or non-virtually, and every live object wears a MINTED table out of
// `standalone/rt_data_mint_tables.inc`. So a mis-ordered declaration in
// src/engine/io/Serializer.h would stay invisible right through B.3.4 and
// surface only when B.3 steps 5-8 retire the minted tables — as a corrupted
// save file, with no recent change to blame. The order was checked by eye at
// the block-2 close and is right. This is so it STAYS right.
//
// The retired transcription probes proved the ABI rule but not the shipped
// declaration. These probe the SHIPPED classes: a concrete subclass of each, every slot overridden with its
// own marker, then a raw call at every `rw_vtbl::k*` / `encoding_vtbl::k*` byte
// offset asserting that the marker equals offset/8.
//
// Two compiler facts, so the next reader does not fight them:
//   * `IO::ReaderWriter` is abstract, so the probe must be a SUBCLASS (it is);
//     since B3X W1 its destructor is public and virtual (retail slot 0 = D1 +
//     D0), and every raw offset below is translated by RetailByteOffset.
//   * `IO::BinaryReader`/`BinaryWriter`/`Visitor` are `final` (concrete since
//     ENDGAME B.3.9 IO; tests/unit/b39_io_classes_test.cpp builds them), so
//     they cannot be subclassed into a probe.
//     The BASE is the right thing to probe anyway: it is the table whose order
//     the 54 `rw_vtbl::` files depend on.
// ===========================================================================

namespace order {

struct OrderProbe final : IO::ReaderWriter {
    OrderProbe() : IO::ReaderWriter(ProbeSer()) {}
    ~OrderProbe() override { Note(this, 0); }   // retail slot 0 (kDtor)
    DTOR_CHECK_RECORDING_DELETE
    int  GetVersion() const override { Note(this, 1); return 0; }
    void Skip() override { Note(this, 2); }
    void SerializeCharArray  (int, char*,          const char*) override { Note(this, 3); }
    void SerializeBoolArray  (int, bool*,          const char*) override { Note(this, 4); }
    void SerializeFloatArray (int, float*,         const char*) override { Note(this, 5); }
    void SerializeUInt64Array(int, uint64_t*,      const char*) override { Note(this, 6); }
    void SerializeInt64Array (int, int64_t*,       const char*) override { Note(this, 7); }
    void SerializeUIntArray  (int, uint32_t*,      const char*) override { Note(this, 8); }
    void SerializeIntArray   (int, int32_t*,       const char*) override { Note(this, 9); }
    void SerializeUShortArray(int, uint16_t*,      const char*) override { Note(this, 10); }
    void SerializeShortArray (int, int16_t*,       const char*) override { Note(this, 11); }
    void SerializeUCharArray (int, unsigned char*, const char*) override { Note(this, 12); }
    void SerializeSCharArray (int, signed char*,   const char*) override { Note(this, 13); }
    void SerializeChar  (char&,          const char*) override { Note(this, 14); }
    void SerializeBool  (bool&,          const char*) override { Note(this, 15); }
    void SerializeFloat (float&,         const char*) override { Note(this, 16); }
    void SerializeUInt64(uint64_t&,      const char*) override { Note(this, 17); }
    void SerializeInt64 (int64_t&,       const char*) override { Note(this, 18); }
    void SerializeUInt  (uint32_t&,      const char*) override { Note(this, 19); }
    void SerializeInt   (int32_t&,       const char*) override { Note(this, 20); }
    void SerializeUShort(uint16_t&,      const char*) override { Note(this, 21); }
    void SerializeShort (int16_t&,       const char*) override { Note(this, 22); }
    void SerializeUChar (unsigned char&, const char*) override { Note(this, 23); }
    void SerializeSChar (signed char&,   const char*) override { Note(this, 24); }
    void SerializeBytes(int, void*, const char*) override { Note(this, 25); }
};

struct EncOrderProbe final : IO::Encoding {
    bool IsBinary() const override { Note(this, 0); return true; }
    bool IsText() const override { Note(this, 1); return false; }
    int  GetKind() const override { Note(this, 2); return 2; }
    IO::ReaderWriter* CreateReader(IO::Serializer&, int) override { Note(this, 3); return nullptr; }
    IO::ReaderWriter* CreateWriter(IO::Serializer&, int) override { Note(this, 4); return nullptr; }
    ~EncOrderProbe() = default;
};

}  // namespace order

UNIT_TEST(the_real_ReaderWriter_declares_its_26_slots_in_retail_order) {
    order::OrderProbe p;
    void* raw = static_cast<void*>(static_cast<IO::ReaderWriter*>(&p));

    // The three singular shapes first. Retail slot 0 (kDtor) is the compiler's
    // pair: D0 (flags 1, what `~Serializer` calls) on a heap probe, D1 (flags
    // 0) on a placement-new'd one — never on the stack `p`, which scope exit
    // destroys.
    {
        order::OrderProbe* h = dtor_check::NewOnMalloc<order::OrderProbe>();
        void* rawH = static_cast<void*>(static_cast<IO::ReaderWriter*>(h));
        Reset(); dtor_check::ResetFrees();
        dtor_check::CallD0(rawH);
        CHECK_EQ(g_lastMark, rw_vtbl::kDtor / 8);
        CHECK(g_lastSelf == rawH);                  // `this` unadjusted
        CHECK_EQ(dtor_check::g_frees, 1);
        CHECK(dtor_check::g_lastFree == rawH);
        alignas(order::OrderProbe) unsigned char buf[sizeof(order::OrderProbe)];
        order::OrderProbe* once = ::new (static_cast<void*>(buf)) order::OrderProbe();
        Reset(); dtor_check::ResetFrees();
        dtor_check::CallD1(static_cast<IO::ReaderWriter*>(once));
        CHECK_EQ(g_lastMark, rw_vtbl::kDtor / 8);
        CHECK_EQ(dtor_check::g_frees, 0);
    }

    Reset();
    using IntFn = int (*)(void*);
    SlotAt<IntFn, order::OrderProbe>(raw, rw_vtbl::kGetVersion)(raw);
    CHECK_EQ(g_lastMark, rw_vtbl::kGetVersion / 8);

    Reset();
    using VoidFn = void (*)(void*);
    SlotAt<VoidFn, order::OrderProbe>(raw, rw_vtbl::kSkip)(raw);
    CHECK_EQ(g_lastMark, rw_vtbl::kSkip / 8);

    // The eleven ARRAY slots, (int, T*, const char*) — one shape, eleven
    // offsets, each asserted to land on the override declared at that index.
    const int arrayOffsets[11] = {
        rw_vtbl::kArrayChar,   rw_vtbl::kArrayBool,   rw_vtbl::kArrayFloat,
        rw_vtbl::kArrayUInt64, rw_vtbl::kArrayInt64,  rw_vtbl::kArrayUInt,
        rw_vtbl::kArrayInt,    rw_vtbl::kArrayUShort, rw_vtbl::kArrayShort,
        rw_vtbl::kArrayUChar,  rw_vtbl::kArraySChar,
    };
    uint8_t scratch[8] = {0};
    using ArrFn = void (*)(void*, int, void*, const char*);
    for (int i = 0; i < 11; ++i) {
        Reset();
        SlotAt<ArrFn, order::OrderProbe>(raw, arrayOffsets[i])(raw, 1, scratch, "n");
        CHECK_EQ(g_lastMark, arrayOffsets[i] / 8);
        CHECK_EQ(g_lastMark, 3 + i);          // contiguous, slots 3..13
    }

    // The eleven SCALAR slots, (T&, const char*).
    const int scalarOffsets[11] = {
        rw_vtbl::kScalarChar,   rw_vtbl::kScalarBool,   rw_vtbl::kScalarFloat,
        rw_vtbl::kScalarUInt64, rw_vtbl::kScalarInt64,  rw_vtbl::kScalarUInt,
        rw_vtbl::kScalarInt,    rw_vtbl::kScalarUShort, rw_vtbl::kScalarShort,
        rw_vtbl::kScalarUChar,  rw_vtbl::kScalarSChar,
    };
    using ScaFn = void (*)(void*, void*, const char*);
    for (int i = 0; i < 11; ++i) {
        Reset();
        SlotAt<ScaFn, order::OrderProbe>(raw, scalarOffsets[i])(raw, scratch, "n");
        CHECK_EQ(g_lastMark, scalarOffsets[i] / 8);
        CHECK_EQ(g_lastMark, 14 + i);         // contiguous, slots 14..24
    }

    Reset();
    SlotAt<ArrFn, order::OrderProbe>(raw, rw_vtbl::kSerializeBytes)(raw, 8, scratch, "b");
    CHECK_EQ(g_lastMark, rw_vtbl::kSerializeBytes / 8);
    CHECK_EQ(g_lastMark, 25);                 // the last slot in the table

    // 26 distinct retail slots = 27 compiler entries (D1 != D0 included), on
    // the REAL class this time — no slot shared, and in particular no pair of
    // same-named virtuals collapsed into one.
    void* const* vt = *reinterpret_cast<void* const* const*>(raw);
    CHECK(vt[0] != vt[1]);
    for (int a = 0; a < 27; ++a)
        for (int b = a + 1; b < 27; ++b)
            CHECK(vt[a] != vt[b]);

    // B3X W1: a virtual destructor throughout the shipped hierarchy — two
    // Itanium slots for retail slot 0, every offset above one slot on.
    static_assert(std::has_virtual_destructor<IO::ReaderWriter>::value, "B3X W1");
    static_assert(std::has_virtual_destructor<IO::BinaryReader>::value, "B3X W1");
    static_assert(std::has_virtual_destructor<IO::BinaryWriter>::value, "B3X W1");
    static_assert(std::has_virtual_destructor<IO::Visitor>::value,      "B3X W1");
}

UNIT_TEST(the_real_Encoding_declares_its_5_slots_in_retail_order) {
    order::EncOrderProbe e;
    void* raw = static_cast<void*>(static_cast<IO::Encoding*>(&e));

    // Slot 0 is `IsBinary`, NOT a destructor. This is the assertion that keeps
    // `IO::Serializer`'s constructor calling a FACTORY at +0x18/+0x20 rather
    // than `GetKind()` — which would build-clean, link-clean store the int 2 as
    // a ReaderWriter* and corrupt every save.
    using BoolFn = bool (*)(void*);
    using IntFn  = int (*)(void*);
    Reset(); CHECK((SlotAt<BoolFn, order::EncOrderProbe>(raw, 0x00)(raw)) == true);  CHECK_EQ(g_lastMark, 0);
    Reset(); CHECK((SlotAt<BoolFn, order::EncOrderProbe>(raw, 0x08)(raw)) == false); CHECK_EQ(g_lastMark, 1);
    Reset(); CHECK_EQ((SlotAt<IntFn, order::EncOrderProbe>(raw, 0x10)(raw)), 2);     CHECK_EQ(g_lastMark, 2);

    // The two factories, at the byte offsets the Serializer constructor reads
    // RAW, with retail's arity 3.
    int marker = 0;
    using MakeRw = void* (*)(void*, void*, int);
    Reset();
    CHECK((SlotAt<MakeRw, order::EncOrderProbe>(raw, enc_vtbl::kCreateReader)(raw, &marker, 11)) == nullptr);
    CHECK_EQ(g_lastMark, enc_vtbl::kCreateReader / 8);
    CHECK_EQ(g_lastMark, 3);
    Reset();
    CHECK((SlotAt<MakeRw, order::EncOrderProbe>(raw, enc_vtbl::kCreateWriter)(raw, &marker, 12)) == nullptr);
    CHECK_EQ(g_lastMark, enc_vtbl::kCreateWriter / 8);
    CHECK_EQ(g_lastMark, 4);

    // Five distinct entries and no more: the table is bounded at 5 in retail by
    // the adjacency ladder (0x1801554a8 + 0x28 == 0x1801554d0).
    void* const* vt = *reinterpret_cast<void* const* const*>(raw);
    for (int a = 0; a < 5; ++a)
        for (int b = a + 1; b < 5; ++b)
            CHECK(vt[a] != vt[b]);

    static_assert(!std::has_virtual_destructor<IO::Encoding>::value, "B34_RULES 1.1");
}

// ===========================================================================
// PART G — ENDGAME B.3.4 block 4 (STREAMS). PIN THE REAL STREAM CLASSES'
// VTABLE ORDER, and their layout against BOTH duplicate PODs.
//
// Same argument as PART F, one family over. Every live stream in this build
// wears a MINTED table out of `standalone/rt_data_mint_tables.inc` and every
// registered door calls QUALIFIED, so nothing else compiles a real
// `IO::Stream` vtable at all. A slot declared one line out of place in
// src/engine/io/Stream.h or FileStream.h would stay invisible until B.3
// steps 5-8 retire the minted rows, and then surface as a stream that reads
// through `Close` or seeks through `Open`.
//
// HOW SLOT 1 IS PINNED WITHOUT CALLING IT. `GetSource` returns a
// `System::String` BY VALUE, so it carries a hidden return pointer, and the
// registered door (`RtIo_Stream_GetSource`, iobase_vtables.cpp) spells that
// slot in MSVC's argument order while a C++ member compiled here uses the
// compiler's. Raw-calling it from the test would be asserting a calling
// convention, not a slot order. So slot 1 is pinned BY ELIMINATION instead:
// every OTHER slot is raw-called and lands on its own marker, and all the
// entries in the table are asserted distinct. With n distinct entries and n-1
// of them pinned, the remaining one is at the remaining index. That is a
// complete proof of the order and it depends on no convention at all.
// ===========================================================================

namespace streamorder {

// Byte offsets, spelled once. `IO::Stream` is 7 slots; the concrete classes
// append theirs (B34_RULES section 1.3).
constexpr int kDtor          = 0x00;
constexpr int kGetSource     = 0x08;
constexpr int kWrite         = 0x10;
constexpr int kRead          = 0x18;
constexpr int kSetCurrentPos = 0x20;
constexpr int kGetCurrentPos = 0x28;
constexpr int kGetSize       = 0x30;
constexpr int kSlot7         = 0x38;   // Close on MemoryStream, Open on FileStream

struct StreamProbe final : IO::Stream {
    DTOR_CHECK_RECORDING_DELETE
    System::String GetSource() const override { Note(this, 1); return System::String(); }
    bool     Write(int, const void*) override  { Note(this, 2); return true; }
    bool     Read(int, void*) override         { Note(this, 3); return true; }
    bool     SetCurrentPos(int) override       { Note(this, 4); return true; }
    uint32_t GetCurrentPos() const override    { Note(this, 5); return 0; }
    uint32_t GetSize() const override          { Note(this, 6); return 0; }
    ~StreamProbe() override { Note(this, 0); }   // retail slot 0 (B3X W1)
};

struct MemoryStreamProbe final : IO::MemoryStream {
    DTOR_CHECK_RECORDING_DELETE
    System::String GetSource() const override { Note(this, 1); return System::String(); }
    bool     Write(int, const void*) override  { Note(this, 2); return true; }
    bool     Read(int, void*) override         { Note(this, 3); return true; }
    bool     SetCurrentPos(int) override       { Note(this, 4); return true; }
    uint32_t GetCurrentPos() const override    { Note(this, 5); return 0; }
    uint32_t GetSize() const override          { Note(this, 6); return 0; }
    void     Close() override                  { Note(this, 7); }
    ~MemoryStreamProbe() override { Note(this, 0); }
};

// The six slots every Stream in the family shares, raw-called at their retail
// byte offsets. `raw` is the object; each CHECK says the entry at that offset
// is the override declared at offset/8, with `this` unadjusted.
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
// block to the class-scope operator delete (flags 1). D1 on a probe
// placement-new'd into a buffer, never destroyed again.
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

}  // namespace streamorder

UNIT_TEST(the_real_Stream_declares_its_7_slots_in_retail_order) {
    streamorder::StreamProbe p;
    void* raw = static_cast<void*>(static_cast<IO::Stream*>(&p));

    streamorder::CheckSharedStreamSlots(raw);
    streamorder::CheckDistinctSlots(raw, 7);
    streamorder::CheckStreamDtorPair<streamorder::StreamProbe>();

    // Slot 1 by elimination (see the banner), and confirmed to be a virtual at
    // all by an ordinary C++ call through the base.
    Reset();
    { const IO::Stream& s = p; (void)s.GetSource(); }
    CHECK_EQ(g_lastMark, streamorder::kGetSource / 8);

    // B3X W1: a virtual destructor. Itanium spends TWO slots on it, which puts
    // `Write` (retail slot 2) at compiler slot 3; the product reaches every
    // slot by name, and the raw reads here translate through RetailByteOffset.
    static_assert(std::has_virtual_destructor<IO::Stream>::value,
                  "IO::Stream: retail slot 0 is a virtual destructor");
    static_assert(std::has_virtual_destructor<IO::MemoryStream>::value,
                  "IO::MemoryStream: retail slot 0 is a virtual destructor");
}

UNIT_TEST(the_real_MemoryStream_adds_Close_at_slot_7_and_no_PreCommit) {
    streamorder::MemoryStreamProbe p;
    void* raw = static_cast<void*>(static_cast<IO::Stream*>(&p));

    streamorder::CheckSharedStreamSlots(raw);

    // Slot 7 is `Close` (0x1800ae9e0) and MemoryStream adds NOTHING after it —
    // the nine-slot shape with `PreCommit` at +0x40 belongs to the Compressed
    // branch. The same +0x38 means `Open` on the FileStream branch.
    Reset();
    using VoidFn = void (*)(void*);
    SlotAt<VoidFn, IO::Stream>(raw, streamorder::kSlot7)(raw);
    CHECK_EQ(g_lastMark, streamorder::kSlot7 / 8);
    CHECK_EQ(g_lastMark, 7);

    streamorder::CheckDistinctSlots(raw, 8);
    streamorder::CheckStreamDtorPair<streamorder::MemoryStreamProbe>();

    Reset();
    { const IO::Stream& s = p; (void)s.GetSource(); }
    CHECK_EQ(g_lastMark, 1);

    // `this` is unadjusted through the single-inheritance chain: the raw slot
    // callers all over runtime/src hand the same pointer to base and derived.
    CHECK(static_cast<void*>(static_cast<IO::Stream*>(&p)) ==
          static_cast<void*>(static_cast<IO::MemoryStream*>(&p)));
}

UNIT_TEST(the_real_Stream_family_is_layout_identical_to_both_duplicate_PODs) {
    // B34_RULES section 1.5.2: there are TWO `Stream` PODs, declared either
    // side of the Wave-4 lane boundary in two namespaces, and neither header
    // includes the other. `IO::Stream` replaces both, and both stay alive and
    // trivially destructible until their last user converts. If any of the
    // three ever drifts, this fails here rather than in a save file.
    static_assert(sizeof(IO::Stream) == sizeof(RtIo::Stream), "RtIo POD agrees");
    static_assert(sizeof(IO::Stream) == sizeof(rt::io::Stream), "rt::io POD agrees");
    static_assert(sizeof(IO::MemoryStream) == sizeof(RtIo::MemoryStream), "POD agrees");

    // A field write through the CLASS is visible through BOTH PODs at the same
    // bytes — the property every `reinterpret_cast` in src/engine/io relies on.
    streamorder::MemoryStreamProbe p;
    p._cursor = 0x1234;
    p._size   = 0x5678;
    p._direction = 1;
    const RtIo::MemoryStream* a =
        reinterpret_cast<const RtIo::MemoryStream*>(static_cast<IO::MemoryStream*>(&p));
    const rt::io::Stream* b =
        reinterpret_cast<const rt::io::Stream*>(static_cast<IO::Stream*>(&p));
    CHECK_EQ((int)a->cursor, 0x1234);
    CHECK_EQ((int)a->size,   0x5678);
    CHECK_EQ((int)a->base.direction, 1);
    CHECK_EQ((int)b->direction, 1);
}

// ---------------------------------------------------------------------------
// The FileStream branch, where slot 7 means something ELSE.
// ---------------------------------------------------------------------------
namespace streamorder {

struct FileStreamProbe final : IO::FileStream {
    DTOR_CHECK_RECORDING_DELETE
    System::String GetSource() const override { Note(this, 1); return System::String(); }
    bool     Write(int, const void*) override  { Note(this, 2); return true; }
    bool     Read(int, void*) override         { Note(this, 3); return true; }
    bool     SetCurrentPos(int) override       { Note(this, 4); return true; }
    uint32_t GetCurrentPos() const override    { Note(this, 5); return 0; }
    uint32_t GetSize() const override          { Note(this, 6); return 0; }
    bool     Open(const System::Filename&) override { Note(this, 7); return true; }
    ~FileStreamProbe() override { Note(this, 0); }
};

// BinaryFileStream differs from FileStream in retail slot 0 ONLY (B3X W1: the
// D1/D0 pair), so probing it is probing that claim: the same eight overrides
// must land at the same eight offsets through a table one class further down.
struct BinaryFileStreamProbe final : IO::BinaryFileStream {
    DTOR_CHECK_RECORDING_DELETE
    System::String GetSource() const override { Note(this, 1); return System::String(); }
    bool     Write(int, const void*) override  { Note(this, 2); return true; }
    bool     Read(int, void*) override         { Note(this, 3); return true; }
    bool     SetCurrentPos(int) override       { Note(this, 4); return true; }
    uint32_t GetCurrentPos() const override    { Note(this, 5); return 0; }
    uint32_t GetSize() const override          { Note(this, 6); return 0; }
    bool     Open(const System::Filename&) override { Note(this, 7); return true; }
    ~BinaryFileStreamProbe() override { Note(this, 0); }
};

}  // namespace streamorder

UNIT_TEST(the_real_FileStream_puts_Open_at_slot_7_not_Close) {
    streamorder::FileStreamProbe p;
    void* raw = static_cast<void*>(static_cast<IO::Stream*>(&p));

    streamorder::CheckSharedStreamSlots(raw);

    // THE TRAP IN THE FAMILY, asserted rather than commented. `IO::Stream`
    // declares seven virtuals, so slot 7 is whatever the derived class declares
    // FIRST: `Close()` on the MemoryStream/Compressed branch, `Open(Filename)`
    // here. A port that dispatched +0x38 on a FileStream expecting `Close`
    // would call `Open` — same offset, same table shape, and nothing else in
    // the build would diagnose it.
    Reset();
    using OpenFn = bool (*)(void*, const void*);
    System::Filename fn;
    CHECK((SlotAt<OpenFn, IO::Stream>(raw, streamorder::kSlot7)(raw, &fn)) == true);
    CHECK_EQ(g_lastMark, streamorder::kSlot7 / 8);
    CHECK_EQ(g_lastMark, 7);

    streamorder::CheckDistinctSlots(raw, 8);
    streamorder::CheckStreamDtorPair<streamorder::FileStreamProbe>();

    Reset();
    { const IO::Stream& s = p; (void)s.GetSource(); }
    CHECK_EQ(g_lastMark, 1);

    // FileStream's slot-7 override takes a Filename and MemoryStream's takes
    // nothing: the two branches genuinely disagree about +0x38, and the type
    // system now carries that rather than a comment.
    static_assert(std::has_virtual_destructor<IO::FileStream>::value,
                  "IO::FileStream: retail slot 0 is a virtual destructor (B3X W1)");
    static_assert(std::has_virtual_destructor<IO::BinaryFileStream>::value,
                  "IO::BinaryFileStream: retail slot 0 is a virtual destructor (B3X W1)");
}

UNIT_TEST(BinaryFileStream_differs_from_FileStream_in_slot_0_only) {
    streamorder::FileStreamProbe fs;
    streamorder::BinaryFileStreamProbe bfs;
    void* rawBfs = static_cast<void*>(static_cast<IO::Stream*>(&bfs));

    // All eight slots land where FileStream's do.
    streamorder::CheckSharedStreamSlots(rawBfs);
    Reset();
    using OpenFn = bool (*)(void*, const void*);
    System::Filename fn;
    SlotAt<OpenFn, IO::Stream>(rawBfs, streamorder::kSlot7)(rawBfs, &fn);
    CHECK_EQ(g_lastMark, 7);
    streamorder::CheckDistinctSlots(rawBfs, 8);
    streamorder::CheckStreamDtorPair<streamorder::BinaryFileStreamProbe>();

    // The class adds NO state — which is the whole content of the retail fact
    // that `??_7BinaryFileStream@` and `??_7FileStream@` are byte-identical in
    // slots 1..7 and differ only at slot 0.
    static_assert(sizeof(IO::BinaryFileStream) == sizeof(IO::FileStream),
                  "BinaryFileStream exists to swap slot 0, not to add a field");

    // `this` is unadjusted two levels down, which is what every raw slot caller
    // in runtime/src assumes when it hands the same pointer to either type.
    CHECK(static_cast<void*>(static_cast<IO::Stream*>(&bfs)) ==
          static_cast<void*>(static_cast<IO::FileStream*>(&bfs)));
    CHECK(static_cast<void*>(static_cast<IO::FileStream*>(&bfs)) ==
          static_cast<void*>(static_cast<IO::BinaryFileStream*>(&bfs)));

    // And the two probes' tables are DIFFERENT objects, so the test is not
    // accidentally reading one table twice.
    void* const* vtFs  = *reinterpret_cast<void* const* const*>(
        static_cast<void*>(static_cast<IO::Stream*>(&fs)));
    void* const* vtBfs = *reinterpret_cast<void* const* const*>(rawBfs);
    CHECK(vtFs != vtBfs);
}

UNIT_TEST(the_FileStream_pair_is_layout_identical_to_its_PODs) {
    static_assert(sizeof(IO::FileStream) == sizeof(RtIo::FileStream), "POD agrees");
    static_assert(sizeof(IO::BinaryFileStream) == sizeof(RtIo::BinaryFileStream),
                  "POD agrees");
    // The three offsets FileStream.h pins THROUGH the embedded FileInfo, which
    // is what identified it at +0x20 in the first place.
    CHECK_EQ((int)(offsetof(IO::FileStream, _file) +
                   offsetof(RtIO::FileInfo, cursor)), 0x2c);
    CHECK_EQ((int)(offsetof(IO::FileStream, _file) +
                   offsetof(RtIO::FileInfo, size)), 0x28);
    CHECK_EQ((int)(offsetof(IO::FileStream, _file) +
                   offsetof(RtIO::FileInfo, winHandle)), 0x38);

    streamorder::BinaryFileStreamProbe p;
    p._filename._data = nullptr;
    p._file.cursor = 0x99;
    const RtIo::BinaryFileStream* pod =
        reinterpret_cast<const RtIo::BinaryFileStream*>(
            static_cast<IO::BinaryFileStream*>(&p));
    CHECK_EQ((int)pod->base.file.cursor, 0x99);
    CHECK(pod->base.filename == nullptr);
}

// ===========================================================================
// ENDGAME B.3.4 PACKAGE+FS — `IO::PackageFile`: 0xf0 bytes and NINE slots
// ===========================================================================
//
// Two things need pinning and they need pinning in two different ways.
//
// THE LAYOUT is pinned on the REAL CLASS. `sizeof` and `offsetof` odr-use
// nothing, so `src/engine/io/PackageFile.h` can be included here and its own
// static_asserts fire at compile time; the CHECKs below re-state the load-bearing
// ones as a running test so a failure names the field.
//
// THE SLOT ORDER is pinned on a SHAPE-IDENTICAL PROBE, and that is a deliberate
// compromise with a reason, not a shortcut. GCC emits a class's vtable in the
// translation unit defining its KEY FUNCTION — the first non-pure, non-inline
// virtual — and for `IO::PackageFile` that is `TypeName()`, whose body is in
// `src/engine/io/package_file.cpp`, a file that stands on `RT_DATA_SLOT` and
// the port registry and therefore cannot link into the unit build (`tools/build.sh unit`). Referring to
// the real class's vtable here would drag that whole TU in. This is the SAME
// constraint `tests/unit/sources.cmake` already records for `Object::ExternalList`
// ("external_class_doors.cpp is deliberately NOT here and must not be added …
// it is also where ExternalList's vtable is emitted").
//
// So the probe reproduces the exact shape the claim is about — `Object::External`
// (the REAL class, which does link, via external_class.cpp) plus EXACTLY ONE
// added virtual — and asserts that the added virtual lands at index 8. That is
// precisely the proposition B34_BRIEFS section E.2 left open and this block
// closes; the four pieces of RETAIL evidence behind it are in
// src/engine/io/PackageFile.h's banner, and this test is what keeps the
// DECLARATION from drifting away from them.

namespace {

int g_pkgMark = -1;
void PkgNote(int slot) { g_pkgMark = slot; }

// A CONCRETE subclass of the REAL `Object::External`, declaring its nine slots
// in `IO::PackageFile`'s exact order: External's eight, then Construct.
struct PkgOrderProbe : Object::External {
    // retail slot 0 (B3X W1: the D1/D0 pair) records; the operator delete is
    // retail flags 1's free, recorded.
    DTOR_CHECK_RECORDING_DELETE
    const char*   TypeName() const override         { PkgNote(1); return "P"; }
    uint32_t      Type() const override             { PkgNote(2); return 0x50u; }
    int32_t       GetFileVersion() const override   { PkgNote(3); return 5; }
    CompileResult Compile(const System::Filename&) override { PkgNote(4); return 1; }
    bool          Serialize(IO::Stream&) override         { PkgNote(5); return true; }
    Object::External* Reconstruct() override        { PkgNote(6); return this; }
    void          Destruct() override               { PkgNote(7); }
    virtual Object::External* Construct()           { PkgNote(8); return this; }
    ~PkgOrderProbe() override { PkgNote(0); }
};

// `index` is RETAIL's slot; T (the object's class) decides the compiler's.
template <typename Fn, class T>
Fn PkgSlot(const void* obj, int index) {
    void* const* vt = *reinterpret_cast<void* const* const*>(obj);
    return reinterpret_cast<Fn>(vt[RetailSlot<T>(index)]);
}

}  // namespace

UNIT_TEST(package_file_adds_exactly_one_virtual_at_slot_8_over_External) {
    PkgOrderProbe probe;

    // Raw slot calls at every retail index 0..8, each asserting its own
    // marker. A mis-ordered declaration in PackageFile.h would land a call on
    // the wrong body and the marker would disagree with the index. Retail
    // index 0 is the D1/D0 pair (B3X W1): D1 on a placement-new'd probe (the
    // stack `probe` must not be destroyed twice), D0 on a heap one.
    {
        alignas(PkgOrderProbe) unsigned char buf[sizeof(PkgOrderProbe)];
        PkgOrderProbe* once = ::new (static_cast<void*>(buf)) PkgOrderProbe();
        g_pkgMark = -1; dtor_check::ResetFrees();
        PkgSlot<void (*)(void*), PkgOrderProbe>(once, 0)(once);
        CHECK_EQ(g_pkgMark, 0);
        CHECK_EQ(dtor_check::g_frees, 0);
        PkgOrderProbe* heap = dtor_check::NewOnMalloc<PkgOrderProbe>();
        g_pkgMark = -1; dtor_check::ResetFrees();
        dtor_check::CallD0(heap);
        CHECK_EQ(g_pkgMark, 0);
        CHECK_EQ(dtor_check::g_frees, 1);
        CHECK(dtor_check::g_lastFree == heap);
    }
    g_pkgMark = -1;
    PkgSlot<const char* (*)(const void*), PkgOrderProbe>(&probe, 1)(&probe);
    CHECK_EQ(g_pkgMark, 1);
    g_pkgMark = -1;
    PkgSlot<uint32_t (*)(const void*), PkgOrderProbe>(&probe, 2)(&probe);
    CHECK_EQ(g_pkgMark, 2);
    g_pkgMark = -1;
    PkgSlot<int32_t (*)(const void*), PkgOrderProbe>(&probe, 3)(&probe);
    CHECK_EQ(g_pkgMark, 3);
    g_pkgMark = -1;
    PkgSlot<uint32_t (*)(void*, const void*), PkgOrderProbe>(&probe, 4)(&probe, nullptr);
    CHECK_EQ(g_pkgMark, 4);
    g_pkgMark = -1;
    PkgSlot<bool (*)(void*, void*), PkgOrderProbe>(&probe, 5)(&probe, nullptr);
    CHECK_EQ(g_pkgMark, 5);
    g_pkgMark = -1;
    PkgSlot<void* (*)(void*), PkgOrderProbe>(&probe, 6)(&probe);
    CHECK_EQ(g_pkgMark, 6);
    g_pkgMark = -1;
    PkgSlot<void (*)(void*), PkgOrderProbe>(&probe, 7)(&probe);
    CHECK_EQ(g_pkgMark, 7);

    // *** THE ONE THAT MATTERS. *** Retail's ??_7PackageFile@IO@@6B@ has its
    // `Construct` body (0x1800252f0) at +0x40, i.e. index 8, and
    // ??_7External@Object@@6B@ is only eight slots long. If `Construct` were
    // declared on an unmodelled class BETWEEN External and PackageFile, it
    // would still land at 8 here — but External's own table would then be nine
    // slots in retail, and it is not. So what this pins is the C++ side of the
    // claim: External plus one added virtual puts that virtual at index 8.
    g_pkgMark = -1;
    PkgSlot<void* (*)(void*), PkgOrderProbe>(&probe, 8)(&probe);
    CHECK_EQ(g_pkgMark, 8);

    // Nine DISTINCT retail slots = ten compiler entries and no more. B3X W1:
    // the virtual ~External spends two slots and shifts everything after it
    // by one, so retail's Construct (+0x40, index 8) is compiler index 9 —
    // which PkgSlot's RetailSlot translation asserted above.
    void* const* vt = *reinterpret_cast<void* const* const*>(&probe);
    CHECK(vt[0] != vt[1]);
    for (int a = 0; a < 10; ++a)
        for (int b = a + 1; b < 10; ++b)
            CHECK(vt[a] != vt[b]);
    static_assert(std::has_virtual_destructor<Object::External>::value,
                  "B3X W1: the virtual ~External pushes Construct from compiler slot 8 to 9");
    CHECK_EQ(RetailSlot<PkgOrderProbe>(8), 9L);
}

// ===========================================================================
// PART H — ENDGAME B.3.4b SYSTEM (5): `IO::Serializer::Serialize(int64_t&)`.
// ===========================================================================
// The 18th overload, `?Serialize@Serializer@IO@@QEAAXAEA_JPEBD@Z` @
// 0x180035000, ReaderWriter slot **+0x90** (`rw_vtbl::kScalarInt64`). It is the
// first scalar added since block 3, and the only one whose absence was ever
// load-bearing: `System::Time::Serialize` (retail 0x1800ca740) hand-inlined
// this very slot for the whole of B.3.4 because there was no member to call and
// the four-byte `int&` door (+0xA0) is a different slot AND a different width.
//
// WHAT THIS PINS THAT A BUILD CANNOT. The member body is
// `ScalarCall(_readerWriter, rw_vtbl::kScalarInt64, v, name)` — a RAW slot read
// at a byte offset. Nothing about `kScalarInt64 == 0x90` is checked by the type
// system, so a one-slot drift would compile, link, and quietly serialize eight
// bytes through +0x88 (`kScalarUInt64`) or +0x98 (`kScalarUInt`, FOUR bytes) —
// a save-format change with no build error. The probe drives the REAL member
// (`src/engine/io/serializer_class.cpp`, in `tests/unit/sources.cmake`) against a
// hand-built table of the shape a compiler ReaderWriter table has since B3X W1
// (27 entries: D1, D0, then retail slots 1..25 — the member dispatches BY NAME
// now, so retail +0x90 is compiler +0x98), with a DISTINCT body in every one of
// the four neighbouring slots, so landing on the wrong one is a failure and not
// a coincidence.
//
// It also pins the REFERENCE semantics: retail's `AEA_J` is an `int64&`, so the
// slot receives the caller's ADDRESS and the read side writes THROUGH it. The
// body below stores a value back, and the test reads it out of the caller's own
// variable.
namespace ser_int64 {

long long g_seen        = 0;     // what slot 0x90 was handed
int       g_calls90     = 0;
int       g_callsWrong  = 0;     // any of the three neighbours
const char* g_name      = nullptr;

void ScalarInt64(void* /*self*/, int64_t* v, const char* name) {
    ++g_calls90;
    g_seen = static_cast<long long>(*v);
    g_name = name;
    *v = 0x0102030405060708LL;   // the read side writes THROUGH the reference
}
void ScalarUInt64(void*, uint64_t*, const char*)  { ++g_callsWrong; }  // +0x88
void ScalarUInt  (void*, uint32_t*, const char*)  { ++g_callsWrong; }  // +0x98
void ScalarInt   (void*, int32_t*,  const char*)  { ++g_callsWrong; }  // +0xA0
void Nothing     (void*)                          { ++g_callsWrong; }

void* g_table[27] = {
    (void*)&Nothing,      // D1     retail +0x00 kDtor
    (void*)&Nothing,      // D0     retail +0x00 kDtor
    (void*)&Nothing,      // +0x08  kGetVersion
    (void*)&Nothing,      // +0x10  kSkip
    (void*)&Nothing,      // +0x18  kArrayChar
    (void*)&Nothing,      // +0x20  kArrayBool
    (void*)&Nothing,      // +0x28  kArrayFloat
    (void*)&Nothing,      // +0x30  kArrayUInt64
    (void*)&Nothing,      // +0x38  kArrayInt64
    (void*)&Nothing,      // +0x40  kArrayUInt
    (void*)&Nothing,      // +0x48  kArrayInt
    (void*)&Nothing,      // +0x50  kArrayUShort
    (void*)&Nothing,      // +0x58  kArrayShort
    (void*)&Nothing,      // +0x60  kArrayUChar
    (void*)&Nothing,      // +0x68  kArraySChar
    (void*)&Nothing,      // +0x70  kScalarChar
    (void*)&Nothing,      // +0x78  kScalarBool
    (void*)&Nothing,      // +0x80  kScalarFloat
    (void*)&ScalarUInt64, // +0x88  kScalarUInt64   <- the neighbour below
    (void*)&ScalarInt64,  // +0x90  kScalarInt64    <- THE ONE
    (void*)&ScalarUInt,   // +0x98  kScalarUInt     <- the neighbour above
    (void*)&ScalarInt,    // +0xA0  kScalarInt      <- the four-byte door
    (void*)&Nothing,      // +0xA8  kScalarUShort
    (void*)&Nothing,      // +0xB0  kScalarShort
    (void*)&Nothing,      // +0xB8  kScalarUChar
    (void*)&Nothing,      // +0xC0  kScalarSChar
    (void*)&Nothing,      // +0xC8  kSerializeBytes
};

// One vptr plus ReaderWriter's three fields — the storage a minted-table
// object occupies. Nothing constructs an `IO::ReaderWriter` here.
struct RwStorage {
    void*   vptr;
    uint8_t allowCustomSerialize;
    uint8_t _pad09[7];
    void*   serializer;
    void*   stream;
};
static_assert(sizeof(RwStorage) == 0x20, "ReaderWriter storage is 0x20 bytes");

}  // namespace ser_int64

UNIT_TEST(serializer_serialize_int64_lands_on_slot_0x90_and_round_trips) {
    using namespace ser_int64;

    static_assert(rt::io::rw_vtbl::kScalarInt64 == 0x90,
                  "the member body spells this constant; if it moves, the "
                  "eight bytes go through a different retail slot");

    RwStorage rw{};
    rw.vptr = static_cast<void*>(g_table);
    // The retail offset, translated: the entry the by-name call reads.
    static_assert(RetailByteOffset<IO::ReaderWriter>(rt::io::rw_vtbl::kScalarInt64) == 0x98,
                  "retail +0x90 is compiler +0x98 behind D1/D0");
    CHECK(g_table[RetailSlot<IO::ReaderWriter>(rt::io::rw_vtbl::kScalarInt64 / 8)] ==
          reinterpret_cast<void*>(&ScalarInt64));

    // The Serializer is NOT constructed: its constructor drives an Encoding
    // factory and consumes a 12-byte stream header (PART E), which has nothing
    // to do with this overload. A raw 16-byte buffer wearing the two public
    // fields is exactly the object the 861 caller sites hand these doors, and
    // a raw buffer has no implicit destructor, so nothing can run twice.
    alignas(8) uint8_t buf[16] = {};
    IO::Serializer* ser = reinterpret_cast<IO::Serializer*>(buf);
    ser->_stream       = nullptr;
    ser->_readerWriter = reinterpret_cast<IO::ReaderWriter*>(&rw);

    g_calls90 = 0;
    g_callsWrong = 0;
    g_seen = 0;
    g_name = nullptr;

    // This is `System::Time::Serialize`'s whole body since B.3.4b SYSTEM (5).
    int64_t ticks = 0x7fedcba987654321LL;
    ser->Serialize(ticks, "ticks");

    CHECK_EQ(g_calls90, 1);
    CHECK_EQ(g_callsWrong, 0);                       // not +0x88, +0x98 or +0xA0
    CHECK_EQ(g_seen, 0x7fedcba987654321LL);          // all EIGHT bytes arrived
    CHECK(g_name != nullptr);                        // `name` is forwarded
    CHECK_EQ(ticks, 0x0102030405060708LL);           // the read side wrote back
}

}  // namespace
