#pragma once
// ============================================================================
// src/engine/graphics/ParticleInstance.h — the real C++
//   `Graphics::ParticleInstance` and `Graphics::ParticleSystemInstance`
//                                              (ENDGAME B.3 step 5, block E)
// ============================================================================
//
// WHY THIS IS A SEPARATE HEADER RATHER THAN THE BOTTOM OF `Particle.h`.
//
// The two classes derive from the real `Graphics::Geometry`, so they need
// `Geometry.h` complete — and `Geometry.h` includes
// `src/engine/volume/Volume.h` for `Box`. `Volume.h` declares
// `struct Rectangle`, and `<wingdi.h>` declares a FUNCTION called `Rectangle`,
// so `Volume.h` cannot be reached from a translation unit that has already
// pulled in `<windows.h>`. That is the same collision `src/engine/graphics/
// scene.cpp` and five of its siblings dodge by forward-declaring `ResolveAddr`
// by hand instead of including `standalone/resolve.h`.
//
// `src/engine/graphics/vtbatch7_animgroup_particlesystem.cpp` DOES include
// `standalone/resolve.h`, and it includes `Particle.h`. That file belongs to
// block H2, not to block E, so adding `Geometry.h` to `Particle.h` would have
// broken another block's file with a `Volume::Rectangle must be 0x10 bytes`
// static-assert failure — measured, not predicted: it was the first build error
// of this block. Keeping the classes here means `Particle.h` is byte-for-byte
// as `vtbatch7` has always seen it, and only the files that want the real
// classes pay for `Volume.h`.
//
// Everything else about these two classes — the slot lists, their verification
// against `standalone/rt_data_mint_tables.inc`, and the layout evidence — is in
// the banner below and in `Particle.h`'s own banners for the PODs they are
// cross-asserted against.
// ============================================================================

#include <cstdint>
#include <cstddef>

#include "Particle.h"      // the RtGraphics PODs these are asserted against
#include "Geometry.h"      // Graphics::Geometry, the real base

// ============================================================================
// B.4: the pool record and the live-particle record, NAMED FIELD BY FIELD.
// Both were described by offset constants in the two files that read them
// (wave27_lane_gfxpart.cpp's kPool_* / kRec_*, wave28_lane_partsys.cpp's, which
// measured the rest); the names are the readers', and every offset below is one
// of those constants. Neither type has a retail name (no mangled symbol names
// either -- Particle.h's banner); `ParticleBoundsRecord` is this tree's.
//
// The 0x58-byte LIVE PARTICLE, element of `ParticleBoundsRecord::_records`
// (SetSize 0x180061bb0). ParticleSystemInstance::Update writes it and both
// particle Draws read it; each pair is {current, previous}.
// ============================================================================
namespace RtGraphics {

struct ParticleRecord {
    int64_t       _lifetime;    // +0x00  ticks
    int64_t       _age;         // +0x08  ticks
    Math::Vector3 _pos;         // +0x10  current
    Math::Vector3 _posPrev;     // +0x1c
    Math::Vector3 _color;       // +0x28  rgb, current (Wave 27: "PosB")
    Math::Vector3 _colorPrev;   // +0x34
    float         _alpha;       // +0x40  current (Wave 27: "Scroll")
    float         _alphaPrev;   // +0x44
    float         _rot;         // +0x48  DEGREES, current
    float         _rotPrev;     // +0x4c
    float         _size;        // +0x50  current
    float         _sizePrev;    // +0x54
};
static_assert(sizeof(ParticleRecord) == 0x58, "the live-particle stride is 0x58");
static_assert(offsetof(ParticleRecord, _lifetime) == 0x0, "ParticleRecord::_lifetime");
static_assert(offsetof(ParticleRecord, _age) == 0x8, "ParticleRecord::_age");
static_assert(offsetof(ParticleRecord, _pos)       == 0x10, "ParticleRecord::_pos @ +0x10");
static_assert(offsetof(ParticleRecord, _posPrev)   == 0x1c, "ParticleRecord::_posPrev @ +0x1c");
static_assert(offsetof(ParticleRecord, _color)     == 0x28, "ParticleRecord::_color @ +0x28");
static_assert(offsetof(ParticleRecord, _colorPrev) == 0x34, "ParticleRecord::_colorPrev @ +0x34");
static_assert(offsetof(ParticleRecord, _alpha)     == 0x40, "ParticleRecord::_alpha @ +0x40");
static_assert(offsetof(ParticleRecord, _alphaPrev) == 0x44, "ParticleRecord::_alphaPrev @ +0x44");
static_assert(offsetof(ParticleRecord, _rot)       == 0x48, "ParticleRecord::_rot @ +0x48");
static_assert(offsetof(ParticleRecord, _rotPrev)   == 0x4c, "ParticleRecord::_rotPrev @ +0x4c");
static_assert(offsetof(ParticleRecord, _size)      == 0x50, "ParticleRecord::_size @ +0x50");
static_assert(offsetof(ParticleRecord, _sizePrev)  == 0x54, "ParticleRecord::_sizePrev @ +0x54");

// The 0x40-byte POOL RECORD (`ParticleInstance::_boundsPtr`, element of
// `Particle::_records`). Built by the unexported C++ ctor 0x180103bf0 (it
// carries an EH state for `_records`), placement-new'd into the 0x40 block its
// two callers allocate: `_bounds` seeded (+1e8 x3, -1e8 x3), `_records` zeroed
// and then sized to `particle->_subCount`, `_liveCount` 0, `frameTag` -1, the
// two tick counters 0.
struct ParticleBoundsRecord {
    explicit ParticleBoundsRecord(const Graphics::Particle* particle);   // 0x180103bf0 (wave27_lane_gfxpart.cpp)

    ::Box    _bounds;                             // +0x00  handed to SetBounds as `const Box&`
    Collection::Array<ParticleRecord> _records;   // +0x18  {_data, _size@0x20, _capacity@0x24}
    int32_t  _liveCount;                          // +0x28  read by both Draws and AddDrawItems
    int32_t  frameTag;                            // +0x2c  ctor -1; vs ParticleInstance::_frameIndex
    int64_t  _spawnAccum;                         // +0x30  ticks (Update)
    int64_t  _totalTicks;                         // +0x38  ticks (Update)
};
static_assert(sizeof(ParticleBoundsRecord) == 0x40, "both allocation sites ask for 0x40");
static_assert(offsetof(ParticleBoundsRecord, _bounds) == 0x0, "ParticleBoundsRecord::_bounds");
static_assert(offsetof(ParticleBoundsRecord, _records)    == 0x18, "ParticleBoundsRecord::_records @ +0x18");
static_assert(offsetof(ParticleBoundsRecord, _liveCount)  == 0x28, "ParticleBoundsRecord::_liveCount @ +0x28");
static_assert(offsetof(ParticleBoundsRecord, frameTag)    == 0x2c, "ParticleBoundsRecord::frameTag @ +0x2c");
static_assert(offsetof(ParticleBoundsRecord, _spawnAccum) == 0x30, "ParticleBoundsRecord::_spawnAccum @ +0x30");
static_assert(offsetof(ParticleBoundsRecord, _totalTicks) == 0x38, "ParticleBoundsRecord::_totalTicks @ +0x38");

}  // namespace RtGraphics

// ############################################################################
// ENDGAME B.3 step 5 (block E) — `Graphics::ParticleInstance` and
// `Graphics::ParticleSystemInstance`, the REAL C++ classes
// ############################################################################
//
// `Graphics::Particle` and `Graphics::ParticleSystem` are NOT this block's
// classes — they are block H2's, and the two PODs above are left untouched.
// The two *Instance classes ARE this block's: they are `Graphics::Geometry`
// subclasses (B35_RULES §2.1), not `Object::External` subclasses, and RULE B5-5
// puts them in the same block as `Geometry`.
//
// THE SLOT LISTS WERE RE-VERIFIED AGAINST `standalone/rt_data_mint_tables.inc`,
// which is generated from retail and is an authority on slot targets, as the
// block brief requires. `kVSlots98` (ParticleInstance, 0x1801550e0, 10 slots)
// and `kVSlots100` (ParticleSystemInstance, 0x180155040, 10 slots) agree with
// `B35_SLOTS.md` on all twenty entries, target for target, including the two
// that `B35_RULES.md` §13 item 6 flags as INFERRED elsewhere. Nothing needed
// correcting.
//
// ABSTRACT, per B34_BRIEFS §A.4: nothing holds either class by value as a C++
// member, and slot 0's body is a minted row (§11), so slot 0 stays `= 0`.
//
// ENDGAME B.3 step 9 (block GFX, B39_RULES D1): THAT PARAGRAPH IS HISTORY.
// Both slot 0s are the classes' own VECTOR deleting dtors; ParticleInstance
// stays abstract (slot 1 `_purecall`, as retail), ParticleSystemInstance is
// CONCRETE with a public real ctor. The ??0 / ??1 doors, `DoorBody`, both
// RT_DELDTOR rows (0x180020310 / 0x180020ce0) and both minted tables are
// retired (tools/rt_mint_retired/gfx.txt).
// ############################################################################

namespace Graphics {

// ============================================================================
// Graphics::ParticleInstance : Graphics::Geometry — 0xd8, TEN slots, ABSTRACT.
//
// `??_7ParticleInstance@Graphics@@6B@` @ 0x1801550e0:
//   [0] 0x180020310  (unexported) vector deleting destructor
//   [1] _purecall    inherited-from Geometry   Update(const Time&)  <- STILL PURE
//   [2] 0x180104230  ?Draw@…MHHH@Z              wave27_lane_gfxpart.cpp
//   [3] 0x180104700  ?AddDrawItems@…            wave27_lane_gfxpart.cpp
//   [4] 0x1800a4020  Geometry::UpdateAttached            INHERITED
//   [5] 0x1800a43c0  Geometry::GetSubTransform           INHERITED
//   [6] 0x1801047f0  ?GetInstanceTransform@…    wave27_lane_gfxpart.cpp  RETSLOT
//   [7] 0x180015590  Geometry::SetSharedInstanceData     INHERITED
//   [8] 0x1800a4420  Geometry::Clean                     INHERITED
//   [9] 0x180104a00  ?SetParticle@…             wave27_lane_gfxpart.cpp
//
// Slot 1 is still `_purecall` here — `ParticleInstance` does NOT fill it, and
// that single pure slot is the whole reason B35_RULES §2.2 calls this class
// ABSTRACT. `ParticleSystemInstance` fills it.
//
// *** SLOT 6 IS THE 2026-09-13 PERF-ONLY CRASH (RULE B5-12). ***
// `?GetInstanceTransform@ParticleInstance@Graphics@@UEBA?AVMatrix@Math@@HM@Z`
// was ported as `void`; the hidden return slot arrives in RDX and must come
// back in RAX at every exit. At -O0 RAX happened to still hold it; at -O2 it
// held garbage and `Renderer::DrawItemList` -> `Context::SetTransform`
// dereferenced it (`1a4200c3`). Declaring it here as a by-value
// `Math::Matrix` return is what makes GCC produce the same shape MSVC did.
// VERIFY AT -O2, i.e. in the PERF build; a debug build cannot see this.
//
// *** A NAME-HIDING TRAP `B35_RULES` DOES NOT LIST. *** Retail gives this class
// TWO members called `Update`:
//    slot 1, INHERITED and PURE:  ?Update@X@Graphics@@UEAAXAEBVTime@System@@@Z
//    NON-VIRTUAL:                 ?Update@ParticleInstance@Graphics@@QEAA_NXZ
//                                 @ 0x180104180 -> `bool Update()`
// A derived-class declaration of either name HIDES the other, silently, which
// is the same hazard B5-17 names for the two Light `Draw` overloads eight slots
// apart. Both retail names are kept (they are different signatures, so C++
// overload resolution handles them once they are in ONE scope) and a `using`
// declaration pulls the base's spelling back into view at each level. The
// non-virtual `Update` is PORTED in `src/engine/graphics/animation.cpp`, which
// is NOT this block's file: its door and its body stay where they are.
// ============================================================================
class ParticleInstance : public Geometry {
public:
    ParticleInstance(const ParticleInstance& src);            // 0x180020200  not ported
    ParticleInstance& operator=(const ParticleInstance& src); // 0x180020270  not ported

    // Keep `Geometry::Update(const System::Time&)` visible beside the
    // non-virtual `bool Update()` below — see the name-hiding note above.
    using Geometry::Update;

    // [0] 0x180020310 -- MSVC's VECTOR deleting destructor, both arms exactly as
    // the retired RT_DELDTOR row ran them (`MOV EDX,0xd8` = the element size).
    // Every subclass (ParticleSystemInstance, the exe's TornadoInstance)
    // declares its own (B9-4).
    // ??1ParticleInstance@Graphics@@UEAA@XZ  0x1801041f0, retail whole:
    //     LEA RAX,[??_7ParticleInstance] ; MOV [RCX],RAX ; CALL 0x1801040b0
    //     (RemoveState) ; JMP 0x1800a41c0 (~Geometry)
    // The restamp and the ~Geometry tail are the compiler's; the body is the
    // call. (ENDGAME B.3.9 GFX: the ??1 door in deleting_dtor_leaves.cpp is
    // retired with the RT_DELDTOR row it served.)
    ~ParticleInstance() override { RemoveState(); }
    // slot [1] Update(const Time&) is NOT overridden here: still _purecall.
    void Draw(Video::Context& context, const Camera& camera,
              ExternalPtrHandle material,
              float opacity, int a, int b, int c) const override;          // [2] 0x180104230
    void AddDrawItems(const Scene& scene, Renderer& renderer) const override;  // [3] 0x180104700
    // Retail order for the retslot — see Geometry.h's B5-12 block. THIS is the
    // slot the 2026-09-13 perf-only crash came through.
    Math::Matrix* GetInstanceTransform(Math::Matrix* ret, int node,
                                       float t) const override;            // [6] 0x1801047f0 RETSLOT
    virtual void SetParticle(Object::ExternalPtr<Particle> particle);      // [9] 0x180104a00
    // slots 4, 5, 7, 8 are Geometry's own bodies, inherited unchanged.

    // EXT SEAM (NOT RETAIL, docs/extensions/handins/CREATEONDEMAND.md, DESIGN.md hook rule 5):
    // ext.create_on_demand's prefetch. Null by default and under the faithful profile. Run at the end
    // of SetParticle with the instance holding its particle: the extension queues the first-use
    // creates of the particle's material textures, ahead of the first emission.
    static void (*_extParticleAssigned)(const Particle& particle);

    // `?Update@ParticleInstance@Graphics@@QEAA_NXZ` @ 0x180104180 — NON-VIRTUAL,
    // returns bool, takes nothing. PORTED, body in `animation.cpp` (NOT this
    // block's file), so the door keeps it and this member is declared only.
    bool Update();

    // `?RemoveState@ParticleInstance@Graphics@@IEAAXXZ` @ 0x1801040b0.
    // `IEAA` = PROTECTED, and this DLL exports its private and protected
    // members (reference_aeaa_is_private_not_unexported.md). PORTED, body in
    // `wave28_lane_partsys.cpp` — this block's file.
    void RemoveState();

    // Exported, NOT PORTED, no caller: declared, deliberately undefined.
    Object::ExternalPtr<Particle> GetParticle() const;                     // 0x1800201e0

    // `?IncrementInstanceIndex@ParticleInstance@Graphics@@SAXXZ` @ 0x1800201f0.
    // A CLASS STATIC over `_frameIndex` below. PORTED in `animation.cpp`.
    static void IncrementInstanceIndex();

    // ?_frameIndex@ParticleInstance@Graphics@@1HA  0x1801fe77c — retail's
    // PROTECTED static int (`1`). ENDGAME B.3.9 block GFX (B39_RULES D7 row
    // A37): the class static, defined in animation.cpp and REDIRECTED onto the
    // mint's storage row by RtGraphicsPortInit, so the doors the foreign
    // readers use (particlecomponent.cpp, reg_unreg_lane3.cpp's import slot)
    // and this member are ONE int. Public because a reader in this block is a
    // free-function body (wave28_lane_partsys.cpp).
    static int _frameIndex;

protected:
    // ??0ParticleInstance@Graphics@@QEAA@XZ  0x180104130 — the REAL constructor:
    // Geometry() and the stamp are the compiler's; the three stores are the
    // body. (ENDGAME B.3.9 GFX: the ??0 door and `DoorBody` are retired.)
    ParticleInstance() {
        _resource  = nullptr;   // +0xc0
        _boundsPtr = nullptr;   // +0xc8
        _index     = -1;        // +0xd0  (+0xd4 not written)
    }

public:
    // B.4: the resource IS a `Graphics::Particle` (`_active` +0x20, `_records`
    // +0x38 -- the POD `RtGraphics::ParticleResource` describes those two).
    Particle*                         _resource;   // +0xc0  null until attached
    RtGraphics::ParticleBoundsRecord* _boundsPtr;  // +0xc8  cached by Update
    int32_t                           _index;      // +0xd0  ctor -1
    int32_t                           _pad_d4;     // +0xd4  not written by the ctor
};

static_assert(sizeof(ParticleInstance) == 0xd8,
              "Graphics::ParticleInstance is 0xd8 bytes (MOV EDX,0xd8 in its own "
              "slot-0 vector deleting destructor 0x180020310)");
static_assert(offsetof(ParticleInstance, _pad_d4) == 0xD4, "ParticleInstance::_pad_d4");
static_assert(alignof(ParticleInstance) == 8, "qword aligned (the vptr)");
static_assert(offsetof(ParticleInstance, _resource)  == 0xc0, "ParticleInstance::_resource @ +0xc0 (??0 0x180104130)");
static_assert(offsetof(ParticleInstance, _boundsPtr) == 0xc8, "ParticleInstance::_boundsPtr @ +0xc8 (Update 0x180104180)");
static_assert(offsetof(ParticleInstance, _index)     == 0xd0, "ParticleInstance::_index @ +0xd0 (ctor -1)");
static_assert(offsetof(ParticleInstance, _resource) == sizeof(Geometry),
              "ParticleInstance's own fields begin at the end of the Geometry base: "
              "both ctors call Geometry::Geometry and then write +0xc0 with no gap");

// ============================================================================
// Graphics::ParticleSystemInstance : Graphics::ParticleInstance — 0xe0, TEN slots.
//
// `??_7ParticleSystemInstance@Graphics@@6B@` @ 0x180155040:
//   [0] 0x180020ce0  (unexported) vector deleting destructor
//   [1] 0x1800a5600  ?Update@ParticleSystemInstance@…   wave28_lane_partsys.cpp
//                    <- the class that finally FILLS Geometry's slot 1
//   [2] 0x180104230  ParticleInstance::Draw                  INHERITED
//   [3] 0x180104700  ParticleInstance::AddDrawItems          INHERITED
//   [4] 0x1800a4020  Geometry::UpdateAttached                INHERITED
//   [5] 0x1800a43c0  Geometry::GetSubTransform               INHERITED
//   [6] 0x1801047f0  ParticleInstance::GetInstanceTransform  INHERITED
//   [7] 0x180015590  Geometry::SetSharedInstanceData         INHERITED
//   [8] 0x1800a4420  Geometry::Clean                         INHERITED
//   [9] 0x1800a6a80  ?SetParticle@ParticleSystemInstance@…  wave27_lane_gfxpart.cpp
//
// NOTE what is NOT here: `?CreateSystem@` and `?Construct@`. Those belong to
// `Graphics::ParticleSystem` (an `Object::External` subclass, block H2), whose
// table is the one with B35_RULES §2.3 item 2's slot-order trap — `Construct`
// is at slot 9 BEHIND `CreateSystem` at slot 8. That trap is block H2's; it
// cannot reach this class, which has no `Construct` at all.
// ============================================================================
class ParticleSystemInstance : public ParticleInstance {
public:
    ParticleSystemInstance(const ParticleSystemInstance& src);             // 0x180020bd0  not ported
    ParticleSystemInstance& operator=(const ParticleSystemInstance& src);  // 0x180020c50  not ported

    // Keep `ParticleInstance::Update()` (the non-virtual bool overload) visible
    // beside the slot-1 override below — see ParticleInstance's name-hiding note.
    using ParticleInstance::Update;

    // [0] 0x180020ce0 -- MSVC's VECTOR deleting destructor, both arms exactly as
    // the retired RT_DELDTOR row ran them (`MOV EDX,0xe0` = the element size).
    // ??1ParticleSystemInstance@Graphics@@UEAA@XZ 0x1800a5520. VIRTUAL (B3X W1).
    // ENDGAME B.3.9 GFX: the retired door's body (batchc_gfx_leaves.cpp); the
    // restamps, ~ParticleInstance (RemoveState) and ~Geometry are the chain.
    ~ParticleSystemInstance() override;
    void Update(const System::Time& time) override;                        // [1] 0x1800a5600
    void SetParticle(Object::ExternalPtr<Particle> particle) override;      // [9] 0x1800a6a80
    // slots 2..8 are ParticleInstance's / Geometry's bodies, inherited unchanged.

    // ??0ParticleSystemInstance@Graphics@@QEAA@XZ 0x1800a55a0 -- the REAL
    // constructor, PUBLIC: Geometry(), ParticleInstance()'s three stores and
    // both stamps are the chain; `+0xd8 = 0` is the body. (ENDGAME B.3.9 GFX:
    // the ??0 door is retired; ParticleSystem::CreateSystem and the exe menu
    // scene spell `::new (p) ParticleSystemInstance()`.)
    ParticleSystemInstance() { _emitter = nullptr; }

public:
    // +0xd8  B.4: typed -- the emitter record (Particle.h); the name stays,
    // tests/unit/graphics_geometry_abi_test.cpp pins it. Borrowed from
    // `ParticleSystem::_pool[_index]` when the resource is active, owned
    // (built by SetParticle, freed by ~ParticleSystemInstance) when it is not.
    RtGraphics::ParticleEmitter* _emitter;
};

static_assert(sizeof(ParticleSystemInstance) == 0xe0,
              "Graphics::ParticleSystemInstance is 0xe0 bytes (MOV EDX,0xe0 in "
              "its own slot-0 vector deleting destructor 0x180020ce0)");
static_assert(alignof(ParticleSystemInstance) == 8, "qword aligned (the vptr)");
static_assert(offsetof(ParticleSystemInstance, _emitter) == 0xd8,
              "ParticleSystemInstance::_emitter @ +0xd8 (the one extra field its ctor zeroes)");
static_assert(offsetof(ParticleSystemInstance, _emitter) == sizeof(ParticleInstance),
              "its own field begins at the end of the ParticleInstance base");

}  // namespace Graphics
