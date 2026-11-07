// ============================================================================
// src/engine/graphics/lights_vtable.cpp
// ??_7AmbientLight@Graphics@@6B@ (0x180158cd0), ??_7SpotLight@ (0x180158db0)
// and the two remaining ??_7SunLight@ slots.  Vtable batch 6.
// ============================================================================
//
//   AmbientLight  slot 1 Update 0x1800fa7a0   slot 2 Draw          0x1800fa790
//                 slot 3 AddDrawItems 0x1800fa890
//                 slot 9 DrawShadowMap 0x1800fa880
//                 slot10 Draw          0x1800fa7b0
//                 ??0AmbientLight 0x1800fa940, ??1AmbientLight 0x180058f30
//   SpotLight     slot 1 Update 0x180105950   slot 2 Draw          0x180105940
//                 slot 3 AddDrawItems 0x180105e30
//                 slot 9 DrawShadowMap 0x180105d00
//                 slot10 Draw          0x180105960
//                 ??0SpotLight 0x180106690, ??1SpotLight 0x180106270
//   SunLight      slot 9 DrawShadowMap 0x180106bc0
//                 slot10 Draw          0x1801067b0
//                 (ctor/dtor/Set already ported in sunlight.cpp)
//
//   slot 0 of AmbientLight and SpotLight are ROWS in
//   src/engine/object/deleting_dtor.cpp, not bodies here.
//
// ---------------------------------------------------------------------------
// FOUR THINGS THAT WOULD BE WRONG IF READ FROM THE DECOMPILER
// ---------------------------------------------------------------------------
// 1. `Renderer::DrawItemList` TAKES FIVE VALUES (this + four params), so its
//    last int travels on the STACK at [RSP+0x20]. Three of the five bodies
//    below reach it by a TAIL JMP and pass that int by WRITING THEIR OWN arg5
//    HOME SLOT first (`MOV [RSP+0xa0],EAX`), and the two DrawShadowMaps pass
//    their own arg5 THROUGH UNTOUCHED — the slot is already correct, so retail
//    writes nothing. Both idioms are invisible in C output and both are spelled
//    out as explicit arguments below.
// 2. `??1AmbientLight@Graphics@@UEAA@XZ` (0x180058f30) IS A FIVE-INSTRUCTION
//    THUNK that tail-jumps `??1Geometry@Graphics@@UEAA@XZ` (0x1800a41c0): the
//    linker folded the two, and 0x180059080's SCALAR arm calls ~Geometry
//    DIRECTLY while its VECTOR arm names the thunk. AmbientLight therefore has
//    no destructor logic of its own, and the body below is the thunk.
// 3. THE SHADOW-BIAS MATRICES ARE BUILT FROM IMMEDIATES, four `MOVAPS` rows at
//    a time out of overlapping 8- and 4-byte stores. SunLight has TWO of them,
//    chosen by a virtual query on `Video::Device::_singleton->[0x100]`;
//    SpotLight has only the first, unconditionally. Getting the sign of the
//    [1][1] term wrong flips every shadow vertically.
// 4. `Video::ImageBuffer::Width()` / `Height()` ARE INLINED at six sites, as a
//    three-way branch on the image's flags word that reads the Video::Device
//    singleton. `ImageDim` below is that inlining, spelled once.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include <cstring>

#include "math/Math.h"
// ENDGAME B.3.5 block E: the real `Graphics::Light` / `AmbientLight` /
// `SpotLight` / `SunLight`. `Light.h` is AUTHORED FROM THE BODIES IN THIS FILE —
// every offset in the "Layout constants" block below is one of its field pins,
// and the header cites this file as its first source.
#include "graphics/Light.h"
#include "graphics/SunLight.h"
// ENDGAME B.3.5 caller batch: the cross-class edges this file opens. Every one
// of them is a real class today, so the by-name doors below become member calls.
#include "graphics/Camera.h"    // Graphics::Renderer, Graphics::Camera
#include "graphics/Scene.h"     // Graphics::Scene
#include "video/Context.h"      // Video::Context
#include "video/Device.h"       // Video::RenderBuffer
#include "video/ImageBuffer.h"  // Video::ImageBuffer (B3X W0: slot 0 by name)
#include "video/CompanionData.h" // B.4: Video::DeviceData::Slot7

#include "collection/Collection.h"   // rtcoll:: the array instantiations

namespace {

// ---------------------------------------------------------------------------
// Layout, all read from the bodies that use them. B.4: every offset below is
// a named field now -- Light.h / SunLight.h for the lights, Camera.h for the
// Renderer and its draw groups, Context.h for the Context:
//
// Graphics::AmbientLight -- `_colour` (+0xc0, Vector4), `_intensity` (+0xd0).
// Graphics::SpotLight    -- `_frustum` (+0xc0), `_spotScalarA/B` (+0x130/4),
//   `_colour` (+0x138), `_shadowMap` / `_shadowDepth` (+0x148 / +0x150,
//   Video::ImageBuffer*, owned), `_shadowTarget` (+0x158, Video::RenderBuffer*,
//   owned), `_boundImage` (+0x160, bound into Context image slot 4).
// Graphics::SunLight     -- `_dirX..` (+0xc0), `_sunR..` (+0xcc), `_ambientR..`
//   (+0xdc), `_depthTarget` (+0xf8), `_renderBuffer` (+0x100).
// Video::Context         -- `_dirty` (+0x10), `_imageBufferDirty` (+0x18),
//   `_renderBuffer` (+0x28), `_imageBuffers[4]` / `[5]` (+0x50 / +0x58),
//   `_viewport` (+0x1d0), `_scissor` (+0x1e0), `_contextData` (+0x2160);
//   +0x1e78 is inside `_lightStaging` (see WriteShadowMatrix).
// Graphics::Renderer     -- `_drawGroups` (+0x10, stride 0x1f0, `IMUL
//   RDI,RDI,0x1f0`), `_shaderDetail` (+0x3c), `_detailId` (+0x48).
// ---------------------------------------------------------------------------

// The float immediates, as BIT PATTERNS. Read out of the image with
// read_memory, not converted from decimal:
//   0x18016169c = 0x3f800000 =  1.0f
//   0x1801616d8 = 0x40000000 =  2.0f
//   0x180161754 = 0xbf000000 = -0.5f
//   0x180161768 = 0xc0000000 = -2.0f
// Each is exactly representable, so the decimal literals below carry
// retail's bits.

// ---------------------------------------------------------------------------
// Named dependencies, called by name (B3R).
//
// `?CreateTargets@SpotLight@Graphics@@AEAAXXZ` 0x180106050 — PORTED
// (batchc_gfx_leaves.cpp), a member the real `SpotLight()` calls.
// `Video::RenderBuffer::PlatformDestruct` / `operator delete` — members.
// `?SetLight@Context@Video@@` 0x18001a950 — a member of the real Context.
// `?DrawItemList@Renderer@Graphics@@` 0x180105240 — FIVE VALUES; see the
//   banner. The fifth is the SHADER INDEX.
// `Math::Transform3::Inverse` / `AsMatrix`, `Math::Matrix::operator*` and
//   `Math::Transform::AsTransform3` — members of the real Math classes, called
//   on typed operands (B.4: the `(self, ret)` wrappers are gone).
// `??0Camera@Graphics@@QEAA@AEBVFrustum@Volume@@AEBVTransform3@Math@@_N@Z`
//   0x1800fe960 — the member constructor (renderer.cpp).
// `Renderer::AddLight` / `BeginDrawGroup` / `EndDrawGroup` and
//   `Scene::AddGlobalDrawItems` / `AddCulledDrawItems` — members.
// The two unexported Collection::Array<T>::SetSize instantiations
// AmbientLight::AddDrawItems drives (no export name in either shipped build),
// both ported and called by body name:
//   0x180023830  Array<int>::SetSize(n, const int&)          array.cpp kSpec_Fill4
//   0x180063a80  Array<{Light*,int}>::SetSize(n, const T&)   (stride 0x10)
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// `Video::ImageBuffer::Width()` / `Height()` (0x1800f2530 / 0x1800f24e0) are
// INLINED at six sites in retail: the three-way branch on the image's flags
// word at +0x34 (bit 8 full screen, bit 9 half screen, bit 6 mip shift by
// `Configuration::shadowScalar`). B.4: the ported members ARE that ladder
// (buffers.cpp), so each site calls them by name.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// `if (ctx->rect != {x,y,w,h}) { ctx->dirty |= bit; ctx->rect = {x,y,w,h}; }`
// The viewport form IS `Context::SetViewport` (0x18001a060, context.cpp:
// dirty 0x200), inlined by retail and called by name here. The scissor form is
// `Context::SetScissor` (0x18001a0f0, dirty 0x400), which has no C++ body, so
// it is spelled out on the named fields.
// ---------------------------------------------------------------------------
void SetScissor(Video::Context& context, const Video::Viewport& vp) {
    Video::Viewport& r = context._scissor;
    if (r.x == vp.x && r.y == vp.y && r.width == vp.width && r.height == vp.height) return;
    context._dirty |= RtVideo::kDirtyScissor;
    r = vp;
}

// ---------------------------------------------------------------------------
// The shadow bias matrix. TWO variants; SpotLight always uses A, SunLight picks
// between them with a virtual query on the Device.
//
//   A (0x1800fa7... / 0x180105993 / 0x18010681f):   B (0x18010689d):
//     [ 0.5   0    0   0 ]                            [ 0.5  0    0    0 ]
//     [ 0   -0.5   0   0 ]                            [ 0    0.5  0    0 ]
//     [ 0     0    1   0 ]                            [ 0    0    0.5  0 ]
//     [ 0.5  0.5   0   1 ]                            [ 0.5  0.5  0.5  1 ]
//
// A maps a [-1,1] clip box with a top-left texture origin onto [0,1]x[0,1] and
// leaves z alone (the DX10+ [0,1] depth range); B also remaps z from [-1,1],
// which is the DX9 convention. That reading is offered as orientation only —
// the numbers are the port. `Matrix()` zeroes all sixteen.
// ---------------------------------------------------------------------------
void BiasMatrixA(Math::Matrix& b) {
    b = Math::Matrix();
    b.m[0]  = 0.5f;
    b.m[5]  = -0.5f;
    b.m[10] = 1.0f;
    b.m[12] = 0.5f;
    b.m[13] = 0.5f;
    b.m[15] = 1.0f;
}
void BiasMatrixB(Math::Matrix& b) {
    b = Math::Matrix();
    b.m[0]  = 0.5f;
    b.m[5]  = 0.5f;
    b.m[10] = 0.5f;
    b.m[12] = 0.5f;
    b.m[13] = 0.5f;
    b.m[14] = 0.5f;
    b.m[15] = 1.0f;
}

// ---------------------------------------------------------------------------
// The shared tail of SpotLight::Draw and SunLight::Draw: take the draw group's
// projection Matrix and its view Transform3, and write
//
//     ctx->shadowMatrix = AsMatrix(Inverse(viewTransform)) * projection * bias
//
// into Context+0x1e78, then raise the 0x2000 dirty bit.
//
// LEFT: +0x1e78 is `_lightStaging + 0xa0`; Context.h (video's header) keeps
// the 0x1a0-byte staging block undivided, so the Matrix there is viewed once.
// ---------------------------------------------------------------------------
constexpr size_t kLightStagingShadowMatrix = 0x1e78 - 0x1dd8;   // Context+0x1e78
static_assert(offsetof(Video::Context::LightStaging, shadowMatrix) == kLightStagingShadowMatrix,
              "Context::LightStaging::shadowMatrix is Context+0x1e78");

void WriteShadowMatrix(Video::Context& context, const Math::Matrix& projection,
                       const Math::Transform3& viewXform, const Math::Matrix& bias) {
    const Math::Transform3 inverse = viewXform.Inverse();
    const Math::Matrix     asMatrix = inverse.AsMatrix();
    const Math::Matrix     product  = asMatrix * projection;
    const Math::Matrix     result   = product * bias;

    context._lightStaging.shadowMatrix = result;   // B.4 R-VIDEO: the named field at +0xa0
    context._dirty |= RtVideo::kDirtyLightConstants;
}

}  // namespace

// ############################################################################
// Graphics::AmbientLight
// ############################################################################

// ============================================================================
// ??0AmbientLight@Graphics@@QEAA@XZ 0x1800fa940 / ??1AmbientLight@Graphics@@UEAA@XZ
// 0x180058f30 -- the two DOORS are gone (ENDGAME B.3.9 block GFX, B39_RULES D1).
// The ctor's body is the real `AmbientLight()` (Light.h); the destructor thunk
// (five instructions, a tail jump onto ~Geometry that the linker folded) is the
// empty `~AmbientLight()` below plus the compiler's chain. Nothing in retail or
// the tree ever called ??0AmbientLight.
// ============================================================================

// ############################################################################
// ENDGAME B.3.5 block E — the Light-branch bodies, MOVED ONTO THE MEMBERS
// ############################################################################
//
// Every body below is a member of `Graphics::AmbientLight`, `Graphics::SpotLight`
// or `Graphics::SunLight`. ENDGAME B.3.9 block GFX: AmbientLight and SpotLight
// are concrete and wear the compiler's table, so their slot doors are gone.
//
// RULE B5-17 IS APPLIED HERE. Retail names slot 2 and slot 10 both `Draw`. Slot
// 2 keeps the retail spelling; slot 10 is `DrawLight`.
//
// B.4: the `Video::Context&` and `Graphics::Renderer&` parameters are read
// through their own named fields; the one-cast bridges are gone.
// ############################################################################

// slot 1 — `RET`. Empty in the shipped build; an ambient light has nothing to
// tick (reference_retail_ships_empty_bodies.md).
// slot 2 — `RET`. The per-item Draw arm; an ambient light emits no draw items,
// only the light state its slot-10 Draw sets.
// slot 9 — `RET`. An ambient light casts no shadow.
// ============================================================================
// slot 3 — ?AddDrawItems@AmbientLight@Graphics@@UEBAXAEBVScene@2@AEAVRenderer@2@@Z
//                                                    retail 0x1800fa890
// ============================================================================
// TWO `Array<T>::SetSize(n + 1, fill)` calls and nothing else. THE SCENE
// ARGUMENT IS NEVER READ — retail keeps `this` in RBX and the renderer in RDI
// and touches RDX not once. Transcribed as such rather than "tidied" into a use.
//
//   1. `renderer->_sceneGroups[renderer->_currentSceneGroup]._drawableIds` —
//      the Array at +0x100 of the 0x110-byte scene group (the SAME array
//      ReflectionImageBuffer::Render appends to) — grows by one, filled with
//      `renderer->_drawables._size` (+0x28).
//   2. `renderer->_drawables` (+0x20), the Array of {Light*, int} pairs, grows
//      to `_drawables._size + 1`, filled with `{ this, _drawGroupsUsed }`.
//
// The two counts come from DIFFERENT places: the first is the target array's
// own `_size`, the second is `_drawables._size`. Swapping them would be
// invisible at a single light and wrong at two.
// ============================================================================
namespace Graphics {

// `??1Light@Graphics@@UEAA@XZ` 0x180058d20 is a bare tail into ~Geometry with
// NO restamp of its own. ENDGAME B.3.8 block RB defines the C++ destructor
// EMPTY so the real subclass chains link; the compiler adds a Light restamp
// retail does not make, between two restamps, with nothing dispatching in
// between. (ENDGAME B.3.9 block GFX: its door `RtGraphics_Light_Dtor` and the
// Light RT_DELDTOR row are retired with the minted `??_7Light@` -- every Light
// subclass is concrete and the compiler stamps the Light table in passing.)
Light::~Light() {}

// ??1AmbientLight@Graphics@@UEAA@XZ 0x180058f30 -- five instructions, four of
// them the MSVC unwind marker, then `JMP ??1Geometry`: AmbientLight adds no
// destructible member and the linker folded its destructor onto ~Geometry. The
// empty body plus the compiler's ~Light / ~Geometry chain is all of it.
AmbientLight::~AmbientLight() {}

void AmbientLight::Update(const System::Time& time) { (void)time; }

void AmbientLight::Draw(Video::Context& context, const Camera& camera,
                        ExternalPtrHandle material, float opacity,
                        int a, int b, int c) const {
    (void)context; (void)camera; (void)material; (void)opacity;
    (void)a; (void)b; (void)c;
}

void AmbientLight::DrawShadowMap(Video::Context& context, Renderer& renderer,
                                 int a, int b) const {
    (void)context; (void)renderer; (void)a; (void)b;
}

void AmbientLight::AddDrawItems(const Scene& scene, Renderer& renderer) const {
    (void)scene;                                   // retail never touches RDX
    const int32_t groupIndex = renderer._currentSceneGroup;     // +0x30
    const int32_t lightId    = renderer._drawables._size;       // +0x28

    Collection::Array<int32_t>& lights =
        renderer._sceneGroups._data[groupIndex]._drawableIds;   // stride 0x110, +0x100

    int32_t fill = lightId;                        // MOV [RSP+0x40],EAX
    rtcoll::RtColl_ArraySetSizeFill_180023830(&lights, lights._size + 1, &fill);

    // Retail keeps `this` in RBX and stores it into the {Light*, int} pair
    // (`MOV [RSP+0x20],RBX`) as `RendererDrawable::_object` (`const Light*`).
    // `_pad0c` is written ZERO here, as the local's `_pad` always was.
    RendererDrawable pair;
    pair._object = this;
    pair._value  = renderer._drawGroupsUsed;       // +0x38
    pair._pad0c  = 0;
    rtcoll::RtColl_ArraySetSizeFill_180063a80(&renderer._drawables, lightId + 1, &pair);
}

// ============================================================================
// slot 10 — ?Draw@AmbientLight@Graphics@@UEBAXAEAVContext@Video@@AEAVRenderer@2@HHHH@Z
//                                                    retail 0x1800fa7b0
// ============================================================================
// Set the ambient light state on the Context, then tail-jump DrawItemList.
//
// SIX of `SetLight`'s seven parameters are CONSTANTS here — two zero Vector3s,
// a zero Vector4, 0.0f, and the int 1 — and only two come from the object: the
// colour at +0xc0 and the scalar at +0xd0.
//
// THE TAIL: `DrawItemList(renderer, ctx, arg4, arg6, arg7)`. arg5 is NOT
// forwarded — retail OVERWRITES its own arg5 home slot with arg7 to make the
// stack argument land, which is the idiom the banner describes.
// ============================================================================
void AmbientLight::DrawLight(Video::Context& context, Renderer& renderer,
                             int a4, int a5, int a6, int a7) const {
    (void)a5;                                      // consumed only as the ABI slot

    const Math::Vector3 zeroV3a;                   // Vector3(): zero
    const Math::Vector3 zeroV3b;
    const Math::Vector4 zeroV4;                    // Vector4(): zero

    context.SetLight(zeroV3a, zeroV3b, zeroV4, _colour, 0.0f, _intensity, 1);

    renderer.DrawItemList(context, a4, a6, a7);
}

}  // namespace Graphics

// ############################################################################
// Graphics::SpotLight
// ############################################################################

// ============================================================================
// ??0SpotLight@Graphics@@QEAA@XZ                     retail 0x180106690
//
// Geometry ctor, the two vptr stamps, TWELVE zeroed qwords from +0xc0 to
// +0x118, the three literals, five more zeroed qwords, `CreateTargets()`, and
// only THEN `[+0x30] = 0x8000` — the kind word is written AFTER the call, not
// before, and the order is transcribed because CreateTargets reads the object.
//
// +0x158 is NOT zeroed by the ctor (the run of stores skips from +0x150 to
// +0x160). CreateTargets fills it. Recorded, not helpfully zeroed.
//
// ENDGAME B.3.9 block GFX: that body IS the real `SpotLight()` (Light.h); the
// door that stamped the minted tables is gone.
// ============================================================================
namespace Graphics {

// ============================================================================
// ??1SpotLight@Graphics@@UEAA@XZ                     retail 0x180106270
//
// Restamp, then the three owned resources in retail's order, then tail-jump
// ~Geometry. TWO DESTROY IDIOMS again: the two ImageBuffers go through their
// own vtable slot 0 with flags = 1 (destroy AND free), the RenderBuffer goes
// the long way because it has no virtual destructor.
//
// ENDGAME B.3.9 block GFX: the body moved here from the retired ??1 door. The
// restamp and the tail into ~Light (empty) / ~Geometry are the compiler's.
// ============================================================================
SpotLight::~SpotLight() {
    Video::ImageBuffer* const owned[2] = { _shadowMap, _shadowDepth };   // +0x148, +0x150
    for (Video::ImageBuffer* const img : owned) {
        if (img == nullptr) continue;
        delete img;                                // slot 0, flags 1 (B3X W0)
    }
    Video::RenderBuffer* const rb = _shadowTarget;           // +0x158
    if (rb != nullptr) {
        rb->PlatformDestruct();
        Video::RenderBuffer::operator delete(rb);  // 0x18001f360
    }
}

// slot 1 / slot 2 — both `RET`, like AmbientLight's.
void SpotLight::Update(const System::Time& time) { (void)time; }

void SpotLight::Draw(Video::Context& context, const Camera& camera,
                     ExternalPtrHandle material, float opacity,
                     int a, int b, int c) const {
    (void)context; (void)camera; (void)material; (void)opacity;
    (void)a; (void)b; (void)c;
}

}  // namespace Graphics

// ============================================================================
// slot 3 — ?AddDrawItems@SpotLight@Graphics@@UEBAXAEBVScene@2@AEAVRenderer@2@@Z
//                                                    retail 0x180105e30
// ============================================================================
// Queue TWO draw groups: the shadow-map pass into the light's own render
// target, then a second pass with a null target.
//
// THE WHOLE BODY IS BEHIND `_shadowMap && _shadowDepth` (+0x148 and +0x150,
// both non-null). A spot light whose targets failed to allocate contributes
// nothing rather than crashing, and that is retail's own guard, not one added
// here.
//
// The camera is built from the light's Transform at +0x84 (through
// `Transform::AsTransform3`) and its Frustum at +0xc0, with the bool argument
// `MOV R9B,1`. It is passed as BOTH camera arguments of the first
// BeginDrawGroup — retail really does put the same pointer in R8 and R9.
//
// The second BeginDrawGroup's first camera is `renderer->_drawGroups[0]
// ._camera` (+0x10 then +0x08) — the main view's camera — while the second
// stays the light's. Its flags are 3/3 where the first pass used 1/1, and its
// render target is NULL (`XOR EDX,EDX`).
// ============================================================================
namespace Graphics {

void SpotLight::AddDrawItems(const Scene& scene, Renderer& renderer) const {
    // THE WHOLE BODY IS BEHIND `_shadowMap && _shadowDepth` — retail's own
    // guard, not one added here.
    if (_shadowMap == nullptr) return;
    if (_shadowDepth == nullptr) return;

    const Math::Transform xform = _transform._current;       // +0x84, 0x1c
    const Math::Transform3 t3 = xform.AsTransform3();

    const Camera camera(_frustum, t3, true);   // 0x1800fe960, MOV R9B,1

    renderer.AddLight(this);

    int32_t id = renderer.BeginDrawGroup(_shadowTarget, camera, camera,
                                         static_cast<int32_t>(MaterialPass::Depth),
                                         Bits(MaterialTypeMask::Opaque));
    scene.AddGlobalDrawItems(renderer);
    if (renderer._detailId != 0)                     // +0x48
        scene.AddCulledDrawItems(renderer);
    renderer.EndDrawGroup(id);

    const Camera& mainCamera = renderer._drawGroups._data[0]._camera;   // group 0 +0x08
    id = renderer.BeginDrawGroup(nullptr, mainCamera, camera, static_cast<int32_t>(MaterialPass::SpotLight),
                                 Bits(MaterialTypeMask::Opaque | MaterialTypeMask::Blended));
    scene.AddGlobalDrawItems(renderer);
    if (renderer._detailId != 0)                     // +0x48
        scene.AddCulledDrawItems(renderer);
    renderer.EndDrawGroup(id);
}

}  // namespace Graphics

// ============================================================================
// slot 9 — ?DrawShadowMap@SpotLight@Graphics@@UEBAXAEAVContext@Video@@AEAVRenderer@2@HH@Z
//                                                    retail 0x180105d00
// ============================================================================
// Bind the light's render target, set the viewport to the shadow map's own
// size, clear, and hand the list to the renderer.
//
// THE CLEAR: `obj = ctx->_contextData; obj->vtable[4](obj, 6, 0, 1.0f, 0)` is
// slot 4 of `Video::ContextData`, `Clear(flags, color, depth, stencil)` —
// `Context::Clear` (0x1800f23f0) inlined, called by name here. FIVE values —
// the float travels in XMM3 and the trailing int at [RSP+0x20]; Ghidra prints
// this with the float and the last int dropped.
//
// THE FIFTH VALUE OF THE TAIL `DrawItemList` IS THIS FUNCTION'S OWN arg5,
// PASSED THROUGH UNTOUCHED: retail tail-jumps without writing its arg5 home
// slot, so the callee reads the very int this function was given.
// ============================================================================
namespace Graphics {

void SpotLight::DrawShadowMap(Video::Context& context, Renderer& renderer,
                              int a4, int a5) const {
    context.SetRenderBuffer(_shadowTarget);          // inlined in retail

    const Video::ImageBuffer* const image = _shadowMap;
    const int32_t h = image->Height();               // read FIRST in retail
    const int32_t w = image->Width();
    context.SetViewport(Video::Viewport{ 0, 0, w, h });   // dirty 0x200

    context.Clear(Video::ClearFlags::Depth | Video::ClearFlags::Stencil, 0, 1.0f, 0);
    // The fifth value is THIS FUNCTION'S OWN arg5, passed through untouched.
    renderer.DrawItemList(context, a4, 0, a5);
}

}  // namespace Graphics

// ============================================================================
// slot 10 — ?Draw@SpotLight@Graphics@@UEBAXAEAVContext@Video@@AEAVRenderer@2@HHHH@Z
//                                                    retail 0x180105960
// ============================================================================
// The draw-group index is arg5 (`MOVSXD R13,[RBP+0xf0]`), NOT arg4 — arg4
// arrives in R9D and is never read by this body. Both facts are transcribed.
//
// From the draw group at `renderer->_drawGroups[arg5]` (stride 0x1f0), its
// `_camera` (+0x08):
//   +0x9c  `_camera._projection`, the shadow projection Matrix
//   +0x6c  `_camera._cameraToWorld`, the shadow view Transform3
//   +0x78  `_cameraToWorld.axisY`, ALSO read as a Vector3 for SetLight
//   +0x90  `_cameraToWorld.translation`, ditto
// The two Vector3s overlap the Transform3 on purpose: they are re-read out of
// the group rather than out of a local copy.
//
// The tail is `DrawItemList(renderer, ctx, arg5 + 1, arg6, arg7)` — note the
// **+ 1**, which SunLight's tail does not have.
// ============================================================================
namespace Graphics {

void SpotLight::DrawLight(Video::Context& context, Renderer& renderer,
                          int a4, int a5, int a6, int a7) const {
    (void)a4;                                       // R9D, never read by retail

    // The draw-group index is arg5, NOT arg4.
    const Camera& groupCamera = renderer._drawGroups._data[a5]._camera;

    Math::Matrix bias;
    BiasMatrixA(bias);

    WriteShadowMatrix(context, groupCamera._projection, groupCamera._cameraToWorld, bias);

    context.SetImageBuffer(_boundImage, 4);          // +0x50, sub-bit 0x10
    context.SetImageBuffer(_shadowDepth, 5);         // +0x58, sub-bit 0x20

    const int32_t dim = _shadowDepth->Width();

    const Math::Vector4 zeroV4;                    // Vector4(): zero
    context.SetLight(groupCamera._cameraToWorld.translation,   // RDX, +0x90
                     groupCamera._cameraToWorld.axisY,          // R8,  +0x78
                     _colour,
                     zeroV4,
                     _spotScalarA,
                     _spotScalarB,
                     dim);

    // Note the `+ 1`, which SunLight's tail does not have.
    renderer.DrawItemList(context, a5 + 1, a6, a7);
}

}  // namespace Graphics

// ############################################################################
// Graphics::SunLight — the two slots sunlight.cpp did not port
// ############################################################################

// ============================================================================
// slot 10 — ?Draw@SunLight@Graphics@@UEBAXAEAVContext@Video@@AEAVRenderer@2@HHHH@Z
//                                                    retail 0x1801067b0
// ============================================================================
// The same shape as SpotLight's, with three differences, all of them load-
// bearing:
//   * the bias matrix is CHOSEN, not fixed — `Video::Device::_singleton->
//     deviceData` (+0x100) is asked through its own vtable slot 7 (`CALL
//     [RAX+0x38]`, `DeviceData::Slot7`) and its bool answer picks matrix A over
//     matrix B (see BiasMatrixA/B);
//   * the group fields are the CULL camera's (+0x174 projection, +0x144 view),
//     not the camera's +0x9c / +0x6c;
//   * SetLight's two Vector3/Vector4 arguments come from the OBJECT (+0xc0,
//     +0xcc, +0xdc) rather than from the group, its scalars are the literals
//     0.0f and 1.0f, and the first argument is a ZERO Vector3.
//
// The tail is `DrawItemList(renderer, ctx, arg4, arg6, arg7)` — arg4 here,
// where SpotLight used arg5 + 1, and arg5 is used only as the group index.
// ============================================================================

namespace Graphics {

void SunLight::DrawLight(Video::Context& context, Renderer& renderer,
                         int a4, int a5, int a6, int a7) const {
    const Camera& cullCamera = renderer._drawGroups._data[a5]._cullCamera;

    // Read BEFORE anything else in the body (0x1801067e9..0x18010680d). LEFT:
    // `Device::deviceData` is a `void*` in Device.h (video's header), so the
    // one view of it as its `Video::DeviceData` is here.
    const bool modern =
        ::Video::Device::_singleton->deviceData->Slot7();

    Math::Matrix bias;
    if (modern) BiasMatrixA(bias); else BiasMatrixB(bias);

    WriteShadowMatrix(context, cullCamera._projection, cullCamera._cameraToWorld, bias);

    context.SetImageBuffer(_depthTarget, 5);         // +0x58, sub-bit 0x20

    const int32_t dim = _depthTarget->Width();

    // The three vectors come from the OBJECT here, not from the draw group: the
    // direction Vector3 at +0xc0, the sun Vector4 at +0xcc and the ambient
    // Vector4 at +0xdc — the three `?Set@SunLight@` writes, under their names.
    const Math::Vector3 zeroV3;
    context.SetLight(zeroV3, _direction, _sun, _ambient, 0.0f, 1.0f, dim);

    // arg4 here, where SpotLight used arg5 + 1.
    renderer.DrawItemList(context, a4, a6, a7);
}

}  // namespace Graphics

// ============================================================================
// slot 9 — ?DrawShadowMap@SunLight@Graphics@@UEBAXAEAVContext@Video@@AEAVRenderer@2@HH@Z
//                                                    retail 0x180106bc0
// ============================================================================
// Longer than SpotLight's because it sets THREE rectangles, not one, and the
// third is inset by two pixels:
//
//   1. viewport (+0x1d0) = (0, 0, w, h)                     dirty bit 0x200
//   2. scissor  (+0x1e0) = (0, 0, w, h)                     dirty bit 0x400
//   3. the Clear
//   4. viewport (+0x1d0) = (1, 1, w - 2, h - 2)             dirty bit 0x200
//
// Step 4 REWRITES the viewport step 1 just set, to a one-pixel border inside
// the shadow map — the classic guard band that keeps a cascade's edge texels
// from bleeding. `ADD EAX,-2` / `ADD R9D,-2` on BOTH dimensions, and the origin
// becomes (1, 1), not (0, 0).
//
// *** THE IMAGE IS RE-READ FROM `this->_depthTarget` BEFORE EVERY ONE OF THE
// FOUR DIMENSION QUERIES. *** Retail never caches it, and the queries themselves
// re-read the Device singleton each time.
//
// THE WHOLE BODY IS BEHIND `renderer->_shaderDetail != 0` (`CMP dword
// [R8+0x3c],0 / JZ`) — with no shadow-casting items queued there is nothing to
// draw and even the Context is left untouched.
// ============================================================================

namespace Graphics {

namespace {
// NOT RETAIL (ext.staggered_passes): the emitting thread's "this frame holds the map".
thread_local bool g_extShadowHoldEmit = false;
}  // namespace

void SunLight::ExtShadowHoldEmit(bool held) { g_extShadowHoldEmit = held; }

void SunLight::DrawShadowMap(Video::Context& context, Renderer& renderer,
                             int a4, int a5) const {
    // THE WHOLE BODY IS BEHIND `renderer->_shaderDetail != 0` (+0x3c).
    if (renderer._shaderDetail == 0)
        return;

    // EXT SEAM (NOT RETAIL, issue #253): ext.staggered_passes holds the map this frame (its
    // shadow group is empty): no clear, no redraw; DrawLight samples last frame's map with
    // the sun camera it was rendered with (SunLight.h).
    if (g_extShadowHoldEmit)
        return;

    context.SetRenderBuffer(_renderBuffer);          // inlined in retail

    {
        const Video::ImageBuffer* const image = _depthTarget;
        const int32_t h = image->Height();
        const int32_t w = image->Width();
        context.SetViewport(Video::Viewport{ 0, 0, w, h });   // dirty 0x200
    }
    {
        const Video::ImageBuffer* const image = _depthTarget;   // re-read
        const int32_t h = image->Height();
        const int32_t w = image->Width();
        SetScissor(context, Video::Viewport{ 0, 0, w, h });   // dirty 0x400
    }

    context.Clear(Video::ClearFlags::Depth | Video::ClearFlags::Stencil, 0, 1.0f, 0);

    {
        const Video::ImageBuffer* const image = _depthTarget;   // re-read
        const int32_t h = image->Height() - 2;
        const int32_t w = image->Width() - 2;
        context.SetViewport(Video::Viewport{ 1, 1, w, h });   // dirty 0x200
    }

    const int32_t dim = _depthTarget->Width();       // re-read

    const Math::Vector3 zeroV3;
    context.SetLight(zeroV3, _direction, _sun, _ambient, 0.0f, 1.0f, dim);

    renderer.DrawItemList(context, a4, 0, a5);
}

}  // namespace Graphics
