// ============================================================================
// src/engine/ui/dialog_ctor_dtor.cpp — Wave 10 lane DLG2
//   `??0Dialog@UI@@QEAA@XZ`  0x180112db0  — the default UI::Dialog constructor
//   `??1Dialog@UI@@UEAA@XZ`  0x180112e40  — the virtual UI::Dialog destructor
// ============================================================================
//
// WHY THESE TWO ROWS EXIST AT ALL, AND WHY THAT IS PROGRESS
// ---------------------------------------------------------------------------
// Neither name was in the UI census before 2026-09-08. They ENTERED it as the
// direct consequence of lane UI3 closing the `UI::DialogInstance` family:
// retail INLINES both bodies into the 6-argument
// `?AddController@DialogInstance@UI@@` (its stack temporary is a `UI::Dialog`,
// proved by the `??_7Dialog@UI@@6B@` stamp at 0x180112deb), and UI3's port calls
// the out-of-line exports by name instead of re-inlining them.
//
// So the port replaced an INLINED, UNNAMED dependency with a NAMED, SCOPEABLE
// export. That is `reference_porting_can_grow_the_cutover_surface.md` working
// as intended, not a regression: the two bodies always ran on this path, and
// the census could not see them only because nothing in our tree named them.
// This file closes both.
//
// ---------------------------------------------------------------------------
// THE TWO MANGLINGS, READ OUT OF THE SHIPPED EXPORT DIRECTORY FIRST
// ---------------------------------------------------------------------------
// `tools/pe_exports.py` against C:\GOG Games\Banished\Runtime-x64.dll
// (4,926 exports), BEFORE a line of body was written — a name the DLL does not
// contain resolves to NULL in silence, the port still registers, and the census
// still counts it (reference_resolveexport_phantom_names.md):
//
//   0x180112db0  ??0Dialog@UI@@QEAA@XZ    present
//   0x180112e40  ??1Dialog@UI@@UEAA@XZ    present
//
// Both at exactly the addresses the disassembly gives. NO PHANTOM.
//
// *** THE TWO SPELLINGS DIFFER AND BOTH ARE LOAD-BEARING. *** The ctor is
// `QEAA` (public, non-virtual). The dtor is `UEAA` — the VIRTUAL spelling.
// `??1Dialog@UI@@QEAA@XZ` is NOT in the export table and would resolve NULL
// without a word of complaint (reference_phantom_dtor_mangling_qeaa_vs_ueaa.md,
// which is exactly this defect one class over).
//
// The three vtables the ctor stamps were checked the same way and are exported
// at exactly the addresses the shipped `LEA`s name:
//   ??_7Base@Object@@6B@      0x180154788
//   ??_7External@Object@@6B@  0x1801547a0
//   ??_7Dialog@UI@@6B@        0x1801562a0
// In the port the three stores are the compiler's (`UI::Dialog::Dialog()`,
// Dialog.h).
//
// ---------------------------------------------------------------------------
// `??0Dialog@UI@@QEAA@XZ` — the shipped body, 0x2b instructions
// ---------------------------------------------------------------------------
//   180112dc1  LEA RAX,[0x180154788] ; MOV [RCX],RAX      <- Base vtable
//   180112dcb  LEA RAX,[0x1801547a0] ; MOV [RCX],RAX      <- External vtable
//   180112dd5  LEA RAX,[RCX+0x8]     ; MOV [RSP+0x28],RAX <- SEH-visible temp
//   180112dde  XOR EDX,EDX
//   180112de0  MOV [RAX],RDX                              <- +0x08 THROUGH RAX
//   180112de3  MOV [RCX+0x10],RDX
//   180112de7  MOV [RCX+0x18],RDX
//   180112deb  LEA RAX,[0x1801562a0] ; MOV [RCX],RAX      <- Dialog vtable
//   180112df5  +0x20,+0x28 = 0 (qwords)
//   180112dfd  MOV dword [RCX+0x30],0xffffffff            <- -1, NOT zero
//   180112e04  MOV dword [RCX+0x34],EDX
//   180112e07  MOV qword [RCX+0x38],RDX
//   180112e0b  MOV dword [RCX+0x40],EDX                   <- DWORD; +0x44 UNTOUCHED
//   180112e0e  +0x48 … +0x80 = 0 (qwords, ten of them)
//   180112e31  MOV RAX,RCX ; RET                          <- returns `this`
//
// THREE THINGS ONLY THE DISASSEMBLY SHOWS
// (reference_ghidra_c_output_vs_disassembly.md):
//
//  (a) THE FIRST QWORD IS WRITTEN THREE TIMES. Base, then External, then Dialog
//      — the ordinary MSVC base-ctor chain, fully inlined. The first two stores
//      are dead in the shipped binary; in the port all three are the
//      compiler's base-ctor chain (`UI::Dialog::Dialog()`, Dialog.h).
//
//  (b) `+0x44` IS NEVER WRITTEN. 0x180112e0b is a DWORD store to +0x40 and the
//      next store is a QWORD at +0x48. A "tidy" port that zeroes +0x40 as a
//      qword would write four bytes retail leaves holding whatever the caller's
//      buffer held — and on the 6-arg AddController's path that is UNINITIALISED
//      STACK. The port's ctor (`UI::Dialog::Dialog()`, Dialog.h) leaves
//      +0x44..+0x47 unwritten too.
//
//  (c) `+0x30` IS -1, NOT 0. The single most likely transcription slip in a
//      body that is otherwise 16 zero stores.
//
// THE CTOR'S WRITTEN EXTENT IS +0x00..+0x87 (the last store is a QWORD at
// +0x80). `src/engine/ui/Dialog.h` asserts `sizeof(Dialog) == 0x88` from the
// vector deleting destructor's stride (`MOV EDX,0x88` at 0x180038b3f), not from
// this extent: a ctor's written extent is a LOWER BOUND on a class size.
//
// ---------------------------------------------------------------------------
// `??1Dialog@UI@@UEAA@XZ` — the shipped body
// ---------------------------------------------------------------------------
//   180112e56  LEA RAX,[0x1801562a0] ; MOV [RCX],RAX      <- re-stamp Dialog vtbl
//   180112e60  ADD RCX,0x78 ; CALL 0x180009460            <- ~Array on +0x78
//   180112e6a  LEA RCX,[RBX+0x68] ; CALL 0x180039440      <- ~Array on +0x68
//   180112e74  LEA RCX,[RBX+0x58] ; CALL 0x180039360      <- ~Array on +0x58
//   180112e7e  LEA RCX,[RBX+0x48] ; CALL 0x180039280      <- ~Array on +0x48
//   180112e88  the System::String release at +0x28 (see below)
//   180112eb2  MOV RCX,RBX ; JMP 0x1800bc950              <- TAIL-JUMP ~External
//
// THE STRING RELEASE IS RETAIL'S ASYMMETRIC ONE, kept rather than tidied by
// `~String` (String.h). `_spriteName` at +0x28 is a refcounted `char*` whose count sits at
// `data - 4`:
//   * count reaches zero -> the FIELD is first rewritten to `data - 8`
//     (`ADD qword [RBX+0x28],-0x8`) and THAT is what is handed to `MemFree`.
//     The field is LEFT holding `data - 8`; it is not nulled.
//   * count is still positive -> the field IS nulled.
// The two branches leave the field in different states and that difference is
// observable. `?CreateElement@Dialog@` and `?CreateInstance@ElementDescription@`
// reproduce the same shape (dialog.cpp).
//
// ---------------------------------------------------------------------------
// EVERY CALLEE, AND ITS DISPOSITION — the honest version
// ---------------------------------------------------------------------------
//   0x180009460  ~Array  (+0x78)   PORTED  J1_ENTRY, array.cpp (lane UIC)
//   0x180039440  ~Array  (+0x68)   PORTED  J1_ENTRY, array.cpp (lane UIC)
//   0x180039360  ~Array  (+0x58)   PORTED  J2_ENTRY, array_destroy.cpp
//   0x180039280  ~Array  (+0x48)   PORTED  J2_ENTRY, array_destroy.cpp
//   0x1800c7ad0  ?MemFree@         PORTED  (Wave 2 / lane ALLOC)
//   0x1800bc950  ~External         PORTED  (external_class.cpp)
//
// THE TWO WITH AN ELEMENT PASS ARE STATED, NOT GLOSSED. WAVE10_LANE_UI3.md §7b called
// all four "the same family" and suggested two more `J1_ENTRY` lines. **THAT IS
// WRONG AND THIS LANE MEASURED IT RATHER THAN INHERITING IT**
// (reference_holdback_blockers_are_stale_check_them_first.md). All 115 leading
// bytes of each were read with Ghidra `read_memory` and compared against the
// registered J1 reference 0x180033960:
//
//   0x180009460 / 0x180039440   J1 shape: `MOV RDI,RCX ; MOV RSI,[RCX] ; TEST`
//   0x180039360 / 0x180039280   `MOV RDI,RCX ; MOV R8D,[RCX+8] ; MOV RDX,[RCX]
//                                ; CALL rel32` — an EXTRA element-destroy pass
//                                before the free (targets 0x180039640 and
//                                0x180039580 respectively, two DIFFERENT helpers)
//
// They are `~Array<T>` for a T that HAS a destructor. Registering them as J1
// would have silently replaced a body that destroys every element with one that
// only frees the block — a leak, landed under a comment claiming a byte-level
// identity that does not hold. In the port they are the destructors of the
// `_namedTemplates` and `_controllerConfigs` Array members (`~Dialog` below).
//
// Neither is exported (checked against all 4,926 export names).
//
// ---------------------------------------------------------------------------
// THE ALLOCATOR, AND WHY IT IS NOT A BLOCKER
// ---------------------------------------------------------------------------
// The dtor frees: `_spriteName`'s ~String calls the ported allocator's
// `?MemFree@MemoryAllocator@System@@SAXPEAX@Z` (0x1800c7ad0) by name.
//
// PORTABILITY: no <windows.h>, per PLAN.md §4.1 decision 1.
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>

#include "collection/Collection.h" // ENDGAME B.3.2 step 2 — Array<void*> bridge
#include "system/String.h"
#include "ui/Dialog.h"

// ============================================================================
// ENDGAME B.3.9 block UI-D: the two DOORS this file carried are RETIRED. The
// constructor above is `UI::Dialog::Dialog()` (Dialog.h, the three vptr stores
// are the compiler's); the destructor is `UI::Dialog::~Dialog()` below, which
// the retired `RtUiDialog_Dtor` door ran by hand. Their callers -- the factory
// pair, Construct/Reconstruct and the four stack temporaries retail inlines --
// spell `::new (p) UI::Dialog()` and `~T()` (slot 0, flags 0). With the doors go the
// `[DLG2] FIRST LIVE CALL` lines.
// ============================================================================
// ============================================================================
// `??1Dialog@UI@@UEAA@XZ`  0x180112e40
// ============================================================================
namespace UI {

Dialog::~Dialog() {
    // 180112e56: the re-stamp of the Dialog vtable is the compiler's.

    // THE FOUR ARRAYS, DESCENDING: +0x78, +0x68, +0x58, +0x48.
    // B4 Y RT-UI: 180112e60..180112e87 are the four Array members' own
    // destructors, which the compiler runs after this (empty) body in reverse
    // declaration order: `_references` (+0x78, retail 0x180009460, J1),
    // `_templates` (+0x68, 0x180039440, J1), `_namedTemplates` (+0x58,
    // 0x180039360: kSpec_ExtStr10at08's destroy 0x180039640, then J1) and
    // `_controllerConfigs` (+0x48, 0x180039280: kSpec_CfgEntry20's destroy
    // 0x180039580, then J1). The J2 element walk runs only when the size is
    // positive in both forms (the helper's own loop guard).

    // ---- the System::String release at +0x28, ASYMMETRY AND ALL ------------
    // 180112e88..180112eaa: B4 X-RH — `_spriteName` is a System::String
    // member, so this release is its destructor, which the compiler runs after
    // the four arrays and before ~External: retail's order.

    // 180112eba: the TAIL-JMP to ~External is the compiler's chain.
}

}  // namespace UI
