// ============================================================================
// src/engine/graphics/animation.cpp — Wave 6 lane G
// Graphics::AnimationEvent + Graphics::AnimationManager + Graphics::Particle +
// Graphics::ParticleInstance + Graphics::ParticleSystemInstance +
// Graphics::FrameCopyImageBuffer
// ============================================================================
//
// Scope: docs/history/runtime_port/wave6_lanes.txt "=== LANE G ===", 18 rows (15 code
// functions + 3 data symbols this lane cannot register -- see the bottom of
// this file). Layouts and the evidence for every offset are in
// src/engine/graphics/Animation.h and Particle.h; this file's own header
// comments carry the per-function oracle argument.
//
//   | Export                                    | Retail      | Ord  | Status   | Oracle          |
//   |--------------------------------------------|-------------|------|----------|-----------------|
//   | ??0AnimationEvent@Graphics@@QEAA@XZ         | 0x1800a2150 |   41 | PORTED   | FULL (scratch)  |
//   | ??1AnimationEvent@Graphics@@UEAA@XZ         | 0x1800a2120 |  526 | PORTED   | structural only |
//   | ??0AnimationManager@Graphics@@QEAA@XZ       | 0x1800fc430 |   45 | PORTED   | FULL (scratch)  |
//   | ??1AnimationManager@Graphics@@QEAA@XZ       | 0x1800fc400 |  528 | PORTED   | post-state only |
//   | ?Update@AnimationManager@Graphics@@...      | 0x1800fc240 | 4473 | HELD BACK | -- see below   |
//   | ?Serialize@AnimationManager@Graphics@@...   | 0x1800fc190 | 3623 | HELD BACK | -- see below   |
//   | ??0Particle@Graphics@@QEAA@XZ               | 0x180103e80 |  298 | PORTED   | FULL (scratch)  |
//   | ??0ParticleInstance@Graphics@@QEAA@XZ       | 0x180104130 |  300 | PORTED   | FULL (scratch)  |
//   | ?Update@ParticleInstance@Graphics@@...      | 0x180104180 | 4498 | PORTED   | idempotent replay|
//   | ?IncrementInstanceIndex@ParticleInstance@...| 0x1800201f0 | 3065 | PORTED   | FULL             |
//   | ??0ParticleSystemInstance@Graphics@@...     | 0x1800a55a0 |  304 | PORTED   | FULL (scratch)  |
//   | ??0FrameCopyImageBuffer@Graphics@@QEAA@XZ   | 0x1800a3d10 |  198 | PORTED*  | see correction |
//   | ??1FrameCopyImageBuffer@Graphics@@UEAA@XZ   | 0x1800a3c80 |  597 | PORTED*  | see correction |
//   | ?Create@FrameCopyImageBuffer@Graphics@@...  | 0x1800a3b70 | 1877 | PORTED*  | see correction |
//   | ?Generate@FrameCopyImageBuffer@Graphics@...  | 0x1800a3b20 | 2395 | PORTED*  | see correction |
//
// *** CORRECTION (2026-09-12, vtable batch 6; PLAN.md §6 step 8). *** The four
// FrameCopyImageBuffer rows above said HELD BACK and the long "WHY 6 OF 15 CODE
// FUNCTIONS ARE HELD BACK" section below still argues for holding them. THAT
// DECISION WAS REVERSED: Wave 23 lane GFXBODY ported and registered all four in
// src/engine/graphics/wave23_lane_gfxbody.cpp, and vtable batch 6 has since
// ported the REST of that class's table (slots 1/2/3/5/6/7/8/11) plus its two
// siblings in src/engine/graphics/imagebuffer_derived.cpp, reaching wave23's
// ctor through the registry door. The four rows are marked PORTED* here rather
// than rewritten so that a reader who meets the old claim elsewhere recognises
// it. The prose below is kept as the HISTORICAL record of why the hold was made
// and is NOT a current disposition.
//
// Every name above was checked against the shipped export directory of
// C:\GOG Games\Banished\Runtime-x64.dll with pefile BEFORE any body was
// written (reference_resolveexport_phantom_names.md). All 15 code exports and
// both data exports used internally (Object::External::~External, Graphics::
// Geometry::Geometry/SetBounds) are present; addresses/ordinals above are
// that query's output, not Ghidra's.
//
// WHY 6 OF 15 CODE FUNCTIONS ARE HELD BACK
// -----------------------------------------
// **FrameCopyImageBuffer (ctor, dtor, Create, Generate) -- all four, as one
// block.** This class exists only to grab a screenshot
// (src/game/Game/w2_takescreenshot.cpp). Its ctor/Create/Generate allocate
// and drive `Video::RenderBuffer` and `Video::ImageBuffer` objects through
// VIRTUAL CALLS into vtables this lane does not own -- `Video::Device` /
// `Video::Context` / `Video::RenderBuffer` are lanes A/B/C's scope this wave,
// and `Video::RenderBuffer::PlatformConstruct` ultimately dispatches into
// `src/render/dx11/`, our OWN DX11 replacement, on the other side. Getting the RenderBuffer
// layout even one field wrong here would corrupt a live GPU resource object
// two other lanes' code also touches this same wave -- exactly the
// "half-ported class" hazard PLAN §4.1 decision 5 names, except worse: the
// half this lane would be porting is entangled with THREE other lanes'
// in-flight layouts, not the DLL's own dormant internals. There is also no
// memcmp oracle for "did the GPU texture get copied correctly" -- only a
// screenshot pixel comparison would tell, which is outside RT_DIFF's model.
// RE-CHECKED IN THE WAVE 8.5 CLOSE-OUT and still held, but the FRAMING above
// has aged badly and would mislead the next reader: "lanes A/B/C's scope this
// wave" reads as resolved now that Wave 6 has closed with 0 divergences. The
// entanglement did NOT resolve -- `Video::RenderBuffer`'s ctor/dtor and
// `Video::Context`'s ctor are still individually unported and held back by
// name in src/engine/video/context.cpp and device.cpp. The GPU-oracle
// argument above is the load-bearing one and is unaffected either way.
// See docs/history/runtime_port/WAVE8_5_HOLDBACK_RECHECK.md.
// Held back as a whole class rather than split, for the same reason
// WAVE5_LANE_D.md gives for NOT splitting Picking::Scene: a method of ours
// and a method of the DLL's operating on the same RenderBuffer must agree on
// every byte of a layout this lane was never asked to pin.
//
// **AnimationManager::Update and ::Serialize.** Both mutate through a nested,
// unexported `AnimationState` record (0x30-byte groups of them, in turn
// containing 0x48-byte `AnimationState` elements referencing `Graphics::
// GraphicsMesh::Node` arrays -- lane E's scope this wave -- and an untyped
// `InterpolatedState<Transform>*`) that this lane did not have budget to
// fully pin. Update is also the single largest blast-radius function in this
// lane's whole scope: it runs on EVERY character's skeleton, EVERY frame
// (gameupdateloop.cpp:1293), and already sits behind a default-on non-retail
// batching path (src/ext/render_culling/render_culling.cpp, ANIM_BATCH_ALL) this
// lane's own brief said to read before touching it. A wrong argument-slot
// mapping in the 9-argument `AnimationState::Update` call this function makes
// would silently corrupt every animated model's pose with no oracle cheaper
// than full visual QA. Serialize is a save-path function serializing the same
// unpinned AnimationState array -- Wave 5 held back every serializer-
// constructing function for want of an oracle
// (reference_slot3_wrapper_must_resolve_first.md: a serializer that consumes
// the wrong byte count desyncs the stream and surfaces LATER on an innocent
// record), and this lane applies the same standard. Both stay on the DLL;
// their exe dispatch slot (0x140152050) and ResolveExport call site
// (gameupdateloop_serialize.cpp:163) are UNCHANGED by this lane.
//
// WHAT THIS LEAVES SAFELY PORTABLE
// ---------------------------------
// The five ctors, the two dtors that don't touch Video, IncrementInstanceIndex
// (a one-line static counter bump) and ParticleInstance::Update (whose only
// external dependency, Graphics::Geometry::SetBounds, is EXPORTED, pure, and
// already fully decompiled -- no unexported internal left unpinned). These are
// exactly the functions where this lane could verify every byte the body
// touches.
// ============================================================================

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>

#include "graphics/Animation.h"
#include "graphics/AnimationManager.h"   // B.3.9: AnimationManager::_singleton
#include "graphics/Particle.h"
// ENDGAME B.3.5 block H2: the real `Graphics::ParticleInstance` (block E's
// class), needed because the two non-constructor particle bodies in this file —
// `ParticleInstance::Update` and `ParticleInstance::IncrementInstanceIndex` —
// are now its MEMBERS. The header was declared for them by block E and needed no
// edit. It reaches `Geometry.h` and therefore `Volume.h`, which is safe HERE for
// the reason this file forward-declares `ResolveAddr` instead of including
// `standalone/resolve.h`: no `<windows.h>` is in this translation unit, so
// `Volume::Rectangle` does not collide with `<wingdi.h>`'s `Rectangle`.
#include "graphics/ParticleInstance.h"
#include "collection/Collection.h"
#include <new>
#include "system/MemoryAllocator.h"

// ENDGAME B.3.5 block H1 — the GREP HAZARD this file now lives with. The two
// `using` lines above bind the PODs at FILE scope, but inside the
// `namespace Graphics { ... }` block at the bottom of this file a bare
// `AnimationEvent` / `AnimationManager` binds the REAL CLASS instead. Every POD
// spelling inside that block is qualified `RtGraphics::` for exactly that
// reason (B34_BRIEFS section A.7, RULE B4b-5).
using RtGraphics::ParticleResource;
using RtGraphics::ParticleBoundsRecord;

namespace {

// ENDGAME B.3.9 block GFX: `RtParticleInstanceVtable` /
// `RtParticleSystemInstanceVtable` (the two minted tables, rebased, with lazy
// function-local caches) and `RtGeometryCtor` / `kGeometryCtor` / `kPIctor` /
// `kPSIctor` are gone with the two ctor doors below.

// ENDGAME B.3.9 block GFX (D7 row A44): `RtAnimMgrSingletonSlot` (the lazy
// by-address cache of 0x1801fe7b0) is gone -- the cell is the class static
// `Graphics::AnimationManager::_singleton`, defined at the end of this file.

// ENDGAME B.3.9 block GFX (D7 row A37): `RtFrameIndexSlot` (a lazy
// function-local ResolveExport cache) is gone -- the cell is the class static
// `Graphics::ParticleInstance::_frameIndex`, defined at the end of this file
// and redirected onto the mint's row by RtGraphicsPortInit.

// Graphics::Geometry::SetBounds(const Volume::Box&) -- retail 0x180015700,
// ordinal 3836, EXPORTED. Also not this lane's class; called at its own
// export from ParticleInstance::Update below, exactly as retail does.
// ENDGAME B.3.5 caller batch: `Graphics::Geometry::SetBounds` is a DEFINED member
// (scene.cpp:1373), so the `SetBoundsFn` typedef and this wrapper are GONE and
// `ParticleInstance::Update` calls the member on its own Geometry base. The door
// stays registered in scene.cpp for callers outside that file.

}  // namespace

// ============================================================================
// ??0AnimationEvent@Graphics@@QEAA@XZ 0x1800a2150 / ??1AnimationEvent@Graphics@@
// UEAA@XZ 0x1800a2120 -- the two DOORS are gone (ENDGAME B.3.9 block GFX,
// B39_RULES D1). Retail's ctor is External() inlined plus the stamps and
// nothing else; its dtor is the restamp plus a tail jump into ~External. Both
// are the compiler's work on the real protected `AnimationEvent()` and the
// empty `~AnimationEvent()` below. Their callers were the retired RT_DELDTOR
// row 0x180011800 and nothing else (the exe's AnimAudioEvent uses the C++
// base ctor since B.3.8 VIS).
// ============================================================================
// ============================================================================
// Graphics::AnimationManager::AnimationManager()
// retail 0x1800fc430, ordinal 45
//
// Zeroes a CRITICAL_SECTION-shaped 0x14-byte prefix (never actually
// initialised as a real lock -- see Animation.h) then seeds a 624-entry
// Mersenne-Twister state array from `_time64(NULL)` using the textbook
// `init_genrand` recurrence, and finally publishes `this` into the exported
// `_singleton`.
//
// ORACLE: full, scratch-object -- with one necessary exception. `_time64`
// advances between the two calls (this port's and the diff run's), so the
// FIRST rngState word (the raw time seed) and everything the recurrence
// derives from it will legitimately differ call to call. The oracle
// therefore reproduces retail's own recurrence bit-for-bit using OUR seed
// fed into it (not comparing the seed itself), which is the only thing this
// function actually computes -- the MT recurrence's correctness, independent
// of what time it happened to run.
// ============================================================================
//
// B.4 wave Y (RT-GFX): the body is the real constructor. Retail's stores:
// qword 0 at +0x00 and +0x08 (`_instances`' Array(), three named stores, H7),
// dword 0 at +0x10, `_time64` into +0x14, the 623-step recurrence, then
// `MOV [0x1801fe7b0],RBX`. +0x9d4 is never written (no initialiser names it).
namespace Graphics {
AnimationManager::AnimationManager() : _instances() {
    _rngIndex = 0;

    const uint32_t seed = static_cast<uint32_t>(std::time(nullptr));
    _rngState[0] = seed;
    for (int i = 1; i < 624; ++i) {
        const uint32_t prev = _rngState[i - 1];
        _rngState[i] = (prev ^ (prev >> 30)) * 0x6c078965u + static_cast<uint32_t>(i);
    }

    _singleton = this;   // MOV [0x1801fe7b0],RBX -- publish the instance
}
}  // namespace Graphics

extern "C" Graphics::AnimationManager* RtGraphics_AnimationManager_Ctor(Graphics::AnimationManager* self) {
    return ::new (static_cast<void*>(self)) Graphics::AnimationManager();
}

// ============================================================================
// Graphics::AnimationManager::~AnimationManager()
// retail 0x1800fc400, ordinal 528
//
// Nulls `_singleton`, then calls FUN_18005bf60(this) treating `this` as an
// LPCRITICAL_SECTION -- a defensive teardown against a lock this class never
// actually calls InitializeCriticalSection on (DebugInfo is always null, so
// the only live branch inside FUN_18005bf60 -- freeing DebugInfo's block --
// never fires). Called at its own retail address (unexported, Win32-shaped,
// PLAN §4.1 decision 1) rather than reproduced.
//
// ORACLE: post-state only. A second run would renull an already-null
// singleton and re-run the no-op lock teardown -- harmless, but nothing new
// to observe, so this checks what retail's body must leave behind instead:
// `_singleton` cleared.
// ============================================================================
//
// B.4 wave Y (RT-GFX): retail 0x1800fc400 is `MOV [0x1801fe7b0],0` then a tail
// JMP 0x18005bf60 -- `_instances`' ~Array (its row kSpec_AnimInstance runs the
// 0x18005c550 element destroy, then the free). The "LPCRITICAL_SECTION" reading
// above is the retired one (Animation.h's CORRECTION banner): the destructor is
// the empty-bodied member below, the array its member's implicit destruction.
namespace Graphics {
AnimationManager::~AnimationManager() {
    _singleton = nullptr;   // 0x1801fe7b0 cleared
}
}  // namespace Graphics

extern "C" void RtGraphics_AnimationManager_Dtor(Graphics::AnimationManager* self) {
    self->~AnimationManager();
}

// ============================================================================
// Graphics::Particle::Particle()
// retail 0x180103e80, ordinal 298
//
// Pure stores, no external calls (disassembly confirmed) -- the same
// inlined-base-ctor shape as AnimationEvent.
//
// *** BYTES [0x21,0x24) AND [0x2c,0x30) ARE TWO GAPS RETAIL NEVER WRITES. ***
// Disassembly of 0x180103e80, the full store list:
//   +0x20 byte  = 1     +0x24 dword = 1     +0x28 dword = 0xf
//   +0x30 qword = 0     +0x38 qword = 0     +0x40 qword = 0
// Nothing touches +0x21/+0x22/+0x23 between the +0x20 and +0x24 stores, and
// nothing touches +0x2c/+0x2d/+0x2e/+0x2f between the +0x28 and +0x30 stores.
// Both were MEASURED by diff runs, one at a time, each initially reported as
// a divergent oracle (see the first as calls=1/divergences=1, comparing the
// real object's leftover heap bytes against this file's own 0xCD scratch
// poison in a range NEITHER implementation writes -- ours never wrote there
// either). The second gap was found only after the first fix narrowed the
// comparison to `[0x24,0x48)` and a follow-up run showed THAT range still
// diverging -- the cheaper way to find every gap in one pass is to walk the
// store list once, as above, and exclude every address it skips, rather than
// re-deriving one gap per run. Fixed the same way scene.cpp's Scene ctor
// oracle excludes its own two never-written dwords: the comparison below
// skips both gaps instead of raw-memcmp'ing the whole struct.
//
// ORACLE: full, scratch-object, minus the [0x21,0x24) and [0x2c,0x30) gaps.
//
// ENDGAME B.3.9 block GFX: the DOOR is gone. Its body was already ONE body with
// the real protected `Particle()` (Particle.h, InitFields); its callers were
// the factory interpreter (via ParticleSystem) and nothing else.
// ============================================================================

// ============================================================================
// Graphics::ParticleInstance::ParticleInstance()
// retail 0x180104130, ordinal 300
//
// Calls the EXPORTED, pure Geometry::Geometry(this) (see file banner), then
// stamps its own vtable and zeroes/initialises its three own fields.
//
// *** THE GEOMETRY BASE [0x00, 0xc0) IS DELIBERATELY NOT DIFFED. *** It is
// written by the SAME retail `Geometry::Geometry` export in both "ours" (the
// real object) and "theirs" (the scratch), so it can never genuinely
// diverge -- but `Geometry::Geometry`'s own disassembly (0x1800a4270) leaves
// a real gap at +0x14..+0x17 (a DWORD store to +0x10 followed directly by a
// QWORD store to +0x18), and a raw memcmp there was MEASURED to report a
// false divergence on every call: the real object's pre-existing heap bytes
// there happened to already be zero while this scratch's 0xCD poison filled
// the same untouched bytes with 0xCD. Comparing that region only measures
// which memory arena's leftover content survived, not a behavioural
// difference -- the fix is to not compare bytes neither implementation
// writes, the same rule Particle's ctor oracle above and scene.cpp's Scene
// ctor oracle both follow. `+0xd4` (this struct's own trailing alignment
// pad, after the `_index` dword at +0xd0) is the same kind of gap for OUR
// added fields and is excluded for the same reason.
//
// ORACLE: full over [0xc0, 0xd4) -- the three fields this class actually
// adds (`_resource`, `_boundsPtr`, `_index`). Geometry::Geometry is proven
// pure (no external calls in its own disassembly) so building "theirs" on
// the stack and discarding it remains safe.
// ============================================================================
//
// ENDGAME B.3.9 block GFX: the door is RETIRED. Geometry() and the stamp are the
// compiler's chain of the real protected `ParticleInstance()`
// (ParticleInstance.h), whose body is the three stores; its only builders are
// the two subclasses' ctors (ParticleSystemInstance, the exe's TornadoInstance).

// ============================================================================
// Graphics::ParticleInstance::Update()
// retail 0x180104180, ordinal 4498
//
// Reads `_resource` (+0xc0); if its `_active` byte (+0x20) is set, refreshes
// the cached bounds-record pointer `_boundsPtr` from `_resource->_boxes[
// _index]`. If the resource is active AND that record's per-frame stamp
// (+0x2c) already equals the global `ParticleInstance::_frameIndex`, this
// instance was already refreshed this frame: call `Geometry::SetBounds` with
// the cached record (treated as a `const Volume::Box&`, matching retail's own
// mangled signature) and return false. Otherwise stamp the record with the
// current frame index and return true (the caller, e.g. tornado.cpp's
// TornadoInstance::Update, treats this as "needs its own bounds computed").
//
// ORACLE: idempotent replay, the same shape scene.cpp uses for SetTransform.
// `Geometry::SetBounds` is exported, pure and confirmed idempotent by its own
// decompile (it only writes when the six floats actually differ, and a
// second call with the SAME record is therefore a true no-op) so replaying
// retail's body on the REAL object immediately after ours is safe. What is
// compared is the state BOTH calls must agree on: `_boundsPtr` and the
// record's frame-tag byte. The one thing this cannot independently confirm
// is the boolean return on the branch that DOES advance the tag (the very
// first call in a frame) -- a replay of that call necessarily lands on the
// "already tagged" branch and returns false, so there is no second
// observation of "true" to compare against. That branch's condition is the
// same one the byte-state check exercises on every call, so a clean replay
// across many calls is strong evidence for it, but it is not a proof, and is
// stated here rather than implied.
//
// FAITHFULLY UNGUARDED, per CLAUDE.md "don't fix a faithful crash with a null
// check": retail dereferences `_resource->_active` with NO null check on
// `_resource`, and (on the "needs update" path) writes through `_boundsPtr`
// with no null check either -- if a caller reaches this with a resource that
// was never attached, retail crashes, and so does this port. Neither guard
// is added.
// ============================================================================

// ============================================================================
// Graphics::ParticleInstance::IncrementInstanceIndex()
// retail 0x1800201f0, ordinal 3065
//
// One line: `_frameIndex += 1`. Increments the SAME live static the DLL
// itself reads (RtFrameIndexSlot resolves the export's actual address), so
// this port and any code still reading the DLL's own `_frameIndex` directly
// (there is none in our tree today, but the DLL's internal callers might)
// stay in agreement.
//
// ORACLE: full. Reads the pre-state, runs ours, then runs retail's own body
// AGAIN immediately after (a second incrementing call is not idempotent by
// itself, but comparing "ours advanced it by exactly 1" against "retail also
// advances it by exactly 1" from the same starting point is): both are run
// from independent snapshots restored around the calls so neither call's
// result depends on the other having run first.
// ============================================================================

// ============================================================================
// Graphics::ParticleSystemInstance::ParticleSystemInstance()
// retail 0x1800a55a0, ordinal 304
//
// Re-runs ParticleInstance's own field inits inline (Geometry::Geometry,
// then a transient stamp of ParticleInstance::_vftable_, then the same three
// field writes) before overwriting the vtable with its own and zeroing one
// more field -- the same base-ctor-inlining shape seen throughout this file.
//
// *** MEASURED FALSE DIVERGENCE, SAME CAUSE AS ParticleInstance's, FIXED THE
// SAME WAY. *** A run diffing this ctor raw (whole-struct memcmp) reported
// divergences on every call at +0x14..+0x17 -- inside the Geometry base this
// class does not own, and the exact gap `Geometry::Geometry` itself leaves
// unwritten (see ParticleInstance's ctor above). Both implementations call
// the identical retail `Geometry::Geometry` there, so it can never
// genuinely diverge; the report was this file's own 0xCD scratch poison
// disagreeing with the real object's pre-existing (here: zeroed) heap bytes
// in a region NEITHER writes. Fixed by comparing only the fields this class
// itself sets, exactly as ParticleInstance's ctor now does.
//
// ORACLE: full over [0xc0, 0xd4) (inherited from ParticleInstance) and
// [0xd8, 0xe0) (this class's own `_f_d8`) -- everything this ctor actually
// writes, and nothing it, or Geometry::Geometry beneath it, leaves untouched.
// ============================================================================
//
// ENDGAME B.3.9 block GFX: the door is RETIRED. Retail 0x1800a55a0 is
// `Geometry(); stamp PI; +0xc0/+0xc8 = 0, +0xd0 = -1; stamp PSI; +0xd8 = 0` --
// the real public `ParticleSystemInstance()` (ParticleInstance.h) with
// ParticleInstance() inlined. Its builders -- ParticleSystem::CreateSystem
// (wave28_lane_partsys.cpp) and the exe menu scene (menudialogloopt.cpp) --
// spell `::new (p) Graphics::ParticleSystemInstance()`.

// ############################################################################
// ENDGAME B.3.5 step 5, block H1 — `Graphics::AnimationEvent`'s member bodies.
//
// They sit in their own `namespace Graphics { }` block AFTER every other
// namespace in this file has closed, because C++ requires a member definition
// to appear in a namespace ENCLOSING the class and `Graphics` and `RtGraphics`
// are siblings (B34_BRIEFS section A.2).
//
// WHAT MOVED AND WHAT DID NOT, stated plainly rather than implied:
//
//  * `TypeName()` / `Type()` are MEMBERS THAT FORWARD, not doors that delegate.
//    Their bodies are `RtGraphics_AnimationEvent_TypeName` /
//    `_Type` in `src/engine/graphics/graphics_typeinfo_accessors.cpp`, which is
//    NOT block H1's file set, so the direction is reversed for these two: the
//    registered door is untouched and the member calls it. Exactly the split
//    `src/engine/object/external_class_doors.cpp` documents.
//
//  * `~AnimationEvent()` IS DUPLICATED, NOT DELEGATED, and the door below is
//    left exactly as it was. Retail 0x1800a2120 stamps AnimationEvent's own
//    vtable and then tail-jumps `~External`; a real destructor chain emits both
//    for free, naming GCC's tables. The DOOR must keep naming the MINTED table,
//    because every live `AnimationEvent` in the process was built by the ctor
//    door and wears it — B34_RULES section 4.3's test ("would the member form
//    change which vtable a live object wears?") answers YES, so the two
//    spellings coexist. This is the `Object::External` precedent verbatim
//    (`external_class.cpp:152` beside `external.cpp`'s `RtObject_External_Dtor`).
//
//  * THE CONSTRUCTOR DOOR IS NOT A DELEGATION EITHER (B4b-2). It is called on
//    raw memory by `src/game/Component/animaudioevent.cpp:45`, which then
//    stamps its own subclass table; the class has no real constructor at all.
//
// ENDGAME B.3.9 block GFX: the last two bullets are HISTORY. Both doors are
// retired with the minted table (the exe builds through the real protected
// ctor since B.3.8), so the empty destructor below is the only one.
// ############################################################################

namespace Graphics {

// `AnimationEvent::TypeName` (slot 1, 0x1800116f0) and `::Type` (slot 2,
// 0x1800116d0) USED TO BE DEFINED HERE, forwarding INTO the `extern "C"` doors in
// `graphics_typeinfo_accessors.cpp`. ENDGAME B.3.5 block GC flipped that: the
// member IS the body now, in that file, beside the byte decode that is its
// evidence, and its door is the one-line qualified delegation. Nothing moved out
// of this file except two forwarders.

// ??1AnimationEvent@Graphics@@UEAA@XZ  retail 0x1800a2120.
//
// EMPTY ON PURPOSE. The two instructions retail spends here are the vtable
// stamp (the chain emits it on entry to this destructor) and the tail jump into
// `~External` (the chain emits that on exit). Writing either by hand would name
// the minted table, which is the one thing a converted object must not carry.
AnimationEvent::~AnimationEvent() {}

// ?CreateNewObject@AnimationEvent@Graphics@@SAPEAVBase@Object@@XZ 0x180011700 and
// ?CreateInPlaceObject@AnimationEvent@Graphics@@SAPEAVBase@Object@@PEAX@Z
// 0x180011710: the ABSTRACT factory pair, both `XOR EAX,EAX ; RET` in retail
// (the FACTREST row they replace carried no steps for exactly that reason).
Object::Base* AnimationEvent::CreateNewObject() { return nullptr; }
Object::Base* AnimationEvent::CreateInPlaceObject(void* place) { (void)place; return nullptr; }

// ############################################################################
// ENDGAME B.3.5 step 5, block H2 — the two NON-CONSTRUCTOR particle bodies this
// file owns, moved onto `Graphics::ParticleInstance`'s members.
//
// WHAT MOVED AND WHAT DELIBERATELY DID NOT:
//
//  * `bool ParticleInstance::Update()` (retail 0x180104180, `QEAA_NXZ`) and
//    `static void ParticleInstance::IncrementInstanceIndex()` (0x1800201f0,
//    `SAXXZ`) moved. Both were declared by block E in `ParticleInstance.h`; no
//    header edit was needed.
//
//  * THE FIVE CONSTRUCTORS AND THE THREE VTABLE STAMPS STAY EXACTLY AS THEY ARE
//    (B4b-2 / ENDGAME section B.1 decision 3). `RtGraphics_Particle_Ctor`,
//    `_ParticleInstance_Ctor` and `_ParticleSystemInstance_Ctor` are called on
//    memory that already exists and that the rest of the tree expects to be
//    wearing the MINTED `??_7` table out of `rt_data_mint_tables.inc`; a member
//    constructor would stamp GCC's tables instead. The mint rows are not retired
//    in this step either (B35_RULES section 11). They keep the POD spelling and
//    their diff oracles' byte ranges unchanged.
//
//  * `?_frameIndex@ParticleInstance@Graphics@@1HA` (0x1801fe77c) is the class
//    static `ParticleInstance::_frameIndex` since ENDGAME B.3.9 block GFX (D7),
//    redirected onto the mint's row.
// ############################################################################

// ?Update@ParticleInstance@Graphics@@QEAA_NXZ  retail 0x180104180.
//
// FAITHFULLY UNGUARDED, per CLAUDE.md "don't fix a faithful crash with a null
// check": retail dereferences `_resource->_active` with NO null test on
// `_resource`, and on the "needs update" arm writes through `_boundsPtr` with no
// test either. Neither guard is added; the full evidence and the oracle's
// limits are in the banner above the door.
//
// B.4: `_resource` is the real `Graphics::Particle` and `_boundsPtr` the named
// `RtGraphics::ParticleBoundsRecord` (ParticleInstance.h).
bool ParticleInstance::Update() {
    const Particle* const resource = _resource;             // +0xc0, unguarded
    if (resource->_active != 0) {                            // +0x20, unguarded
        _boundsPtr = resource->_records._data[_index];       // +0x38 array, +0xd0 index
    }

    bool needsUpdate;
    if (resource->_active != 0 && _boundsPtr->frameTag == _frameIndex) {
        // `Geometry::SetBounds(const Volume::Box&)`, exactly as retail does —
        // a QUALIFIED non-virtual member call since the ENDGAME B.3.5 caller
        // batch. `this` IS the Geometry base (offset zero), so the cast the POD
        // form needed is gone.
        this->::Graphics::Geometry::SetBounds(
            _boundsPtr->_bounds);
        needsUpdate = false;
    } else {
        _boundsPtr->frameTag = _frameIndex;                  // unguarded, faithful
        needsUpdate = true;
    }

    return needsUpdate;
}

// ?IncrementInstanceIndex@ParticleInstance@Graphics@@SAXXZ  retail 0x1800201f0.
// One line: `_frameIndex += 1` (ENDGAME B.3.9 GFX: the class static; the
// null-tolerant slot read went with the lazy cache -- the cell always exists).
void ParticleInstance::IncrementInstanceIndex() {
    ++_frameIndex;
}

}  // namespace Graphics

// ============================================================================
// Graphics::Particle::ReleaseFields — ??1Particle@Graphics@@UEAA@XZ 0x180103c70
// up to its External half (ENDGAME B.3.8 block RB). Read off the disassembly:
//
//   for (i = 0; i < [this+0x40]; ++i) {          ; count RE-READ each pass
//       rec = ([this+0x38])[i];                  ; data RE-READ each pass
//       if (rec) {
//           if (rec->+0x18) MemFree(rec->+0x18); ; the record's own buffer
//           rec->+0x18 = 0; rec->+0x20 = 0;      ; one qword each
//           MemFree(rec);
//       }
//   }
//   if ([this+0x38]) MemFree([this+0x38]);
//   [this+0x38] = 0; [this+0x40] = 0;            ; data, then size+capacity
//   ... ~External (restamp, refs release, path String release) — the
//   compiler's base chain in the real destructor.
//
// Every MemFree is retail's inlined `_singleton+0x640` lock / Free
// (0x1800c6c00) / unlock, i.e. System::MemoryAllocator::MemFree. The record
// elements are NOT nulled in the array (retail does not).
// ============================================================================
// B.4: on `_records` and each pool record's own `_records` header by name.
void Graphics::Particle::ReleaseFields() {
    for (int32_t i = 0; i < _records._size; ++i) {
        RtGraphics::ParticleBoundsRecord* const rec = _records._data[i];
        if (rec == nullptr) continue;
        if (void* const buf = rec->_records._data) System::MemoryAllocator::MemFree(buf);
        rec->_records._data     = nullptr;
        rec->_records._size     = 0;               // one qword: size AND capacity
        rec->_records._capacity = 0;
        System::MemoryAllocator::MemFree(rec);
    }
    if (void* const data = _records._data) System::MemoryAllocator::MemFree(data);
    _records._data     = nullptr;
    _records._size     = 0;                        // one qword: size AND capacity
    _records._capacity = 0;
}

// ?_singleton@AnimationManager@Graphics@@0PEAV12@EA  retail 0x1801fe7b0 --
// the class static (ENDGAME B.3.9 block GFX, B39_RULES D7 row A44). Its mint
// storage row is retired (rt_mint_retired/gfx.txt): every reader is in this
// block's files.
Graphics::AnimationManager* Graphics::AnimationManager::_singleton = nullptr;

// ?_rng@Particle@Graphics@@1VRandom@System@@A  retail 0x1801fe7f0 -- the class
// static (ENDGAME B.3.9 block GFX, B39_RULES D7 row A46). Its mint storage row
// is retired with every reader (rt_mint_retired/gfx.txt).
// ENDGAME B7 (CTOR, D5): retail's CRT initialiser 0x180149650 for it is
// `System::Random::Random()` (0x1800c7f20) inlined -- mt[0] = (uint32)_time64(NULL)
// (CALL 0x18007fb9c), then the 623-step recurrence -- so the definition below now
// runs that real ctor at this TU's CRT init, and the file-local seeder that
// transcribed it (ParticleRngSeed) is gone. Retail's inlined copy drops the
// `index = 0` store because BSS is already zero; the ctor writes the same 0 there
// (value-identical: `_index` stays 0, which forces the first draw to run NextGroup).
System::Random Graphics::Particle::_rng;


// ?_frameIndex@ParticleInstance@Graphics@@1HA  retail 0x1801fe77c -- the class
// static (ENDGAME B.3.9 block GFX, B39_RULES D7 row A37), redirected onto the
// mint's storage row by RtGraphicsPortInit (foreign readers remain).
int Graphics::ParticleInstance::_frameIndex = 0;
