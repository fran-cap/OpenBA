// ============================================================================
// src/engine/object/internal_ptr_array_serialize.cpp
//
// The array form of InternalPtr<T>::Serialize — 180 bytes, two instantiations
// reached by the cutover closure (docs/history/runtime_port/closure/WORKLIST.md):
//
//     0x180009a20  T = Achievement::AchievementInstance
//     0x180009fa0  T = Achievement::GoalInstance
//
//     void (Serializer* ser, int count, InternalBasePtr* elems)   RCX, EDX, R8
//
// Retail, per element:
//     InternalBasePtr::Serialize(&elems[i], ser);
//     obj = elems[i].ptr;
//     if (obj && obj->GetTypeId() != T::typeId) {
//         rec = Factory::_singleton->FindNode(obj->GetTypeId()) + 8;   // unguarded
//         while (rec->typeId != T::typeId) { rec = rec->parent; if (!rec) { elems[i].ptr = 0; break; } }
//     }
//
// The walk after the load is exactly Object::Base::IsDerivedFrom (type_system.cpp),
// including the unguarded FindNode result, so the port calls that body rather
// than transcribing the walk a third time. T's typeId is the first dword of its
// `?_typeInfo@` node; the caller passes it as `T::TypeId()`.
// ============================================================================
#include <cstdint>
#include <cstdio>

#include "object/Internal.h"

// A C++-linkage body in namespace RtObject (type_system.cpp).
namespace RtObject {
bool RtObject_Base_IsDerivedFrom(const ::Object::Base* self, uint32_t typeId);
}

// ---------------------------------------------------------------------------
// ENDGAME B.3.1 step 1. The body is now
// `Object::InternalPtr<T>::SerializeArray` (src/engine/object/Internal.h),
// one template body for both instantiations — re-verified 2026-09-13,
// function-size-bounded, at identical=55 / position-only=2 / REAL=1, the one
// REAL instruction being the `_typeInfo` load. `T`'s type id is supplied by
// the caller as `T::TypeId()`.
//
// The `IsDerivedFrom` customization point the class declares: it is reached
// through `RtObject::RtObject_Base_IsDerivedFrom` (type_system.cpp), which calls
// `Object::Base::IsDerivedFrom`.
// ---------------------------------------------------------------------------
namespace Object {
bool InternalPtrIsDerivedFrom(const Internal* obj, uint32_t typeId) {
    return RtObject::RtObject_Base_IsDerivedFrom(obj, typeId);
}
}  // namespace Object

// B.4 P2 wave X (X-R1): the registered shim and its two per-T thunks
// (0x180009a20 AchievementInstance, 0x180009fa0 GoalInstance) are retired; the
// two callers (achievement_serialize.cpp, achievement_vtable.cpp) name
// `Object::InternalPtr<T>::SerializeArray(ser, count, data, T::TypeId())` over
// their typed `Array<InternalPtr<T>>` directly. T's type id is the class's own
// DEFINE_TYPE record's, read fresh per call (retail: the first dword of
// ?_typeInfo@T@Achievement@@, 0x1801f98d0 / 0x1801f9940).
