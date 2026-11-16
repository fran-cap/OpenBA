// ============================================================================
// tests/unit/b39_obj_externallist_test.cpp — ENDGAME B.3.9 OBJ (B39_RULES D1,
// rule B9-16): `Object::ExternalList` is concrete.
//
// The class's KEY FUNCTION is `GetFileVersion` (its constructor and slot 0 are inline
// in External.h), so GCC emits its vtable in whichever translation unit defines
// the out-of-line members. In production that is external_class_doors.cpp,
// which does not link into the unit build (`tools/build.sh unit`); here THIS
// file defines them as RECORDING stubs — exactly the shape
// tests/unit/pathfinding_picking_abi_test.cpp uses for Picking::PickingMesh —
// so the REAL header's inline constructor and the compiler's destructor pair
// (B3X W1: retail's slot-0 vector deleting destructor is `virtual ~T()`, D1 +
// D0) run against a real compiler table. No other unit TU defines an
// ExternalList member (external_class.cpp deliberately defines none).
// ============================================================================

#include "unit.h"
#include "slot_check.h"
#include "dtor_check.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <type_traits>

#include "object/External.h"
#include "object/Internal.h"   // Object::detail::InternalMemAlloc

namespace {

int                   g_elDtorCount = 0;
Object::ExternalList* g_elDtorOrder[8] = {};
int                   g_elLastSlot = -1;


const void* ElVTableOf(const void* o) { return *reinterpret_cast<const void* const*>(o); }

}  // namespace

// ---- the recording out-of-line members (see the banner) --------------------
namespace Object {
// The factory pair lives in external_list.cpp (not linked here); the class
// record (DEFINE_TYPE, external_class.cpp) names it, so the unit link needs a
// definition. Slots 1/2 are DECLARE_TYPE's own over that record.
Base*             ExternalList::CreateNewObject()        { return nullptr; }
Base*             ExternalList::CreateInPlaceObject(void*) { return nullptr; }
int32_t           ExternalList::GetFileVersion() const { g_elLastSlot = 3; return 3; }
bool              ExternalList::Serialize(IO::Stream&)   { g_elLastSlot = 5; return true; }
External*         ExternalList::Reconstruct()      { g_elLastSlot = 6; return this; }
void              ExternalList::Destruct()         { g_elLastSlot = 7; }
External*         ExternalList::Construct()        { g_elLastSlot = 8; return this; }
ExternalList::~ExternalList() {
    if (g_elDtorCount < 8) g_elDtorOrder[g_elDtorCount] = this;
    ++g_elDtorCount;
}
}  // namespace Object

// Concrete, 0x30 bytes. B3X W1: the destructor is public and virtual (retail
// slot 0 = D1 + D0).
UNIT_TEST(b39_obj_externallist_is_concrete_with_public_virtual_dtor) {
    CHECK(!std::is_abstract<Object::ExternalList>::value);
    CHECK(std::is_destructible<Object::ExternalList>::value);
    CHECK(std::has_virtual_destructor<Object::ExternalList>::value);
    CHECK_EQ(sizeof(Object::ExternalList), (size_t)0x30);
}

// Slot order read off the REAL header's pointers-to-virtual-member
// (??_7ExternalList@Object@@6B@ 0x180154800, nine slots).
UNIT_TEST(b39_obj_externallist_slot_order_by_pmf) {
    using EL = Object::ExternalList;
    CHECK_DTOR_SLOT(EL);   // 0x180007800
    CHECK_SLOT(EL, TypeName,       0x08/8);   // 0x180007370
    CHECK_SLOT(EL, Type,           0x10/8);   // 0x180007350
    CHECK_SLOT(EL, GetFileVersion, 0x18/8);   // 0x1800bfa10
    CHECK_SLOT(EL, Compile,        0x20/8);   // 0x1800066e0 (External's)
    CHECK_SLOT(EL, Serialize,      0x28/8);   // 0x1800bfa20
    CHECK_SLOT(EL, Reconstruct,    0x30/8);   // 0x180007570
    CHECK_SLOT(EL, Destruct,       0x38/8);   // 0x180007640
    CHECK_SLOT(EL, Construct,      0x40/8);   // 0x1800075e0
}

// The real constructor makes retail's factory stores (0x1800074c3..f4): the
// External fields and +0x20 / +0x28 zero as two qwords (so _pad2c too), and
// the compiler's table, the same one for every instance.
UNIT_TEST(b39_obj_externallist_ctor_makes_retails_stores) {
    alignas(8) uint8_t buf[0x30];
    std::memset(buf, 0xCD, sizeof buf);
    Object::ExternalList* l = ::new (buf) Object::ExternalList();
    CHECK(static_cast<void*>(l) == static_cast<void*>(buf));
    CHECK(l->_path._data == nullptr);
    CHECK(l->_refs._data == nullptr);
    CHECK_EQ(l->_refs._size, 0);
    CHECK_EQ(l->_refs._capacity, 0);
    CHECK(l->_resources == nullptr);
    CHECK_EQ(l->_count, 0);
    CHECK_EQ(l->_pad2c, 0);

    alignas(8) uint8_t buf2[0x30];
    Object::ExternalList* l2 = ::new (buf2) Object::ExternalList();
    CHECK(ElVTableOf(l) != nullptr);
    CHECK(ElVTableOf(l) == ElVTableOf(l2));

    g_elLastSlot = -1;
    CHECK(l->GetFileVersion() == 3);
    CHECK_EQ(g_elLastSlot, 3);

    g_elDtorCount = 0;
    l->~ExternalList();                     // retail flags 0: destroy, no free
    l2->~ExternalList();
    CHECK_EQ(g_elDtorCount, 2);
}

// Retail slot 0 FREES on `flags & 1`, and its ARRAY arm read the int32 count
// at `this - 8`, destroyed DOWNWARD with the 0x30 stride, freed and returned
// `this - 8` — both arms as the retired RT_DELDTOR row 0x180007800 ran them.
// B3X W1: flags 1 is `delete l` (the class's D0: ~ExternalList once, then the
// root's class-scope MemFree operator delete; host free, matching
// InternalMemAlloc), the free itself proven on a Recording<ExternalList>; the
// vector arm has no Itanium counterpart and `delete[]` of a `new
// ExternalList[3]` replaces it.
UNIT_TEST(b39_obj_externallist_deletingdtor_both_arms) {
    static_assert(dtor_check::HasClassDelete<Object::ExternalList>::value, "class-scope operator delete");
    void* p = Object::detail::InternalMemAlloc(sizeof(Object::ExternalList), 8);
    CHECK(p != nullptr);
    Object::ExternalList* l = ::new (p) Object::ExternalList();
    g_elDtorCount = 0;
    delete l;
    CHECK_EQ(g_elDtorCount, 1);
    CHECK(g_elDtorOrder[0] == p);

    Object::Base* r = dtor_check::NewOnMalloc<dtor_check::Recording<Object::ExternalList>>();
    g_elDtorCount = 0; dtor_check::ResetFrees();
    delete r;
    CHECK_EQ(g_elDtorCount, 1);
    CHECK_EQ(dtor_check::g_frees, 1);
    CHECK(dtor_check::g_lastFree == static_cast<const void*>(r));

    CHECK_ARRAY_DELETE(Object::ExternalList, g_elDtorCount, g_elDtorOrder);
}

