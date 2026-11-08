// ============================================================================
// src/engine/io/modification_refresh.cpp — WAVE 9 LANE MOD (second pass)
// ?RefreshList@Modification@IO@@QEAAXXZ, 0x1800af5f0 — the live cutover wall
// ============================================================================
//
// WHY THIS FILE EXISTS
// --------------------
// Attempt 19 of the DLL-less boot did not merely *store* a null for this
// symbol, it CALLED one: `[RtResolve] MISS "?RefreshList@Modification@IO@@
// QEAAXXZ" -> NULL`, then a call through RAX=0 with RCX=RDX=the Modification
// object and a return address inside `Engine::Startup+0x338`. Of the 27
// symbols still unsatisfied on that boot path, this is the one the boot
// actually reached; everything else was resolved-and-stored.
//
// The first lane-MOD pass (docs/history/runtime_port/WAVE9_LANE_MOD.md §6) HELD THIS
// BACK, and the holdback was correct when written: RefreshList stack-builds an
// `IO::PackageFile` and calls `ReadPackfile` on it, and on 2026-09-07
// `??0PackageFile@IO@@QEAA@XZ` resolved to NULL — it was attempt 14's wall.
// Porting a body that cannot be exercised produces a `calls=0` green line over
// unverified code, which is the failure `reference_uncalled_port_is_not_
// verified.md` exists to prevent.
//
// THAT BLOCKER IS GONE, AND IT WAS RE-DERIVED HERE RATHER THAN INHERITED
// (`reference_holdback_blockers_are_stale_check_them_first.md`). Lane PKG2
// ported the constructor and the leaf accessors (src/engine/io/
// package_file.cpp:838 `RegisterRuntimeExport("??0PackageFile@IO@@QEAA@XZ")`
// plus `RegisterRuntimeAddr(0x1800b4440)`), and lane PKG3 ported ReadPackfile
// itself (src/engine/io/package_file_read.cpp:999-1001). Both doors, both
// rows. So RefreshList's PackageFile dependency is satisfied and this body can
// be both run and diffed.
//
// ---------------------------------------------------------------------------
// WHAT THIS FILE PORTS — eight rows
// ---------------------------------------------------------------------------
//
//   | retail      | symbol / role                              | instrs |
//   |-------------|--------------------------------------------|--------|
//   | 0x1800af5f0 | ?RefreshList@Modification@IO@@QEAAXXZ      |   934  |
//   | 0x180028610 | Array<Mod>::SetSize(int, const Mod&)       |   101  |
//   | 0x1800274c0 | Set<Filename> insert fix-up (AVL retrace)  |    71  |
//   | 0x180027f50 | Set<Filename> rotate-right                 |    43  |
//   | 0x180027e20 | Set<Filename> rotate-left                  |    43  |
//   | 0x180025530 | ?GetIconData@PackageFile@IO@@              |    20  |
//   | 0x180025580 | ?GetPreviewData@PackageFile@IO@@           |    20  |
//   | 0x18007f034 | the registry-string integer parse helper   |     3  |
//
// RefreshList is 934 instructions, not the "~450" the first pass's holdback
// note estimated from a decompile's line count. The difference is what MSVC
// inlined into this one body: the whole `Set<Filename>` insert, the same tree's
// LOOKUP and in-order walk in the override pass, three `Collection::Array`
// grow-and-copy loops, the `PackageFile` constructor, and eleven copies of the
// refcounted-String release idiom. Everything that reads like a helper call in
// retail's C is open-coded in the instruction stream.
//
// ---------------------------------------------------------------------------
// WHAT THIS FILE DECLINES, WITH THE MEASURED REASON  (the honest half)
// ---------------------------------------------------------------------------
// RefreshList's call closure is 22 distinct addresses. 14 were already
// registered before this lane started; this file closes 5 of the remaining 8
// (0x180028610, 0x1800274c0, 0x180025530, 0x180025580, 0x18007f034) plus the
// two rotations the fix-up needs and RefreshList itself. THREE REMAIN, each
// declined for a reason measured today rather than inherited:
//
//   0x1800b4540  ??1PackageFile@IO@@UEAA@XZ (ord 639) — 87 instructions. NOT
//     THIS LANE'S ROW: src/engine/io/package_file.cpp:925 already names it as
//     lane PKG2's next wall, "(5 unported callees)". Writing it here would
//     duplicate a scoped row, and a duplicate `RegisterRuntimeAddr` is
//     last-binding-wins — it silently makes one of the two bodies dead code
//     (`reference_grep_for_the_port_before_writing_it.md`).
//
//   0x1800accc0  ?CloseFile@FileSystem@IO@@ (ord 1726) — 56 instructions, and
//     lane PKG2's own document claims it (`WAVE9_LANE_PKG2.md:944`, "the
//     teardown half"). It also OPENS a door rather than closing one: it calls
//     0x1800ae020, which nothing in the tree registers. A lane that closes one
//     door and opens one has not moved the count.
//
//   0x180028760  the Array<Mod> introsort — NO LONGER DECLINED. BATCH 8b
//     ported it and its small-range pass (0x180028fc0) in
//     src/engine/io/modification_sort.cpp; the third door it named,
//     0x1800c98a0 (`String::StrCompare`), was already ported in
//     src/engine/system/string.cpp. The objection recorded here — that the
//     population ties and so the SWAP SEQUENCE, not sortedness, decides which
//     record wins — was right, and is the reason that port TRANSCRIBES the
//     shipped MSVC sort skeleton (../collection/introsort_msvc.h) rather than
//     substituting one. What was wrong was the conclusion that a
//     replayed-permutation probe had to come first: a transcription of the
//     bytes needs no corpus to justify it.
//
// So with the DLL PRESENT this body is complete and diffable; at cutover it
// moves the wall from `RefreshList` to those three. The set-diff is in
// docs/history/runtime_port/WAVE9_LANE_MOD2.md and is honest about that.
//
// ---------------------------------------------------------------------------
// THE ORACLE — and why RefreshList is replayable when it does not look it
// ---------------------------------------------------------------------------
// RefreshList destroys and rebuilds `this->mods` from scratch: it frees every
// record, re-scans the pack directory, re-reads every .pkm header and re-reads
// the registry. Nothing about it can be restored-and-replayed byte-wise, and a
// byte compare of the result is meaningless because the two runs allocate
// different heap blocks.
//
// It IS replayable in the sense that matters, for a reason visible in its first
// instruction: `CALL 0x180010b80` destroys the existing array UNCONDITIONALLY,
// before anything else. The function is therefore idempotent over the pair
// (pack directory, registry) — which is exactly what the game relies on when
// the mods dialog's Ok calls it a second time. So:
//
//     run OURS  -> photograph the resulting list BY VALUE
//     run THEIRS on the same object (it re-destroys and rebuilds)
//     photograph again -> compare the two photographs
//
// Retail's run is the one that LANDS, the same property SaveList's oracle has:
// a diffing run's live behaviour is retail's behaviour.
//
// The photograph is BY VALUE, never by pointer, because every String and every
// Set node in the second run is a different heap block:
//
//     count, and per mod: strcmp(name), strcmp(author), strcmp(description),
//     strcmp(file), enabled, order, version, userVersion, iconWidth,
//     iconHeight, crc, icon.size, preview.size, files.size, set.count,
//     the SET'S IN-ORDER KEY SEQUENCE (strcmp, node by node), and
//     the FILES ARRAY's contents (strcmp, element by element).
//
// The in-order key sequence is the term that makes this non-tautological for
// the part of the body hardest to get right. The set is an AVL tree built by an
// inlined insert plus a retrace (0x1800274c0) plus two rotations, and a
// rebalance bug does not change the SET of keys — it changes the tree.
// Comparing the in-order walk catches a tree that lost, duplicated or
// misordered a node; comparing `set.count` alongside catches a count that
// drifted from the tree. WHAT IT CANNOT CATCH is a wrong rotation that still
// leaves a valid BST over the same keys with the same count — a differently
// SHAPED but equally correct tree. That is stated rather than papered over.
//
// EFFECTIVENESS CONTROL. `the diff oracle` records how many calls actually built
// something (count > 0). A run whose pack directory holds no .pkm produces
// `divergences=0` over an empty list — a green line meaning "never ran", which
// is precisely `reference_uniform_population_is_not_a_verification.md`. The
// measurement section of WAVE9_LANE_MOD2.md quotes the work count, and a zero
// there invalidates the run rather than being explained away.
//
// NEGATIVE CONTROL. `the old DLL-era switch=<n>` perturbs OUR photograph after
// it is taken and before the compare — 1 flips mods[0].enabled, 2 flips a
// bit in the LAST KEY of the first set's in-order walk, 3 bumps
// mods[0].order, 4 drops a key from that walk. Crucially 2 is what reaches
// `set.inorder`, the term the AVL port rests on: the first draft of this
// probe had only the DROP case, which fires on the length check before the
// key walk ever runs, so the tree-shape term had no control at all. Each must
// make the shadow FAIL and name the term it broke. A control that cannot fire
// is worse than none (`reference_probe_needs_an_effectiveness_control.md`).
//
// ---------------------------------------------------------------------------
// THE DELIBERATE DEVIATIONS FROM THE INSTRUCTION STREAM
// ---------------------------------------------------------------------------
// (1) Retail INLINES the `PackageFile` constructor into RefreshList
// (0x1800af830..0x1800af8e6). This file CALLS the exported constructor at
// 0x1800b4440, which lane PKG2 ported. That is safe because the two instruction
// streams are THE SAME STORES IN THE SAME ORDER — checked store by store. Both
// write the two throw-away base vtables (0x180154788 then 0x1801547a0), zero
// +0x08/+0x10/+0x18, store 0x180155700 at +0x00, zero +0x20..+0x68, put the
// qword 0x2710 at +0x70 (priority 10000 AND platform 0), zero +0x80/+0x88/
// +0x98/+0xa0, dword-zero +0xa8, byte-one +0xac, zero +0xb0/+0xb8, dword -1 at
// +0xc0, zero +0xc8, dword-zero +0xd0, zero +0xe0/+0xe8, and qword -1 at +0xd8.
// NEITHER writes +0x78/+0x7c (the icon dimensions, which package_file.cpp
// already documents as left to ReadPackfile) NOR +0x90. Writing the store list
// out a second time would be a second place for it to drift.
//
// (2) The allocator, exactly as modification.cpp's banner sets out: retail
// inlines `MOV RBX,[0x1801fc718]; ADD RBX,0x640; EnterCriticalSection; …;
// LeaveCriticalSection` at every allocation and free here, and the composed
// statics 0x1800c7a50 / 0x1800c7ad0 are byte-for-byte that sequence. Same lock,
// same singleton, same order; only the inlining differs.
//
//
// NO NEW WIN32 API AND NO NEW IMPORT LIBRARY.
// ============================================================================

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>          // placement new -- see the AVL node key ctor below

#include "io/Modification.h"
#include "io/Persistence.h"   // ENDGAME B.3.9 IO: the class static
#include "io/FileSystem.h"    // ENDGAME B.3.9 IO: the class static
#include "io/PackageFile.h"   // B.3.4 PACKAGE+FS: the real class
#include "system/Filename.h"
#include "collection/Collection.h"   // ENDGAME B.3.2 closing pass
#include "system/MemoryAllocator.h"
#include "rt_libm.h"        // rtcrt::StrICmp
#include "system/Configuration.h"   // io/Persistence.h: RtIo::SettingsSet
#include "load_time.h"   // [LoadTime] stamps (NOT RETAIL, print-only; block LOADSTAMP)

namespace RtIO {

// ENDGAME B.3.1 step 2: the two Filename entry points this file used to
// forward-declare here (CtorCopy 0x1800067d0, AdjustDelimiters 0x1800c6970)
// are gone from this translation unit -- every use was one of Filename.h's
// documented idioms and is now spelled as the class itself (see the sites
// below). `pattern`, `packDir`, the loop's Filename temporaries, `found`'s
// elements and (B.4 acceptance MOD) `Mod::file` are `System::Filename`.

// ---------------------------------------------------------------------------
// Borrowed from modification.cpp (same lane, same kill switch). Declared rather
// than re-implemented: two copies of a refcount idiom is how one of them
// drifts — the near-miss modification.cpp §4 records about RtString_AdoptInline.
// ---------------------------------------------------------------------------
// (0x180011110 / 0x180011220, the two Array operator= bodies RefreshList
// calls, are declared by Modification.h; Mod's copy ctor 0x1800101e0 and its
// dtor 0x180011460 are Mod's own members there.)

// The String header's refcount word (String.h's accessor).
static inline int32_t& RefCount(char* s) {
    return RtSystem::StringRefCount(s);
}

// The release retail inlines eleven times in RefreshList, e.g.
// 0x1800af6bb..0x1800af6eb:
//     if (p) { if (--p[-1 dword] == 0) { slot = p - 8; MemFree(slot); }
//              else                    { slot = 0; } }
// The ELSE branch's store is not a tidy-up — it is what the field is left
// holding when the block survives, and it is why a caller that keeps a
// reference must take that reference BEFORE the release (see `nameRef`).
static void ReleaseInto(char** slot) {
    char* p = *slot;
    if (p == nullptr) return;
    if (--RefCount(p) == 0) {
        *slot = p - 8;
        ::System::MemoryAllocator::MemFree(*slot);          // the null test lives inside 0x1800c7ad0
    } else {
        *slot = nullptr;
    }
}

// ===========================================================================
// PART 1 — the Set<Filename> AVL internals: 0x180027f50 rotate-right,
// 0x180027e20 rotate-left and 0x1800274c0 the retrace live in
// src/engine/io/modification_avl8.cpp (their own translation unit, so the host
// unit test can link them; moved verbatim 2026-10-17).
// ===========================================================================

// ---------------------------------------------------------------------------
// 0x18007eefc — `__ascii_stricmp`, the comparator of THIS tree. Hoisted to file
// scope (ENDGAME B.3.2 closing pass) because the `Compare` policy below needs
// it as well as `RefreshList`'s two walks; the cache is the same "hit once,
// retry a miss" it had inside the function.
//
// It is `_stricmp`, NOT the signed `strcmp` the pathid tree uses
// (set_avl_pathid.cpp's banner makes that the one field that separates the two
// families). A differently-cased path is the SAME key here.
// ---------------------------------------------------------------------------
int ModStrICmp(const char* a, const char* b) {
    return rtcrt::StrICmp(a, b);   // 0x18007eefc (B3R RT-IO: by name)
}

}  // namespace RtIO  (reopened below)

// ===========================================================================
// PART 1b — the width-8 AVL core, as `Collection::AvlOps<Node, 0x8>`
// (ENDGAME B.3.2 closing pass)
// ===========================================================================
//
// The three bodies above ARE the ported core for element width 8 / node 0x28,
// and they were the last width `Collection::Set<T>` could not reach: the
// template's `AvlOps` carried 0x10, 0x18 and 0x30 and fell through to
// `AvlGeneric` for 8. These three forwarders are what set_core.cpp's are for
// the other three widths, and they live HERE, next to the bodies, rather than
// in set_core.cpp: that file's rule is that it is the only place that knows the
// three node TYPES, and this node type belongs to IO::Modification.
//
// NOTHING IS RE-DERIVED. `AvlRotateLeft`/`AvlRotateRight`/the retrace are
// unchanged above, still registered at 0x180027e20 / 0x180027f50 / 0x1800274c0,
// and still reached by address by anything that wants them that way.
//
// THE RETRACE TAKES `&set.root`. Retail's `CMP RDX,[RCX]` reads RCX as a
// pointer to the root SLOT, and the template hands its `Set*`; `_root` is at
// +0x00, so the two are the same address. The other three widths' cores take
// the set and read `set->root` themselves — a spelling difference, not a
// behavioural one, and it is why these forwarders cast rather than offset.
namespace Collection {
namespace detail {

// PortedRetrace8 (the 0x1800274c0 forwarder) was deleted in B.4 wave X: the
// template calls RtColl_SetInsertFixup_1800274c0 above by name.
// PortedRotateLeft8 / PortedRotateRight8 (0x180027e20 / 0x180027f50 forwarders)
// were deleted in B.4 P2 R-IO: no caller was left; the two rotations are
// reached only from the retrace above.

// (B.4 acceptance MOD: `KeyTraits<System::Filename>` -- the Filename null
// state the descent tests -- moved to Modification.h, beside the set type.)

}  // namespace detail
}  // namespace Collection

namespace RtIO {

// (B.4 acceptance MOD: `FilenameICompare` and `ModFilenameSet` -- the
// `Collection::Set<System::Filename, FilenameICompare>` that IS Mod+0x60 -- are
// declared in Modification.h, with the node-layout asserts.)

// ===========================================================================
// PART 2 — 0x180028610, Collection::Array<Mod>::SetSize(int n, const Mod& fill)
// ===========================================================================
//
// The growth rule is the one modification.cpp's ArrayAssignGrow documents
// (capacity tripled only on the exact push-one-element case), with stride 0x80
// and align 8. What is specific to this body is WHERE each new element's
// contents come from:
//
//   start = (oldData == newData) ? oldSize : 0        // CMP RBP,[R15] ; CMOVZ
//   for (i = start; i < n; ++i)
//       Mod::Mod(&new[i], i < oldSize ? &old[i] : &fill);
//
// i.e. on a REALLOCATION every surviving element is copy-constructed out of the
// old buffer and the tail is copy-constructed from `fill`; in place, only the
// tail is touched. Then the old elements are destroyed — from the old buffer if
// it was replaced, or from the truncated tail if it was not.
//
// `RefreshList` uses it exactly once, as `SetSize(size + 1, stackMod)`, which is
// this container's only append idiom.
//
// The `ADD RCX,RDI ; JZ` at 0x180028695 tests the ELEMENT address, not the
// buffer, and is retail's only protection against a failed MemAlloc.
// Transcribed, not hoisted — modification.cpp §4.
//
// B.4 acceptance MOD: that is `rtcoll::SetSizeCore` driven by `kSpec_Mod`
// (modification.cpp) -- the same grow rule, the same `realloced ? 0 : oldSize`
// start, the same computed-address test, assign = Mod's copy ctor from the old
// element or the fill, then ~Mod over the old buffer and its free, or over the
// truncated tail -- so the append below is `ModList::SetSize(n, fill)` and the
// hand-transcribed body that stood here is gone.

// ===========================================================================
// PART 3 — 0x180025530 GetIconData / 0x180025580 GetPreviewData
// ===========================================================================
// Both are the same twenty instructions: zero the caller's 16-byte return slot,
// then, unless the slot IS the member being returned, run the member's
// Array::operator= into it. The zeroing is what makes the assign safe — the
// assign begins by freeing `dst->data`, and the slot is raw stack.
//
// The self-compare (`LEA RDX,[RCX+0x80]` / `CMP RBX,RDX`) can only be true if a
// caller passes the member's own address as the return slot. Transcribed, not
// dropped: it IS the guard, and neither Array::operator= carries one of its own
// (only the Filename instantiation does — modification.cpp).
//
// The element types come from the export names, not from the call site:
// `?$Array@I` (unsigned int) for the icon, `?$Array@E` (unsigned char) for the
// preview — which is why the two call different assigns.
// ENDGAME B.3.4 PACKAGE+FS commit 3: both bodies are now members of the real
// `IO::PackageFile`, at the bottom of this file. Both doors keep their names
// and both registrations and delegate. Neither is a virtual slot -- they are
// `QEBA` -- so the qualification below is for the reader, not for dispatch.
// (B.4 R-IO: the two doors `RtIo_PackageFile_GetIconData` /
// `RtIo_PackageFile_GetPreviewData` had one caller, RefreshList below, which
// calls the members directly now; the doors are deleted.)

// ===========================================================================
// PART 4 — 0x18007f034, the registry-string integer parse helper
// ===========================================================================
//     XOR EDX,EDX ; LEA R8D,[RDX+0xa] ; JMP 0x180083e18
// a bare tail-jump to CRT `strtol(s, NULL, 10)`. That identification is not new
// here: WAVE4_LANE_C.md:167 and src/engine/io/persistence.cpp:127 both record
// it, and keymap_load.cpp:270 and video/device.cpp:773 already call std::strtol
// for the same address. This row exists to give the ADDRESS a door — the three
// existing sites call std::strtol inline, so nothing had registered
// 0x18007f034 and a by-address caller (this one) would find null at cutover.
extern "C" int RtCrt_ParseInt10_18007f034(const char* s) {
    return static_cast<int>(std::strtol(s, nullptr, 10));
}

// ===========================================================================
// PART 5 — 0x1800af5f0, ?RefreshList@Modification@IO@@QEAAXXZ
// ===========================================================================

// B3R RT-IO: the fourteen callees below were function-pointer statics filled
// through address doors; every one is called by name now (FileSystem /
// PackageFile / String / Filename members, RtSystem_ConfigMap_Find,
// RtColl_ArraySetSizeFill_180026880, RtIo_Modification_SortMods).

// The `Mod` int columns, named from the PackageFile fields RefreshList copies
// them out of (0x1800afaba..0x1800afade and 0x1800afc8e). Modification.h called
// them i18/i1c/i24/i28/i2c (by offset) because the lifecycle triple that pinned the layout
// only ever COPIES them; this is the body that gives them meaning, and the
// header now carries these names:
//
//     Mod::version     <- PackageFile +0x68   ?GetVersion@      (0x1800253d0)
//     Mod::userVersion <- PackageFile +0x6c   ?GetUserVersion@  (0x1800253e0)
//     Mod::iconWidth   <- PackageFile +0x78   ?GetIconWidth@    (0x180025510)
//     Mod::iconHeight  <- PackageFile +0x7c   ?GetIconHeight@   (0x180025520)
//     Mod::crc         <- PackageFile +0xa8   ?GetCRC@          (0x180025500)
//     Mod::order starts at 0 and is overwritten from "ModIndex_<stem>".

}  // namespace RtIO

// ============================================================================
// ENDGAME B.3.4 PACKAGE+FS — `IO::Modification::RefreshList`'s member body
// ============================================================================
namespace IO {

void Modification::RefreshList() {
    // A member of `IO::X` must be DEFINED in a namespace ENCLOSING the class
    // (B34_BRIEFS section A.2), so this body cannot live inside `namespace
    // RtIO` where its two dozen file-local helpers, typedefs and
    // using-declarations do. ONE function-scope using-directive brings them all
    // in unchanged, which is what keeps this 380-line transcription a MOVE
    // rather than a rewrite: every line below is the line that was in the door,
    // byte for byte, except the five field reads that became members.
    //
    // It is safe against B34_RULES section 7.2's grep hazard rather than
    // exposed to it: the only name `IO` and `RtIO` share is `Modification`, and
    // inside a member the injected-class-name wins outright, so a collision
    // would be a compile error and never a silent rebind.
    using namespace RtIO;
    using PathNode = ModFilenameSet::Node;   // the path set's node (Mod+0x60)

    // `this + 8` is the Array<Mod> header {mods, count, capacity}. Retail holds
    // it in RDI for the whole body and spills it at [RBP+0x208].
    ModList* const arr = &_mods;

    // 0x1800af62c — UNCONDITIONAL. This is what makes the function idempotent
    // and therefore diffable; see the banner.
    arr->DestroyAndFree();                                   // 0x180010b80

    // 0x1800af631: `CMP byte ptr [RBX+1],0 ; JNZ main`. With saving disabled the
    // list is restored from the backup EnableSavingSettings(false) made and the
    // function returns — no disk scan, no registry read.
    if (_savingSettings == 0) {
        *arr = _savedMods;                                   // 0x180010110
        return;
    }
    LoadTime::Stamp("mods.refresh");   // NOT RETAIL: print-only (the disk scan of the .pkm files)


    // The two singletons retail loads at 0x1800af648 / 0x1800af64f. Read through
    // the exported data slots rather than the raw addresses so a standalone run
    // sees the objects Engine::Startup actually constructed.
    // (ENDGAME B.3.9 IO (D7): the class statics; the address of a static is
    // never null, so the two slot tests the doors carried are gone.)
    ::IO::FileSystem* const  fs   = ::IO::FileSystem::_singleton;
    ::IO::Persistence* const pers = ::IO::Persistence::_singleton;

    // 0x1800af65f..0x1800af669 — three zeroed slots. The pack-directory Filename
    // at [RBP+0x200] is NOT among them: GetPackDirectory constructs it.
    RtIO::FilenameArray found = { nullptr, 0, 0 };
    {
        // `pattern` is a String::Set call (0x1800c95d0) landed on a Filename-
        // typed slot -- same eight bytes, and FindFiles wants both as
        // Filenames regardless. Scoped in a block so both release at exactly
        // retail's point (0x1800af6b4 / 0x1800af6ef), not at the end of this
        // (934-instruction) function.
        System::Filename pattern;
        pattern.AsString().Set("*.pkm");                         // literal 0x1801557f0

        System::Filename packDir(fs->GetPackDirectory());        // 0x180007c10

        static_cast<const ::IO::FileSystem*>(fs)->FindFiles(       // 0x1800ad7a0
            packDir.AsString(), pattern.AsString(), false, &found);
    }  // ~packDir then ~pattern (reverse declaration order) release here --
       // retail released packDir then pattern too (0x1800af6b4, 0x1800af6ef).


    // 0x1800aff3d .. 0x1800b0113: the registry part of loop 1 and the append, verbatim (a lambda only
    // so that ext.menu_entry's cached record goes through the very same lines; the read path calls
    // it where the lines stood, before ~PackageFile).
    auto applySettings = [&](Mod* const m) {
        // 0x1800aff3d — the registry key stem: the mod's path with its directory
        // stripped, kept alive across the temporary's release. `nameRef`
        // outlives the scope deliberately, same idiom as SaveList's `ref`
        // (modification.cpp): take the extra reference, then let the block's
        // close run retail's release of the temporary.
        char* nameRef;
        {
            System::Filename pathTmp(m->file.GetPathFilename(true));   // 0x1800c6740
            nameRef = pathTmp._data;
            if (nameRef != nullptr) ++RefCount(nameRef);         // take the ref FIRST
        }  // ~pathTmp releases here, retail's 0x1800aff.. release ("then drop the temp")

        // 0x1800affc9 — "ModEnabled_<stem>". The comparison against "true"
        // (0x180155870) is a byte-for-byte inlined strcmp in retail
        // (0x1800b0013..0x1800b0048) including its `value[0] != 't'` fast path;
        // `strcmp(v, "true") == 0` is the same predicate on every input. A null
        // value takes retail's `EDX = -1` arm and a missing entry leaves the
        // flag at the 0 the record was born with.
        {
            // String::Format (0x1800c9730) lands on a Filename-typed local, as
            // `pattern` above does; `keyStr` is the class for its own RAII
            // release only. Constructed over the default ctor's null, as the
            // door that stood here did.
            System::Filename keyStr;
            ::new (&keyStr.AsString()) System::String(
                System::String::Format("%s%s", "ModEnabled_", nameRef));
            // The settings map is `Collection::Set<Pair<String,String>>`
            // (RtIo::SettingsSet); 0x18002ace0 is its `Find` (B7 LX).
            const RtIo::SettingsNode* node =
                pers->_settingsMap.Find(keyStr.AsString()._data);   // 0x18002ace0
            if (node != nullptr) {
                const char* v = node->element.value._data;       // +0x08
                const int cmp = (v == nullptr) ? -1 : std::strcmp(v, "true");
                m->enabled = static_cast<uint8_t>(cmp == 0);
            }
        }  // ~keyStr releases here
        // 0x1800b0085 — "ModIndex_<stem>", parsed base 10. A missing entry
        // leaves `order` at 0.
        {
            System::Filename keyStr;
            ::new (&keyStr.AsString()) System::String(
                System::String::Format("%s%s", "ModIndex_", nameRef));
            const RtIo::SettingsNode* node =
                pers->_settingsMap.Find(keyStr.AsString()._data);   // 0x18002ace0
            if (node != nullptr) {
                const char* v = node->element.value._data;       // +0x08
                m->order = RtCrt_ParseInt10_18007f034(v);        // 0x18007f034
            }
        }  // ~keyStr releases here

        // 0x1800b00f9 — append. The only writer of the array in the whole body.
        arr->SetSize(arr->_size + 1, *m);                        // 0x180028610

        ReleaseInto(&nameRef);                                   // 0x1800b0113
    };

    // -----------------------------------------------------------------------
    // LOOP 1 — one iteration per .pkm found (0x1800af790 .. 0x1800b019f)
    // -----------------------------------------------------------------------
    System::Filename* const foundData = found.data;
    for (int32_t i = 0; i < found.size; ++i) {
        // The stack Mod, zeroed member by member at 0x1800af790..0x1800af7d7:
        // Mod's default ctor, which leaves +0x24/+0x28/+0x2c (icon width, icon
        // height, crc) and the padding at +0x51..+0x57 unwritten, as retail
        // does (every path that APPENDS the record writes all three first and
        // the two early-out paths destroy it without reading them). Its
        // destructor (0x180011460) runs at the end of each iteration, the
        // `continue`s included -- retail's three ~Mod sites.
        Mod mod;
        Mod* const m = &mod;

        // 0x1800af7df — m.file = found[i], by reference, then normalised. A
        // single AdjustDelimiters after a shared-assign on an object that
        // starts null is `Filename::operator=(const Filename&)`, 0x180006770
        // (Filename.h's DELIMITERS note; `m->file` was just zeroed above, and
        // the exemplar treats a freshly-zeroed member the same way --
        // filesystem_setupdirs.cpp's `sourceDirectory`/`dataDirectory`).
        m->file = foundData[i];

        // EXT SEAM (NOT RETAIL, docs/extensions/handins/MENUENTRY.md): ext.menu_entry fills the record
        // of a .pkm unchanged since the last scan (name, size, write time) without reading it, or
        // reports it as one retail discards; the settings part runs as retail's.
        if (_extModCached != nullptr) {
            const int cached = _extModCached(foundData[i], *m);
            if (cached == kModSkipped)
                continue;
            if (cached == kModFilled) {
                applySettings(m);
                continue;
            }
        }

        // 0x1800af830..0x1800af8e6 — the inlined PackageFile constructor:
        // ENDGAME B.3.9 IO, the class's real constructor on the same stack
        // storage (the store list the banner compares, in the same order).
        alignas(16) uint8_t pkgStorage[0xF0];
        IO::PackageFile* const pkgObj = ::new (static_cast<void*>(pkgStorage)) IO::PackageFile();

        const bool read = pkgObj->IO::PackageFile::ReadPackfile(foundData[i]);   // 0x1800b46b0
        // 0x1800af923 — `platform` (+0x74) non-zero means the package is not for
        // this build, and retail CLOSES THE FILE before discarding it. The
        // ReadPackfile-failed path does NOT close: there the file never opened.
        if (!read) {
            pkgObj->IO::PackageFile::~PackageFile();           // 0x1800b4540
            if (_extModStored != nullptr)   // EXT SEAM (MENUENTRY.md): a discarded file
                _extModStored(foundData[i], nullptr);
            continue;                                            // ~mod: 0x180011460
        }
        if (pkgObj->_platform != 0) {                            // +0x74
            fs->CloseFile(pkgObj->_file);                        // 0x1800accc0, +0xC0
            pkgObj->IO::PackageFile::~PackageFile();
            if (_extModStored != nullptr)   // EXT SEAM (MENUENTRY.md): a discarded file
                _extModStored(foundData[i], nullptr);
            continue;                                            // ~mod
        }

        // 0x1800af941 — the three String accessors. Each fills a temporary the
        // caller must release; the member takes a REFERENCE to the same block,
        // which is why the temporary's release comes after the member assign.
        {
            // `tmp` is the class purely for its own RAII release. GetName's
            // by-value String is constructed into it, over the null the default
            // ctor left, exactly as the door that stood here did.
            System::Filename tmp;
            ::new (&tmp.AsString()) System::String(pkgObj->IO::PackageFile::GetName());       // 0x180025310
            m->name = tmp.AsString();   // String::operator= (release, adopt)
        }  // ~tmp releases here, retail's release after the assign
        {
            System::Filename tmp;
            ::new (&tmp.AsString()) System::String(pkgObj->IO::PackageFile::GetAuthor());     // 0x180025350
            m->author = tmp.AsString();   // String::operator= (release, adopt)
        }  // ~tmp releases here
        {
            System::Filename tmp;
            ::new (&tmp.AsString()) System::String(pkgObj->IO::PackageFile::GetDescription());  // 0x180025390
            m->description = tmp.AsString();   // String::operator= (release, adopt)
        }

        // 0x1800afaba — the four ints that come straight off the header.
        m->version = pkgObj->_version;       // +0x68
        m->userVersion = pkgObj->_userVersion;   // +0x6c
        m->iconWidth = pkgObj->_iconWidth;     // +0x78
        m->iconHeight = pkgObj->_iconHeight;    // +0x7c

        // 0x1800afae2 — the icon, then the preview. Each arrives in a temporary
        // Array this body hands to the member's operator= and then frees.
        // The temporary's ~Array is the inline free and the three zero stores
        // (0x1800afb71..0x1800afbb6), at the end of each block.
        {
            Collection::Array<uint32_t> tmp = pkgObj->IO::PackageFile::GetIconData();   // 0x180025530
            if (&m->ints != &tmp) RtColl_ArrayI32Assign_180011110(&m->ints, &tmp);
        }
        {
            Collection::Array<uint8_t> tmp = pkgObj->IO::PackageFile::GetPreviewData(); // 0x180025580
            if (&m->bytes != &tmp) RtColl_ArrayU8Assign_180011220(&m->bytes, &tmp);
        }

        m->crc   = static_cast<int32_t>(pkgObj->_crc);            // +0xa8 crc  0x1800afc8e
        m->order = 0;                                             //      0x1800afc98

        // -------------------------------------------------------------------
        // LOOP 1a — every path inside the package, inserted into the mod's AVL
        // set (0x1800afcb0 .. 0x1800aff1d). The insert is INLINE in retail; only
        // the retrace is a call.
        //
        // The two comparison arms are ASYMMETRIC about nulls and both
        // asymmetries are retail's: a null NODE key stops the leftward descent
        // and sends the walk right, while a null SEARCH key descends LEFT in the
        // first arm and ends the walk with NO INSERT in the second.
        // -------------------------------------------------------------------
        const int32_t entryCount = pkgObj->_entries._size;       // +0xb8
        for (int32_t e = 0; e < entryCount; ++e) {
            // GetFileName's by-value return is constructed straight into `key`
            // (the hidden return slot).
            System::Filename key(pkgObj->IO::PackageFile::GetFileName(e));   // 0x180025450

            // ENDGAME B.3.2 closing pass: the 70 lines retail inlined here are
            // `Collection::Set<System::Filename, FilenameICompare>::Insert`, arm
            // for arm — the template's `InsertCorePattern` IS this descent (its
            // banner names 0x180030380 / 0x18002a170 / 0x180025c70 / 0x1800275e0
            // as the other four copies of it), and `AvlOps<Node, 0x8>` is the
            // retrace and the two rotations THIS FILE ports at 0x1800274c0 /
            // 0x180027e20 / 0x180027f50. Nothing moved to a different body.
            //
            // Every asymmetry the transcription called out survives in the
            // template and is worth naming once more, because each is one `if`:
            //   * empty tree (0x1800afccc): the count is incremented and the
            //     root stored EVEN IF THE ALLOCATION FAILED — `InsertCorePattern`
            //     stores whatever `AllocNode` returned, null included.
            //   * a null NODE key skips the left test; a null SEARCH key
            //     descends LEFT in the first arm and reports EQUAL (no insert,
            //     no count) in the second — `KeyTraits<System::Filename>` above
            //     plus the pattern's two arms.
            //   * the retrace starts at the PARENT of the new node and takes a
            //     SLOT, re-read after every rotation.
            //   * a duplicate key returns the existing node and does not count.
            m->set.Insert(key);
            // ~key releases here, retail's 0x1800afeb5 release -- the last
            // statement of the loop body, exactly where retail's release sat.
        }

        fs->CloseFile(pkgObj->_file);                            // 0x1800aff2a, +0xC0

        // EXT SEAM (NOT RETAIL, docs/extensions/handins/MENUENTRY.md): the record as read, before the
        // settings part (enabled 0, order 0), for the next scan of an unchanged .pkm.
        if (_extModStored != nullptr)
            _extModStored(foundData[i], m);

        // 0x1800aff3d .. 0x1800b0113 -- the settings part and the append, moved verbatim into
        // `applySettings` above (ext.menu_entry serves a cached record through the same lines).
        applySettings(m);
        pkgObj->IO::PackageFile::~PackageFile();                // 0x1800b0172 ~PackageFile
        // ~mod at the end of the iteration: 0x1800b017c
    }

    // -----------------------------------------------------------------------
    // LOOP 2 — the override pass (0x1800b01a5 .. 0x1800b0332)
    //
    // For every ordered pair (i, j), i != j: walk mod i's path set IN ORDER and
    // look each key up in mod j's set. On the FIRST key they share, append mod
    // j's .pkm filename to mod i's `files` array and move on to the next j. That
    // array is therefore "the mods this one collides with", one entry per
    // colliding mod, in j order — not a list of colliding paths.
    //
    // The lookup is the same two-armed compare as the insert with the
    // allocation removed. The in-order walk is open-coded: descend to the
    // leftmost node, then take the successor repeatedly (leftmost of the right
    // subtree, else climb while we are a right child).
    // -----------------------------------------------------------------------
    LoadTime::Stamp("mods.refresh.overrides");   // NOT RETAIL: print-only (loop 2)
    // EXT SEAM (NOT RETAIL, docs/extensions/handins/MENUENTRY.md): ext.menu_entry rebuilds every
    // mod's `files` from the last scan when the same .pkm files (name, size, write time) were found
    // in the same order; the loop then runs over no mod.
    const bool overridesRestored = _extOverridesRestore != nullptr && _extOverridesRestore(*this);
    for (int32_t i = 0; !overridesRestored && i < _mods._size; ++i) {
        for (int32_t j = 0; j < _mods._size; ++j) {
            if (i == j) continue;                                // CMP R13,R15 ; JZ
            Mod* const mods = arr->_data;                        // re-read, as retail does
            PathNode* node = mods[i].set._root;
            if (node == nullptr) continue;
            while (node->left != nullptr) node = node->left;     // 0x1800b0210
            while (node != nullptr) {
                PathNode* probe = mods[j].set._root;
                bool hit = false;
                while (probe != nullptr) {
                    // 0x1800b0234 — go LEFT while probe->key is non-null and
                    // node->key sorts before it (a null node->key sorts first).
                    bool goLeft;
                    if (probe->element._data == nullptr)     goLeft = false;
                    else if (node->element._data == nullptr) goLeft = true;
                    else goLeft = (ModStrICmp(node->element._data, probe->element._data) < 0);
                    if (goLeft) { probe = probe->left; continue; }
                    // 0x1800b0258 — a null node->key here is a MATCH, and so is
                    // any probe->key that does not sort before it.
                    if (node->element._data == nullptr) { hit = true; break; }
                    if (probe->element._data == nullptr) { probe = probe->right; continue; }
                    if (ModStrICmp(probe->element._data, node->element._data) < 0) { probe = probe->right; continue; }
                    hit = true;
                    break;
                }
                if (hit) {
                    // 0x1800b02d2 — append mods[j].file to mods[i].files.
                    RtColl_ArraySetSizeFill_180026880(&mods[i].files, mods[i].files._size + 1,
                                &mods[j].file);                  // 0x180026880
                    break;                                       // next j
                }
                // in-order successor (0x1800b027f)
                if (node->right != nullptr) {
                    node = node->right;
                    while (node->left != nullptr) node = node->left;
                } else {
                    PathNode* up = node->parent;
                    if (up == nullptr)         node = node->parent;   // null
                    else if (up->left == node) node = up;
                    else {
                        while (up != nullptr && up->right == node) {
                            node = up;
                            up = up->parent;
                        }
                        node = node->parent;
                    }
                }
            }
        }
    }
    if (!overridesRestored && _extOverridesBuilt != nullptr)   // EXT SEAM (MENUENTRY.md)
        _extOverridesBuilt(*this);

    // 0x1800b0338 — the sort. PORTED (BATCH 8b,
    // src/engine/io/modification_sort.cpp), called by name. The third
    // argument is a stack slot the callee never reads.
    if (arr->_size > 1) {
        void* scratch = nullptr;
        RtIo_Modification_SortMods(arr->_data,
                                   arr->_size, &scratch);
    }

    // 0x1800b0350 — release every found Filename, then the array itself.
    for (int32_t i = 0; i < found.size; ++i) ReleaseInto(&foundData[i]._data);
    ::System::MemoryAllocator::MemFree(found.data);
    found.data = nullptr; found.size = 0; found.capacity = 0;
    LoadTime::Stamp("mods.refresh.done");   // NOT RETAIL: print-only

    // -----------------------------------------------------------------------
    // The oracle. See the banner for why running theirs SECOND is legal here.
    // -----------------------------------------------------------------------
}

}  // namespace IO

// ============================================================================
// ENDGAME B.3.4 PACKAGE+FS — `IO::PackageFile`'s two counted-array getters
// ============================================================================
// Defined outside `namespace RtIO` because a member of `IO::X` must appear in a
// namespace ENCLOSING the class (B34_BRIEFS section A.2). They stay in THIS
// file because this is where their doors and their registrations have always
// been, and because the two `Collection::Array<T>::operator=` instantiations
// they call are this file's, reached by address.
namespace IO {

// The self-compare (`LEA RDX,[RCX+0x80]` / `CMP RBX,RDX`) can only be true if a
// caller passes the member's own address as the return slot. Transcribed, not
// dropped: it IS the guard, and neither Array::operator= carries one of its own
// (only the Filename instantiation does -- modification.cpp).
//
// The element types come from the export names, not from the call site:
// `?$Array@I` (unsigned int) for the icon, `?$Array@E` (unsigned char) for the
// preview -- which is why the two call different assigns. The two offsets that
// used to be spelled here as `self + 0x80` / `self + 0x98` are now the named
// members `_iconData` / `_previewData`, each pinned by a static_assert in
// src/engine/io/PackageFile.h.
// By value (NRVO): `ret` IS the caller's slot. Retail 0x180025530: MSVC's
// unwind state [RSP+0x20] = 0 (MOV [RSP+0x20],EAX) while the slot is live,
// the zeroing (MOV [RBX],RAX / MOV [RBX+8],RAX), the self test, CALL
// 0x180011110, state 1. The landing pad GCC emits for `ret` is that unwind.
Collection::Array<uint32_t> PackageFile::GetIconData() const {
    Collection::Array<uint32_t> ret;                    // MOV [RBX],RAX / [RBX+8],RAX
    if (&ret != &_iconData)                             // LEA RDX,[RCX+0x80] ; CMP RBX,RDX
        RtIO::RtColl_ArrayI32Assign_180011110(&ret, &_iconData);  // 0x180011110
    return ret;
}

Collection::Array<uint8_t> PackageFile::GetPreviewData() const {
    Collection::Array<uint8_t> ret;
    if (&ret != &_previewData)                          // LEA RDX,[RCX+0x98] ; CMP RBX,RDX
        RtIO::RtColl_ArrayU8Assign_180011220(&ret, &_previewData);   // 0x180011220
    return ret;
}

}  // namespace IO
