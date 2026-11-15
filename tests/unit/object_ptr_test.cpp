// ============================================================================
// tests/unit/object_ptr_test.cpp — ENDGAME B.3.1 step 1, the smart pointers.
//
// WHAT IS AND IS NOT COVERED HERE
// -------------------------------
// `Object::ExternalPtr<T>` and `Object::InternalPtr<T>`/`InternalBasePtr` are
// NON-OWNING views: one raw pointer, no refcount, nothing to run on copy or
// destruction (docs/history/endgame_b/SMARTPTR_INVENTORY.md §1, §5.2).
// So the whole of their observable behaviour on the host is the trivial
// semantics tested below — and those semantics are load-bearing, not trivia:
// `Collection::Array` bulk-moves these eight bytes with a plain qword copy and
// an explicit "NO refcount touch" (src/engine/collection/array_destroy.cpp:1000), so a
// user-provided copy ctor or destructor would be SILENTLY BYPASSED. The
// `is_trivially_*` checks below are the regression test for that hazard.
//
// `Serialize` is NOT tested here and cannot be: it needs a live
// `IO::Serializer` with a real `ReaderWriter` vtable, `Object::ExternalFactory`
// (two AVL trees and a singleton), `Object::Base::IsDerivedFrom` and the
// `?_typeInfo@T@…` records — none of which exists on the host. It is covered by
// the real run (`tools/build.sh run`, and the resave gate on the coordinator's merge),
// which is what has always verified it.
//
// `CheckPtr` and `AssignId` are deliberately untestable no-ops: they are bare
// `RET`s in retail AND in the profile build, so there is nothing to assert
// beyond "they compile and do nothing", which the calls below do.
// ============================================================================
#include "unit.h"

#include <memory>
#include <type_traits>

#include "object/External.h"
#include "object/Internal.h"

namespace {

// Stand-ins for the pointees. Nothing is dereferenced through a live object
// here; only the pointer value is ever observed.
struct TestExternal { void* vftable; char* path; void* refs; int32_t size, capacity; };  // the 0x20 bytes of an External (the RtObject::External POD mirror is gone)
TestExternal g_extA;

using ExtPtr  = Object::ExternalPtr<TestExternal>;
using BasePtr = Object::InternalBasePtr;
using IntPtr  = Object::InternalPtr<Object::Internal>;

// `InternalBasePtr` holds an `Object::Internal*`. Two never-constructed slots
// of that type give the pointer values (Object::Internal's ctor registers with
// the InternalFactory, which this file does not install).
Object::Internal* const g_objA = std::allocator<Object::Internal>().allocate(1);
Object::Internal* const g_objB = std::allocator<Object::Internal>().allocate(1);

}  // namespace

// ---------------------------------------------------------------------------
// Layout and triviality — the array-bulk-copy hazard (SMARTPTR_INVENTORY §5.4)
// ---------------------------------------------------------------------------
UNIT_TEST(object_ptr_sizeof_is_one_pointer) {
    CHECK_EQ(sizeof(ExtPtr), sizeof(void*));
    CHECK_EQ(sizeof(ExtPtr), (size_t)8);
    CHECK_EQ(sizeof(Object::ExternalPtr<Object::ExternalList>), (size_t)8);
    CHECK_EQ(sizeof(BasePtr), (size_t)8);
    CHECK_EQ(sizeof(IntPtr), (size_t)8);   // the typed wrapper adds nothing
    CHECK_EQ(alignof(ExtPtr), alignof(void*));
    CHECK_EQ(alignof(BasePtr), alignof(void*));
}

UNIT_TEST(object_ptr_is_trivially_copyable_and_destructible) {
    // If any of these ever fails, every Array<T> resize in the engine has
    // started silently skipping whatever the new copy ctor/dtor was to do.
    CHECK(std::is_trivially_copyable<ExtPtr>::value);
    CHECK(std::is_trivially_destructible<ExtPtr>::value);
    CHECK(std::is_trivially_copy_constructible<ExtPtr>::value);
    CHECK(std::is_trivially_copy_assignable<ExtPtr>::value);

    CHECK(std::is_trivially_copyable<BasePtr>::value);
    CHECK(std::is_trivially_destructible<BasePtr>::value);
    CHECK(std::is_trivially_copy_constructible<BasePtr>::value);
    CHECK(std::is_trivially_copy_assignable<BasePtr>::value);

    CHECK(std::is_trivially_copyable<IntPtr>::value);
    CHECK(std::is_trivially_destructible<IntPtr>::value);
}

UNIT_TEST(external_ptr_survives_a_bulk_byte_copy) {
    // Exactly what Collection::Array<T>::operator= does to these eight bytes.
    ExtPtr src(&g_extA);
    ExtPtr dst;
    std::memcpy(&dst, &src, sizeof(ExtPtr));
    CHECK(dst.Get() == &g_extA);
    CHECK(src.Get() == &g_extA);
}

UNIT_TEST(internal_base_ptr_shim_copy_matches_assignment) {
    // `Shim_Copy` is a static forwarder because retail's `Copy` is protected
    // (`IEAA`).
    BasePtr src(g_objB);
    BasePtr dst;
    BasePtr::Shim_Copy(&dst, &src);
    CHECK(dst.Get() == g_objB);
}

UNIT_TEST(internal_base_ptr_checkptr_and_assignid_are_no_ops) {
    // Bare RETs in retail AND in the profile build. They must never validate:
    // a stale InternalBasePtr does not self-invalidate in any shipped build
    // (memory reference_checkptr_is_a_ret_in_every_build).
    BasePtr p(g_objA);
    BasePtr::Shim_CheckPtr(&p);
    BasePtr::Shim_AssignId(&p);
    CHECK(p.Get() == g_objA);   // unchanged, and no validation happened

    BasePtr stale(g_objA);
    stale = BasePtr(reinterpret_cast<Object::Internal*>(0x1234));
    BasePtr::Shim_CheckPtr(&stale);
    CHECK(stale.Get() == reinterpret_cast<Object::Internal*>(0x1234));
}

