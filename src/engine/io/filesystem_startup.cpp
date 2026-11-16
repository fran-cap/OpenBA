// ============================================================================
// src/engine/io/filesystem_startup.cpp — Wave 9 lane ENG, iteration 2
// IO::FileSystem::Startup — the row behind SetCommandLine in the boot queue
// ============================================================================
//
//   | Export                            | Retail      | Ord  | Instrs |
//   |-----------------------------------|-------------|------|--------|
//   | ?Startup@FileSystem@IO@@QEAA_NXZ  | 0x1800abc50 | 4097 |   38   |
//
// Name checked against Runtime-x64.dll's export directory with pefile before a
// line was written. No new Win32 API and no new import library.
//
// It lives in its own file rather than in `filesystem.cpp` (Wave 4 lane D)
// purely so that two lanes were not editing one file while both were in flight.
//
// ---------------------------------------------------------------------------
// THE BODY, AND THE ONE THING THAT IS NOT OBVIOUS IN IT
// ---------------------------------------------------------------------------
//   0x1800abc67  SetupDirectories(this)                     ; ord 4029
//   0x1800abc6c  this->existsInPackfileGate = 1             ; +0x18
//   0x1800abc79  String tmp; String::Set(&tmp, "onlypkg")   ; literal 0x1801556e8
//   0x1800abc8b  found = Find(Configuration::_singleton + 0x10, &tmp) != null
//   0x1800abca7  ~tmp
//   0x1800abcd4  if (found) this->platformExistsGate = 0    ; +0x19
//   0x1800abcdb  LoadPackages(this)                         ; ord 3209
//   0x1800abce0  return true                                ; MOV AL,1 -- no failure path
//
// The literal was read with `read_memory` at 0x1801556e8 and is the seven bytes
// `onlypkg`, NUL-terminated. So the two gate bytes at +0x18/+0x19 are the
// `-onlypkg` command-line switch: with it set, `FileSystem::Exists` stops
// consulting the real filesystem and answers only out of the mounted `.pkg`
// files (src/engine/io/FileSystem.h documents both gates from `Exists`'s own
// two `CMP byte` tests). On this project's boots the command line is empty
// (see src/engine/system/configuration.cpp §2), so `found` is false and the
// gate keeps the value `SetupDirectories` left in it.
//
// The unguarded `*(Configuration**)0x1801fc6f8` is retail's and is KEPT -- see
// the comment at the call.
//
// ---------------------------------------------------------------------------
// THE TWO CALLEES
// ---------------------------------------------------------------------------
// `?SetupDirectories@FileSystem@IO@@QEAAXXZ` (0x1800abd00, ord 4029) is
// FileSystem::SetupDirectories (filesystem_setupdirs.cpp) and
// `?LoadPackages@FileSystem@IO@@QEAAXXZ` (0x1800ab460, ord 3209) is
// FileSystem::LoadPackages (filesystem_packages.cpp); Startup calls both as
// members.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include <cstdio>

#include "io/FileSystem.h"
#include "system/String.h"
#include "system/Configuration.h"   // ENDGAME B.3.9 SS1: Configuration::_singleton

// ============================================================================
// ?Startup@FileSystem@IO@@QEAA_NXZ                          retail 0x1800abc50
// ============================================================================

// ============================================================================
// `IO::FileSystem::Startup` — the member body
// ============================================================================
// Defined in `namespace IO` because a member of `IO::X` must appear in a
// namespace ENCLOSING the class (B34_BRIEFS section A.2).
//
// Same callees in the same order, the same unguarded Configuration
// dereference, the same single `return true`. B3R RT-IO: SetupDirectories and
// LoadPackages are member calls and the map find is `Configuration::_map.Find`
// (they were two RT_EXPORT_ONCE doors and a by-address resolve).
namespace IO {

bool FileSystem::Startup() {

    SetupDirectories();

    _existsInPackfileGate = 1;

    // ENDGAME B.3.1 step 2: retail's `String::Set(&tmp, "onlypkg")`
    // (0x1800abc79, the literal at 0x1801556e8) followed by the dtor at
    // 0x1800abca7 is exactly the `const char*` ctor and RAII destruction --
    // the two RT_EXPORT_ONCE calls that used to spell ctor/dtor by name are
    // gone; `tmp`'s destructor is the retail dtor now.
    System::String tmp("onlypkg");

    // Retail: `MOV RCX,[0x1801fc6f8]; ADD RCX,0x10` -- the map lives at
    // Configuration + 0x10, and retail dereferences the singleton without a
    // check. ENDGAME B.3.9 SS1 (B39_RULES D7): `?_singleton@Configuration@System@@`
    // IS `::System::Configuration::_singleton` now (class static, always exists) --
    // the existence guard the door version needed is gone, so the deref below is
    // retail's true unguarded one.
    //
    // The find itself (0x18002ace0) has NO export name in either build; it is
    // the settings Set's own `Find` (B7 LX: Configuration::_map is the Set).
    bool found = ::System::Configuration::_singleton->_map.Find(tmp._data) != nullptr;   // Configuration + 0x10

    if (found) _platformExistsGate = 0;

    LoadPackages();


    return true;
}

}  // namespace IO

