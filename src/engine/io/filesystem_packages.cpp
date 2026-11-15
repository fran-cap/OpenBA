// ============================================================================
// src/engine/io/filesystem_packages.cpp — Wave 9 lane PKG
// IO::FileSystem::LoadPackages — the cutover wall of attempt 11
// ============================================================================
//
//   | Export                                       | Retail      | Ord  | Instrs |
//   |----------------------------------------------|-------------|------|--------|
//   | ?LoadPackages@FileSystem@IO@@QEAAXXZ         | 0x1800ab460 | 3209 |  430   |
//   | ?ClosePackages@FileSystem@IO@@QEAAXXZ        | 0x1800abab0 | 1728 |   90   |
//   | ?GetPackDirectory@FileSystem@IO@@QEBA?AV…XZ  | 0x180007c10 | 2828 |   15   |
//   | ?GetFileName@PackageFile@IO@@QEBA?AV…H@Z     | 0x180025450 | 2602 |   18   |
//
// Every mangled name above was read out of Runtime-x64.dll's own export
// directory with `pefile`
// BEFORE a line of this file was written — and so was every ORDINAL in this
// file, printed straight from the export table's ordinal array plus its
// ordinal base rather than transcribed from a note. That is not ceremony:
// the first draft of this header carried eight ordinals reconstructed by
// hand and SEVEN OF THE EIGHT WERE WRONG (ClosePackages 1904 vs 1728,
// GetPackDirectory 2872 vs 2828, GetFileName 2650 vs 2602, FindFiles 2306 vs
// 2350, ??0PackageFile 505 vs 289, ReadPackfile 3560 vs 3461, CloseFile 1900
// vs 1726). An ordinal is exactly the kind of detail that reads as evidence
// and is never checked by the compiler — not looked up in a decompile, and
// not typed from memory.
//
// NO WIN32 API. This file calls no OS function directly.
//
// ---------------------------------------------------------------------------
// 1. THE ROW WAS SIZED WRONG GOING IN, AND THE CORRECTION MATTERS
// ---------------------------------------------------------------------------
// The lane brief described LoadPackages as "~300 instructions". It is **430**,
// and the extra hundred are not padding: they are a THIRD loop (the path→entry
// index build) and a call to a packfile SORT that the brief's structural
// sketch did not mention at all. The sketch also had the two array-growth
// loops iterating over "[RSI+0x20]/[+0x28]/[+0x2c]" and "[RSI+0x30]/[+0x38]"
// as if those were two different destination arrays. They are not. BOTH loops
// APPEND TO THE SAME ARRAY at +0x20; the difference is the SOURCE they read:
// loop 1 walks the `*.pkg` files FindFiles just discovered, loop 2 walks the
// user/mod pack filenames already sitting in the FileSystem at +0x30.
// Everything below is transcribed from the disassembly at 0x1800ab460, not
// from that sketch or from Ghidra's C (which drops dereferences and prints
// MOVSXD as a zero extension — reference_ghidra_c_output_vs_disassembly.md).
//
// ---------------------------------------------------------------------------
// 2. THE BODY
// ---------------------------------------------------------------------------
//   if (this->existsInPackfileGate == 0) return;          // +0x18, the gate
//
//   ClosePackages(this);                                  // drop any previous mount
//   String pattern = "*.pkg";                             // literal 0x1801556f0
//   Filename packDir = this->GetPackDirectory();          // a copy of +0x10
//   Array<Filename> found;                                // {ptr, count} on the stack
//   this->FindFiles(packDir, pattern, /*recursive*/false, found);
//   ~packDir; ~pattern;
//
//   for (i = 0; i < found.count; ++i)                     // LOOP 1
//       append(this->packfiles, ReadPackfile(new PackageFile, found[i]));
//   if (this->packfileCount > 1) SortPackfiles(...);      // 0x1800264a0
//   for (i = 0; i < this->userPackFilenameCount; ++i)     // LOOP 2
//       append(this->packfiles, ReadPackfile(new PackageFile, this->userPackFilenames[i]));
//
//   for (pi = 0; pi < this->packfileCount; ++pi)          // LOOP 3 — the index
//       for (fi = 0; fi < packfiles[pi]->entryCount; ++fi) {
//           Filename key = packfiles[pi]->GetFileName(fi);
//           node = IndexFind(this->packfileIndexRoot, key);
//           if (node) { if (packfiles[node->packIndex]->overridable) node->value = {pi,fi}; }
//           else      BstInsert(&this->tree, &out, key, {pi,fi});
//           ~key;
//       }
//
//   ...then the temporary Filename array is released element by element and
//   its block freed.
//
// The `*.pkg` literal was read with `read_memory` at 0x1801556f0 and is the
// five bytes `*.pkg`, NUL-terminated — not guessed from the function's name.
// It agrees with the install on disk: C:\GOG Games\Banished\WinData holds
// data0.pkg, data1.pkg, patch0.pkg and patch1.pkg, and lane FS independently
// established that GetPackDirectory's +0x10 resolves to that same WinData
// directory. So loop 1 runs FOUR times on a stock install, which is the fact
// §5 turns on.
//
// ---------------------------------------------------------------------------
// 3. THE PACKFILE GATE IS OPEN HERE, AND THAT IS THE WHOLE DIFFERENCE FROM
//    LANE FS
// ---------------------------------------------------------------------------
// `??0FileSystem@IO@@QEAA@XZ` (0x1800ac7c0) writes `MOV word ptr [RCX+0x18],
// 0x100`, i.e. +0x18 = 0 and +0x19 = 1. Lane FS used exactly that to prove the
// packfile arm of `FileSystem::Exists` was DEAD inside SetupDirectories.
//
// It is not dead here. `FileSystem::Startup` sets `this->existsInPackfileGate
// = 1` BETWEEN the two calls (src/engine/io/filesystem_startup.cpp), so by the
// time control reaches LoadPackages the byte is 1 — and attempt 11's crash
// dump read `this[+0x18] = 0x101`, which is the same fact measured live rather
// than derived. The `JZ epilogue` at 0x1800ab483 is therefore NOT taken and the
// entire body below is live.
//
//
// ---------------------------------------------------------------------------
// 4. LoadPackages' CALLEES
// ---------------------------------------------------------------------------
//
//   ?FindFiles@FileSystem@IO@@QEBAX…      0x1800ad7a0  ord 2350   289 instrs
//       The `*.pkg` enumeration. A FindFirstFileW/FindNextFileW/FindClose loop
//       through dispatch slots 0x18014b0d8/0e0/0e8, plus a recursive second
//       half behind its `bool` parameter (passed FALSE here). Its own callees
//       are already ported: WideString ctor/Set/AsUTF8 (lane FS), String
//       operator+ (0x1800c9440), Filename::AdjustDelimiters (0x1800c6970);
//       and Array<Filename>::SetSize at 0x180026880 (filesystem_find.cpp).
//   ??0PackageFile@IO@@QEAA@XZ            0x1800b4440  ord  289    48 instrs
//       A plain field-initialising constructor, and cheap. It writes three
//       vtable pointers; PackageFile's is described in §7.
//   ?ReadPackfile@PackageFile@IO@@…       0x1800b46b0  ord 3461  6,597 BYTES
//       The .pkg format parser itself. This is a lane, not a tail-end.
//   ?CloseFile@FileSystem@IO@@…           0x1800accc0  ord 1726
//   FUN_180008340 (recursive index-node free)          0x180008340
//       ***PORTED 2026-09-08 by Wave 9 lane COLL*** (avl_trees.cpp). Note it
//       releases exactly ONE element slot (the key String) and NOT the second:
//       here that slot is the packed {packIndex,fileIndex} qword, and the same
//       body serves other instantiations, so a symmetric release would corrupt
//       the heap on the first call.
//   FUN_180025a80 → FUN_180025c70 (BST insert)         0x180025a80
//       ***PORTED 2026-09-08 by Wave 9 lane COLL***
//       (src/engine/collection/avl_trees.cpp, group D), together with
//       0x180008520, 0x180008840 and 0x180025e10. The prediction below is
//       exactly right and is worth keeping: it IS the insert twin of the config
//       map's find, comparator and null-key asymmetries included, and it IS
//       keyed with _stricmp rather than the signed strcmp. It is also a full
//       AVL insert, not a plain BST one — 0x180025e10 is a rebalancing
//       retrace and the node carries a balance and a height.
//       …which in turn needs FUN_180008520 (node alloc) and FUN_180025e10
//       (the rebalance). Structurally the insert twin of the config map's find
//       that src/engine/system/configuration.cpp already ports, comparator and
//       null-key asymmetries included — but keyed with _stricmp (see below),
//       not the signed-char strcmp that file uses.
//   FUN_1800264a0 (the packfile sort)                  0x1800264a0  110 instrs
//       ***PORTED 2026-09-08 by Wave 9 lane COLL***
//       (src/engine/collection/introsort.cpp). The objection recorded here and
//       at the call site was correct and is what the port had to answer, not a
//       reason not to do it: an oracle DOES exist, because the shipped body is
//       a LEAF (no CALL instructions) and a sort is a pure permutation of a
//       caller-owned buffer. A bare LoadLibrary harness ran both over 784,000
//       tie-heavy cases and compared the PERMUTATION, not the sortedness:
//       0 differences, with a stable-sort negative control caught in 7,571 of
//       7,800 cases. The tie rule the comment feared is one instruction:
//       `CMOVG`, not `CMOVGE`, so among equal keys the EARLIEST position wins.
//       The order is DESCENDING.
//   FUN_18007eefc / FUN_18007eec0 (_stricmp)           0x18007eefc
//       ***PORTED 2026-09-08 by Wave 9 lane COLL***
//       (src/engine/crt/msvc_stricmp.cpp). The warning below is confirmed and
//       QUANTIFIED: a signed-byte lowering comparison disagrees in SIGN with
//       this one in 169,849 of 400,000 randomised high-byte pairs, so a shared
//       "TreeStrCmp" really would be wrong for one of the two trees.
//       The CRT's own copy. 0x18007eec0 is `__ascii_stricmp`: it lowers only
//       'A'-'Z' and compares the bytes UNSIGNED (`MOVZX`). Worth writing down
//       because it is the MIRROR IMAGE of the trap configuration.cpp records:
//       that file's comparator must use SIGNED chars, this one must not.
//       A single shared "TreeStrCmp" serving both would be wrong for one of
//       them on any byte >= 0x80 — and a package path can contain one.
//
// ---------------------------------------------------------------------------
// 5. THE ORACLE: THERE IS NONE, AND HERE IS WHY, RATHER THAN A WEAK ONE
// ---------------------------------------------------------------------------
// LoadPackages cannot be replayed and cannot be shadowed on a scratch object:
//
//   * It is NOT a pure read. It heap-allocates one 0xF0-byte PackageFile per
//     package, opens each .pkg on disk (172 MB of them on this install), grows
//     an array, builds a tree of one node per FILE INSIDE every package, and
//     frees a temporary array. Running it twice mounts every package twice.
//   * A scratch-`FileSystem` stand-in — the trick that made lane FS's
//     SetupDirectories replayable — does not work either. SetupDirectories
//     reads two bytes and writes three Strings; LoadPackages' output IS the
//     process's file-system state, and the objects it builds are handed to a
//     singleton the rest of the engine reads.
//
//
// ---------------------------------------------------------------------------
// 7. ??_7PackageFile@IO@@6B@, AND HOW ITS SIZE WAS ESTABLISHED
// ---------------------------------------------------------------------------
// The vtable:
//
//     ??_7PackageFile@IO@@6B@   DLL 0x180155700   9 slots
//
// Size established by READING THE BYTES at 0x180155700, not by trusting the
// mangled name — the name says nothing about length, and this wave has already
// paid once for a `PAD` suffix that put an 8 KB buffer in an 8-byte slot. Nine
// consecutive qwords there are code addresses inside .text (0x1800259d0,
// 0x180025150, 0x180025130, 0x1800b6080, 0x1800b6090, 0x1800b60a0, 0x1800252d0,
// 0x180025300, 0x1800252f0); the TENTH qword is 0x0000000000000000 and the
// bytes immediately following it are the ASCII "%08lX%04hX%04hX…" format
// string, i.e. the next object in .rdata. So the table is nine slots and the
// terminator is observed, not assumed.
//
// ============================================================================

#include <cstdint>
#include <cstddef>
#include <cstdio>

#include "rt_libm.h"        // rtcrt::StrICmp
#include "io/FileSystem.h"
#include "io/PackageFile.h"   // ENDGAME B.3.4a closing: the two members
#include "system/String.h"
#include "system/Filename.h"
#include "collection/Collection.h"
#include "system/string_internal.h"
#include "collection/avl_trees.h"   // rtavl::FilenameSetInsert
#include "io/packfile_fold_index.h"   // block PATHINDEX: the hash index beside _packfileIndex

#include <new>        // placement new into the retail hidden return slot
#include <utility>

namespace RtIO {

using RtSystem::RtMemAlloc;
using RtSystem::RtMemFree;

namespace {

// `Collection::Array<Filename>` as LoadPackages builds it on its own stack is
// `RtIO::FilenameArray` (src/engine/io/FileSystem.h), the 16-byte header; the
// body zeroes it at 0x1800ab48e/96. FindFiles receives `&found` and owns the
// growth.

// ClosePackages passes `IO::FileSystem::_singleton` (ord 4811, retail
// 0x1801fc6a8), not its own `this`, to CloseFile — retail loads
// `MOV RCX,qword ptr [0x1801fc6a8]` at 0x1800abaef even though `this` is in
// RDI and is almost certainly the same object. Reproduced literally.

// ---- the array-growth idiom, inlined TWICE by retail ----------------------
//
// This is Collection::Array<PackageFile*>::SetSize(n+1) with the element
// written in, and MSVC emitted it in full at 0x1800ab5bb and again at
// 0x1800ab75d — the two copies are instruction-for-instruction identical apart
// from register allocation. One C++ function serves both, exactly as
// src/engine/collection/array.cpp does for its eleven identical destructors.
//
// Four things a "cleanup" would quietly break, so none of them is cleaned up:
//
//  * `if (newCount != oldCount)` guards the whole thing. newCount is
//    oldCount+1, so it is always true. Retail tests it anyway; so do we.
//  * The growth factor is `newCount * 3`, and ONLY when this is the very first
//    element past capacity (`newCount == oldCount+1`) AND the array was not
//    empty (`oldCount != 0`). A first-ever append therefore allocates capacity
//    1, not 3 — `LEA EAX,[R13+0x1] ; CMP R14D,EAX ; JNZ ; TEST R13D,R13D ; JZ`
//    at 0x1800ab5de.
//  * The copy loop starts at `oldCount` when the block did NOT move
//    (`CMOVZ EAX,R13D` on `CMP R15,[RSI+0x20]`) and at 0 when it did. It is one
//    loop doing both the memcpy and the append.
//  * `if (dst)` inside the copy loop is retail's and is kept: it can only fire
//    when the base is null, which is the out-of-memory path.
//
// The allocation goes through RtMemAlloc, which is
// `?MemAlloc@MemoryAllocator@System@@SAPEAX_KH@Z` (0x1800c7a50) — verified by
// disassembly to be exactly the lock / Allocate(_singleton,size,align) /
// unlock sequence retail inlines here, same slots (0x18014b390 / 0x18014b388),
// same allocator singleton (0x1801fc718), same align 8. Likewise RtMemFree is
// 0x1800c7ad0 and carries retail's `if (p)` inside itself, which is why the
// callers below hand it a possibly-null pointer exactly as retail does.
void AppendPackfile(::IO::FileSystem* self, ::IO::PackageFile* obj) {
    ::IO::PackageFile** const old  = self->_packfiles._data;
    const int32_t             oldN = self->_packfiles._size;
    const int32_t             newN = oldN + 1;
    if (newN == oldN)
        return;
    self->_packfiles._size = newN;
    if (newN > self->_packfiles._capacity) {
        self->_packfiles._capacity = newN;
        if (newN == oldN + 1 && oldN != 0)
            self->_packfiles._capacity = newN * 3;
        // MOVSXD RBX,dword[RSI+0x2c] ; SHL RBX,3 — a SIGN extension of the
        // capacity, then times 8. Ghidra's C prints this as a zero extension.
        const uint64_t bytes =
            static_cast<uint64_t>(static_cast<int64_t>(self->_packfiles._capacity)) * 8u;
        self->_packfiles._data = static_cast<::IO::PackageFile**>(RtMemAlloc(bytes, 8));  // LEFT: an alloc result; the tier-I shape does not match the RtMemAlloc spelling
    }
    int64_t k = (old == self->_packfiles._data) ? static_cast<int64_t>(oldN) : 0;
    for (; k < static_cast<int64_t>(newN); ++k) {
        ::IO::PackageFile** const dst = self->_packfiles._data + k;
        if (dst != nullptr)
            *dst = (k < static_cast<int64_t>(oldN)) ? old[k] : obj;
    }
    if (old != self->_packfiles._data && old != nullptr)
        RtMemFree(old);
}

// ---- the path -> {pack,file} index find -----------------------------------
//
// Transcribed from 0x1800ab8b6-0x1800ab90c. It is the same walk, with the same
// two ASYMMETRIC null-key arms, that src/engine/system/configuration.cpp
// ports for the config map at 0x18002ace0 — a null SEARCH key returns the
// current node, a null NODE key descends — and it is the same walk again inside
// the insert at 0x180025c70 (avl_trees.cpp). The ONE difference, and it is the whole
// reason this is not shared code with configuration.cpp: the comparator here is
// the CRT's `_stricmp` (0x18007eefc → 0x18007eec0), which lowercases 'A'-'Z'
// and compares UNSIGNED bytes, whereas the config map inlines a SIGNED-char
// strcmp. On any byte >= 0x80 those two disagree on the sign, and a package
// path can contain one.
//
// The comparator 0x18007eefc has no export name in either build; it is
// rtcrt::StrICmp (src/engine/crt/msvc_stricmp.cpp), called by name (B3R RT-IO).
int PathStrICmp(const char* a, const char* b) {
    return rtcrt::StrICmp(a, b);
}

PackfileIndexNode* IndexFind(PackfileIndexNode* node, const char* key) {
    while (node != nullptr) {
        if (node->element.key._data != nullptr) {
            if (key == nullptr)                     { node = node->left; continue; }
            if (PathStrICmp(key, node->element.key._data) < 0)    { node = node->left; continue; }
        }
        // rightTest — 0x1800ab8e6
        if (key == nullptr)
            return node;
        if (node->element.key._data != nullptr && PathStrICmp(node->element.key._data, key) >= 0)
            return node;
        node = node->right;
    }
    return nullptr;
}

}  // namespace

// ============================================================================
// ?GetPackDirectory@FileSystem@IO@@QEBA?AVFilename@System@@XZ  retail 0x180007c10
// ============================================================================
//     Filename FileSystem::GetPackDirectory() const
//     { return Filename(this->packDirectory); }      // copy ctor, AddRef only
//
// A String-RETURNING const member, so the hidden return slot is the SECOND
// argument (RCX=this, RDX=ret) and RAX is the return slot on the way out —
// `MOV RAX,RBX` at 0x180007c43. Getting that wrong is
// reference_hidden_retslot_wild_write.md.
//
// The `MOV dword ptr [RSP+0x20],0` / `,1` pair around the call is MSVC's EH
// state slot for the partially-constructed return value. It has no effect on
// the object and is not reproduced.
//
// **+0x08 and +0x10 share one buffer on a stock install.** SetupDirectories
// seeds packDirectory as a copy of dataDirectory (retail's inlined
// `String::operator=` with RSI = this+0x10, RDI = this+8), and that assignment
// AddRefs the same buffer rather than duplicating it. So unless a `-pathpkg`
// switch is passed, this->dataDirectory and this->packDirectory are LITERALLY
// THE SAME POINTER, and reading the wrong one of the two would not show at run
// time.
//
// What pins the field is the disassembly: `LEA RDX,[RCX+0x10]` at
// 0x180007c2e.
// ENDGAME B.3.4 PACKAGE+FS commit 5b: the three IO::FileSystem bodies in this
// file are members of the class (GetPackDirectory, LoadPackages, ClosePackages).
// This file is bound by B34_BRIEFS section D — nothing added, removed or
// reordered on the package mount/unmount path.

// ============================================================================
// ?GetFileName@PackageFile@IO@@QEBA?AVFilename@System@@H@Z    retail 0x180025450
// ============================================================================
//     Filename PackageFile::GetFileName(int i) const
//     { return Filename(*(Filename*)((char*)this->entries + i * 16)); }
//
//   MOVSXD RDX,R8D ; SHL RDX,0x4 ; ADD RDX,qword ptr [RCX+0xb0]
//
// The MOVSXD is a SIGN extension of the int index — the same instruction
// Ghidra's C prints as a zero extension, which cost Wave 6 three defects. It
// only matters for a negative index, which retail never passes; it is written
// as int64 anyway because that is what the instruction says.
//
// There is NO bounds check. Retail's caller (LOOP 3 below) bounds the index by
// `entryCount` itself, so the unguarded read is load-bearing and stays.
//
// Pure; reads `this` and writes only the return slot.
// ENDGAME B.3.4a closing: the body is `IO::PackageFile::GetFileName(int)`,
// defined in this file's `namespace IO` block below.
//
// Section D applies: nothing added, removed or reordered, and in particular no
// bounds check -- retail has none and the caller (LOOP 3 below) bounds the index
// with entryCount itself, so the unguarded read is load-bearing and stays.

// ============================================================================
// ?ClosePackages@FileSystem@IO@@QEAAXXZ                      retail 0x1800abab0
// ============================================================================
// Unmounts everything LoadPackages mounted: closes each package's file handle,
// runs each PackageFile's deleting destructor, frees the array, then destroys
// the path index tree and zeroes both containers.
//
// ---------------------------------------------------------------------------
// ON THE BOOT PATH ITS ENTIRE BODY IS UNREACHABLE, AND THAT IS WHY IT LANDS
// ---------------------------------------------------------------------------
// This is lane FS §2's control-flow-reach pruning applied again. The FIRST call
// in the process comes from FileSystem::Startup on a freshly constructed
// FileSystem: packfileCount is 0, so the loop body never runs; packfiles is
// null, so the free is a no-op; packfileIndexRoot is null, so the tree teardown
// is skipped. So on that call its three callees — CloseFile, the recursive
// node free at 0x180008340, and PackageFile's deleting destructor through
// vtable slot 0 — are not reached.
//
// **THAT IS TRUE OF THE FIRST CALL ONLY.**
// The mods menu remounts packages (see project_mods_menu_closed.md), which
// calls this again with a POPULATED array and a POPULATED tree — and then all
// three run.
//
// ORACLE: none, and it is not close. It frees everything it touches; running it
// twice is a double free, and there is no scratch stand-in because the objects
// it destroys are the process's real packages.

}  // namespace RtIO

// ============================================================================
// `IO::FileSystem`'s member bodies for this file — B34_BRIEFS section D BINDING
// ============================================================================
// Defined outside `namespace RtIO` because a member of `IO::X` must appear in a
// namespace ENCLOSING the class (B34_BRIEFS section A.2); these bodies reach
// this translation unit's `RtIO`-scoped names — the anonymous-namespace helpers
// `AppendPackfile`, `IndexFind`, `PathStrICmp`, and the alias `PackfileIndexSet`
// (FileSystem.h) — as
// `RtIO::X`. Every POD is spelled `RtIO::` or `RtSystem::` and every real class
// `System::` (B34_RULES section 7.2).
//
// *** VERBATIM. *** Identical control flow, identical callees, identical
// statement order, identical comments. The only change is the receiver's
// spelling. LoadPackages' `ClosePackages()` is a member call; retail's call
// from LoadPackages is at 0x1800ab48a.
namespace IO {

System::Filename FileSystem::GetPackDirectory() const {
    // The Filename copy ctor on +0x10, as retail's `LEA RDX,[RCX+0x10]`.
    return System::Filename(_packDirectory);
}

void FileSystem::ClosePackages() {
    const int32_t hadPackfiles = _packfiles._size;
    const bool    hadIndex     = _packfileIndex.root != nullptr;

    for (int32_t i = 0; i < _packfiles._size; ++i) {
        // Retail RELOADS the array base on every iteration and indexes it twice
        // (0x1800abae0 and 0x1800abafb) rather than caching the element. Kept:
        // the deleting destructor below could in principle write the array.
        PackageFile* pkg = _packfiles[i];

        // CloseFile(FileSystem::_singleton, &pkg->_file). The FileInfo lives
        // at pkg+0xC0 (`ADD RDX,0xc0`) and the receiver is the SINGLETON, not
        // `this` — see the note above AppendPackfile.
        _singleton->CloseFile(pkg->_file);

        PackageFile* again = _packfiles[i];
        if (again != nullptr) {
            // `MOV RAX,[RCX] ; MOV EDX,1 ; CALL qword ptr [RAX]` — vtable slot
            // 0, the VECTOR deleting destructor, flag 1 = also free the block.
            // Slot 0 of ??_7PackageFile@IO@@6B@ is 0x1800259d0 (§7); it is
            // reached through the object's own vtable pointer, faithfully, and
            // not by address. B3X W0: by name on the object's own class.
            delete again;
        }
    }

    // `if (p) { lock; Free; unlock }` with the null test inside 0x1800c7ad0.
    RtIO::RtMemFree(_packfiles._data);
    _packfiles._data = nullptr;
    // Retail zeroes +0x28 as ONE QWORD (`MOV qword ptr [RDI+0x28],RBP`), which
    // clears the count AND the capacity. Unlike Array::SetSize, this DOES
    // shrink capacity — the next LoadPackages therefore reallocates from 1.
    _packfiles._size    = 0;
    _packfiles._capacity = 0;

    ::IO::PackIndexClear(&_packfileIndex);   // NOT RETAIL (block PATHINDEX): the nodes go next
    RtIO::PackfileIndexNode* const root = _packfileIndex.root;
    if (root != nullptr) {
        // ENDGAME B.3.2 step 2 — FUN_180008340, the recursive subtree free, IS
        // `Collection::Set<PackfileIndexPair>::DestroySubtree` (Collection.h):
        // both children freed post-order first, then the node's own element
        // destructor (releases the key Filename with retail's asymmetric
        // idiom — the freeing branch leaves the slot holding `data - 8`, not
        // null; `~Filename` reproduces it, String.h), then the node itself
        // freed through the same allocator door `detail::SetNodeFree` reaches
        // (0x180009c30). A single `DestroySubtree(root)` frees the identical
        // set of nodes in the identical order retail's split call
        // (free(left), free(right), then unwind the root by hand) produced:
        // the whole left subtree post-order, then the whole right subtree
        // post-order, then the root — DestroySubtree just does both halves
        // under one call instead of the caller doing the root's half by hand.
        RtIO::PackfileIndexSet::DestroySubtree(root);
    }
    // Zeroed unconditionally, even when root was already null.
    _packfileIndex.count = 0;
    _packfileIndex.root  = nullptr;
    // NOT RETAIL (fix.large_packages, docs/extensions/handins/LARGEPKM.md): the packages are gone,
    // so are their 64-bit offsets. The next LoadPackages rebuilds them.
    _extWidePackages = nullptr;
    _extWideCount    = 0;
    // EXT SEAM (NOT RETAIL, docs/extensions/handins/MENUENTRY.md): the packages are gone; ext.menu_entry
    // forgets its remount fingerprint unless this close is the remount's own.
    if (_extPackagesClosed != nullptr) _extPackagesClosed(*this);

}

// ============================================================================
// ?LoadPackages@FileSystem@IO@@QEAAXXZ                       retail 0x1800ab460
// ============================================================================
// See §2 for the shape and §5 for why there is no oracle.
void FileSystem::LoadPackages() {

    // 0x1800ab47f — the packfile gate. See §3: it is 1 by the time we get here,
    // set by FileSystem::Startup between SetupDirectories and this call, so the
    // early return is NOT the live path. It is transcribed because it IS the
    // body's first instruction, and because `-onlypkg` is not the only thing
    // that can clear the byte over the life of a process.
    if (_existsInPackfileGate == 0)
        return;

    ClosePackages();

    RtIO::FilenameArray found;
    found.data     = nullptr;   // MOV qword ptr [RBP-0x20],0
    found.size     = 0;         // MOV qword ptr [RBP-0x18],0
    found.capacity = 0;

    // The two path values live only until FindFiles returns; the inner scope is
    // retail's release point, and the destructors run in retail's order —
    // packDir (+0x50) first, then pattern (+0x48).
    {
        System::String pattern("*.pkg");   // literal read at 0x1801556f0

        // GetPackDirectory's by-value return is constructed straight into
        // `packDir` (the hidden return slot): one construction, one release.
        System::Filename packDir(GetPackDirectory());

        // FindFiles(dir, pattern, recursive=false, out). Retail passes the value
        // GetPackDirectory RETURNED (RAX) as RDX, which is the same pointer as
        // &packDir — the hidden return slot is handed straight on. Fifth argument
        // goes on the stack at [RSP+0x20].
        FindFiles(packDir.AsString(), pattern, false, &found);
    }

    // ---- LOOP 1: one PackageFile per discovered *.pkg ---------------------
    // Retail allocates 0xF0 bytes aligned 8 under the allocator lock, runs the
    // constructor only `if (p)`, then ReadPackfile, then appends. ENDGAME
    // B.3.9 IO: the constructor is `IO::PackageFile`'s own, on the same block.
    for (int32_t i = 0; i < found.size; ++i) {
        void* block = RtIO::RtMemAlloc(0xF0u, 8);
        PackageFile* pkg = nullptr;
        if (block != nullptr)
            pkg = ::new (block) IO::PackageFile();   // B.3.9 IO: the real ctor
        // Unguarded on `pkg` — retail calls ReadPackfile with whatever the
        // constructor branch left in R12, including zero on the out-of-memory
        // path (`XOR R12D,R12D` at 0x1800ab5a5). Faithful; not a null check.
        pkg->IO::PackageFile::ReadPackfile(found.data[i]);
        RtIO::AppendPackfile(this, pkg);
    }

    // ---- the packfile sort ------------------------------------------------
    // FUN_1800264a0(packfiles, count) — an MSVC introsort over PackageFile*
    // whose ENTIRE comparator is `MOV EAX,[RAX+0x70] ; CMP dword ptr [RCX+0x70],
    // EAX`, i.e. the +0x70 priority, and which orders DESCENDING (its selection
    // pass swaps the ARGMIN to the tail — `CMOVG RDX,R8` moves the tracker to
    // the SMALLER key). The third argument retail passes at the call site
    // (`LEA R8,[RBP+0x48]`) is DEAD: the callee never reads R8.
    //
    // ***PORTED 2026-09-08*** — src/engine/collection/introsort.cpp.
    //
    // The paragraph this replaces said, correctly: "It is 110 instructions of
    // introsort with an explicit 62-entry pending-range stack, median-of-3 and
    // two partition scans, and every PackageFile arrives here with the SAME
    // +0x70 (the constructor seeds 0x2710 and only ReadPackfile changes it). A
    // tie-heavy population is exactly where an unstable sort's exact swap
    // sequence decides the answer, and that answer decides which package owns a
    // colliding path in LOOP 3. A 'clean' std::sort here would compile, run,
    // and silently pick different winners. With no run and no oracle available
    // in this lane, transcribing it would be an unverified guess at a
    // precedence rule."
    //
    // Every clause of that stands. What changed is the last one: an oracle DOES
    // exist. The shipped body is a LEAF — `filter_mnemonics="CALL"` returns
    // nothing — so it needs no allocator, no engine and no boot, and a sort is
    // a pure permutation of a caller-owned buffer, so it replays. A bare
    // LoadLibrary harness ran ours and the shipped body over 784,000 cases
    // (key domains 1/2/3/5/17/1000/random, sizes 1..300, 112,000 of them
    // ALL-TIES) and compared the PERMUTATION rather than the sortedness:
    // **0 differences**. A stable sort by the same key — correctly descending,
    // differing only in tie order — was caught in 7,571 of 7,800 cases, so the
    // comparison is demonstrably sensitive to exactly the defect feared.
    //
    // Four all-equal packages never leave the n<=8 path, so on the live boot call
    // alone the partition, pivot-fixup and equal-run counters all read 0 and a
    // green run would be vacuous.
    //
    // B3R RT-IO: the call is rtsort::SortByKey70 by name (it was the address
    // door, which resolved to the same body).
    if (_packfiles._size > 1) {
        rtsort::SortByKey70(_packfiles._data, _packfiles._size);
    }

    // ---- LOOP 2: the user/mod pack filenames already on the FileSystem -----
    // Same allocation and append as loop 1; the only difference is the source
    // (this->userPackFilenames[i], +0x30, bounded by +0x38) and that there is
    // NO sort afterwards — mods keep the order they were mounted in.
    for (int32_t i = 0; i < _userPackFilenames.size; ++i) {
        void* block = RtIO::RtMemAlloc(0xF0u, 8);
        PackageFile* pkg = nullptr;
        if (block != nullptr)
            pkg = ::new (block) IO::PackageFile();   // B.3.9 IO: the real ctor
        pkg->IO::PackageFile::ReadPackfile(_userPackFilenames.data[i]);
        RtIO::AppendPackfile(this, pkg);
    }

    // ---- LOOP 3: the path -> {packIndex, fileIndex} index ------------------
    // EXT SEAM (NOT RETAIL, docs/extensions/handins/PKGCACHE.md): ext.package_cache may build the
    // identical tree from the derived cache; the loop then runs over no package.
    const bool restored = _extIndexRestore != nullptr && _extIndexRestore(*this);
    const int32_t indexedPacks = restored ? 0 : _packfiles._size;
    int64_t inserted = 0, overridden = 0, kept = 0;
    for (int32_t pi = 0; pi < indexedPacks; ++pi) {
        PackageFile* pkg = _packfiles[pi];
        const int32_t entries = pkg->_entries._size;      // MOV EAX,[RCX+0xb8]
        for (int32_t fi = 0; fi < entries; ++fi) {
            // Retail RE-INDEXES the array here too rather than reusing `pkg`.
            PackageFile* owner = _packfiles[pi];

            // GetFileName writes through the hidden return slot (retail passes
            // an untouched [RBP+0x50]); the by-value return constructs `key` in
            // place, and the one release is ~key at the bottom of the iteration.
            System::Filename key(owner->IO::PackageFile::GetFileName(fi));

            // NOT RETAIL (block PATHINDEX): the hash index answers when it can.
            RtIO::PackfileIndexNode* node = nullptr;
            const bool answered = ::IO::PackIndexFind(&_packfileIndex, key._data, &node);
            if (!answered || OPENBA_PATHINDEX_CHECK) {
                RtIO::PackfileIndexNode* const indexed = node;
                node = RtIO::IndexFind(_packfileIndex.root, key._data);
                ::IO::PackIndexCheck(answered, key._data, indexed, node);
            }
            if (node != nullptr) {
                // The incumbent's OWN +0xAC decides, not the newcomer's: retail
                // indexes packfiles[] with the FOUND NODE's packIndex
                // (`MOV ECX,[RBX+0x8]`) and tests that object's byte.
                PackageFile* incumbent = _packfiles[node->element.value.packIndex];
                if (incumbent->_overridable != 0) {
                    // Retail writes the pair as ONE qword store at [RBX+8].
                    node->element.value = RtIO::PackfileIndexValue{
                        static_cast<uint32_t>(pi), static_cast<uint32_t>(fi) };
                    ++overridden;
                } else {
                    ++kept;
                }
            } else {
                // FUN_180025a80(&tree, &out, &key, &value) — PORTED 2026-09-08
                // (src/engine/collection/avl_trees.cpp group D), called by name.
                // RCX is `this+0x40`, the {root,count} tree object; RDX is a
                // scratch out-slot retail never reads afterwards.
                //
                // ENDGAME B.3.2 step 2 — DELIBERATELY NOT CONVERTED to
                // `Collection::Set<PackfileIndexPair>::Insert(const T&)`.
                // Retail's `FilenameSetInsert` (avl_trees.cpp) copy-constructs
                // the key ONCE, straight into a stack temporary alongside the
                // raw value qword, hands the temporary's ADDRESS to the
                // descent, and releases that ONE temporary afterwards — one
                // Filename copy ctor, one release. `Set<T>::Insert(const T&)`
                // would need a `PackfileIndexPair` ARGUMENT built by the
                // caller (a second temporary, its own Filename copy ctor) and
                // then copy-constructs AGAIN into the node from *that*
                // argument — two copies and two releases where retail has one,
                // an extra AddRef/Release pair on the Filename refcount the
                // resave gate can observe (COLLECTION_INVENTORY.md hazard 2 /
                // B32_RULES). What the class would need to express this site
                // without the extra round trip is an insert overload that
                // builds the element in place from a borrowed key/value pair
                // (e.g. `Insert(const Key&, const Value&)` constructing
                // directly into the temp the descent reads, the way
                // `FilenameSetInsert` does) — not present on `Collection::Set`
                // today, and not added here (no new members on Collection.h
                // per B32_RULES). B3R RT-IO: calls rtavl::FilenameSetInsert by name.
                const RtIO::PackfileIndexValue value{ static_cast<uint32_t>(pi),
                                                      static_cast<uint32_t>(fi) };
                RtIO::PackfileIndexNode* out = nullptr;
                rtavl::FilenameSetInsert(&_packfileIndex, &out, &key, &value);
                ::IO::PackIndexInsert(&_packfileIndex, out);   // NOT RETAIL (block PATHINDEX)
                ++inserted;
            }

            // ~key is retail's release at the bottom of the iteration.
        }
    }

    // EXT SEAM (NOT RETAIL, PKGCACHE.md): the tree the loop built, for the derived cache.
    if (!restored && _extIndexBuilt != nullptr)
        _extIndexBuilt(*this);

    // ---- teardown of the temporary Filename array -------------------------
    // Retail releases each element with the inlined String idiom and then frees
    // the block, and it does BOTH unconditionally of the count being zero (the
    // block free is guarded only on the pointer). Note it uses the same
    // lock/Free/unlock as everywhere else; RtMemFree is that sequence with the
    // null test inside.
    const int32_t foundCount = found.size;
    for (int64_t i = 0; i < static_cast<int64_t>(found.size); ++i)
        found.data[i].~Filename();
    RtIO::RtMemFree(found.data);
    found.data  = nullptr;
    found.size = 0;

    // EXT SEAM (NOT RETAIL, docs/extensions/handins/LARGEPKM.md): fix.large_packages builds the
    // 64-bit entry offsets of any mounted package past 2 GiB, now that the pack indices are final.
    if (_extPackagesMounted != nullptr) _extPackagesMounted(*this);
}

// ============================================================================
// ENDGAME B.3.4a closing: the two IO::PackageFile bodies that lived in
// FileSystem's files. Both move VERBATIM -- same control flow, same callees,
// same order. Keyword/callee tally, door body vs member body:
//   GetFileInfo: 1 copy construction of the +0xc0 FileInfo, 0 branches,
//                0 other callees, 0 bounds checks -- before and after.
//   GetFileName: 1 sign-extended index (`static_cast<int64_t>`), 1 stride-16
//                scale off `_entries` (+0xb0), 1 System::Filename copy ctor,
//                0 branches, 0 bounds checks -- before and after.
// Every POD is spelled `RtIO::` and every real class `System::`
// (B34_RULES section 7.2).
// ============================================================================

RtIO::FileInfo PackageFile::GetFileInfo() const {
    // 18002541e `LEA RDX,[RCX+0xc0]`: the EMBEDDED FileInfo, not a pointer
    // field. 180025428 is the FileInfo copy ctor, 180025436 the return.
    return _file;
}

System::Filename PackageFile::GetFileName(int index) const {
    // 180025475 `MOVSXD RDX,R8D ; SHL RDX,0x4 ; ADD RDX,[RCX+0xb0]`. The MOVSXD
    // is a SIGN extension, which is what the int64_t cast spells -- Ghidra's C
    // prints it as a zero extension and that cost Wave 6 three defects. It only
    // matters for a negative index, which retail never passes; it is written as
    // int64 anyway because that is what the instruction says.
    // The entry's `name` (+0x00) is a Filename; `Filename(const Filename&)`
    // is the copy ctor 0x1800067d0, into the hidden return slot.
    return _entries._data[static_cast<int64_t>(index)].name;
}

}  // namespace IO

