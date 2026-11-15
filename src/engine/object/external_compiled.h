#pragma once
// ============================================================================
// src/engine/object/external_compiled.h — Wave 11 lane OBJEXT
//   `Object::ExternalFactory::GetCompiledPath` / `FindOrLoad`.
// ============================================================================

#include <cstdint>

#include "object/External.h"
#include "system/Filename.h"

namespace RtObject {

// ?GetCompiledPath@ExternalFactory@Object@@SA?AVFilename@System@@AEBV34@@Z
//                                                          retail 0x1800bec10
// A STATIC returning by value: RCX = the hidden return slot, RDX = the path.
System::Filename* RtObject_ExternalFactory_GetCompiledPath(System::Filename* ret,
                                                           const System::Filename* path);

// ?FindOrLoad@ExternalFactory@Object@@QEAAPEBVExternal@2@AEBVFilename@System@@@Z
//                                                          retail 0x1800bf320
const ::Object::External* RtObject_ExternalFactory_FindOrLoad(ExternalFactory* self,
                                                    const System::Filename* path);

// NOT RETAIL (docs/extensions/handins/VISITREPLAY.md): FindOrLoad for the visit pass's walk
// (external_visit.cpp): the same body, without ext.find_fastpath.
const ::Object::External* RtObject_ExternalFactory_FindOrLoadVisit(ExternalFactory* self,
                                                         const System::Filename* path);

// NOT RETAIL (docs/extensions/handins/VISITREPLAY.md): FindOrLoad's hit arm on a row ext.find_fastpath
// named (external_compiled.cpp). `row` is an rtresset::Node of `_resources`.
const ::Object::External* RtObject_ExternalFactory_FindOrLoadHitRow(ExternalFactory* self, void* row);

// EXT SEAM (NOT RETAIL, docs/extensions/KITFREE.md §2): the resource providers' entry, null by default.
// FindOrLoad's miss branch calls it, with the EXPANDED path, only after
// External::Load found neither a loose .crs nor a package entry; a non-null
// result is registered exactly as a loaded object (`_path = expanded`, Add).
// src/ext/resources/resources.cpp points it at Ext::Resources::Provide when
// the first provider registers; the faithful profile registers none.
extern ::Object::External* (*g_extProvideSeam)(const char* expandedPath);

// Effectiveness control: every counter is a branch taken INSIDE a ported body.
struct CompiledCounters {
    uint64_t getCompiledCalls;
    uint64_t findOrLoadCalls;
    uint64_t buildCalls;       // the shared six-term concatenation
    uint64_t emptyPath;        // FindOrLoad's null/zero-length early-out
    uint64_t cacheHit;
    uint64_t hitReAdded;       // ... and _loadEnabled was set, so Add ran
    uint64_t hitNotReAdded;
    uint64_t cacheMiss;
    uint64_t loaded;           // External::Load returned an object
    uint64_t loadFailed;
};
extern CompiledCounters g_cc;

}  // namespace RtObject
