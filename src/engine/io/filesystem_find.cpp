// ============================================================================
// src/engine/io/filesystem_find.cpp — Wave 9 lane FIND
// The two null Exists() doors, and the FindFiles wall behind them
// ============================================================================
//
//   | Export / address                                  | Retail      | Ord  | Instrs |
//   |---------------------------------------------------|-------------|------|--------|
//   | ?ExistsInPackfile@FileSystem@IO@@QEBA_N…          | 0x1800ab180 | 2322 |   45   |
//   | ?PlatformExists@FileSystem@IO@@AEBA_N…            | 0x1800adc70 | 3378 |   27   |
//   | ?FindFiles@FileSystem@IO@@QEBAX…                  | 0x1800ad7a0 | 2350 |  289   |
//   | Array<Filename>::SetSize   (no name in any build)  | 0x180026880 |  --  |   61   |
//   | Array<Filename>::Destroy   (no name in any build)  | 0x180009af0 |  --  |   28   |
//
// All three MANGLED NAMES and all three ORDINALS above were printed straight
// out of Runtime-x64.dll's export directory with `pefile` BEFORE a line of this
// file was written — names from the name array, ordinals from the ordinal array
// plus the ordinal base, not transcribed from a note or a decompile. Lane PKG
// got seven of eight ordinals wrong reconstructing them by hand. The two
// internals have NO name in either shipped build.
//
// ---------------------------------------------------------------------------
// NO NEW IMPORT LIBRARY. Read this first; it is the one thing only the
// integrator can test.
// ---------------------------------------------------------------------------
// This file calls four Win32 entry points: GetFileAttributesW, FindFirstFileW,
// FindNextFileW, FindClose — through Platform::PathAttributes / FindFirst /
// FindNext / FindClose since Phase D.4 THREADS E.6 (platform_file.h; the file was
// `filesystem_find_win.cpp`). All four are KERNEL32, which MinGW links by
// default and which this tree already depends on — GetFileAttributesW is
// also called by filesystem_setupdirs.cpp's SetupDirectories.
//
// ---------------------------------------------------------------------------
// 1. WHY THE TWO SMALL ROWS OUTRANK THE BIG ONE
// ---------------------------------------------------------------------------
// `?Exists@FileSystem@IO@@` is ALREADY PORTED (filesystem.cpp:210) and its body
// is, in full:
//
//     if (fs->existsInPackfileGate && ExistsInPackfile(path)) return true;
//     if (!fs->platformExistsGate) return false;
//     return PlatformExists(path);
//
// Both callees are members ported below. `FileSystem::Startup` sets the
// packfile gate at `FileSystem+0x18` to 1 before it calls `LoadPackages`, so
// from that instant every `Exists()` in the process takes the packfile arm
// first.
//
// ---------------------------------------------------------------------------
// 2. THE INHERITED HOLDBACK, AND THE RULE IT PRODUCED
// ---------------------------------------------------------------------------
// Lane FS declined `ExistsInPackfile` because the packfile gate is 0. That was
// CORRECT where it was written — inside `SetupDirectories`, the gate really is
// 0 and the arm really is dead — and it is SPENT two functions later, because
// `FileSystem::Startup` deliberately writes that byte to 1 in between. Not
// stale by drift; not wrong when made. The state it depends on is changed by
// the code between the two call sites.
//
// So: **re-derive an inherited blocker against the state at YOUR call site, not
// the state at the site it was written for**, and say which function last
// writes that state. Every holdback this lane leaves behind (§8) names its
// state and its last writer, which is what makes a holdback re-checkable
// instead of inherited forever.
//
// ---------------------------------------------------------------------------
// 3. ?ExistsInPackfile@ — 0x1800ab180
// ---------------------------------------------------------------------------
// Read as disassembly, never as Ghidra's C (which drops dereferences and prints
// MOVSXD as a zero extension — reference_ghidra_c_output_vs_disassembly.md).
//
//     bool FileSystem::ExistsInPackfile(const Filename& path) const
//     {
//         if (GetLength(path) <= GetLength(this->dataDirectory)) return false;
//         String rel = path.data + GetLength(this->dataDirectory);
//         AdjustDelimiters(rel);
//         node = walk(this->packfileIndexRoot, rel);
//         ~rel;
//         return node != nullptr;
//     }
//
// **It is the SAME WALK, instruction for instruction, as the ported
// `GetPackfileId` (0x1800ab050)** — same length test (`CMP EDX,ECX ; JLE`, i.e.
// proceed only when the path is strictly longer than dataDirectory), same
// strip-by-length with no prefix compare, the same two calls at 0x1800c95d0
// (`String::Set`) and 0x1800c6970 (`Filename::AdjustDelimiters`), the same
// `_stricmp` at 0x18007eefc, the same node layout, and the same two-sided
// `!(a<b) && !(b<a)` equality idiom. Confirmed by disassembling BOTH and
// comparing the CALL targets, not by pattern-matching the shape:
// `disassemble_function(0x1800ab050, filter_mnemonics="CALL")` lists exactly
// 0x1800c95d0 / 0x1800c6970 / 0x18007eefc / 0x18007eefc / 0x1800c7ad0.
//
// The ONLY differences are the two ends: GetPackfileId returns
// `packfiles[node.packIndex]->packfileId` (or 0), this one returns
// `node != nullptr`. The descent below is therefore a deliberate transcription
// of the shared walk in the SAME shape filesystem.cpp already uses, so the two
// stay comparable by eye; it is not a call into that function, because retail
// does not do that either.
//
// NO ADDED NULL GUARDS. The `if (!key)` / `if (!str1)` arms below are retail's
// own — they are the `TEST RDX,RDX ; JZ` and `TEST RAX,RAX ; JZ` at 0x1800ab206
// and 0x1800ab20b — not defensive additions.
//
// ---------------------------------------------------------------------------
// 4. ?PlatformExists@ — 0x1800adc70, and why it turned out to be free
// ---------------------------------------------------------------------------
//     bool FileSystem::PlatformExists(const Filename& path) const
//     { WideString w(path); return GetFileAttributesW(w.data) != INVALID_FILE_ATTRIBUTES; }
//
// Twenty-seven instructions, of which fifteen are the inlined String release.
// `this` is never read: `LEA RCX,[RSP+0x50]` at 0x1800adc7f destroys RCX before
// anything looks at it, and RDX (the Filename) is passed straight through as
// the second argument. The call at 0x1800adc84 is 0x1800cab50 and the call at
// 0x1800adc8d is `[0x18014b0d0]`, which `pefile`'s import directory says is
// **KERNEL32!GetFileAttributesW** — read out of the import table, not guessed
// from `CMP EAX,-0x1` (though the compare is the confirmation: -1 is
// INVALID_FILE_ATTRIBUTES, not a HRESULT).
//
// **0x1800cab50 is ALREADY PORTED.** It is
// `??0WideString@System@@QEAA@AEBVString@1@@Z`, `System::WideString`'s ctor
// from a String (src/engine/system/widestring.cpp:421), the ctor the body below
// calls. So this row has ZERO unported dependencies, and the lines below are the
// same ones that already sit inline at filesystem_setupdirs.cpp:316-321,
// where lane FS transcribed this very body out of the MSVC-inlined copy in
// `SetupDirectories`. Two lanes reached the same lines from opposite
// directions; that agreement is worth more than either derivation alone.
//
// ---------------------------------------------------------------------------
// 5. ?FindFiles@ — 0x1800ad7a0, the wall
// ---------------------------------------------------------------------------
//     void FileSystem::FindFiles(const String& dir, const String& pattern,
//                                bool recursive, Array<Filename>& out) const
//
// Argument order pinned from the body, not from the mangling (the mangling has
// two `String const&` in a row and cannot tell them apart): RDX is the one that
// becomes the PREFIX of every concatenation and of the FindFirstFileW pattern,
// R8 is the one appended to it. So RDX = directory, R8 = pattern — which is
// also the order lane PKG's call site already passes. The fifth argument, the
// out array, is at `[RBP+0x210]` = entry `RSP+0x28`, i.e. the first stack slot
// past the four homes.
//
//   Filename dirAlias = dir;  AdjustDelimiters(dirAlias);   // *** see below
//   { WideString w(dir + pattern);  h = FindFirstFileW(w.data, &fd); }
//   if (h != -1) {
//       do {
//           if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
//           Filename e = dir + WideString(fd.cFileName).AsUTF8();
//           AdjustDelimiters(e);
//           out.SetSize(out.size + 1, e);
//       } while (FindNextFileW(h, &fd));
//       FindClose(h);
//   }
//   if (recursive) {
//       { WideString w(dir + "*");  h2 = FindFirstFileW(w.data, &fd); }
//       if (h2 != -1) {
//           do {
//               if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
//               if (fd.cFileName[0] == L'.') continue;
//               String sub = dir + WideString(fd.cFileName).AsUTF8() + "\\";
//               FindFiles(sub, pattern, recursive, out);
//           } while (FindNextFileW(h2, &fd));
//           FindClose(h2);
//       }
//   }
//   ~dirAlias;
//
// *** THE DEAD LOCAL THAT IS NOT DEAD, and the one thing here a "cleanup" would
// silently break. `dirAlias` at [RSP+0x58] is built at 0x1800ad7d3 by the
// inlined String copy-ctor idiom (`MOV RAX,[RDX] ; TEST ; MOV [RSP+0x58],RAX ;
// INC dword [RAX-4]`), handed to AdjustDelimiters at 0x1800ad7ed, released at
// 0x1800adc13 — and **never read**. Every concatenation in the body uses RSI,
// the caller's `dir`, directly.
//
// It is not dead code. `AdjustDelimiters`' first phase rewrites '/' to '\\'
// **in place, through the shared buffer** (see the port at
// src/engine/system/filename.cpp:219 — it stores through `_data`, and
// `_data` here IS the caller's buffer, refcounted, not copied). The local
// exists SOLELY for that side effect on the caller's string. Dropping it as an
// unused variable would compile, run, and silently stop normalising the
// caller's path. Transcribed as written, with the release in retail's place.
//
// (The second phase, the "\.." splice, replaces the buffer instead of editing
// it, so it does NOT propagate to the caller. That asymmetry is retail's; the
// transcription inherits it for free by calling the same ported function.)
//
// TWO LITERALS, READ AS BYTES rather than inferred from the code around them:
// 0x1801557e0 is `"*"` and 0x1801557e4 is `"\\"` (pefile `get_data`:
// `2a 00 00 00` and `5c 00 00 00`). The '.' test at 0x1800adaaf is
// `CMP word ptr [RBP-0x64],0x2e` — a compare of the FIRST WCHAR against L'.',
// which skips "." and ".." together and also any dot-file. One instruction, two
// cases; a port that spelled it as two `wcscmp`s would differ on a third.
//
// THE SUB-DIRECTORY ARM IS THE OPPOSITE TEST. The file loop skips entries with
// FILE_ATTRIBUTE_DIRECTORY (`TEST ... ; JNZ` at 0x1800ad892); the recursive
// loop skips entries WITHOUT it (`TEST ... ; JZ` at 0x1800adaa4). Same mask,
// inverted branch, two separate enumerations of the same directory with two
// different patterns. Retail really does open the directory twice.
//
// ---------------------------------------------------------------------------
// 6. Array<Filename>::SetSize @0x180026880 — the one genuinely new body
// ---------------------------------------------------------------------------
// `FindFiles` appends through it, so it had to come too. It is lane Q's J3
// algorithm — the SAME `SetSizeCore` in src/engine/collection/array.cpp — with
// a new element spec, and this file reuses that body through array_internal.h
// rather than writing a fifteenth copy of the growth rule. (That header says
// "only two files include this"; this is the third, and the reason is exactly
// the one the header gives: re-deriving `SetSizeCore` here would be testing a
// copy of the port instead of the port.)
//
// Read against lane Q's non-trivial exemplar 0x180061480, which has the same
// realloc/copy/destroy skeleton. Differences, each pinned to an instruction:
//
//   * stride 8, not 0x10 — `SHL RCX,0x3` at 0x1800268d7 (0x180061480 has
//     `SHL RCX,0x4`). align 8 — `MOV EDX,0x8` at 0x1800268db, same.
//   * THREE-argument form: the fill is R8, spilled to R12 at 0x1800268a0 and
//     read at 0x180026910 (`MOV RDX,R12`). Not one of lane Q's eight
//     two-argument instantiations; registering it behind that signature would
//     read a register the caller never set.
//   * the element assign is a CALL, not an inlined copy: 0x180026913 calls
//     0x1800067d0, which is `??0Filename@System@@QEAA@AEBV01@@Z` — already
//     ported (src/engine/system/filename.cpp:86). Filename's copy ctor zeroes `self`
//     before touching it, which is what makes it safe on the raw uninitialised
//     tail slots this loop hands it.
//   * the element destroy is 0x180009af0 (0x180061480 uses 0x180005930), so it
//     needs its own body — §6b.
//   * `JZ` at 0x180026906 tests the COMPUTED address `_data + i*8`, not the
//     index and not `_data`. SetSizeCore already models that exactly.
//
// 6b. The destroy helper at 0x180009af0 is 28 instructions and is the array
// form of the String release idiom: for each of `count` slots, `if (!p)
// continue; if (--p[-4] != 0) { slot = 0; } else { slot = p - 8; if (slot)
// Free(slot) under the allocator lock; }`. The `JZ` at 0x180009b3f takes its
// flags from `ADD qword ptr [RBX],-0x8` on the line before, so the guarded
// thing is the BLOCK BASE, not the string pointer — `RtMemFree` (0x1800c7ad0)
// is that Enter/Free/Leave with the null test inside it, which is why the code
// below hands it a possibly-null pointer exactly as retail does.
//
// Lane Q's J1 result — one body serving eleven addresses — does NOT
// generalise here: a destroy loop is element-typed, and another
// `Array<T>::Destroy` instantiation would release something other than a
// refcounted char buffer. Nothing is claimed about any other destroy address.
//
//
// ---------------------------------------------------------------------------
// 8. HOLDBACKS — each with the state it depends on and that state's LAST WRITER
// ---------------------------------------------------------------------------
// Nothing in this row's own call chain is held back; the three exports and both
// internals are ported. What `FindFiles` does NOT close, and why, in the form
// §2 argues for:
//
//   * The other `Array<T>::Destroy` instantiations. STATE: which element type
//     each address serves. LAST WRITER: MSVC, at link time — there is no
//     runtime state to re-check, so this one is closed by READING each body,
//     not by re-deriving a condition. Deliberately not attempted here.
//
// ============================================================================

#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstring>

#include "platform_file.h"   // Platform::PathAttributes / FindFirst / FindNext / FindClose

#include "io/FileSystem.h"
#include "system/String.h"
#include "system/Filename.h"
#include "system/string_internal.h"
#include "system/widestring_internal.h"
#include "collection/array_internal.h"
#include "io/packfile_fold_index.h"   // block PATHINDEX: the hash index beside _packfileIndex

#include <new>        // placement new into a raw Array<Filename> element
#include <utility>

// ENDGAME B.3.1 step 2: the two Wave-2 / lane-FS entry points this file
// forward-declared here — `RtSystem_Filename_CtorCopy` (0x1800067d0) and
// `RtSystem_Filename_AdjustDelimiters` (0x1800c6970) — are gone from this
// translation unit. The first is the class's copy ctor; every call to the
// second was the tail of a mutating Filename entry point (the `const char*`
// ctor 0x180026cf0, or the `const String&` ctor 0x18000c780), which the class
// spells as itself with AdjustDelimiters private inside it.

namespace RtIO {


// `FilenameArray` (the 16-byte out-parameter header, retail's
// `Collection::Array<Filename>`) is defined in src/engine/io/FileSystem.h,
// with the 0x180026880 pins.



// Case-insensitive ASCII compare, sign-preserving. Duplicated from
// filesystem.cpp for the reason that file gives: src/engine stays free of a
// CRT `_stricmp` whose locale behaviour differs between toolchains (PLAN.md
// §4.1). Retail calls the CRT's at 0x18007eefc, in the "C" locale (the DLL
// never calls setlocale), where it is plain ASCII case folding.
static int StriCmpAscii(const char* a, const char* b) {
    for (;;) {
        unsigned char ca = static_cast<unsigned char>(*a++);
        unsigned char cb = static_cast<unsigned char>(*b++);
        if (ca >= 'A' && ca <= 'Z') ca = static_cast<unsigned char>(ca + 32);
        if (cb >= 'A' && cb <= 'Z') cb = static_cast<unsigned char>(cb + 32);
        if (ca != cb) return static_cast<int>(ca) - static_cast<int>(cb);
        if (ca == 0) return 0;
    }
}

// ============================================================================
// Array<Filename>::SetSize @0x180026880  +  its destroy helper @0x180009af0
// See §6. The growth rule itself is lane Q's SetSizeCore; only the element ops
// and the spec row are new.
// ============================================================================

// 0x180026913: `CALL 0x1800067d0` = Filename::Filename(Filename const&).
// The destination is a raw array element, so the copy ctor is placement-
// constructed into it — which is exactly what retail's CALL does.
static void AssignFilename(uint8_t* d, const uint8_t* s) {
    new (d) System::Filename(*reinterpret_cast<const System::Filename*>(s));  // CAST-OK: elem_stride
}

// 0x180009af0. RCX is spilled at 0x180009af9 and never read again, which is why
// lane Q's ElemDestroy signature carries an ignored first argument at all.
void DestroyFilenameArray(void* /*ignoredThis*/, uint8_t* base, int count) {
    if (count <= 0) return;                        // TEST R8D,R8D ; JLE ret
    System::Filename* slot = reinterpret_cast<System::Filename*>(base);  // CAST-OK: elem_stride
    for (int i = 0; i < count; ++i, ++slot) {
        // ENDGAME B.3.1 step 2: ~Filename (0x180006980) IS this loop body, step
        // for step — the `TEST RAX,RAX ; JZ next` null test, the decrement of
        // the refcount at data-4, the `MOV [RBX],R14` sharing branch and the
        // `ADD qword ptr [RBX],-0x8` + MemFree freeing branch, with retail's
        // release asymmetry (String.h) intact. The JZ at 0x180009b3f takes its
        // flags from that ADD, so the free guard is on the BLOCK BASE, and
        // RtMemFree carries the same null test inside.
        slot->~Filename();
    }
}

// NO UNWRITTEN WINDOW IN EITHER ARM: AssignFilename writes the whole 8-byte
// element (the copy ctor's first statement is `self->data = nullptr`), and
// there is no init arm — retail's loop has no `else`, because this
// instantiation is always called with a fill.
//
// SEED ZEROS +0x00..+0x08: the entire element is a refcounted char* that both
// the assign (`++str[-4]`) and the destroy (`--str[-4]`) DEREFERENCE, so a
// probe must leave it zero. That is the whole stride, which is another way of
// saying this spec is not probe-seedable with garbage — noted rather than
// papered over.
static const rtcoll::ArraySpec kSpec_Filename = {
    "Array<Filename>", 8, 8, &AssignFilename, nullptr, &DestroyFilenameArray,
    /*byteCopy*/ false,          // the assign refcounts; it is NOT a byte copy,
                                 // so SetSizeCore's copied-prefix check stays off
    /*assign unwritten*/ 0, 0,
    /*init   unwritten*/ 0, 0,
    /*seed zero       */ 0x00, 0x08 };

void RtColl_ArraySetSizeFill_180026880(void* a, int n, const void* fill) {
    rtcoll::SetSizeCore(reinterpret_cast<rtcoll::ArrayHdr*>(a), n,  // CAST-OK: elem_stride
                        static_cast<const uint8_t*>(fill), &kSpec_Filename);  // CAST-OK: elem_stride
}

// ENDGAME B.3.2 step 2 — the internal caller now names the container it acts
// on. `kSpec_Filename`'s `init` field is null (this instantiation is always
// called with a fill, so no default-construct arm ever runs); every other
// field already agrees with `rtcoll::SpecFor<System::Filename>()` — `assign` IS
// the placement copy-construct, `destroy` IS the per-element `~Filename()`
// loop, `byteCopy` is false, because a copy-constructing type cannot be one.
// But it does NOT match SpecFor<Filename>() exactly (SpecFor would derive a
// non-null `init`, since Filename's default ctor is user-provided), so this
// stays the hand-derived row (kSpec_Filename), reached through
// `rtcoll::AsArray<&kSpec_Filename>` rather than through
// `Collection::Array<System::Filename>`'s trait-derived spec — never silently
// substituted for it (`out` is `FilenameArray*`, layout-identical to the
// 16-byte Array header `AsArray` reinterprets).
static void AppendFilename(FilenameArray* out, const System::Filename* value) {
    // 0x1800ad900: `MOV EDX,[RDI+8] ; INC EDX` — newSize is size+1, read fresh
    // from the header each iteration.
    rtcoll::AsArray<&kSpec_Filename>(out)->SetSizeFill(out->size + 1, value);
}

// ============================================================================
// ?ExistsInPackfile@FileSystem@IO@@QEBA_NAEBVFilename@System@@@Z  0x1800ab180
// See §3.
// ============================================================================

}  // namespace RtIO

// ============================================================================
// `IO::FileSystem`'s member bodies for this file — B34_BRIEFS section D BINDING
// ============================================================================
// Defined outside `namespace RtIO` because a member of `IO::X` must appear in a
// namespace ENCLOSING the class (B34_BRIEFS section A.2); these bodies reach
// this translation unit's `RtIO`-scoped names — `StriCmpAscii` — as
// `RtIO::X`. Every POD is spelled `RtIO::` or `RtSystem::` and every real class
// `System::` (B34_RULES section 7.2).
//
// *** VERBATIM. *** Identical control flow, identical callees, identical
// statement order, identical comments; only the receiver's spelling changed.
// `FindFilesImpl` — the file-local RECURSIVE half of FindFiles — is NOT moved
// onto the class and NOT touched: it recurses into itself at 0x1800adb2c and
// making it a member would change nothing but would put a live recursion inside
// a section-D-bound body for no gain.
namespace IO {

bool FileSystem::ExistsInPackfile(const System::Filename& pathRef) const {
    const System::Filename* const path = &pathRef;

    bool ours = false;

    // 0x1800ab1a0-0x1800ab1c2. Both lengths come from the allocation header at
    // `data-8` when data is non-null and are 0 otherwise, and the test is
    // `JLE`: proceed only when the path is STRICTLY longer.
    const char* pathData = path->_data;
    const int pathLen = pathData ? path->GetLength() : 0;
    const int dataDirLen = _dataDirectory._data
                         ? _dataDirectory.GetLength() : 0;

    if (dataDirLen < pathLen) {
        // 0x1800ab1d9: strip BY LENGTH ALONE — retail does no prefix compare
        // and trusts the caller passed a path under dataDirectory. String::Set
        // is what copies; the pointer arithmetic aliases the caller's buffer.
        // Set-then-AdjustDelimiters IS the Filename const char* ctor,
        // ??0Filename@System@@QEAA@PEBD@Z (0x180026cf0); its destructor is
        // retail's release at the bottom of this block.
        System::Filename relPath(pathData + dataDirLen);

        const char* str1 = relPath._data;
        // NOT RETAIL (block PATHINDEX): the hash index answers when it can.
        RtIO::PackfileIndexNode* indexed = nullptr;
        const bool answered = ::IO::PackIndexFind(&_packfileIndex, str1, &indexed);
        RtIO::PackfileIndexNode* node = _packfileIndex.root;
        RtIO::PackfileIndexNode* found = nullptr;
        if (answered && !OPENBA_PATHINDEX_CHECK) {
            found = indexed;
            node = nullptr;
        }
        while (node) {
            const char* key = node->element.key._data;
            // condA == "relPath >= key" in retail's null-extended ordering.
            bool condA;
            if (!key)       condA = true;
            else if (!str1) condA = false;
            else            condA = RtIO::StriCmpAscii(str1, key) >= 0;

            if (condA) {
                // condB == "key >= relPath". Both true means equal, which is
                // MSVC's `!(a<b) && !(b<a)` find — not a lower_bound.
                bool condB;
                if (!str1)     condB = true;
                else if (!key) condB = false;
                else           condB = RtIO::StriCmpAscii(key, str1) >= 0;

                if (condB) { found = node; break; }
                node = node->right;
            } else {
                node = node->left;
            }
        }

        ::IO::PackIndexCheck(answered, str1, indexed, found);
        ours = (found != nullptr);   // 0x1800ab281: TEST RBX,RBX ; SETNZ AL
        // ~relPath is retail's release at 0x1800ab28e.
    }

    return ours;
}

// ============================================================================
// ?PlatformExists@FileSystem@IO@@AEBA_NAEBVFilename@System@@@Z    0x1800adc70
// See §4. PRIVATE (AEBA) in retail, not QEBA.
// ============================================================================
bool FileSystem::PlatformExists(const System::Filename& pathRef) const {
    const System::Filename* const path = &pathRef;
    // retail never reads `this`; RCX is destroyed at 0x1800adc7f

    // ??0WideString(const String&) (0x1800cab50) over the Filename read AS a
    // String, GetFileAttributesW, then the inlined release: ~WideString
    // (0x18000a730) at the end of the block, the point the release stood.
    bool ours;
    {
        const System::WideString ws(path->AsString());
        ours = (Platform::PathAttributes(ws._data) != Platform::kInvalidPathAttributes);  // GetFileAttributesW
    }

    return ours;
}

}  // namespace IO

// `FindFilesImpl` goes back inside `namespace RtIO`:
// the helper is file-local, recursive and RtIO-scoped, and reopening the
// namespace keeps it exactly where it was rather than moving a section-D-bound
// recursive body. `IO::FileSystem::FindFiles` follows, after it.
namespace RtIO {

// ============================================================================
// ?FindFiles@FileSystem@IO@@QEBAX…                                0x1800ad7a0
// See §5. The member `IO::FileSystem::FindFiles` below calls this body, which
// recurses into itself.
// ============================================================================
// ENDGAME B.3.1 step 2: this helper is file-local and recursive, so its two
// String parameters are the classes. The member `IO::FileSystem::FindFiles`
// below bridges into this one.
static void FindFilesImpl(const void* fs, const System::String* dir,
                          const System::String* pattern,
                          bool recursive, FilenameArray* out) {
    // --- 0x1800ad7d3-0x1800ad7f2: the alias whose only purpose is its side
    //     effect on the caller's buffer. NOT DEAD — see §5.
    //     Adopt-then-AdjustDelimiters IS ??0Filename@System@@QEAA@AEBVString@1@@Z
    //     (0x18000c780), and that side effect is inside the ctor now.
    System::Filename dirAlias(*dir);

    Platform::FindData fd;   // WIN32_FIND_DATAW

    // --- 0x1800ad7f3-0x1800ad81a: FindFirstFileW(dir + pattern) -------------
    Platform::FindHandle h;
    {
        System::String search = *dir + *pattern;   // 0x1800c9440
        const System::WideString w(search);         // 0x1800cab50
        h = Platform::FindFirst(w._data, &fd);      // FindFirstFileW
        // Retail's release order: the WideString first (0x1800ad823), then the
        // concatenation (~search, 0x1800ad850) -- reverse declaration order.
    }

    // --- 0x1800ad888-0x1800ad9f1: the FILE loop -----------------------------
    if (h != Platform::kInvalidFindHandle) {
        do {
            // 0x1800ad892: skip DIRECTORIES. The recursive arm below inverts
            // exactly this branch.
            if (fd.attributes & Platform::kFileAttributeDirectory) continue;

            // WideString(const wchar_t*) 0x18000a6e0 inlined: the null store
            // 0x1800ad89d, Set 0x1800caf40; AsUTF8 0x1800cacd0 into utf8.
            const System::WideString nameW(fd.name);
            System::String utf8 = nameW.AsUTF8();

            System::String full = *dir + utf8;          // 0x1800c9440
            System::Filename entry(full);               // 0x18000c780

            AppendFilename(out, &entry);

            // B.4 Z MIRROR2 (`behaviour:`, known_port_issues row 581, read in
            // the disassembly): retail's order is entry (0x1800ad915), full
            // (0x1800ad94e), utf8 (0x1800ad97f), nameW LAST (0x1800ad9b0):
            // the four destructors, in reverse declaration order.
        } while (Platform::FindNext(h, &fd));           // FindNextFileW
        Platform::FindClose(h);                         // FindClose
    }

    // --- 0x1800ad9f7-0x1800adc0c: the RECURSIVE arm -------------------------
    if (recursive) {
        Platform::FindHandle h2;
        {
            System::String star = *dir + "*";   // 0x1800c92d0, literal 0x1801557e0
            const System::WideString w2(star);   // 0x1800cab50
            h2 = Platform::FindFirst(w2._data, &fd);   // FindFirstFileW
            // ~w2 then ~star: retail's two releases here, in that order.
        }

        if (h2 != Platform::kInvalidFindHandle) {
            do {
                // 0x1800adaa4: only DIRECTORIES. 0x1800adaaf: one WCHAR compare
                // against L'.', which covers "." and ".." together.
                if (!(fd.attributes & Platform::kFileAttributeDirectory)) continue;
                if (fd.name[0] == '.') continue;

                const System::WideString nameW(fd.name);       // 0x1800adaba / 0x1800caf40
                System::String utf8 = nameW.AsUTF8();          // 0x1800cacd0

                System::String sub    = *dir + utf8;      // 0x1800c9440
                System::String subDir = sub + "\\";       // 0x1800c92d0, 0x1801557e4

                // 0x1800adb2c: the pattern and the out array are passed
                // THROUGH unchanged; only the directory changes.
                FindFilesImpl(fs, &subDir, pattern, recursive, out);

                // B.4 Z MIRROR2 (row 581, disassembly read): subDir
                // (0x1800adb32), sub (0x1800adb63), utf8 (0x1800adb94), nameW
                // LAST (0x1800adbc5): the destructors, reverse declaration order.
            } while (Platform::FindNext(h2, &fd));      // FindNextFileW
            Platform::FindClose(h2);                    // FindClose
        }
    }

    // --- 0x1800adc13: ~dirAlias --------------------------------------------
}

}  // namespace RtIO

namespace IO {

void FileSystem::FindFiles(const System::String& dir,
                           const System::String& pattern,
                           bool recursive, RtIO::FilenameArray* out) const {

    const int32_t before = out->size;
    (void)before;
    RtIO::FindFilesImpl(this, &dir, &pattern, recursive, out);

}

}  // namespace IO

