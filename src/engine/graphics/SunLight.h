#pragma once
// ============================================================================
// src/engine/graphics/SunLight.h — Graphics::SunLight   Wave 9 lane GFX2
// ============================================================================
//
// Recovered from the three shipped bodies that write the class:
//
//   0x1801081f0  ??0SunLight@Graphics@@QEAA@XZ                        (ctor)
//   0x180107f10  ?Set@SunLight@Graphics@@QEAAXAEBVVector3@Math@@AEBVVector4@4@1@Z
//   0x180108160  ??1SunLight@Graphics@@UEAA@XZ                        (dtor)
//
// SIZE. 0x108, from `MemAlloc(0x108, 8)` at the two exe call sites that build
// one (src/game/Game/menudialogloopt.cpp:733 and
// src/game/Game/gameupdateloop_load.cpp:1209). Consistent with the last field
// the ctor writes, `+0x100`.
//
// THE INHERITANCE CHAIN, and why `Light` contributes no bytes. The ctor calls
// `Graphics::Geometry::Geometry` (0x1800a4270) and then stamps TWO vtables into
// the SAME slot, back to back:
//
//   1801081f0+0x26  LEA RAX,[0x180158d30]  MOV [RDI],RAX   ; ??_7Light@Graphics@@6B@
//   1801081f0+0x30  LEA RAX,[0x180158e10]  MOV [RDI],RAX   ; ??_7SunLight@Graphics@@6B@
//
// Both addresses were confirmed against the shipped export table
// (tools/pe_exports.py: ord for ??_7Light@Graphics@@6B@ = 0x180158d30,
// ??_7SunLight@Graphics@@6B@ = 0x180158e10). One slot, single inheritance, and
// the next store is at +0xC0 — which is `sizeof(Geometry)`. So `Graphics::Light`
// adds a vtable override and NO data members, and SunLight's own fields start
// immediately at the end of Geometry.
//
// +0xEC IS A HOLE AND IS NEVER WRITTEN BY ANY OF THE THREE BODIES. The ctor
// goes +0xe8 -> +0xf0 and skips it; `Set` writes +0xc0..+0xe8 and stops. It is
// almost certainly the tail padding of the 12-byte `_ambient`/16-byte alignment
// pair. Any oracle over this class MUST exclude it
// (reference_oracle_must_not_compare_unwritten_bytes.md) — and the probe in
// sunlight.cpp does.
//
// A CORRECTION TO WAVE9_LANE_GFX.md §6.5, which named the two buffers the other
// way round ("depth ImageBuffer* +0xf0, colour ImageBuffer* +0xf8"). Read off
// the two `ImageBuffer::Create` argument tuples plus RenderBuffer::Create's own
// parameter order:
//
//   +0xf0  Create(ImageType=0, Usage=0, Format=5, ...)   Usage 0 -> render target
//   +0xf8  Create(ImageType=0, Usage=1, Format=7, ...)   Usage 1 -> DEPTH
//   RenderBuffer::Create(rb, +0xf0, +0xf8) and RenderBuffer's own layout is
//   {colorBuffer @ +0x00, depthBuffer @ +0x08} (src/engine/video/Device.h)
//
// so +0xf0 is the COLOUR target and +0xf8 is the DEPTH target. That ordering is
// also what makes the capability gate make sense: the colour target is the
// OPTIONAL one (see sunlight.cpp's note on DeviceData slot 6) and the depth
// target is built unconditionally, which is how every shadow-map path works.
// ============================================================================

#include <cstdint>
#include <cstddef>

#include "Geometry.h"
#include "Light.h"      // ENDGAME B.3.5 block E: Graphics::Light, the real base
#include "../object/Internal.h"   // ENDGAME B.3.8 RB: InternalMemFree (the concrete slot 0)





// ############################################################################
// ENDGAME B.3 step 5 (block E) — `Graphics::SunLight`, the REAL C++ class
// ############################################################################
//
// `??_7SunLight@Graphics@@6B@` @ 0x180158e10, ELEVEN slots, sizeof 0x108,
// parent `Graphics::Light` (B35_SLOTS.md; the parent edge is the one this
// file's own banner above derived independently from the two back-to-back
// vtable stamps in the constructor):
//
//   [0] +0x00  0x180064620  (unexported) vector deleting destructor
//   [1] +0x08  0x1801067a0  ?Update@SunLight@…          wave28_lane_partsys.cpp
//   [2] +0x10  0x180106790  ?Draw@…MHHH@Z               wave28_lane_partsys.cpp
//   [3] +0x18  0x180106f50  ?AddDrawItems@…             wave28_lane_partsys.cpp
//   [4] +0x20  0x1800a4020  Geometry::UpdateAttached         inherited
//   [5] +0x28  0x1800a43c0  Geometry::GetSubTransform        inherited
//   [6] +0x30  0x1800154a0  Geometry::GetInstanceTransform   inherited
//   [7] +0x38  0x180015590  Geometry::SetSharedInstanceData  inherited
//   [8] +0x40  0x1800a4420  Geometry::Clean                  inherited
//   [9] +0x48  0x180106bc0  ?DrawShadowMap@…            lights_vtable.cpp
//  [10] +0x50  0x1801067b0  ?Draw@…HHHH@Z  -> DrawLight lights_vtable.cpp
//
// The POD above keeps its name and its users; this class describes the same
// bytes with `Light` as a real base and is cross-asserted against it field for
// field. The field NAMES are the POD's, deliberately: the bodies in
// `sunlight.cpp`, `sun_camera.cpp` and `wave28_lane_partsys.cpp` read
// `_dirX` / `_sunR` / `_ambientA` and move onto these members unchanged.
// ############################################################################

namespace Graphics {

class SunLight : public Light {
public:
    SunLight(const SunLight& src);                 // 0x180064410  not ported
    SunLight& operator=(const SunLight& src);      // 0x180064510  not ported

    // ??0SunLight@Graphics@@QEAA@XZ 0x1801081f0 — the REAL constructor
    // (ENDGAME B.3.8 block RB, B38_RULES D1), PUBLIC: the exe instantiates the
    // class itself (table copy 0x14015e290, MenuScene / MainMenuDialog,
    // `MemAlloc(0x108)`). Geometry(), the Light stamp and the SunLight stamp
    // are the compiler's; InitFields is the rest of retail's body. ENDGAME
    // B.3.9 pilot: the ??0/??1 doors and `DoorBody` are gone -- the exe's one
    // builder spells `::new (p) Graphics::SunLight()` (B39_RULES D1).
    SunLight() : _direction(0.0f, 0.0f, -1.0f), _sun(1.0f, 1.0f, 1.0f, 1.0f),
                 _ambient(0.0f, 0.0f, 0.0f, 1.0f) { InitFields(); }

    // [0] CONCRETE: the exe copy's slot 0 is its scalar deleting dtor
    // 0x140019e40 (~SunLight, MemFree on flags & 1) -- the table every SunLight
    // retail builds actually wears (only the exe calls ??0SunLight), so the
    // arms are the SCALAR ones (B39_RULES D1: the arms of the table the object
    // wore). The DLL's vector row 0x180064620 is retired with the mint row.
    // 0x180108160: restamp (the compiler's), the two image buffers and the
    // render buffer (ReleaseFields), then ~Light (empty) and ~Geometry — the
    // compiler's chain, retail's tail call.
    ~SunLight() override { ReleaseFields(); }
    void Update(const System::Time& time) override;                      // [1] 0x1801067a0
    void Draw(Video::Context& context, const Camera& camera,
              ExternalPtrHandle material,
              float opacity, int a, int b, int c) const override;         // [2] 0x180106790
    void AddDrawItems(const Scene& scene, Renderer& renderer) const override; // [3] 0x180106f50
    void DrawShadowMap(Video::Context& context, Renderer& renderer,
                       int a, int b) const override;                       // [9] 0x180106bc0
    void DrawLight(Video::Context& context, Renderer& renderer,
                   int a, int b, int c, int d) const override;             // [10] 0x1801067b0

    // `?Set@SunLight@Graphics@@QEAAXAEBVVector3@Math@@AEBVVector4@4@1@Z`
    // @ 0x180107f10 — PORTED, body in `src/engine/graphics/sunlight.cpp`.
    // The mangled name's trailing `1` is a back-reference to the SECOND
    // parameter's type, so arguments two and three are both
    // `const Math::Vector4&`.
    void Set(const Math::Vector3& direction, const Math::Vector4& sun,
             const Math::Vector4& ambient);

    // `?SetDirection@SunLight@Graphics@@QEAAXAEBVVector3@Math@@@Z`
    // @ 0x180107ee0 — exported, NOT PORTED, no caller. Declared, undefined.
    void SetDirection(const Math::Vector3& direction);

    // `?GetSunCamera@SunLight@Graphics@@QEBA?AVCamera@2@AEAVRenderer@2@@Z`
    // @ 0x180107010 is PORTED (`src/engine/graphics/sun_camera.cpp`) and is a
    // HIDDEN-RETSLOT export: it returns `Graphics::Camera` — 0xd8 — BY VALUE
    // (B35_RULES §2.5).
    //
    // ENDGAME B.3.5 BLOCK F added this line and moved the body onto it. Block E
    // recorded the declaration as a comment because a by-value return needs the
    // COMPLETE type and `Graphics::Camera` was still a POD; the class exists now
    // and `Light.h` (included above) includes `Camera.h`. NON-VIRTUAL, so the
    // by-value return is correct C++; the door keeps retail's
    // `(this, ret, renderer)` order and placement-news the prvalue.
    Camera GetSunCamera(Renderer& renderer) const;   // 0x180107010

    // EXT SEAM (NOT RETAIL, docs/extensions/handins/EYECAM.md §9): the engine end of ext.eye_camera's shadow-fit view.
    // Null always, except for the span
    // of the one Renderer::Draw call the eye camera makes in its seam at
    // GameUpdateLoop::Draw step 7 (the selection is there; src/engine does not
    // include src/ext). While set, AddDrawItems fits the shadow camera to this
    // view instead of draw group 0's camera: GetSunCamera's fit assumes a view
    // that looks DOWN at the ground (its box half-size is scaled by
    // sqrt(|forward.z|) and an upward corner ray is extruded 1000 units), so a
    // level eye-height view shrinks the box to nothing around the eye. A data
    // pointer, not a hook: no indirect call, GetSunCamera itself unchanged.
    static const Camera* _extShadowFitView;

    // EXT SEAM (NOT RETAIL, ext.camera_angle, docs/extensions/handins/CAMERAANGLE.md section 6): null by
    // default and under the faithful profile; otherwise src/ext/registry_boot.cpp points it at the row's
    // live bool. While it points at true, GetSunCamera's box is as deep along the sun as the view's
    // sphere needs (retail: a fixed 256), so far ground on the side away from the sun is shadow-tested
    // instead of reading as shadowed past the far plane. A view that fits retail's box is unchanged.
    static const bool* _extDeepSunBox;

    // EXT SEAM (NOT RETAIL, fix.shadow_zoom_pulse, docs/retail_bugs.md section 33): null by default and
    // under the faithful profile; otherwise src/ext/registry_boot.cpp points it at the fix's bool. While
    // it points at true, GetSunCamera rounds the box's half size up to ShadowZoom::SteadyHalf's ladder, so
    // a zoom does not move the shadow grid on every frame.
    static const bool* _extSteadySunBox;

    // EXT SEAM (NOT RETAIL, docs/extensions/handins/FAA-SHADOW.md): the engine end of ext.max_shadow_res
    // (FOLIAGE_AA.md §7). Null by default
    // (and always under the faithful profile); otherwise src/ext/registry_boot.cpp
    // points it at the row's live bool. While it points at `true` and the retail
    // "Shadow Resolution" combo is at High (`Configuration::shadowScalar == 0`),
    // the sun map's raw size is 0x1000 instead of retail's 0x800; Medium / Low
    // (scalar 1 / 2) keep 0x800 and so keep 1024 / 512. Width() and the
    // RenderBuffer extent shift the raw size as before, so the PCF texelSize
    // (1 / Width()) and the texel snap follow. A data pointer, not a hook.
    static const bool* _extMaxShadowRes;
    // NOT RETAIL -- the size half of the same seam (ext.max_shadow_size, FAA-SHADOW2): the raw
    // size the row builds at High, 4096 or 8192 (anything else reads as 4096). Null by default and
    // under the faithful profile; read only while `_extMaxShadowRes` points at true. An 8192 map
    // the device cannot create falls back to 4096 (one stderr line; the key is left as set).
    static const int32_t* _extMaxShadowSize;
    // NOT RETAIL -- ext.max_shadow_zoom (SHADOWZOOM.md section 6, graphics/shadow_zoom.h): 1 = the
    // casters are drawn into a corner of the map sized to the view (full size up close, half when
    // zoomed out at the default tilt), 0 = the whole map. Null by default and under the faithful
    // profile; read only while `_extMaxShadowRes` points at true.
    static const int32_t* _extShadowZoom;
    // The shift GetSunCamera snaps with and AddDrawItems scales the sun projection by (0 = the
    // whole map, retail). 0 unless the row and the key are on. Main thread (pass 1).
    static int32_t ExtShadowZoomShift();

    // EXT SEAM (NOT RETAIL, issue #253): the engine end of ext.staggered_passes' shadow hold
    // (graphics/stagger_hold.h). Null by default and while the row is off (retail: the map is
    // rendered every frame); otherwise src/ext/staggered_passes/ points it at
    // ext.staggered_passes_shadow (1 .. 4). While it points at N > 1, AddDrawItems may hold the
    // map for up to N - 1 frames on a near-still view: it still adds the light and opens the
    // shadow draw group, with the sun camera the held map was rendered with as its cull camera
    // (DrawLight builds the sampling matrix from it), and gathers no casters. Main thread.
    static const int32_t* _extStaggerEvery;
    // Main thread: whether the Draw whose pass 1 just ran held the map, and reset. The pass-2
    // side passes it to ExtShadowHoldEmit: the faithful Renderer::Draw directly, ext.render_pipe
    // through its Packet (the render thread may still be emitting the previous frame).
    static bool ExtTakeShadowHeld();
    // The thread running pass 2: true while the frame being emitted holds the map, so
    // DrawShadowMap neither clears nor redraws it. Thread-local: render_pipe emits on its own
    // thread while the main thread runs the next pass 1.
    static void ExtShadowHoldEmit(bool held);

    // NOT RETAIL (ext.max_shadow_res): the row's toggle, called by the registry between frames on the
    // main thread. Runs the same descriptor rewrite + Device::settingsChanged as the per-frame sync
    // below, so the rebuild is the one the next frame decision already treats as sequential. Left to
    // the per-frame sync alone (inside AddDrawItems), a render_pipe frame raised it after that decision
    // and the render thread ran DeviceData_Resize (a window move) -> deadlock (FAA-MENU).
    // Raises settingsChanged without touching descriptors when no sun light has drawn yet.
    static void ExtRequestMaxShadowResSync();
#if BANISHED_TWEAK_OVERLAY
    // The debug tweak overlay's stats line (debug flavour only): the built sun map's width the
    // last `[ShadowRes]` line reported (0 = none yet). Any thread.
    static int32_t ExtTweakSunMapSize();
#endif

private:
    void InitFields();      // sunlight.cpp
    void ReleaseFields();   // sunlight.cpp
    // NOT RETAIL (ext.max_shadow_res): called from AddDrawItems only while
    // `_extMaxShadowRes` is set. When the raw size the row and the combo ask for
    // differs from the map's, it rewrites the two descriptors and raises
    // Device::settingsChanged: the next BeginCommands runs retail's own rebuild
    // (DeviceData_Resize -> ReleaseDynamic / RecreateDynamic), the path a
    // combo change and a device reset already take.
    // `request` = may rewrite the descriptors and raise Device::settingsChanged: only the
    // main-thread request sites pass true (a raise from AddDrawItems on the render thread
    // deadlocked render_pipe twice in FAA-MENU); the per-frame call records and logs only.
    void ExtSyncMaxShadowRes(bool request) const;   // sunlight.cpp

public:
    // The three `?Set@SunLight@` arguments (0x180107f10). Retail's ctor
    // (0x1801081f0) stores them FIRST after the two stamps, before +0xF0:
    // that is the mem-initialiser list of SunLight() above, with the values
    // retail stores; the dtor (0x180108160) does not touch them.
    Math::Vector3 _direction;   // +0xC0  ctor (0, 0, -1)
    Math::Vector4 _sun;         // +0xCC  ctor (1, 1, 1, 1)
    Math::Vector4 _ambient;     // +0xDC  ctor (0, 0, 0, 1)
    uint32_t _padEC;          // +0xEC  NEVER WRITTEN by ctor, Set or dtor
    // B.4: the three pointers carry their classes (the bytes are unchanged).
    Video::ImageBuffer*  _colourTarget;  // +0xF0  (capability-gated)
    Video::ImageBuffer*  _depthTarget;   // +0xF8  (always built)
    Video::RenderBuffer* _renderBuffer;  // +0x100
};

static_assert(sizeof(SunLight) == 0x108,
              "Graphics::SunLight is 0x108 bytes (both exe call sites MemAlloc "
              "exactly 0x108, and the deleting dtor 0x180064620 agrees)");
static_assert(alignof(SunLight) == 8, "qword aligned (the vptr)");


static_assert(offsetof(SunLight, _direction)    == 0xC0,  "SunLight::_direction @ +0xC0 (??0 0x1801081f0; .z = -1.0f @ +0xC8)");
static_assert(offsetof(SunLight, _sun)          == 0xCC,  "SunLight::_sun @ +0xCC (Set arg 2)");
static_assert(offsetof(SunLight, _ambient)      == 0xDC,  "SunLight::_ambient @ +0xDC (Set arg 3; .w @ +0xE8)");
static_assert(offsetof(SunLight, _padEC)       == 0xEC,  "SunLight::+0xEC is never written by any of the three bodies");
static_assert(offsetof(SunLight, _colourTarget) == 0xF0,  "SunLight::_colourTarget @ +0xF0 (Usage 0 = render target)");
static_assert(offsetof(SunLight, _depthTarget)  == 0xF8,  "SunLight::_depthTarget @ +0xF8 (Usage 1 = depth)");
static_assert(offsetof(SunLight, _renderBuffer) == 0x100, "SunLight::_renderBuffer @ +0x100");

}  // namespace Graphics
