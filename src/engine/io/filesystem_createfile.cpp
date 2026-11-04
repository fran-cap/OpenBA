// ============================================================================
// src/engine/io/filesystem_createfile.cpp — Wave 19 lane FILE
// IO::FileSystem CreatePath / CreateFileW / PlatformCreateFile / CloseFile /
//                PlatformCloseFile / ~FileSystem
// ============================================================================
//
//   | Export                                                | Retail      | Ord  | Instrs |
//   |-------------------------------------------------------|-------------|------|--------|
//   | ?CreatePath@FileSystem@IO@@QEBA_NAEBVFilename@…@Z     | 0x1800ae430 | 2130 |  105   |
//   | ?CloseFile@FileSystem@IO@@QEAAXAEAUFileInfo@12@@Z     | 0x1800accc0 | 1726 |   50   |
//   | ??1FileSystem@IO@@QEAA@XZ                             | 0x1800ac6d0 |  594 |   50   |
//   | ?CreateFileW@FileSystem@IO@@QEAA?AUFileInfo@12@AEB…@Z | 0x1800acc50 | —    |   17   |
//   | ?PlatformCreateFile@FileSystem@IO@@AEAA_N…@Z          | 0x1800ade60 | 3357 |  ~95   |
//   | ?PlatformCloseFile@FileSystem@IO@@AEAAX…@Z            | 0x1800ae020 | —    |   62   |
//
//
// ---------------------------------------------------------------------------
// THE LAST THREE ROWS ARE NOT ON THE WAVE 19 ROW LIST, AND THAT IS THE POINT
// ---------------------------------------------------------------------------
// Lane FILE's brief names eight rows and its acceptance criterion is the
// SCREENSHOT CUTOVER HOLE, not the row count. Walking the call graph out of the
// two rows that ARE on the list found three more bodies between them and Win32,
// none of them ported:
//
//     ?Create@FileStream@       -> ?CreateFileW@FileSystem@   -> ?PlatformCreateFile@
//     ?CloseFile@FileSystem@    -> ?PlatformCloseFile@
//
// Porting only the named rows would have left `ResolveExport` falling through
// to the DLL at each of those three, and the DLL-absent screenshot would still
// have died — one link further down than before. `called` grows by these; that
// is DEFERRED WORK BEING SURFACED, not invented work, and it is exactly what
// Wave 18 lane VRES said about the rows that produced this lane.
//
// Two of the three are `AEAA` — PRIVATE, not unexported. This DLL exports its
// private members and `?PlatformCreateFile@` is ordinal 3357
// (`reference_aeaa_is_private_not_unexported.md`).
//
// ---------------------------------------------------------------------------
// THE FACT THAT DECIDES ALL OF THIS: A WRITE GOES TO %TEMP% FIRST
// ---------------------------------------------------------------------------
// `?PlatformCreateFile@` does NOT open the caller's path. It calls
// `GetTempPathW`, `GetTempFileNameW`, and opens THAT — then stores the temp
// path in `FileInfo::_name0` (+0x20) and the CALLER'S path in `_name1` (+0x28).
// `?PlatformCloseFile@` then closes the handle, `DeleteFileW`s the destination
// and `MoveFileW`s the temp over it.
//
// So the pair is an ATOMIC-REPLACE, and three consequences follow that a
// signature or a decompile summary would each get wrong:
//
//   1. **Nothing appears at the caller's path until CLOSE.** A screenshot that
//      is written but never closed leaves a stray file in %TEMP% and no BMP.
//      `~FileStream` is what closes it, which is why that row and this one had
//      to land in the same lane.
//   2. **`_name0` is the TEMP path and `_name1` is the FINAL path**, not the
//      other way round. `MoveFileW`'s arguments settle it: at 0x1800ae0d7/0da
//      RDX comes from the WideString built over `_name1` and RCX from the one
//      built over `_name0`, and MoveFileW's first argument is the EXISTING
//      file. Two same-typed adjacent String fields are otherwise a coin flip
//      (FileStream.h records losing exactly that coin flip once on
//      TextFileStream's `objectName`/`objectType` pair).
//   3. **The rename is skipped when `_name0` has ZERO LENGTH**, not merely when
//      it is null. 0x1800ae065 is `CMP dword ptr [RAX-0x8],0x0` — the String
//      block's LENGTH field, at -8, not the refcount at -4.
//
// ---------------------------------------------------------------------------
// `?CreatePath@` RETURNS TRUE UNCONDITIONALLY
// ---------------------------------------------------------------------------
// Its epilogue is `MOV AL,0x1` on the ONLY path out (0x1800ae637); every
// `CreateDirectoryW` result is discarded. It is not a status, and a port that
// propagated the Win32 return would diverge on every path that already exists.
// `?Create@FileStream@` discards it too.
//
// PORTABILITY: this file was `filesystem_createfile_win.cpp` until Phase D.4
// THREADS E.5; its Win32 calls are Platform:: primitives now (platform_file.h:
// DirCreate, FileCreateTemp, FileClose, FileDelete, FileMove). The Win32 entry
// points retail calls were read out of Runtime-x64.dll's OWN import table with
// `pefile` — `tools/dispatch_slot_map.py` is for the EXE's imports:
//
//     0x18014b0c8 CreateDirectoryW   0x18014b340 GetTempPathW
//     0x18014b338 GetTempFileNameW   0x18014b320 CreateFileW
//     0x18014b3b8 CloseHandle        0x18014b330 DeleteFileW
//     0x18014b100 MoveFileW
// ============================================================================

#include <cstdint>
#include <cstddef>
#include <cstdio>

#include "platform_file.h"   // Platform::DirCreate / FileCreateTemp / FileClose / FileDelete / FileMove
#include "io/FileSystem.h"
#include "io/FileStream.h"
#include "system/String.h"
#include "system/Filename.h"
#include "system/string_internal.h"
#include "system/WideString.h"
#include "io/Modification.h"   // RtColl_ArrayFilenameDtor_1800098d0
#include "collection/avl_trees.h"      // rtavl::SubtreeFree
#include "io/packfile_fold_index.h"   // block PATHINDEX: the hash index beside _packfileIndex

#include <new>        // placement new into the retail hidden return slot
#include <utility>    // std::move -- the pointer steal out of a POD out-param

// ENDGAME B.3.1 step 2: the two lane-String entry points this file
// forward-declared here — `RtSystem_Filename_AdjustDelimiters` (0x1800c6970)
// and `RtSystem_Filename_Assign` (0x180006770) — are gone from this
// translation unit. The second is `Filename::operator=`, and every call to the
// first was the tail of a mutating Filename entry point, which the class
// spells as itself with AdjustDelimiters private inside it.

namespace RtIO {


namespace {

// (B.4 Z MIRROR2: the WideStr slot, WideFromString and WideRelease are gone:
// the four sites hold real System::WideString locals, whose ctor 0x1800cab50 /
// 0x18000a6e0 and dtor 0x18000a730 are retail's, the release inlined there.)

// Collection::Array<Filename>::~Array, retail 0x1800098d0, is
// RtIO::RtColl_ArrayFilenameDtor_1800098d0 (modification.cpp); the unexported
// AVL subtree free at 0x180008340 is rtavl::SubtreeFree (avl_trees.cpp). Both
// are called by name (B3R RT-IO).
//
// ENDGAME B.3.2 step 2 (2026-09-13) — NOT CONVERTED to `Collection::Set<T>`,
// deliberately. `FileSystem::packfileIndexRoot`/`packfileIndexCount` (+0x40/
// +0x48) ARE a `Collection::Set<T>` header in every way that matters here, and
// 0x180008340 IS `Set<T>::DestroySubtree` (avl_trees.cpp names it: "the
// recursive subtree free"). What blocks the conversion:
//   1. the element (`PackfileIndexNode`'s {key, packIndex, fileIndex}) is
//      released ASYMMETRICALLY — only the `key` slot (a raw `const char*`, not
//      a `System::String`) is `Release()`d, the packed {packIndex,fileIndex}
//      qword is not. `Set<T>::DestroySubtree` calls `n->element.~T()`, a real
//      C++ destructor over the WHOLE element; expressing "release only one
//      raw-pointer field" would need a hand-written element type with a
//      matching destructor, which is new class design, not a call-site swap.
//   2. `packfileIndexRoot`/`Count` are two loose fields, not a typed member —
//      other files reach the same tree by raw node pointer (`node[3]`/
//      `node[4]` walks in filesystem.cpp, filesystem_find.cpp,
//      filesystem_openfile.cpp, filesystem_packages.cpp), none of which
//      are in this batch, so retyping the header field is out of scope.
// The member that would be needed: `Collection::Set<PackfileIndexElem>` where
// `PackfileIndexElem`'s destructor implements the key-only String release
// (`StringReleaseInline`), once that type exists and the other four files'
// raw walks are converted alongside it.

}  // namespace

// The port-only attendance counters `g_fcc` that stood here are deleted (review
// F-140, the same race as file_stream.cpp's): nothing read them, and CreateFileW /
// CloseFile run on the Persistence worker and the Loader as well as the main thread.

// ============================================================================
// IO::FileSystem::CreatePath(Filename const&) const       retail 0x1800ae430
// ============================================================================
// Walk the path's characters; at every `\` or `/`, take the prefix up to (but
// not including) that separator and `CreateDirectoryW` it. Returns TRUE
// UNCONDITIONALLY — see the header note.
//
// FOUR DETAILS THE DISASSEMBLY SETTLES:
//
//  1. THE LENGTH COMES FROM THE STRING BLOCK, NOT strlen. 0x1800ae459 is
//     `MOV EAX,[RAX-0x8]` — the block's length field — and a NULL `data` yields
//     length 0 and no iterations at all (0x1800ae45e).
//  2. `SubString(0, i)` IS EXCLUSIVE OF THE SEPARATOR. R8D is 0 and R9D is the
//     loop counter EBP, which is the index OF the separator, so the directory
//     created for `a\b\c` is `a`, then `a\b` — never a path ending in `\`.
//  3. `AdjustDelimiters` RUNS ON THE COPY, NOT ON THE SUBSTRING. Retail builds
//     the substring, ADOPTS it into a second String slot with an explicit
//     refcount increment (0x1800ae4aa/4b2), normalises THAT, and releases the
//     substring separately. Collapsing the two slots would normalise a buffer
//     the substring temporary still owns.
//  4. THE `this` POINTER IS NEVER READ. It is a `QEBA` const member that
//     touches no field — which is why `?Create@FileStream@` can and does call
//     it on the singleton rather than on anything of its own.
// ============================================================================
// ENDGAME B.3.4 PACKAGE+FS commit 5a: the body is now
// `IO::FileSystem::CreatePath`, at the bottom of this file. The door keeps its
// name, its signature (file_stream.cpp forward-declares and calls it) and both
// registration lines, and delegates. No qualified `IO::` call is needed:
// `IO::FileSystem` has no vtable at all (src/engine/io/FileSystem.h), so
// B34_BRIEFS section A.1's re-entry hazard cannot arise.
// B.4 R-IO: the `RtIo_FileSystem_CreatePath` door is gone; its caller calls
// the member.

// ============================================================================
// IO::FileSystem::PlatformCreateFile(FileInfo&, Filename const&)
//                                                         retail 0x1800ade60
// ============================================================================
// Open a NEW temp file in %TEMP% and record both names on the FileInfo — see
// the header note on the atomic-replace pair.
//
// THE CREATE FLAGS ARE READ OFF THE PUSHES, NOT GUESSED:
//   0x1800aded5  EDX = 0x40000000  GENERIC_WRITE
//   0x1800aded2  R8D = 0           dwShareMode = 0 (exclusive)
//   0x1800adecf  R9D = 0           lpSecurityAttributes = null
//   0x1800adec7  [rsp+0x20] = 2    CREATE_ALWAYS
//   0x1800adebf  [rsp+0x28] = 0x80 FILE_ATTRIBUTE_NORMAL
//   0x1800adeba  [rsp+0x30] = 0    hTemplateFile = null
//
// THE FAILURE TEST IS UNSIGNED AND CATCHES TWO VALUES, NOT ONE.
// `DEC RAX ; CMP RAX,-0x3 ; JA fail` fails when `handle - 1` is above
// 0xFFFFFFFFFFFFFFFD unsigned, i.e. when the handle is INVALID_HANDLE_VALUE
// (-1) **or NULL**. A port that tested only INVALID_HANDLE_VALUE would treat a
// null handle as success. On failure the FileInfo's `handle` is left at
// whatever the caller set (`?CreateFileW@` sets -1) and `false` is returned;
// the Win32 handle at +0x18 is stored EITHER WAY, before the test.
//
// `handle = -2` on success is the LOOSE-FILE sentinel FileSystem.h documents
// (-1 nothing, -2 loose, otherwise a packed (packIndex<<24)|fileIndex).
//
// GetTempFileNameW's prefix comes from the .rdata literal at 0x1801557e8, which
// READS AS `L"SRS"` (53 00 52 00 53 00 00 00 — Shining Rock Software). It is
// still fetched live through the address door rather than spelled here: it is an
// unexported literal, and a transcription of it is a guess no static check
// could catch. The bytes are quoted so a reader can see the door was verified.
// ============================================================================
// The body is the member `IO::FileSystem::PlatformCreateFile`, at the bottom of
// this file (`AEAA`, private, in retail).

// ENDGAME B.3.4 PACKAGE+FS commit 5a: the remaining doors of this file,
// gathered here so every one of them is a single delegating line and the
// retail bodies are all in the one `namespace IO` block below. Each keeps its
// name, its signature — `CreateFileW` keeps the (self, hidden-return-slot,
// path) form and `CloseFile` is called cross-file by file_stream.cpp,
// text_file_stream.cpp, persistence_saveinfo_job.cpp and
// persistence_worker.cpp — and its registration lines, unchanged.
// B.4 R-IO: the `RtIo_FileSystem_CreateFileW` / `RtIo_FileSystem_CloseFile`
// doors are gone; every caller calls the member on `IO::FileSystem::_singleton`.

}  // namespace RtIO

// ============================================================================
// `IO::FileSystem`'s member bodies for this file
// ============================================================================
// Defined outside `namespace RtIO` because a member of `IO::X` must appear in a
// namespace ENCLOSING the class (B34_BRIEFS section A.2); these bodies reach
// this translation unit's `RtIO`-scoped names — the anonymous-namespace
// helpers `WideStr`/`WideFromString`/`WideRelease` and the `using`-ed System names — as `RtIO::X`.
// Every POD is spelled `RtIO::` or `RtSystem::` and every real class `System::`
// (B34_RULES section 7.2).
//
// NOTHING IN ANY OF THE SIX BODIES CHANGED but the spelling of the receiver and
// of the two cross-body calls (`CloseFile` to `PlatformCloseFile`, `CreateFileW`
// to `PlatformCreateFile`, both now member calls rather than door calls —
// neither ever went through a resolve door). Same Win32 calls, same order, same
// discarded results, same early-out positions, same destructor order.
namespace IO {

bool FileSystem::CreatePath(const System::Filename& pathRef) const {
    const System::Filename* const path = &pathRef;
    // detail 4 — retail reads no field of `this`

    // Retail passes the Filename straight into String's members, which is what
    // AsString() spells (Filename.h).
    const System::String& src = path->AsString();
    const int32_t len = src.GetLength();                    // 1800ae459

    for (int32_t i = 0; i < len; ++i) {                     // 1800ae467/62e
        const char c = src._data[i];                        // 1800ae473
        if (c != '\\' && c != '/') continue;                // 1800ae477/47d

        // Detail 3, in class terms: SubString (0x1800c9230) builds a temporary,
        // Filename(const String&) (0x18000c780) adopts it into a SECOND slot and
        // normalises that, and the temporary is released at the end of the full
        // expression — retail's 0x1800ae4c3, in the same place.
        System::Filename dir(src.SubString(0, i));          // 1800ae489..4bd

        const System::WideString w(dir);                    // 1800ae53f: 0x1800cab50
        Platform::DirCreate(w._data);                       // 1800ae54a CreateDirectoryW — result DISCARDED
        // ~w (1800ae551..5bd), then ~dir (0x1800ae5bd): reverse declaration order.
    }

    return true;                                          // 1800ae637: MOV AL,1
}

bool FileSystem::PlatformCreateFile(RtIO::FileInfo& infoRef,
                                    const System::Filename& path) {
    RtIO::FileInfo* const info = &infoRef;

    // The three-character prefix retail passes to GetTempFileNameW: the .rdata
    // bytes at 0x1801557e8 are `53 00 52 00 53 00 00 00` = L"SRS" (Shining Rock
    // Software), read from the DLL's .rdata (file offset 0x1541e8) 2026-10-11.
    // Until then the port passed a null prefix (B3R R4's null arm), so the temp
    // file was named `<hex>.TMP` instead of retail's `SRS<hex>.TMP`.
    static constexpr Platform::PathChar kTempPrefix[] = u"SRS";
    const Platform::PathChar* prefix = kTempPrefix;
    Platform::PathChar tempFile[Platform::kMaxPath];
    // GetTempPathW 1800ade91/96, GetTempFileNameW(dir, prefix, 0, tempFile)
    // 1800adea4..b2, CreateFileW(GENERIC_WRITE, 0, CREATE_ALWAYS) 1800adeb..e5.
    const Platform::FileHandle h = Platform::FileCreateTemp(prefix, tempFile);
    info->winHandle = h;        // 1800adee5 — stored EITHER WAY

    // `LEA RAX,[h-1] ; CMP RAX,-3 ; JA fail`: the unsigned `h - 1 > 2^64 - 3`
    // holds for exactly h == 0 and h == -1, i.e. NULL and INVALID_HANDLE_VALUE.
    if (h == nullptr || h == Platform::kInvalidFileHandle) { // 1800adee9..f0
        return false;                                        // 1800adffa
    }

    info->handle = -2;                                       // 1800adef6 — LOOSE

    // temp path -> _name0, via WideString::Set / AsUTF8 / AdjustDelimiters.
    // B.4 Z MIRROR2 (`behaviour:`, known_port_issues row 582, disassembly
    // read): retail releases norm (0x1800adf5e), utf8 (0x1800adf93), then the
    // WideString LAST (0x1800adfc0): the three locals' destructors, reverse
    // declaration order.
    {
        const System::WideString w(tempFile);                // 1800adefc null store, Set 1800adf0f
        System::String utf8 = w.AsUTF8();                    // 1800adf21

        // adopt-then-AdjustDelimiters == Filename(const String&), 0x18000c780.
        System::Filename norm(utf8);                         // 1800adf27..47
        // Filename::operator=(const Filename&), 0x180006770.
        info->_name0 = norm;                                 // 1800adf58

    }

    // the CALLER's path -> _name1
    info->_name1 = path;                                     // 1800adfea/f1

    return true;                                             // 1800adff6
}

// ============================================================================
// IO::FileSystem::CreateFileW(Filename const&)            retail 0x1800acc50
// ============================================================================
// SEVENTEEN INSTRUCTIONS: build an empty FileInfo in the caller's return slot,
// then hand it to PlatformCreateFile. It RETURNS THE FileInfo BY VALUE, so RDX
// is the hidden return slot and R8 is the Filename — the signature detail
// file_stream.cpp's header note is about.
//
// The initialisation is the SAME field list `??0FileStream`'s inlined prologue
// writes, +0x04 excluded in both. Two unrelated functions leaving the same
// field alone is the agreement FileStream.h's note is built on.
//
// THE PLATFORM RESULT IS DISCARDED (0x1800acca4 has no test after it). The
// caller learns success from `handle != -1`, which PlatformCreateFile sets to
// -2 only on the success path — so the information is carried by the field, not
// by the bool.
// ============================================================================
RtIO::FileInfo FileSystem::CreateFileW(const System::Filename& path) {
    // FileInfo's default ctor in the hidden return slot (1800acc6c..91: the
    // four fields, the two Filename() null stores 0x180006220, then winHandle;
    // +0x04 untouched, exactly as in ??0FileStream's prologue).
    RtIO::FileInfo ret;

    PlatformCreateFile(ret, path);           // 1800acca4 — result discarded

    return ret;                              // 1800acca9
}

// ============================================================================
// IO::FileSystem::PlatformCloseFile(FileInfo&)            retail 0x1800ae020
// ============================================================================
// Close the Win32 handle, then perform the atomic replace: DeleteFileW the
// destination, MoveFileW the temp over it, and clear both name slots.
//
// FIVE THINGS THE DISASSEMBLY SETTLES, each of which a paraphrase gets wrong:
//
//  1. EVERY EARLY-OUT IS A PLAIN RETURN, and the three of them are at
//     0x1800ae041 (no handle), 0x1800ae05f (`_name0` is null) and 0x1800ae069
//     (`_name0` has zero LENGTH). The last reads `[RAX-0x8]`, the block's
//     length, NOT the refcount at -4.
//  2. THE HANDLE IS RESET TO -1 BEFORE ANY OF THE NAME WORK (0x1800ae04d), so
//     a second close is a no-op even if the rename throws.
//  3. `DeleteFileW` IS CALLED ON `_name1`, the DESTINATION — 0x1800ae073 puts
//     RSI (`&_name1`) in RDX for the WideString ctor. It removes an existing
//     file so the move can land; its result is discarded.
//  4. `MoveFileW(_name0, _name1)`, in that order. See the header note — the
//     argument order is what identifies which field is which.
//  5. BOTH NAME SLOTS ARE CLEARED THROUGH `Filename::operator=` FROM A NULL
//     TEMPORARY (0x1800ae14b/15e), not by storing null. That runs the release
//     AND `AdjustDelimiters` on an empty string, which is what retail does; a
//     bare `= nullptr` would leak both buffers.
// ============================================================================
void FileSystem::PlatformCloseFile(RtIO::FileInfo& infoRef) {
    RtIO::FileInfo* const info = &infoRef;
    if (info->winHandle == Platform::kInvalidFileHandle) return;               // 1800ae03d/41

    PackedMapRelease(info->winHandle);   // MAPREAD (NOT RETAIL): unmap first (FileSystem.h)
    Platform::FileClose(info->winHandle);                    // 1800ae047 CloseHandle
    info->winHandle = Platform::kInvalidFileHandle;                            // 1800ae04d

    if (!info->_name0._data) return;                         // 1800ae059/5f
    if (RtSystem::StringLength(info->_name0._data) == 0) return;  // 1800ae065/69

    // 3. delete the destination
    {
        const System::WideString w(info->_name1);            // 1800ae07b: 0x1800cab50
        Platform::FileDelete(w._data);                       // 1800ae084 DeleteFileW — discarded
        // ~w: 1800ae08d..b9
    }

    // 4. move the temp over it. Retail builds the DESTINATION wide string
    //    FIRST (0x1800ae0c1, over _name1) and the SOURCE second (0x1800ae0d1,
    //    over _name0), then calls MoveFileW(source, destination).
    {
        const System::WideString dst(info->_name1);          // 1800ae0c1
        const System::WideString src(info->_name0);          // 1800ae0d1
        Platform::FileMove(src._data, dst._data);            // 1800ae0dd MoveFileW — discarded
        // ~src (1800ae0e4..111) then ~dst (1800ae111..13e): reverse declaration order.
    }

    // 5. clear both, through Filename::operator= from a null temporary
    //    (0x180006770); the temporary's own destructor is a no-op on null.
    info->_name0 = System::Filename();                       // 1800ae13e/14b
    info->_name1 = System::Filename();                       // 1800ae151/15e
}

// ============================================================================
// IO::FileSystem::CloseFile(FileInfo&)                    retail 0x1800accc0
// ============================================================================
// `PlatformCloseFile(info); *info = FileInfo();`
//
// THE RESET IS A REAL ASSIGNMENT FROM A REAL TEMPORARY, not a field-by-field
// clear: retail builds a fresh FileInfo on its own stack (0x1800acce3..d0b),
// calls `FileInfo::operator=` (0x1800acd1a) and then destroys the temporary
// (0x1800acd20..d81, _name1 before _name0). That matters because
// `FileInfo::operator=` runs `AdjustDelimiters` on BOTH name slots on BOTH
// paths and short-circuits self-assignment of the buffer — behaviour a
// hand-written clear does not have.
//
// The temporary leaves +0x04 unwritten, so the assignment copies a dword of
// retail's own stack garbage into the caller's FileInfo. That is faithful and
// it is why filesystem_openfile.cpp's oracle already excludes +0x04
// (`reference_oracle_must_not_compare_unwritten_bytes.md`).
// ============================================================================
void FileSystem::CloseFile(RtIO::FileInfo& infoRef) {
    RtIO::FileInfo* const info = &infoRef;
    PlatformCloseFile(*info);                        // 1800accde

    // FileInfo's default ctor on the temporary (1800acce3..acd0b: the four
    // fields, the two Filename() null stores 0x180006220, winHandle; +0x04
    // NOT written), FileInfo::operator= (1800acd1a), then ~FileInfo on the
    // temporary (1800acd20 +0x28 first, 1800acd50 +0x20).
    *info = RtIO::FileInfo();

    // ---- NOT PART OF THE PORT ------------------------------------------
    // A one-shot trigger for file_stream_slots_probe.cpp, which needs a LIVE
    // FileSystem singleton (it builds its paths through GetSaveDirectory) and
    // therefore cannot run from RuntimePortInit or from an atexit handler.
    //
    // This body is the latest-and-most-attended anchor this lane owns: 2854
    // calls on the gameplay route, all of them after the package mount. Call
    // 2000 is arbitrary but comfortably inside that. It is gated on the probe's
}

// ============================================================================
// IO::FileSystem::~FileSystem                             retail 0x1800ac6d0
// ============================================================================
// Six steps, in retail's order:
//   1. SubtreeFree(&packfileIndexRoot, packfileIndexRoot)    1800ac6ff
//   2. packfileIndexRoot = null; packfileIndexCount = 0      1800ac706/09
//   3. ~Array<Filename>(&userPackFilenames)  (+0x30)         1800ac710
//   4. ~Array<PackageFile*>(&packfiles)      (+0x20)         1800ac71a
//   5. release packDirectory (+0x10), dataDirectory (+0x08),
//      sourceDirectory (+0x00), in THAT order                1800ac720..79c
//
// NOTE THE CLASS HAS NO VTABLE. `FileSystem::sourceDirectory` is at +0x00 and
// the destructor's last act is to release it — a `QEAA` non-virtual destructor
// on a class whose first field is a String, which is why no table is stamped
// anywhere in this body. FileSystem.h already pins that layout.
//
// STEP 4's ELEMENT DESTRUCTOR IS A POD FREE, NOT A PER-ELEMENT WALK.
// 0x1800097f0 frees the block and zeroes the three header fields; it does NOT
// release the PackageFile* elements. That is retail: `ClosePackages` owns those
// and has already run. A port that "helpfully" destroyed the elements here
// would double-free every mounted package
// (`reference_a_written_recipe_can_be_wrong.md` is exactly this shape — two
// destructors that look interchangeable and are not).
// ============================================================================
// STEP 5 IS THE COMPILER'S. The three directories are `System::Filename`
// members (B4 X-R3): they are destroyed after this body, in reverse
// declaration order (+0x10, +0x08, +0x00), which is retail's 0x1800ac720 ..
// 0x1800ac79c, each under its own unwind state as member destructors are.
FileSystem::~FileSystem() {
    // Step 1. The tree object is (root, count) at +0x40/+0x48; retail passes
    // its ADDRESS as `this` and the root as the node.
    ::IO::PackIndexClear(&_packfileIndex);   // NOT RETAIL (block PATHINDEX): the nodes go next
    rtavl::SubtreeFree(&_packfileIndex, _packfileIndex.root);   // 1800ac6ff

    _packfileIndex.root  = nullptr;                    // 1800ac706
    _packfileIndex.count = 0;                          // 1800ac709

    // Step 3 — Array<Filename>, ported by modification.cpp and registered.
    RtIO::RtColl_ArrayFilenameDtor_1800098d0(&_userPackFilenames);   // 1800ac710

    // Step 4 — ~Array<PackageFile*> (0x1800097f0, called at 1800ac71a): the
    // `_packfiles` member's own destructor (B.4 Y RT), run by the compiler
    // after this body and before the three Filenames — retail's order.

    // Step 5: ~Filename on _packDirectory, _dataDirectory, _sourceDirectory, run
    // by the compiler after this body (1800ac720 / 1800ac74a / 1800ac775).
}

}  // namespace IO

