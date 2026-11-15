#pragma once
// ============================================================================
// src/engine/system/setupdir_helpers.h — Wave 9 lane FS
// ============================================================================
// Two of the three System-namespace leaves of IO::FileSystem::SetupDirectories
// (retail Configuration::GetPlatformName / GetBuildName), declared here so
// src/engine/io/filesystem_setupdirs.cpp can call them by name.
//
// Both return a String BY VALUE (B.4 Z LAST IO: declared so; the hidden return
// slot is the compiler's).
// ============================================================================

#include "system/String.h"
#include "system/Filename.h"

namespace RtSystem {
// ?GetPlatformName@Configuration@System@@QEAA?AVString@2@W4Platform@12@@Z
//                                                                  0x1800c5210
System::String RtSystem_Configuration_GetPlatformName(void* self, int platform);

// ?GetBuildName@Configuration@System@@QEAA?AVString@2@W4Build@12@@Z 0x1800c5280
System::String RtSystem_Configuration_GetBuildName(void* self, int build);

}  // namespace RtSystem
