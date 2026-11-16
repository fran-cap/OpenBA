#pragma once
// ============================================================================
// src/engine/object/resource_ext.h -- the resource table's extension seams
//   (NOT RETAIL, docs/extensions/handins/VISITREPLAY.md)
// ============================================================================
// Two class A extensions sit on `ExternalFactory::_resources`:
//
//   ext.visit_replay  (src/ext/visit_replay/): Add's first-touch reference visit
//     of a row replayed from the list of rows that visit hands to Add's hit arm,
//     recorded the last time it ran for real.
//   ext.find_fastpath (src/ext/find_fastpath/): FindOrLoad's hit arm answered
//     from the request's characters (and the save load's reference id) when the
//     same request already named a row.
//
// Both are null when their row is off and under the faithful profile; every faithful site
// pays one null test. A row is passed as `void*` (an rtresset::Node of
// _resources, external_set.h): the seams do not name the tree's node type.
// ============================================================================

#include <cstdint>

namespace Object {

class External;
class ExternalFactory;

// What changed in the resource table, for the extensions' invalidation.
enum class ResourceEvent : uint8_t {
    Insert,      // rtresset::InsertCore linked a new row
    Miss,        // FindOrLoad's miss arm ran (a load, or a provider, or nothing)
    Erase,       // rtresset::Erase is about to unlink `row`
    Clear,       // every row freed wholesale (rtresset::IndexClear, ~ExternalFactory)
    Reload,      // External::Load(path, existing): `external` rebuilt in place
    PathWrite,   // `external->_path` is about to become `newPath`
    RefTable,    // SerializeReferenceTable rebuilds `_references`
};

// ext.visit_replay. The hooks run inside RtObject_ExternalFactory_AddHit
// (external_add.cpp), in this order for one call: `edge` at entry; on a first
// touch `replay`, and when it declines, `visitBegin` / the real visit /
// `visitEnd`. `viaAdd` says the call came through Add's descent (FindOrLoad's
// hit arm) rather than the visit's own-row shortcut.
struct ExtVisitReplay {
    void (*edge)(ExternalFactory& f, void* row, External* external, bool viaAdd);
    bool (*replay)(ExternalFactory& f, void* row, External* external);
    void (*visitBegin)(ExternalFactory& f, void* row);
    void (*visitEnd)(ExternalFactory& f, void* row);
    void (*event)(ExternalFactory& f, ResourceEvent e, void* row, const External* external, const char* newPath);
};

// ext.find_fastpath. `find` / `findRef` return the row FindOrLoad's hit arm
// would land on, or null (the faithful path runs); `store` / `storeRef` are
// told what a faithful hit arm found.
struct ExtFindFast {
    void* (*find)(ExternalFactory& f, const char* request, int len);
    void (*store)(ExternalFactory& f, const char* request, int len, const External* found);
    void* (*findRef)(ExternalFactory& f, uint32_t refId);
    void (*storeRef)(ExternalFactory& f, uint32_t refId, const char* request, int len);
    void (*event)(ExternalFactory& f, ResourceEvent e, void* row, const External* external, const char* newPath);
};

// Hands `e` to whichever of the two is installed (external_set.cpp).
void NotifyResourceEvent(ResourceEvent e, void* row, const External* external, const char* newPath);

}  // namespace Object
