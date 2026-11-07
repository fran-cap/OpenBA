#pragma once
// ============================================================================
// src/engine/system/Compression.h — the real `System::Compression`
// ============================================================================
//
// ENDGAME B.3.4b SYSTEM (4b). The layout and the codec notes stay where they
// were written — `src/engine/system/compression.cpp`'s banner is 157 lines of
// instruction-by-instruction derivation of both halves of the codec and is NOT
// restated here. This header carries the SHAPE and the evidence for it.
//
// ============================================================================
// "IS IT A CLASS?" — YES, AND IT IS THE ONE 4b CLASS WITH A HEAP INSTANCE
// ============================================================================
// The test (B34B_RULES.md §1.2): MSVC emits a class-scoped mangling only for a
// member of a class of that name. Read out of `standalone/rt_export_addrs.inc`:
//
//   ??0Compression@System@@QEAA@XZ                   0x1800c41a0  QEAA  ctor
//   ?Compress@Compression@System@@QEAAHPEBXIPEAXI@Z  0x1800c4230  QEAA  member
//   ?Decompress@Compression@System@@SAHPEBXIPEAXI@Z  0x1800c4660  SA    static
//   ??1Compression@System@@QEAA@XZ                   0x1800c4170  QEAA  dtor
//
// A `QEAA` constructor and a `QEAA` member alongside an `SA` static: an
// instance class whose one static utility hangs off it. Not the
// `IO::TextCodec` shape (receiverless free functions, no class-scoped exported
// name, nothing landed).
//
// ============================================================================
// THE LAYOUT — 8 BYTES, ONE POINTER, AND WHERE THE OBJECT COMES FROM
// ============================================================================
// `??0Compression@System@@QEAA@XZ` is three statements:
//     EnterCS(_singleton+0x640); p = Allocate(_singleton, 0x40000, 4); LeaveCS;
//     *this = p;  memset(p, 0, 0x40000);
// so the object is `{ uint32_t* table; }` — ONE pointer at +0x00 — and the
// table is 0x40000 bytes = 0x10000 `uint32_t` buckets, which is exactly what
// the hash `(uint16)((dv >> 8) - 5*dv)` can index. `Compress`'s first act on
// `this` is `MOV RCX,[R11]` (0x1800c42c6), the same single dereference.
//
// **THE INSTANCE IS ALWAYS HEAP-ALLOCATED AND NEVER A C++ OBJECT TODAY.** Both
// construction sites — `src/engine/io/compressed_stream_write.cpp:629-633`
// (`CompressedStream::Create`) and `src/engine/save/saveload_meta.cpp:395-398`
// — do `MemAlloc(8, 4)` and then call the `??0` door BY NAME on those eight
// raw bytes, storing the ctor's return. Both files are outside this step's
// file set and neither changes: the door placement-news the class into the
// bytes they hand it and returns `self`, which is retail's `MOV RAX,RCX`.
//
// **THERE IS NO POD TO CROSS-ASSERT AGAINST**, which is why this header has a
// `sizeof` and an `offsetof` assert but no alias pair: before this commit the
// type was spelled `void*` at every door and `*static_cast<uint32_t**>(self)`
// inside the body. The class is the first name these eight bytes have had.
//
// ============================================================================
// WHAT IS DELIBERATELY ABSENT
// ============================================================================
// **NO DESTRUCTOR, AND THAT IS NOT AN OVERSIGHT.** `??1Compression@System@@`
// (0x1800c4170) is unported and stays unported — retail does not destroy a
// Compression through it on the live path at all. The teardown is INLINED into
// `?Release@CompressedStream@IO@@` (`compressed_stream_release.cpp:104-113`,
// retail 0x1800aa1f3..0x1800aa26f), which is already ported and registered and
// which frees the 0x40000 table and then the 8-byte object directly through
// `MemFree`. So the class stays TRIVIALLY DESTRUCTIBLE — asserted below — and
// a real destructor here would be a second, private opinion about a teardown
// that already happens somewhere else. `compression.cpp`'s registration block
// carries the full argument and the finding that this is NOT a
// cutover-required row.
//
// **NO `First` / `Next` / `Index`.** Retail's three private statics
// (0x180023e20 / 0x180023e30 / 0x180023e40) are unported, uncalled and
// undeclared. An uncalled port is an unverified port.
//
// ⚠ THE GREP HAZARD (B34_RULES §7.2) is armed for this name from this commit
// on: inside a `namespace System { ... }` block a bare `Compression` binds this
// class. There is no POD of the same name to confuse it with — the hazard here
// is only that `Compression` used to be a comment, and is now a type.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include <type_traits>

namespace System {

class Compression {
public:
    // The LZF-shaped format (compression.cpp): a control byte below kMaxLiteralRun
    // starts a literal run of control + 1 bytes (a full run's control is
    // kMaxLiteralRun - 1, written into the slot kMaxLiteralRun + 1 bytes back),
    // else a match; offsets stay below kMaxOffset, a match is at most
    // kMaxMatchLength long. The hash table is kHashTableBytes.
    static constexpr uint32_t kMaxLiteralRun  = 0x20;      // 32 literals per run
    static constexpr uint32_t kMaxOffset      = 0x2000;    // the 13-bit match window
    static constexpr uint32_t kMaxMatchLength = 0x108;     // 264
    static constexpr uint32_t kHashTableBytes = 0x40000;   // 65536 uint32 slots
    // ??0Compression@System@@QEAA@XZ                  retail 0x1800c41a0
    // Allocates the 0x40000-byte hash table through the MemoryAllocator door
    // and zeroes it. NOT null-checked: retail stores the result and memsets it
    // unconditionally, and an allocation failure here is an upstream fault to
    // find, not one to paper over.
    Compression();

    // ?Compress@Compression@System@@QEAAHPEBXIPEAXI@Z retail 0x1800c4230
    // LZ77 with a one-slot-per-bucket hash head table, producing exactly the
    // stream `Decompress` consumes. The encoder is the ONLY reason the class
    // has instance state; the table is useless across calls, which is why the
    // constructor zeroes it.
    int Compress(const void* src, uint32_t srcLen, void* dst, uint32_t dstCap);

    // ?Decompress@Compression@System@@SAHPEBXIPEAXI@Z retail 0x1800c4660
    // `SA` — a public STATIC member. It is pure, it calls nothing, and it
    // touches no instance state, which is what lets `compressed_stream_read.cpp`
    // reach it with no receiver at all (two `RT_EXPORT_ONCE` sites).
    static int Decompress(const void* src, uint32_t srcLen,
                          void* dst, uint32_t dstCap);

    // ---- the 8 bytes -------------------------------------------------------
    // PUBLIC for the same reason `System::Thread`'s handles are: the
    // unconverted tree reaches these bytes through a cast, and `offsetof` on
    // them has to be well formed. 0x10000 `uint32_t` buckets, each a byte
    // OFFSET from the source pointer (never an absolute address).
    uint32_t* _table;   // +0x00
};

static_assert(sizeof(Compression) == 8,
              "System::Compression is ONE pointer: both construction sites "
              "MemAlloc(8, 4) and hand those eight bytes to the ??0 door");
static_assert(offsetof(Compression, _table) == 0x00,
              "Compress reads the table with a single MOV RCX,[R11] at "
              "0x1800c42c6 -- the pointer is the whole object");
static_assert(!std::is_polymorphic<Compression>::value,
              "System::Compression must never acquire a vtable: retail has "
              "none, and a vptr would make the object 16 bytes against a "
              "MemAlloc(8, 4) that neither call site can be told about");
static_assert(std::is_standard_layout<Compression>::value,
              "the three doors reach it through a reinterpret_cast from void*");
static_assert(std::is_trivially_destructible<Compression>::value,
              "??1Compression@System@@ (0x1800c4170) is unported on purpose: "
              "the live teardown is inlined into IO::CompressedStream::Release, "
              "which frees the table and then the object through MemFree");

}  // namespace System
