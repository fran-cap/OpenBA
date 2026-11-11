// ============================================================================
// tests/unit/video_program_abi_test.cpp — ENDGAME B.3.5 block B.
//
// `Video::ShaderProgram` / `VertexProgram` / `PixelProgram` / `VertexLayout` /
// `VideoProgram`: the three PARENTS pinned as pointer conversions (the trap
// this block exists to avoid — `CompiledExternal` adds no bytes over
// `External`, so only a type test can see a wrong parent), the layout
// relations the headers leave unsaid, `ShaderProgram::Parameter`'s array spec,
// and which of the five is constructible. Sizes and offsets are the headers'
// own static_asserts; the mirror classes that used to restate the slot order
// and the qualified-call rule are gone (a mirror can only test itself; the
// real slot order is pinned by pointer-to-member in the b39_* tests).
// ============================================================================
#include <cstdint>
#include <cstddef>
#include <type_traits>

#include "unit.h"

#include "video/ShaderProgram.h"
#include "io/Stream.h"   // IO::MemoryStream: Serialize takes IO::Stream&
#include "video/VertexLayout.h"
#include "video/VideoProgram.h"

// ===========================================================================
// PART A — THE THREE PARENTS. The trap this block exists to avoid.
//
// A pointer conversion to a base is a compile-time fact about the class's
// inheritance, so `std::is_convertible` IS the test: it cannot pass for a class
// that was given the wrong parent. `Object::CompiledExternal` adds no BYTES over
// `Object::External` (both 0x20), which is exactly why the mistake would be
// invisible to every size and offset assertion in this file — the only thing it
// changes is which vtable a constructor stamps, and B35_RULES 2.1 says retail's
// `??0VideoProgram` stamps three tables where `??0ShaderProgram` stamps four.
// ===========================================================================
UNIT_TEST(video_program_parents) {
    // ShaderProgram and VertexLayout ARE CompiledExternals.
    CHECK((std::is_convertible<Video::ShaderProgram*, Object::CompiledExternal*>::value));
    CHECK((std::is_convertible<Video::VertexLayout*,  Object::CompiledExternal*>::value));

    // VertexProgram and PixelProgram are ShaderPrograms — and so, transitively,
    // CompiledExternals. They are NOT each other.
    CHECK((std::is_convertible<Video::VertexProgram*, Video::ShaderProgram*>::value));
    CHECK((std::is_convertible<Video::PixelProgram*,  Video::ShaderProgram*>::value));
    CHECK(!(std::is_convertible<Video::VertexProgram*, Video::PixelProgram*>::value));
    CHECK(!(std::is_convertible<Video::PixelProgram*,  Video::VertexProgram*>::value));

    // *** VideoProgram IS AN External AND IS NOT A CompiledExternal. ***
    CHECK((std::is_convertible<Video::VideoProgram*, Object::External*>::value));
    CHECK(!(std::is_convertible<Video::VideoProgram*, Object::CompiledExternal*>::value));
    CHECK(!(std::is_convertible<Video::VideoProgram*, Video::ShaderProgram*>::value));

    // Every one of the five reaches Object::Base, which is what makes the minted
    // `??_7` tables interchangeable with GCC's slot for slot.
    CHECK((std::is_convertible<Video::ShaderProgram*, Object::Base*>::value));
    CHECK((std::is_convertible<Video::VideoProgram*,  Object::Base*>::value));

    // The base subobject is at offset 0 in all of them — single inheritance, no
    // virtual bases — which is what licenses the `reinterpret_cast` bridges every
    // door in this family uses.
    CHECK_EQ((size_t)reinterpret_cast<const char*>(
                 static_cast<const Object::External*>(
                     reinterpret_cast<const Video::VideoProgram*>(0x1000))) -
             (size_t)0x1000,
             (size_t)0);
    CHECK_EQ((size_t)reinterpret_cast<const char*>(
                 static_cast<const Video::ShaderProgram*>(
                     reinterpret_cast<const Video::VertexProgram*>(0x1000))) -
             (size_t)0x1000,
             (size_t)0);
}

// ===========================================================================
// PART C — `ShaderProgram::Parameter` MUST STAY TRIVIAL, and this is the test
// that catches the double release.
//
// `Parameter` is held by value inside `Collection::Array<Parameter>`. Retail
// exports five special members for it (??0 x3, ??4, ??1) and block B declares
// NONE of them, because declaring a destructor or a copy would flip
// `rtcoll::SpecFor<Parameter>::destroy` from null to `&TDestroy<Parameter>` —
// and then the compiler-emitted `~Array<Parameter>` would release every
// parameter NAME a second time, after the registered array destructor
// (0x18004fb00, `ReleaseStringsCounted`) already did. The spec is checked here
// directly, not just the type traits, because the spec is what the array
// actually consults.
// ===========================================================================
UNIT_TEST(video_shader_parameter_trivial) {
    // B.4 wave Y: `name` is a System::String now; the members name retail's
    // row, whose destroy is the one String release (0x18004fed0).
    CHECK(!(std::is_trivially_destructible<Video::ShaderProgram::Parameter>::value));
    const rtcoll::ArraySpec* spec = decltype(Video::ShaderProgram::_textures)::Spec();
    CHECK(spec == &rtcoll::kSpec_ShaderParam);
    CHECK(spec->destroy != nullptr);
    CHECK_EQ(spec->stride, (uint32_t)0x10);

    // `VertexLayout::Element` is the same arrangement one class over (B.4 wave Y):
    // its `name` is a System::String too, and the member names the row.
    CHECK(!(std::is_trivially_destructible<Video::VertexLayout::Element>::value));
    CHECK(decltype(Video::VertexLayout::_elements)::Spec() == &rtcoll::kSpec_VertexElement);
    CHECK_EQ(decltype(Video::VertexLayout::_elements)::Spec()->stride, (uint32_t)0x20);
}

// ===========================================================================
// PART F — which of the five is constructible.
//
// Written when every live instance was built by a registered ctor DOOR that
// stamped a MINTED `??_7…@Video@@6B@`, and `protected = default` constructors
// plus a pure slot 0 kept a `Video::VertexProgram p;` from stamping GCC's table.
// ENDGAME B.3.9 VID made the four leaves concrete with retail's public
// constructors (the mint rows are retired); B3X W1 made the destructors public
// and virtual. So the abstract base alone is still not default-constructible.
// ===========================================================================
UNIT_TEST(video_program_only_the_abstract_base_is_not_default_constructible) {
    // B3X W1: `is_default_constructible` also asks for an accessible
    // destructor. Until the cutover every destructor here was protected, so
    // all five answered false; since ENDGAME B.3.9 VID the four concrete
    // classes have retail's PUBLIC constructor (0x1800f6100 VertexProgram,
    // 0x1800f3950 PixelProgram, VertexLayout, VideoProgram), and with the
    // public virtual destructor they are default-constructible. The abstract
    // base's constructor stays protected.
    CHECK(!(std::is_default_constructible<Video::ShaderProgram>::value));
    CHECK((std::is_default_constructible<Video::VertexProgram>::value));
    CHECK((std::is_default_constructible<Video::PixelProgram>::value));
    CHECK((std::is_default_constructible<Video::VertexLayout>::value));
    CHECK((std::is_default_constructible<Video::VideoProgram>::value));

    // Abstract, because slot 0 is `override = 0` — the `RT_DELDTOR` rows in
    // deleting_dtor.cpp are the bodies and B35_RULES 11 keeps them.
    CHECK((std::is_abstract<Video::ShaderProgram>::value));
    CHECK(!(std::is_abstract<Video::VertexProgram>::value));   // B.3.9 VID: concrete
    CHECK(!(std::is_abstract<Video::PixelProgram>::value));   // B.3.9 VID: concrete
    CHECK(!(std::is_abstract<Video::VertexLayout>::value));   // B.3.9 VID: concrete
    // ENDGAME B.3.9 VID: VideoProgram is CONCRETE (tests/unit/b39_vid_program_test.cpp).
    CHECK(!(std::is_abstract<Video::VideoProgram>::value));

    // B3X W1 retired ENDGAME B.1 decision 2: the whole family has a virtual
    // destructor (retail slot 0 = D1 + D0) and deletion is `delete p`.
    CHECK((std::has_virtual_destructor<Video::ShaderProgram>::value));
    CHECK((std::has_virtual_destructor<Video::VertexProgram>::value));
    CHECK((std::has_virtual_destructor<Video::PixelProgram>::value));
    CHECK((std::has_virtual_destructor<Video::VertexLayout>::value));
    CHECK((std::has_virtual_destructor<Video::VideoProgram>::value));
}
