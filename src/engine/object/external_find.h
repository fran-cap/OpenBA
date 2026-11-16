#pragma once
// ============================================================================
// src/engine/object/external_find.h — Wave 9 lane RRES, follow-on rung
//   `ExternalFactory::Find`, the private cache lookup.
// ============================================================================
//
//
// It is NOT folded into src/engine/io/external_path.cpp, which reaches the
// filesystem through Platform::PathAttributes (platform_file.h); this body needs
// nothing from the platform seam (docs/runtime_port/PLAN.md §4.1 decision 1).
// ============================================================================

#include <cstdint>

#include "object/External.h"
#include "system/Filename.h"

namespace RtObject {

// ?Find@ExternalFactory@Object@@AEAAPEAVExternal@2@AEBVFilename@System@@@Z
//                                                     retail 0x1800be0e0, ord 2336
// RCX = this, RDX = path. Returns the cached External*, or null on a miss.
::Object::External* RtObject_ExternalFactory_Find(ExternalFactory* self, const System::Filename* path);

// The effectiveness control. `hits` and `misses` are the two outcomes of the
// tree descent; `identDefaulted` is the ":resource" arm. A run in which
// `identDefaulted` stays 0 has not exercised the only interesting branch in the
// function, and the report must say so rather than report `calls` and stop
// (the diff oracle, lane M: a large `calls` is not coverage).
struct FindCounters {
    uint64_t calls;
    uint64_t hits;
    uint64_t misses;
    uint64_t identDefaulted;
    uint64_t identPresent;
    uint64_t divergent;      // no writer
};
extern FindCounters g_fc;

}  // namespace RtObject
