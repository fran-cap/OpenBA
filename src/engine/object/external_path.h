#pragma once
// ============================================================================
// src/engine/object/external_path.h — Wave 9 lane RRES
//   `Object::ExternalFactory`'s PATH half: the two path-stack readers and
//   `ExpandRelativePath`, plus the `System::Filename::GetPathExtension` those
//   two need.
// ============================================================================
//
// WHY THIS FILE EXISTS AS ITS OWN TRANSLATION UNIT
//
// Lane RES landed the resource TREE (`external_set.cpp`) and declined
// `FindOrLoad` with a measured blocker: `ExpandRelativePath` (834 instructions)
// could not be written because `ExternalFactory + 0x30 / +0x38` were recorded
// as NOT PINNED in `src/engine/object/External.h`. That is now pinned — see
// the `PathStackEntry` block in that header — and this file is the port that
// the pin makes possible.
//
//
// WHAT IS DELIBERATELY *NOT* HERE
//
//   `PushPath` (0x1800bd0e0) and `PopPath` (0x1800bd040) are the WRITERS of the
//   path stack this file reads. They are in external_path_stack.cpp, with
//   `0x18002e220`, the `Collection::Array<PathStackEntry>` row they need
//   (`kSpec_PathStack`). See docs/history/runtime_port/WAVE9_LANE_RRES.md.
//
// GetPathExtension's BODY IS IN src/engine/io/filename_extension.cpp
//
//   `?GetPathExtension@Filename@System@@QEBA?AV12@XZ` (0x1800c64f0) is a
//   `System::Filename` method. It is declared below as
//   `RtSystem_Filename_GetPathExtension` and defined in
//   src/engine/io/filename_extension.cpp; the member `Filename::GetPathExtension`
//   (src/engine/system/filename.cpp) forwards to it. `ExpandRelativePath` calls
//   it twice and `PushPath` calls it once.
// ============================================================================

#include <cstdint>

#include "object/External.h"
#include "system/Filename.h"

namespace RtObject {

// ?GetPathExtension@Filename@System@@QEBA?AV12@XZ           retail 0x1800c64f0
// Hidden return slot: RCX = self, RDX = ret.
System::Filename* RtSystem_Filename_GetPathExtension(const System::Filename* self,
                                                     System::Filename* ret);

// ?GetPathStackFilename@ExternalFactory@Object@@QEBA?AVFilename@System@@XZ
//                                                          retail 0x1800bcf60
System::Filename* RtObject_ExternalFactory_GetPathStackFilename(const ExternalFactory* self,
                                                                System::Filename* ret);

// ?GetPathStackDirectory@ExternalFactory@Object@@QEBA?AVFilename@System@@XZ
//                                                          retail 0x1800bcfd0
System::Filename* RtObject_ExternalFactory_GetPathStackDirectory(const ExternalFactory* self,
                                                                 System::Filename* ret);

// ?ExpandRelativePath@ExternalFactory@Object@@QEBA?AVFilename@System@@AEBV34@_N@Z
//                                                          retail 0x1800bd360
// RCX = this, RDX = hidden return slot, R8 = path, R9B = appendIdentifier.
System::Filename* RtObject_ExternalFactory_ExpandRelativePath(const ExternalFactory* self,
                                                              System::Filename* ret,
                                                              const System::Filename* path,
                                                              bool appendIdentifier);

// The effectiveness control. Every counter is a branch taken INSIDE a ported
// body, not a call to one: a large `calls` with an untaken branch is the
// `worked=0` trap one level down (the diff oracle, lane M's Finding 1).
struct PathCounters {
    uint64_t expandCalls;
    uint64_t expandEmptyPath;     // the `!path->data || len == 0` early-out
    uint64_t identDefaulted;      // identifier was empty -> "resource"
    uint64_t identPresent;
    uint64_t filenameFromStack;   // filename was empty -> taken from the stack
    uint64_t filenamePresent;
    uint64_t stackEmpty;          // a GetPathStack* call on a 0-deep stack
    uint64_t stackNonEmpty;
    uint64_t hitPackfile;         // found through the GATED ExistsInPackfile
    uint64_t attrProbeEntered;    // the GetFileAttributesW arm was REACHED
    uint64_t hitAttributes;       // ... and it said the file exists
    uint64_t hitPackfileRetry;    // found through the UNGATED second probe
    uint64_t notFound;            // all three probes missed
    uint64_t appendedIdentifier;  // the R9B == true tail
    uint64_t noIdentifierTail;
    uint64_t extCalls;
    uint64_t extNullNoData;       // GetPathExtension: self->data == null
    uint64_t extNoDot;            // ... no '.' at or before the scan start
    uint64_t extEmptyAfterDot;    // ... '.' is the last character
    uint64_t extColonTrim;        // ... a ':' shortened the scan window
    uint64_t extOk;
};
extern PathCounters g_pc;

}  // namespace RtObject
