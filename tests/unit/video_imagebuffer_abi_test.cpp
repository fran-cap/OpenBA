// ============================================================================
// tests/unit/video_imagebuffer_abi_test.cpp — ENDGAME B.3.5 block C.
//
// The layout relations the headers leave unsaid for `Video::ImageBuffer`
// (0x68, 10 slots), the chain across the namespace boundary
// (`Graphics::GeneratedImageBuffer` 0x70, `FrameCopyImageBuffer` 0x88,
// `ReflectionImageBuffer` 0x90) and `Video::RenderState` (0x80) with its three
// nested value types. Sizes and offsets are the headers' own static_asserts.
// The mirror hierarchy that used to restate the slot order is gone: a mirror
// declared in the test can only ever test itself; the real classes' slot
// order and both slot-0 arms are pinned in rb_graphics_abi_test.cpp and
// rb_video_abi_test.cpp.
// ============================================================================
#include <cstdint>
#include <cstddef>
#include <type_traits>

#include "unit.h"

#include "video/ImageBuffer.h"
#include "io/Stream.h"   // IO::MemoryStream: Serialize takes IO::Stream&
#include "video/RenderState.h"
#include "graphics/GeneratedImageBuffer.h"
#include "graphics/FrameCopyImageBuffer.h"

// ===========================================================================
// PART A — `Video::ImageBuffer`'s layout.
//
// A field accidentally added would move `_data` at +0x60, which `src/render/dx11/`
// reads; ImageBuffer.h's static_asserts pin the layout.
// ===========================================================================
UNIT_TEST(video_imagebuffer_layout) {
    CHECK_EQ(alignof(Video::ImageBuffer), (size_t)8);

    // The `Object::CompiledExternal` base adds no bytes over `External`'s 0x20,
    // which is what puts `_width` at +0x20 rather than anywhere else.
    CHECK_EQ(sizeof(Object::External), sizeof(Object::CompiledExternal));

    // `_data` is the real abstract base block A declared, not a `void*`.
    CHECK(  (std::is_same<decltype(Video::ImageBuffer::_data),
                          Video::ImageBufferData*>::value) );
}

// ===========================================================================
// PART D — the chain's three derived layouts.
//
// `GeneratedImageBuffer` adds exactly ONE field (the int latch at +0x68) plus the
// four bytes MSVC leaves before the 8-aligned tail of the two concretes, and the
// two concretes' owned pointers start at +0x70. Each size is its own deleting
// destructor's `MOV EDX,imm`; the headers' static_asserts pin them.
// ===========================================================================
UNIT_TEST(graphics_imagebuffer_chain_layout) {
    // The latch, inherited by both concretes at the SAME offset — which is why
    // `Graphics::ReflectionImageBuffer` has a field there doing the same job and
    // why it is `GeneratedImageBuffer`'s member, not each concrete's own.

    // The chain really is a chain in C++, not three siblings over one POD: the
    // namespace boundary between `Video::ImageBuffer` and
    // `Graphics::GeneratedImageBuffer` is the one B35_RULES section 4 calls out,
    // and single inheritance across it is what keeps `this` unadjusted.
    CHECK( (std::is_base_of<Video::ImageBuffer, Graphics::GeneratedImageBuffer>::value) );
    CHECK( (std::is_base_of<Graphics::GeneratedImageBuffer,
                            Graphics::FrameCopyImageBuffer>::value) );
    CHECK( (std::is_base_of<Graphics::GeneratedImageBuffer,
                            Graphics::ReflectionImageBuffer>::value) );
    // And `FrameCopy` is NOT a base of `Reflection` — they are siblings, which is
    // the shape `?_ParentTypeName@` gives both ("GeneratedImageBuffer").
    CHECK(!(std::is_base_of<Graphics::FrameCopyImageBuffer,
                            Graphics::ReflectionImageBuffer>::value) );
}

// ===========================================================================
// PART E — `Video::RenderState` and its three nested value types.
//
// The parent is `Object::External` DIRECTLY, and that is the claim worth a test
// rather than a comment: every other Video external in the tree goes through
// `Object::CompiledExternal`, which adds no bytes, so a wrong base would NOT
// change `sizeof` and NOT fail any offset assert — it would only change which
// vtable the object stamps and which literal `?_ParentTypeName@` returns, i.e. it
// would change the type-id CRC and stop saves loading. `is_base_of` is true for
// both, so the test is the NEGATIVE one.
// ===========================================================================
UNIT_TEST(video_renderstate_layout) {
    CHECK_EQ(alignof(Video::RenderState), (size_t)8);

    CHECK( (std::is_base_of<Object::External, Video::RenderState>::value) );
    // THE ONE THAT MATTERS: NOT through CompiledExternal.
    CHECK(!(std::is_base_of<Object::CompiledExternal, Video::RenderState>::value) );
    // ...while `Video::ImageBuffer` IS, which is what makes the two branches
    // different and the distinction worth encoding in the type system.
    CHECK( (std::is_base_of<Object::CompiledExternal, Video::ImageBuffer>::value) );

    // `_data` is block A's real abstract base, not a `void*`.
    CHECK(  (std::is_same<decltype(Video::RenderState::_data),
                          Video::RenderStateData*>::value) );

    // The three nested value types. `?Initialize@` copies each of them dword by
    // dword INCLUDING the padding, which the member expresses as a struct
    // assignment — sound only while all three stay trivially copyable, which their
    // inline default constructors (RenderState.h) do not break.
    CHECK(std::is_trivially_copyable<Video::RenderState::AlphaBlend>::value);
    CHECK(std::is_trivially_copyable<Video::RenderState::Depth>::value);
    CHECK(std::is_trivially_copyable<Video::RenderState::Stencil>::value);

    // `Stencil::reference` (+0x08) is serialized as a UChar among eight UInts
    // (0x1800f48e0). A UInt there would move the next eight fields by three
    // bytes in every saved file, so the one-byte width is pinned. B5 RES: the
    // Stencil names are the dx11 consumer's (RenderState.h's Stencil banner);
    // this byte was `writeMask` and +0x20 `reference` before.

    // B3X W1: `RenderState` and the `ImageBuffer` chain DID acquire a virtual
    // destructor (retail slot 0 = D1 + D0 under GCC), all five together; every
    // raw reader of these tables went by name first (B3X W0).
    CHECK(std::has_virtual_destructor<Video::RenderState>::value == true);
    CHECK(std::has_virtual_destructor<Video::ImageBuffer>::value == true);
    CHECK(std::has_virtual_destructor<Graphics::GeneratedImageBuffer>::value == true);
    CHECK(std::has_virtual_destructor<Graphics::FrameCopyImageBuffer>::value == true);
    CHECK(std::has_virtual_destructor<Graphics::ReflectionImageBuffer>::value == true);
}

// ===========================================================================
// PART F — the explicit padding in the three nested value types.
//
// NOT a behavioural test: the three exported default constructors (0x1800f4b30 /
// 0x1800f4a80 / 0x1800f49d0) are inline in `src/engine/video/RenderState.h` and
// are not run here. Their constants are verified by the real run (the
// `RenderState` ctor runs all three and the resave gate reads them back out of a
// save).
//
// What IS checkable here, and matters: retail writes a BYTE at `AlphaBlend+0x00`
// and never touches +0x01..+0x03, and a WORD at `Depth+0x00` and never +0x02.
// Those runs are DECLARED padding, so each struct's size comes from the
// declaration rather than from the compiler's choice of alignment — which is what
// stops `?Initialize@`'s struct assignment (a whole-extent copy, including the
// padding, exactly as retail's dword-by-dword copy does) from moving.
// `sizeof(Class::member)` needs no object.
// ===========================================================================
UNIT_TEST(video_renderstate_nested_padding_is_declared) {
    CHECK_EQ(sizeof(Video::RenderState::AlphaBlend::_pad01), (size_t)3);
    CHECK_EQ(sizeof(Video::RenderState::Depth::_pad02),      (size_t)2);
    CHECK_EQ(sizeof(Video::RenderState::Stencil::_pad02),    (size_t)2);
    CHECK_EQ(sizeof(Video::RenderState::Stencil::_pad09),    (size_t)3);
    // And `RenderState`'s own seven unwritten bytes after `_flag70`, which retail
    // leaves alone and which the class must therefore not initialise.
    CHECK_EQ(sizeof(Video::RenderState::_pad71), (size_t)7);
    // `GeneratedImageBuffer::_pad6c` is NEVER WRITTEN by its ctor or dtor; it is
    // the four bytes MSVC leaves before the 8-aligned tail of the two concretes.
    CHECK_EQ(sizeof(Graphics::GeneratedImageBuffer::_pad6c), (size_t)4);
}
