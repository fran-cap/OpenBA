#pragma once
// ============================================================================
// src/engine/object/external_path_stack.h — Wave 11 lane OBJEXT
//   `Object::ExternalFactory::PushPath` / `PopPath` and the four
//   `Collection::Array<PathStackEntry>` bodies they need.
// ============================================================================
//
// ============================================================================

#include <cstddef>
#include <cstdint>

#include "object/External.h"
#include "system/Filename.h"

namespace RtObject {

// ?PushPath@ExternalFactory@Object@@QEAAXAEBVFilename@System@@@Z  0x1800bd0e0
void RtObject_ExternalFactory_PushPath(ExternalFactory* self, const System::Filename* path);

// ?PopPath@ExternalFactory@Object@@QEAAXXZ                        0x1800bd040
void RtObject_ExternalFactory_PopPath(ExternalFactory* self);

// The effectiveness control. Every counter is a branch taken INSIDE a ported
// body, not a call to one: a large `calls` with an untaken branch is the
// `worked=0` trap one level down (the diff oracle).
struct PathStackCounters {
    uint64_t pushCalls;
    uint64_t popCalls;
    uint64_t setSizeCalls;
    uint64_t removeAtCalls;
    uint64_t removeAtInRange;     // the shift-down loop was entered
    uint64_t removeAtPastEnd;     // index >= size: only the --size runs
    uint64_t removeLastCalls;
    uint64_t elemsShifted;        // elements moved down by RemoveAt
    uint64_t elemsDestroyed;      // elements released by the destroy-range
    int32_t  depthAfterPush;
    int32_t  depthAfterPop;
};
extern PathStackCounters g_psc;

// (B.4 Z MIRROR: the `PathStackElement` view over the POD is gone; the element
// is `PathStackEntry` itself, whose halves are the real System::Filename and
// whose constructor / destructor are retail's (External.h).)

}  // namespace RtObject

extern "C" {
// The four container entry points, spelled with their retail addresses so a
// reader can grep either way.
void  RtObject_PathStackSetSize_18002e220(void* arr, int n, const void* fill);
void  RtObject_PathStackDestroyRange_18002e8d0(void* ignoredThis, uint8_t* base, int count);
RtObject::PathStackEntry* RtObject_PathStackRemoveLast_18002ccb0(
          RtObject::PathStackArray* arr, void* out);
void  RtObject_PathStackRemoveAt_18002de30(RtObject::PathStackArray* arr,
                                           int index);
}

// Declared only: no definition and no caller in the tree.
namespace RtObject { int RtObjectExternalPathStackRegister(); }
