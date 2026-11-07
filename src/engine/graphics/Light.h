#pragma once
// ============================================================================
// src/engine/graphics/Light.h — `Graphics::Light`, `AmbientLight`,
//                                  `SpotLight`   (ENDGAME B.3 step 5, block E)
// ============================================================================
//
// THIS HEADER IS AUTHORED, NOT PORTED. `B35_RULES.md` §13 item 3 lists
// `Graphics::Light` (0xC0), `AmbientLight` (0xd8) and `SpotLight` (0x168) as
// three of the layouts whose SIZE was read from retail this pass but whose
// FIELDS were not — there was no `struct` for any of them anywhere in the tree
// (`B35_FILEMAP.md` §3d: "no struct anywhere; only `SunLight.h` exists for the
// lights family"). Closing that gap is part of block E. Every field below is
// pinned by a named retail body, and the body is cited next to it.
//
// THE THREE SOURCES, all of them retail:
//
//  1. `src/engine/graphics/lights_vtable.cpp` — the byte-faithful, diffed
//     transcriptions of `??0AmbientLight` (0x1800fa940), `??1AmbientLight`
//     (0x180058f30), `??0SpotLight` (0x180106690), `??1SpotLight`
//     (0x180106270) and all ten Light-branch slot bodies, each carrying the
//     retail store list in its banner. That file is this block's, and the
//     offsets in its "Layout constants, all read from the bodies that use
//     them" block ARE the measurement; this header turns them into a type.
//  2. The MANGLED NAMES of the two `Set`-shaped exports, which fix the field
//     TYPES that a store width alone cannot:
//       ?Set@AmbientLight@Graphics@@QEAAXAEBVVector4@Math@@M@Z   0x1800fa900
//         -> (const Math::Vector4&, float)  == +0xc0 Vector4, +0xd0 float
//       ??0SpotLight@Graphics@@QEAA@MMMMMAEBVVector4@Math@@V?$ExternalPtr@
//         VImageBuffer@Video@@@Object@@@Z                        0x180106300
//         -> (float, float, float, float, float, const Math::Vector4&,
//             ExternalPtr<Video::ImageBuffer>)
//     Neither is ported; both are read here for their TYPE information only.
//  3. `src/engine/volume/Volume.h`'s `::Frustum` — six `Vector4`
//     planes at +0x00..+0x5f and a `bool isOrthographic` at +0x60, 0x64
//     bytes. That is what identifies SpotLight's +0xc0 block: `??0SpotLight`
//     zeroes exactly TWELVE qwords from +0xc0 to +0x118 (= the 0x60 bytes of
//     six Vector4 planes, to the byte) and then stores the BYTE 1 at +0x120
//     (= `isOrthographic = true`). A Frustum at +0xc0 ends at +0x123, and the
//     ctor's next store is +0x128. The fit is exact on both ends, and it is
//     corroborated independently by `?AddDrawItems@SpotLight@` handing
//     `this + 0xc0` to `??0Camera@Graphics@@QEAA@AEBVFrustum@Volume@@AEB
//     VTransform3@Math@@_N@Z` as its `const ::Frustum&` argument.
//
// `Graphics::Light` HAS NO EXPORTED MEMBER AT ALL — the export directory
// carries exactly one `Light@Graphics@@` symbol, the vtable
// `??_7Light@Graphics@@6B@` @ 0x180158d30. So `Light` is a pure interface
// layer: a vtable, two new slots, and not one byte of data.
//
// PORTABILITY: no <windows.h>, per PLAN.md §4.1 decision 1.
// ============================================================================

#include <cstdint>
#include <cstddef>

#include "Geometry.h"                       // Graphics::Geometry (the base)
#include "../volume/Volume.h"               // ::Frustum, Box
#include "../math/Math.h"                   // Math::Vector4
// ENDGAME B.3.5 block F: `SpotLight::GetSpotCamera` returns `Graphics::Camera`
// BY VALUE, and a by-value return needs the COMPLETE type. Block E recorded the
// declaration as a comment because `Camera` was still a POD; this is that debt
// paid. Camera.h does not include this header, so there is no cycle.
#include "Camera.h"                         // Graphics::Camera (by value)
#include "../object/Internal.h"             // B.3.9: InternalMemFree (the concrete slot 0s)

namespace Video { class ImageBuffer; class RenderBuffer; }   // B.4: the owned targets

namespace Graphics {

// ============================================================================
// Graphics::Light : Graphics::Geometry — 0xC0, ELEVEN slots, ABSTRACT,
// and it adds NO FIELDS.
//
// `??_7Light@Graphics@@6B@` @ 0x180158d30, read as `.rdata` qwords
// (B35_SLOTS.md):
//
//   [0] +0x00  0x180058dd0  (unexported) vector deleting destructor
//   [1] +0x08  _purecall    inherited-from Geometry   Update
//   [2] +0x10  _purecall    inherited-from Geometry   Draw   (slot-2 overload)
//   [3] +0x18  _purecall    inherited-from Geometry   AddDrawItems
//   [4] +0x20  0x1800a4020  Geometry::UpdateAttached         inherited
//   [5] +0x28  0x1800a43c0  Geometry::GetSubTransform        inherited
//   [6] +0x30  0x1800154a0  Geometry::GetInstanceTransform   inherited
//   [7] +0x38  0x180015590  Geometry::SetSharedInstanceData  inherited
//   [8] +0x40  0x1800a4420  Geometry::Clean                  inherited
//   [9] +0x48  _purecall    DrawShadowMap   <- NEW on this branch
//  [10] +0x50  _purecall    DrawLight       <- NEW on this branch
//
// *** ELEVEN SLOTS AND YET sizeof == sizeof(Geometry) == 0xC0. *** Both facts
// are measured: the size is the `MOV EDX,0xc0` in 0x180058dd0, and
// `SunLight.h`'s banner reached the same conclusion independently from the
// other end — `??0SunLight` stamps ??_7Light (0x180158d30) and then
// ??_7SunLight (0x180158e10) into the SAME slot back to back and its next
// store is at +0xC0. A `Light` with even one data member would move
// `AmbientLight`'s, `SpotLight`'s and `SunLight`'s first field and break all
// three at once, so this "adds nothing" is load-bearing, not cosmetic.
//
// -------------------------------------------------------------------------
// RULE B5-17 — THE TWO `Draw` OVERLOADS GET DISTINCT NAMES
// -------------------------------------------------------------------------
// Retail names BOTH slot 2 and slot 10 `Draw`:
//   slot  2  ?Draw@X@Graphics@@UEBAXAEAVContext@Video@@AEBVCamera@2@
//              V?$ExternalPtr@VMaterialInstance@Graphics@@@Object@@MHHH@Z
//   slot 10  ?Draw@X@Graphics@@UEBAXAEAVContext@Video@@AEAVRenderer@2@HHHH@Z
// They are EIGHT SLOTS APART, and MSVC's grouped-and-reversed overload
// ordering is exactly what ENDGAME §B.1.2 dodges by renaming: get the pair
// the wrong way round and the mistake is silent. Slot 2 keeps the retail
// spelling `Draw` because `Geometry` introduces it and six other classes
// override it; slot 10, which enters the hierarchy HERE, is spelled
// **`DrawLight`**. Both mangled names stay exactly as they are in every
// registration and every mint row — the rename is a C++ spelling, not a
// symbol change.
// ============================================================================
class Light : public Geometry {
public:
    // `??_7Light@Graphics@@6B@` is the only exported `Light@Graphics@@`
    // symbol: no ctor, no dtor, no member. Both light constructors stamp this
    // table and then immediately overwrite it with their own, which is the
    // MSVC base-ctor idiom and is why the stamp is "dead" in
    // lights_vtable.cpp's transcriptions.

    // [9] 0x48 — pure here; AmbientLight / SpotLight / SunLight all override.
    virtual void DrawShadowMap(Video::Context& context, Renderer& renderer,
                               int a, int b) const = 0;
    // [10] 0x50 — pure here; all three override. Retail name `Draw`; see B5-17.
    virtual void DrawLight(Video::Context& context, Renderer& renderer,
                           int a, int b, int c, int d) const = 0;

protected:
    ~Light();   // folded onto ~Geometry by the linker; see AmbientLight below
};

static_assert(sizeof(Light) == 0xC0,
              "Graphics::Light adds NO data members: MOV EDX,0xc0 in its own "
              "slot-0 vector deleting destructor 0x180058dd0, and SunLight.h's "
              "ctor reading agrees");
static_assert(sizeof(Light) == sizeof(Geometry),
              "if these ever differ, AmbientLight/SpotLight/SunLight's first "
              "field has moved and all three are wrong");
static_assert(alignof(Light) == 8, "qword aligned (the vptr)");

// ============================================================================
// Graphics::AmbientLight : Graphics::Light — 0xd8, eleven slots.
//
// `??_7AmbientLight@Graphics@@6B@` @ 0x180158cd0:
//   [0] 0x180059080  (unexported) vector deleting dtor  (deleting_dtor.cpp row)
//   [1] 0x1800fa7a0  ?Update@…            — `RET`, empty in the shipped build
//   [2] 0x1800fa790  ?Draw@…MHHH@Z        — `RET`
//   [3] 0x1800fa890  ?AddDrawItems@…
//   [4..8] inherited from Light / Geometry
//   [9] 0x1800fa880  ?DrawShadowMap@…     — `RET`, an ambient light casts none
//  [10] 0x1800fa7b0  ?Draw@…HHHH@Z        — DrawLight
//
// FIELDS, from `??0AmbientLight` (0x1800fa940) and `?Set@AmbientLight@` :
//   +0xc0  Math::Vector4 _colour     ctor: two zeroed qwords; Set's first arg
//   +0xd0  float         _intensity  ctor: 0x41200000 == 10.0f; Set's second
//   +0xd4  four bytes the ctor NEVER writes — the tail pad that takes 0xd4 to
//          the 8-byte sizeof 0xd8. Recorded, not zeroed.
// The ctor also writes `Geometry::_materialTypeFlags` (+0x30) = 0x8000, which
// is the "kind" word both light constructors set; it is Geometry's field, not
// a new one.
//
// `??1AmbientLight@Graphics@@UEAA@XZ` (0x180058f30) IS A FIVE-INSTRUCTION
// THUNK that tail-jumps `??1Geometry@Graphics@@UEAA@XZ` (0x1800a41c0) — the
// linker folded the two because AmbientLight adds no destructible member. Its
// door is registered at the thunk address AND by name; see lights_vtable.cpp.
// ============================================================================
// ENDGAME B.3 step 9 (block GFX, B39_RULES D1): CONCRETE. The real ctor below
// is retail's ??0 body, its own slot 0 is the vector deleting dtor, ~AmbientLight
// is public and virtual. The ??0 / ??1 / slot doors, the minted
// `??_7AmbientLight@Graphics@@6B@` and its RT_DELDTOR row are retired
// (tools/rt_mint_retired/gfx.txt): nothing in retail or the tree builds one
// (no caller of ??0AmbientLight by any binding), so no route changes.
// ============================================================================
class AmbientLight : public Light {
public:
    AmbientLight(const AmbientLight& src);              // 0x180058f50  not ported
    AmbientLight& operator=(const AmbientLight& src);   // 0x180058fe0  not ported

    // ??0AmbientLight@Graphics@@QEAA@XZ 0x1800fa940 -- the REAL ctor. `CALL
    // ??0Geometry` and the Light / AmbientLight stamps are the compiler's; the
    // colour's two zeroed qwords are Math::Vector4's own zeroing ctor; then
    // +0xd0 = 10.0f and +0x30 = 0x8000. +0xd4 is written by nothing.
    AmbientLight() : Light() {
        _intensity         = 10.0f;     // 0x41200000
        _materialTypeFlags = static_cast<uint32_t>(MaterialTypeMask::Light);   // 0x8000
    }

    // [0] 0x180059080 -- MSVC's VECTOR deleting destructor, both arms exactly as
    // the retired RT_DELDTOR row ran them (`MOV EDX,0xd8` = the element size).
    // 0x180058f30, the thunk onto ~Geometry: an empty body (lights_vtable.cpp);
    // the compiler chains ~Light and ~Geometry. VIRTUAL (B3X W1).
    ~AmbientLight() override;
    void Update(const System::Time& time) override;                      // [1] 0x1800fa7a0
    void Draw(Video::Context& context, const Camera& camera,
              ExternalPtrHandle material,
              float opacity, int a, int b, int c) const override;         // [2] 0x1800fa790
    void AddDrawItems(const Scene& scene, Renderer& renderer) const override; // [3] 0x1800fa890
    void DrawShadowMap(Video::Context& context, Renderer& renderer,
                       int a, int b) const override;                       // [9] 0x1800fa880
    void DrawLight(Video::Context& context, Renderer& renderer,
                   int a, int b, int c, int d) const override;             // [10] 0x1800fa7b0

    // `?Set@AmbientLight@Graphics@@QEAAXAEBVVector4@Math@@M@Z` @ 0x1800fa900 —
    // exported, NOT PORTED, no caller. Declared for its type evidence and so
    // no caller batch has to edit this header.
    void Set(const Math::Vector4& colour, float intensity);

public:
    Math::Vector4 _colour;      // +0xc0  ctor 0,0,0,0
    float         _intensity;   // +0xd0  ctor 10.0f
    uint32_t      _padD4;       // +0xd4  NEVER WRITTEN by ctor, dtor or Set
};

static_assert(sizeof(AmbientLight) == 0xd8,
              "Graphics::AmbientLight is 0xd8 bytes (MOV EDX,0xd8 in 0x180059080)");
static_assert(alignof(AmbientLight) == 8, "qword aligned");
static_assert(offsetof(AmbientLight, _colour)    == 0xc0, "AmbientLight::_colour @ +0xc0 (??0 0x1800fa940)");
static_assert(offsetof(AmbientLight, _intensity) == 0xd0, "AmbientLight::_intensity @ +0xd0 (10.0f)");
static_assert(offsetof(AmbientLight, _padD4)     == 0xd4, "AmbientLight tail pad @ +0xd4");

// ============================================================================
// Graphics::SpotLight : Graphics::Light — 0x168, eleven slots.
//
// `??_7SpotLight@Graphics@@6B@` @ 0x180158db0:
//   [0] 0x1800642b0  (unexported) vector deleting dtor
//   [1] 0x180105950  ?Update@…            — `RET`
//   [2] 0x180105940  ?Draw@…MHHH@Z        — `RET`
//   [3] 0x180105e30  ?AddDrawItems@…      — queues TWO draw groups
//   [4..8] inherited
//   [9] 0x180105d00  ?DrawShadowMap@…
//  [10] 0x180105960  ?Draw@…HHHH@Z        — DrawLight
//
// FIELDS, from `??0SpotLight` (0x180106690), `??1SpotLight` (0x180106270),
// `?DrawShadowMap@` (0x180105d00), `?Draw@…HHHH@Z` (0x180105960) and
// `?AddDrawItems@` (0x180105e30) — all five transcribed in lights_vtable.cpp:
//
//   +0xc0  ::Frustum _frustum      the ctor zeroes the six Vector4
//                                        planes as twelve qwords (+0xc0..+0x118)
//                                        and then stores the BYTE 1 at +0x120,
//                                        which is `isOrthographic`. Handed to
//                                        ??0Camera@…@AEBVFrustum@Volume@@… as
//                                        `this + 0xc0`.
//   +0x124 uint32 pad                    Frustum's own tail pad (Volume.h:
//                                        "fields run through +0x60; natural
//                                        alignment 4 pads to 0x64"), so this is
//                                        +0x121..+0x123 rolled up to the next
//                                        4-byte field. NEVER written.
//   +0x128 float  _near                  ctor 0x3dcccccd == 0.1f
//   +0x12c float  _far                   ctor 0x41200000 == 10.0f
//   +0x130 float  _spotScalarA  \  the two floats `Context::SetLight` is handed
//   +0x134 float  _spotScalarB  /  as its `float, float` pair (slot-10 Draw).
//                                  *** NEITHER IS WRITTEN BY THE DEFAULT
//                                  CTOR *** — its store run jumps +0x12c to
//                                  +0x138. The seven-argument ctor at
//                                  0x180106300 is what fills them.
//   +0x138 Math::Vector4 _colour         ctor zeroes +0x138 and +0x140
//   +0x148 void* _shadowMap              Video::ImageBuffer*, OWNED (the dtor
//                                        destroys it through its own vtable
//                                        slot 0 with flags = 1)
//   +0x150 void* _shadowDepth            Video::ImageBuffer*, OWNED, same
//   +0x158 void* _shadowTarget           Video::RenderBuffer*, OWNED (the long
//                                        form: PlatformDestruct then operator
//                                        delete — RenderBuffer has no virtual
//                                        dtor). *** NOT ZEROED BY THE CTOR ***:
//                                        its store run skips +0x150 to +0x160
//                                        and `CreateTargets` (0x180106050)
//                                        fills it. Recorded, not zeroed.
//   +0x160 void* _boundImage             bound into Context+0x50 by slot-10
//                                        Draw; ctor zeroes it
//
// B.4: the three owned pointers carry their classes, forward-declared
// (`Video::ImageBuffer*` / `Video::RenderBuffer*`); `lights_vtable.cpp` reaches
// them through their own vtable slot 0 / their exported members.
// ============================================================================
// ENDGAME B.3 step 9 (block GFX, B39_RULES D1): CONCRETE, like AmbientLight
// above -- real ctor (retail's ??0 body), own vector slot 0, protected
// non-virtual ~SpotLight holding the retired ??1 door's body. No builder of a
// SpotLight exists in retail or the tree.
// ============================================================================
class SpotLight : public Light {
public:
    SpotLight(const SpotLight& src);                 // 0x180063e60  not ported
    SpotLight& operator=(const SpotLight& src);      // 0x180064080  not ported

    // ??0SpotLight@Graphics@@QEAA@XZ 0x180106690 -- the REAL ctor. `CALL
    // ??0Geometry` and the two stamps are the compiler's. The twelve zeroed
    // qwords +0xc0..+0x118 are the Frustum's six planes (Math::Vector4's zeroing
    // ctor); then isOrthographic = 1, the two literals, the colour (zeroing
    // ctor), +0x148 / +0x150 / +0x160 = 0 -- +0x130, +0x134 and +0x158 are NOT
    // written -- then `CreateTargets()` and only THEN +0x30 = 0x8000 (the kind
    // word is written AFTER the call; CreateTargets reads the object).
    SpotLight() : Light() {
        _frustum.isOrthographic = true;
        _near        = 0.1f;            // 0x3dcccccd
        _far         = 10.0f;           // 0x41200000
        _shadowMap   = nullptr;
        _shadowDepth = nullptr;
        _boundImage  = nullptr;
        CreateTargets();
        _materialTypeFlags = static_cast<uint32_t>(MaterialTypeMask::Light);   // 0x8000
    }

    // [0] 0x1800642b0 -- MSVC's VECTOR deleting destructor, both arms exactly as
    // the retired RT_DELDTOR row ran them (`MOV EDX,0x168` = the element size).
    // 0x180106270: the restamp is the compiler's; the three owned resources
    // (lights_vtable.cpp), then ~Light (empty) and ~Geometry -- the compiler's
    // chain, retail's tail jump. VIRTUAL (B3X W1).
    ~SpotLight() override;
    void Update(const System::Time& time) override;                      // [1] 0x180105950
    void Draw(Video::Context& context, const Camera& camera,
              ExternalPtrHandle material,
              float opacity, int a, int b, int c) const override;         // [2] 0x180105940
    void AddDrawItems(const Scene& scene, Renderer& renderer) const override; // [3] 0x180105e30
    void DrawShadowMap(Video::Context& context, Renderer& renderer,
                       int a, int b) const override;                       // [9] 0x180105d00
    void DrawLight(Video::Context& context, Renderer& renderer,
                   int a, int b, int c, int d) const override;             // [10] 0x180105960

    // `?GetSpotCamera@SpotLight@Graphics@@QEBA?AVCamera@2@AEAVRenderer@2@@Z`
    // @ 0x180105fb0 — a HIDDEN-RETSLOT export (B35_RULES §2.5: it returns
    // `Graphics::Camera`, 0xd8, BY VALUE). NOT PORTED anywhere in the tree and
    // no caller: DECLARED here, deliberately UNDEFINED.
    //
    // ENDGAME B.3.5 BLOCK F added this line. Block E left it as a comment
    // because `Graphics::Camera` was still a POD; the class exists now
    // (Camera.h), this header includes it, and the declaration is the real one.
    // NON-VIRTUAL, so the by-value return is correct C++ (the member is never
    // reached through a slot); whoever ports it writes a door with retail's
    // `(this, ret, renderer)` order, exactly as `sun_camera.cpp` does for
    // `SunLight::GetSunCamera`.
    Camera GetSpotCamera(Renderer& renderer) const;  // 0x180105fb0  not ported
    //
    // `?CreateTargets@SpotLight@Graphics@@AEAAXXZ` @ 0x180106050 is PRIVATE
    // (`AEAA`) and this DLL exports its private members
    // (reference_aeaa_is_private_not_unexported.md). It is PORTED, in
    // `src/engine/graphics/batchc_gfx_leaves.cpp` — NOT this block's file —
    // so its body stays where it is and its door keeps working.
    void CreateTargets();                            // 0x180106050  door in batchc_gfx_leaves.cpp

public:
    ::Frustum _frustum;        // +0xc0 .. +0x123
    uint32_t        _pad124;         // +0x124  Frustum tail pad; never written
    float           _near;           // +0x128  ctor 0.1f
    float           _far;            // +0x12c  ctor 10.0f
    float           _spotScalarA;    // +0x130  NOT written by the default ctor
    float           _spotScalarB;    // +0x134  NOT written by the default ctor
    Math::Vector4   _colour;         // +0x138  ctor 0,0,0,0
    // B.4: the three owned pointers carry their classes (pointers: no ctor /
    // dtor step of their own, the bytes and the ctor's stores are unchanged).
    Video::ImageBuffer*  _shadowMap;     // +0x148  owned
    Video::ImageBuffer*  _shadowDepth;   // +0x150  owned
    Video::RenderBuffer* _shadowTarget;  // +0x158  owned; NOT zeroed by the ctor
    Video::ImageBuffer*  _boundImage;    // +0x160  bound into Context image slot 4
};

static_assert(sizeof(SpotLight) == 0x168,
              "Graphics::SpotLight is 0x168 bytes (MOV EDX,0x168 in 0x1800642b0)");
static_assert(offsetof(SpotLight, _pad124) == 0x124, "SpotLight::_pad124");
static_assert(alignof(SpotLight) == 8, "qword aligned");
static_assert(offsetof(SpotLight, _frustum)      == 0xc0,  "SpotLight::_frustum @ +0xc0 (twelve zeroed qwords + isOrthographic at +0x120)");
static_assert(offsetof(SpotLight, _near)         == 0x128, "SpotLight::_near @ +0x128 (0.1f)");
static_assert(offsetof(SpotLight, _far)          == 0x12c, "SpotLight::_far @ +0x12c (10.0f)");
static_assert(offsetof(SpotLight, _spotScalarA)  == 0x130, "SpotLight::_spotScalarA @ +0x130 (SetLight arg 6)");
static_assert(offsetof(SpotLight, _spotScalarB)  == 0x134, "SpotLight::_spotScalarB @ +0x134 (SetLight arg 7)");
static_assert(offsetof(SpotLight, _colour)       == 0x138, "SpotLight::_colour @ +0x138");
static_assert(offsetof(SpotLight, _shadowMap)    == 0x148, "SpotLight::_shadowMap @ +0x148 (dtor slot-0 destroy)");
static_assert(offsetof(SpotLight, _shadowDepth)  == 0x150, "SpotLight::_shadowDepth @ +0x150 (dtor slot-0 destroy)");
static_assert(offsetof(SpotLight, _shadowTarget) == 0x158, "SpotLight::_shadowTarget @ +0x158 (dtor long form)");
static_assert(offsetof(SpotLight, _boundImage)   == 0x160, "SpotLight::_boundImage @ +0x160");

// The Frustum sits where the ctor's twelve zeroed qwords and its lone byte
// store land, and nowhere else can it sit: assert both ends.
static_assert(offsetof(SpotLight, _frustum) + offsetof(::Frustum, isOrthographic) == 0x120,
              "??0SpotLight stores the BYTE 1 at +0x120; that byte is "
              "::Frustum::isOrthographic");
static_assert(offsetof(SpotLight, _frustum) + sizeof(::Frustum) == 0x124,
              "a 0x64-byte Frustum at +0xc0 ends at +0x124, which is where the "
              "ctor's next store (+0x128) leaves room for exactly one pad dword");

}  // namespace Graphics
