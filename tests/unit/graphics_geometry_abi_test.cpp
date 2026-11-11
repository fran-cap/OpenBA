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
