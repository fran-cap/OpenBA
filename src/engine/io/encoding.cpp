// ============================================================================
// src/engine/io/encoding.cpp — the IO::Encoding factory pair. The two
// IO::BinaryReader / IO::BinaryWriter constructors they call are in Serializer.h.
// ============================================================================
//
//   0x1800a8120  IO::Encoding::CreateReader   (UNEXPORTED — vtable slot +0x18)
//   0x1800a8080  IO::Encoding::CreateWriter   (UNEXPORTED — vtable slot +0x20)
//   0x1800a8340  ??0BinaryReader@IO@@QEAA@AEAVSerializer@1@H@Z   (EXPORTED)
//   0x1800a8a30  ??0BinaryWriter@IO@@QEAA@AEAVSerializer@1@H@Z   (EXPORTED)
//
// A CORRECTION TO THE INHERITED NOTES. Both constructors are EXPORTED, checked
// against the DLL's export directory by address, not assumed:
//     0x1800a8340 -> ??0BinaryReader@IO@@QEAA@AEAVSerializer@1@H@Z
//     0x1800a8a30 -> ??0BinaryWriter@IO@@QEAA@AEAVSerializer@1@H@Z
// "AEAVSerializer@1@H" is (Serializer&, int) and pins the signature that
// src/engine/io/Serializer.h had only inferred from the call site. The two
// FACTORIES are genuinely unexported (the same check returns nothing for either
// address).
//
// ----------------------------------------------------------------------------
// THE FACTORIES — transcribed from 0x1800a8120..0x1800a81b3 and
// 0x1800a8080..0x1800a8113, which are the SAME 52 bytes except for the CALL
// target. Details the decompiled C loses:
//
//   * "MOV EDX,0x30" / "LEA R8D,[RDX + -0x2c]" — the alignment argument is 4,
//     computed off the size register rather than written as a literal. So the
//     request is MemAlloc(0x30, 4), and 0x30 is where sizeof(BinaryCodec) comes
//     from.
//   * The allocation is EnterCriticalSection(_singleton+0x640) / CALL
//     0x1800c6e60 (?Allocate@MemoryAllocator@System@@QEAAPEAX_KH@Z) /
//     LeaveCriticalSection. That trio is
//     ?MemAlloc@MemoryAllocator@System@@SAPEAX_KH@Z (0x1800c7a50) INLINED —
//     the same identification src/engine/io/compressedstream.cpp makes for the
//     identical trio in the two stream constructors, and it is why calling the
//     exported MemAlloc here is byte-equivalent rather than merely similar.
//   * RCX — the IO::Encoding "this" — is NEVER READ. Both factories overwrite
//     it before first use. The encoding object is a pure policy tag with no
//     state; "self" is accepted and ignored here for exactly that reason, and
//     that is faithful, not an omission.
//   * On allocation failure the body does XOR EAX,EAX and returns NULL
//     WITHOUT constructing. Reproduced. Retail's callers do not check it; that
//     is retail's problem to have, and a guard here would hide it rather than
//     fix it.
//
// ----------------------------------------------------------------------------
// THE CONSTRUCTORS — and the reason this row was worth taking
// ----------------------------------------------------------------------------
// These are NOT plain field-stampers. Each one performs three STREAM
// OPERATIONS through serializer->stream's vtable before it returns, so
// constructing a BinaryReader CONSUMES 12 bytes of the stream and constructing
// a BinaryWriter EMITS 12 bytes. That is the stream header, and it is why
// SaveSettings' first dispatch after building its stream is this factory.
//
// Stream vtable slots used (RETAIL offsets, from src/engine/io/Stream.h,
// where they are read byte-wise out of ??_7Stream@IO@@6B@ at 0x1801549a0):
//     +0x10  Write(int size, const void* src) -> bool
//     +0x18  Read (int size, void* dst)       -> bool
//     +0x28  GetCurrentPos()                  -> int
//
// WRITER (0x1800a8a30), in order:
//     write 4 bytes of 0xAAAAAAAA            <- a fixed MAGIC, built on the
//                                               stack (MOV [RSP+0x50],
//                                               0xaaaaaaaa), never a field
//     _word20 = GetCurrentPos()              <- remembers where the next field
//                                               lands, for a later back-patch
//     write 4 bytes of _word24 (== -1)       <- the PLACEHOLDER, emitted from
//                                               the field just set to -1
//     _word24 = GetCurrentPos()
//     write 4 bytes of _version
//
// READER (0x1800a8340) is the mirror image, and the asymmetry is real:
//     read 4 bytes into a STACK SLOT that is never used   <- the magic is
//                                                            consumed and
//                                                            DISCARDED, not
//                                                            checked
//     read 4 bytes into _word20              <- the back-patched value
//     _word24 = GetCurrentPos()
//     read 4 bytes into _version             <- OVERWRITES the "version"
//                                               argument the caller passed
//
// THE READER'S "version" ARGUMENT IS DEAD ON EVERY SUCCESSFUL CALL. It is
// stored to +0x28 by MOV [RCX+0x28],R8D and then overwritten by the third
// Read. It survives only if that Read fails and leaves the destination alone —
// which is precisely why the store is still reproduced here instead of being
// "cleaned up" into a single assignment from the stream.
//
// TWO STORE-WIDTH DETAILS that the decompiled C prints identically and that are
// not identical:
//   * READER zeroes +0x20 and +0x24 with ONE 8-byte store
//     (MOV qword ptr [RCX+0x20],RAX, RAX just XORed). Writing the two u32
//     fields separately is byte-equivalent and is what is done here.
//   * WRITER writes them as TWO SEPARATE DWORDS (MOV dword [RCX+0x20],0 then
//     MOV dword [RCX+0x24],0xffffffff), because the values differ.
//
// Both constructors stamp ??_7ReaderWriter@IO@@6B@ FIRST and then overwrite it
// with the concrete table — the inlined ReaderWriter base constructor. Both
// stores are reproduced: they are two instructions in the shipped bytes and
// collapsing them would be a divergence for no gain.
//
// MOV RAX,RSI before the epilogue: MSVC constructors return "this", which is
// what makes the factory's "return Ctor(...)" produce the object pointer.
//
// ============================================================================

#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <new>

#include "io/Serializer.h"
#include "system/MemoryAllocator.h"

namespace RtIo {

namespace {

// ---------------------------------------------------------------------------
// ATTENDANCE COUNTERS — NOT A VERIFICATION
// ---------------------------------------------------------------------------
// These count how many times each factory was ENTERED. They compare nothing.
// ENDGAME B.3.9 IO: the two constructor counters went with the constructor
// doors -- the ctors are now `IO::BinaryReader` / `IO::BinaryWriter`'s own
// (src/engine/io/Serializer.h), reached only from these two factories, so
// their counts equalled CreateReader / CreateWriter's.
struct EncCounts {
    unsigned long long createReader = 0, createWriter = 0;
    ~EncCounts() {
        if ((createReader | createWriter) == 0) {
            printf("[Encoding] ATTENDANCE: neither factory was "
                        "entered in this run.\n");
            return;
        }
        printf("[Encoding] ATTENDANCE (entries, not comparisons): "
                    "CreateReader=%llu CreateWriter=%llu\n",
                    createReader, createWriter);
    }
};
EncCounts g_encCounts;

}  // namespace

// ---------------------------------------------------------------------------
// THE FIVE BINARY-ENCODING SLOTS ARE `IO::BinaryEncoding`'s METHODS.
// ---------------------------------------------------------------------------
// ENDGAME B.3.9 IO: every stream constructor now builds the encoding as the
// real class (`::new (p) IO::BinaryEncoding()`), so a live encoding wears the
// compiler's table and its five slots reach the members below directly. The
// five by-address slot doors (0x1800a81e0 / d0 / c0 / 0x1800a8120 / 0x1800a8080)
// had the minted unexported table 0x1801554a8 as their only caller; they were
// deleted with that row.
//
// Slots 0, 1 and 2 are inline in Serializer.h, transcribed from the shipped
// bytes (`MOV AL,1; RET` / `XOR AL,AL; RET` / `MOV EAX,2; RET`); every body
// ignores `this` -- an Encoding is a stateless policy tag.

}  // namespace RtIo

// ===========================================================================
// IO::BinaryEncoding — the two factory bodies.
// ===========================================================================
// Byte-for-byte what the two `extern "C"` doors held before B.3.4 block 1; only
// the enclosing declaration changed. `this` is accepted and ignored because
// retail's 0x1800a8120 / 0x1800a8080 overwrite RCX before first use.
namespace IO {

// MemAlloc(0x30, 4) -- the alignment is `LEA R8D,[RDX-0x2c]` off the size
// register -- then the real constructor in place. A failed allocation returns
// null unconstructed (retail's XOR EAX,EAX path, unguarded).
ReaderWriter* BinaryEncoding::CreateReader(Serializer& ser, int version) {
    ++RtIo::g_encCounts.createReader;
    void* p = ::System::MemoryAllocator::MemAlloc(sizeof(BinaryReader), 4);
    if (p == nullptr) return nullptr;
    return ::new (p) BinaryReader(ser, version);
}

ReaderWriter* BinaryEncoding::CreateWriter(Serializer& ser, int version) {
    ++RtIo::g_encCounts.createWriter;
    void* p = ::System::MemoryAllocator::MemAlloc(sizeof(BinaryWriter), 4);
    if (p == nullptr) return nullptr;
    return ::new (p) BinaryWriter(ser, version);
}

}  // namespace IO

// ENDGAME B.3.9 IO: nothing in this file is registered any more. The two
// constructor doors (??0BinaryReader 0x1800a8340, ??0BinaryWriter 0x1800a8a30)
// had the two factories above as their only callers and died with the minted
// ReaderWriter-family rows; the five encoding slot doors died with 0x1801554a8.
