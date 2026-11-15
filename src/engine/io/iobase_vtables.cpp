// ============================================================================
// src/engine/io/iobase_vtables.cpp — Wave 19 lane IOBASE, tranche 1
// ============================================================================
//
// The retail bodies behind four `??_7…@IO@@6B@` tables, plus the two
// `IO::VisitorStream` exports that ride with them. All but the five
// VisitorStream slot bodies are class members in Stream.h, Serializer.h and
// FileStream.h. Every one was
// read as DISASSEMBLY out of the shipped `Runtime-x64.dll` (`objdump -d`), not
// from Ghidra's C (reference_ghidra_c_output_vs_disassembly.md).
//
//   0x18000c6a0  ?GetSource@Stream@IO@@UEBA?AVString@System@@XZ   slot 1 of SIX
//   0x1800242a0  MemoryStream vector deleting dtor      slot 0, UNEXPORTED
//   0x1800a82f0  ??1BinaryReader@IO@@UEAA@XZ
//   0x180023a10  BinaryReader vector deleting dtor      slot 0, UNEXPORTED
//   0x1800bac90  ??1Visitor@IO@@UEAA@XZ
//   0x18002b710  Visitor vector deleting dtor           slot 0, UNEXPORTED
//   0x1800baff0  ?GetSize@VisitorStream@IO@@UEBAIXZ
//   0x1800bb000  ?GetCurrentPos@VisitorStream@IO@@UEBAIXZ
//   0x1800bb010  ?SetCurrentPos@VisitorStream@IO@@UEAA_NH@Z
//   0x1800bb020  ?Read@VisitorStream@IO@@UEAA_NHPEAX@Z
//   0x1800bb030  ?Write@VisitorStream@IO@@UEAA_NHPEBX@Z
//   0x1800bb040  ??1VisitorStream@IO@@UEAA@XZ
//   0x1800bb090  ??0VisitorStream@IO@@QEAA@XZ
//   0x18002bf80  VisitorStream vector deleting dtor     slot 0, UNEXPORTED
//
// ----------------------------------------------------------------------------
// 1. `?GetSource@Stream@IO@@` — ONE BODY, `IO::Stream::GetSource` (Stream.h)
// ----------------------------------------------------------------------------
//
// The shipped body, in full (0x18000c6a0):
//
//     mov  [rsp+0x10],rdx        ; the HIDDEN RETURN SLOT
//     sub  rsp,0x18
//     movq [rsp+8],-2            ; SEH frame marker, no semantics
//     xor  eax,eax
//     mov  [rsp],eax             ; dtor-state local, dead
//     mov  [rdx],rax             ; *ret = nullptr    <-- the whole function
//     movl [rsp],1               ; dtor-state local, dead
//     mov  rax,rdx
//     ret
//
// i.e. `String GetSource() const { return String(); }` — a default-constructed
// `System::String`, whose entire representation is one null qword. RDX is the
// hidden return slot for the by-value `String`, so the typedef below spells it
// (reference_hidden_retslot_wild_write.md: an fn_* typedef that omits the
// hidden slot is a wild write that still returns the right answer).
//
// It reads NOTHING from `this`. That is not an inference from a decompile —
// RCX is never touched in the nine instructions above.
//
// ----------------------------------------------------------------------------
// 2. THE FOUR VECTOR DELETING DESTRUCTORS ARE ONE SHAPE, AND I READ EACH LOOP
// ----------------------------------------------------------------------------
// `IO::BinaryWriter`'s slot 0 (0x180023b50, Serializer.h) is the exact twin of
// these, and Serializer.h's slot-0 note describes the shape. That is a written
// recipe from a good lane, which is
// precisely the thing reference_a_written_recipe_can_be_wrong.md is about — a
// prescribed pair of dtor ports would have swapped a body that destroys EVERY
// element for one that destroys only the first. So each of the four here was
// disassembled on its own, and so was the helper:
//
//   0x18007ec98 is `__ehvec_dtor(base, size, count, dtor)`. Its loop, read from
//   its own bytes, walks from the LAST element to the FIRST:
//       rbx = base + count*size ;  edi = count-1
//       while (edi >= 0) { rbx -= size; (*dtor)(rbx); --edi; }
//   It destroys every element.
//
// The common shape (all four, byte for byte):
//
//     if (flags & 2) {                               // delete[] form
//         count = *(int32_t*)((char*)this - 8);      // the array cookie
//         __ehvec_dtor(this, sizeof(T), count, ~T);
//         if (flags & 1) MemFree((char*)this - 8);
//         return (char*)this - 8;
//     }
//     ~T(this);                                      // INLINED in retail
//     if (flags & 1) MemFree(this);
//     return this;
//
// with `sizeof(T)` = 0x30 (MemoryStream), 0x30 (BinaryReader), 0x28 (Visitor),
// 0x18 (VisitorStream), matching each type header's `static_assert`.
//
// ENDGAME B.3.9 IO: all four are now the classes' own virtual destructors
// (Stream.h, Serializer.h, FileStream.h); the abort below and its helper are
// gone. The paragraph is kept as the record of what the arm used to be.
//
// **THE `flags & 2` ARM IS A NAMED ABORT, NOT A PORT — and that is a declared
// divergence.** ENC-2 took the same position for BinaryWriter and gave the
// reason for BinaryWriter only. Re-derived here per class rather than
// inherited:
//
//   * MemoryStream — every producer in the DLL and in this tree constructs one
//     by value on a stack frame or as a sub-object of CompressedMemoryStream
//     (+0x68). No array allocation exists.
//   * BinaryReader / Visitor — the only producers are IO::Encoding::CreateReader
//     (0x1800a8120) and the Visitor factory, both a bare `MemAlloc(0x30, 4)` /
//     `MemAlloc(0x28, 4)` that lays down NO array cookie.
//   * VisitorStream — its one caller, `?Add@ExternalFactory@Object@@`, stack-
//     constructs a single instance (src/engine/object/external_add.cpp:286).
//
// Reaching the arm means a wrong-typed pointer arrived or a producer exists
// that this lane did not find, and in EITHER case the eight bytes at `this-8`
// are heap metadata rather than a count — so continuing would loop an
// arbitrary number of times over arbitrary memory. Aborting with a message
// that says what would have to have happened is the honest disposition; a
// silent single-element destroy would be exactly the swapped body the memory
// above warns about. This is called out in
// docs/history/runtime_port/WAVE19_LANE_IOBASE.md as the one place in this
// file that is not byte-faithful.
//
// `0x180006a10`, the unexported free these bodies call, is byte-identical to
// the exported `?MemFree@MemoryAllocator@System@@SAXPEAX@Z` (0x1800c7ad0) —
// asserted by compressedstream.cpp and Serializer.h, and
// re-read here: both are `TEST RCX,RCX; JZ ret; …; Enter; Free(_singleton,p);
// Leave`, over the SAME allocator sub-object (`[0x1801fc718] + 0x640`). Using
// the exported name is exact, not merely equivalent.
//
// ----------------------------------------------------------------------------
// 3. ~BinaryReader CONSUMES FOUR BYTES, AND THAT IS THE MIRROR OF ~BinaryWriter
// ----------------------------------------------------------------------------
// 0x1800a82f0 in full:
//
//     mov  [rcx],??_7BinaryReader@IO@@6B@     ; restamp on dtor entry
//     mov  rcx,[rcx+0x18]                     ; the ReaderWriter's Stream*
//     mov  r8,[rsp+0x48]                      ; &a dead 4-byte stack local
//     mov  edx,4
//     call [rax+0x18]                         ; Stream::Read(4, &scratch)
//     mov  [rbx],??_7ReaderWriter@IO@@6B@     ; base-unwind on dtor exit
//
// Slot +0x18 is index 3 == `Read` on every table in the family (Stream.h:76).
// The four bytes are read and **discarded** — the destination is a stack slot
// nothing reads afterwards. That is the read-side mirror of ~BinaryWriter's
// `Write(0xBBBBBBBB)` trailer (Serializer.h, `kRecordTrailer`): the writer emits the
// trailer magic, the reader swallows it. The scratch buffer is reproduced as a
// real local because the Read genuinely stores through it.
//
// Retail INLINES ~BinaryReader into the deleting dtor at 0x180023a10; the
// out-of-line 0x1800a82f0 is the same instructions. Here both are the one
// `IO::BinaryReader::~BinaryReader` (Serializer.h).
//
// ~Visitor (0x1800bac90) is TWO STAMPS AND NOTHING ELSE — no member is read.
//
// ----------------------------------------------------------------------------
// 4. IO::VisitorStream IS A NULL SINK, AND ITS FIVE STREAM SLOTS ARE 2 BYTES
// ----------------------------------------------------------------------------
// FileStream.h:361-411 pins the class and says so; this file is the code half.
// Read out of .text, each body complete:
//
//     0x1800baff0  GetSize        xor eax,eax ; ret      -> 0
//     0x1800bb000  GetCurrentPos  xor eax,eax ; ret      -> 0
//     0x1800bb010  SetCurrentPos  mov al,1    ; ret      -> true
//     0x1800bb020  Read           mov al,1    ; ret      -> true
//     0x1800bb030  Write          mov al,1    ; ret      -> true
//
// `Read` returns true WITHOUT touching the caller's buffer. That is faithful
// and it is the point: `?Add@ExternalFactory@Object@@` runs an External's own
// `Serialize` against one so the object walks itself exactly as a save would
// and every byte is swallowed. The only thing that happens is the side effect
// in the Encoding the constructor installs at +0x10.
//
// THE CONSTRUCTOR (0x1800bb090) is the only one with substance:
//     Stream ctor inlined:  vptr = ??_7Stream@ ; direction = 0 ; encoding = 0
//     vptr = ??_7VisitorStream@ ; direction = 1              // 1 == WRITING
//     e = MemAlloc(8, 4)                                     // the Encoding
//     if (e) *(void**)e = 0x1801559f8                        // Visitor encoding
//     if (this->encoding) MemFree(this->encoding)            // the ctor's own 0
//     this->encoding = e
//     return this
//
// Two things in that order are worth writing down. First, `direction` is set
// to 0 by the inlined Stream ctor and then to **1** — the null sink presents
// itself as a WRITE stream, which is what makes an External serialize rather
// than deserialize into it. Second, the `if (this->encoding) MemFree(...)` is
// unreachable on this path (the store four instructions earlier made it null)
// and is reproduced anyway because it costs one branch and retail emits it.
//
// **0x1801559f8 IS NOT 0x1801554a8.** The shared BinaryEncoding table the rest
// of src/engine/io stamps is 0x1801554a8; the Visitor encoding is a DIFFERENT,
// also-unexported table at 0x1801559f8, and external_add.cpp:58 already records
// that. The constructor builds it as `IO::VisitorEncoding` (Serializer.h),
// and stamping the wrong one would produce a stream that walks the object and
// records nothing.
//
// ----------------------------------------------------------------------------
// 5. ??1Modification@IO@@ CLEARS THE EXPORTED SINGLETON, AND THAT IS THE POINT
// ----------------------------------------------------------------------------
// 0x1800b0470, in full — five instructions of substance:
//
//     mov  [rip+0x14c21f],0        ; ?_singleton@Modification@IO@@2PEAV12@EA
//     lea  rcx,[rbx+0x18]  ; call 0x180010b80   ; ~Array<Mod>(savedMods)
//     lea  rcx,[rbx+0x08]  ; jmp  0x180010b80   ; ~Array<Mod>(mods)   TAIL
//
// The ORDER is savedMods FIRST, then mods, and it is reproduced rather than
// tidied — the two arrays alias each other's element storage nowhere, but the
// claim "order does not matter here" is one this lane did not verify and does
// not need to make.
//
// **The singleton store is a SIDE EFFECT THAT MUST BE REPRODUCED**
// (reference_port_must_reproduce_side_effects.md).
// `?_singleton@Modification@IO@@2PEAV12@EA` is the class static
// `IO::Modification::_singleton` (modification.cpp), and the store clears it.
//
// `0x180010b80` (`Collection::Array<Mod>::~Array`) is ALREADY PORTED —
// `RtIO::ModList::DestroyAndFree` (row kSpec_Mod, modification.cpp) — and is called by name
// (B3R RT-IO; it was reached through the address door before).
// ============================================================================

#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <new>          // placement new into the retail hidden return slot

#include "io/Stream.h"
#include "io/FileStream.h"
#include "io/Serializer.h"
#include "io/Modification.h"
#include "system/String.h"

namespace RtIo {

// ===========================================================================
// ?GetSource@Stream@IO@@UEBA?AVString@System@@XZ   0x18000c6a0
// ===========================================================================
// ENDGAME B.3.9 IO (W2): the slot door that stood here went with the minted
// ??_7Stream@IO row, its last reader. The body is `IO::Stream::GetSource`
// (src/engine/io/Stream.h: `return System::String();`), slot 1 of every
// stream class's compiler table.

// ===========================================================================
// ?GetDirection@Stream@IO@@QEBA?AW4Direction@12@XZ   0x180006200
// ===========================================================================
// ENDGAME B.3.4 callers batch H. The WHOLE shipped body is
// `MOV EAX,dword ptr [RCX + 0x8]; RET` — NON-virtual (there is no slot for it
// in Stream's 7-slot table), const, and it reads `_direction` with no null
// test. The member is stated inline on `IO::Stream`
// (src/engine/io/Stream.h); B3R RT-IO deleted its registered door, which
// had no reader left.

// ===========================================================================
// IO::VisitorStream — the null sink. Header §4.
// ===========================================================================
// ENDGAME B.3.9 IO: the class is built by its real constructor
// (src/engine/io/FileStream.h -- `?Add@ExternalFactory@`'s stack instance)
// and wears the compiler's table. The ctor / dtor / slot-0 / five slot doors
// that stamped and filled the minted ??_7VisitorStream row are deleted with it;
// the five slot bodies are the members below.

// ===========================================================================
// ??1Modification@IO@@QEAA@XZ   0x1800b0470   — header §5
// ===========================================================================
extern "C" void RtIo_Modification_Dtor(IO::Modification* self) {
    // Clear the class static; see header §5.
    ::IO::Modification::_singleton = nullptr;                   // 1800b0486  (B.3.9 IO: the class static)

    self->_savedMods.DestroyAndFree();     // 1800b0495  +0x18  ~Array<Mod> 0x180010b80
    self->_mods.DestroyAndFree();          // 1800b04a4  +0x08  TAIL
}

}  // namespace RtIo

// ============================================================================
// ENDGAME B.3.4 STREAMS (4/6) — the five `IO::VisitorStream` slot bodies.
// ============================================================================
// A member of `IO::X` cannot be defined inside `namespace RtIo` — `IO` and
// `RtIo` are siblings, and C++ requires a member definition to appear in a
// namespace ENCLOSING the class (B34_BRIEFS section A.2). So the bodies open
// their own `namespace IO` block after the RtIo block closes, exactly as
// memory_stream.cpp, file_stream.cpp and serialize_family.cpp do.
//
// These are the whole class: two instructions each, read out of .text, and
// header section 4 above enumerates them with their raw bytes. `this` is never
// touched by any of the five — retail's bodies do not even load RCX — so none
// of them names it, and that absence is the null sink's definition rather than
// an omission.
//
// `IO::VisitorStream::Write` is the class's KEY FUNCTION (the first non-inline,
// non-pure virtual it declares), so GCC emits `vtable for IO::VisitorStream`
// in THIS translation unit. That is why the host unit build needs the stub
// copies in tests/unit/io_stream_abi_stubs.cpp: this file is not in the host
// unit build (tests/unit/sources.cmake does not list it).
namespace IO {

// ?Write@VisitorStream@IO@@UEAA_NHPEBX@Z   0x1800bb030   `mov al,1 ; ret`
// IT ACCEPTS EVERY BYTE AND STORES NONE. This is the sink half of the
// reference-visit pass; the Encoding at +0x10 is where the walk's only
// observable effect happens.
bool VisitorStream::Write(int, const void*) { return true; }

// ?Read@VisitorStream@IO@@UEAA_NHPEAX@Z   0x1800bb020   `mov al,1 ; ret`
// Returns true WITHOUT writing to `dst` — faithful, and the mechanism of the
// whole reference-visit pass. A port that filled the buffer would be inventing
// data retail never produces.
bool VisitorStream::Read(int, void*) { return true; }

// ?SetCurrentPos@VisitorStream@IO@@UEAA_NH@Z   0x1800bb010   `mov al,1 ; ret`
bool VisitorStream::SetCurrentPos(int) { return true; }

// ?GetCurrentPos@VisitorStream@IO@@UEBAIXZ   0x1800bb000   `xor eax,eax ; ret`
uint32_t VisitorStream::GetCurrentPos() const { return 0; }

// ?GetSize@VisitorStream@IO@@UEBAIXZ   0x1800baff0   `xor eax,eax ; ret`
uint32_t VisitorStream::GetSize() const { return 0; }

}  // namespace IO

