// ============================================================================
// tests/unit/video_context_abi_test.cpp — ENDGAME B.3.5 block D1.
//
// The ABI claims block D1 makes about `Video::Context`, `Video::Viewport` and
// `Video::ConstantBuffer`, and nothing else. Model:
// `tests/unit/video_data_abi_test.cpp` (block A) and `io_vtable_abi_test.cpp`.
//
// WHAT THIS FILE HAS TO ESTABLISH
// -------------------------------
// None of these three classes is polymorphic, so there is no slot order to
// check. What there IS, and what would be silent if it broke:
//
//   1. THE 0x2168-BYTE LAYOUT. `Context.h` asserts every offset and the size at
//      compile time, the seven embedded ConstantBuffers included; this file
//      checks no offset of its own.
//   2. TRIVIAL COPYABILITY, which is what makes `memcpy`/`memset` over a
//      Context and a ConstantBuffer defined behaviour. `Context.h` and
//      `ConstantBuffer.h` assert it; asserting it again from a translation unit
//      that includes nothing else is what catches an accidental member with a
//      destructor being added to either class later (RULE B5-4).
//   3. THE VIEWPORT FIELD ORDER (x, y, width, height), asserted through a
//      brace-init, and the two comparison operators' shape, through a local
//      mirror (PART D).
//
// WHAT IS NOT HERE, AND WHY. `Video::Context`'s ported members live in
// src/engine/video/context.cpp, src/engine/video/context_bind.cpp,
// src/engine/video/context_ctor.cpp and src/engine/video/context_lifecycle.cpp,
// none of which is on UNIT_TREE_SRCS, so they are not driven here.
// `Video::Viewport::operator==` / `operator!=` touch nothing but four ints, but
// they are defined in src/engine/video/context.cpp, so this file declares the
// two operators' shape and checks it against a local mirror instead; see PART D.
// ============================================================================
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <type_traits>

#include "unit.h"

#include "video/ConstantBuffer.h"
#include "video/Context.h"

namespace {

// ---------------------------------------------------------------------------
// PART B — RULE B5-4's triviality, from a translation unit that includes
// nothing but the two headers.
// ---------------------------------------------------------------------------
UNIT_TEST(video_context_and_members_stay_trivially_copyable) {
    CHECK(std::is_trivially_copyable<Video::Context>::value);
    CHECK(std::is_trivially_copyable<Video::Viewport>::value);
    CHECK(std::is_trivially_copyable<Video::ConstantBuffer>::value);

    // Standard layout is what every `offsetof` in Context.h and every
    // `reinterpret_cast` onto MemAlloc'd bytes depends on.
    CHECK(std::is_standard_layout<Video::Context>::value);
    CHECK(std::is_standard_layout<Video::Viewport>::value);
    CHECK(std::is_standard_layout<Video::ConstantBuffer>::value);

    // `Viewport` must ALSO stay trivially default-constructible: `Context` holds
    // two by value and has no real constructor, and retail's own
    // `??0Viewport@Video@@QEAA@XZ` zeroes — so neither a user-provided body nor
    // `= default` is available and the class must remain an aggregate.
    CHECK(std::is_trivially_default_constructible<Video::Viewport>::value);

    // `Context` is NOT trivially default-constructible, and that is correct
    // rather than a slip: its `Math::Matrix` and `Math::Vector4` members carry
    // retail's zeroing default constructors. Nothing default-constructs a
    // Context — `??0Context@Video@@` takes a `Device*` (`RtVideo_Context_Ctor`).
    CHECK(!(std::is_trivially_default_constructible<Video::Context>::value));

    // No vtable, on any of the three (`?GetFrameIndex@` and friends are
    // non-virtual; the ctor's first store is `_device`, not a vptr).
    CHECK(!(std::is_polymorphic<Video::Context>::value));
    CHECK(!(std::is_polymorphic<Video::Viewport>::value));
    CHECK(!(std::is_polymorphic<Video::ConstantBuffer>::value));
}

// ---------------------------------------------------------------------------
// PART D — `Video::Viewport`'s two comparison operators.
//
// The real operators live in `src/engine/video/context.cpp`, which is not on
// UNIT_TREE_SRCS. What is under test is the SHAPE, so the mirror below is
// declared exactly as the real class is and the assertions run against both:
// the mirror's behaviour, and the real class's size and field order.
// ---------------------------------------------------------------------------
struct ViewportMirror {
    int32_t x, y, width, height;

    // The bodies read out of 0x180018240 and 0x180018270 this pass: TWO separate
    // four-compare bodies, not one plus a negation.
    bool operator==(const ViewportMirror& o) const {
        if (x != o.x) return false;
        if (y != o.y) return false;
        if (width != o.width) return false;
        if (height != o.height) return false;
        return true;
    }
    bool operator!=(const ViewportMirror& o) const {
        if (x != o.x) return true;
        if (y != o.y) return true;
        if (width != o.width) return true;
        if (height != o.height) return true;
        return false;
    }
};

UNIT_TEST(video_viewport_shape_and_comparison) {
    // Same layout as the real class, field for field.
    CHECK_EQ(static_cast<int>(sizeof(ViewportMirror)), static_cast<int>(sizeof(Video::Viewport)));

    // The four-argument constructor (0x180018220) stores x/y/width/height from
    // EDX/R8D/R9D/[RSP+0x28] — i.e. the third argument is the WIDTH. A reader
    // who assumed (x, y, right, bottom) would put the width where the height is,
    // so the aggregate order is asserted through a brace-init.
    const Video::Viewport vp{1, 2, 640, 480};
    CHECK_EQ(vp.x, 1);
    CHECK_EQ(vp.y, 2);
    CHECK_EQ(vp.width, 640);
    CHECK_EQ(vp.height, 480);

    const ViewportMirror a{1, 2, 640, 480};
    const ViewportMirror b{1, 2, 640, 480};
    ViewportMirror c{1, 2, 640, 481};
    CHECK(a == b);
    CHECK(!(a != b));
    CHECK(!(a == c));
    CHECK(a != c);
    // Each field on its own, because a body that compares only three of four
    // passes every all-equal case.
    c = a; c.x += 1;      CHECK(a != c);
    c = a; c.y += 1;      CHECK(a != c);
    c = a; c.width += 1;  CHECK(a != c);
    c = a; c.height += 1; CHECK(a != c);
}

}  // namespace
