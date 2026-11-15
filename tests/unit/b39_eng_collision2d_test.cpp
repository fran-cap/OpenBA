// ============================================================================
// tests/unit/b39_eng_collision2d_test.cpp — ENDGAME B.3.9 block ENG
//
//   Collision2D::Surface        : Object::External  0x30  9 slots (Compile inherited)
//   Collision2D::CollisionMesh  : Object::External  0x70  9 slots (Compile overridden)
//
// Both were file-local PODs built by the FACTREST step table until block ENG
// defined the classes (B39_RULES D1/D2, rule B9-16): concrete, slot order by
// PMF, the real ctor's retail stores, both arms of retail's slot-0 deleting
// destructor (B3X W1: a public `virtual ~T()`, D1 + D0; the vector arm is
// retired, `delete[]` replaces it). The out-of-line members are MARKERS here
// (surface.cpp / collisionmesh.cpp do not link into a host test); the ctors are
// inline in the headers and are the code under test.
// ============================================================================
#include "unit.h"
#include "slot_check.h"
#include "dtor_check.h"

#include "collision2d/Surface.h"
#include "collision2d/CollisionMesh.h"

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <new>
#include <type_traits>

namespace {
int         g_dtorCount = 0;
const void* g_dtorOrder[8] = {};
void NoteDtor(const void* self) {
    if (g_dtorCount < 8) g_dtorOrder[g_dtorCount] = self;
    ++g_dtorCount;
}
}  // namespace

namespace Collision2D {
int32_t Surface::GetFileVersion() const { return 1; }
bool Surface::Serialize(IO::Stream&) { return true; }
Object::External* Surface::Reconstruct() { return this; }
void Surface::Destruct() {}
Object::External* Surface::Construct() { return this; }
Surface::~Surface() { NoteDtor(this); }

int32_t CollisionMesh::GetFileVersion() const { return 3; }
Object::External::CompileResult CollisionMesh::Compile(const System::Filename&) { return 1; }
bool CollisionMesh::Serialize(IO::Stream&) { return true; }
Object::External* CollisionMesh::Reconstruct() { return this; }
void CollisionMesh::Destruct() {}
Object::External* CollisionMesh::Construct() { return this; }
CollisionMesh::~CollisionMesh() { NoteDtor(this); }
}  // namespace Collision2D

namespace {

const void* VTableOf(const void* obj) { return *reinterpret_cast<const void* const*>(obj); }

// Retail flags 1 is `delete p` (T's D0: the chain once, then the class-scope
// MemFree operator delete; InternalMemFree == free here). The free is proven on
// a Recording<T> stand-in. The retired vector arm (flags 3: elements behind an
// 8-byte cookie, destroyed through element 0's slot 0) becomes `delete[]` of a
// `new T[3]`: three destructors, downward.
template <class T>
void CheckBothArms() {
    constexpr int kSize = (int)sizeof(T);   // the row's element size is T's header static_assert
    static_assert(dtor_check::HasClassDelete<T>::value, "D0 frees through a class-scope operator delete");
    void* m = Object::detail::InternalMemAlloc(kSize, 8);
    CHECK(m != nullptr);
    T* p = ::new (m) T();
    g_dtorCount = 0;
    delete p;
    CHECK_EQ(g_dtorCount, 1);

    T* r = dtor_check::NewOnMalloc<dtor_check::Recording<T>>();
    g_dtorCount = 0; dtor_check::ResetFrees();
    delete r;
    CHECK_EQ(g_dtorCount, 1);
    CHECK_EQ(dtor_check::g_frees, 1);
    CHECK(dtor_check::g_lastFree == static_cast<const void*>(r));

    CHECK_ARRAY_DELETE(T, g_dtorCount, g_dtorOrder);
}

}  // namespace

UNIT_TEST(b39_eng_collision2d_classes_are_concrete) {
    CHECK((std::is_base_of<Object::External, Collision2D::Surface>::value));
    CHECK((std::is_base_of<Object::External, Collision2D::CollisionMesh>::value));
    CHECK(!std::is_abstract<Collision2D::Surface>::value);
    CHECK(!std::is_abstract<Collision2D::CollisionMesh>::value);
    // B3X W1: public virtual destructors (retail slot 0 = D1 + D0).
    CHECK(std::is_destructible<Collision2D::Surface>::value);
    CHECK(std::is_destructible<Collision2D::CollisionMesh>::value);
    CHECK(std::has_virtual_destructor<Collision2D::Surface>::value);
    CHECK(std::has_virtual_destructor<Collision2D::CollisionMesh>::value);
    // sizeof 0x30 / 0x70 are Surface.h's / CollisionMesh.h's static_asserts.
}

UNIT_TEST(b39_eng_surface_slot_order_by_pmf) {
    using S = Collision2D::Surface;
    CHECK_DTOR_SLOT(S);   // 0x180064ea0
    CHECK_SLOT(S, TypeName,       0x08/8);   // 0x180064a70
    CHECK_SLOT(S, Type,           0x10/8);   // 0x180064a50
    CHECK_SLOT(S, GetFileVersion, 0x18/8);   // 0x18010ca30
    CHECK_SLOT(S, Compile,        0x20/8);   // External's 0x1800066e0
    CHECK_SLOT(S, Serialize,      0x28/8);   // 0x18010ca40
    CHECK_SLOT(S, Reconstruct,    0x30/8);   // 0x180064c70
    CHECK_SLOT(S, Destruct,       0x38/8);   // 0x180064d40
    CHECK_SLOT(S, Construct,      0x40/8);   // 0x180064ce0
    // slot 4 is NOT overridden: it is Object::External's own Compile.
    CHECK((std::is_same<decltype(&S::Compile),
                        Object::External::CompileResult (Object::External::*)(const System::Filename&)>::value));
}

UNIT_TEST(b39_eng_collisionmesh_slot_order_by_pmf) {
    using C = Collision2D::CollisionMesh;
    CHECK_DTOR_SLOT(C);   // 0x1800656b0
    CHECK_SLOT(C, TypeName,       0x08/8);   // 0x180064f70
    CHECK_SLOT(C, Type,           0x10/8);   // 0x180064f50
    CHECK_SLOT(C, GetFileVersion, 0x18/8);   // 0x180108cc0
    CHECK_SLOT(C, Compile,        0x20/8);   // 0x180108cb0
    CHECK_SLOT(C, Serialize,      0x28/8);   // 0x180108cd0
    CHECK_SLOT(C, Reconstruct,    0x30/8);   // 0x1800651b0
    CHECK_SLOT(C, Destruct,       0x38/8);   // 0x1800652c0
    CHECK_SLOT(C, Construct,      0x40/8);   // 0x180065240
    CHECK((std::is_same<decltype(&C::Compile),
                        Object::External::CompileResult (C::*)(const System::Filename&)>::value));
}

// Surface's factory stores (0x180064bc3..bf4): qword +0x20, dword +0x28;
// +0x2c is never written.
UNIT_TEST(b39_eng_surface_ctor_makes_retails_stores) {
    alignas(8) uint8_t buf[0x30];
    std::memset(buf, 0xCD, sizeof buf);
    Collision2D::Surface* s = ::new (buf) Collision2D::Surface();
    CHECK(static_cast<void*>(s) == static_cast<void*>(buf));
    for (int off = 0x08; off < 0x2c; ++off) CHECK_EQ((int)buf[off], 0);
    for (int off = 0x2c; off < 0x30; ++off) CHECK_EQ((int)buf[off], 0xCD);
    alignas(8) uint8_t buf2[0x30];
    Collision2D::Surface* s2 = ::new (buf2) Collision2D::Surface();
    CHECK(VTableOf(s) != nullptr);
    CHECK(VTableOf(s) == VTableOf(s2));
    g_dtorCount = 0;
    s->~Surface();                          // retail slot 0, flags 0
    s2->~Surface();
    CHECK_EQ(g_dtorCount, 2);
}

// CollisionMesh's factory stores (0x1800650c3..114): ten zero qwords
// +0x20..+0x68 after External's three.
UNIT_TEST(b39_eng_collisionmesh_ctor_makes_retails_stores) {
    alignas(8) uint8_t buf[0x70];
    std::memset(buf, 0xCD, sizeof buf);
    Collision2D::CollisionMesh* c = ::new (buf) Collision2D::CollisionMesh();
    CHECK(static_cast<void*>(c) == static_cast<void*>(buf));
    for (int off = 0x08; off < 0x70; off += 8) {
        uint64_t q = 1;
        std::memcpy(&q, buf + off, sizeof q);
        CHECK_EQ(q, (uint64_t)0);
    }
    g_dtorCount = 0;
    c->~CollisionMesh();                    // retail slot 0, flags 0
    CHECK_EQ(g_dtorCount, 1);
}

// Retail slot-0 bodies whose vector arm is retired: Surface 0x180064ea0,
// CollisionMesh 0x1800656b0.
UNIT_TEST(b39_eng_surface_deletingdtor_frees_both_arms) {

    CheckBothArms<Collision2D::Surface>();
}
UNIT_TEST(b39_eng_collisionmesh_deletingdtor_frees_both_arms) {
    CheckBothArms<Collision2D::CollisionMesh>();
}
