// ============================================================================
// src/engine/io/package_file_dtor.cpp — ~IO::PackageFile
//                                        Wave 25 lane BODY
// ============================================================================
//
//   | Export                     | Retail      | Ord | Bytes | Instrs |
//   |----------------------------|-------------|-----|-------|--------|
//   | ??1PackageFile@IO@@UEAA@XZ | 0x1800b4540 | 639 |  318  |   87   |
//
// `UEAA` is the VIRTUAL spelling and it is the one the DLL exports — the
// non-virtual `QEAA` form of this name does not exist, and asking for it would
// have resolved NULL with `ResolveExport` hiding the miss
// (reference_phantom_dtor_mangling_qeaa_vs_ueaa.md). The name above was printed
// out of the shipped export directory by ADDRESS, not typed from the pattern.
//
// ---------------------------------------------------------------------------
// 1. THE STATED BLOCKERS, AND WHAT RE-DERIVING THEM FOUND
// ---------------------------------------------------------------------------
// Two blockers were on the record for this row, one of each shape. Kickoff §1a:
// dependency-shaped claims decay, oracle-shaped ones do not. Here BOTH failed,
// which is itself the news.
//
// (a) DEPENDENCY — `package_file.cpp:925` lists this row as deliberately absent
//     with the parenthetical **"(5 unported callees)"**.
//     **RE-DERIVED: FALSE. The dtor has SEVEN distinct callees and SIX of them
//     are ported AND registered in this tree today.** Measured by disassembling
//     0x1800b4540..0x1800b467e and grepping each target for a
//     `RegisterRuntimeAddr`/`RegisterRuntimeExport`:
//
//       0x18000c180  ~FileSystem::FileInfo   PORTED  io/file_stream.cpp:886
//       0x1800269a0  ~Array<T> (elem loop)   NOT PORTED  <- the only one
//       0x1800112a0  ~Array<uint8>           PORTED  io/modification.cpp:909
//       0x1800111a0  ~Array<int32>           PORTED  io/modification.cpp:907
//       0x1800c7ad0  ?MemFree@MemoryAllocator PORTED  system/allocator.cpp:1701
//       0x180010ab0  ~Array<T> (elem dtor)   PORTED  collection/array_destroy.cpp:902
//       0x1800bc950  ~Object::External       PORTED  object/external.cpp:762
//
//     The count was wrong by four. The callees are now the members' and the
//     base's own destructors, which the compiler runs after the body below.
//
//     ENDGAME B.3.2 step 2 (callers, 2026-09-13): both 0x1800269a0 and
//     0x180010ab0 are now `Collection::Array<T>::~Array()` member calls at
//     their call sites in §2's body, not `ResolveAddr` forwarders — see the
//     comments there for the instantiation each one names.
//
// (b) ORACLE — `WAVE23_LANE_IOPORT.md §2.4`, carried forward by Wave 24 lane
//     SMALLBODY as "the strongest-held of the nine, and correctly scoped":
//     *"it has no oracle... a fabricated subject is a fake rather than a
//     synthetic one"*.
//     **RE-DERIVED: FALSE.** The objection is real but it is an objection to
//     ONE construction (fabricating a PackageFile by hand), not to the
//     existence of an oracle (reference_synthetic_subject_beats_a_fake.md).
//
// ---------------------------------------------------------------------------
// 2. THE BODY — WHAT THE BYTES DO
// ---------------------------------------------------------------------------
// Stamp the vtable, destroy five embedded members, release five Strings, then
// TAIL-JUMP to `~Object::External`.
//
//   1800b455b  LEA RAX,[0x180155700] / MOV [RCX],RAX   ??_7PackageFile@IO@@6B@
//   1800b4565  ADD RCX,0xc0 / CALL 0x18000c180         ~FileInfo        (+0xC0)
//   1800b4572  LEA RCX,[RBX+0xb0] / CALL 0x1800269a0   ~Array           (+0xB0)
//   1800b457f  LEA RCX,[RBX+0x98] / CALL 0x1800112a0   ~Array<uint8>    (+0x98)
//   1800b458c  LEA RCX,[RBX+0x80] / CALL 0x1800111a0   ~Array<int32>    (+0x80)
//   1800b4599  the String release idiom at +0x60, +0x58, +0x50, +0x48, +0x40
//   1800b4658  LEA RCX,[RBX+0x30] / CALL 0x180010ab0   ~Array           (+0x30)
//   1800b4662  LEA RCX,[RBX+0x20] / CALL 0x180010ab0   ~Array           (+0x20)
//   1800b466c  MOV RCX,RBX / JMP 0x1800bc950           ~External, TAIL
//
// FOUR TRANSCRIPTION POINTS THAT MATTER:
//
// (i)   **THE FIVE STRINGS ARE RELEASED IN DESCENDING ORDER** — +0x60 first,
//       +0x40 last. That is reverse construction order, which is what a dtor
//       must do, and it reaches the allocator in that order. A port that
//       released them ascending would produce an identical object and a
//       different free sequence, and no comparator on the object bytes could
//       see it (array_destroy_internal.h records the same trap on three rows).
//
// (ii)  **THE RELEASE IDIOM'S TWO ARMS LEAVE THE SLOT HOLDING DIFFERENT
//       THINGS.** On the freeing arm the slot keeps `data - 8`; on the sharing
//       arm it is NULLED. That asymmetry is retail's. Here the five Strings are
//       the members' own `System::String` destructors, which the compiler runs
//       after the body below.
//
// (iii) **TWO MORE STRING FIELDS THAN THE ACCESSORS NAME.** Past +0x40 `_name`,
//       +0x48 `_author` and +0x50 `_description`, the dtor releases **+0x58 and
//       +0x60 with the same String idiom**, so both are `System::String`. In
//       PackageFile.h they are `_icon` and `_preview`, marked purpose unknown:
//       a destructor pins the TYPE and says nothing about the MEANING
//       (reference_a_writer_beats_readers_agreeing_on_a_sign.md is the
//       same discipline one step over).
//
// (iv)  **THE LAST CALL IS A TAIL JUMP, SO ITS RETURN VALUE IS THIS
//       FUNCTION'S.** `JMP 0x1800bc950` — whatever `~Object::External` leaves
//       in RAX is what `~PackageFile` returns. Retail's callers are free to use
//       it. Here the destructor is the C++ `IO::PackageFile::~PackageFile()`,
//       which returns nothing, and the tail into ~External is the compiler's
//       base-destructor call (the body below).
//
// ============================================================================

#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstring>

#include "system/String.h"
#include "io/FileSystem.h"
#include "io/PackageFile.h"     // ENDGAME B.3.9 IO: the member dtor
#include "io/Modification.h"    // the two Array dtor bodies
#include "system/string_internal.h"
#include "../collection/array_internal.h"   // ENDGAME B.3.2 step 2 -- kSpec_PkgEntry, AsArray


// ============================================================================
// ??1PackageFile@IO@@UEAA@XZ   retail 0x1800b4540  ord 639
// ============================================================================
// ENDGAME B.3.9 IO: the class's own destructor. The door that stood here, its
// by-address/by-name registration and `StampVtable` are gone: the entry stamp
// at 0x1800b455b is the compiler's, and the tail JMP into ~External
// (0x1800b4679) is the compiler's base-destructor call. The order in between is
// retail's, unchanged.
IO::PackageFile::~PackageFile() {

    // B.4 Z LAST IO (`behaviour:`): 0x1800b456c (~FileInfo 0x18000c180 on
    // +0xC0) and retail 0x1800269a0 (`~Array<IO::PackageFile::Entry>` on +0xB0,
    // on its own row kSpec_PkgEntry: stride 0x10, a destroy that releases the
    // embedded String) are the two members' own destructors now, which the
    // compiler runs after this body in reverse declaration order: FileInfo
    // first, then the entry array, as retail.

    // 0x1800b4586 (~Array<uint8> 0x1800112a0, +0x98) and 0x1800b4593
    // (~Array<uint32> 0x1800111a0, +0x80) are the members' own destructors
    // since B4 X7-RT: the compiler runs them after this body, preview first
    // (reverse declaration order), before the five Strings below.

    // 0x1800b4599..0x1800b4658 (the five Strings, +0x60 down to +0x40) and
    // 0x1800b465c / 0x1800b4666 (`~Array<System::String>` 0x180010ab0 on +0x30
    // then +0x20) are the members' own destructors, which the compiler runs
    // after this body in reverse declaration order: retail's order (B4 X-R3).

    // 0x1800b4679 — the tail into ~External is the compiler's.
}
