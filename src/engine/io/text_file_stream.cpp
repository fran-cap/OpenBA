// ============================================================================
// src/engine/io/text_file_stream.cpp — Wave 20, lane SAVETEXT
//   Five of the eight unported slots of `??_7TextFileStream@IO@@6B@`.
// ============================================================================
//
//   slot [3] +0x18  ?Read@TextFileStream@IO@@UEAA_NHPEAX@Z          0x1800ba900
//   slot [4] +0x20  ?SetCurrentPos@TextFileStream@IO@@UEAA_NH@Z     0x1800babd0
//   slot [5] +0x28  ?GetCurrentPos@TextFileStream@IO@@UEBAIXZ       0x1800bac30
//   slot [6] +0x30  ?GetSize@TextFileStream@IO@@UEBAIXZ             0x1800ba8e0
//   slot [9] +0x48  ?Create@TextFileStream@IO@@UEAA_NAEBVFilename.. 0x1800ba940
//
// Slots [1] and [7] are FileStream's `GetSource` (0x1800aab50) and `Open`
// (0x1800aac70) and were ALREADY PORTED by Wave 19 lane FILE — verified here in
// all three forms before writing a line (mangled name, retail VA, and the
// `RtIo_FileStream_<Method>` convention; file_stream.cpp:685/784 and :958/974).
// They are not re-ported. Slots [0], [2] and [8] are NOT in this file; the
// DEFERRED section at the bottom says exactly why, each with its own reason.
//
// ---------------------------------------------------------------------------
// THE ONE FACT THAT ORGANISES ALL FIVE: TextFileStream IS TWO STREAMS
// ---------------------------------------------------------------------------
// `TextFileStream::buffer` (+0x70) is the discriminator, and FOUR of these five
// bodies branch on it:
//
//     buffer != 0   -> an in-MEMORY stream over that buffer,
//                      size at +0x78, cursor at +0x7c
//     buffer == 0   -> a FILE stream, delegating to the inherited FileInfo,
//                      size at +0x28, cursor at +0x2c
//
// That pairing is what pins +0x78 as the buffer's SIZE and +0x7c as its CURSOR:
// `GetSize` returns +0x78 or +0x28 from the two arms of one branch and
// `GetCurrentPos` returns +0x7c or +0x2c from the two arms of another, so each
// buffer field is identified by the inherited field it stands in for, and those
// two inherited offsets are independently asserted in FileStream.h.
//
// FileStream.h models +0x78 as `void* _unk78`, "zeroed by the ctor, never
// freed". THESE BODIES SHOW IT IS AN int32 SIZE, and the "never freed" half
// stops being an observation and becomes a consequence: it is not a pointer, so
// there is nothing to free. The header is not edited by this lane — the field
// is read here through an explicit offset rather than through the member, so
// that the disagreement is visible at the point of use instead of being quietly
// resolved in a shared header that other lanes' bodies also compile against.
//
// `Read` (slot 3) is the exception: it does NOT branch. It reads from the
// buffer unconditionally, with no null check — see its own note.
//
// ---------------------------------------------------------------------------
// THE DIRECT OS CALLS (this file was `text_file_stream_write.cpp` until Phase D.4)
// ---------------------------------------------------------------------------
// `SetCurrentPos` calls `SetFilePointer` (import 0x18014b0f0) and `Create`
// calls `WriteFile` (import 0x18014b108), both DIRECTLY rather than through any
// stream virtual — the same bypass `?SaveText@External@Object@@` makes. Since
// THREADS E.5 they are Platform::FileSeek32 / FileWrite (platform_file.h).
// ============================================================================

#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstring>

#include "platform_file.h"   // Platform::FileSeek32 / FileWrite

#include "io/FileStream.h"
#include "io/FileSystem.h"
#include "system/String.h"
#include "system/Filename.h"
#include "system/MemoryAllocator.h"
#include "io/Stream.h"            // the four RtIo_TextCodec_* bodies

namespace RtIO {

// THE TWO NAMESPACES ARE ONE LETTER APART AND BOTH EXIST. The stream TYPES live
// in `RtIo` (src/engine/io/FileStream.h:69) and the ported BODIES live in
// `RtIO` (this namespace, and file_stream.cpp:166). `FileInfo` is in `RtIO`.
// file_stream.cpp:176-179 carries the same four using-declarations for the same
// reason; without them `TextFileStream` does not name a type inside `RtIO` and
// the error points at the parameter list rather than at the namespace.
using RtIo::Stream;
using RtIo::FileStream;
using RtIo::TextFileStream;

// `?Create@FileStream@IO@@IEAA_NAEBVFilename@System@@@Z` (0x1800aa8f0) — PORTED
// and registered by lane FILE (file_stream.cpp:483 and :559). Called DIRECTLY,
// not through a door: a door would resolve to this same body and add a resolve
// site the cutover census has to carry
// (reference_count_the_doors_not_the_calls.md).

// ---- Wave 28 lane SLOTS: the callees slots 2 and 8 need -------------------
// All three are PORTED and are called DIRECTLY rather than through a door, for
// the reason stated above `RtIo_FileStream_Create`: a door would resolve to
// these same bodies and add a resolve site the cutover census has to carry
// (reference_count_the_doors_not_the_calls.md).
//   ?Open@FileStream@IO@@UEAA_NAEBVFilename@System@@@Z  0x1800aac70  file_stream.cpp
//   ?ReadFile@FileSystem@IO@@QEAA_NAEAUFileInfo@12@HPEAX@Z 0x1800ace00
//                                                    filesystem_openfile.cpp
//   ?CloseFile@FileSystem@IO@@QEAAXAEAUFileInfo@12@@Z  0x1800accc0
//                                                    filesystem_createfile.cpp
// B.4 R-IO: all three are members, called by name; the two FileSystem ones on
// `IO::FileSystem::_singleton`, read exactly where retail reads it
// (`MOV RCX,[0x1801fc6a8]`).

// THE FOUR `TfsBufSize` / `TfsBufCursor` OFFSET HELPERS ARE GONE — B.3.4
// STREAMS (4/6). They existed because the `RtIo::TextFileStream` POD models
// +0x78 as one `void* _unk78` while these bodies read an int32 SIZE at +0x78
// and an int32 CURSOR at +0x7c, and Wave 20 deliberately reached past the POD
// rather than edit a header other lanes' bodies compile against.
//
// `IO::TextFileStream` now DECLARES the pair (FileStream.h), pinned by exactly
// the evidence these bodies supply — GetSize returns +0x78 or the inherited
// FileInfo::size, GetCurrentPos returns +0x7c or the inherited FileInfo::cursor
// — so the member bodies below spell `_bufSize` and `_bufCursor` and the
// disagreement is RESOLVED rather than routed around. The POD is unchanged and
// is cross-asserted against the pair in the header. Same eight bytes, one
// fewer indirection, and nothing outside this file ever named the helpers.

// ---------------------------------------------------------------------------
// EFFECTIVENESS COUNTERS. `calls` alone cannot separate a body that ran from
// one that did anything
// (reference_calls_zero_can_mean_always_took_the_uncovered_branch.md), so every
// body here counts WHICH ARM it took. For the four that branch on `buffer` the
// two arms are counted separately; a run where one arm reads 0 has exercised
// the other arm only, and says so.
// ---------------------------------------------------------------------------
struct TfsCounters {
    uint64_t read;                             // slot 3
    uint64_t setPosBuffer, setPosFile;         // slot 4, by arm
    uint64_t setPosSeek;                       // ... and the Win32 sub-arm
    uint64_t getPosBuffer, getPosFile;         // slot 5, by arm
    uint64_t getSizeBuffer, getSizeFile;       // slot 6, by arm
    uint64_t create, createFailed, createBom;  // slot 9
    // Wave 28 lane SLOTS. `Write` counts its two arms separately because they
    // are two different functions sharing an entry point and their RETURN
    // VALUES are not even comparable (see its banner); a run where
    // `writeTranscode` reads 0 has exercised only the pass-through.
    uint64_t write, writeDirect, writeTranscode;  // slot 2, by arm
    uint64_t open, openFailed;                    // slot 8
};
TfsCounters g_tfs{};

// ---------------------------------------------------------------------------
// NEGATIVE-CONTROL PERTURBATIONS, set only by text_file_stream_probe.cpp and
// read HERE, in the bodies under test. They live in the body rather than in the
// probe because a control that perturbs a COPY of the code catches in a screen
// it cannot reach (WAVE11_LANE_OBJEXT.md 10.3/10.5). Each is consumed at
// exactly one site, each site increments `g_tfsAtt`, and each produces a
// COMPARABLE DIVERGENCE against the shipped export -- never undefined
// behaviour.
//
//   1  GetSize: drop the `buffer` branch and always return the FILE size.
//      -> screen A. This is the mistake a port that ignored +0x70 would make.
//   2  SetCurrentPos (buffer arm): `pos <= size` instead of `pos < size`.
//      -> screen B, and ONLY at pos == size exactly. An off-by-one that no
//      other value of `pos` can expose.
//   3  Read: omit the `bufCursor += len` advance.
//      -> screen C, on POST-STATE only; the bytes copied are still right, so a
//      screen that compared only the destination buffer would pass.
//   4  Create: select the byte-order mark from `_requestedFormat` (+0x68)
//      instead of `_activeEncoding` (+0x6c).
//      -> screen D, and only when the two are passed DIFFERENT values. This is
//      the precise confusion the field pair invites and the reason this probe
//      passes them apart.
// ---------------------------------------------------------------------------
int      g_tfsCtl = 0;
uint64_t g_tfsAtt = 0;

// ============================================================================
// IO::TextFileStream::GetSize() const                    retail 0x1800ba8e0
// IO::TextFileStream::GetCurrentPos() const              retail 0x1800bac30
// ============================================================================
// Six instructions each, and the SAME six-instruction shape:
//
//     CMPQ $0,[RCX+0x70] ; JE file ; MOV EAX,[RCX+0x78 or 0x7c] ; RET
//     file: MOV EAX,[RCX+0x28 or 0x2c] ; RET
//
// NEITHER HAS A `.pdata` ENTRY — both are leaf functions with no prologue, so
// the unwinder needs none. Worth stating because `tools/mint_extent_audit.py`
// and every other `.pdata`-driven instrument CANNOT SEE THESE TWO FUNCTIONS AT
// ALL: asked "which function owns 0x1800ba8e0?" it has no answer, and a reader
// who takes that silence for "no such function" concludes the slot is empty.
// The vtable is the authority here and the vtable names both.
//
// Both return UNSIGNED (`UEBAIXZ`) while `FileInfo::cursor` and `::size` are
// modelled `int32_t` — identical bits, and the return TYPE is what the caller's
// comparison sees. Same note as FileStream's pair, and the same reason it is
// only visible in the export directory: a one-instruction body cannot show a
// return type.
// ============================================================================
// (ENDGAME B.3.9 IO: the two slot doors that delegated here went with the
// minted table; the bodies are the members at the bottom of this file.)

// ============================================================================
// IO::TextFileStream::Read(int, void*)                   retail 0x1800ba900
// ============================================================================
//     memmove(buf, (char*)this->buffer + this->bufCursor, len);
//     this->bufCursor += len;
//     return true;
//
// THREE THINGS THIS BODY DOES NOT DO, each of which a port written from the
// signature rather than from the bytes would add:
//
//  1. IT DOES NOT BRANCH ON `buffer`. Unlike its three neighbours it reads the
//     buffer unconditionally. With `buffer == 0` and `bufCursor == 0` that is a
//     read from address 0 — retail's, not ours, and NOT to be guarded (the
//     project's "don't fix a faithful crash with a null check" rule). The
//     asymmetry is real and is why a `TextFileStream` cannot be read from until
//     something has installed a buffer; `?Open@TextFileStream@` (slot 8, NOT in
//     this file) is what installs one.
//  2. IT DOES NOT BOUNDS-CHECK. `bufCursor + len` is never compared against the
//     size at +0x78. `SetCurrentPos` range-checks and `Read` does not.
//  3. IT ALWAYS RETURNS TRUE. `MOV AL,1` at 0x1800ba92e is unconditional —
//     there is no failure path, and the return value carries no information.
//
// THE COPY IS `memmove`, NOT `memcpy`. 0x18008a290 is the DLL's statically
// linked `memmove`; src/engine/system/error.cpp:152 established that from its
// prologue (the `SUB RDX,RCX / JAE` overlap test at 0x18008a2a0, which a
// `memcpy` does not have) and src/engine/save/saveload_loadmeta.cpp:342 says
// it again. A `std::memcpy` here would be UB for an overlapping range that
// retail handles, and the compiler is free to make that difference visible.
//
// NOTE, NOT ACTED ON: src/engine/ui/spritecontext_flush.cpp:146 calls
// 0x18008a290 "the DLL's `memcpy`". That comment disagrees with the two above
// and with the prologue. It is another row's file and this lane has not touched
// it; flagged for whoever owns it.
// ============================================================================

// ============================================================================
// IO::TextFileStream::SetCurrentPos(int)                 retail 0x1800babd0
// ============================================================================
// The two arms answer the same question in two different ways:
//
//   buffer:  bufCursor = pos   (ALWAYS, even when out of range)
//            return 0 <= pos < bufSize        <- a RANGE TEST, no I/O at all
//   file:    file.cursor = pos (ALWAYS, on both sub-arms, before the branch)
//            packed handle -> return true     <- no Win32 call at all
//            -1 or -2      -> return SetFilePointer(winHandle,pos,0,0) == pos
//
// THE FILE ARM IS `?SetCurrentPos@FileStream@IO@@` (0x1800aac10), which
// file_stream_write.cpp:128 already ports. It is TRANSCRIBED here rather than
// delegated to that body: retail duplicates the code rather than calling it, so
// delegating would attribute this slot's calls to the other slot's counters and
// would make an A/B on `the old DLL-era switch` silently move this row too.
//
// The discriminator is `CMP dword [RCX+0x20],-0x3 ; JA` and it is **UNSIGNED**:
// the Win32 path is taken when `handle` is -1 or -2 ("nothing opened" and
// "loose file"), the early `return true` when it is a PACKED
// `(pack << 24) | fileIndex` descriptor. Read as a SIGNED test it inverts, and
// every packed seek issues a `SetFilePointer` on a handle that is not one.
// FileSystem.h documents the same `CMP ..,-3 ; JA` idiom in `ReadFile`; this is
// a second, independent body using it.
//
// THE BUFFER ARM'S RANGE TEST IS `pos < size`, NOT `pos <= size`
// (`CMP EDX,[RCX+0x78] ; JGE fail` at 0x1800babe6), so seeking exactly to the
// end reports FALSE while still moving the cursor there. Both halves matter: a
// caller that seeks to EOF and trusts the return would stop early, and one that
// ignores the return would still be correctly positioned.
// ============================================================================

// ============================================================================
// IO::TextFileStream::Create(Filename const&, Text::Format, Text::Format)
//                                                        retail 0x1800ba940
// ============================================================================
// THE ARGUMENT LIST IS TWO FORMATS, NOT A FORMAT AND A FILENAME. The mangled
// name ends `W4Format@Text@4@1@Z`, and that trailing `1` is a BACK-REFERENCE to
// back-reference-table entry 1, which is `W4Format@Text@4@` itself — entry 0 is
// `AEBVFilename@System@@`. So the signature is
// `(Filename const&, Text::Format, Text::Format)`.
//
// The DISASSEMBLY is the check that matters, and it agrees:
// `MOV [RCX+0x68],R8D ; MOV [RCX+0x6c],R9D` stores BOTH of the last two
// arguments as DWORDS, which a `Filename const&` arriving in R9 could not be.
// Read as `(Filename const&, Format, Filename const&)`, a port would truncate a
// pointer into `_activeEncoding` and then pick a byte-order mark out of the low
// half of an address.
//
// So the two format arguments land exactly on the pair FileStream.h names:
//     R8D -> +0x68 `_requestedFormat`      R9D -> +0x6c `_activeEncoding`
// and this is a THIRD, independent source for those being two separate fields.
// The constructor writes both to 6 and `?SaveText@External@Object@@` writes both
// to 2 — NEITHER of those can tell the two apart. A caller that can pass
// DIFFERENT values to them can, and this is that caller.
//
// The body is four steps:
//   1. store both formats                                    1800ba949/4d
//   2. `FileStream::Create(path)`; on false RETURN FALSE, no BOM    1800ba951
//   3. the byte-order mark, selected by `_activeEncoding` ALONE:
//        0 or 2 -> none    3 -> EF BB BF    4 -> FF FE    5 -> FE FF
//      written with a DIRECT `WriteFile` on `file.winHandle`, bypassing the
//      stream's own `Write` virtual, with the byte count added to `file.cursor`
//      by hand. Retail's chain is `test / sub 2 / dec / dec / dec`, so any value
//      outside {0,2,3,4,5} also falls through to "none".
//   4. `return true` — UNCONDITIONALLY, and NOT the result of `WriteFile`
//      (`MOV AL,1` at 0x1800ba9d6 is the join point of the no-BOM and the BOM
//      paths alike). A failed mark is invisible to the caller.
//
// THIS IS THE SAME BOM TABLE `?SaveText@External@Object@@` CARRIES, reached from
// a different entry point: external_savetext.cpp transcribed it from
// 0x1800bbaba..0x1800bbaca and this body has it at 0x1800ba978..0x1800ba9a0.
// The two agree, and that is the FIRST CORROBORATION that table has had —
// SaveText's copy is unreachable as shipped (it hard-sets `_activeEncoding` to 2
// eleven instructions earlier) and rested on inspection plus one negative
// control. Create's copy is reachable by any caller that passes a format.
//
// RETAIL BUILDS THE MARK IN A WAY WORTH DESCRIBING EXACTLY, because the obvious
// reading of the disassembly is wrong. It writes the two leading bytes to a
// scratch at RSP+0x50, the UTF-8 third byte 0xBF DIRECTLY to RSP+0x5a, then
// copies only the two scratch bytes to RSP+0x58 and passes RSP+0x58 to
// `WriteFile`. RSP+0x58..0x5a is therefore the real buffer, and its third byte
// was placed before its first two. The scratch at RSP+0x50 is then REUSED as
// `lpNumberOfBytesWritten` (zeroed at 0x1800ba9c0, after R9 already points at
// it). Modelled below as one 3-byte buffer plus a separate counter, which is
// what the machine ends up doing; the aliasing is retail's stack economy, not
// semantics, and nothing observable depends on it.
// ============================================================================

// ============================================================================
// WAVE 28 LANE SLOTS — the last two slots of ??_7TextFileStream@IO@@6B@
// ============================================================================
//
//   slot [2] +0x10  ?Write@TextFileStream@IO@@UEAA_NHPEBX@Z         0x1800ba9f0
//   slot [8] +0x40  ?Open@TextFileStream@IO@@UEAA_NAEBVFilename@
//                     System@@W4Format@Text@4@@Z                    0x1800ba640
//
// With these the table is 10/10. See the DEFERRED block below, which is now the
// history of these two rather than a live holdback: Wave 20 lane SAVETEXT and
// Wave 27 lane IOTEXT both declined them as SIZE JUDGEMENTS with no premise to
// expire, and a size judgement is exactly the kind a later lane is free to
// overturn by doing the work.
//
// ---------------------------------------------------------------------------
// THE SCOPE DECISION THIS LANE WAS ASKED TO MAKE, RECORDED AT THE CODE
// ---------------------------------------------------------------------------
// The census lists `?GetObjectName@`, `?GetObjectType@` and `?GetObjectVersion@`
// as TextFileStream BODY rows reached only by `file_stream_probe.cpp`. **They
// are NOT slots of this table and are NOT ported.** The table's ten slots were
// dumped straight out of retail's `.rdata` with pefile — independently of
// `rt_data_mint_tables.inc`, which is GENERATED and therefore one source, not
// two — and none of the three appears in it. They are `QEBA` non-virtual const
// accessors, a `LEA` and a `RET` each, and porting them would move three census
// rows while satisfying nothing
// (`reference_cheap_wins_list_is_the_stalest_section.md`).
//
// ---------------------------------------------------------------------------
// THE FOUR HELPERS BOTH BODIES SHARE — all four UNEXPORTED, all four DOORS
// ---------------------------------------------------------------------------
// This lane does NOT reimplement the codec tables. Both bodies pass a RUNTIME
// encoding id in 0..5 to two jump-table dispatchers, so a port from spec would
// have to reproduce all six entries of both tables; `string.cpp`,
// `widestring.cpp` and `package_file_read.cpp` each wrote only the ONE OR
// TWO entries their caller could reach and said so. Here the id is a variable,
// so the honest shape is a door.
//
// *** ALL FOUR ARE PORTED AS OF BATCH 8b (2026-09-12) ***
// src/engine/io/text_codec.cpp carries all four, and the four `ResolveAddr`
// helpers below now land on OUR bodies because the resolver is registry-first;
// the call sites are unchanged. What made this smaller than the paragraph above
// expected: the two "dispatchers" are not dispatchers onto twelve separate
// bodies. Both jump tables were read out of the image, and EVERY entry points
// back INSIDE the dispatching function - they are MSVC switch arms, four
// distinct ones per table (ids 2/3 share the UTF-8 arm, 4/5 the UTF-16 arm). So
// the work was two functions with a switch each, not twelve ports from spec.
// The ADDRESSEE block further down is left in place with its own correction.
//
//   0x1800c9d60  DetectFormat(src, len) -> format id
//                Compares a 3-byte "EF BB BF" then a 2-byte "FF FE" then
//                "FE FF" against the head of the buffer; returns 3, 4, 5 or 0.
//   0x1800c9fc0  DecodeOne(fmt, src, end, &cp, fmt) -> bytes consumed
//                A `JMP` through the table at 0x1800ca1b8 indexed by the FIFTH
//                argument. **The format is passed TWICE — once in ECX and once
//                as arg5** (`MOVSLQ 0x30(%rsp),%rax` selects the entry;
//                `MOV %ECX,%R11D` is what some entries read). Both are
//                reproduced; passing only one would index entry 0 for every
//                encoding.
//   0x1800c9b60  EncodeOne(cp, dst, fmt) -> bytes written
//                `JMP` through the table at 0x1800c9d40 indexed by R8D.
//   0x1800ca260  ConvertText(srcFmt, forceDetect, src, srcLen,
//                            dstFmt, <scratch>, flagByte, dst, dstLen)
//                Nine arguments. Arg6 (`[rsp+0x28]` at the call site) is NEVER
//                WRITTEN BY THE CALLER and that is CORRECT, not a bug: the
//                callee uses `0x88(%rsp)` — which is arg6's slot — as its own
//                codepoint SCRATCH, writing `$0xffffffff` into it before every
//                read. MSVC spilled a local into the incoming argument area.
//                A port that "fixed" this by zeroing arg6 would change nothing;
//                a port that omitted the slot would shift args 7, 8 and 9.
//
// Each is spelled as a LITERAL inside its own `ResolveAddr` call, never through
// a shared wrapper: `runtime_census.py` and `addr_door_audit.py` both find doors
// by grepping for that literal and a door reached through a variable is
// invisible to both (`reference_literal_grep_blind_to_variables.md`).
//
//   CONDITION   live whenever slot 2 or slot 8 runs with a transcode to do.
//   CONSEQUENCE with the DLL present each resolves to the shipped body.
//               At cutover all four are NULL and both slots fault.
//
// > **[ADDRESSEE — the four text-codec internals 0x1800c9d60, 0x1800c9fc0,
// > 0x1800c9b60, 0x1800ca260]** ~~the Wave 29 lane that takes IO's remaining
// > `internal` address rows~~ — **DISCHARGED, BATCH 8b.** All four, plus the
// > 0x180029bb0 array-of-String serializer this block named as the same
// > transcoder family, are ported and registered in
// > src/engine/io/text_codec.cpp. The prediction that "roughly half of both
// > tables is already written down in this tree" was true and was not what the
// > port needed: the tables' entries are switch arms inside the two functions,
// > so they were transcribed from those two bodies rather than collected from
// > the four files that had guessed at individual entries. Of those, only
// > `package_file_read.cpp` §94's two entries were read from the image; the
// > `string.cpp` / `widestring.cpp` spec versions are independent of this port
// > and are left alone.
//
// ---------------------------------------------------------------------------
// ONE TABLE, READ THREE TIMES, AGREEING — the BOM lengths
// ---------------------------------------------------------------------------
// Both bodies decide how many bytes of byte-order mark to SKIP with the same
// ladder: format 3 -> 3 bytes, 4 -> 2, 5 -> 2, everything else -> 0. That is
// the mirror of the table `?Create@TextFileStream@` above WRITES (3 -> EF BB BF,
// 4 -> FF FE, 5 -> FE FF) and of the one `?SaveText@External@Object@@` carries.
// `DetectFormat` itself returns 3 after matching EF BB BF and 4 after matching
// FF FE, which is a FOURTH witness and the only one that is neither a writer
// nor a skipper. Nothing here rests on a single reading.
//
// ---------------------------------------------------------------------------
// NO ORACLE FOR EITHER, and the refusals are structural
// ---------------------------------------------------------------------------
// `Write` ends in a real `WriteFile` on a real handle; a replay would emit
// every byte TWICE and advance the cursor twice. `Open` reads the whole file,
// CLOSES it, and installs an owned buffer at `+0x70`; a replay would run
// against a closed handle and leak the first buffer, and the second call could
// not read the same bytes because the first one closed the file. Neither is
// diffable in either direction. `the diff oracle` records liveness only.
//
// UNVERIFIED: neither body has been observed executing.
// ============================================================================

// ---- the four unexported codec bodies --------------------------------------
// 0x1800c9d60 / 0x1800c9fc0 / 0x1800c9b60 / 0x1800ca260 are
// RtIo_TextCodec_DetectFormat / DecodeOne / EncodeOne / ConvertText
// (text_codec.cpp), called by name (B3R RT-IO; they were address doors).

// The shared BOM-skip ladder. Retail inlines it three times (twice in `Open`,
// once in `Write`) as `test / sub 2 / dec / dec / dec`, so every value outside
// {3,4,5} — INCLUDING 1 and 6 — falls through to 0.
inline int32_t TfsBomSkip(IO::Text::Format detected) {
    switch (detected) {
        case IO::Text::Format::Utf8Bom:  return 3;   // EF BB BF
        case IO::Text::Format::Utf16Le:  return 2;   // FF FE
        case IO::Text::Format::Utf16Be:  return 2;   // FE FF
        default: return 0;
    }
}

// `?MemAlloc@MemoryAllocator@System@@SAPEAX_KH@Z` 0x1800c7a50 and
// `?MemFree@MemoryAllocator@System@@SAXPEAX@Z` 0x1800c7ad0: called by name,
// System::MemoryAllocator::MemAlloc / MemFree (B3R RT-IO).

// ============================================================================
// IO::TextFileStream::Write(int, void const*)            retail 0x1800ba9f0
// ============================================================================
// TWO BODIES IN ONE, and the discriminator is the FIRST thing it reads:
//
//     _requestedFormat (+0x68) == _activeEncoding (+0x6c)
//         -> a straight WriteFile of the caller's bytes. No allocation, no
//            codec, no buffer. 22 instructions.
//         -> return (written == len)
//     otherwise
//         -> transcode: measure, allocate, convert, WriteFile, free.
//         -> return (written == convertedLength)
//
// **The return values of the two arms are NOT comparable**: the fast arm
// compares against the CALLER'S length and the slow arm against the CONVERTED
// length, which is a different number whenever the encodings differ in width.
// A port that hoisted one `return written == len` out of the branch would be
// wrong on every UTF-8 -> UTF-16 write and right on every other.
//
// THE MEASURE PASS ENCODES INTO THE CODEPOINT SLOT ITSELF (`LEA 0x90(%rsp),%RDX`
// is the same address it just read the codepoint from). Only the RETURNED
// LENGTH is used, so the encoded bytes are deliberately thrown away — retail is
// sizing the output, not producing it. Reproduced exactly; giving the measure
// pass its own scratch would be harmless here and would hide that the second
// pass (in `Open`) uses a DIFFERENT slot precisely because it does keep bytes.
//
// `_requestedFormat == 6` means "detect from the data" and only then is
// `DetectFormat` consulted; the detected format both selects the decoder and
// decides how many BOM bytes to skip.
// ============================================================================

// ============================================================================
// IO::TextFileStream::Open(Filename const&, Text::Format)  retail 0x1800ba640
// ============================================================================
// The largest body in the table, and the one that INSTALLS the buffer four of
// the other bodies branch on. Eight steps, in retail's order:
//
//   1. `FileStream::Open(path)` (slot 7, 0x1800aac70, already ours). FALSE ->
//      return FALSE immediately, with nothing allocated and no buffer installed.
//   2. `raw = MemAlloc(file.size, 4)`; `file.cursor = 0`.
//   3. If `file.handle` is -1 or -2 (UNSIGNED `CMP ..,-3 ; JBE` — the same
//      idiom FileStream.h documents), `SetFilePointer(winHandle, 0, 0, FILE_BEGIN)`.
//      A PACKED `(pack << 24) | index` descriptor skips the seek.
//   4. `FileSystem::ReadFile(_singleton, &file, size, raw)` — THE WHOLE FILE,
//      in one call, into a block sized from `file.size`.
//   5. If `file.handle != -1`, `FileSystem::CloseFile(_singleton, &file)`.
//      **The file is closed here, inside Open.** Everything after this point
//      works on memory. This is why `Read` (slot 3) never touches the handle.
//   6. Pass 1: detect the source format, skip its BOM, and MEASURE the output.
//   7. `buffer = MemAlloc(outBytes, 4)` and `bufSize = outBytes`, stored at
//      +0x70 / +0x78 — the two fields the rest of the class branches on.
//   8. Pass 2: **`DetectFormat` IS CALLED A SECOND TIME**, on the same bytes,
//      and its result is re-derived rather than carried in a register.
//      Transcode into the new buffer, free `raw`, set `_activeEncoding` (+0x6c)
//      to the requested format, return TRUE.
//
// THREE THINGS A PORT WRITTEN FROM THE SHAPE RATHER THAN THE BYTES GETS WRONG:
//
//  a) **Pass 2 encodes into a DIFFERENT scratch from pass 1.** Pass 1 uses the
//     codepoint slot itself (it is only sizing); pass 2 uses a separate 4-byte
//     slot and then `memmove`s exactly `n` bytes out of it into the buffer.
//     Reusing the codepoint slot in pass 2 would work by accident today and is
//     not what the machine does.
//  b) **The copy is `memmove`, not `memcpy`** — 0x18008a290, established from
//     its prologue by `src/engine/system/error.cpp:152` and repeated by
//     `saveload_loadmeta.cpp:342` and by `Read` above.
//  c) **`_requestedFormat` (+0x68) IS NOT WRITTEN.** Only `_activeEncoding`
//     (+0x6c) is, and only at the very end. A stream opened this way keeps
//     whatever +0x68 the constructor left (6), which is exactly what makes a
//     later `Write` take its transcoding arm. Absences are as load-bearing as
//     presences.
//
// THE OVERFLOW GUARD IS RETAIL'S, NOT A PORT'S CAUTION: pass 2 stops copying
// once the running total exceeds `bufSize`, and separately skips the copy when
// the destination allocation came back NULL — but it keeps DECODING to the end
// in both cases. The two passes can disagree about the output size only if
// `DetectFormat` returns something different the second time, which on the same
// bytes it cannot; the guard is there anyway and is reproduced anyway.
// ============================================================================


// ============================================================================
// WAVE 29 LANE IOLANE — the three NON-VTABLE `TextFileStream` accessors
// ============================================================================
// IO::TextFileStream::GetObjectVersion() const           retail 0x1800ba250
// IO::TextFileStream::GetObjectType()    const           retail 0x1800ba260
// IO::TextFileStream::GetObjectName()    const           retail 0x1800ba270
//
// THESE ARE NOT VTABLE SLOTS. All three are `QEBA` (public non-virtual const),
// they appear nowhere in `??_7TextFileStream@IO@@6B@` (whose ten slots are
// enumerated at the bottom of this file), and they are reached only by an
// explicit call on a concrete TextFileStream.
//
// ---- RETAIL'S BYTES, read with objdump on the shipped retail DLL -----------
// Not read out of Ghidra: Ghidra's decompiled C drops dereferences and prints
// MOVSXD as a zero extension, and for a one-instruction body there is nothing
// left to check it against.
//
//   1800ba250:  8b 41 50        mov    0x50(%rcx),%eax    ; GetObjectVersion
//   1800ba253:  c3              ret
//   1800ba260:  48 8d 41 60     lea    0x60(%rcx),%rax    ; GetObjectType
//   1800ba264:  c3              ret
//   1800ba270:  48 8d 41 58     lea    0x58(%rcx),%rax    ; GetObjectName
//   1800ba274:  c3              ret
//
// Two instructions each, `int3` padding on both sides, no `.pdata` entry (leaf,
// no prologue) — so every `.pdata`-driven extent instrument in `tools/` is
// blind to all three, exactly as it is to GetSize/GetCurrentPos above. The
// EXTENT here is DERIVED from the instruction stream (ret at +3 and +4), not
// from `.pdata`, which for these three has nothing to understate.
//
// ---- THE RETURN CONVENTION, and why the wave's headline trap does NOT apply -
// The kickoff and this lane's brief both warned that a `String` return means a
// HIDDEN RDX RETURN SLOT and that an `fn_*` typedef omitting it is a wild write
// that still returns the right answer. **That trap is not present on any of
// these three, and the mangling says so before the bytes do:**
//
//     ?GetObjectName@...@QEBA AEBV String@System@@ XZ   AEBV = REF-to-const
//     ?GetObjectType@...@QEBA AEBV String@System@@ XZ   AEBV = REF-to-const
//     ?GetObjectVersion@...@QEBA H XZ                   H    = int
//
// `?AV` is the by-value spelling and is what carries the hidden slot; `AEBV` is
// a reference and is returned in RAX like any pointer. The bytes agree
// independently: neither body READS RDX at all, and a `?AV` body cannot avoid
// reading it — it must copy the caller's slot address into RAX to return it.
// Two mechanisms, same answer, and the second does not depend on my reading of
// the first. So the correct C signature returns a POINTER and takes exactly one
// argument. (`?GetSource@Stream@IO@@UEBA?AVString@System@@XZ`, already ported
// in this tree, is the `?AV` shape and is the known positive I handed lane
// AVBODY's screen.)
//
// ---- THE TWO FIELDS ARE THE OTHER WAY ROUND FROM THE NAMES ----------------
// `GetObjectType` returns +0x60 and `GetObjectName` returns +0x58 — the LOWER
// offset is the NAME. That is counter-intuitive and it is already established
// twice over in src/engine/io/FileStream.h:274-287 (from these three bodies
// AND, independently, from `?SetObject@` writing its FIRST String argument to
// +0x60 and its SECOND to +0x58). This port is transcription of what that
// header already asserts with `static_assert`, so a layout drift breaks the
// BUILD rather than these bodies.
//
// ---- RETAIL SHIPS EMPTY BODIES — checked, these are not that ---------------
// Kickoff warning 5. An abstract/empty body is `ret` (or `xor eax,eax ; ret`)
// and nothing else. All three of these DEREFERENCE `this` at a named offset and
// two of them take its address, so each does observable work. Not empty.
//
// ---- THE ORACLE ------------------------------------------------------------
// `the diff oracle(ours, the raw DLL door<...>)`: `the raw DLL door` is `the raw export door`,
// i.e. GetProcAddress against the SHIPPED DLL, which never consults the port
// registry — so this compares OUR body with RETAIL'S body and not with itself
// (reference_a_probe_whose_ours_side_reaches_the_body_by_address..., and
// reference_shadow_marker_is_not_an_oracle.md). It costs a GetProcAddress per
// call and that is only acceptable because `the diff oracle` evaluates nothing
// at all unless `the old DLL-era switch` (the Wave 2 5.6-point perf breach was
// exactly this call OUTSIDE a diff guard).
//
// Read the `calls=` column and not only `divergences=`: a port with no caller
// reports clean. And `calls=0` with `worked>0` would mean the body ran every
// time and always took a branch the oracle does not cover — which here it
// cannot, since these bodies have no branches at all.
// ============================================================================
using FnGetObjStr = char* const* (*)(const TextFileStream*);
using FnGetObjVer = int32_t      (*)(const TextFileStream*);

extern "C" int32_t RtIo_TextFileStream_GetObjectVersion(const TextFileStream* self) {
    const int32_t ours = self->objectVersion;                   // 1800ba250  +0x50
    return ours;                                                // 1800ba253
}

extern "C" char* const* RtIo_TextFileStream_GetObjectType(const TextFileStream* self) {
    char* const* ours = &self->objectType;                      // 1800ba260  +0x60
    return ours;                                                // 1800ba264
}

extern "C" char* const* RtIo_TextFileStream_GetObjectName(const TextFileStream* self) {
    char* const* ours = &self->objectName;                      // 1800ba270  +0x58
    return ours;                                                // 1800ba274
}

// Read by text_file_stream_probe.cpp so attendance prints beside the verdicts
// and a reader cannot see one without the other.
void RtIoTextFileStreamCounters(unsigned long long* out) {
    out[0]=g_tfs.read;
    out[1]=g_tfs.setPosBuffer;  out[2]=g_tfs.setPosFile;   out[3]=g_tfs.setPosSeek;
    out[4]=g_tfs.getPosBuffer;  out[5]=g_tfs.getPosFile;
    out[6]=g_tfs.getSizeBuffer; out[7]=g_tfs.getSizeFile;
    out[8]=g_tfs.create;        out[9]=g_tfs.createFailed;  out[10]=g_tfs.createBom;
}

}  // namespace RtIO

// ============================================================================
// ENDGAME B.3.4 STREAMS (4/6) — the seven `IO::TextFileStream` slot bodies.
// ============================================================================
// A member of `IO::X` cannot be defined inside `namespace RtIO` — `IO` and
// `RtIO` are siblings, and C++ requires a member definition to appear in a
// namespace ENCLOSING the class (B34_BRIEFS section A.2). So the bodies open
// their own `namespace IO` block after the doors close, and reach this file's
// helpers and counters as `RtIO::TfsDetectFormat`, `RtIO::g_tfs` and so on —
// which works because a member of an enclosing namespace is reachable by
// qualification from anywhere else in the same translation unit.
//
// SEVEN OF THE TEN SLOTS. Slot 0 is PURE (deleting_dtor.cpp's row 41), slot 1
// is `IO::FileStream::GetSource` and slot 7 is `IO::FileStream::Open`, both
// inherited and not overridden. So this file owns 2, 3, 4, 5, 6, 8 and 9.
//
// SLOT 8 IS `OpenWithFormat`, NOT `Open` — B34_RULES section 1.3. Slot 7 is the
// base's `Open(const Filename&)` and slot 8 is this class's own
// `Open(const Filename&, Format)`, so retail overloads `Open` ACROSS the
// base/derived boundary with both entries live. Declaring the derived one
// `Open` would HIDE the base's name at every inherited call site.
//
// THE COUNTERS ARE BUMPED IN THE MEMBER, NOT IN THE DOOR, so they keep counting
// entries to the BODY rather than entries to the door — which is what they have
// always measured and what makes their numbers comparable across this change.
// The four negative-control perturbations (`g_tfsCtl` 1..4) move WITH their
// bodies for the same reason: a control that perturbs a copy of the code
// catches in a screen it cannot reach.
//
// `IO::TextFileStream::Write` is the class's KEY FUNCTION (the first non-inline,
// non-pure virtual it declares), so GCC emits `vtable for IO::TextFileStream`
// in THIS translation unit. That is why the host unit build needs the stub
// copies in tests/unit/io_stream_abi_stubs.cpp: this file stands on the port
// registry, the allocator and <windows.h> and cannot link into a host test.
namespace IO {

// ============================================================================
// IO::TextFileStream::GetSize() const                    retail 0x1800ba8e0
// IO::TextFileStream::GetCurrentPos() const              retail 0x1800bac30
// ============================================================================
// The banner above the doors has the six-instruction shape and the reason
// neither has a `.pdata` entry. What changes here is only the spelling: the
// two buffer-mode fields are members now (`_bufSize` at +0x78, `_bufCursor` at
// +0x7c) instead of casts through `TfsBufSize` / `TfsBufCursor`, and the
// inherited pair is `_file.size` / `_file.cursor` instead of
// `self->base.file.*`. Same eight loads, same branch, same order.
uint32_t TextFileStream::GetSize() const {
    bool useBuffer = (_buffer != nullptr);
    if (RtIO::g_tfsCtl == 1) { ++RtIO::g_tfsAtt; useBuffer = false; }
    if (useBuffer) {                                                // 1800ba8e0
        ++RtIO::g_tfs.getSizeBuffer;
        return static_cast<uint32_t>(_bufSize);                     // 1800ba8e7  +0x78
    }
    ++RtIO::g_tfs.getSizeFile;
    return static_cast<uint32_t>(_file.size);                       // 1800ba8eb  +0x28
}

uint32_t TextFileStream::GetCurrentPos() const {
    if (_buffer != nullptr) {                                       // 1800bac30
        ++RtIO::g_tfs.getPosBuffer;
        return static_cast<uint32_t>(_bufCursor);                   // 1800bac37  +0x7c
    }
    ++RtIO::g_tfs.getPosFile;
    return static_cast<uint32_t>(_file.cursor);                     // 1800bac3b  +0x2c
}

// ============================================================================
// IO::TextFileStream::Read(int, void*)                   retail 0x1800ba900
// ============================================================================
// The door's banner has the three things this body does NOT do, and they are
// all still true on the member: it does not branch on `_buffer`, it does not
// bounds-check, and it always returns true. With `_buffer == 0` and
// `_bufCursor == 0` it reads from address 0 — retail's, not ours, and NOT to be
// guarded. The copy is `memmove`, not `memcpy`.
bool TextFileStream::Read(int len, void* buf) {
    ++RtIO::g_tfs.read;

    // 1800ba90a..1800ba921. BOTH `len` and the cursor are sign-extended to 64
    // bits (`MOVSLQ`) before the add, so a negative cursor addresses BACKWARDS
    // from the buffer rather than wrapping to a huge positive offset.
    std::memmove(buf,
                 _buffer + static_cast<int64_t>(_bufCursor),
                 static_cast<size_t>(static_cast<int64_t>(len)));   // 1800ba921

    if (RtIO::g_tfsCtl == 3) { ++RtIO::g_tfsAtt; }
    else                     { _bufCursor += len; }                // 1800ba926
    return true;                                                   // 1800ba92e
}

// ============================================================================
// IO::TextFileStream::SetCurrentPos(int)                 retail 0x1800babd0
// ============================================================================
// The door's banner carries the two arms, the UNSIGNED `CMP ..,-3 ; JA`
// discriminator and the `pos < size` (not `<=`) range test. All three are
// transcribed unchanged; the file arm stays a TRANSCRIPTION of
// `?SetCurrentPos@FileStream@IO@@` rather than a delegation to it, because
// retail duplicates the code and delegating would attribute this slot's calls
// to the other slot's counters.
bool TextFileStream::SetCurrentPos(int pos) {

    if (_buffer != nullptr) {                                      // 1800babd6
        ++RtIO::g_tfs.setPosBuffer;
        const int32_t size = _bufSize;
        _bufCursor = pos;                                          // 1800babdf  +0x7c
        bool ok = (pos >= 0) && (pos < size);                      // 1800babe2/e6
        if (RtIO::g_tfsCtl == 2) {
            ++RtIO::g_tfsAtt; ok = (pos >= 0) && (pos <= size);
        }
        return ok;
    }

    ++RtIO::g_tfs.setPosFile;
    _file.cursor = pos;                                            // 1800bac02  +0x2c

    // UNSIGNED — see the door's banner.
    if (RtIO::FileInfo::IsPackedHandle(_file.handle)) {            // 1800babfe/05
        return true;                                               // 1800bac07
    }

    ++RtIO::g_tfs.setPosSeek;
    const uint32_t got = Platform::FileSeek32(
        _file.winHandle,                 // 1800bac0f  +0x38
        pos);                                                      // 1800bac13/16 SetFilePointer
    const bool ok = (static_cast<int32_t>(got) == pos);            // 1800bac1f
    return ok;
}

// ============================================================================
// IO::TextFileStream::Create(Filename const&, Format, Format)
//                                                        retail 0x1800ba940
// ============================================================================
// SLOT 9. The door's banner has the back-reference argument that proves the
// last two parameters are both FORMATS, the four-step body, and the BOM table
// this body is the only reachable writer of. `_requestedFormat` and
// `_activeEncoding` are members now; the selector is still `_activeEncoding`
// ALONE, which is what negative control 4 exists to catch.
//
// `?Create@FileStream@IO@@` still takes the `RtIo::FileStream` POD (it belongs
// to the lifecycle block in file_stream.cpp, not to this one), so the call
// bridges through the POD view of `this` rather than changing the callee.
bool TextFileStream::Create(const System::Filename& path,
                            Text::Format requestedFormat, Text::Format activeEncoding) {
    ++RtIO::g_tfs.create;

    _requestedFormat = requestedFormat;                      // 1800ba949  +0x68
    _activeEncoding  = activeEncoding;                       // 1800ba94d  +0x6c

    if (!FileStream::Create(path)) {                         // 1800ba951  (B.3.9 IO: the member)
        ++RtIO::g_tfs.createFailed;
        return false;                                        // 1800ba95a
    }

    uint8_t bom[3] = { 0, 0, 0 };
    int32_t bomLen = 0;
    Text::Format bomSelector = _activeEncoding;              // 1800ba960  +0x6c
    if (RtIO::g_tfsCtl == 4) { ++RtIO::g_tfsAtt; bomSelector = _requestedFormat; }
    switch (bomSelector) {                                   // 1800ba963..76
        case Text::Format::Utf8Bom:   // 1800ba994
            bom[0] = 0xEF;
            bom[1] = 0xBB;
            bom[2] = 0xBF;
            bomLen = 3;
            break;
        case Text::Format::Utf16Le:   // 1800ba985
            bom[0] = 0xFF;
            bom[1] = 0xFE;
            bomLen = 2;
            break;
        case Text::Format::Utf16Be:   // 1800ba978
            bom[0] = 0xFE;
            bom[1] = 0xFF;
            bomLen = 2;
            break;
        default:                      // 0, 2, anything else
            break;
    }

    if (bomLen != 0) {
        ++RtIO::g_tfs.createBom;
        uint32_t written = 0;                                                 // 1800ba9c0
        Platform::FileWrite(_file.winHandle,        // 1800ba9ab WriteFile
                            bom, static_cast<uint32_t>(bomLen), &written);    // 1800ba9c9
        _file.cursor += static_cast<int32_t>(written);                        // 1800ba9d3
    }

    return true;                                             // 1800ba9d6
}

// ============================================================================
// IO::TextFileStream::Write(int, void const*)            retail 0x1800ba9f0
// ============================================================================
// SLOT 2, and TWO BODIES IN ONE — the door's banner has the discriminator, why
// the two arms' return values are NOT comparable, and why the measure pass
// deliberately encodes into the codepoint slot and throws the bytes away.
// Unchanged here: only the field spellings moved onto members.
bool TextFileStream::Write(int len, const void* buf) {
    ++RtIO::g_tfs.write;

    Text::Format srcFormat = _requestedFormat;                  // 1800baa03  +0x68
    const Text::Format dstFormat = _activeEncoding;             // 1800baa06  +0x6c

    // ---- fast arm: no transcode -------------------------------------------
    if (srcFormat == dstFormat) {                               // 1800baa15
        ++RtIO::g_tfs.writeDirect;
        uint32_t written = 0;                                   // 1800baa28
        Platform::FileWrite(_file.winHandle,    // 1800baa1a WriteFile
                            buf, static_cast<uint32_t>(len), &written);   // 1800baa30
        _file.cursor += static_cast<int32_t>(written);                    // 1800baa3d
        const bool ok = (static_cast<int32_t>(written) == len);           // 1800baa41
        return ok;                                              // 1800baa48
    }

    // ---- slow arm: transcode ----------------------------------------------
    ++RtIO::g_tfs.writeTranscode;
    const uint8_t* const begin = static_cast<const uint8_t*>(buf);  // CAST-OK: byte_io
    const uint8_t* const end   = begin + len;                   // 1800baa70
    const uint8_t* p           = begin;

    if (srcFormat == Text::Format::Detect) {                                       // 1800baa74
        srcFormat = RtIo_TextCodec_DetectFormat(begin, len);        // 1800baa7e
        p = begin + RtIO::TfsBomSkip(srcFormat);                // 1800baaac
    }

    // Pass 1 — MEASURE. Nothing is kept but the byte count.
    int32_t outBytes = 0;
    while (p < end) {                                           // 1800baab2
        int32_t cp = -1;                                        // 1800baac0
        int32_t consumed;
        if (static_cast<uint32_t>(srcFormat) <= static_cast<uint32_t>(Text::Format::Utf16Be)) {           // 1800baacb
            consumed = RtIo_TextCodec_DecodeOne(srcFormat, p, end, &cp, srcFormat);  // 1800baae4
        } else {
            cp = -1; consumed = 1;                              // 1800baaf2
        }
        p += consumed;                                          // 1800baafc
        if (cp >= 0) {                                          // 1800baaff
            int32_t n = 0;
            if (static_cast<uint32_t>(dstFormat) <= static_cast<uint32_t>(Text::Format::Utf16Be))         // 1800bab03
                n = RtIo_TextCodec_EncodeOne(cp, &cp, dstFormat);   // 1800bab14
            outBytes += n;                                      // 1800bab1d
        }
    }

    // Pass 2 — CONVERT into a fresh block, write it, free it.
    uint8_t* dst = static_cast<uint8_t*>(::System::MemoryAllocator::MemAlloc(
            static_cast<size_t>(outBytes), 4));                 // 1800bab2d
    RtIo_TextCodec_ConvertText(_requestedFormat, 0, begin, len,     // 1800bab5d
                           _activeEncoding, 0, 0, dst, outBytes);

    uint32_t written = 0;                                       // 1800bab74
    Platform::FileWrite(_file.winHandle,        // 1800bab62 WriteFile
                        dst, static_cast<uint32_t>(outBytes), &written);  // 1800bab80
    _file.cursor += static_cast<int32_t>(written);              // 1800bab8d
    ::System::MemoryAllocator::MemFree(dst);                    // 1800bab94

    const bool ok = (static_cast<int32_t>(written) == outBytes);  // 1800bab9e
    return ok;
}

// ============================================================================
// IO::TextFileStream::OpenWithFormat(Filename const&, Format)
//                                                        retail 0x1800ba640
// ============================================================================
// SLOT 8 — retail's `?Open@TextFileStream@IO@@UEAA_NAEBVFilename@System@@
// W4Format@Text@4@@Z`, renamed per B34_RULES section 1.3 so it cannot be
// confused with the slot-7 `Open` it overloads. The door's banner has the eight
// steps, the three things a port from the shape gets wrong, and the note that
// the overflow guard is retail's rather than a port's caution. All unchanged.
//
// THIS IS THE BODY THAT INSTALLS `_buffer` AND `_bufSize`, the two fields the
// rest of the class branches on, and it CLOSES the file before returning —
// which is why `Read` never touches the handle. `_requestedFormat` (+0x68) is
// deliberately NOT written; only `_activeEncoding` (+0x6c) is, and only at the
// very end.
bool TextFileStream::OpenWithFormat(const System::Filename& path, Text::Format format) {
    ++RtIO::g_tfs.open;

    if (!FileStream::Open(path)) {                              // 1800ba652  (B.3.9 IO: non-virtual, as the door was)
        ++RtIO::g_tfs.openFailed;
        return false;                                           // 1800ba659
    }

    const int32_t fileSize = _file.size;                        // 1800ba67f  +0x28
    uint8_t* const raw = static_cast<uint8_t*>(
        ::System::MemoryAllocator::MemAlloc(
                static_cast<size_t>(fileSize), 4));             // 1800ba690
    _file.cursor = 0;                                           // 1800ba699  +0x2c

    // UNSIGNED: -1 and -2 seek, a packed descriptor does not (FileStream.h).
    if (!RtIO::FileInfo::IsPackedHandle(_file.handle))          // 1800ba695 JBE
        Platform::FileSeek32(_file.winHandle, 0);               // 1800ba6b6 SetFilePointer

    FileSystem::_singleton->ReadFile(_file, fileSize, raw);     // 1800ba6cd
    if (_file.handle != RtIO::FileInfo::kHandleNone)            // 1800ba6d2
        FileSystem::_singleton->CloseFile(_file);               // 1800ba6e3

    const uint8_t* const rawEnd = raw + fileSize;               // 1800ba6f0

    // ---- pass 1: MEASURE ---------------------------------------------------
    Text::Format srcFormat = RtIo_TextCodec_DetectFormat(raw, fileSize); // 1800ba6f4
    const uint8_t* p  = raw + RtIO::TfsBomSkip(srcFormat);      // 1800ba721

    int32_t outBytes = 0;
    while (p < rawEnd) {                                        // 1800ba727
        int32_t cp = -1;                                        // 1800ba730
        int32_t consumed;
        if (static_cast<uint32_t>(srcFormat) <= static_cast<uint32_t>(Text::Format::Utf16Be)) {           // 1800ba73b
            consumed = RtIo_TextCodec_DecodeOne(srcFormat, p, rawEnd, &cp, srcFormat);  // 1800ba754
        } else {
            cp = -1; consumed = 1;                              // 1800ba762
        }
        p += consumed;                                          // 1800ba76c
        if (cp >= 0) {                                          // 1800ba76f
            int32_t n = 0;
            if (static_cast<uint32_t>(format) <= static_cast<uint32_t>(Text::Format::Utf16Be))            // 1800ba773
                n = RtIo_TextCodec_EncodeOne(cp, &cp, format);      // 1800ba784
            outBytes += n;                                      // 1800ba78d
        }
    }

    // ---- install the buffer ------------------------------------------------
    _bufSize = outBytes;                                        // 1800ba7a4  +0x78
    uint8_t* const buffer = static_cast<uint8_t*>(
        ::System::MemoryAllocator::MemAlloc(
                static_cast<size_t>(outBytes), 4));             // 1800ba7a7
    _buffer = buffer;                                           // 1800ba7b1  +0x70
    const int32_t bufSize = _bufSize;                           // 1800ba7b5

    // ---- pass 2: CONVERT ---------------------------------------------------
    // DetectFormat is re-run on the same bytes — retail's, not a duplication.
    srcFormat = RtIo_TextCodec_DetectFormat(raw, fileSize);         // 1800ba7c4
    p = raw + RtIO::TfsBomSkip(srcFormat);                      // 1800ba7e4

    uint8_t* out   = buffer;                                    // 1800ba7ec
    int32_t written = 0;
    while (p < rawEnd) {                                        // 1800ba7f2
        int32_t cp = -1;                                        // 1800ba800
        int32_t consumed;
        if (static_cast<uint32_t>(srcFormat) <= static_cast<uint32_t>(Text::Format::Utf16Be)) {           // 1800ba80b
            consumed = RtIo_TextCodec_DecodeOne(srcFormat, p, rawEnd, &cp, srcFormat);  // 1800ba827
        } else {
            cp = -1; consumed = 1;                              // 1800ba83c
        }
        p += consumed;                                          // 1800ba846
        if (cp < 0) continue;                                   // 1800ba849

        // Pass 2's OWN scratch — see the door's note (a). Four bytes is the
        // widest any table entry emits.
        uint8_t scratch[4] = { 0, 0, 0, 0 };
        int32_t n = 0;
        if (static_cast<uint32_t>(format) <= static_cast<uint32_t>(Text::Format::Utf16Be))                // 1800ba84d
            n = RtIo_TextCodec_EncodeOne(cp, scratch, format);      // 1800ba85b
        written += n;                                           // 1800ba864
        if (written > bufSize) continue;                        // 1800ba866  JG
        if (buffer == nullptr) continue;                        // 1800ba86a
        if (n > 0) {                                            // 1800ba872
            std::memmove(out, scratch, static_cast<size_t>(n)); // 1800ba881
            out += n;                                           // 1800ba886
        }
    }

    ::System::MemoryAllocator::MemFree(raw);                    // 1800ba89e
    _activeEncoding = format;                                   // 1800ba8cc  +0x6c
    return true;                                                // 1800ba8d0
}

}  // namespace IO

// ============================================================================
// DEFERRED — the three slots NOT in this file, each with its own reason and its
// own expiry condition, because "needs X" is a TODO and not a blocker.
// ============================================================================
//   [0] +0x00  0x18002ba90  deleting dtor — CLOSED, Wave 21 lane ENC. This slot's
//       expiry was "EXPIRES when `??1TextFileStream@` is ported", which was the
//       WRONG shape of expiry: the deleting dtor does not need its ~Class
//       CALLEE ported, only the two ADDRESSES it reaches, and both are reached
//       by ResolveAddr the same way whether the callee is ours or the DLL's
//       (the shared eh-vector-iterator generic used by the whole
//       src/engine/object/deleting_dtor.cpp family already does this for the
//       ToolTipDialog and CompressedFileStream rows). It is now
//       src/engine/object/deleting_dtor.cpp's row 41 ("IO::TextFileStream"),
//       calling `??1TextFileStream@IO@@UEAA@XZ` (0x1800ba490 — EXPORTED, still
//       NOT ported, so this makes that address newly `called`) through the
//       family's shared iterator (0x18007ec98). Verified by
//       tools/rt_deleting_dtor_verify.py against retail's shipped bytes.
//       IT IS ALSO THE SOURCE OF A LAYOUT FACT ALREADY IN USE: its `MOV EDX,0x80`
//       element stride is where FileStream.h's `sizeof(TextFileStream) == 0x80`
//       comes from.
//   [2] +0x10  0x1800ba9f0  Write — 462 bytes (`.pdata`). *** CLOSED, WAVE 28
//       LANE SLOTS *** — see the WAVE 28 section above. The deferral said in
//       terms that it was "a size judgement, not a blocker, and it has no
//       premise to expire", and that was the correct reading: nothing expired,
//       a later lane simply did the work. Note also that the `.pdata` figure
//       quoted here is a LOWER bound — MSVC chains fragments through
//       UNW_FLAG_CHAININFO — so "462 bytes" understated the job
//       (reference_pdata_is_a_lower_bound_on_function_extent.md). Measured from
//       the instruction listing the body runs 0x1800ba9f0..0x1800babbd = 462,
//       which here happens to agree.
//   [8] +0x40  0x1800ba640  Open(Filename, Text::Format) — 666 bytes.
//       *** CLOSED, WAVE 28 LANE SLOTS *** — same section, same reason.
//       Measured extent 0x1800ba640..0x1800ba8d9 = 666.
//
// So `??_7TextFileStream@IO@@6B@` went 2/10 -> 7/10 (Wave 20 lane SAVETEXT)
// -> 8/10 (Wave 21 lane ENC, slot 0) -> **10/10, WAVE 28 LANE SLOTS.**
// COMPLETE IS NOT PROMOTABLE. It says only that promoting would put no trap in
// the table; whether the row may be promoted turns on owning the function that
// STAMPS it, which no source scan answers
// (reference_a_green_readiness_screen_is_not_a_route.md). **Wave 28 lane SLOTS
// did NOT promote this row and establishes nothing about its stamper.**
// ============================================================================
