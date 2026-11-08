// ============================================================================
// tests/unit/graphics_geometry_abi_test.cpp — ENDGAME B.3 step 5, block E
//   the `Graphics::Geometry` tree as real C++ classes
//
//   Graphics::Geometry        ROOT, abstract, 0xC0, 9 slots   (Geometry.h)
//   Graphics::MeshInstance    : Geometry, 0x120, 9 slots      (Mesh.h)
//   Graphics::EdgeMeshInstance: Geometry, 0xd0,  9 slots      (Mesh.h)
//   Graphics::ParticleInstance: Geometry, abstract, 0xd8, 10  (ParticleInstance.h)
//   Graphics::ParticleSystemInstance : ParticleInstance, 0xe0, 10
//   Graphics::Light           : Geometry, abstract, 0xC0, 11  (Light.h)
//   Graphics::AmbientLight    : Light, 0xd8, 11               (Light.h)
//   Graphics::SpotLight       : Light, 0x168, 11              (Light.h)
//   Graphics::SunLight        : Light, 0x108, 11              (SunLight.h)
//
// What is here is what the headers do NOT static_assert: the relations between
// the layouts (derived fields begin at sizeof(base), the Frustum embedded in
// SpotLight), the retail-order hidden return slot on slots 5 and 6 pinned as a
// TYPE, and the class shape (virtual destructor, abstract exactly where retail
// is abstract, a real copy constructor). Sizes and offsets are the headers'
// own static_asserts; the slot ORDER of the real classes is pinned by
// pointer-to-member in rb_graphics_abi_test.cpp.
//
// The local probe hierarchy that used to mirror the slot list is gone: a probe
// declared in the test can only ever test itself, and the retail-vs-GCC
// argument order of a by-value virtual return (GCC passes the hidden slot
// BEFORE `this`) is recorded in reference_gcc_msabi_retslot_before_this.md.
// ============================================================================
#include "unit.h"

// The shipped headers. The tests measure THESE, not local copies, so a failure
// means the header and the ABI disagree.
#include "graphics/Geometry.h"
#include "graphics/Mesh.h"
#include "graphics/ParticleInstance.h"
#include "graphics/Light.h"
#include "graphics/SunLight.h"

#include <cstdint>
#include <cstddef>
#include <type_traits>
#include <utility>

namespace {

// ===========================================================================
// PART A — LAYOUT relations the headers leave unsaid
// ===========================================================================
UNIT_TEST(graphics_geometry_sizes_match_retail) {
    // Each number is the `MOV EDX,imm` immediate in that class's OWN slot-0
    // vector deleting destructor, read as disassembly by block 0 (B35_RULES
    // §2.2). They are not derived from each other.

    // Every one is qword aligned: the vptr is the first member and nothing in
    // the tree has a wider alignment requirement.
    CHECK_EQ(alignof(Graphics::Geometry),               (size_t)8);
    CHECK_EQ(alignof(Graphics::MeshInstance),           (size_t)8);
    CHECK_EQ(alignof(Graphics::EdgeMeshInstance),       (size_t)8);
    CHECK_EQ(alignof(Graphics::ParticleInstance),       (size_t)8);
    CHECK_EQ(alignof(Graphics::ParticleSystemInstance), (size_t)8);
    CHECK_EQ(alignof(Graphics::Light),                  (size_t)8);
    CHECK_EQ(alignof(Graphics::AmbientLight),           (size_t)8);
    CHECK_EQ(alignof(Graphics::SpotLight),              (size_t)8);
    CHECK_EQ(alignof(Graphics::SunLight),               (size_t)8);
}

UNIT_TEST(graphics_derived_fields_begin_at_end_of_base) {
    // The whole tree rests on single inheritance with the base subobject at
    // offset zero and NO tail-padding reuse: retail's constructors all call the
    // base constructor and then write their own first field at exactly
    // `sizeof(base)` with no gap.
    // and the one that is two levels deep
    CHECK_EQ(offsetof(Graphics::ParticleSystemInstance, _emitter),
             sizeof(Graphics::ParticleInstance));
}

UNIT_TEST(graphics_geometry_field_offsets) {
    // Each offset is pinned by the named retail body in Geometry.h's banner.
    CHECK_EQ(offsetof(Graphics::Geometry, _transform)
             + offsetof(Graphics::InterpolatedState<Math::Transform>, _previous), (size_t)0xa0);

    // The class and the POD must describe the same bytes: the POD still has
    // live users outside block E's file set, so a drift between the two is the
    // failure this pins.
}

UNIT_TEST(graphics_light_field_offsets_authored_from_retail) {
    // These three layouts did not exist anywhere in the tree before block E
    // (B35_RULES §13 item 3: the SIZES were measured, the FIELDS were not), so
    // this is the only test that pins them.

    // AmbientLight: `??0AmbientLight` zeroes two qwords at +0xc0/+0xc8 (the
    // Vector4) and stores 0x41200000 == 10.0f at +0xd0; `?Set@AmbientLight@`'s
    // mangled name is `(const Math::Vector4&, float)`, which fixes the types.

    // SpotLight: the ctor zeroes exactly TWELVE qwords from +0xc0 to +0x118 —
    // the 0x60 bytes of a Frustum's six Vector4 planes — and then stores the
    // BYTE 1 at +0x120, which is `Frustum::isOrthographic`. Both ends of that
    // fit are checked, because it is the whole of the identification.
    CHECK_EQ(sizeof(::Frustum),                       (size_t)0x64);
    CHECK_EQ(offsetof(Graphics::SpotLight, _frustum) + offsetof(::Frustum, isOrthographic),
             (size_t)0x120);

    // SunLight: the eleven scalars `?Set@SunLight@` copies, the +0xEC hole no
    // body writes, and the three owned pointers.
    CHECK_EQ(offsetof(Graphics::SunLight, _direction) + 8, (size_t)0xC8);
    CHECK_EQ(offsetof(Graphics::SunLight, _ambient) + 12, (size_t)0xE8);
}
// ===========================================================================
// PART B — the two RETSLOT slots (RULE B5-12 as amended). Slots 5 and 6
// return a class BY VALUE through a vtable slot. GCC's Microsoft-ABI lowering
// passes the hidden return pointer BEFORE `this`, MSVC (retail) after it, so
// the real declarations spell the out-parameter in RETAIL ORDER and return it.
// Pinned as a type, so a later block cannot "tidy" them back to a by-value
// return without this file failing to compile.
// ===========================================================================
static_assert(
    std::is_same<
        decltype(std::declval<const Graphics::Geometry&>().GetSubTransform(
                     (Math::Transform*)nullptr, 0)),
        Math::Transform*>::value,
    "Geometry::GetSubTransform must keep the explicit retail-order return slot");
static_assert(
    std::is_same<
        decltype(std::declval<const Graphics::Geometry&>().GetInstanceTransform(
                     (Math::Matrix*)nullptr, 0, 0.0f)),
        Math::Matrix*>::value,
    "Geometry::GetInstanceTransform must keep the explicit retail-order return slot");

// ===========================================================================
// PART C — the claims the conversion makes about the class shape itself.
// ===========================================================================

UNIT_TEST(graphics_geometry_tree_has_a_virtual_destructor) {
    // B3X W1 (the normal-virtual cutover) retired ENDGAME §B.1 decision 2's
    // one-slot deleting-destructor virtual: the tree now HAS a virtual destructor,
    // which GCC spends TWO slots on (D1 complete, D0 deleting), so every retail
    // index n >= 1 sits at n + 1 — the shift slot_check.h's RetailSlot translates.
    CHECK(std::has_virtual_destructor<Graphics::Geometry>::value);
    CHECK(std::has_virtual_destructor<Graphics::MeshInstance>::value);
    CHECK(std::has_virtual_destructor<Graphics::EdgeMeshInstance>::value);
    CHECK(std::has_virtual_destructor<Graphics::ParticleInstance>::value);
    CHECK(std::has_virtual_destructor<Graphics::ParticleSystemInstance>::value);
    CHECK(std::has_virtual_destructor<Graphics::Light>::value);
    CHECK(std::has_virtual_destructor<Graphics::AmbientLight>::value);
    CHECK(std::has_virtual_destructor<Graphics::SpotLight>::value);
    CHECK(std::has_virtual_destructor<Graphics::SunLight>::value);
}

UNIT_TEST(graphics_geometry_tree_is_abstract_and_polymorphic) {
    // B34_BRIEFS §A.4, amended: abstract where nothing holds the class BY VALUE
    // as a C++ member. Nothing in the tree does — the four by-value mirrors of
    // B35_RULES §3 hold the `RtGraphics` PODs — and slot 0's body is a minted
    // row that §11 forbids retiring in step 5, so slot 0 stays `= 0` on every
    // one of the nine. If a later block makes one concrete it must also give
    // slot 0 a delegating body and a protected `= default` constructor, and this
    // test is where that decision has to be recorded.
    CHECK(std::is_abstract<Graphics::Geometry>::value);
    CHECK(!std::is_abstract<Graphics::MeshInstance>::value);   // concrete since B.3.8 RB (exe table copy 0x14015e240)
    CHECK(!std::is_abstract<Graphics::EdgeMeshInstance>::value);   // concrete since B.3.9 GFX (rb_graphics_abi_test b39_gfx_*)
    CHECK(std::is_abstract<Graphics::ParticleInstance>::value);
    CHECK(!std::is_abstract<Graphics::ParticleSystemInstance>::value);   // concrete since B.3.9 GFX (rb_graphics_abi_test b39_gfx_*)
    CHECK(std::is_abstract<Graphics::Light>::value);
    CHECK(!std::is_abstract<Graphics::AmbientLight>::value);   // concrete since B.3.9 GFX (rb_graphics_abi_test b39_gfx_*)
    CHECK(!std::is_abstract<Graphics::SpotLight>::value);      // concrete since B.3.9 GFX
    CHECK(!std::is_abstract<Graphics::SunLight>::value);      // concrete since B.3.8 RB (exe table copy 0x14015e290)

    CHECK(std::is_polymorphic<Graphics::Geometry>::value);
    CHECK(std::is_polymorphic<Graphics::SunLight>::value);

    // RULE B5-10: `Geometry`'s copy constructor is a REAL, REGISTERED, EXPORTED
    // body (0x180015b60), never `= default`. A `= default` would make the class
    // trivially copyable, which is exactly the bitwise copy of the nested
    // `Array<Geometry*>` heap pointer the rule forbids — so the negative here is
    // the test.
    CHECK(!std::is_trivially_copyable<Graphics::Geometry>::value);
    CHECK(!std::is_trivially_copy_constructible<Graphics::Geometry>::value);
}

}  // namespace
