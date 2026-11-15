// ============================================================================
// src/engine/io/persistence_startup.cpp — Wave 9 lane ENG, iteration 3
// IO::Persistence::Startup — Engine::Startup's step 4
// ============================================================================
//
//   | Export                             | Retail      | Ord  | Instrs |
//   |------------------------------------|-------------|------|--------|
//   | ?Startup@Persistence@IO@@QEAA_NXZ  | 0x1800b7ee0 | 4103 |   45   |
//
// Name checked against Runtime-x64.dll's export directory with pefile before a
// line was written. No new Win32 API and no new import library.
//
// Its own file rather than an edit to `persistence.cpp` (Wave 4 lane C) purely
// so two lanes were not editing one file while both were in flight.
//
// This file touches nothing under `src/engine/io/filesystem*`; lane FS owns
// those. It CALLS `?GetSaveDirectory@FileSystem@IO@@` by name, which is a
// resolve, not an edit.
//
// ---------------------------------------------------------------------------
// THE BODY
// ---------------------------------------------------------------------------
//   0x1800b7ef7  ReadSettings(this)                       ; ord 3462
//   0x1800b7f01  Filename tmp = FileSystem::_singleton->GetSaveDirectory()
//                                                          ; ord 2886, hidden
//                                                            return slot in RDX
//   0x1800b7f10  this->_savePath = tmp                    ; String::operator=,
//                                                            INLINED
//   0x1800b7f5d  ~tmp
//   0x1800b7f8e  return true                              ; MOV AL,1 -- retail
//                                                            has NO failure arm
//
// `GetSaveDirectory` returns a `System::Filename` BY VALUE, so the hidden
// return slot is the FIRST argument and `this` is the second
// (reference_hidden_retval_and_int_pointer_args.md). Retail passes
// `RCX = FileSystem::_singleton`, `RDX = &tmp` and then reads the result back
// out of `[RAX]`, where `RAX == RDX`. `Filename` and `String` are
// layout-identical (src/engine/system/Filename.h), one pointer.
//
// The assignment at 0x1800b7f10 is `String::operator=` inlined, and it
// matters because of its FIRST test:
//
//     CMP RCX,[RAX]   ; if the two String::data pointers are already EQUAL,
//     JZ  skip        ; do nothing at all -- no release, no AddRef
//
// A self-assignment through the release-then-adopt path would free the buffer
// and then AddRef freed memory. Retail's guard is what prevents that, and it is
// the one line here that a "tidy" rewrite would drop.
//
// ---------------------------------------------------------------------------
// ITS TWO CALLEES
// ---------------------------------------------------------------------------
// `?ReadSettings@Persistence@IO@@QEAAXXZ` (0x1800b84b0, ord 3462) is in
// persistence_settings.cpp; `?GetSaveDirectory@FileSystem@IO@@QEBA?AVFilename@
// System@@XZ` (0x1800acfd0, ord 2886) is in filesystem.cpp. Both are called by
// name.
//
// ---------------------------------------------------------------------------
// ORACLE
// ---------------------------------------------------------------------------
// None. It runs once, from `Engine::Startup`; `ReadSettings` reads settings.cfg
// (persistence_settings.cpp) into the object and a replay would do it twice.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <utility>

#include "io/Persistence.h"
#include "io/FileSystem.h"   // ENDGAME B.3.9 IO: the class static
#include "system/String.h"
#include "system/Filename.h"
#include <new>


namespace {

// ?_singleton@FileSystem@IO@@0PEAV12@EA, ord 4811, retail 0x1801fc6a8.
// Written by `??0FileSystem@IO@@QEAA@XZ`; read here as the class static.
::IO::FileSystem** FileSystemSingletonSlot() {
    return &::IO::FileSystem::_singleton;   // ENDGAME B.3.9 IO (D7): the class static.
}

}  // namespace

// ============================================================================
// ?Startup@Persistence@IO@@QEAA_NXZ                        retail 0x1800b7ee0
// ============================================================================

// ============================================================================
// `IO::Persistence::Startup` -- the member body
// ============================================================================
// Defined in `namespace IO` because a member of `IO::X` must appear in a
// namespace ENCLOSING the class (B34_BRIEFS section A.2); the file-local helper
// lives in this translation unit's GLOBAL anonymous namespace and is reached as
// `::FileSystemSingletonSlot`.
//
// B3R RT-IO: `ReadSettings` and `FileSystem::GetSaveDirectory` are called by
// name (they were two RT_EXPORT_ONCE doors). The unguarded FileSystem-singleton
// DEREFERENCE stays unguarded and the guard on whether the SLOT EXISTS stays
// exactly where it was.
namespace IO {

bool Persistence::Startup() {

    ReadSettings();

    // GetSaveDirectory's by-value Filename is constructed straight into `tmp`
    // (its hidden return slot); with no slot `tmp` is the null Filename.
    // Retail dereferences FileSystem::_singleton without a check (0x1800b7f01
    // is a bare `MOV RCX,[0x1801fc6a8]`). The DEREFERENCE stays unguarded. The
    // `fsSlot` test is on the class static's address, which is never null.
    ::IO::FileSystem** const fsSlot = ::FileSystemSingletonSlot();
    System::Filename tmp = fsSlot ? (*fsSlot)->GetSaveDirectory() : System::Filename();

    // String::operator=, retail 0x1800c9810, inlined at 0x1800b7f10 -- the
    // exact self-assignment guard on the BUFFER (the banner above).
    _savePath = tmp;

    // ~tmp is retail's release at 0x1800b7f5d.

    return true;
}

}  // namespace IO

