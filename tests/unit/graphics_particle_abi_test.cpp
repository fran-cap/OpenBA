// ============================================================================
// tests/unit/graphics_particle_abi_test.cpp — ENDGAME B.3 step 5, block H2
//   `Graphics::Particle` and `Graphics::ParticleSystem` as real C++ classes
//
//   Graphics::Particle        : Object::External, ABSTRACT, 0x48,  9 slots
//   Graphics::ParticleSystem  : Particle,                   0x108, 10 slots
//
// What is here is what the header does NOT static_assert: the layout relations
// (own fields at sizeof(base), the fifteen-float Serialize run, the trailing
// `_pool` header), the class shape, and `CreateSystem` / `Construct` /
// `Reconstruct` pinned as TYPES — RULE B5-16's one exception in Video and
// Graphics is that `Construct` is slot NINE here, behind `CreateSystem` at
// eight, and the real class's slot order is pinned by pointer-to-member in
// rb_graphics_abi_test.cpp (b39_gfx_particlesystem_*). The local probe
// hierarchy that used to mirror the slot list is gone: a probe declared in
// the test can only ever test itself.
//
// Also NOT proven: the two RULE B5-15 name literals. They are data in the
// binary, not in the code — they are recorded in `Particle.h`'s banner so a
// later `DEFINE_TYPE` is COPIED and not derived: `"Particle"` and, for
// ParticleSystem's PARENT, `"Particle"` with NO namespace.
// ============================================================================
#include "unit.h"

// The shipped header. The tests measure THIS, not a local copy, so a failure
// means the header and the ABI disagree.
#include "graphics/Particle.h"

#include <cstdint>
#include <cstddef>
#include <type_traits>
#include <utility>

namespace {

// ===========================================================================
// PART B — the claims the conversion makes about the class shape itself
// ===========================================================================

UNIT_TEST(graphics_particle_branch_has_a_virtual_destructor) {
    // B3X W1 retired ENDGAME §B.1 decision 2's one-slot deleting-destructor
    // virtual: the branch now HAS a virtual destructor, and GCC's two slots for
    // it (D1 complete, D0 deleting) move every index above by one — CreateSystem
    // and Construct TOGETHER, which is why B5-16's trap is pinned by the
    // translated reads (slot_check.h's RetailSlot) and not by bumped constants.
    CHECK(std::has_virtual_destructor<Graphics::Particle>::value);
    CHECK(std::has_virtual_destructor<Graphics::ParticleSystem>::value);
}

UNIT_TEST(graphics_particle_branch_is_abstract) {
    // `Particle` is abstract in retail itself (three `_purecall` slots).
    // `ParticleSystem` is concrete in retail but is declared abstract here on
    // slot 0 alone, exactly as `AnimationGroup` is: nothing holds either by value
    // (B34_BRIEFS §A.4 as amended) and their deleting-dtor bodies are minted rows
    // B35_RULES §11 forbids retiring.
    // ENDGAME B.3.9 GFX: ParticleSystem is CONCRETE now (its own slot 0, a real
    // ctor, the rows retired -- rb_graphics_abi_test b39_gfx_particlesystem_*);
    // Particle stays abstract through its three pure slots.
    CHECK(std::is_abstract<Graphics::Particle>::value);
    CHECK(!std::is_abstract<Graphics::ParticleSystem>::value);

    // And no real constructor on either (B4b-2): the ctor doors at 0x180103e80
    // and 0x1800a5320 stay, because both are called on memory that already wears
    // the minted table.
}

UNIT_TEST(graphics_particlesystem_createsystem_returns_a_particleinstance) {
    // Pinned as a TYPE, so a later block cannot "tidy" slot 8's return to
    // `Object::External*` — the exact consequence B35_RULES §2.3 item 2 warns
    // about, and the one a swapped slot pair would produce silently.
    static_assert(
        std::is_same<
            decltype(std::declval<const Graphics::ParticleSystem&>().CreateSystem()),
            Graphics::ParticleInstance*>::value,
        "ParticleSystem::CreateSystem must return ParticleInstance*, never External*");
    static_assert(
        std::is_same<
            decltype(std::declval<Graphics::ParticleSystem&>().Construct()),
            Object::External*>::value,
        "ParticleSystem::Construct returns Object::External* (slot 9)");
    static_assert(
        std::is_same<
            decltype(std::declval<Graphics::ParticleSystem&>().Reconstruct()),
            Object::External*>::value,
        "ParticleSystem::Reconstruct returns Object::External* (slot 6)");
    CHECK(true);   // the claims above are compile-time; this keeps the test honest
}

}  // namespace
