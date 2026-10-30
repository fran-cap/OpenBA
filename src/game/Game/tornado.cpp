// src/game/Game/tornado.cpp
// Standalone factory for Tornado (factory cascade — last concrete misc landmine).
//
// Tornado (size 0x90) extends Graphics::Particle. The exe factory
// (Toolbar_StaticInit_sub_4e90 @ 0x140024e90):
//     p = MemAlloc(0x90, 8); return FUN_1400d23c0(p);
// and the ctor FUN_1400d23c0 @ 0x1400d23c0:
//     Graphics::Particle::Particle(p)        // base ctor, sets base vtable + [0x00,0x48)
//     *(p)        = &vtable_Tornado(0x140160190)
//     <float block at +0x48 .. +0x70>
//     *(qword p+0x74) = -1                   // = two System::Colors, both opaque white
//     System::Time::Time(p+0x80, g_fUVScale)
//     System::Time::Time(p+0x88, g_fUVScale)
//
// vtable_Tornado at 0x140160190 is TEN slots (0x140160190..0x1401601d8). What looks
// like slots 10-19 is the separate TornadoInstance vtable starting at 0x1401601e0 —
// the two share an identical 0x14003aXXX Runtime-thunk tail, which is exactly what
// makes them read as one 20-slot table. Slots, from the BYTES:
//     3 = 0x1400d2370  GetFileVersion | 1
//     5 = 0x1400d2210  Serialize        <- reads the .rsc tornado fields
//     8 = 0x1400d21b0  CreateInstance   -> TornadoInstance (vtable 0x1401601e0,
//                                          whose slot 1 = 0x1400d2470 is the swirl sim)
// the rest are Object boilerplate or Runtime thunks.
//
// The Tornado particle is PURELY VISUAL. All tornado gameplay — the path, the
// movement, and the destruction — lives in TornadoComponent
// (src/game/Component/tornadocomponent.cpp, vtable 0x14015aa80), which does not
// touch this class at all.
//
// ENDGAME B.3.8 block LOOP: `Tornado` and `TornadoInstance` are REAL classes
// (src/game/Game/Tornado.h) with compiler vtables. Gone: the vtable COPY site --
// InstallTornadoVtable / Tornado_CreateInstance captured a live Particle /
// ParticleInstance table and patched slots 1/2/3/5/6/7/8/9 and 1 into static
// copies -- the ctor doors for both bases (the compiler runs the real ctors), the
// lazy type-id cache and REGISTER_TYPE (DEFINE_EXE_TYPE).
//
// Slots 6/7/8 of Graphics::Particle are all _purecall (0x18007ec28) -- the base class
// is abstract, there is nothing to "fall back to" -- so the moment Serialize made the
// resource load, slot 8 fired and the process died with R6025. That aborts through the
// MSVC runtime dialog, not a VEH exception, so standalone_crash.log stays EMPTY and
// tools/boot_state.sh reports BLOCKED_DIALOG. Same for ParticleInstance slot 1.
//
// WHY SLOT 5 STOPPED BEING OPTIONAL (2026-08-22 follow-up wave)
// -------------------------------------------------------------
// While the ParticleComponent finder was broken this class was never asked to load, so
// leaving Serialize dormant cost nothing. Once the finder worked, the tornado spawn
// began crashing in ParticleComponent_Draw+0x177 on a READ of 0x7c -- geoArr[0].geo was
// null. The trace in ParticleComponent_Setup narrowed it to nullResource=1: the
// ParticleAttachment ExternalPtr ITSELF was null, one layer above the dormant slots.
// Two measurements pinned the cause:
//   * all 9 other ParticleComponents in the world report nullResource=0 nullInstance=0.
//     Every one of them points at a Runtime ParticleSystem; the tornado alone points at
//     this exe-side class. So external resolution works -- this class was special.
//   * a factory trace showed the object IS built at load time and the ExternalPtr is
//     STILL null at spawn. So External::Load constructs it and then REJECTS it.
// The rejection is what a dormant Serialize buys you: the base Particle::Serialize
// consumes only the base fields and leaves the remaining 15 (9+2 floats, 2 Colors,
// 2 Times) of every Tornado.rsc body unread, so the load fails and stores null.

// ENDGAME B.3.5 Video/Graphics caller batch: Graphics::ParticleInstance (and its
// Graphics::Geometry base) are real classes. FIRST in the block on purpose —
// "resolve.h" pulls <windows.h>, whose <wingdi.h> declares a global `Rectangle`
// FUNCTION that hides `Volume::Rectangle`'s class name inside Volume.h's own
// static_asserts (Geometry.h includes Volume.h).
#include "graphics/ParticleInstance.h"
#include "Game/Tornado.h"
#include "object/Internal.h"   // Object::detail::InternalMemAlloc / InternalMemFree
#include "object/External.h"
#include "object/TypeInfo.h"
#include "graphics/Camera.h"
#include "math/Math.h"
#include "crt/msvc_libm.h"     // MsvcLibm::Sinf / Cosf (the exe's sinf / cosf)
#include "volume/Volume.h"        // RtVolume_Box_Extend
#include "Game/exe_constants.h"   // kRdata_*
#include "io/Serializer.h"   // ENDGAME B.3.4 caller batch B
#include <cstdint>
#include <cstdio>
#include <cstddef>
#include <cmath>
#include <new>
#include <initializer_list>   // caller batch CB6: Serialize's field list
#include <atomic>
#include <mutex>
#include "system/Value.h"      // ENDGAME B.3.4b System caller batch (Random/Time/Color)

// Kept from the diagnosis pass: it separates the two failures that both end in a null
// ExternalPtr. Factory never called => External::Load found no type at all. Factory
// called and the pointer still null => the object was built and then REJECTED, which is
// what an unread .rsc body looks like.
namespace {
std::atomic<int> s_factoryTraceBudget{0};
std::atomic<int> s_serializeTraceBudget{0};
std::atomic<int> s_updateTicks{0};
std::atomic<int> s_updateTraceBudget{0};
std::atomic<bool> s_updateBailSaid{false};
}  // namespace

static void TornadoFactoryTrace(const char* who, void* p) {
    if (s_factoryTraceBudget.fetch_add(1) >= 8) return;
    fprintf(stderr, "[Tornado/particle] %s -> %p (real class; the instance swirl runs "
            "on TornadoInstance::Update)\n", who, p);
    fflush(stderr);
}

// ---------------------------------------------------------------------------
// Tornado::Tornado -- FUN_1400d23c0. Graphics::Particle() and the 0x140160190
// stamp are the compiler's; then the Tornado defaults. Field meanings are from
// Tornado::Serialize (0x1400d2210, which names the types) cross-read against
// TornadoInstance::Update (0x1400d2470, which consumes them).
// ---------------------------------------------------------------------------
Tornado::Tornado() {
    _tornadoWidth        = 4.0f;    // 0x40800000
    _tornadoHeight       = 20.0f;   // 0x41a00000  rise over a life
    _funnelWidth         = 5.0f;    // 0x40a00000  outer sweep
    _funnelSpin          = 45.0f;   // 0x42340000  degrees/sec
    _particleSpinSpeed   = 1.0f;    // 0x3f800000
    _emissionRate        = 1.0f;    // 0x3f800000  interval = 1/x seconds
    _particleLifeTime    = 10.0f;   // 0x41200000
    _particleLifeTimeTol = 0.0f;
    _particleStartSize   = 0.2f;    // 0x3e4ccccd  also the spawn size
    _particleEndSize     = 3.0f;    // 0x40400000
    _particleScaleSize   = 30.0f;   // 0x41f00000  clamped into [start, end]
    // Two System::Colors -- START and END tint -- all-0xFF = opaque white at both
    // ends; Update divides their bytes by 255.0 (DAT_14017222c).
    _startColor._value = 0xFFFFFFFFu;
    _endColor._value   = 0xFFFFFFFFu;
    // Two System::Time fields seeded with g_fUVScale (= 1.0f): fade-in and fade-out.
    {
        float uv = kRdata_14017214c;   // 0x14017214c
        RtSystem_Time_Ctor(&_particleFadeIn, uv);
        RtSystem_Time_Ctor(&_particleFadeOut, uv);
    }
}

// Retail slot 3 (0x1400d2370): call the base GetFileVersion through dispatch slot
// 0x1401527c0, then OR in bit 0.
//
// The base call is `Graphics::Particle::GetFileVersion`, spelled QUALIFIED: this
// function IS slot 3 of the Tornado table, so an unqualified virtual call would
// dispatch straight back into it.
// ARITY: `?GetFileVersion@Particle@Graphics@@UEBAHXZ` takes NO argument, so `a2` — which
// the old typedef forwarded as a hedge against Ghidra dropping a parameter — is not
// passed. The mangled name is the authority, and retail's slot-3 body
// (`MOV EAX,0x1000; RET`) reads nothing but its return constant.
int32_t Tornado::GetFileVersion() const {
    return Graphics::Particle::GetFileVersion() | 1;
}

// Retail slot 5 (0x1400d2210). Faithful, in retail order -- and the ORDER IS THE
// FORMAT: the name argument is NULL on every call (r8=0), so the compiled .rsc body is
// positional and one transposed field silently shifts every value after it.
//
//   base Serialize (via base vtable slot 5, retail dispatch 0x1401527c8)
//   9 floats : +0x48 +0x4c +0x50 +0x58 +0x5c +0x60 +0x64 +0x68 +0x6c
//   2 Colors : +0x74 +0x78
//   2 Times  : +0x80 +0x88
//   2 floats : +0x54 +0x70          <- LAST, not with the other nine
//
// Returns true (retail sets al=1).
bool Tornado::Serialize(IO::Stream& stream) {
    // ENDGAME B.3.5 batch K1 — same shape as GetFileVersion above: a QUALIFIED
    // non-virtual call to the base body, because this function IS slot 5 of the Tornado
    // table. `?Serialize@Particle@Graphics@@UEAA_NAEAVStream@IO@@@Z` returns bool and
    // retail discards it, so the result is discarded here too.
    (void)Graphics::Particle::Serialize(stream);

    // Caller batch CB6: the fields are read by name (was `s + off` over `this`).
    {
    IO::Serializer ser(stream, 0);

    // +0x48 .. +0x6c in retail's stream order (+0x54 comes later).
    for (float* f : {&_tornadoWidth, &_tornadoHeight, &_funnelWidth, &_particleSpinSpeed,
                     &_emissionRate, &_particleLifeTime, &_particleLifeTimeTol,
                     &_particleStartSize, &_particleEndSize})
        ser.Serialize(*f, nullptr);

    _startColor.Serialize(ser);        // +0x74 start tint
    _endColor.Serialize(ser);          // +0x78 end tint
    _particleFadeIn.Serialize(ser);    // +0x80 fade in
    _particleFadeOut.Serialize(ser);   // +0x88 fade out

    ser.Serialize(_funnelSpin, nullptr);          // +0x54
    ser.Serialize(_particleScaleSize, nullptr);   // +0x70
    }   // ~Serializer, where retail destroys it

    // Outcome, once. The ctor defaults are 4.0 / 20.0 / 5.0 at +0x48/+0x4c/+0x50; if the
    // .rsc body was really read these become its values (Tornado.rsc: 20 / 75 / 26).
    // Unchanged defaults mean Serialize ran but consumed nothing.
    {
        if (s_serializeTraceBudget.fetch_add(1) < 4) {
            fprintf(stderr, "[Tornado/particle] Serialize read: +0x48=%.2f +0x4c=%.2f "
                    "+0x50=%.2f +0x54=%.2f +0x70=%.2f (ctor defaults were "
                    "4.00/20.00/5.00/45.00/30.00)\n",
                    _tornadoWidth, _tornadoHeight, _funnelWidth,
                    _funnelSpin, _particleScaleSize);
            fflush(stderr);
        }
    }
    return true;
}

// Retail slot 6 (0x140024ee0): slot 0 with flags 0 -- destruct without freeing --
// then the ctor again in place.
Object::External* Tornado::Reconstruct() {
    this->~Tornado();
    return ::new (static_cast<void*>(this)) Tornado();
}

// Retail slot 7 (0x140001640): slot 0 with flags 0, tail. A shared thunk in retail.
void Tornado::Destruct() { this->~Tornado(); }

// Retail slot 8 (0x1400d21b0): alloc 0xe0/align 8, run the ParticleInstance ctor,
// stamp vtable_TornadoInstance, zero +0xd8, return it (0 on a failed alloc).
Graphics::ParticleInstance* Tornado::CreateSystem() const {
    void* p = Object::detail::InternalMemAlloc(0xe0, 8);
    if (!p) return nullptr;
    return ::new (p) TornadoInstance();
}

// Retail slot 9 (0x140024ed0): self ? ctor(self) : null -- placement new.
Object::External* Tornado::Construct() {
    return ::new (static_cast<void*>(this)) Tornado();
}

// ---------------------------------------------------------------------------
// TornadoInstance
// ---------------------------------------------------------------------------
// Graphics::ParticleInstance() and the 0x1401601e0 stamp are the compiler's.
TornadoInstance::TornadoInstance() {
    _funnelSpinDegrees = 0.0f;   // retail: dword 0 at +0xd8 (the bits of 0.0f)
}

// ---------------------------------------------------------------------------
// TornadoInstance::Update -- retail 0x1400d2470, INSTANCE vtable slot 1.
//
// vtable_TornadoInstance (0x1401601e0) is exactly ten slots -- index 10 already holds
// string bytes ("Weather...") -- and slot 1 is the only one the subclass overrides.
// The base Graphics::ParticleInstance slot 1 is _purecall, so this body is not an
// optimisation: without it the first frame aborts with R6025.
//
// Layouts, all read off the retail body:
//   TornadoInstance  +0xc0 Tornado* resource  +0xc8 pool*  +0xd8 float funnel spin (deg)
//   pool             +0x00 Box(min xyz,max xyz)  +0x18 particle array (stride 0x58)
//                    +0x20 maxCount  +0x28 liveCount  +0x30 spawn accumulator (Time)
//                    +0x38 total accumulator (Time, written but never read here)
//   Tornado resource -- names below are the ToolKit ones, from
//   ToolKit/resource/ParticleSystems/Tornado/Tornado.rsc, which lists exactly the
//   15 fields Tornado::Serialize reads and in the same order, confirming the map:
//     +0x48 _tornadoWidth 20   +0x4c _tornadoHeight 75   +0x50 _funnelWidth 26
//     +0x54 _funnelSpin 30     +0x58 _particleSpinSpeed 400  +0x5c _emissionRate 20
//     +0x60 _particleLifeTime 30  +0x64 _particleLifeTimeTol 2
//     +0x68 _particleStartSize 2.5  +0x6c _particleEndSize 15
//     +0x70 _particleScaleSize 30   +0x74/+0x78 start/end Color
//     +0x80 _particleFadeIn 0.05    +0x88 _particleFadeOut 1.0
//   _particleStartSize/_particleEndSize are used as a CLAMP RANGE here, not as a
//   size ramp -- the ramp is _particleScaleSize times the radius profile.
//
// Each Color is unpacked BYTE +2 -> .x, +1 -> .y, +0 -> .z, with byte +3 carried
// separately as the alpha. Both call sites do it identically (spawn 0x1400d25f2,
// per-particle 0x1400d26b1), so the reversal is retail, not a transcription slip. It is
// almost certainly a BGRA->RGB unpack rather than a deliberate channel swap, but
// Tornado.rsc uses greys (0xFFC0C0C0 / 0x80909090) so the run cannot distinguish the
// two; only the alpha half is proven, by the measured 1.00 -> 0.50 fade.
// ---------------------------------------------------------------------------
// The pool is ParticleInstance::_boundsPtr, RtGraphics::ParticleBoundsRecord
// (graphics/ParticleInstance.h): the Box handed to SetBounds, the 0x58-byte
// ParticleRecord array (+0x18; its _size +0x20 is the pool's capacity), the live
// count +0x28 and the two tick accumulators +0x30 / +0x38.
namespace {
using TornadoParticle = RtGraphics::ParticleRecord;
using TornadoPool     = RtGraphics::ParticleBoundsRecord;

// Runtime leaves, called by name (B3R): Object::Base::IsDerivedFrom (the member;
// `resource` is tested non-null first), ??0Time@System@@QEAA@M@Z -> RtSystem_Time_Ctor
// (system/Value.h), ?Extend@Box@Volume@@ -> RtVolume_Box_Extend (volume/Volume.h).

}  // namespace

// Retail's sinf/cosf are the exe's statically linked MSVC CRT bodies at 0x140131c90 /
// 0x140131090, instruction-for-instruction the Runtime's Crt_sinf / Crt_cosf, which
// MsvcLibm::Sinf / Cosf port. A last-bit libm difference moves a dust particle.
void TornadoInstance::Update(const System::Time& time) {

    // Caller batch CB6: the instance's fields (ParticleInstance's +0xc0/+0xc8, the
    // class's +0xd8) and the resource's (a proved Tornado, below) are read by name.

    // Base ParticleInstance::Update is the frame gate: it returns false when this
    // instance has already been stepped this frame.
    // ?Update@ParticleInstance@Graphics@@QEAA_NXZ, the NON-VIRTUAL
    // `bool Update()` (ParticleInstance.h's name-hiding note: the arg-less
    // overload, not slot 1's `Update(const Time&)`).
    if (!Graphics::ParticleInstance::Update()) return;

    Graphics::Particle* resource = _resource;                          // +0xc0
    TornadoPool*        pool     = _boundsPtr;                         // +0xc8

    // Retail: `cmovne rbx,rsi` -- rsi is the resource only when IsDerivedFrom(Tornado)
    // holds, else 0, and every later read (`divss 0x5c(%rsi)`) would fault on it. In
    // retail that branch is unreachable because slot 2 GetTypeId reports Tornado. In
    // standalone it is reachable if our vtable copy ever loses the slot-2 override, so
    // this is a DELIBERATE deviation: bail loudly instead of faulting on a null we
    // manufactured ourselves.
    const uint32_t tornadoTid = Tornado::TypeId();
    if (!resource || !resource->IsDerivedFrom(tornadoTid)) {
        if (!s_updateBailSaid.exchange(true)) {
            fprintf(stderr, "[Tornado/particle] Update bailed: resource=%p is not a Tornado "
                    "(tid=%u). Retail cannot reach this -- Tornado::Type() must "
                    "be reporting the base Particle again.\n", static_cast<void*>(resource), tornadoTid);
            fflush(stderr);
        }
        return;
    }
    const Tornado* const res = static_cast<const Tornado*>(resource);   // proved just above

    const int64_t dt = time._ticks;
    pool->_spawnAccum += dt;
    pool->_totalTicks += dt;

    // ---- spawn ------------------------------------------------------------
    System::Time periodTime{0};
    RtSystem_Time_Ctor(&periodTime, 1.0f / res->_emissionRate);       // 1 / _emissionRate, in ticks
    const int64_t period = periodTime._ticks;

    // The Colors' bytes B, G, R, A (+0..+3 of the packed 0xAARRGGBB value).
    const uint32_t cStartV = res->_startColor._value;   // +0x74
    const uint32_t cEndV   = res->_endColor._value;     // +0x78
    const uint8_t cStart[4] = { uint8_t(cStartV), uint8_t(cStartV >> 8), uint8_t(cStartV >> 16), uint8_t(cStartV >> 24) };
    const uint8_t cEnd[4]   = { uint8_t(cEndV),   uint8_t(cEndV >> 8),   uint8_t(cEndV >> 16),   uint8_t(cEndV >> 24) };

    while (pool->_spawnAccum > period) {
        if (pool->_liveCount >= pool->_records._size) break;
        pool->_spawnAccum -= period;

        TornadoParticle* p = &pool->_records._data[pool->_liveCount++];
        p->_age = 0;

        System::Time life{0};
        RtSystem_Time_Ctor(&life, res->_particleLifeTime + Graphics::Particle::_rng.RandFloatTol() * res->_particleLifeTimeTol);
        p->_lifetime = life._ticks;

        p->_alpha = p->_alphaPrev = 0.0f;               // retail: one 8-byte zero at +0x40

        p->_color.x = p->_colorPrev.x = cStart[2] / 255.0f;
        p->_color.y = p->_colorPrev.y = cStart[1] / 255.0f;
        p->_color.z = p->_colorPrev.z = cStart[0] / 255.0f;

        Math::Vector3 z((Math::Scalar(0.0f)));   // Vector3(const Scalar&) splat
        p->_pos.x = p->_posPrev.x = z.x;
        p->_pos.y = p->_posPrev.y = z.y;
        p->_pos.z = p->_posPrev.z = z.z;

        p->_rot  = p->_rotPrev  = Graphics::Particle::_rng.RandFloatTol() * 180.0f;
        p->_size = p->_sizePrev = res->_particleStartSize;      // _particleStartSize
    }

    // ---- the growth box is rebuilt from scratch every frame, starting at the
    //      ORIGIN rather than at an inverted-infinite box. Retail, faithfully. ----
    {
        Math::Vector3 z1((Math::Scalar(0.0f)));
        Math::Vector3 z2((Math::Scalar(0.0f)));
        pool->_bounds.min.x = z2.x; pool->_bounds.min.y = z2.y; pool->_bounds.min.z = z2.z;
        pool->_bounds.max.x = z1.x; pool->_bounds.max.y = z1.y; pool->_bounds.max.z = z1.z;
    }

    const Math::Vector3 startColor(cStart[2] / 255.0f, cStart[1] / 255.0f, cStart[0] / 255.0f);
    const Math::Vector3 endColor(cEnd[2]   / 255.0f, cEnd[1]   / 255.0f, cEnd[0]   / 255.0f);
    const float startAlpha    = cStart[3] / 255.0f;
    const float endAlpha      = cEnd[3]   / 255.0f;

    const float dtSec = time.AsSeconds();

    // The whole funnel rotates: +0xd8 accumulates degrees at the resource's spin rate.
    const float spin = _funnelSpinDegrees + dtSec * res->_funnelSpin;  // +0xd8, _funnelSpin
    _funnelSpinDegrees = spin;
    const float spinRad = spin * 0.01745329f;
    const float sinG = MsvcLibm::Sinf(spinRad);
    const float cosG = MsvcLibm::Cosf(spinRad);

    // 90 deg/sec is a HARDCODED constant in the retail body (0x140172204), not
    // _particleSpinSpeed -- that one (+0x58) drives the helical angle below instead.
    const float spinDelta = dtSec * 90.0f;             // per-particle billboard spin

    const float width      = res->_tornadoWidth;   // _tornadoWidth
    const float height     = res->_tornadoHeight;   // _tornadoHeight
    const float funnelW    = res->_funnelWidth;   // _funnelWidth
    const float spinSpeed  = res->_particleSpinSpeed;   // _particleSpinSpeed
    const float sizeFloor  = res->_particleStartSize;   // _particleStartSize -- clamp LOW
    const float sizeCeil   = res->_particleEndSize;   // _particleEndSize   -- clamp HIGH
    const float sizeScale  = res->_particleScaleSize;   // _particleScaleSize
    const int64_t fadeOutTicks = res->_particleFadeOut._ticks;   // +0x88

    // ---- per particle, walked BACKWARDS (retail counts down from liveCount-1) ----
    for (int i = pool->_liveCount - 1; i >= 0; --i) {
        TornadoParticle* p = &pool->_records._data[i];
        p->_age += dt;

        // The record's two tick counts are System::Time values (ParticleRecord keeps them int64).
        const float t = System::Time{p->_age}.AsSeconds() / System::Time{p->_lifetime}.AsSeconds();

        // Lateral swirl: a cosine wobble that grows with age, biased so young
        // particles start inside the funnel (the -0.33 is retail's).
        const float wob   = MsvcLibm::Cosf((t - 0.2f) * 3.1415927f * 2.0f);
        const float swirl = (wob * 0.5f + 0.5f) * (t + 0.5f) - 0.33f;
        const float offX  = sinG * funnelW * swirl;
        const float offY  = cosG * funnelW * swirl;

        // Radius profile: a cubic bulge around t=0.4 plus a quadratic flare past t=0.3.
        float tail = t - 0.3f;
        if (!(tail > 0.0f)) tail = 0.0f;
        const float c   = t - 0.4f;
        const float rad = fabsf(c * c * c) * 3.7f + 0.05f + tail * tail * 4.0f;

        const float a    = spinSpeed * 0.31830987f * t;   // spinSpeed / pi * t
        const float sinA = MsvcLibm::Sinf(a);
        const float cosA = MsvcLibm::Cosf(a);

        p->_posPrev.x = p->_pos.x;
        p->_posPrev.y = p->_pos.y;
        p->_posPrev.z = p->_pos.z;

        const float px = width * (sinA * rad) + offX;
        const float py = width * (cosA * rad) + offY;
        // The billboard's own spin angle also lifts it: retail folds |angle|/40 into z.
        const float pz = fabsf(p->_rot) / 40.0f + t * height;

        p->_pos.x = px;
        p->_pos.y = py;
        p->_pos.z = pz;

        {
            const Math::Vector3 v(px, py, pz);
            ::Box retBox;
            const ::Box* nb = RtVolume_Box_Extend(&pool->_bounds, &retBox, &v);
            pool->_bounds.min.x = nb->min.x; pool->_bounds.min.y = nb->min.y; pool->_bounds.min.z = nb->min.z;
            pool->_bounds.max.x = nb->max.x; pool->_bounds.max.y = nb->max.y; pool->_bounds.max.z = nb->max.z;
        }

        float newAngle = spinDelta + p->_rot;
        if (newAngle > 180.0f) newAngle -= 360.0f;
        p->_rotPrev = p->_rot;
        p->_rot     = newAngle;

        // Retail recomputes the age ratio here; the age has not changed since, so it is
        // the same value of t, reused as the colour/alpha interpolant.
        const float u = System::Time{p->_age}.AsSeconds() / System::Time{p->_lifetime}.AsSeconds();

        float sz = rad * sizeScale;
        if (sizeFloor > sz)      sz = sizeFloor;
        else if (sz > sizeCeil)  sz = sizeCeil;
        p->_sizePrev = p->_size;
        p->_size     = sz;

        {
            Math::Vector3 cc = startColor.Lerp(endColor, u);
            p->_colorPrev.x = p->_color.x;
            p->_colorPrev.y = p->_color.y;
            p->_colorPrev.z = p->_color.z;
            p->_color.x = cc.x;
            p->_color.y = cc.y;
            p->_color.z = cc.z;
        }

        // Alpha envelope: fade in over resource +0x80, then fade out over +0x88
        // measured backwards from death. Retail computes BOTH and picks with a compare.
        const float   fadeInT  = System::Time{p->_age}.AsSeconds() / res->_particleFadeIn.AsSeconds();
        const int64_t outAge   = fadeOutTicks - p->_lifetime + p->_age;
        const float   fadeOutT = System::Time{outAge}.AsSeconds() / res->_particleFadeOut.AsSeconds();

        float env = (1.0f < fadeInT) ? (1.0f - fadeOutT) : fadeInT;
        if (env > 1.0f)      env = 1.0f;
        else if (0.0f > env) env = 0.0f;

        Math::Scalar a0(startAlpha);
        Math::Scalar la = a0.Lerp(Math::Scalar(endAlpha), Math::Scalar(u));
        p->_alpha = p->_alphaPrev = env * la.v;
    }

    // ?SetBounds@Geometry@Graphics@@QEAAXAEBVBox@Volume@@@Z. The
    // door took `(void* geometry, const void* box)` and retail hands it the pool
    // pointer: the accumulated `Volume::Box` is the pool's first 0x18 bytes.
    SetBounds(pool->_bounds);   // the pool's Box

    // Outcome trace, budgeted. "the swirl is running" is not observable from the
    // absence of a crash: a pool that never spawns, a period that came out infinite
    // (emissionRate == 0) and a live sim all look identical in a quiet log. Print the
    // numbers that separate them -- live/max, the spawn period in ticks, the box the
    // renderer will cull against, and one particle's actual position.
    {
        // Spread the samples: the pool cannot spawn before the accumulator crosses
        // one period, so three consecutive frames would only ever show live=0.
        const int ticks = s_updateTicks.fetch_add(1) + 1;
        if (s_updateTraceBudget.load() < 8 && (ticks <= 2 || ticks % 120 == 0)) {
            s_updateTraceBudget.fetch_add(1);
            const TornadoParticle* s = (pool->_liveCount > 0) ? &pool->_records._data[0] : nullptr;
            fprintf(stderr, "[Tornado/particle] tick %d: live=%d/%d period=%lld ticks "
                    "spin=%.1fdeg box=(%.2f,%.2f,%.2f)-(%.2f,%.2f,%.2f) p0=(%.2f,%.2f,%.2f) "
                    "size=%.2f alpha=%.2f\n",
                    ticks, pool->_liveCount, pool->_records._size, (long long)period, spin,
                    pool->_bounds.min.x, pool->_bounds.min.y, pool->_bounds.min.z,
                    pool->_bounds.max.x, pool->_bounds.max.y, pool->_bounds.max.z,
                    s ? s->_pos.x : 0.0f, s ? s->_pos.y : 0.0f, s ? s->_pos.z : 0.0f,
                    s ? s->_size : 0.0f, s ? s->_alpha : 0.0f);
            fflush(stderr);
        }
    }

    // ---- reap: erase every particle whose age reached its lifetime, shifting the
    //      tail down one slot. Retail re-tests the same index after a removal. ----
    for (int i = 0; i < pool->_liveCount; ++i) {
        TornadoParticle* arr = pool->_records._data;
        if (arr[i]._age < arr[i]._lifetime) continue;
        --pool->_liveCount;
        for (int j = i; j < pool->_liveCount; ++j) pool->_records._data[j] = pool->_records._data[j + 1];
        --i;
    }
}

// ---------------------------------------------------------------------------
// Reflection
// ---------------------------------------------------------------------------
::Object::Base* Tornado::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(Tornado), 8);
    Object::Base* built = p ? static_cast<Object::Base*>(::new (p) Tornado()) : nullptr;
    TornadoFactoryTrace("Factory", built);
    return built;
}
::Object::Base* Tornado::CreateInPlaceObject(void* memory) {
    TornadoFactoryTrace("CreateInPlace", memory);
    if (!memory) return nullptr;
    return static_cast<Object::Base*>(::new (memory) Tornado());
}

DEFINE_EXE_TYPE(Tornado, "Graphics::Particle")
