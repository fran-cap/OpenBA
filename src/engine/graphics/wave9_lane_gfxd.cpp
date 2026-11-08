// ============================================================================
// src/engine/graphics/wave9_lane_gfxd.cpp — WAVE 9 LANE GFXD
// The bottom half of the Graphics::Renderer class lane.
// ============================================================================
//
// WHAT THIS LANE IS, AND WHY IT IS SHAPED THIS WAY
//
// `?Draw@Renderer@Graphics@@` was declined by lanes GFX, F, GFX2 and GFXR. Lane
// GFXR's correction is the reason this file exists: nobody had run `Draw`'s five
// callees through the DLL's EXPORT TABLE, and four of the five turn out to be
// ordinary named exports (`Renderer::Reset`, `Renderer::AddScene`,
// `Renderer::BeginDrawGroup`, `Scene::AddCulledDrawItems`). Only `0x180062d60`
// is an unexported internal. So `Draw` is one unnamed body behind four portable
// exports — a specifiable class lane rather than a wall.
//
// This lane takes the two of those four whose closures are finite and readable,
// and declines the other two with measured blockers (WAVE9_LANE_GFXD.md §4).
//
// *** AND IT REPEATS LANE GFXR'S LESSON ON ITS OWN CALLEES, WHICH IS THE POINT
// OF THE LESSON. *** Every address below was run through the export table
// before it was written down. Three that a source-tree grep would have recorded
// as "unnamed internals" are exports:
//
//     0x180017a70   ??0Camera@Graphics@@QEAA@AEBV01@@Z   (copy constructor)
//     0x1800fe7f0   ??0Camera@Graphics@@QEAA@XZ          (default constructor)
//     0x180017c40   ??4Camera@Graphics@@QEAAAEAV01@AEBV01@@Z  (operator=)
//
// A FOURTH CORRECTION FALLS OUT OF THE SAME LOOKUP, and it is a live one:
// renderer.cpp's registration block is headed "Graphics::Camera (all five of
// the class's exports)". **`Graphics::Camera` has THIRTEEN exports**, listed in
// WAVE9_LANE_GFXD.md §2. Five of them were ported by lane F; two more are
// ported here; six remain. Nothing depended on the wrong count, but a lane
// reading that comment would have believed the class was closed.
//
// ---------------------------------------------------------------------------
// A FACT NEITHER RECORD CARRIED: `Renderer::Reset`'s own closure
// ---------------------------------------------------------------------------
// GFXR's table says `Draw` is blocked by ONE unnamed body. That is true of
// `Draw`'s own call list and it understates the lane: `Reset` — one of the four
// "portable exports" — is itself three unexported bodies deep, and none of the
// three was in the tree:
//
//     0x1800626a0   clear a DrawGroup's three per-pass item arrays  [NOT EXPORTED]
//     0x180062ae0   Array<Renderer::SceneEntry>::SetSize(0)         [NOT EXPORTED]
//     0x180062c40   Array<Renderer::Drawable>::SetSize(0)           [NOT EXPORTED]
//
// and `AddScene` two more (`0x1800637b0`, `0x18001bdc0`), which drag in
// `0x180062860` and `0x18001ce90`. "Portable" was the right word — all seven are
// small and all seven are ported below — but "one unnamed body" is the count for
// `Draw`, not for the lane. Recorded so the next lane sizes `BeginDrawGroup` and
// `AddCulledDrawItems` from their own closures rather than from GFXR's table.
//
// ---------------------------------------------------------------------------
// THE ARRAY BODIES ARE LANE Q'S `SetSizeCore`, NOT A SECOND COPY OF IT
// ---------------------------------------------------------------------------
// Five of the seven internals are `Collection::Array<T>` instantiations, and
// every one of them matched `rtcoll::SetSizeCore` instruction for instruction:
// the `newSize == _size` early return, the `newSize > _capacity` grow with the
// `* 3` bump on a by-one append from non-empty, the `realloced ? 0 : oldSize`
// loop start, the realloc-destroy arm and the shrink-tail arm. So they are
// registered as new `ArraySpec` rows against lane Q's body.
//
// They are declared HERE rather than in array.cpp, which is where lane GFX3 and
// lane UIF put theirs. That precedent's stated reason is "a private second copy
// of SetSizeCore in the graphics tree is exactly the drift this parameterisation
// exists to prevent" — and that reason is satisfied: this file calls
// `rtcoll::SetSizeCore` and writes no loop of its own. What it avoids is a
// third concurrent lane editing array.cpp in the same wave. The cost is that
// array_internal.h's "Only two files include this" is now stale by one; that is
// recorded in WAVE9_LANE_GFXD.md §7 rather than fixed under a neighbour.
//
// ---------------------------------------------------------------------------
// UNWRITTEN BYTES — three separate holes, all real, none of them modelling laziness
// ---------------------------------------------------------------------------
// `reference_oracle_must_not_compare_unwritten_bytes.md`. Each arm below leaves
// bytes alone that retail also leaves alone, and the probe excludes exactly
// those and no others:
//
//   Camera copy ctor  (0x180017a70): copies 53 dwords (+0x00..+0xd3) and the
//                     BYTE at +0xd4. **+0xd5..+0xd7 is never written.**
//   Camera default    (0x1800fe7f0): writes +0x00..+0x60 and +0x64..+0xd4.
//                     **+0x61..+0x63 and +0xd5..+0xd7 are never written.**
//   SceneEntry assign (0x18001bdc0): the Camera hole lands at element +0xe5..
//                     +0xe7, and **+0xfc..+0xff is never written** (retail
//                     copies five separate dwords at 0xe8/ec/f0/f4/f8 and skips
//                     0xfc).
//   SceneEntry init   (0x180062860): the two Camera holes at element +0x71..
//                     +0x73 and +0xe5..+0xe7, plus **+0xfc..+0xff**.
//
// `ArraySpec` carries ONE assign window and ONE init window, and kSpec_SceneEntry
// needs two and three. That limit is already filed against kSpec_Sprite38 and
// kSpec_SpriteDraw. Rather than widen a shared struct under another lane, this
// file sets both windows to "none" and its OWN probe carries the real hole
// lists — which is sound because the only consumer of those two fields is a
// probe, and this spec's probe is here. Stated as an honest limit rather than
// left to be discovered: **if a future caller drives kSpec_SceneEntry through
// array_probe.cpp, that probe will compare bytes neither side writes and report
// a divergence that is not one.** WAVE9_LANE_GFXD.md §6 limit 3.
//
// ---------------------------------------------------------------------------
// WHAT THIS LANE DOES *NOT* CLAIM
// ---------------------------------------------------------------------------
// Nothing in our tree calls `Renderer::Reset` or `Renderer::AddScene` today —
// the only live door on this class is `?Draw@Renderer@Graphics@@`
// (src/game/Game/gameupdateloop.cpp:627 and engine/funcs/engine_funcs.cpp:3599),
// and `Draw` is still the DLL's, so the DLL's own internal `CALL 0x180105110`
// reaches the DLL's `Reset`, not ours. These registrations are therefore INERT
// today and their `calls=` is 0 by construction, not by accident
// (`reference_uncalled_port_is_not_verified.md`). The oracle is the probe at the
// bottom of this file, and the probe is the ONLY thing that executes these
// bodies before cutover. That is exactly lane X's situation and it is stated the
// same way.
// ============================================================================

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <new>

#include "graphics/Camera.h"
#include "../collection/array_destroy_internal.h"
#include "../collection/array_internal.h"


namespace rtgfxd {

using rtcoll::ArrayHdr;
using rtcoll::ArraySpec;

// ============================================================================
// The two DLL internals this lane CALLS rather than ports.
//
// Both spell their address as a LITERAL inside ResolveAddr, and must keep doing
// so: tools/runtime_census.py scans for that literal at the call site, and
// hiding an edge behind a one-line helper is how an edge goes missing from the
// census (reference_census_cannot_see_the_macro_doors.md).
//
// Both are ALREADY OURS — ResolveAddr consults the port registry first
// (reference_resolveaddr_consults_port_registry_first.md), and lane Q registered
// both — so neither is a cutover residue row. Neither caches a null: a miss is
// retried, per the contract in rt_resolve_cache.cpp.
// ============================================================================

// Collection::Array<int32>::operator= — retail 0x18001cdf0, lane Q's
// RtColl_I32ArrayAssign_18001cdf0. The tail call of SceneEntry's copy-assign.
//
// ENDGAME B.3.2 step 2 (callers) — NOT converted to a member call.
// `RendererSceneEntry::drawableIds` (Camera.h) is a real `Collection::
// Array<int32_t>`, but the class's `operator=` is presently `=default` (a
// SHALLOW bitwise copy — Collection.h's Array banner: the deep
// `ArrayAssignCore` arrives as `operator=` only once the last bitwise copier
// converts). Rewriting the call below as `dst->drawableIds =
// src->drawableIds;` would silently swap this deep, allocator-owning assign
// for a shallow one — sharing `_data` between the two records and double-
// freeing it. B3R: a call of that SAME ported body by name,
// `rtcoll::RtColl_I32ArrayAssign_18001cdf0`.
// (B.4 Z RT2: the one-line `RtArrayAssignI32(void*, const void*)` forwarder
// is gone; its caller below calls the typed body.)

// ENDGAME B.3.2 step 2 (callers) — the `RtArrayDtorI32` forwarder that used to
// sit here (0x18001c750) is GONE: it is array.cpp's J1 body, spec-independent
// (`ArrayDtorSited(a) { a->~Array(); }` on the plain trait-derived
// `ArrayHdr = Collection::Array<uint8_t>`), so `AddScene`'s call below now
// destroys its stack record's trailing Array header directly.

// ============================================================================
// Graphics::Camera — the two constructors, both EXPORTS.
// ============================================================================

// ??0Camera@Graphics@@QEAA@AEBV01@@Z — retail 0x180017a70. Returns `this`
// (`MOV RAX,RCX` at 0x180017c37).
//
// FIFTY-THREE dword copies covering +0x00..+0xd3, then ONE byte at +0xd4
// (`MOVZX EAX,byte ptr [RDX+0xd4]` / `MOV byte ptr [RCX+0xd4],AL`). sizeof is
// 0xd8, so +0xd5..+0xd7 is a hole retail does not touch — see the banner.
// B.4: on the real class -- the 53 dwords are the frustum (incl. its pad), the
// Transform3 and the projection, then the `_zUp` byte.
//
// B.4 S4 GFX2 (behaviour): the body is `Graphics::Camera`'s own copy
// constructor (it was `= default`, a full 0xd8 blit, on the C++ copy path).
// Every member is built by its mem-initialiser, so no default constructor
// (Vector4 / Transform3 / Matrix zero) runs first; `_pad_d5` has none and is
// left unwritten, as retail leaves +0xd5..+0xd7. `Frustum`'s implicit copy is
// trivial and moves all 0x64 bytes, its +0x61..+0x63 pad included, as retail's
// dword run does.
}  // namespace rtgfxd

Graphics::Camera::Camera(const Camera& src)
    : _frustum(src._frustum),               // +0x00..+0x63 (25 dwords)
      _cameraToWorld(src._cameraToWorld),   // +0x64..+0x93
      _projection(src._projection),         // +0x94..+0xd3
      _zUp(src._zUp) {}                     // +0xd4 (MOVZX / MOV byte)

namespace rtgfxd {

// The door's `(this, src)` shape, returning `this`: the copy ctor above,
// placement-constructed into the caller's member.
Graphics::Camera* RtGraphics_Camera_CopyCtor(Graphics::Camera* self,
                                             const Graphics::Camera* src) {
    return ::new (static_cast<void*>(self)) Graphics::Camera(*src);
}

// ??0Camera@Graphics@@QEAA@XZ — retail 0x1800fe7f0. Returns `this`.
//
// Both float constants were LIFTED AS RAW BITS out of the shipped .rdata rather
// than retyped as decimals (reference_crt_coefficients_must_be_lifted_not_
// retyped.md): 0x18016175c is 0xbf800000 and 0x18016169c is 0x3f800000. Both
// happen to be exactly representable, so the two spellings agree here — the
// discipline is kept anyway, because "it looked exact" is not a check.
//
// INSTRUCTION ORDER, and where this file departs from it deliberately: retail
// zeroes the first 0x60 bytes as twelve qwords in the order 0x00, 0x10, 0x08,
// 0x18, 0x20 ... and then overwrites every one of those bytes with the 24
// MOVSS stores below. The reorder to plain ascending order is safe *for this
// specific pass only*, because all twelve stores write the SAME constant (zero)
// to DISJOINT addresses; no float arithmetic is reassociated anywhere in this
// function, so the "moved term" hazard does not arise
// (reference_moved_term_hides_where_terms_cancel.md).
//
// B.4: on the real class's fields. The twelve zero qwords over +0x00..+0x5f are
// dead stores retail overwrites with the 24 plane floats, so the planes are
// written once; the two pad windows stay unwritten either way.
Graphics::Camera* RtGraphics_Camera_DefaultCtor(Graphics::Camera* self) {
    float kOne, kNegOne, kZero;
    const uint32_t bOne = 0x3f800000u, bNegOne = 0xbf800000u, bZero = 0x00000000u;
    std::memcpy(&kOne, &bOne, 4);
    std::memcpy(&kNegOne, &bNegOne, 4);
    std::memcpy(&kZero, &bZero, 4);

    self->_frustum.isOrthographic = true;              // MOV byte [RCX+0x60],1

    // Volume::Frustum's 24 floats, in retail's store order and with retail's
    // exact constants. XMM2 is XORPS'd (+0.0f), XMM1 is 1.0f, XMM0 is -1.0f.
    static const signed char kFrustum[24] = {
        //  0     1     2     3     4     5     6     7
        //  0x00  0x04  0x08  0x0c  0x10  0x14  0x18  0x1c
             0,    1,    0,    0,    0,   -1,    0,   -1,
        //  0x20  0x24  0x28  0x2c  0x30  0x34  0x38  0x3c
             1,    0,    0,   -1,   -1,    0,    0,   -1,
        //  0x40  0x44  0x48  0x4c  0x50  0x54  0x58  0x5c
             0,    0,   -1,   -1,    0,    0,    1,   -1 };
    for (int k = 0; k < 6; ++k) {
        float c[4];
        for (int j = 0; j < 4; ++j) {
            const signed char s = kFrustum[k * 4 + j];
            c[j] = (s == 0) ? kZero : (s > 0 ? kOne : kNegOne);
        }
        Math::Vector4& plane = self->_frustum.planes[k];
        plane.x = c[0]; plane.y = c[1]; plane.z = c[2]; plane.w = c[3];
    }

    Math::Transform3& t = self->_cameraToWorld;        // +0x64..+0x93, zero
    t.axisX.x = kZero; t.axisX.y = kZero; t.axisX.z = kZero;
    t.axisY.x = kZero; t.axisY.y = kZero; t.axisY.z = kZero;
    t.axisZ.x = kZero; t.axisZ.y = kZero; t.axisZ.z = kZero;
    t.translation.x = kZero; t.translation.y = kZero; t.translation.z = kZero;

    for (int i = 0; i < 16; ++i)                       // Matrix, +0x94..+0xd3
        self->_projection.m[i] = kZero;

    self->_zUp = 0;                                    // MOV byte [RCX+0xd4],DL
    return self;
}

// ============================================================================
// Renderer::SceneEntry — the element operations of the 0x110-stride array.
// Both are unexported COMDATs; both are reached only through kSpec_SceneEntry.
// ============================================================================

// Retail 0x18001bdc0 — copy-assign. Returns `dst` (`MOV RAX,RDI`).
//
// The zero-then-assign of the inner Array at +0x100 is retail's, not a
// defensive addition: 0x18001be3c..0x18001be41 stores two zero qwords over the
// triple and only then calls the array's operator=, which is what makes the
// assignment safe against a destination holding a foreign pointer.
//
// B.4: the element views, once each, as the `Graphics::RendererSceneEntry` it is
// (the ArraySpec callback hands bytes, array_internal.h), then its fields.
uint8_t* SceneEntryAssign(uint8_t* dstv, const uint8_t* srcv) {
    Graphics::RendererSceneEntry* const dst = reinterpret_cast<Graphics::RendererSceneEntry*>(dstv);   // element op of a collection spec row takes void* (the type-erased collection core: one spec-row signature serves every element type)  // CAST-OK: elem_stride
    const Graphics::RendererSceneEntry* const src =
        reinterpret_cast<const Graphics::RendererSceneEntry*>(srcv);   // the erased container core's element callback (void* dst / src): elem_stride  // CAST-OK: elem_stride
    dst->_scene        = src->_scene;                  // +0x00
    dst->_renderBuffer = src->_renderBuffer;           // +0x08
    RtGraphics_Camera_CopyCtor(&dst->_camera, &src->_camera);
    dst->_shaderDetail      = src->_shaderDetail;      // +0xe8
    dst->_shader            = src->_shader;            // +0xec
    dst->_materialTypeFlags = src->_materialTypeFlags; // +0xf0
    dst->_drawGroupIndex    = src->_drawGroupIndex;    // +0xf4
    dst->_detailId          = src->_detailId;          // +0xf8   +0xfc SKIPPED
    dst->_drawableIds._data     = nullptr;             // two zero qwords
    dst->_drawableIds._size     = 0;
    dst->_drawableIds._capacity = 0;
    rtcoll::RtColl_I32ArrayAssign_18001cdf0(&dst->_drawableIds, &src->_drawableIds);
    return dstv;
}

// Retail 0x180062860 — default construct. Returns `dst` (`MOV RAX,RBX`).
// +0xf8 is 0x40 and +0xf4 is -1; +0xfc is not written.
uint8_t* SceneEntryInit(uint8_t* dstv) {
    Graphics::RendererSceneEntry* const dst = reinterpret_cast<Graphics::RendererSceneEntry*>(dstv);   // element op of a collection spec row takes void* (the type-erased collection core: one spec-row signature serves every element type)  // CAST-OK: elem_stride
    dst->_scene        = nullptr;
    dst->_renderBuffer = nullptr;
    RtGraphics_Camera_DefaultCtor(&dst->_camera);
    dst->_shaderDetail      = 0;                       // MOV qword [RBX+0xe8],RDI
    dst->_shader            = 0;
    dst->_materialTypeFlags = 0;
    dst->_drawGroupIndex    = -1;
    dst->_detailId          = 0x40;                    // +0xfc SKIPPED
    dst->_drawableIds._data     = nullptr;
    dst->_drawableIds._size     = 0;
    dst->_drawableIds._capacity = 0;
    return dstv;
}

// Retail 0x18001ce90 — destroy a RANGE of SceneEntries: free each element's
// inner Array buffer at +0x100 and clear its 16-byte header, unconditionally.
// That is exactly lane DESTROY's BODY A with kD_InnerArr_110, which is the spec
// row already written for THIS address (array_destroy.cpp:255). Registered
// below, which lane DESTROY did not do — it registered only 0x18001c7e0, the
// `~Array` that CALLS this.
void SceneEntryDestroyRange(void* /*ignoredThis*/, uint8_t* base, int count) {
    rtcoll::FreeInnerArraysCore(base, count, &rtcoll::kD_InnerArr_110);
}

// `ArraySpec`'s ElemAssign/ElemInit are VOID-returning, and retail's two
// bodies both return `this` in RAX (0x18001be52 `MOV RAX,RDI`, 0x1800628bc
// `MOV RAX,RBX`). The REGISTERED entry points must keep that return, because a
// by-address caller reads it; the spec gets these two thin adapters instead.
// Bending the port's signature to fit the table would have been the wrong way
// round -- an fn_* typedef that drops part of the ABI is the
// reference_hidden_retslot_wild_write.md shape.
void SceneEntryAssignSpec(uint8_t* dst, const uint8_t* src) { SceneEntryAssign(dst, src); }
void SceneEntryInitSpec(uint8_t* dst) { SceneEntryInit(dst); }

// ---- element ops of the two trivially-destructible arrays ------------------

// Renderer::DrawItem, stride 0x38. The ASSIGN is retail's seven-qword unrolled
// copy at 0x180062762 (a pure byte copy of the whole element). The INIT arm at
// 0x1800627a1 is NOT a zero-fill: it writes qwords at +0x00/+0x08/+0x10, a dword
// 0 at +0x18, the dword **1** at +0x20 and a dword 0 at +0x30, leaving +0x1c,
// +0x24..+0x2f and +0x34..+0x37 alone.
void DrawItemAssign(uint8_t* dst, const uint8_t* src) { std::memcpy(dst, src, 0x38); }
void DrawItemInit(uint8_t* dstv) {
    Graphics::RendererDrawItem* const dst = reinterpret_cast<Graphics::RendererDrawItem*>(dstv);   // element op of a collection spec row takes void* (the type-erased collection core: one spec-row signature serves every element type)  // CAST-OK: elem_stride
    dst->_geometry = nullptr;                          // +0x00
    dst->_material = nullptr;                          // +0x08
    dst->_instance = nullptr;                          // +0x10
    dst->_flags    = 0;                                // +0x18 \ one dword
    dst->_node     = 0;                                // +0x1a /
    dst->_maxInstances     = 1;                                // +0x20
    dst->_animationInstanceId    = 0;                                // +0x30
}

// Renderer::Drawable, stride 0x10. ASSIGN copies two qwords; INIT writes a zero
// QWORD at +0x00 and a zero DWORD at +0x08, leaving +0x0c..+0x0f alone — a
// single window, so this spec can express it exactly.
void DrawableAssign(uint8_t* dst, const uint8_t* src) { std::memcpy(dst, src, 0x10); }
// B.4 Z MIRROR2 GFX: by name -- the QWORD is the pointer, the DWORD `_value`;
// `_pad0c` is not written.
static void DrawableInitFields(Graphics::RendererDrawable* d) {
    d->_object = nullptr;                              // +0x00  one qword
    d->_value  = 0;                                    // +0x08  one dword
}
void DrawableInit(uint8_t* dst) { rtcoll::InitAs<Graphics::RendererDrawable, &DrawableInitFields>(dst); }

// ============================================================================
// The three ArraySpec rows.
//
// `byteCopy` is TRUE only where `assign` really is a pure byte copy, because it
// is what gates SetSizeCore's realloc prefix memcmp. kSpec_SceneEntry's assign
// leaves two holes, so it is FALSE — turning that check on would compare bytes
// neither side wrote and fail 100% of reallocs.
//
// The unwritten windows are 0/0 ("none") on kSpec_SceneEntry and kSpec_DrawItem
// because both need MORE THAN ONE window and the struct carries one; the real
// hole lists live in this file's probe. See the banner. kSpec_Drawable's init
// hole fits and is spelled properly.
// ============================================================================

const ArraySpec kSpec_SceneEntry = {          // 0x180062ae0, 0x1800637b0
    "Array<Graphics::Renderer::SceneEntry>", 0x110, 8,
    &SceneEntryAssignSpec, &SceneEntryInitSpec, &SceneEntryDestroyRange,
    /*byteCopy*/ false,
    /*assign unwritten*/ 0, 0,       // really {0xe5,0xe8} and {0xfc,0x100}
    /*init   unwritten*/ 0, 0,       // really {0x71,0x74},{0xe5,0xe8},{0xfc,0x100}
    /*seed zero       */ 0x100, 0x110 };   // the inner Array is DEREFERENCED

const ArraySpec kSpec_DrawItem = {            // inside 0x1800626a0, x3
    "Array<Graphics::Renderer::DrawItem>", 0x38, 8,
    &DrawItemAssign, &DrawItemInit, nullptr, /*byteCopy*/ true,
    /*assign unwritten*/ 0, 0,
    /*init   unwritten*/ 0, 0,       // really {0x1c,0x20},{0x24,0x30},{0x34,0x38}
    /*seed zero       */ 0, 0 };

const ArraySpec kSpec_Drawable = {            // 0x180062c40
    "Array<Graphics::Renderer::Drawable>", 0x10, 8,
    &DrawableAssign, &DrawableInit, nullptr, /*byteCopy*/ true,
    /*assign unwritten*/ 0, 0,
    /*init   unwritten*/ 0x0c, 0x10,
    /*seed zero       */ 0, 0 };

// ============================================================================
// The registered array entry points.
//
// Two of the three are the CONSTANT-FOLDED one-argument shape lane UIF
// documented at 0x1800783d0: retail's `newSize` is the immediate 0 and the body
// takes ONE argument, so registering them behind the two- or three-argument
// signature would read a register the caller never set.
//
// NEITHER IS A `~Array`, and reading them as one would be a live defect rather
// than a harmless mislabel: `~Array` frees the buffer unconditionally, and both
// of these reach their `CMP old,[a] / JZ return` epilogue without freeing
// anything on the path Reset takes (capacity is already >= 0, so no realloc
// happens and `old == _data`). Reset REUSES the buffers; it does not release
// them.
// ============================================================================

// ENDGAME B.3.2 step 2 (callers) — these four entry points now call `.SetSize`/
// `.SetSizeFill` on the `Collection::Array<uint8_t, …, ExplicitSpec<&SPEC>>`
// bridge instead of naming `rtcoll::SetSizeCore` directly, so the class is the
// one place the (newSize, fill, spec) argument order is spelled. Neither row
// is trait-equivalent (kSpec_SceneEntry has a non-null init and byteCopy=false;
// kSpec_Drawable/kSpec_DrawItem carry partial-write holes), so the bridge
// stays `AsArray<&kSpec_X>`, not a typed `Collection::Array<RealElement>`.
extern "C" void RtGfxd_SceneArraySetSize0(void* a) {
    rtcoll::AsArray<&kSpec_SceneEntry>(a)->SetSize(0);
}

extern "C" void RtGfxd_DrawableArraySetSize0(void* a) {
    rtcoll::AsArray<&kSpec_Drawable>(a)->SetSize(0);
}

extern "C" void RtGfxd_SceneArraySetSizeFill(void* a, int n, const void* fill) {
    rtcoll::AsArray<&kSpec_SceneEntry>(a)->SetSizeFill(n, fill);
}

// Retail 0x1800626a0 — clear one DrawGroup's three per-pass item arrays, then
// zero the group's first qword (`MOV qword ptr [R12],R13` at 0x180062819, with
// R13 still the zero it was XOR'd to at entry). The three arrays sit at
// group+0x1c0 with stride 0x10, which is what RendererDrawGroup::items already
// records.
extern "C" void RtGfxd_DrawGroupClearPasses(Graphics::RendererDrawGroup* group) {
    for (int i = 0; i < 3; ++i)
        rtcoll::AsArray<&kSpec_DrawItem>(&group->_items[i])->SetSize(0);
    group->_renderBuffer = nullptr;                    // the first qword
}

// ============================================================================
// ?Reset@Renderer@Graphics@@QEAAXXZ — retail 0x180105110.
//
// Store order is retail's, and it is not the order a reader would guess: +0x34
// is set to -1 FIRST, before the group loop, and +0x40 and +0x38 are cleared
// LAST, after both arrays. +0x40 is `frameAlpha`, a float, and retail writes it
// with `MOV dword ptr [RBX+0x40],0x0` — an integer store of the zero bit
// pattern, which is +0.0f. It is spelled as a dword here for that reason.
//
// The loop re-reads `_drawGroups._size` from memory on every iteration
// (`CMP EDI,dword ptr [RBX+0x18]` at 0x180105145), which is faithful and also
// harmless: the callee cannot change it.
//
// NOTE WHAT RESET DOES NOT DO. It does not touch `_drawGroups`' own header at
// all — the group objects are cleared in place and the array keeps its size and
// its buffer, so the next frame's BeginDrawGroup reuses them. Only `_scenes` and
// `_drawables` are emptied, and even they keep their capacity.
// ============================================================================

// ============================================================================
// ?AddScene@Renderer@Graphics@@QEAAXPEBVScene@2@PEBVRenderBuffer@Video@@
//                                  AEBVCamera@2@HHHI@Z — retail 0x180105700.
//
// *** THE FOUR STACK ARGUMENTS ARE NOT STORED IN SOURCE ORDER, AND THAT IS THE
// ONE THING IN THIS FUNCTION A READER WILL GET WRONG. *** Read off the four
// `[RSP+0x1xx]` loads at 0x18010572c..0x18010576f:
//
//     arg5 (H, [RSP+0x170])  ->  entry +0xe8
//     arg6 (H, [RSP+0x178])  ->  entry +0xf8      <-- out of order
//     arg7 (H, [RSP+0x180])  ->  entry +0xec      <-- out of order
//     arg8 (I, [RSP+0x188])  ->  entry +0xf0
//
// and +0xf4 is the immediate -1 (the entry's drawGroupIndex, which Draw writes
// back later from Renderer+0x34).
//
// `rec` IS DELIBERATELY UNINITIALISED. Retail builds this record on the stack
// and never writes +0xfc, nor the Camera's +0xd5..+0xd7 tail at +0xe5..+0xe7;
// the assign then copies those uninitialised bytes into the array element. A
// port that zeroed them would be "helpful" and WRONG in the one way that shows
// up as a byte divergence against a DLL-built Renderer
// (reference_oracle_must_not_compare_unwritten_bytes.md, and the same reasoning
// renderer.cpp's Renderer ctor gives for leaving +0x3c and +0x4c alone).
//
// The trailing `~Array<int32>` on `rec+0x100` is retail's (0x18010579c). It is
// not redundant: SetSize's assign copy-constructs a fresh buffer into the array
// element, so the record's own buffer — allocated by the Array's operator= at
// 0x18001cdf0 — has to be released before the record leaves scope.
// ============================================================================

}  // namespace rtgfxd

// ############################################################################
// ENDGAME B.3 step 5 (block F) — THE TWO `Renderer` BODIES, MOVED ONTO MEMBERS
// ############################################################################
//
// `Graphics::Renderer` is a real C++ class as of block F
// (`src/engine/graphics/Camera.h`), so `Reset` and `AddScene` — the two of
// `Renderer::Draw`'s four portable-export callees this lane ported — move onto
// it. The two `Graphics::Camera` CONSTRUCTORS above DO NOT MOVE: RULE B5-4 keeps
// ctor doors as doors, and both bodies are byte-faithful PARTIAL writes (the copy
// ctor leaves +0xd5..+0xd7 alone; the default ctor leaves +0x61..+0x63 and
// +0xd5..+0xd7 alone) that a C++ constructor with an initialiser list cannot
// express. They stay where they are, on the `RtGraphics::Camera` POD, and the
// three `ArraySpec` rows and their four registered entry points stay with them.
//
// NAME-LOOKUP HYGIENE (B34B_RULES §7.2): the `using RtGraphics::Camera;` /
// `Renderer;` declarations above live inside `namespace rtgfxd`, so they are not
// visible here at all; inside `namespace Graphics` a bare `Camera`, `Renderer` or
// `RendererSceneEntry` binds the REAL CLASS. Everything this lane owns is reached
// with an explicit `rtgfxd::` qualifier.
// ############################################################################

namespace Graphics {

// ---------------------------------------------------------------------------
// ?Reset@Renderer@Graphics@@QEAAXXZ — retail 0x180105110.
//
// Store order is retail's, and it is not the order a reader would guess:
// `_currentDrawGroup` is set to -1 FIRST, before the group loop, and `_alphaT`
// and `_drawGroupsUsed` are cleared LAST, after both arrays. `_alphaT` is a float
// and retail writes it with `MOV dword ptr [RBX+0x40],0x0` — an INTEGER store of
// the zero bit pattern, which is +0.0f; `= 0.0f` is the same four bytes.
//
// The loop re-reads `_drawGroups._size` from memory on every iteration
// (`CMP EDI,dword ptr [RBX+0x18]` at 0x180105145), which is faithful and also
// harmless: the callee cannot change it. `GetSize()` is that read.
//
// NOTE WHAT RESET DOES NOT DO. It does not touch `_drawGroups`' own header at
// all — the group objects are cleared in place and the array keeps its size and
// its buffer, so the next frame's `BeginDrawGroup` reuses them. Only
// `_sceneGroups` and `_drawables` are emptied, and even they keep their capacity.
// ---------------------------------------------------------------------------
void Renderer::Reset() {
    _currentDrawGroup = -1;

    for (int32_t i = 0; i < _drawGroups.GetSize(); ++i)
        rtgfxd::RtGfxd_DrawGroupClearPasses(&_drawGroups[i]);

    rtgfxd::RtGfxd_SceneArraySetSize0(&_sceneGroups);
    rtgfxd::RtGfxd_DrawableArraySetSize0(&_drawables);

    _alphaT         = 0.0f;   // retail: MOV dword ptr [RBX+0x40],0x0
    _drawGroupsUsed = 0;
}

// ---------------------------------------------------------------------------
// ?AddScene@Renderer@Graphics@@QEAAXPEBVScene@2@PEBVRenderBuffer@Video@@
//                                  AEBVCamera@2@HHHI@Z — retail 0x180105700.
//
// *** THE FOUR STACK ARGUMENTS ARE NOT STORED IN SOURCE ORDER, AND THAT IS THE
// ONE THING IN THIS FUNCTION A READER WILL GET WRONG. *** Read off the four
// `[RSP+0x1xx]` loads at 0x18010572c..0x18010576f:
//
//     arg5 (H, [RSP+0x170])  ->  entry +0xe8    `_shaderDetail`
//     arg6 (H, [RSP+0x178])  ->  entry +0xf8    `_detailId`          <-- swapped
//     arg7 (H, [RSP+0x180])  ->  entry +0xec    `_shader`            <-- swapped
//     arg8 (I, [RSP+0x188])  ->  entry +0xf0    `_materialTypeFlags`
//
// and +0xf4 (`_drawGroupIndex`) is the immediate -1, which `Draw` writes back
// later from `Renderer::_currentDrawGroup`. The C++ parameter list below is
// spelled in RETAIL'S DECLARATION ORDER (…, shaderDetail, detailId, shader,
// materialTypeFlags) so the door is a straight pass-through and the swap stays
// visible exactly once, here, where the assignments are.
//
// THE RECORD IS DELIBERATELY NOT ZEROED. Retail builds this record on the stack and
// never writes +0xfc, nor the Camera's +0xd5..+0xd7 tail (which lands at
// +0xe5..+0xe7); the assign then copies those uninitialised bytes into the array
// element. A port that zeroed them would be "helpful" and WRONG in the one way
// that shows up as a byte divergence against a DLL-built Renderer
// (reference_oracle_must_not_compare_unwritten_bytes.md, and the same reasoning
// renderer.cpp's Renderer ctor gives for leaving +0x3c and +0x4c alone). B.4 S4
// GFX2: the record is a `RendererSceneEntry` built by retail's SceneGroup ctor
// (0x1800628d0), whose Camera member is built by Camera's copy ctor (retail's
// 0x180017a70), so no zeroing default ctor runs and the holes stay holes.
//
// The trailing `~Array<int32>` on the record's `_drawableIds` is retail's
// (0x18010579c). It is not redundant: `SetSizeFill`'s assign copy-constructs a
// fresh buffer into the array ELEMENT, so the record's own buffer — allocated by
// the inner Array's `operator=` at 0x18001cdf0 — has to be released before the
// record leaves scope.
// ---------------------------------------------------------------------------
void Renderer::AddScene(const Scene* scene, const Video::RenderBuffer* renderBuffer,
                        const Camera& camera, int shaderDetail, int detailId,
                        int shader, uint32_t materialTypeFlags) {
    // B.4 S4 GFX2: the stack record is a real local, built by retail's SceneGroup
    // constructor (0x1800628d0, which AddScene inlines: the same stores in the
    // same order, Camera.h) — +0xfc and the Camera's +0xd5..+0xd7 tail stay
    // unwritten, because neither has an initialiser.
    RendererSceneEntry rec(scene, renderBuffer, camera, shaderDetail, detailId,
                           shader, materialTypeFlags);

    rtgfxd::RtGfxd_SceneArraySetSizeFill(&_sceneGroups, _sceneGroups.GetSize() + 1, &rec);
    // retail 0x18001c750 — the record's own Array<int32> header, freed by its
    // implicit destructor (`_drawableIds` is the only member with one).
}

}  // namespace Graphics

// ============================================================================
// REGISTRATION
//
// FOUR EXPORTS and ELEVEN ADDRESSES. Every mangled name is spelled as a
// LITERAL, never through a named constant: tools/runtime_census.py matches
// `RegisterRuntimeExport("<lit>"` and cannot see through a constant, which is
// how Wave 3's Object count read 21/41 when it was really 27/41
// (reference_census_cannot_see_nonliteral_registrations.md).
//
// EVERY ONE OF THE ELEVEN ADDRESSES WAS CHECKED FOR AN EXISTING REGISTRATION
// FIRST. A duplicate is last-binding-wins and silently turns one of the two
// bodies into dead code, which review does not catch and a diff table does
// (reference_grep_for_the_port_before_writing_it.md). All eleven came back with
// zero `RegisterRuntime*` hits across runtime/, standalone/, code/, engine/ and
// src/render/dx11/; the only pre-existing mentions are five COMMENTS, listed in
// WAVE9_LANE_GFXD.md §5.
//
// THESE REGISTRATIONS ARE INERT TODAY and that is stated rather than implied.
// No door in the tree resolves any of the four names or any of the eleven
// addresses, so nothing changes in a live run and every site's `calls=` is 0.
// They are what a lane porting `Draw` needs to exist, and they are cheap
// insurance in the sense lane GFXR's limit 3 describes — NOT evidence of
// anything, and they must not be read as closing a residue row.
//
// ONE OF THE ELEVEN IS SOMEONE ELSE'S PROOF. 0x18001ce90 is `FreeInnerArraysCore`
// with lane DESTROY's kD_InnerArr_110 — the spec row written for THIS address
// (array_destroy.cpp:255) — and array_destroy_probe.cpp:729 already drives that
// exact pairing against the shipped body. Lane DESTROY verified it and
// registered only 0x18001c7e0, the `~Array` that calls it. This lane registers
// the callee, and does not re-prove what that probe proved.
// ============================================================================
