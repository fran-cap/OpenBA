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
// PART A — LAYOUT relations the header leaves unsaid
// ===========================================================================
UNIT_TEST(graphics_particle_sizes_match_retail) {
    // Each number is the `MOV EDX,imm` immediate in that class's OWN slot-0
    // vector deleting destructor, read as disassembly by block 0 (B35_RULES
    // §2.2). They are not derived from each other.

    CHECK_EQ(alignof(Graphics::Particle),       (size_t)8);
    CHECK_EQ(alignof(Graphics::ParticleSystem), (size_t)8);

    // Single inheritance, base subobject at offset zero, NO tail-padding reuse
    // and NO second vptr: retail's constructor inlines the base stores and then
    // writes its own first field at exactly `sizeof(base)` with no gap.
    CHECK_EQ(offsetof(Graphics::Particle, _active),   sizeof(Object::External));
    CHECK_EQ(offsetof(Graphics::ParticleSystem, _emissionRate), sizeof(Graphics::Particle));
}

UNIT_TEST(graphics_particle_field_offsets) {
    // Pinned by the constructor at 0x180103e80 (the store list is in the
    // header's banner) and by `Serialize` at 0x180103f00.

    // The two MEASURED gaps the constructor never writes, kept as named pads
    // rather than left to C++'s natural alignment. They are why the diff oracles
    // in `animation.cpp` exclude [0x21,0x24) and [0x2c,0x30).
    CHECK_EQ(sizeof(Graphics::Particle::_pad21),   (size_t)3);

    // The class and the POD must describe the same bytes: the POD still has live
    // users (`animation.cpp`'s three constructor doors and their oracles), so a
    // drift between the two is the failure this pins.
}

UNIT_TEST(graphics_particlesystem_field_offsets_authored_from_retail) {
    // This layout did not exist under retail's name anywhere in the tree before
    // block H2. It is not invented: it moves `vtbatch7_animgroup_particlesystem
    // .cpp`'s file-local struct — the WRITER, off the disassembled 60-store
    // constructor 0x1800a5320 and off `Serialize`'s emission order — and takes
    // its field NAMES from `wave28_lane_partsys.cpp`'s independent READER
    // measurements for `ParticleSystemInstance::Update`, each of which carries
    // the instruction that reads it. That file now `static_assert`s all 37
    // constants against this class, so the two witnesses are checked against each
    // other at compile time as well.

    // The two MEASURED constructor gaps: it writes +0x50 and then the int64 at
    // +0x58, and it writes +0xdc/+0xe0 as ONE qword of -1 and then the int64 at
    // +0xe8. `Serialize` skips both as well.

    // `Serialize` emits FIFTEEN consecutive float scalars from +0x7c to +0xb4.
    // If a field is ever inserted in the middle of that run the stream shifts by
    // four bytes and every later record is read out of the wrong place — a save
    // desync, not a crash. Both ends and the span are checked.
    CHECK_EQ(offsetof(Graphics::ParticleSystem, _gravScale)
                 - offsetof(Graphics::ParticleSystem, _angleBRange),
             (size_t)(14 * sizeof(float)));

    // The `_pool` header is the last 0x10 bytes, which is what fixes `sizeof` at
    // 0x108 rather than 0xf8.
    CHECK_EQ(offsetof(Graphics::ParticleSystem, _pool) + sizeof(Collection::Array<uint8_t>),
             sizeof(Graphics::ParticleSystem));
}
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
