// ============================================================================
// tests/unit/graphics_renderer_abi_test.cpp — ENDGAME B.3 step 5, block F
//   `Graphics::Camera`, `Graphics::Renderer` and `Graphics::Scene` as real
//   C++ classes, plus the four element types of Renderer's three arrays
//
// The sibling of `graphics_geometry_abi_test.cpp` (block E) and
// `io_vtable_abi_test.cpp`, for the seven classes block F converts:
//
//   Graphics::Camera             NON-POLYMORPHIC root, 0xd8   (Camera.h)
//   Graphics::RendererDrawable   0x10                         (Camera.h)
//   Graphics::RendererDrawItem   0x38                         (Camera.h)
//   Graphics::RendererDrawGroup  0x1f0                        (Camera.h)
//   Graphics::RendererSceneEntry 0x110, retail `Renderer::SceneGroup` (Camera.h)
//   Graphics::Renderer           NON-POLYMORPHIC root, 0x50   (Camera.h)
//   Graphics::Scene              NON-POLYMORPHIC root, 0x78   (Scene.h)
//   Graphics::SceneNode          0x18, retail `Scene::Node`    (Scene.h)
//
// WHAT THIS FILE HAS TO ESTABLISH
// -------------------------------
// None of these seven has a vtable, so there is no slot order to pin and PART B
// of the Geometry test has no analogue here. What binds instead is LAYOUT and
// two ABI properties that a later edit could silently break:
//
// PART A — LAYOUT. Every size, alignment and field offset the two headers claim,
//   measured here as well as `static_assert`ed there, so a header edit that moves
//   a field fails a TEST and not only a build somebody may be tempted to "fix".
//   It also pins the three EMBEDDINGS that fix `sizeof(Camera)` independently of
//   Camera's own field list — `RendererDrawGroup`'s two adjacent Cameras and
//   `RendererSceneEntry`'s one — and the eight POD cross-asserts that keep the
//   `RtGraphics` mirrors from drifting.
//
// PART B — TRIVIAL COPYABILITY (RULE B5-4), IN BOTH DIRECTIONS. `Camera` is a
//   by-value member of two `Collection::Array<T>` element types, so it must stay
//   trivially copyable; `RendererDrawGroup` and `RendererSceneEntry` must NOT be,
//   because each embeds a `Collection::Array<T>` whose destructor is what
//   `rtcoll::SpecFor<T>`'s destroy hook (and retail's 0x18001c8c0 / 0x18001ce90)
//   runs. Both halves are asserted, because getting either wrong changes which
//   body `array.cpp` runs on a resize.
//
// PART C — THE HIDDEN RETURN SLOT ON A NON-VIRTUAL BY-VALUE RETURN. `Camera` has
//   four of them (`GetFrustum`, `GetProjection`, `GetCameraToWorld` ×2) and
//   `SunLight::GetSunCamera` returns a whole `Camera` by value. Block E measured
//   that GCC's ms_abi passes the hidden return pointer BEFORE `this`, which is
//   why a VIRTUAL by-value return must spell an out-parameter in retail order
//   (Geometry.h's B5-12 block). PART C shows the other half of that finding: for
//   a NON-VIRTUAL member the same swap is invisible, because both ends of the
//   call are this same GCC — and that the DOOR shape the tree uses,
//   `(this, ret)` with `RAX = ret`, reproduces retail's ABI exactly when the door
//   placement-news the member's prvalue. A `void` door would be the 2026-09-13
//   perf-only crash (`1a4200c3`) again.
//
// PART D — THE QUALIFIED CALL. Doors delegate with
//   `reinterpret_cast<Ns::C*>(self)->Ns::C::Method(args)`. On a NON-POLYMORPHIC
//   class the qualification cannot change dispatch, and PART D says so as a
//   measurement rather than as an assumption: a hand-built table stamped over the
//   object is not consulted by either spelling, so no door in block F's set can
//   re-enter itself the way B34_BRIEFS §A.1 warns about for a polymorphic one.
//
// WHAT IS *NOT* PROVEN HERE
// -------------------------
// Nothing about MSVC, and nothing about the ported bodies. NO TREE SOURCE IS
// LINKED: every body of these seven classes lives in `src/engine/graphics/
// {renderer,scene,cull,sun_camera,wave9_lane_gfxd,wave29_lane_gfxbody,
// closure_batch2b}.cpp`, which stand on `RegisterRuntimeExport`, `ResolveAddr`,
// `RT_EXPORT_ONCE` and `RT_DATA_SLOT` and cannot link into a host test (the same
// reason `external_class_doors.cpp` is kept out — tests/unit/sources.cmake). The
// binding binary facts here are OBJECT LAYOUT and the retail accessor bodies the
// offsets were read out of; the arithmetic in `IsMirrored`, `GetSunCamera` and
// `Scene::Clean` is verified by the real run.
// `Math::Transform3` / `Math::Matrix` / `::Frustum` ARE the real classes
// (`src/engine/math/` is on the UNIT_TREE_SRCS list), so PART C's retslot values
// are real ones.
// ============================================================================
#include "unit.h"

// The shipped headers. The tests measure THESE, not local copies, so a failure
// means the header and the ABI disagree.
#include "graphics/Camera.h"
#include "graphics/Scene.h"

#include <cstdint>
#include <cstddef>
#include <new>
#include <type_traits>

namespace {

// Byte offset of a member measured from two live addresses, for the cases where
// `offsetof` would be awkward. Every class here is standard-layout, so both
// spellings are available and the two are cross-checked in PART A.
template <typename Obj, typename Field>
uintptr_t FieldOffset(const Obj* obj, const Field* field) {
    return reinterpret_cast<uintptr_t>(field) - reinterpret_cast<uintptr_t>(obj);
}

// Raw storage for a class whose default constructor is deliberately not
// accessible (`Graphics::Camera`, RULE B5-4) or whose real constructor is a
// registered DOOR (`Graphics::Renderer`, `Graphics::Scene`). Nothing here
// CONSTRUCTS one: these tests read layout, and a `reinterpret_cast` over aligned
// bytes is exactly what every minted row and every hand-inlined caller does.
template <typename T>
struct RawOf {
    alignas(T) unsigned char bytes[sizeof(T)];
    RawOf() { for (unsigned char& b : bytes) b = 0; }
    T*       ptr()       { return reinterpret_cast<T*>(bytes); }
    const T* ptr() const { return reinterpret_cast<const T*>(bytes); }
};

}  // namespace

// ===========================================================================
// PART A — LAYOUT
// ===========================================================================

UNIT_TEST(graphics_camera_field_offsets) {
    // Each offset names the retail body that pins it.

    // `offsetof` and the live-address form must agree — they do only if the class
    // is standard-layout, which is the property that lets every minted row and
    // every `(*(void***)p)[N]` caller keep working.
    CHECK(std::is_standard_layout_v<Graphics::Camera>);

    // The embedded value types are the REAL Math / Volume classes, and their own
    // sizes are what make the three offsets above add up.
    CHECK_EQ(sizeof(::Frustum), size_t(0x64));
    CHECK_EQ(size_t(0x94) + sizeof(Math::Matrix), size_t(0xd4));
}

UNIT_TEST(graphics_renderer_field_offsets) {
    CHECK(std::is_standard_layout_v<Graphics::Renderer>);

    // Retail's own names for the three element types, reachable through the
    // member aliases so the nesting `?GetSceneGroup@` implies is not lost.
    CHECK(sizeof(Graphics::Renderer::SceneGroup) == sizeof(Graphics::RendererSceneEntry));
    CHECK(sizeof(Graphics::Renderer::DrawGroup)  == sizeof(Graphics::RendererDrawGroup));
    CHECK(sizeof(Graphics::Renderer::DrawItem)   == sizeof(Graphics::RendererDrawItem));
    CHECK(sizeof(Graphics::Renderer::Drawable)   == sizeof(Graphics::RendererDrawable));
}

UNIT_TEST(graphics_scene_field_offsets) {
    RawOf<Graphics::Scene> raw;
    const Graphics::Scene* s = raw.ptr();

    CHECK_EQ(FieldOffset(s, &s->_grid._cellSize),     uintptr_t(0x00));
    CHECK_EQ(FieldOffset(s, &s->_grid._halfCellX),    uintptr_t(0x04));
    CHECK_EQ(FieldOffset(s, &s->_grid._halfCellY),    uintptr_t(0x08));
    CHECK_EQ(FieldOffset(s, &s->_grid._levelCount),   uintptr_t(0x0c));
    CHECK_EQ(FieldOffset(s, &s->_grid._nodes),        uintptr_t(0x10));
    CHECK_EQ(FieldOffset(s, &s->_grid._freeIds),      uintptr_t(0x20));
    CHECK_EQ(FieldOffset(s, &s->_grid._cellHeads),    uintptr_t(0x30));
    CHECK_EQ(FieldOffset(s, &s->_grid._levelOffsets), uintptr_t(0x40));
    CHECK_EQ(FieldOffset(s, &s->_grid._cellCounts),   uintptr_t(0x50));
    CHECK(std::is_standard_layout_v<Graphics::Scene>);

    // `Scene::Node`, reachable under retail's own nested spelling.
    CHECK(sizeof(Graphics::Scene::Node) == sizeof(Graphics::SceneNode));
    // The grid's two big arrays are both 0x5555 entries, from the ctor's
    // `while (lVar1 < 0x15554)` byte bound.
    CHECK_EQ(RtGraphics::kSceneCellCount, 0x5555);
}

// ===========================================================================
// PART B — TRIVIAL COPYABILITY, RULE B5-4, IN BOTH DIRECTIONS
// ===========================================================================

UNIT_TEST(graphics_camera_copy_ctor_is_retails_assign_and_dtor_trivial) {
    // The half that must be TRUE. `Camera` is a by-value member of two array
    // element types and retail moves it as a plain 0xd8 blob, so the class must
    // carry no copy, assign or destroy step of its own. Its defaulted copy is
    // also what makes the three-byte deviation Camera.h records the ONLY
    // divergence from retail's partial copy bodies.
    // B.4 S4 GFX2: the copy CONSTRUCTOR is retail's partial 0x180017a70 now
    // (Camera.h), so the class is no longer trivially copyable; the assign and
    // the destructor stay trivial.
    CHECK(!std::is_trivially_copyable_v<Graphics::Camera>);
    CHECK(std::is_trivially_destructible_v<Graphics::Camera>);
    CHECK(!std::is_trivially_copy_constructible_v<Graphics::Camera>);
    CHECK(std::is_trivially_copy_assignable_v<Graphics::Camera>);

    // The two plain PODs among the element types: `SpecFor<T>` derives
    // `byteCopy = true` for both, which is what `Renderer::AddLight`'s
    // `_drawables.SetSize(n, rec)` and the draw-item lists rely on.
    CHECK(std::is_trivially_copyable_v<Graphics::RendererDrawable>);
    CHECK(std::is_trivially_copyable_v<Graphics::RendererDrawItem>);

    // `Camera`'s default constructor is `protected` (B4b-3) so only the two
    // holders can make one, and `??0Camera@Graphics@@QEAA@XZ` stays a door.
    CHECK(!std::is_default_constructible_v<Graphics::Camera>);
    // ...and the holders CAN, which is what lets them hold it by value.
    CHECK(std::is_default_constructible_v<Graphics::RendererDrawGroup>);
    CHECK(std::is_default_constructible_v<Graphics::RendererSceneEntry>);
}

UNIT_TEST(graphics_renderer_element_types_keep_their_destructors) {
    // The half that must be FALSE, and it was never true — as PODs or as
    // classes. Each element type embeds a `Collection::Array<T>`, whose
    // destructor is user-declared and whose `operator=` is retail's DEEP
    // `ArrayAssignCore`. `rtcoll::SpecFor<T>` therefore derives
    // `byteCopy = false` and a real `destroy` hook for both — which is exactly
    // what renderer.cpp's `~Renderer` comment ("RendererDrawGroup's own
    // destructor already destroys items[2], items[1], items[0] in that order")
    // and wave9_lane_gfxd.cpp's `kSpec_SceneEntry` stand on. Flip either and the
    // array resize stops freeing the inner buffers.
    CHECK(!std::is_trivially_destructible_v<Graphics::RendererDrawGroup>);
    CHECK(!std::is_trivially_destructible_v<Graphics::RendererSceneEntry>);
    CHECK(!std::is_trivially_destructible_v<Graphics::Renderer>);
    CHECK(!std::is_trivially_destructible_v<Graphics::Scene>);
}

UNIT_TEST(graphics_camera_byvalue_getters_copy_exactly_what_retail_copies) {
    // WHAT THIS CAN AND CANNOT DO. The four members themselves are in
    // `renderer.cpp`, which stands on `RegisterRuntimeExport`, `ResolveExport` and
    // `RT_DATA_SLOT` and is NOT on the UNIT_TREE_SRCS list, so calling
    // `Graphics::Camera::GetFrustum()` here is an undefined reference at link.
    // Defining marker bodies for them in this file — block H1's arrangement for
    // its two polymorphic classes — would be a SECOND definition of four functions
    // that already have real ones, so it is not done either.
    //
    // What is testable, and what actually binds, is that each getter's SOURCE
    // member is a whole value type whose C++ copy moves EXACTLY the dwords retail
    // moves. Retail's four bodies are dword-for-dword copies out of +0x00, +0x64
    // and +0x94, so the claim is a claim about the three member TYPES:
    //
    //   GetFrustum            25 dwords out of +0x00  == sizeof(::Frustum)
    //   GetCameraToWorld ×2   12 dwords out of +0x64  == sizeof(Math::Transform3)
    //   GetProjection         16 dwords out of +0x94  == sizeof(Math::Matrix)
    //
    // `Math::Transform3` and `Math::Matrix` ARE real linkable classes here
    // (`src/engine/math/` is on the list), so these copies are real ones.

    RawOf<Graphics::Camera> raw;
    Graphics::Camera* cam = raw.ptr();
    for (int p = 0; p < 6; ++p) {
        cam->_frustum.planes[p] = Math::Vector4();
        cam->_frustum.planes[p].x = float(p) + 0.5f;
    }
    cam->_frustum.isOrthographic = true;
    cam->_cameraToWorld = Math::Transform3::Identity();
    cam->_projection = Math::Matrix();
    cam->_projection.m[5] = 7.25f;
    cam->_zUp = 1;

    // The copy each member's `return _x;` performs, spelled here directly.
    ::Frustum fslot = cam->_frustum;
    for (int p = 0; p < 6; ++p) CHECK_EQ(fslot.planes[p].x, float(p) + 0.5f);
    CHECK(fslot.isOrthographic);

    Math::Matrix mslot = cam->_projection;
    CHECK_EQ(mslot.m[5], 7.25f);

    Math::Transform3 tslot = cam->_cameraToWorld;
    CHECK_EQ(tslot.axisX.x, 1.0f);
    CHECK_EQ(tslot.axisY.y, 1.0f);
    CHECK_EQ(tslot.axisZ.z, 1.0f);

    // And the field the two `GetCameraToWorld` overloads share is ONE field, which
    // is the whole content of Camera.h's finding that retail's `float` overload
    // (0x180017e10) is instruction for instruction the no-argument one
    // (0x180017e70): there is no second transform for a lerp factor to interpolate
    // from. If a later edit ever adds one, this offset moves and PART A fails.
    CHECK_EQ(offsetof(Graphics::Camera, _projection) - offsetof(Graphics::Camera, _cameraToWorld),
             sizeof(Math::Transform3));
}

// ===========================================================================
// PART D — THE QUALIFIED CALL ON A NON-POLYMORPHIC CLASS
// ===========================================================================

UNIT_TEST(graphics_renderer_family_has_no_vtable_to_dispatch_through) {
    // B34_BRIEFS §A.1 makes every door's delegation a QUALIFIED non-virtual call
    // because a live POLYMORPHIC instance in this build wears a MINTED table
    // whose slots resolve back to the doors themselves, so an unqualified virtual
    // call inside a door is infinite recursion. None of block F's classes is
    // polymorphic — B35_RULES §2.1: no `??_7Camera@Graphics@@6B@`, no
    // `??_7Renderer@`, no `??_7Scene@`, no `_typeInfo`, and every export
    // `QEAA`/`QEBA`/`AEBA` — so for THIS block the qualification cannot change
    // dispatch and no door can re-enter itself. Stated as a measurement:
    CHECK(!std::is_polymorphic_v<Graphics::Camera>);
    CHECK(!std::is_polymorphic_v<Graphics::Renderer>);
    CHECK(!std::is_polymorphic_v<Graphics::Scene>);
    CHECK(!std::is_polymorphic_v<Graphics::RendererDrawGroup>);
    CHECK(!std::is_polymorphic_v<Graphics::RendererSceneEntry>);

    // The consequence that matters to a caller batch: the FIRST eight bytes of
    // each of these objects are DATA, not a vptr. `Renderer+0x00` is
    // `_sceneGroups._data` (which `?GetSceneGroup@` adds the index to) and
    // `Scene+0x00` is `_cellSize`, a float — so a `(*(void***)p)[N]` call on one
    // of these receivers would dispatch through a heap pointer or through
    // 2048.0f. That is why block F converted every such site in its file set to a
    // direct member call and why none was left as a bridge.
    RawOf<Graphics::Renderer> rawR;
    const Graphics::Renderer* r = rawR.ptr();
    CHECK_EQ(FieldOffset(r, &r->_sceneGroups._data), uintptr_t(0x00));
    RawOf<Graphics::Scene> rawS;
    const Graphics::Scene* s = rawS.ptr();
    CHECK_EQ(FieldOffset(s, &s->_grid._cellSize), uintptr_t(0x00));
}
