// ============================================================================
// src/engine/system/setupdir_helpers.cpp — Wave 9 lane FS
// Two of the three System-namespace leaves of IO::FileSystem::SetupDirectories (the third,
// Filename::GetPathDirectory, is in filename.cpp)
// ============================================================================
//
//   | Export                                                    | Retail      | Ord  |
//   |-----------------------------------------------------------|-------------|------|
//   | ?GetPathDirectory@Filename@System@@QEBA?AV12@XZ           | 0x1800c6880 | 2836 |
//   | ?GetPlatformName@Configuration@System@@QEAA?AVString@2@W4Platform@12@@Z | 0x1800c5210 | 2850 |
//   | ?GetBuildName@Configuration@System@@QEAA?AVString@2@W4Build@12@@Z       | 0x1800c5280 | 2470 |
//
// All three verified against Runtime-x64.dll's export directory with pefile
// before a body was written. All three RETURN A String BY VALUE, so the
// hidden return slot sits between `this` and the declared arguments — RCX =
// this, RDX = &ret, R8 = the first real parameter. Getting that wrong here
// would have made the enum argument land in the return slot.
//
// NOTHING HERE TOUCHES WIN32, so it stays out of a `_win` file per PLAN §4.1.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cstdio>
#include <new>

#include "system/String.h"
#include "system/Filename.h"
#include "system/string_internal.h"
#include "setupdir_helpers.h"

namespace RtSystem {

namespace {

bool SameText(const char* a, const char* b) {
    if ((a == nullptr) != (b == nullptr)) return false;
    if (a == nullptr) return true;
    return std::strcmp(a, b) == 0;
}

}  // namespace

// ============================================================================
// ?GetPlatformName@Configuration@System@@QEAA?AVString@2@W4Platform@12@@Z
//                                                          retail 0x1800c5210
// ============================================================================
// Platform 0 -> "Win" (literal at 0x180155cc4, read with read_memory).
// Anything else -> the NULL String, not an empty one: retail writes
// `*ret = 0` and returns without ever calling Set.
//
// `this` (RCX) is moved nowhere and NEVER DEREFERENCED — checked in the
// disassembly, not inferred from the decompile. That is not a curiosity: it is
// what makes this call safe for SetupDirectories to make with whatever `cfg`
// it passes.
System::String RtSystem_Configuration_GetPlatformName(void* self, int platform) {
    (void)self;   // retail never reads it

    // Retail's `*ret = 0` IS the default ctor (??0String@System@@QEAA@XZ,
    // 0x180006760, one qword store) in the hidden return slot (the named
    // return value below is built there). The conditional Set is retail's own
    // 0x1800c95d0 call.
    System::String out;
    if (platform == 0)
        out.Set("Win");

    return out;
}

// ============================================================================
// ?GetBuildName@Configuration@System@@QEAA?AVString@2@W4Build@12@@Z
//                                                          retail 0x1800c5280
// ============================================================================
// Build 0 -> "Data" (0x180155cd0), build 1 -> "Eval" (0x180155cd8), anything
// else -> the null String. Retail spells the second test as `DEC R8D ; JZ`,
// not as a second compare; same thing, transcribed as an if-chain.
//
// `this` is unread here too. Together with GetPlatformName this is what makes
// the boot's data directory: `Format("%s%s%s%s\\", root, "", "Win", "Data")`
// = "<install>\WinData\", which is exactly the directory the shipped game
// ships (verified on disk).
System::String RtSystem_Configuration_GetBuildName(void* self, int build) {
    (void)self;   // retail never reads it

    // As GetPlatformName above.
    System::String out;
    if (build == 0)      out.Set("Data");
    else if (build == 1) out.Set("Eval");

    return out;
}

}  // namespace RtSystem
