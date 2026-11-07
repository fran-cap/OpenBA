// ============================================================================
// src/engine/io/file_stream.cpp — Wave 19 lane FILE
// IO::FileStream / BinaryFileStream / TextFileStream lifecycle + Create
// ============================================================================
//
//   | Export                                              | Retail      | Ord  | Instrs |
//   |-----------------------------------------------------|-------------|------|--------|
//   | ??0BinaryFileStream@IO@@QEAA@XZ                     | 0x1800a8200 |   55 |   45   |
//   | ??1BinaryFileStream@IO@@UEAA@XZ                     | 0x180023c80 |  532 |    5   |
//   | ??1FileStream@IO@@UEAA@XZ                           | 0x1800aaab0 |  593 |   32   |
//   | ??0TextFileStream@IO@@QEAA@AEBVString@System@@0H@Z  | 0x1800ba340 |  441 |   58   |
//   | ?Create@FileStream@IO@@IEAA_NAEBVFilename@System@@@Z| 0x1800aa8f0 | 1876 |   63   |
//
// plus the UNEXPORTED `~FileInfo` at `0x18000c180` (25 instrs), which
// `~FileStream` calls and which nothing in this tree had ported —
// external_savetext.cpp transcribes it inline for its own inlined epilogue
// but registers nothing, so to a callee census it was absent
// (`reference_half_registered_port_reads_as_absent.md`).
//
// Names, ordinals and addresses printed out of Runtime-x64.dll's export
// directory with `pefile`, not grepped and not carried over from a doc.
//
//
// ---------------------------------------------------------------------------
// WHY THIS LANE EXISTS: THE SCREENSHOT CUTOVER HOLE
// ---------------------------------------------------------------------------
// Wave 18 lane VRES ported `?SaveImageToFile@ImageBuffer@Video@@` and said so
// in its own §T1.2: porting it moved the screenshot hole from "no
// implementation" to "an implementation with unresolved IO dependencies". Those
// dependencies are the rows above plus the FileSystem rows in
// filesystem_createfile.cpp. `ResolveExport` FALLS THROUGH to the shipped
// body for an unported symbol, so before this file existed the whole path
// worked with the DLL present and resolved to null without it.
//
// NO LAYOUT IN THIS FILE WAS RE-DERIVED. `src/engine/io/FileStream.h` has
// pinned this whole family since Wave 11 lane IOSTREAM — sizeof 0x50, filename
// +0x18, FileInfo +0x20, the Win32 handle at +0x38, the byte cursor at +0x2c,
// and TextFileStream's ten fields with the `objectName`/`objectType` pair the
// RIGHT way round (that header records having had them swapped until retail's
// own one-instruction accessors were run against it).
//
// ---------------------------------------------------------------------------
// THE ONE FACT A SIGNATURE WOULD GET WRONG
// ---------------------------------------------------------------------------
// `?CreateFileW@FileSystem@IO@@` RETURNS A `FileInfo` BY VALUE, so its hidden
// return slot is RDX and the `Filename*` is R8 — three arguments in the
// typedef, not two (`reference_hidden_retslot_wild_write.md`). `?Create@
// FileStream@` at 0x1800aa963/96f loads `LEA RDX,[RSP+0x28]` (a 0x30-byte stack
// FileInfo) before the call and then hands the RETURNED pointer to
// FileInfo::operator=. A two-argument typedef writes 0x30 bytes of the caller's
// frame and still returns the right answer.
//
// ---------------------------------------------------------------------------
// TWO VTABLES IN THIS FILE HAVE NO EXPORTED NAME — CHECKED, NOT ASSUMED
// ---------------------------------------------------------------------------
// The first draft of this file stamped the text Encoding through a name it had
// invented, `??_7TextEncoding@IO@@6B@`. That name is a PHANTOM: 0x1801559d0 has
// NO export at all, and a ResolveExport name the DLL does not contain returns
// NULL SILENTLY (`reference_resolveexport_phantom_names.md`). It was caught by
// running every mangled name in this file against the shipped export directory
// with `pefile` BEFORE the body was written, which is the check the doctrine
// asks for and the only reason it was not shipped.
//
// Verified, by VA, against the export directory:
//   0x1801549a0  ??_7Stream@IO@@6B@             EXPORTED   — reached by name
//   0x180154960  ??_7FileStream@IO@@6B@         EXPORTED   — reached by name
//   0x1801554d0  ??_7BinaryFileStream@IO@@6B@   EXPORTED   — reached by name
//   0x180155980  ??_7TextFileStream@IO@@6B@     EXPORTED   — reached by name
//   0x1801554a8  the shared binary Encoding     NO EXPORT  — reached by ADDRESS
//   0x1801559d0  the text Encoding              NO EXPORT  — reached by ADDRESS
//
// compressedstream.cpp:524 reached the same conclusion about 0x1801554a8: for
// an unexported table the address door is the ONLY port-aware door.
//
// ---------------------------------------------------------------------------
// WHAT IS *NOT* PORTED HERE, NAMED RATHER THAN SILENT
// ---------------------------------------------------------------------------
//   ??_7FileStream@IO@@6B@ (0x180154960), ??_7BinaryFileStream@ (0x1801554d0)
//   and ??_7TextFileStream@ (0x180155980) were DEFERRED to lane MINT by the
//   Wave 19 brief.
//
//   [CORRECTED 2026-09-10, post-report. What stood here said all three appear
//   ZERO times by VA in standalone/rt_data_mint_tables.inc, citing WAVE19_IO.md
//   §0b and phase 0c. That was measured at 20aa588 and is now FALSE: phase 0b
//   regenerated the mint and all three are minted, verified by VA on this tip.
//   The cause was neither a generator bug nor a harvest gap -- the generated
//   file was simply STALE, last regenerated at 4f037fd (Wave 10D) while the
//   consumers that put these tables into the census landed after it. A
//   GENERATED ARTEFACT IN THE TREE IS A MEASUREMENT WITH A TIMESTAMP, exactly
//   like a stated blocker, and this comment was a second-hand copy of one.
//   Recorded rather than quietly rewritten.]
//
//   ??_7FileStream@ and ??_7BinaryFileStream@ are minted, and this file's
//   post-report tranche clears BOTH to TRAP=0. Promotion is a separate question
//   and the two rows differ on it.
//
//   THE STAMPER COUNTS BELOW ARE MEASURED HERE, and both differ from the ones
//   this lane was briefed with. `get_xrefs_to` is cheap and neither figure had
//   been run:
//
//     ??_7BinaryFileStream@ (0x1801554d0) — briefed as ONE stamper, measured
//       **TWO**: 0x1800a8270 (the default ctor) and 0x180023cfe (the COPY
//       ctor, 0x180023ca0). Both are ported by this lane, so the row IS fully
//       owned — but only because the copy ctor was added. On the briefed claim
//       it would have been declared owned with a stamper still in the DLL.
//
//     ??_7FileStream@ (0x180154960) — briefed as NINE stampers, measured about
//       **SEVENTEEN**, of which five are already ours (SaveText, ~FileStream,
//       ??0TextFileStream, ??0BinaryFileStream, SaveImageToFile). The
//       CONCLUSION the brief drew still holds — Engine::Run (two sites) and
//       three unexported bodies are not ours, so TRAP=0 here is NOT a
//       promotion (reference_a_ported_row_can_be_permanently_unpromotable.md)
//       — but the number it rested on does not.
//
//   ??_7TextFileStream@ is minted at 20 SLOTS AND IS REALLY 10 -- an unexported
//   table sits at 0x1801559d0, exactly ten slots in, and the generator's
//   adjacent-export bound cannot see it. Reported to lane MINT; the row is
//   handed back rather than taken here.
//
//   ??_7Stream@IO@@6B@ (0x1801549a0) and the shared Encoding table
//   (0x1801554a8) ARE minted already — checked BY VA in
//   rt_data_mint_tables.inc, not by name. Phase 0b warns that the name grep
//   returns `adjacent-export:` labels on OTHER rows and looks exactly like a
//   hit; 0x1801559d0 is NOT minted.
//
//   MINTED IS NOT PROMOTED, and this is where a reader of the above will go
//   wrong. None of these tables is promoted: a run reports
//   only **5 of 62** minted VTABLES are registered on a default run, so
//   ??_7Stream@ prints
//   READY / TRAP=0 on the readiness screen and is STILL AN OPEN DOOR that this
//   lane's own cutover screen catches
//   (reference_a_green_readiness_screen_is_not_a_route.md).
//
//   ??0FileStream@IO@@QEAA@XZ (0x1800aaa10) is deliberately absent. Retail
//   INLINES it into both constructors below rather than calling it, and the
//   census does not list it as called-but-unported. Both constructors here
//   transcribe the inlined copy, which is what retail executes.
//
// PORTABILITY: no <windows.h> in this file (PLAN.md §4.1 decision 1). Nothing
// here touches Win32 — the bodies that do are in filesystem_createfile.cpp
// beside it.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include <cstdio>

#include "io/FileStream.h"
#include <new>          // B.3.4 block 4: the GetSource door placement-constructs
                       // retail's by-value String into the caller's raw slot

#include "io/FileSystem.h"
#include "system/String.h"
#include "system/Filename.h"
#include "system/string_internal.h"

namespace RtIO {

using RtSystem::StringRefCount;

// The stream layouts live in `RtIo` (lower-case o) while FileInfo/FileSystem
// live in `RtIO`. Both spellings are long-standing in this tree — FileSystem.h
// says so at the top of the file — and this lane does not rename either.
using RtIo::Stream;
using RtIo::FileStream;
using RtIo::BinaryFileStream;
using RtIo::TextFileStream;

// B.4 R-IO: the FileSystem members this file calls (CreatePath, CreateFileW,
// OpenFile, ReadFile, CloseFile) are called on `IO::FileSystem::_singleton`,
// read exactly where retail reads it (`?_singleton@FileSystem@IO@@0PEAV12@EA`,
// exported DATA, ord 4811). The five doors and the POD `Fs()` accessor are gone.

namespace {


// ??0FileStream@IO@@QEAA@XZ (0x1800aaa10): ENDGAME B.3.9 IO (W2) -- the door
// is gone; Audio::Stream::Run and the persistence loader build the class.

}  // namespace

// The port-only attendance counters that stood here (`FileStreamCounters` /
// `g_fsc`, the slot counters `g_fss` and file_stream_write.cpp's `g_fsWrite` family)
// are deleted (review F-140). Nothing read them once the cutover probe was gone, and
// their plain `++` ran on every IO thread (the Persistence worker, the Audio::Stream
// worker, the Loader): a data race. Retail has no counter in any of these bodies.

// ============================================================================
// WAVE 20 LANE PROMOTE'S `BfsPromoteCounters` IS GONE — B34_RULES section 4.1
// ============================================================================
// That struct, its global, `OurBfsTable()`, `NoteBfsStamp()`,
// `NoteSlotDispatch()`, their seven call sites and the six-number exporter were
// all deleted by ENDGAME B.3.4 block 4, in the commit that made
// `IO::BinaryFileStream` a real class. They are not left counting zero and not
// left counting a constant.
//
// WHAT THEY ASKED, AND WHY THE QUESTION NO LONGER EXISTS. `??_7FileStream@` and
// `??_7BinaryFileStream@` are byte-identical in slots 1..7 and differ only at
// slot 0, so a per-body counter could not say WHICH of the two promoted rows
// had dispatched into a shared body; the instrument compared the live object's
// vptr against the table our own constructors stamp, to separate the tiers.
//
// With `IO::FileStream` and `IO::BinaryFileStream` declared, that is answered
// structurally: there is exactly one table per class, the compiler's, the two
// are distinct types, and `p->Type()`-style identity is the standing rule
// (B33 section 5). There is nothing to port forward.
//
// Two of its six fields had already stopped meaning anything: the DLL-side
// counter was never incremented once the DLL was removed, and the two ctor
// counters could no longer differ — the stamp helper's own body said so ("the
// minted table is the only table now").
//
// ============================================================================


// ============================================================================
// ~FileInfo                                              retail 0x18000c180
// ============================================================================
// B.4 Z LAST IO: FileInfo's own (implicit) destructor now — the String release
// idiom twice, _name1 (+0x28) FIRST, then _name0 (+0x20): reverse declaration
// order of its two Filename members. The `RtIo_FileInfo_Dtor` body that stood
// here, and its `fileInfoDtor` attendance count, are gone with it.

// ============================================================================
// ??1FileStream@IO@@UEAA@XZ          retail 0x1800aaab0
// ??1BinaryFileStream@IO@@UEAA@XZ    retail 0x180023c80 (a bare JMP to the first)
// ============================================================================
// ENDGAME B.3.9 IO: the bodies are `IO::FileStream::~FileStream` (bottom of
// this file) and the compiler's `~BinaryFileStream`. These two doors STAY
// because callers outside the classes reach them BY NAME on objects they built
// through `??0FileStream` / `??0BinaryFileStream`: Audio::Stream::Run, the
// persistence jobs, Object::External::Load (CompressedFileStream's member,
// through ??1FileStream exactly as retail's 1800bc8c6) and
// Video::ImageBuffer::SaveImageToFile. Each is a qualified `T::~T()`
// call (retail: slot 0, flags 0) -- the destructor chain, no free (B9-6).
// ENDGAME B.3.9 IO (W2): the ??1FileStream / ??1BinaryFileStream doors that
// stood here are gone -- every caller ends its stream with the class's
// qualified `T::~T()` at the position the door call had.


// ============================================================================
// ??0BinaryFileStream@IO@@QEAA@XZ                        retail 0x1800a8200
// ============================================================================
// ENDGAME B.3.9 IO: the body is `IO::BinaryFileStream`'s real constructor
// (FileStream.h). The door STAYS for the callers that reach it by name on raw
// storage (Object::External::Load's CompressedFileStream member,
// Video::ImageBuffer::SaveImageToFile, the persistence jobs): one placement
// new (B9-6). The TextFileStream ctor door went -- its one caller,
// ?SaveText@External@, now builds the class itself.
// ENDGAME B.3.9 IO (W2): the ??0BinaryFileStream door is gone -- every
// builder spells `::new (p) IO::BinaryFileStream()`.

// ============================================================================
// IO::TextFileStream::SetObject(String const&, String const&, int)  0x1800ba280
// ============================================================================
// WAVE 23 lane IOPORT.  Ordinal 3930.  42 instructions.
//
// THE ARGUMENT ORDER IS THE WHOLE RISK IN THIS ROW, so it was derived from the
// REGISTERS, not from the decompiler's parameter names and not from the callers:
//
//   1800ba29e  MOV RDI,RDX   arg1 ... 1800ba313  MOV [RBX+0x60],RAX  objectType
//   1800ba29b  MOV RSI,R8    arg2 ... 1800ba2d2  MOV [RBX+0x58],RAX  objectName
//   1800ba298  MOV EBP,R9D   arg3 ... 1800ba2de  MOV [RBX+0x50],EBP  objectVersion
//
// So SetObject(a, b, v) assigns objectTYPE = a and objectNAME = b: the FIRST
// String is the type and the SECOND is the name.  That is the reverse of the
// reading order the field layout suggests (+0x58 name comes before +0x60 type),
// and this exact pair has been swapped in this project before —
// src/engine/io/FileStream.h records having carried them the wrong way round
// until retail settled it.  Three independent confirmations of the orientation:
//   1. the register trace quoted above (this lane, retail Runtime-x64.dll);
//   2. RtIo_TextFileStream_Ctor immediately above already passes
//      `(self, type, name, version)` in that order to reach 1800ba45e;
//   3. ?GetObjectName@ returns +0x58 and ?GetObjectType@ returns +0x60
//      (FileStream.h's static_asserts quote both LEAs).
//
// THE BODY, in retail's order — the version store sits BETWEEN the two string
// assignments, not before or after both:
//     objectName (+0x58) <- arg2      [release-then-addref, guarded]
//     objectVersion (+0x50) <- arg3
//     objectType (+0x60) <- arg1      [release-then-addref, guarded]
//
// THE SELF-ASSIGNMENT GUARD IS LOAD-BEARING AND IS REPRODUCED.  `CMP RAX,[R8] /
// JZ` at 1800ba2a4 skips the ENTIRE release-and-addref when the incoming String
// already holds the same char*.  Dropping it would release and re-add a
// reference in the same breath: a no-op for a live string, but it frees the
// buffer outright when the field holds the last reference, which is a
// use-after-free the moment a caller passes a field back to itself.
//
// THE STRING IDIOM is System::String's one-pointer representation with the
// refcount at [p-4] and the allocation base at p-8 — the shape avl_trees.cpp's
// StringReleaseInline and a dozen other ported bodies already use.  It is
// written out inline here rather than through that helper because retail's
// spelling differs in one visible way: the assignment stores the new pointer
// DIRECTLY (1800ba2cf `MOV RAX,[RSI] / MOV [RBX+0x58],RAX`) with no preceding
// `slot = 0`, whereas StringAddRefInto stores the zero first.  Equivalent in
// effect, NOT equivalent in bytes; this file follows the bytes.
//
// MemFree is 1800c7ad0 reached through RtSystem::RtMemFree, the port-aware door
// lane E's allocator replaces when it arms — the door every other free here uses.
static void TfsReleaseAssign(char** slot, char* const* incoming) {
    char* cur = *slot;
    if (cur == *incoming) return;                          // 1800ba2a4 / 1800ba2e5
    if (cur != nullptr) {
        if (--reinterpret_cast<int32_t*>(cur)[-1] == 0) {  // 1800ba2ae  // CAST-OK: string_header
            *slot = cur - 8;                               // 1800ba2b7
            RtSystem::RtMemFree(*slot);                    // 1800ba2c0
        } else {
            *slot = nullptr;                               // 1800ba2c7
        }
    }
    char* nw = *incoming;                                  // 1800ba2cf
    *slot = nw;
    if (nw != nullptr) ++reinterpret_cast<int32_t*>(nw)[-1];  // 1800ba2db  // CAST-OK: string_header
}

// ---------------------------------------------------------------------------
// ORACLE: A/B ON SYNTHETIC SUBJECTS, and why it is NOT a replay is the point.
// ---------------------------------------------------------------------------
// SetObject cannot be replayed over the live object.  Running theirs AFTER ours
// is VACUOUS: ours has already stored the incoming pointers, so theirs takes
// the self-assignment guard on both strings and does nothing, and the
// comparison passes no matter what this body did.  That is the
// "a predicate nobody evaluates" shape — permanently green, permanently blind.
//
// Running theirs on a scratch object whose fields ALIAS the real object's
// strings is worse than vacuous: both bodies decrement the same refcount, which
// double-releases a live string.
//
// So the subject is SYNTHETIC.  Each armed call builds independent
// TextFileStream-shaped buffers and freshly allocated refcounted strings, runs
// the DLL's body on one and this port's on the other with identical arguments,
// and compares.  Nothing live is touched and nothing is aliased.
//
// WHAT IS COMPARED: all three written fields (+0x50, +0x58, +0x60) on both
// subjects AND the refcount of every string involved.  The refcounts are the
// half a field comparison alone would miss, and they are exactly where a
// dropped addref or a doubled release shows up.
//
// WHICH BRANCHES IT COVERS — three cases per armed call:
//   (a) old == nullptr            fresh assign, no release
//   (b) old != new, refcount > 1  release-without-free      (1800ba2c7)
//   (c) old == new                the self-assignment guard (1800ba2a7)
// Case (d), release-WITH-free (refcount reaches 0, 1800ba2b7), is NOT
// exercised: driving it means handing the DLL a string it will MemFree, and the
// two subjects would then need separately-allocated-but-equal strings, at which
// point the compared pointers legitimately differ and the comparison stops
// meaning anything.  That branch is covered by STATIC BYTE READING ONLY and is
// listed as such in docs/history/runtime_port/WAVE23_LANE_IOPORT.md rather than being
// quietly folded into a green result.
//
// NEGATIVE CONTROL: the old DLL-era switch makes the port's objectType
// store write nullptr.  +0x60 IS in the compared set, so the control corrupts
// something the oracle actually reads — the property a control needs.  A
// control that corrupts an unobserved value reports NOT CAUGHT and indicts a
// sound instrument; the mirror (corrupting a value that SHOULD differ)
// exonerates a broken one.  Neither applies here: +0x60 must be EQUAL on both
// subjects, because both are handed the same incoming pointer.
extern "C" void RtIo_TextFileStream_SetObject(::IO::TextFileStream* self,
                                              const System::String* type,
                                              const System::String* name,
                                              int version) {

    TfsReleaseAssign(&self->_objectName._data, &name->_data);   // 1800ba294..ba2dd   arg2
    self->_objectVersion = version;                      // 1800ba2de          arg3
    TfsReleaseAssign(&self->_objectType._data, &type->_data);   // 1800ba2e1..ba31e   arg1

}

// ============================================================================
// IO::FileStream::Create(Filename const&)                retail 0x1800aa8f0
// ============================================================================
// PROTECTED (`IEAA`), which means PRIVATE-to-the-hierarchy, NOT unexported —
// this DLL exports its private members and ord 1876 is right there
// (`reference_aeaa_means_private_not_unexported.md`).
//
// Five steps:
//   1. FileSystem::CreatePath(_singleton, path)              1800aa919
//   2. this->filename = path  (release/adopt, self-assign short-circuited)
//                                                            1800aa91e..58
//   3. this->direction = 1  (writing)                        1800aa959
//   4. FileInfo tmp = FileSystem::CreateFileW(_singleton, path);
//      this->file = tmp;  ~tmp;                              1800aa960..9ef
//   5. return this->file.handle != -1                        1800aa9f0
//
// STEP 2's SELF-ASSIGNMENT SHORT-CIRCUIT JUMPS PAST BOTH HALVES.
// `CMP RAX,[RSI] / JZ 0x1800aa959` skips the release AND the adopt, not just
// the release — the same detail filesystem_openfile.cpp records for
// FileInfo::operator=, and getting it wrong drops a shared buffer's count to
// zero and frees a string both sides still point at.
//
// STEP 4's TEMPORARY IS DESTROYED IN RETAIL'S ORDER: _name1 (+0x28, at
// rsp+0x50) before _name0 (+0x20, at rsp+0x48). That is the inlined ~FileInfo,
// and it is the same order the standalone one at 0x18000c180 uses.
// ============================================================================
}  // namespace RtIO

// IO::FileStream::Create -- ENDGAME B.3.9 IO (W2): the member is the entry
// point; B.4 R-IO: the body is the member's own.
bool IO::FileStream::Create(const System::Filename& path) {
    ::IO::FileSystem::_singleton->CreatePath(path);  // 1800aa919

    // ---- 1800aa91e..1800aa958: this->filename = *path -------------------
    // ENDGAME B.3.1 step 2: this whole block IS `System::String::operator=`
    // (0x1800c9810) — the buffer-pointer guard that skips BOTH halves, the
    // release with its asymmetry, the unconditional store and the guarded
    // increment. `FileStream::filename` is declared `char*` in FileStream.h,
    // which is exactly the one pointer a String is, so it is reached as one.
    // NOTE there is NO AdjustDelimiters here: retail's Create does not
    // normalise, so this is String's operator=, not Filename's.
    _filename = path.AsString();

    _direction = 1;                                            // 1800aa959

    // ---- 1800aa960..1800aa9ef -------------------------------------------
    // NOT zero-initialised: CreateFileW writes every field of it that anything
    // reads, and _unk04 is one neither side ever writes.
    // CreateFileW into a temporary (1800aa96f), FileInfo::operator=
    // (1800aa97c), ~FileInfo on the temporary (1800aa982 +0x28, 1800aa9b9 +0x20).
    _file = ::IO::FileSystem::_singleton->CreateFileW(path);

    const bool ok = (_file.handle != RtIO::FileInfo::kHandleNone);   // 1800aa9f0
    return ok;
}

// ============================================================================
// THE VTABLE SLOT BODIES — Wave 19 lane FILE, POST-REPORT TRANCHE (see §6.16:
// sealing forbids altering, not adding; this is new work, clearly marked).
// ============================================================================
//
//   | Export                                             | Retail      | Ord  | Slot |
//   |----------------------------------------------------|-------------|------|------|
//   | ?GetSource@FileStream@IO@@UEBA?AVString@System@@XZ | 0x1800aab50 | 2920 |  1   |
//   | ?Read@FileStream@IO@@UEAA_NHPEAX@Z                 | 0x1800aabf0 | 3450 |  3   |
//   | ?GetCurrentPos@FileStream@IO@@UEBAIXZ              | 0x1800aac50 | 2534 |  5   |
//   | ?GetSize@FileStream@IO@@UEBAIXZ                    | 0x1800aac60 | 2904 |  6   |
//   | ?Open@FileStream@IO@@UEAA_NAEBVFilename@System@@@Z | 0x1800aac70 | 3304 |  7   |
//
// plus the two UNEXPORTED deleting-destructor thunks at slot 0 —
// `0x18000c090` (FileStream's) and `0x180023d80` (BinaryFileStream's).
// `?Write@` (slot 2) and `?SetCurrentPos@` (slot 4) are in file_stream_write.cpp
// because they call Win32 directly; the split is the platform seam, not a
// division of the row.
//
//
// ---------------------------------------------------------------------------
// WHY NINE BODIES CLEAR TWO WHOLE DATA ROWS
// ---------------------------------------------------------------------------
// `??_7FileStream@IO@@6B@` (0x180154960, 8 slots) and
// `??_7BinaryFileStream@IO@@6B@` (0x1801554d0, 8 slots) were both minted by
// Wave 19 phase 0b at 0 ported / 8 TRAP. **Their slots 1..7 are byte-identical**
// — re-derived here by reading both tables' bytes rather than inherited:
//
//   0x1801554d0: 180023d80 1800aab50 1800aab90 1800aabf0
//                1800aac10 1800aac50 1800aac60 1800aac70
//   0x180154960: 18000c090 <the same seven>
//
// So the two rows differ ONLY in slot 0, and 7 shared bodies + 2 thunks = 9
// functions cover 16 trap slots. That is the "scope a DATA row by FUNCTION, not
// by slot" shape, and it was found independently by two Wave 19 cells before it
// was found here.
//
// ---------------------------------------------------------------------------
// WHAT CLEARING THEM DOES AND DOES NOT BUY — the STAMPER criterion
// ---------------------------------------------------------------------------
// A minted vtable row is promotable exactly insofar as WE OWN THE FUNCTION THAT
// STAMPS IT: every stamp of these tables is an in-DLL RIP-relative LEA
// (`1800a8270 LEA RAX,[0x1801554d0] -> MOV [RDI],RAX`), and a LEA is not a door
// (`reference_a_ported_row_can_be_permanently_unpromotable.md`).
//
//   * `??_7BinaryFileStream@` has ONE stamper — `??0BinaryFileStream@`, which
//     this lane already ported. Clearing these traps makes that row promotable.
//   * `??_7FileStream@` has NINE distinct stampers, including `Engine::Run`.
//     **TRAP=0 there is NOT a promotion** and must not be read as one; most
//     stamps stay on retail's side whatever we do to the slots.
//
// The consequence for these bodies is that they must be byte-faithful for BOTH
// tables: an object WE construct gets our minted table and our slot bodies, an
// object retail constructs gets retail's, and the two have to behave alike.
//
// ---------------------------------------------------------------------------
// THE ARRAY ARM OF BOTH THUNKS IS A NAMED ABORT — a declared divergence
// ---------------------------------------------------------------------------
// Both thunks are MSVC's standard scalar/vector deleting destructor, and the
// `flags & 2` (delete[]) arm calls the CRT's `eh_vector_destructor_iterator` at
// `0x18007ec98`, which is unexported and unported. `binary_writer.cpp` faced
// exactly this and made the arm a NAMED ABORT rather than open an address door
// onto a CRT helper for a path no caller takes; this file takes the same
// disposition for consistency, and states its limit:
//
//   * nothing in this tree array-allocates a FileStream or BinaryFileStream —
//     every one is stack-constructed by its caller (`external_load.cpp`,
//     `external_savetext.cpp`, `save_image_to_file.cpp`) or embedded in a
//     `CompressedFileStream`;
//   * **but unlike BinaryWriter, whose only producer is a `MemAlloc(0x30,4)`
//     with no array cookie, this lane did NOT prove no such producer can
//     exist.** The claim here is "no caller in either binary that we have
//     found", which is weaker, and the abort message says so.
//
// This is the one place in this tranche that is not byte-faithful. It is
// deliberate, it is declared, and if it ever fires the message names what would
// have had to happen.
// ============================================================================

// ============================================================================
// `IO::FileStream`'s member bodies — ENDGAME B.3.4 block 4 (STREAMS)
// ============================================================================
// A member of `IO::X` cannot be defined inside `namespace RtIO`: `IO` and
// `RtIO` are siblings and C++ requires the definition to appear in an
// ENCLOSING namespace (B34_BRIEFS section A.2). So the five bodies this file
// owns open their own block here, and reach this file's helpers by their
// `RtIO::` qualification. The two Win32 slots (`Write`, `SetCurrentPos`)
// are in file_stream_write.cpp, next to their own `<windows.h>`.
namespace IO {

// ============================================================================
// IO::FileStream::GetSource() const                      retail 0x1800aab50
// ============================================================================
// IT IS AN ADD-REF, NOT A COPY. The caller receives the same buffer and owes it
// a release. A port that duplicated the characters would be observably wrong to
// anyone comparing pointers, and would leak.
//
// Retail returns the `System::String` by value through a hidden slot in RDX;
// here that is the compiler's business again and the member simply returns one.
// The `[RSP]` slot retail writes 0 then 1 into is the SEH unwind state for the
// temporary, not a field of anything.
System::String FileStream::GetSource() const {
    System::String ret;                                   // 1800aab67: *ret = nullptr
    if (_filename._data) {                                // 1800aab6e
        // The ADD-REF, spelled exactly as retail spells it and NOT through
        // `String`'s copy constructor: two stores, the pointer and the
        // refcount in front of it. `_data` is public during the migration
        // (String.h) for exactly this kind of transcription.
        ret._data = _filename._data;                           // 1800aab73
        ++RtSystem::StringRefCount(ret._data);                 // 1800aab76
    }
    return ret;                                           // 1800aab80
}

// ============================================================================
// IO::FileStream::Read(int, void*)                       retail 0x1800aabf0
// ============================================================================
// SIX INSTRUCTIONS AND A TAIL-JUMP. The whole body is
//
//     return FileSystem::ReadFile(_singleton, &this->file, len, buf);
//
// and the argument shuffle is the only content: `len` arrives in EDX and has to
// reach R8D, `buf` arrives in R8 and has to reach R9, because the callee takes
// `this` in RCX and the FileInfo in RDX.
//
// **READS GO THROUGH FileSystem, WRITES DO NOT.** `?Write@FileStream@` calls
// `WriteFile` directly (file_stream_write.cpp). That asymmetry is retail's, and
// it is why only one of the two needed a `_win` file: reading a packed file has
// to consult the package index, and `FileSystem::ReadFile` is where that lives.
//
// `?ReadFile@FileSystem@IO@@` is already ported and registered (Wave 9 lane
// PKG2 rung 3), so it is called DIRECTLY here rather than through a door.
bool FileStream::Read(int len, void* buf) {
    const bool ok = FileSystem::_singleton->ReadFile(_file, len, buf);  // 1800aac03
    return ok;
}

// ============================================================================
// IO::FileStream::GetCurrentPos() const                  retail 0x1800aac50
// IO::FileStream::GetSize() const                        retail 0x1800aac60
// ============================================================================
// One instruction each: `MOV EAX,[RCX+0x2c]` and `MOV EAX,[RCX+0x28]`.
//
// BOTH RETURN **UNSIGNED** (`UEBAIXZ`), while `FileInfo::cursor` and `::size`
// are modelled `int32_t`. The bits are identical; the return TYPE is what the
// caller's comparison sees, and a signed reading flips every test above 2GB.
// The signature comes from the export directory, which is the only place it is
// visible — a one-instruction body cannot show a return type.
//
// These two are also what `FileStream.h` used to PIN `FileInfo` at +0x20 (its
// route 2), so they are the cheapest possible cross-check on that claim: if the
// offset were wrong these would return the wrong field on every call, on a path
// the package loader takes thousands of times per run.
uint32_t FileStream::GetCurrentPos() const {
    return static_cast<uint32_t>(_file.cursor);            // 1800aac50
}

uint32_t FileStream::GetSize() const {
    return static_cast<uint32_t>(_file.size);              // 1800aac60
}

// ============================================================================
// IO::FileStream::Open(Filename const&)                  retail 0x1800aac70
// ============================================================================
// THE READ TWIN OF `?Create@FileStream@`, and comparing the two is the clearest
// statement of what each does. Same five steps, three differences:
//
//   | step                | Create (0x1800aa8f0)   | Open (0x1800aac70)      |
//   |---------------------|------------------------|-------------------------|
//   | 1. prepare          | CreatePath(_singleton) | *(nothing)*             |
//   | 2. filename         | release/adopt          | release/adopt, IDENTICAL|
//   | 3. direction (+0x8) | **1** (writing)        | **0** (reading)         |
//   | 4. get the FileInfo | FileSystem::CreateFileW| FileSystem::**OpenFile**|
//   | 5. return           | file.handle != -1      | file.handle != -1       |
//
// Step 2's SELF-ASSIGNMENT SHORT-CIRCUIT JUMPS PAST BOTH HALVES here too
// (`CMP RAX,[RDX] ; JZ 0x1800aaccd` skips the release AND the adopt), and step
// 4's temporary is destroyed in the same order — `_name1` (rsp+0x50) before
// `_name0` (rsp+0x48).
//
// **Open does NOT call CreatePath.** Creating directories on the way to a file
// you are about to READ would be wrong, and retail does not do it. Worth
// stating because the two bodies are otherwise near-identical and a port
// written by analogy from `Create` would add it.
//
// `?OpenFile@FileSystem@IO@@` is already ported and registered (Wave 9 lane
// PKG2 rung 3) and returns a `FileInfo` BY VALUE — RDX is the hidden return
// slot, R8 the Filename.
//
// B.4 R-IO: `FileSystem::OpenFile` takes the class, as retail's signature says,
// so `path` goes to it as it is.
bool FileStream::Open(const System::Filename& path) {
    // ---- 1800aac92..1800aaccc: this->filename = *path -------------------
    // `System::String::operator=` (0x1800c9810) — the short-circuit that jumps
    // past BOTH halves is the class's, and Open does not normalise either.
    _filename = path.AsString();

    _direction = 0;                                      // 1800aaccd — READING

    // OpenFile into a temporary (1800aacdf), FileInfo::operator= (1800aacec),
    // ~FileInfo on the temporary (1800aacf2 +0x28, 1800aad29 +0x20).
    _file = FileSystem::_singleton->OpenFile(path);

    const bool ok = (_file.handle != RtIO::FileInfo::kHandleNone);   // 1800aad60
    return ok;
}

// ============================================================================
// IO::FileStream::~FileStream                            retail 0x1800aaab0
// ============================================================================
// ENDGAME B.3.9 IO: the body of the old `??1FileStream` door, now the class's
// destructor. Retail's six steps, minus the two table stamps (the compiler's
// own) and with step 6 in ~Stream:
//   2. if (file.handle != -1) FileSystem::CloseFile(_singleton, &file)
//   3. ~FileInfo(&file)            4. release `_filename`
//   6. (~Stream) if (encoding) MemFree(encoding)
// STEP 2 READS THE SINGLETON, IT DOES NOT USE `this` (`MOV RCX,[0x1801fc6a8]`).
// Step 4 (1800aaaf6) is the `System::String` member's own destructor, which the
// compiler runs after this body and before ~Stream (B4 X-RH).
FileStream::~FileStream() {
    if (_file.handle != RtIO::FileInfo::kHandleNone)              // 1800aaad5
        FileSystem::_singleton->CloseFile(_file);                 // 1800aaae6
    // 1800aaaf0 (~FileInfo 0x18000c180 on +0x20) and 1800aaaf6 (~String on
    // +0x18) are the members' own destructors, run after this body in reverse
    // declaration order (B.4 Z LAST IO).
}

// ============================================================================
// IO::TextFileStream::TextFileStream(String const&, String const&, int)
//                                                         retail 0x1800ba340
// ============================================================================
// The FileStream prologue (the base ctor), seven of its own fields, a TEXT
// encoding, then a tail call to `?SetObject@` with the three arguments
// verbatim. It never writes +0x50 (SetObject's third argument does) nor +0x54.
// `_requestedFormat` and `_activeEncoding` are both 6 here; `?SaveText@
// External@` sets both to 2 and branches on +0x6c alone for the BOM.
TextFileStream::TextFileStream(const System::String& type,
                               const System::String& name, int version)
    : _objectName(),                 // 1800ba3d0  +0x58
      _objectType(),                 // 1800ba3d4  +0x60
      _requestedFormat(Text::Format::Detect),           // 1800ba3d8  +0x68
      _activeEncoding(Text::Format::Detect),            // 1800ba3df  +0x6c
      _buffer(nullptr),              // 1800ba3e6  +0x70
      _bufSize(0),                   // 1800ba3ea  +0x78 (one qword with +0x7c)
      _bufCursor(0) {
    void* encMem = RtSystem::RtMemAlloc(8, 4);                       // 1800ba3ee
    Encoding* enc = nullptr;
    if (encMem) enc = ::new (encMem) TextEncoding();
    if (_encoding) RtSystem::RtMemFree(_encoding);
    _encoding = enc;                      // ..4e

    RtIO::RtIo_TextFileStream_SetObject(this, &type, &name, version);   // 1800ba45e
}

// ============================================================================
// IO::TextFileStream::~TextFileStream                    retail 0x1800ba490
// ============================================================================
// Transcribed from the retail disassembly (WAVE 27 LANE IOTEXT), minus the
// table stamp:
//   2. if (buffer) MemFree(buffer)  -- BARE, and NOT nulled afterwards
//   3. release objectType (+0x60)   4. release objectName (+0x58)
//   5. JMP ~FileStream(this)        -- the compiler's base destructor
// B.4 Y RT: steps 3 and 4 are the two String members' own destructors
// (String::Release, the same null test and asymmetric arms), which run after
// this body in reverse declaration order: +0x60, then +0x58.
TextFileStream::~TextFileStream() {
    if (_buffer) RtSystem::RtMemFree(_buffer);                    // 1800ba4b9
}

// ============================================================================
// IO::CompressedFileStream::~CompressedFileStream        retail 0x1800a9330
//                                                  (WAVE 27 LANE IOTEXT)
// ============================================================================
// Read with objdump against C:/GOG Games/Banished/Runtime-x64.dll — the retail
// file on disk, addressed by true VA — after the integrator measured TWO Ghidra
// defects on this wave's session (a program-selection defect serving the profile
// build, and `disassemble_function` relabelling one body's listing with another
// body's address). objdump shares no mechanism with Ghidra, and stream classes
// are the "near-identical sibling" shape the second defect corrupts invisibly.
//
// Five steps then a tail jump:
//   1800a934b  stamp ??_7CompressedFileStream@IO@@6B@ (0x1801555f0)
//   1800a9355  if (headerSize > 0 && direction == 1) Commit(this)
//   1800a9366  rdi = &this->file                      (+0x68)
//   1800a936e  if (file.file.handle != -1) CloseFile(_singleton, &file.file)
//   1800a9388  ~FileStream(&this->file)
//   1800a939b  JMP ~CompressedStream(this)
//
// STEP 2 IS THE FLUSH, AND ITS GUARD IS TWO CONDITIONS, NOT ONE.
// `CMPL $0,0x48(%rcx) ; JLE` is `headerSize > 0` (Stream.h names +0x48 the
// header Array's _size and calls it "the guard" already), and
// `CMPL $1,0x8(%rcx) ; JNE` is `direction == 1`, i.e. WRITING. So a read-only
// compressed stream, and a writing one that never produced a header, both skip
// Commit. Dropping either half would write a block on teardown of a stream that
// never wrote one.
//
// STEP 4 CLOSES THE INNER STREAM'S FileInfo, NOT ITS OWN. `LEA RDX,[RDI+0x20]`
// with RDI = this+0x68 puts the FileInfo at this+0x88 — FileStream's own
// `file` member inside the embedded BinaryFileStream. And like ~FileStream it
// reaches the FileSystem through the exported singleton rather than through
// `this`, which is the shape filesystem_packages.cpp:387 already records.
//
// EVERY CALLEE IS OURS — the row CLOSES rather than deepening. Checked by
// grepping for each RegisterRuntimeAddr, not assumed:
//   0x1800a9ee0  CompressedStream::Commit  PORTED  compressed_stream_write.cpp:783
//   0x1800accc0  FileSystem::CloseFile     PORTED  filesystem_createfile.cpp:636
//   0x1800aaab0  ~FileStream               PORTED  this file, :867
//   0x1800aa650  ~CompressedStream         PORTED  compressed_stream_release.cpp:196
//
// WHY THIS ROW, given the census tags it INSTRUMENT-SOURCED: the same reason as
// ~TextFileStream above. Its only export-NAME caller is file_stream_probe.cpp,
// but deleting_dtor.cpp's row 40 ("IO::CompressedFileStream", slot 0 of
// `??_7CompressedFileStream@IO@@6B@`) reaches 0x1800a9330 BY ADDRESS, and a
// census that buckets a row by its name callers cannot see that.
//
// NO ORACLE, same reason as the two destructors above: a replay would Commit
// twice, close a closed handle and free the window arrays twice.
//
// ENDGAME B.3.9 IO: this is now `IO::CompressedFileStream`'s own destructor
// (steps 2 and 4; the stamp is the compiler's). Steps 5 and 6 are the member's
// ~BinaryFileStream and ~CompressedStream, which the compiler runs after the
// body. The ??1CompressedFileStream door and its RT_DELDTOR row are gone: the
// class's inline DeletingDtor is slot 0, and External::Load (the one
// construction site) ends its stack instance through it.
IO::CompressedFileStream::~CompressedFileStream() {

    if (_headerSize > 0 &&                                              // 1800a9355
        _direction == 1) {                                              // 1800a935b
        Commit();                                                       // 1800a9361
    }

    // The member BinaryFileStream's FileInfo (this + 0x68 + 0x20).
    RtIO::FileInfo* const inner = &_file._file;                         // 1800a9366
    if (inner->handle != RtIO::FileInfo::kHandleNone) {                 // 1800a936e
        ::IO::FileSystem::_singleton->CloseFile(*inner);                // 1800a937a
    }
}                                                                       // 1800a9388 / 1800a939b

}  // namespace IO


// ============================================================================
// Registration for the slot body in this file that keeps a door.
// ============================================================================
// ENDGAME B.3.9 IO: FileStream / BinaryFileStream / TextFileStream objects are
// built by their real constructors and wear the compiler's tables, so the slot
// doors whose only caller was a minted table are gone (GetSource, Read,
// GetCurrentPos, GetSize, the two slot-0 thunks 0x18000c090 / 0x180023d80, the
// BinaryFileStream copy ctor, and file_stream_write.cpp's Write / SetCurrentPos).
// `?Open@FileStream@` STAYS: Audio::Stream::Run and the persistence loader call
// it by name on a FileStream they built through `??0FileStream`.
// ENDGAME B.3.9 IO (W2): RtIoFileStreamSlotsRegister is gone with the one
// door it registered.
