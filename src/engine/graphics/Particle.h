#pragma once
// ============================================================================
// src/engine/graphics/Particle.h — Wave 6 lane G — Graphics::Particle /
// Graphics::ParticleInstance / Graphics::ParticleSystemInstance layouts
// ============================================================================
//
// Every offset is MEASURED against Runtime-x64.dll (program="Runtime-x64.dll")
// via decompile_function + disassemble_function; the pinning function is
// named next to each field. Nothing here is copied from the profile DLL --
// this lane read the retail export table directly (tools/pe_exports.py-style
// pefile query) before writing any body
// (reference_resolveexport_phantom_names.md).
// ============================================================================

#include <cstdint>
#include <cstddef>

// ENDGAME B.3.5 block E: this header is DELIBERATELY left with no include of
// `Geometry.h`. The real `Graphics::ParticleInstance` /
// `ParticleSystemInstance` classes live in `ParticleInstance.h`, which includes
// both this header and `Geometry.h`; see that file's banner for why the split
// exists (`<windows.h>` and `Volume.h`'s `Rectangle`).
//
// ENDGAME B.3.5 block H2: the REAL `Graphics::Particle` and
// `Graphics::ParticleSystem` are at the BOTTOM of this file, and the two
// includes below are what they need. Neither reaches `Volume.h`, so the
// `Rectangle` collision block E measured is not reintroduced: this header stays
// usable from `vtbatch7_animgroup_particlesystem.cpp`, which includes
// `<windows.h>` through `standalone/resolve.h`. (`AnimationGroup.h` already
// proves the pairing — `vtbatch7` includes it after `resolve.h` and builds.)
#include "../object/External.h"       // Object::External, the proved parent
#include "../object/Internal.h"       // B.3.9: InternalMemFree (the concrete slot 0s)
#include "../collection/Collection.h" // Collection::Array<T>, the +0x38 header

namespace RtGraphics {

// ============================================================================
// Graphics::Particle — 0x48 bytes KNOWN (the base class only; subclasses such
// as Tornado, which is already ported in src/game/Game/tornado.cpp, add
// their own fields after it -- tornado.cpp's own comment confirms "Tornado
// (size 0x90) extends Graphics::Particle").
//
// Pinned by the ctor (retail 0x180103e80, disassembled). Exactly like
// AnimationEvent, the first two vtable stores (0x180154788 Object::Base,
// 0x1801547a0 Object::External) are the inlined base ctor and are immediately
// overwritten -- only the LAST store (0x180155130, Particle::_vftable_)
// persists. No external call happens anywhere in this ctor; it is pure
// stores, which is why this port needs no DLL internal for it at all.
//
// +0x30/+0x38/+0x40 are three zeroed qwords this lane could not identify any
// reader for (Particle's other 8 exported virtuals -- slots 6/7/8 in the
// vtable -- are all `_purecall`, per tornado.cpp's own comment, so the base
// class itself never touches them; only a live subclass would). Left
// UNVERIFIED rather than guessed.
//
// `Particle::_rng` (`?_rng@Particle@Graphics@@1VRandom@System@@A`, retail
// 0x1801fe7f0) and `Particle::_typeInfo`
// (`?_typeInfo@Particle@Graphics@@0VBaseTypeInfo@Object@@A`, retail
// 0x1801fa7b0) are PROTECTED STATIC class members (mangling code `1` and `0`
// respectively), not instance fields -- they do not appear in this struct.
// See animation.cpp for why they cannot be registered by this lane.
// ============================================================================
// *** [0x21,0x24) AND [0x2c,0x30) ARE TWO GAPS THE CTOR NEVER WRITES --
// MEASURED, not merely inferred from C++'s natural alignment padding. ***
// The full store list from disassembly of 0x180103e80:
//   +0x20 byte  = 1     +0x24 dword = 1     +0x28 dword = 0xf
//   +0x30 qword = 0     +0x38 qword = 0     +0x40 qword = 0
// Nothing touches +0x21/+0x22/+0x23 (between the +0x20 and +0x24 stores) or
// +0x2c/+0x2d/+0x2e/+0x2f (between the +0x28 and +0x30 stores). Two diff runs
// confirmed both the hard way, one gap at a time: an early revision of this
// port's ctor oracle raw-memcmp'd the whole struct and reported a divergence
// on every call, because the REAL object's leftover heap bytes in each gap
// differed from the diff harness's own 0xCD scratch-poison fill of the same
// untouched range -- not because either implementation actually wrote
// different values there. The cheap way to find every such gap in one pass
// is to walk the store list once (as above) and exclude every address it
// skips, rather than re-deriving one gap per run. animation.cpp's oracle now
// explicitly excludes both ranges from the comparison instead of masking
// them with initialized fields.

// ============================================================================
// The per-index record inside a ParticleResource's bounds array (see
// ParticleResource below). Geometry::SetBounds's mangled name
// (?SetBounds@Geometry@Graphics@@QEAAXAEBVBox@Volume@@@Z) says retail passes
// this pointer AS a `const Volume::Box&`, so its first 0x18 bytes are
// Box-shaped (Volume::Box is {Vector3 min; Vector3 max;}, see
// src/engine/volume/Volume.h) -- but bytes 0x18..0x2b are UNKNOWN (this
// lane never needs to read or write them, only pass the pointer through), and
// +0x2c is NOT part of Volume::Box (which is only 0x18 bytes total) -- it is
// this record's own trailing field, a per-frame "last touched" stamp compared
// against Graphics::ParticleInstance::_frameIndex.
//
// Pinned by ParticleInstance::Update (retail 0x180104180, decompiled): reads
// and writes `*(int*)(record + 0x2c)`.
// ============================================================================
// *** SIZE CORRECTION, WAVE 27 LANE GFXPART, MEASURED. *** This record is
// 0x40 BYTES, not 0x30. Both of retail's allocation sites ask for exactly 0x40
// (the inlined `Enter/Allocate(0x40,8)/Leave` in ?Serialize@Particle@Graphics@@
// at 0x180103fed, and the per-TU `operator new(0x40)` at 0x180061a80 that
// ?SetParticle@ParticleInstance@ calls), and its constructor (0x180103bf0,
// UNNAMED in the export directory) writes through +0x38. The constructor's full
// store list, disassembled:
//     +0x00..+0x0c = 1e8f  (Box::min seeded to +infinity-ish)
//     +0x0c..+0x18 = -1e8f (Box::max seeded to -infinity-ish)
//     +0x18 = 0            the live-particle Array's _data
//     +0x20 = 0            that Array's _size (+0x24 its capacity)
//     +0x28 = 0            the LIVE PARTICLE COUNT -- read by both Draw bodies
//                          and by ?AddDrawItems@, which returns early on zero
//     +0x2c = -1           the per-frame tag (CONFIRMS the field below)
//     +0x30 = 0, +0x38 = 0
//   then SetSize(this+0x18, particle[+0x28]) through a second unnamed
//   instantiation at 0x180061bb0.
// The `frameTag` reading below is CONFIRMED by that -1 seed and by
// ?Update@ParticleSystemInstance@ comparing it against
// ?_frameIndex@ParticleInstance@Graphics@@ (0x1801fe77c).
// B.4: the record is NAMED FIELD BY FIELD now, from the readers' own names
// (wave28_lane_partsys.cpp's kPool_* / kRec_*). Its full definition, and the
// 0x58-byte live-particle record it holds, are in `ParticleInstance.h`, which
// can include `Volume.h` (`::Box`) where this header deliberately does not
// (the `<windows.h>` / `Rectangle` note at the top); every use here is by
// pointer.
struct ParticleBoundsRecord;

// B.4: the per-instance EMITTER record (`ParticleSystemInstance::_emitter`,
// element of `ParticleSystem::_pool`) and the per-particle EXTRA record its
// array holds, parallel to the pool record's live particles. Named from
// wave28_lane_partsys.cpp's kEmit_* / kExt_* measurements. Neither has a
// retail name (Particle.h's H2 note: no `Emitter` symbol in the DLL); both are
// built by stores into raw blocks, never by a C++ constructor:
// ParticleSystem::Serialize / ParticleSystemInstance::SetParticle zero the
// array header, copy `ParticleSystem::_emissionRate`'s bits into `_rate`, and size the
// array through the kSpec_Rec14 row (0x180020e90).
struct ParticleExtra {
    float _velX, _velY, _velZ;   // +0x00  velocity
    float _rotRate;              // +0x0c  degrees/sec, random sign
    float _sizeRate;             // +0x10
};
static_assert(sizeof(ParticleExtra) == 0x14, "the extra-record stride is 0x14 (kSpec_Rec14)");
static_assert(offsetof(ParticleExtra, _velX) == 0x0, "ParticleExtra::_velX");
static_assert(offsetof(ParticleExtra, _velY) == 0x4, "ParticleExtra::_velY");
static_assert(offsetof(ParticleExtra, _velZ) == 0x8, "ParticleExtra::_velZ");
static_assert(offsetof(ParticleExtra, _rotRate)  == 0x0c, "ParticleExtra::_rotRate @ +0x0c");
static_assert(offsetof(ParticleExtra, _sizeRate) == 0x10, "ParticleExtra::_sizeRate @ +0x10");

struct ParticleEmitter {
    Collection::Array<ParticleExtra> _extra;   // +0x00 {_data, _size@0x08, _capacity@0x0c}
    float _rate;                               // +0x10  the spawn-rate accumulator
};
static_assert(sizeof(ParticleEmitter) == 0x18, "MemAlloc(0x18, 8) at both build sites");
static_assert(offsetof(ParticleEmitter, _extra) == 0x0, "ParticleEmitter::_extra");
static_assert(offsetof(ParticleEmitter, _rate) == 0x10, "ParticleEmitter::_rate @ +0x10");

// ============================================================================
// The resource object a ParticleInstance references at +0xc0 (see below).
// This is NOT this lane's class to own the full layout of -- it is some
// Runtime resource/description type (a compiled ParticleSystem or similar)
// that this lane only reads two fields of, both pinned by
// ParticleInstance::Update:
//   +0x20 `_active`      -- a bool/flag byte; when zero, Update never derefs
//                           the array below or touches `_boxes`.
//   +0x38 `_boxes`        -- base pointer of an array of ParticleBoundsRecord*
//                           (read as `*(longlong*)(resource+0x38) + index*8`,
//                           i.e. an array of POINTERS, stride 8).
// Everything else about this type is opaque and unnamed.
// ============================================================================
struct ParticleResource {
    uint8_t                 _pad0[0x20];
    uint8_t                 _active;   // +0x20
    uint8_t                 _pad21[0x17];
    ParticleBoundsRecord**  _boxes;    // +0x38
};
static_assert(offsetof(ParticleResource, _pad0) == 0x0, "ParticleResource::_pad0");
static_assert(offsetof(ParticleResource, _pad21) == 0x21, "ParticleResource::_pad21");
static_assert(offsetof(ParticleResource, _active) == 0x20, "ParticleResource::_active @ +0x20");
static_assert(offsetof(ParticleResource, _boxes) == 0x38, "ParticleResource::_boxes @ +0x38");

// ============================================================================
// Graphics::Geometry — NOT this lane's class (Wave 6 lane D owns
// Graphics::Geometry + Scene, src/engine/graphics/scene.cpp). Every field
// ParticleInstance/ParticleSystemInstance add sits AFTER a 0xc0-byte Geometry
// base, pinned by both ctors calling `Geometry::Geometry(this)` (retail
// 0x1800a4270, exported as `??0Geometry@Graphics@@QEAA@XZ`) and then writing
// their OWN first field at exactly +0xc0 with no gap. This lane calls that
// ctor at its own export rather than re-deriving Geometry's internals, the
// same "what stays in the DLL" shape scene.cpp uses for Picking::Scene's
// unexported grid helpers.
//
// *** +0x14..+0x17 IS A GAP `Geometry::Geometry` NEVER WRITES -- MEASURED. ***
// Disassembly of 0x1800a4270 shows a DWORD store to +0x10 (`*(undefined4*)
// (this+0x10) = 0xffffffff`) followed directly by a QWORD store to +0x18
// (`*(undefined8*)(this+0x18) = 0`), with +0x14..+0x17 untouched in between.
// A diff run confirmed this in practice: this port's ParticleInstance and
// ParticleSystemInstance ctor oracles originally raw-memcmp'd their whole
// struct (including this opaque base, on the theory that BOTH implementations
// call the identical retail Geometry::Geometry so it can never diverge --
// true, but irrelevant to a byte NEITHER call writes). The real object's
// pre-existing heap bytes there happened to be zero while the scratch
// oracle's own 0xCD poison filled the same gap with 0xCD, so every call
// reported a false divergence. animation.cpp's ctor oracles now compare only
// the fields ParticleInstance/ParticleSystemInstance themselves add
// ([0xc0,0xd4) and, for the latter, [0xd8,0xe0)) and do not compare this
// base at all -- it is written by the exact same export in both cases, so
// there is nothing this lane's oracle could add by re-checking it.
// ============================================================================
struct GeometryBase {
    uint8_t opaque[0xc0];
};

// ============================================================================
// Graphics::ParticleInstance : Graphics::Geometry — 0xd8 bytes.
//
// Pinned by the ctor (retail 0x180104130, decompiled + disassembled): after
// `Geometry::Geometry(this)`, it stamps `this->vtable = &ParticleInstance::
// _vftable_` (retail 0x1801550e0, LEA target confirmed by disassembly) and
// zeroes +0xc0/+0xc8, then sets +0xd0 = -1 (0xffffffff).
//
// +0xc0 `_resource`  -- a ParticleResource* (see above), null until a
//                       subclass or ParticleComponent attaches one.
// +0xc8 `_boundsPtr` -- a cached ParticleBoundsRecord*, pinned by Update.
// +0xd0 `_index`     -- the instance's index into `_resource->_boxes`,
//                       initialised to -1 ("no instance assigned yet").
//
// The ctor's diff oracle compares only [0xc0,0xd4) (see GeometryBase's
// banner above for why the base is excluded, and why +0xd4's alignment pad
// is too).
// ============================================================================

// ============================================================================
// Graphics::ParticleSystemInstance : Graphics::ParticleInstance — 0xe0 bytes.
//
// Pinned by the ctor (retail 0x1800a55a0, decompiled + disassembled): calls
// `Geometry::Geometry(this)`, transiently stamps `&ParticleInstance::
// _vftable_` (0x1801550e0, same constant as ParticleInstance's own ctor --
// this is retail re-running ParticleInstance's field inits inline rather
// than calling it, exactly like the AnimationEvent/Particle base-ctor
// inlining above), zeroes +0xc0/+0xc8, sets +0xd0 = -1, THEN overwrites the
// vtable with its own (0x180155040, ParticleSystemInstance::_vftable_) and
// zeroes one more field at +0xd8.
//
// The ctor's diff oracle compares [0xc0,0xd4) (inherited) and [0xd8,0xe0)
// (own) -- not the Geometry base, for the same measured reason given above.
// ============================================================================

}  // namespace RtGraphics

// ############################################################################
// ENDGAME B.3.5 step 5 (block H2) — `Graphics::Particle` and
// `Graphics::ParticleSystem`, the REAL C++ classes
// ############################################################################
//
// The two *Instance* classes are NOT here: they derive from `Graphics::Geometry`
// and live in `ParticleInstance.h` (block E), for the `Volume.h` /
// `<windows.h>` reason that file's banner records. These two derive from
// `Object::External` and need no geometry at all, which is why they can sit in
// this header beside the PODs they are cross-asserted against.
//
// GREP HAZARD (B4b-5 / B34_BRIEFS A.7), AND IT CHANGED IN THIS BLOCK: before
// H2, a bare `Particle` inside a `namespace Graphics { }` block bound NOTHING
// (the POD is `RtGraphics::Particle`, and a stray spelling was a compile error).
// It now binds the REAL CLASS. Every POD spelling inside such a block in
// `animation.cpp`, `wave27_lane_gfxpart.cpp`, `wave28_lane_partsys.cpp` and
// `vtbatch7_animgroup_particlesystem.cpp` must be written `RtGraphics::`.
//
// PARENTS, PROVED BYTE-WISE (B35_SLOTS.md:506-507, read as `.rdata` bytes):
//     ?_ParentTypeName@Particle@Graphics@@       -> "Object::External"
//     ?_ParentTypeName@ParticleSystem@Graphics@@ -> "Particle"       <- NO NAMESPACE
//
// RULE B5-15 — THE FOUR NAME LITERALS, RECORDED AND NOT USED. Nothing here
// re-mints or retires a reflection row (B35_RULES section 11): the two
// `?_typeInfo@` rows and the two `??_7` mint rows stay, and slots 1/2 keep
// reading the `_typeInfo` nodes. The literals are written down so that a later
// `DEFINE_TYPE` is COPIED, not derived from a C++ base name — deriving
// `Graphics::Particle` for `ParticleSystem`'s parent would change the CRC and
// stop every save that names a ParticleSystem from loading:
//     ?_TypeName@Particle@Graphics@@             -> "Particle"
//     ?_ParentTypeName@Particle@Graphics@@       -> "Object::External"
//     ?_TypeName@ParticleSystem@Graphics@@       -> "ParticleSystem"
//     ?_ParentTypeName@ParticleSystem@Graphics@@ -> "Particle"
//
// SECTION 9 / RULE B4b-4 — `?_rng@Particle@Graphics@@1VRandom@System@@A`
// (0x1801fe7f0). ENDGAME B.3.9 GFX: IT IS A STATIC MEMBER NOW (`Particle::_rng`
// below, one seeder at CRT init). The rest of this paragraph is the step-5
// reasoning, kept as history.
// (0x1801fe7f0) WAS NOT A STATIC MEMBER HERE. It is a `System::Random` held BY
// VALUE and seeded through a constructor DOOR at load (0x180149650), which is
// B4b-2's double-seed case verbatim: a real static member would be constructed
// a second time by our own runtime init and every particle placement would
// desync from the DLL's other readers. It stays an `RT_DATA_SLOT` /
// `ResolveExport` read in `wave27_lane_gfxpart.cpp` and `wave28_lane_partsys.cpp`
// (`ParticleRng()`), and RULE B4b-4 owes it a `MintStorageCell()` redirect at
// the class-static conversion, the END of B.3 — not in step 5. The same holds
// for `?_frameIndex@ParticleInstance@Graphics@@1HA` (0x1801fe77c), read through
// `RtFrameIndexSlot()` in `animation.cpp`.
//
// NO REAL CONSTRUCTOR ON EITHER CLASS (B4b-2 / ENDGAME section B.1 decision 3).
// `??0Particle@Graphics@@QEAA@XZ` (0x180103e80) and
// `??0ParticleSystem@Graphics@@QEAA@XZ` (0x1800a5320) keep their registered ctor
// doors in `animation.cpp` and `vtbatch7_animgroup_particlesystem.cpp`: both are
// called on memory that already exists and that the rest of the tree expects to
// be wearing the MINTED `??_7` table out of `rt_data_mint_tables.inc`. A member
// constructor would stamp GCC's tables instead.
// ############################################################################

namespace Graphics {

// `?CreateSystem@ParticleSystem@Graphics@@UEBAPEAVParticleInstance@2@XZ` returns
// one of these. The class is declared in `ParticleInstance.h`, which includes
// THIS header — so the reference here is a forward declaration and the two
// headers do not form an include cycle.
class ParticleInstance;
class MaterialInstance;   // B.4: Particle::_material (Material.h)

// ============================================================================
// Graphics::Particle : Object::External — 0x48 bytes, NINE slots, ABSTRACT.
//
// `??_7Particle@Graphics@@6B@` @ 0x180155130, read out of `.rdata` one qword at
// a time (B35_SLOTS.md:170-179):
//
//   [0] +0x00  0x180020160  (unexported) the vector deleting destructor
//   [1] +0x08  0x18001ff30  ?TypeName@         graphics_typeinfo_accessors.cpp
//   [2] +0x10  0x18001ff10  ?Type@             graphics_typeinfo_accessors.cpp
//   [3] +0x18  0x180104080  ?GetFileVersion@   (`MOV EAX,0x1000; RET`)
//   [4] +0x20  0x1800066e0  Object::External::Compile        INHERITED
//   [5] +0x28  0x180103f00  ?Serialize@        wave27_lane_gfxpart.cpp
//   [6] +0x30  _purecall    inherited-from Object::External  -> Reconstruct
//   [7] +0x38  _purecall    inherited-from Object::External  -> Destruct
//   [8] +0x40  _purecall    INTRODUCED HERE                 -> CreateSystem
//
// *** B35_RULES section 13 ITEM 6 IS NOW CLOSED, AND THE INFERENCE WAS RIGHT. ***
// The question pass recorded slots 6/7/8's identities as INFERRED from
// `ParticleSystem`'s concrete overrides at the same indices. They are confirmed
// here without new bytes, from two reads that were already byte-level:
//   * slots 6 and 7 are marked `inherited-from Object::External` in the vtable
//     dump, i.e. those two `_purecall` entries are the SAME qwords External's own
//     table carries — and the real `Object::External` (External.h:601, itself
//     read from retail) declares exactly `virtual External* Reconstruct() = 0`
//     at [6] and `virtual void Destruct() = 0` at [7]. Particle therefore
//     OVERRIDES two inherited pure slots and introduces nothing at 6/7.
//   * slot 8 is `_purecall` and is NOT marked inherited (External's table is
//     eight entries), and `ParticleSystem`'s slot 8 is
//     `?CreateSystem@ParticleSystem@Graphics@@UEBAPEAVParticleInstance@2@XZ`
//     @ 0x1800a5140. A subclass cannot override a slot its base did not declare,
//     so `CreateSystem` is introduced PURE here, at 8 — which is the whole
//     mechanism behind RULE B5-16's trap (see ParticleSystem below).
// So slots 6/7 are `override = 0` (they re-declare inherited pure slots at the
// right index) and slot 8 is `virtual ... = 0` (B5-16: `virtual`, not
// `override`, where the branch introduces the slot).
//
// ABSTRACT for the ordinary reason (B34_BRIEFS A.4 as amended): nothing holds a
// `Particle` by value anywhere in the tree, and three of its nine slots are
// `_purecall` in retail itself. Slot 0 is left `= 0` as well — its body is the
// minted `RT_DELDTOR` row at 0x180020160, which B35_RULES section 11 forbids
// retiring, and declaring it pure re-declares the slot at the right index
// without creating a second, divergent one.
//
// THE LAYOUT IS THE POD'S, FIELD FOR FIELD, with the two MEASURED GAPS kept as
// named pads rather than left to C++'s natural alignment (see the POD's banner
// above for the disassembled store list that found them one at a time).
// ============================================================================
//
// ENDGAME B.3 step 9 (block GFX, B39_RULES D1/D2): slot 0 is now the class's
// OWN vector deleting dtor; the class stays ABSTRACT through Reconstruct /
// Destruct / CreateSystem. The ??0 / ??1 doors, the four slot doors, the
// FACTREST row, the RT_DELDTOR row 0x180020160 and the minted
// `??_7Particle@Graphics@@6B@` are retired (tools/rt_mint_retired/gfx.txt).
class Particle : public Object::External {
public:
    // [0] 0x180020160 -- MSVC's VECTOR deleting destructor, both arms exactly as
    // the retired RT_DELDTOR row ran them (`MOV EDX,0x48` = the element size).
    // Every subclass (ParticleSystem, the exe's Tornado) declares its own.
    // ??1Particle@Graphics@@UEAA@XZ  0x180103c70. VIRTUAL
    // (B3X W1). PORTED by ENDGAME B.3.8 block RB as `ReleaseFields`
    // (animation.cpp): free each pool record's +0x18 buffer, zero the record's
    // +0x18/+0x20, free the record, then free `_records` and zero its header.
    // The External half that follows in retail is the compiler's ~External.
    // ENDGAME B.3.9 GFX: the transcribed door in batchc_gfx_leaves.cpp is gone
    // with the RT_DELDTOR row; this is the only destructor.
    ~Particle() override { ReleaseFields(); }

    // [1] 0x18001ff30 / [2] 0x18001ff10 / [3] 0x180104080.
    //   MEMBERS THAT FORWARD, not doors that delegate: all three bodies live in
    //   `src/engine/graphics/graphics_typeinfo_accessors.cpp`, which is NOT block
    //   H2's file set, so the direction is reversed and the member calls the
    //   registered door. The same split `external_class_doors.cpp` documents and
    //   `Graphics::AnimationEvent` (block H1) already uses.
    // [1] TypeName / [2] Type, TypeId 0x18001ff20, `_TypeName` / `_ParentTypeName`
    // and the factory pair: retail's macro (ENDGAME B.3.9 TI-REST, B39_RULES
    // D6); the record is `DEFINE_TYPE`d in
    // src/engine/graphics/graphics_type_records.cpp.
    DECLARE_TYPE(Particle)
public:
    int32_t     GetFileVersion() const override;           // [3] 0x1000 == 4096
    // [4] Compile is Object::External's own body (0x1800066e0), inherited.
    bool        Serialize(IO::Stream& stream) override;           // [5] 0x180103f00

    // [6] / [7] — retail leaves both `_purecall` here; they re-declare the two
    //   slots `Object::External` introduced as pure, at the same indices.
    Object::External* Reconstruct() override = 0;
    void              Destruct() override = 0;

    // [8] — INTRODUCED PURE BY THIS CLASS (RULE B5-16). `virtual`, not
    //   `override`: `Object::External`'s table is eight entries and has no such
    //   slot. `ParticleSystem` is where it gets a body, at slot 8, which is why
    //   `Construct` is pushed to slot 9 on THAT class and nowhere else.
    virtual ParticleInstance* CreateSystem() const = 0;

    // ---- the non-slot exported members -------------------------------------
    // Declared up front, ported or not, so no caller batch has to edit this
    // header (B35 class-block brief). Each is left UNDEFINED with a `not ported`
    // note because nothing in the tree calls it.
    //
    // ?IsInstanced@Particle@Graphics@@QEBA_NXZ            0x18001ff60 — NOT PORTED
    bool IsInstanced() const;
    // ?CreateNewObject@…SAPEAVBase@Object@@XZ             0x18001ff40 — B.3.9 GFX
    // ?CreateInPlaceObject@…SAPEAVBase@Object@@PEAX@Z     0x18001ff50 — B.3.9 GFX
    //   ENDGAME B.3.9: retail's ABSTRACT pair, both `XOR EAX,EAX ; RET`
    //   (wave27_lane_gfxpart.cpp). The note below is the pre-B.3.9 reading.
    //   The reflection block's two factories. Both go through
    //   `System::MemoryAllocator`, which B35_RULES section 11 keeps as doors for
    //   the whole of step 5. Note what is ABSENT: `Particle` has no `??2`/`??3`
    //   of its own in the export table — it is abstract, so only `ParticleSystem`
    //   carries the per-class operator new / delete. (Declared by DECLARE_TYPE.)

    // ?_rng@Particle@Graphics@@1VRandom@System@@A  retail 0x1801fe7f0 — the one
    // MT19937 every particle draws from (ParticleInstance::SetParticle,
    // ParticleSystem's emitter, the exe's tornado funnel). ENDGAME B.3.9 GFX: a
    // real class static (animation.cpp), SEEDED ONCE at CRT init by the
    // transcription of retail's initialiser 0x180149650 beside its definition —
    // the only seeder (B4b-2); the mint's ConstructParticleRng is gone. Retail
    // spells it PROTECTED (`1`); public here because ParticleInstance and the
    // exe's TornadoInstance read it, and access control is not ABI.
    static System::Random _rng;

    // ??0Particle@Graphics@@QEAA@AEBV01@@Z                0x180020030 — NOT PORTED
    // ??4Particle@Graphics@@QEAAAEAV01@AEBV01@@Z          0x1800200e0 — NOT PORTED
    Particle(const Particle& src);
    Particle& operator=(const Particle& src);

    // (ENDGAME B.3.9 GFX: `DoorBody` went with the ??0 door it served.)

protected:
    // ??0Particle@Graphics@@QEAA@XZ  0x180103e80 — the REAL constructor
    // (ENDGAME B.3.8 block RB, RULE B8-1): External() and the stamps are the
    // compiler's; the six stores are ONE body with the door. The two gaps
    // (+0x21..+0x23, +0x2c..+0x2f) stay unwritten.
    Particle() { InitFields(); }


private:
    void InitFields() {
        _active      = true;       // +0x20 (BYTE 1)
        _recordCount = 1;          // +0x24
        _subCount    = 0xf;        // +0x28
        _material    = nullptr;    // +0x30
        _records._data     = nullptr;   // +0x38
        _records._size     = 0;         // +0x40 } one qword
        _records._capacity = 0;         //       }
    }
    void ReleaseFields();   // 0x180103c70 up to the External half (animation.cpp)
public:

public:
    // ---- the 0x28 bytes Particle adds --------------------------------------
    // PUBLIC for the same reason `Object::External`'s are: unconverted bodies
    // reach them at these offsets through a cast.
    bool     _active;        // +0x20  ctor stores 1; Serialize's bool slot (+0x78)
    uint8_t  _pad21[3];      // +0x21  MEASURED GAP — the ctor never writes it
    int32_t  _recordCount;   // +0x24  ctor 1. The POOL RECORD COUNT: Serialize
                             //        hands it to Array<void*>::SetSize over
                             //        `_records` and then builds that many.
    int32_t  _subCount;      // +0x28  ctor 0xf. The PER-RECORD SUB-ARRAY SIZE:
                             //        the record ctor (0x180103bf0) reads it and
                             //        sizes the record's own +0x18 array with it.
    uint32_t _pad2c;         // +0x2c  MEASURED GAP — the ctor never writes it
    MaterialInstance* _material;  // +0x30  ExternalPtr<Graphics::MaterialInstance>.
                             //        B.4: the pointee's class, as a raw pointer
                             //        (not the template: its zeroing default ctor
                             //        would add a store to Particle()). An
                             //        `ExternalPtr` is one pointer wide and holds
                             //        the pointee at +0x00, and `Serialize` hands
                             //        `&_material` to the retail
                             //        `ExternalBasePtr<T>::Serialize`
                             //        instantiation at 0x180014520.
    // +0x38  Array<ParticleBoundsRecord*> — the pool-record pointer array.
    // B.4: typed. A pointer element is trivially destructible, so the implicit
    // member dtor is the same Clear-then-J1 as the raw header's, and
    // `ReleaseFields` empties the header first anyway; SetSize still runs
    // through the registered `kSpec_Fill8` row (0x180062110).
    Collection::Array<RtGraphics::ParticleBoundsRecord*> _records;   // +0x38 {_data, _size@0x40, _capacity@0x44}
};

static_assert(sizeof(Particle) == 0x48,
              "Graphics::Particle is 0x48 bytes (the MOV EDX,0x48 in its own "
              "slot-0 vector deleting destructor 0x180020160)");
static_assert(offsetof(Particle, _pad21) == 0x21, "Particle::_pad21");
static_assert(offsetof(Particle, _pad2c) == 0x2C, "Particle::_pad2c");
static_assert(alignof(Particle) == 8, "qword aligned (the vptr)");
static_assert(offsetof(Particle, _active)      == 0x20, "Particle::_active @ +0x20 (ctor 0x180103e80)");
static_assert(offsetof(Particle, _recordCount) == 0x24, "Particle::_recordCount @ +0x24");
static_assert(offsetof(Particle, _subCount)    == 0x28, "Particle::_subCount @ +0x28");
static_assert(offsetof(Particle, _material)    == 0x30, "Particle::_material @ +0x30 (Serialize 0x180103f00)");
static_assert(offsetof(Particle, _records)     == 0x38, "Particle::_records @ +0x38");
static_assert(offsetof(Particle, _active) == sizeof(Object::External),
              "Particle's own fields begin at the end of the External base: the "
              "ctor inlines External's three zeroed qwords and then writes +0x20");

// ============================================================================
// Graphics::ParticleSystem : Graphics::Particle — 0x108 bytes, TEN slots.
//
// `??_7ParticleSystem@Graphics@@6B@` @ 0x180155090 (B35_SLOTS.md:193-203):
//
//   [0] +0x00  0x180020a70  (unexported) the vector deleting destructor
//   [1] +0x08  0x1800203e0  ?TypeName@
//   [2] +0x10  0x1800203c0  ?Type@
//   [3] +0x18  0x1800a4d10  ?GetFileVersion@   (returns 0x1001)
//   [4] +0x20  0x1800066e0  Object::External::Compile   INHERITED through Particle
//   [5] +0x28  0x1800a4d20  ?Serialize@
//   [6] +0x30  0x180020560  ?Reconstruct@
//   [7] +0x38  0x180020590  ?Destruct@
//   [8] +0x40  0x1800a5140  ?CreateSystem@     <- RULE B5-16's TRAP LIVES HERE
//   [9] +0x48  0x180020580  ?Construct@        <- INTRODUCED HERE, AT NINE
//
// *** THIS IS THE ONE CLASS IN VIDEO AND GRAPHICS WHERE `Construct` IS NOT
// SLOT 8 (RULE B5-16). *** Every other Video/Graphics external introduces
// `Construct` itself at slot 8; here `Particle` already spent slot 8 on
// `CreateSystem` (pure), so `Construct` enters the branch one slot later. Copying
// "Construct is slot 8" onto this class swaps the two and produces a
// `CreateSystem` that returns an `Object::External*` where callers expect a
// `ParticleInstance*` — a wrong-type call through the right index, which is
// silent. `tests/unit/graphics_particle_abi_test.cpp` PART B reads the
// GCC-generated table back slot by slot and pins both indices.
//
// CONCRETE IN RETAIL, DECLARED ABSTRACT HERE — slot 0 only. Nothing holds a
// `ParticleSystem` by value (B34_BRIEFS A.4 as amended), and its deleting-dtor
// body is a minted `RT_DELDTOR` row at 0x180020a70 that section 11 forbids
// retiring, so slot 0 stays `= 0` exactly as `AnimationGroup`'s does. Every
// other slot has a real body.
//
// *** THE LAYOUT IS AUTHORED HERE FOR THE FIRST TIME (B35_RULES section 13
// item 3: the SIZE was verified, the FIELDS were not). *** It is not invented:
// it MOVES the file-local `struct ParticleSystem` from
// `vtbatch7_animgroup_particlesystem.cpp:534-558` — which batch 7b pinned as a
// WRITER, off the disassembled 60-store constructor at 0x1800a5320 and off
// `Serialize`'s emission order — and it NAMES each field from the independent
// READER `wave28_lane_partsys.cpp:424-459` measured for
// `ParticleSystemInstance::Update`, where every `kPS_*` constant carries the
// instruction that reads it. Two independent witnesses, one per direction; the
// offsets were identical and nothing had to be reconciled. Fields the reader
// never touched keep a position name.
//
// TWO MEASURED GAPS, from the constructor's store list: +0x54..+0x57 (it writes
// +0x50 and then the int64 at +0x58) and +0xe4..+0xe7 (it writes +0xdc and +0xe0
// as ONE qword of -1 and then the int64 at +0xe8). `Serialize` skips both as
// well. They are named pads, which says exactly as much as is known.
// ============================================================================
//
// ENDGAME B.3 step 9 (block GFX, B39_RULES D1/D2) -- the "no real constructor"
// and "slot 0 pure" paragraphs are HISTORY. The class is CONCRETE: a real
// public ctor (retail 0x1800a5320, vtbatch7_animgroup_particlesystem.cpp), its
// own vector slot 0, a virtual ~ParticleSystem holding the
// retired ??1 door's body, the factory pair as the class's own statics. The
// minted table, its RT_DELDTOR row 0x180020a70, the FACTREST row and the ctor /
// dtor / slot doors are retired (tools/rt_mint_retired/gfx.txt).
class ParticleSystem : public Particle {
public:
    // ??0ParticleSystem@Graphics@@QEAA@XZ 0x1800a5320 -- the REAL ctor (also
    // inlined by retail into both factories and Construct / Reconstruct).
    // Out of line: it reads `System::Timer::_secondsPerTick`.
    ParticleSystem();

    // [0] 0x180020a70 -- MSVC's VECTOR deleting destructor, both arms exactly as
    // the retired RT_DELDTOR row ran them (`MOV EDX,0x108` = the element size).
    // ??1ParticleSystem@Graphics@@UEAA@XZ  0x1800a51c0. VIRTUAL
    // (B3X W1). Retail's body -- the owned pool
    // walk -- is in vtbatch7_animgroup_particlesystem.cpp; the compiler chains
    // ~Particle (retail's tail jump 0x1800a530d).
    ~ParticleSystem() override;

    // [1] 0x1800203e0 TypeName / [2] 0x1800203c0 Type, TypeId 0x1800203d0, `_TypeName` / `_ParentTypeName`
    // and the factory pair: retail's macro (ENDGAME B.3.9 TI-REST, B39_RULES
    // D6); the record is `DEFINE_TYPE`d in
    // src/engine/graphics/graphics_type_records.cpp.
    DECLARE_TYPE(ParticleSystem)
public:
    int32_t     GetFileVersion() const override;            // [3] 0x1800a4d10 -> 0x1001
    // [4] Compile stays Object::External's body, inherited through Particle.
    bool        Serialize(IO::Stream& stream) override;           // [5] 0x1800a4d20
    Object::External* Reconstruct() override;               // [6] 0x180020560
    void        Destruct() override;                        // [7] 0x180020590
    // [8] THE TRAP. `override`, because `Particle` introduced it pure at 8.
    ParticleInstance* CreateSystem() const override;        // [8] 0x1800a5140
    // [9] INTRODUCED HERE. `virtual`, not `override` (B5-16): no base of this
    //     class declares `Construct` — `Object::External`'s table is eight
    //     entries and `Particle`'s is nine.
    virtual Object::External* Construct();                  // [9] 0x180020580

    // ---- the non-slot exported members -------------------------------------
    // ??2ParticleSystem@Graphics@@SAPEAX_K@Z                 0x1800203f0 — NOT PORTED
    // ??2ParticleSystem@Graphics@@SAPEAX_KPEAX@Z             0x180020460 — NOT PORTED
    // ??3ParticleSystem@Graphics@@SAXPEAX@Z                  0x180020470 — NOT PORTED
    // ?CreateNewObject@…SAPEAVBase@Object@@XZ                0x1800204d0 — (D2) the class's own
    // ?CreateInPlaceObject@…SAPEAVBase@Object@@PEAX@Z        0x180020550 — (D2) the class's own
    //   The three operators stay declared and undefined: placement is `::new`.
    static void* operator new(std::size_t bytes);
    static void* operator new(std::size_t bytes, void* place);
    static void  operator delete(void* p) { ::Object::detail::InternalMemFree(p); }
    // (CreateNewObject / CreateInPlaceObject: declared by DECLARE_TYPE above.)

public:
    // ---- the 0xc0 bytes ParticleSystem adds --------------------------------
    // Each comment is the instruction that reads or writes the field: `ctor` is
    // 0x1800a5320's store list, `Update` is wave28's measured read.
    float    _emissionRate;            // +0x48  ctor 5.0f. Serialize emits it FIRST, and
                              //        Serialize's read-side tail RAW-BIT-COPIES
                              //        it into each pool record's +0x10. No
                              //        reader identified it, so it keeps a
                              //        position name.
    float    _emissionAcceleration;     // +0x4c  MULSS 0x4c(%rdi) @ 0x1800a5758; ctor 0.0f
    float    _emissionDrag;     // +0x50  MULSS 0x50(%rdi) @ 0x1800a5764; ctor 1.0f
    uint32_t _pad54;          // +0x54  MEASURED GAP — ctor and Serialize skip it
    int64_t  _durationTicks;  // +0x58  MOV 0x58(%rdi),%rcx @ 0x1800a5781.
                              //        ctor `(int64)(-1.0 / _secondsPerTick)` —
                              //        a NEGATIVE count, the "no duration" sentinel
    float    _emitDirX;       // +0x60  three floats @ 0x1800a5f65/73/89
    float    _emitDirY;       // +0x64
    float    _emitDirZ;       // +0x68  ctor (0,0,1)
    float    _spreadX;        // +0x6c  MULSS group @ 0x1800a5a68
    float    _spreadY;        // +0x70
    float    _spreadZ;        // +0x74  ctor (1,1,1)
    float    _spreadScale;    // +0x78  MULSS @ 0x1800a5976/59f3/5a9c.
                              //        *** SERIALIZED BEFORE _spreadX/Y/Z ***
    float    _angleBRange;    // +0x7c  MULSS @ 0x1800a5ec2
    float    _angleARange;    // +0x80  MULSS @ 0x1800a5e48
    float    _speedBase;      // +0x84  ADDSS @ 0x1800a5dc6
    float    _speedRange;     // +0x88  MULSS @ 0x1800a5dbd
    float    _accelScale;     // +0x8c  MOVSS @ 0x1800a62f1
    float    _damping;        // +0x90  MOVSS @ 0x1800a64ba
    float    _rotBase;        // +0x94  ADDSS @ 0x1800a5b98
    float    _rotRange;       // +0x98  MULSS @ 0x1800a5b90
    float    _spinBase;       // +0x9c  ADDSS @ 0x1800a5cbc
    float    _spinRange;      // +0xa0  MULSS @ 0x1800a5cb4
    float    _spinAccel;      // +0xa4  MULSS @ 0x1800a65eb
    float    _spinDamp;       // +0xa8  MULSS @ 0x1800a65fa
    float    _lifeBase;       // +0xac  ADDSS @ 0x1800a58e3
    float    _lifeRange;      // +0xb0  MULSS @ 0x1800a58db
    float    _gravScale;      // +0xb4  MOVSS @ 0x1800a6300
    float    _gravX;          // +0xb8
    float    _gravY;          // +0xbc
    float    _gravZ;          // +0xc0  ctor -9.8f (0xc11ccccd)
    float    _sizeBase;       // +0xc4  ADDSS @ 0x1800a5c2a
    float    _sizeRange;      // +0xc8  MULSS @ 0x1800a5c22
    float    _sizeAccel;      // +0xcc  MULSS @ 0x1800a6627
    float    _sizeDamp;       // +0xd0  MULSS @ 0x1800a6636
    float    _colorT0;        // +0xd4  SUBSS @ 0x1800a63db / 0x1800a6692
    float    _colorT1;        // +0xd8  MOVSS @ 0x1800a63d3
    uint32_t _color0;         // +0xdc  four PACKED BYTES, read one at a time by
                              //        Update; Serialize visits it as kScalarUInt
    uint32_t _color1;         // +0xe0  ditto. ctor writes both as one qword of -1
    uint32_t _pad_e4;         // +0xe4  MEASURED GAP — ctor and Serialize skip it
    int64_t  _fadeInTicks;    // +0xe8  CVTSI2SDQ 0xe8(%rdi) @ 0x1800a675c.
                              //        ctor `(int64)(0.25 / _secondsPerTick)`
    int64_t  _fadeOutTicks;   // +0xf0  MOV 0xf0(%rdi),%rcx @ 0x1800a672a. Same
                              //        ctor value, a SECOND DIVSD off one XMM1
    // +0xf8  Array<ParticleEmitter*> — the per-system emitter array `Serialize`
    // rebuilds on the read side, `SetSize(&_pool, _recordCount)` through the
    // registered `kSpec_Fill8` row. B.4: typed; a pointer element is trivially
    // destructible and ~ParticleSystem empties the header before the member
    // dtor runs, as for Particle::_records.
    Collection::Array<RtGraphics::ParticleEmitter*> _pool;   // +0xf8 {_data, _size@0x100, _capacity@0x104}
};

static_assert(sizeof(ParticleSystem) == 0x108,
              "Graphics::ParticleSystem is 0x108 bytes (the MOV EDX,0x108 in its "
              "own slot-0 vector deleting destructor 0x180020a70)");
static_assert(offsetof(ParticleSystem, _pad54) == 0x54, "ParticleSystem::_pad54");
static_assert(offsetof(ParticleSystem, _emitDirY) == 0x64, "ParticleSystem::_emitDirY");
static_assert(offsetof(ParticleSystem, _emitDirZ) == 0x68, "ParticleSystem::_emitDirZ");
static_assert(offsetof(ParticleSystem, _spreadY) == 0x70, "ParticleSystem::_spreadY");
static_assert(offsetof(ParticleSystem, _spreadZ) == 0x74, "ParticleSystem::_spreadZ");
static_assert(offsetof(ParticleSystem, _angleARange) == 0x80, "ParticleSystem::_angleARange");
static_assert(offsetof(ParticleSystem, _speedBase) == 0x84, "ParticleSystem::_speedBase");
static_assert(offsetof(ParticleSystem, _speedRange) == 0x88, "ParticleSystem::_speedRange");
static_assert(offsetof(ParticleSystem, _accelScale) == 0x8C, "ParticleSystem::_accelScale");
static_assert(offsetof(ParticleSystem, _damping) == 0x90, "ParticleSystem::_damping");
static_assert(offsetof(ParticleSystem, _rotBase) == 0x94, "ParticleSystem::_rotBase");
static_assert(offsetof(ParticleSystem, _rotRange) == 0x98, "ParticleSystem::_rotRange");
static_assert(offsetof(ParticleSystem, _spinBase) == 0x9C, "ParticleSystem::_spinBase");
static_assert(offsetof(ParticleSystem, _spinRange) == 0xA0, "ParticleSystem::_spinRange");
static_assert(offsetof(ParticleSystem, _spinAccel) == 0xA4, "ParticleSystem::_spinAccel");
static_assert(offsetof(ParticleSystem, _spinDamp) == 0xA8, "ParticleSystem::_spinDamp");
static_assert(offsetof(ParticleSystem, _lifeBase) == 0xAC, "ParticleSystem::_lifeBase");
static_assert(offsetof(ParticleSystem, _lifeRange) == 0xB0, "ParticleSystem::_lifeRange");
static_assert(offsetof(ParticleSystem, _gravY) == 0xBC, "ParticleSystem::_gravY");
static_assert(offsetof(ParticleSystem, _gravZ) == 0xC0, "ParticleSystem::_gravZ");
static_assert(offsetof(ParticleSystem, _sizeRange) == 0xC8, "ParticleSystem::_sizeRange");
static_assert(offsetof(ParticleSystem, _sizeAccel) == 0xCC, "ParticleSystem::_sizeAccel");
static_assert(offsetof(ParticleSystem, _sizeDamp) == 0xD0, "ParticleSystem::_sizeDamp");
static_assert(offsetof(ParticleSystem, _colorT1) == 0xD8, "ParticleSystem::_colorT1");
static_assert(offsetof(ParticleSystem, _pad_e4) == 0xE4, "ParticleSystem::_pad_e4");
static_assert(alignof(ParticleSystem) == 8, "qword aligned (the vptr)");
static_assert(offsetof(ParticleSystem, _emissionRate) == sizeof(Particle),
              "ParticleSystem's own fields begin at the end of the Particle base: "
              "the ctor inlines Particle's stores and then writes +0x48 with no gap");
static_assert(offsetof(ParticleSystem, _emissionRate)           == 0x48,  "+0x48");
static_assert(offsetof(ParticleSystem, _emissionAcceleration)    == 0x4c,  "+0x4c");
static_assert(offsetof(ParticleSystem, _emissionDrag)    == 0x50,  "+0x50");
static_assert(offsetof(ParticleSystem, _durationTicks) == 0x58,  "+0x58");
static_assert(offsetof(ParticleSystem, _emitDirX)      == 0x60,  "+0x60");
static_assert(offsetof(ParticleSystem, _spreadX)       == 0x6c,  "+0x6c");
static_assert(offsetof(ParticleSystem, _spreadScale)   == 0x78,  "+0x78");
static_assert(offsetof(ParticleSystem, _angleBRange)   == 0x7c,  "+0x7c");
static_assert(offsetof(ParticleSystem, _gravScale)     == 0xb4,  "+0xb4");
static_assert(offsetof(ParticleSystem, _gravX)         == 0xb8,  "+0xb8");
static_assert(offsetof(ParticleSystem, _sizeBase)      == 0xc4,  "+0xc4");
static_assert(offsetof(ParticleSystem, _colorT0)       == 0xd4,  "+0xd4");
static_assert(offsetof(ParticleSystem, _color0)        == 0xdc,  "+0xdc");
static_assert(offsetof(ParticleSystem, _color1)        == 0xe0,  "+0xe0");
static_assert(offsetof(ParticleSystem, _fadeInTicks)   == 0xe8,  "+0xe8");
static_assert(offsetof(ParticleSystem, _fadeOutTicks)  == 0xf0,  "+0xf0");
static_assert(offsetof(ParticleSystem, _pool)          == 0xf8,  "+0xf8");
// THE FIFTEEN-FLOAT RUN Serialize emits one scalar at a time, +0x7c..+0xb4
// inclusive. If a field is ever inserted in the middle, this is what fails
// rather than the stream silently shifting by four bytes.
static_assert(offsetof(ParticleSystem, _gravScale) - offsetof(ParticleSystem, _angleBRange)
                  == 14 * sizeof(float),
              "Serialize's 15 consecutive float scalars run +0x7c..+0xb4");


}  // namespace Graphics

// ==== B3R W0-P0c: Runtime bodies reached by retail address ====
// ENDGAME B.3R (B3R_RULES R5): each body below is registered at the retail
// address in its trailing comment and read through the registry today. It is
// declared here, copied from its definition (linkage, namespace, types), so the
// reader can call it by name. Nothing calls it through this declaration yet.

// ---- graphics/wave27_lane_gfxpart.cpp
extern "C" void* RtGraphics_Emitter_OperatorNew(void);  // 0x1800205a0
extern "C" void RtGraphics_Emitter_OperatorDelete(void* p);  // 0x180020600
extern "C" void* RtGraphics_Emitter_DeleteDtor(void* p);  // 0x180020d60
// 0x180103bf0 is RtGraphics::ParticleBoundsRecord's C++ ctor (ParticleInstance.h).
extern "C" void* RtGraphics_ParticlePool_OperatorNew();  // 0x180061a80
// ==== end B3R W0-P0c ====
