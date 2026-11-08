// ============================================================================
// src/engine/graphics/wave28_lane_partsys.cpp — WAVE 28 LANE PARTSYS
// ============================================================================
//
// | symbol                                                    | retail      |
// |-----------------------------------------------------------|-------------|
// | ?Update@ParticleSystemInstance@Graphics@@UEAAX…@Z          | 0x1800a5600 |
// | ?RemoveState@ParticleInstance@Graphics@@IEAAXXZ            | 0x1801040b0 |
// | ?Update@SunLight@Graphics@@UEAAX…@Z                        | 0x1801067a0 |
// | ?Draw@SunLight@Graphics@@UEBAX…MHHH@Z                      | 0x180106790 |
// | ?AddDrawItems@SunLight@Graphics@@UEBAX…@Z                  | 0x180106f50 |
//
// `?Update@ParticleSystemInstance@` is the whole particle simulation and the
// last unported slot of `??_7ParticleSystemInstance@Graphics@@6B@` (9/10 after
// Wave 27 lane GFXPART).
//
// ---------------------------------------------------------------------------
// PROVENANCE — objdump, not Ghidra
// ---------------------------------------------------------------------------
// Wave 27 found TWO GhidraMCP defects in `disassemble_function` alone (it can
// serve the PROFILE build while reporting retail as current, and it can serve a
// DIFFERENT overlapping function under the address asked for, with no tell).
// So every byte, address, extent and constant below was read with
//
//     objdump -d --start-address=… --stop-address=… "C:/GOG Games/Banished/Runtime-x64.dll"
//     pefile against the same file for the export table and .pdata
//
// Ghidra's decompiled C was used ONLY to recover the control-flow skeleton, and
// it was wrong about at least one thing that matters: it printed
// `Math::Transform::FromDirection(local_108, &local_118)` — TWO arguments — for
// a call whose own mangled name
// (`?FromDirection@Transform@Math@@SA?AV12@AEBVVector3@2@0@Z`) says
// `static Transform FromDirection(const Vector3&, const Vector3&)`, i.e. a
// hidden return slot plus TWO Vector3s. Retail loads all three
// (`LEA RCX,[RSP+0x60] / LEA RDX,[RSP+0x50] / LEA R8,[RSP+0x20]` at
// 0x1800a5fe5) and the third is a ZEROED Vector3 built two instructions
// earlier. Ghidra dropped it — exactly
// reference_ghidra_c_output_vs_disassembly.md.
//
// ---------------------------------------------------------------------------
// EXTENT — .pdata is a LOWER bound, and this body proves it
// ---------------------------------------------------------------------------
// `?Update@ParticleSystemInstance@` is FIVE chained .pdata fragments:
//
//   0x1800a5600..0x1800a569d  0x09d   flags 0
//   0x1800a569d..0x1800a57f6  0x159   UNW_FLAG_CHAININFO
//   0x1800a57f6..0x1800a62f1  0xafb   UNW_FLAG_CHAININFO
//   0x1800a62f1..0x1800a6867  0x576   UNW_FLAG_CHAININFO
//   0x1800a6867..0x1800a6a7a  0x213   UNW_FLAG_CHAININFO
//
// Reading only the first entry would have sized this body at 0x9d bytes — 0.7%
// of the truth. The real extent is 0x1800a5600..0x1800a6a79 = 0x147a bytes
// (the brief's 0x1479 counts the last byte inclusive), independently
// re-derived here rather than trusted.
//
// ---------------------------------------------------------------------------
// THE ELEVEN INLINED MT19937 DRAWS, AND EVERY CONSTANT LIFTED
// ---------------------------------------------------------------------------
// The brief says twelve. ELEVEN tempering sequences appear in this body, all in
// the spawn loop, counted by `and $0xff3a58ad` in the objdump listing (see
// §"RNG constant table" in docs/history/runtime_port/WAVE28_LANE_PARTSYS.md for every
// address). Every one of them is byte-identical to the others and to
// `?SetParticle@ParticleInstance@Graphics@@`'s, which Wave 27 already lifted:
//
//     y ^= y >> 11
//     y ^= (y & 0xff3a58ad) << 7        <- NOT the textbook 0x9d2c5680
//     y ^= (y & 0xffffdf8c) << 15       <- NOT the textbook 0xefc60000
//     y ^= y >> 18
//
// and the index advance is `INC EDX / CMP EDX,0x270 / CMOVE EDX,0` with a
// `?NextGroup@Random@System@@AEAAXXZ` call when the index is 0 BEFORE the read.
//
// The float constants are lifted as BIT PATTERNS read out of the DLL with
// pefile, never retyped:
//
//     0x180161658  0x00000000   0.0f     (the `qw * 0.0f` term in the quat mul)
//     0x18016165c  0x2f800000   2^-32    (the RNG word -> unit float scale)
//     0x18016167c  0x3c8efa35   pi/180
//     0x180161694  0x3f000000   0.5f
//     0x18016169c  0x3f800000   1.0f
//     0x1801616d8  0x40000000   2.0f
//     0x180161710  0x42b40000   90.0f
//     0x180161718  0x437f0000   255.0f
//     0x1801617c0  0x80000000   the float sign mask (XORPS negate)
//
// Every draw is turned into a signed-symmetric unit float the same way:
//
//     r = 1.0f - (float)(uint32)y * 2^-32 * 2.0f        // in (-1, +1]
//
// retail forms the two multiplies SEPARATELY (`MULSS xmm7` then `MULSS xmm8`)
// rather than folding them into a single 2^-31, and subtracts from a register
// holding 1.0f. Folding them is algebraically identical and numerically
// different (reference_moved_term_hides_where_terms_cancel.md), so the two
// multiplies are reproduced.
//
// ---------------------------------------------------------------------------
// A NAMING CORRECTION TO WAVE 27's RECORD MAP — and the licence, not just the
// sentence (kickoff warning 13)
// ---------------------------------------------------------------------------
// `wave27_lane_gfxpart.cpp` names the 0x58-byte particle record's fields from
// `ParticleInstance::Draw`, which only READS them. Two of its names are wrong,
// and the reason they are wrong is that a READER cannot tell a 3-float pair
// that is a position from one that is a colour:
//
//   kRec_PosB (+0x28) / kRec_PosBPrev (+0x34)  is the particle COLOUR (rgb).
//       THIS body writes it from `ParticleSystem[+0xdc..+0xe3]`, eight BYTES
//       divided by 255.0f and lerped between two endpoints. A byte/255 is not
//       a coordinate.
//   kRec_ScrollCur (+0x40) / kRec_ScrollPrv (+0x44)  is the particle ALPHA.
//       THIS body writes BOTH with the SAME value in one 8-byte store
//       (`MOV [r8+r9+0x98],RAX` at 0x1800a67f0 with a duplicated float), so the
//       "cur/prev" lerp Draw performs on it is a no-op by construction.
//   The field Wave 27 could not see at all is the rotation RATE, which lives in
//       the per-particle EXTRA array (+0x0c of a 0x14-byte stride), not in the
//       record. +0x48/+0x4c IS the rotation, and Draw's degrees reading stands.
//
// THE LICENCE, not the sentence: naming a field from a reader that can only see
// its SHAPE is the defect (reference_a_writer_beats_readers_agreeing_on_a_sign.md
// is the same shape one level down). Wave 27's kRec_* constants are NOT renamed
// here — that file's names are load-bearing for its own bodies and renaming
// them is a merge hazard for no behaviour change. The correction is recorded
// here and in the lane doc, on the surface the next particle lane will reach.
//
// ---------------------------------------------------------------------------
// OWNERSHIP (kickoff warning 6) — `RemoveState` frees CONDITIONALLY
// ---------------------------------------------------------------------------
// `ParticleInstance::RemoveState` frees `_boundsPtr` ONLY when
// `_resource->_active == 0`. That is not a null-safety check, it is an
// OWNERSHIP test, and it pairs exactly with
// `?SetParticle@ParticleSystemInstance@` (Wave 27): when `_active` is set the
// pool is BORROWED from `_resource->_boxes[_index]` and is owned by the
// resource; when it is clear the pool was `operator new`'d per instance and is
// owned here. A teardown that freed unconditionally would free a live shared
// object — the over-release direction, which is the dangerous one. Established
// by reading the WRITER (SetParticle), not by the element type.
// ============================================================================

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>

#include "rt_libm.h"
#include "graphics/Geometry.h"
#include "graphics/Particle.h"
#include "graphics/ParticleInstance.h"
// ENDGAME B.3.5 block E: the real `Graphics::SunLight` (and `Light`).
#include "graphics/SunLight.h"
#include "graphics/pass_split.h"   // issue #253: the stress harness's per-group CPU split
#include "graphics/stagger_hold.h"   // ext.staggered_passes: the shadow hold (NOT RETAIL)
#include "video/ImageBuffer.h"       // ext.staggered_passes: the map's Width() in the hold key
#include "math/Math.h"
#include "graphics/Camera.h"    // ENDGAME B.3.5 caller batch: Renderer, Camera
#include "graphics/Scene.h"     // ENDGAME B.3.5 caller batch: Graphics::Scene
#include "video/Device.h"       // ENDGAME B.3.5 caller batch: Video::RenderBuffer
#include "graphics/Material.h"  // RtGraphics_Material_OperatorDelete
#include "collection/Collection.h"   // rtcoll::RtColl_ArrayDtor_180062320
#include "object/TypeInfo.h"    // Object::Base::IsDerivedFrom
#include "system/Value.h"       // System::Random::NextGroup
#include "system/Timer.h"       // System::Timer::TicksToSeconds / SecondsToTicks


namespace {

// ---------------------------------------------------------------------------
// Attendance. A run that entered none of these says so rather than staying
// silent, and `calls=` is read separately from any comparison
// (reference_uncalled_port_is_not_verified.md).
// ---------------------------------------------------------------------------
uint64_t g_psiUpdate        = 0;   // entries
uint64_t g_psiUpdateEarly   = 0;   // took the "same frame already" SetBounds tail
uint64_t g_psiSpawned       = 0;   // particles spawned
uint64_t g_psiAged          = 0;   // particles stepped
uint64_t g_psiKilled        = 0;   // particles compacted out
uint64_t g_piRemoveState    = 0;
uint64_t g_piRemoveStateFree= 0;   // the arm that actually owns the pool
uint64_t g_sunUpdate        = 0;
uint64_t g_sunDrawMhhh      = 0;
uint64_t g_sunAddDrawItems  = 0;
uint64_t g_sunAddDrawEmit   = 0;

// ---------------------------------------------------------------------------
// The shipped float constants, as bit patterns read out of Runtime-x64.dll with
// pefile. `3.14159f/180.0f` and `1.0f/4294967296.0f` are DIFFERENT floats from
// what the DLL ships; these are the DLL's
// (reference_crt_coefficients_must_be_lifted_not_retyped.md).
// ---------------------------------------------------------------------------
inline float FromBits(uint32_t bits) {
    float f;
    std::memcpy(&f, &bits, sizeof f);
    return f;
}
const float kZero      = FromBits(0x00000000u);  // 0x180161658
const float kInv2p32   = FromBits(0x2f800000u);  // 0x18016165c
const float kDegToRad  = FromBits(0x3c8efa35u);  // 0x18016167c
const float kHalf      = FromBits(0x3f000000u);  // 0x180161694
const float kOne       = FromBits(0x3f800000u);  // 0x18016169c
const float kTwo       = FromBits(0x40000000u);  // 0x1801616d8
const float kNinety    = FromBits(0x42b40000u);  // 0x180161710
const float k255       = FromBits(0x437f0000u);  // 0x180161718

// Retail negates with `XORPS mem,xmm` against 0x1801617c0 = 0x80000000, i.e. a
// pure sign-bit flip. Reproduced as a bit operation rather than unary minus so
// the -0.0f case is the DLL's and not the compiler's.
inline float NegBits(float v) {
    uint32_t u;
    std::memcpy(&u, &v, sizeof u);
    u ^= 0x80000000u;
    float r;
    std::memcpy(&r, &u, sizeof r);
    return r;
}

// ---------------------------------------------------------------------------
// Graphics::Particle's static MT19937, and the draw retail inlines.
// Identical in every respect to wave27_lane_gfxpart.cpp's private copy; it is
// repeated rather than exported because that file keeps it in an anonymous
// namespace and this lane must not reshape another wave's translation unit
// (the same decision that file records for its own Serialize helpers).
// ---------------------------------------------------------------------------

// (`ParticleRng()`, the lazy name door onto ?_rng@Particle@Graphics@@, is gone:
// ENDGAME B.3.9 GFX made it the class static `Graphics::Particle::_rng`.)

// One draw, byte-for-byte retail's inlined sequence, on `System::Random`'s
// fields (`_index` +0x00, `_mt[624]` +0x04).
uint32_t RngNext(System::Random& rng) {
    int32_t index = rng._index;
    if (index == 0) {
        // ?NextGroup@Random@System@@AEAAXXZ (0x1800c7dd0): System::Random::NextGroup,
        // the member (B3R).
        rng.NextGroup();
        index = rng._index;
    }
    uint32_t y = rng._mt[static_cast<uint32_t>(index)];
    y ^= y >> 11;
    y ^= (y & 0xff3a58adu) << 7;    // SHIPPED mask, lifted at 0x1800a589c et al
    y ^= (y & 0xffffdf8cu) << 15;   // SHIPPED mask, lifted at 0x1800a58a8 et al
    y ^= y >> 18;
    int32_t next = index + 1;
    if (next == 0x270) next = 0;    // CMP EDX,0x270 / CMOVE EDX,<zero reg>
    rng._index = next;
    return y;
}

// `1.0f - (float)y * 2^-32 * 2.0f`, in retail's exact association: CVTSI2SS
// from a 64-bit register holding the zero-extended word, then two separate
// MULSS, then SUBSS from a register holding 1.0f.
inline float RngSigned(System::Random& rng) {
    const float t = static_cast<float>(static_cast<uint64_t>(RngNext(rng)));
    return kOne - ((t * kInv2p32) * kTwo);
}

// ---------------------------------------------------------------------------
// Timer conversions. Retail INLINES both, but the inlined sequences are
// instruction-identical to the two exported bodies, which Wave 9 lane B3 has
// ported -- so calling them is bit-exact and Timer::_secondsPerTick (a
// QPC-derived runtime global) never has to be duplicated here. This is the
// decision src/engine/engine/loops.cpp already took, with its reasoning.
//   ?TicksToSeconds@Timer@System@@SAM_J@Z  CVTSI2SD / MULSD spt / CVTPD2PS
//   ?SecondsToTicks@Timer@System@@SA_JM@Z  CVTSS2SD / DIVSD spt / CVTTSD2SI
// ---------------------------------------------------------------------------
inline float TicksToSeconds(int64_t t) {
    return System::Timer::TicksToSeconds(t);
}
inline int64_t SecondsToTicks(float s) {
    return System::Timer::SecondsToTicks(s);
}

// ---------------------------------------------------------------------------
// `Graphics::ParticleInstance::_frameIndex`
// (?_frameIndex@ParticleInstance@Graphics@@1HA, retail 0x1801fe77c) — the
// protected static int every ParticleInstance stamps into its pool record's
// frame tag so a system shared by several instances updates ONCE per frame.
// Also tagged RT_UNPROMOTABLE on the DLL-PRESENT route.
// ---------------------------------------------------------------------------
// ENDGAME B.3.9 GFX (D7 row A37): the class static, not a lazy ResolveExport
// cache.
int32_t* FrameIndexPtr() {
    return &Graphics::ParticleInstance::_frameIndex;
}

// `?_typeInfo@ParticleSystem@Graphics@@0VBaseTypeInfo@Object@@A` (0x1801f9a58),
// read for its FIRST DWORD only -- the typeId. That is the whole of what the
// DLL's consumers of a BaseTypeInfo read
// (reference_basetypeinfo_name_is_content_only.md).
// ENDGAME B.3.9 TI-REST: the class's own `DEFINE_TYPE` record, read fresh
// (the function-local cache over the minted record is gone, B9-10).
uint32_t ParticleSystemTypeId() { return Graphics::ParticleSystem::TypeId(); }

// B.4: the offset constants this body read through (kParticle_* / kPool_* /
// kRec_* / kEmit_* / kExt_* / kPS_*) are gone with the reads: the pool record,
// the live-particle record, the emitter and its extra record are named field by
// field (ParticleInstance.h / Particle.h), and `Graphics::ParticleSystem`'s
// tuning fields, which block H2 asserted against this lane's measurements, are
// read by name. Particle.h keeps those offsets asserted.

// ---------------------------------------------------------------------------
// `q * (v,0) * conj(q)`, in retail's EXACT association order.
//
// This sequence appears TWICE in the body, at 0x1800a5f33..0x1800a60db and
// 0x1800a60e0..0x1800a62b9, with identical term order and identical register
// shuffling; both were transcribed independently from objdump and agree, which
// is why one helper serves both.
//
// Three things a rewrite would silently change:
//   * `q.x * zero` and `q.y * zero` and `q.z * zero` are REAL multiplies retail
//     performs against a XORPS-zeroed register (the w component of the vector
//     quaternion). They vanish for finite inputs and do not for NaN.
//   * the `w` term is `q.w * [0x180161658]` — a MULSS against a 0.0f in .rdata,
//     not a fold to zero.
//   * the conjugate is formed with XORPS against 0x80000000, a sign-bit flip.
// ---------------------------------------------------------------------------
struct Vec3f { float x, y, z; };

Vec3f QuatRotate(float qx, float qy, float qz, float qw, const Vec3f& v) {
    const float zero = 0.0f;                   // XORPS xmm4,xmm4
    const float nx = NegBits(qx);              // XORPS with 0x1801617c0
    const float ny = NegBits(qy);
    const float nz = NegBits(qz);

    // t = q * (v, 0)
    const float tx = ((v.x * qw) + (qx * zero) + (v.y * qz)) - (v.z * qy);
    const float ty = ((qy * zero) - (v.x * qz)) + (v.y * qw) + (v.z * qx);
    const float tz = ((v.x * qy) + (qz * zero) - (v.y * qx)) + (v.z * qw);
    const float tw = (((qw * kZero) - (v.x * qx)) - (v.y * qy)) - (v.z * qz);

    // r = t * conj(q), only the vector part is kept
    Vec3f r;
    r.x = ((qw * tx) + (tw * nx) + (tz * ny)) - (ty * nz);
    r.y = ((qw * ty) - (tz * nx)) + (tw * ny) + (tx * nz);
    r.z = (((qw * tz) + (ty * nx)) - (tx * ny)) + (tw * nz);
    return r;
}

}  // namespace

// ============================================================================
// void Graphics::ParticleSystemInstance::Update(const System::Time&)
// ?Update@ParticleSystemInstance@Graphics@@UEAAXAEBVTime@System@@@Z
// retail 0x1800a5600 — 0x147a bytes, slot 1 of ??_7ParticleSystemInstance@…6B@
//
// Retail's shape, in order:
//   A  refresh _boundsPtr from the resource's pool array when the resource is
//      active, and return early (through Geometry::SetBounds) when this pool
//      was already stepped THIS frame;
//   B  stamp the frame tag, derive dt and dt², resolve the ParticleSystem;
//   C  advance the two pool accumulators and the spawn-rate accumulator, and
//      zero the rate when the system has expired;
//   D  the SPAWN loop — eleven inlined MT19937 draws per particle;
//   E  the AGE loop, backwards over the live particles: gravity, damping,
//      position integration, the bounding box, spin, size, colour and alpha;
//   F  the inlined Geometry::SetBounds compare-and-dirty;
//   G  the compacting swap-free removal pass.
//
// FAITHFUL UNGUARDED DEREFS, left alone on purpose (CLAUDE.md: don't "fix" a
// faithful crash with a null check — find the upstream null):
//   * `this->_resource` is dereferenced at the very first instruction pair
//     (MOV 0xc0(%rcx),%rcx / CMPB $0,0x20(%rcx)) with no test.
//   * `sys` is null whenever `IsDerivedFrom` says the resource is not a
//     ParticleSystem, and retail reads `sys + 0x4c` two instructions later
//     regardless (CMOVNE leaves EDI zero). Every retail caller hands a
//     ParticleSystem; if our standalone ever reaches that arm, the fault IS the
//     diagnosis.
//   * `this->_emitter` is dereferenced with no test.
// ============================================================================
// ENDGAME B.3.5 block E: BODY MOVED onto
// `Graphics::ParticleSystemInstance::Update` (slot 1 — the class that finally
// FILLS Geometry's pure slot 1). ENDGAME B.3.9 GFX: the name door is retired
// with the minted table, its only reader.

namespace Graphics {

// [8] ?CreateSystem@ParticleSystem@Graphics@@UEBAPEAVParticleInstance@2@XZ
//     retail 0x1800a5140: MemAlloc(0xe0, 8), the null branch, then the
//     ParticleSystemInstance ctor 0x1800a55a0. `this` is never read. ENDGAME
//     B.3.9 GFX: moved here from vtbatch7_animgroup_particlesystem.cpp, whose
//     `<windows.h>` cannot see ParticleInstance.h, so the ctor is the real
//     class's instead of the by-address ??0 door.
ParticleInstance* ParticleSystem::CreateSystem() const {
    void* inst = Object::detail::InternalMemAlloc(0xe0, 8);
    if (inst == nullptr) return nullptr;                   // 1800a519c -> XOR EAX,EAX
    return ::new (inst) ParticleSystemInstance();          // 1800a51a1
}

void ParticleSystemInstance::Update(const System::Time& timeRef) {
    ++g_psiUpdate;

    const int64_t deltaTicks = timeRef._ticks;

    // --- A -----------------------------------------------------------------
    const bool resActive = (_resource->_active != 0);          // +0xc0 -> +0x20, unguarded

    if (resActive) {
        _boundsPtr = _resource->_records._data[_index];         // MOVSXD 0xd0(%rsi)
    }

    int32_t* frameIndex = FrameIndexPtr();
    const int32_t frame = frameIndex ? *frameIndex : 0;

    RtGraphics::ParticleBoundsRecord* const pool = _boundsPtr;

    if (resActive && pool->frameTag == frame) {
        // Retail TAIL-JUMPS to Geometry::SetBounds after unwinding its frame:
        // a QUALIFIED non-virtual member call on this Geometry.
        ++g_psiUpdateEarly;
        this->::Graphics::Geometry::SetBounds(pool->_bounds);
        return;
    }

    // --- B -----------------------------------------------------------------
    pool->frameTag = frame;

    const Particle* const resource = _resource;

    const float dt  = TicksToSeconds(deltaTicks);   // CVTSI2SDQ/MULSD/CVTPD2PS
    const float dt2 = dt * dt;

    // CMOVNE: `ps` is the resource when the test passes and NULL when it does
    // not -- and retail reads through it either way. ?IsDerivedFrom@Base@Object@@
    // (0x1800c0390) is the member (B3R); the test is what licenses the downcast.
    //
    // *** THE NULL CASE IS UNCHANGED AND STILL FAITHFUL. *** Retail's CMOVNE leaves
    // the register null when the test fails and then reads `sys + 0x4c` two
    // instructions later anyway; `ps->_emissionAcceleration` on a null `ps` is the SAME
    // unguarded deref, not a new one, and CLAUDE.md's rule is that a guard here
    // would skip retail's behaviour rather than restore it. Every retail caller
    // hands a ParticleSystem.
    const ::Graphics::ParticleSystem* const ps =
        resource->IsDerivedFrom(ParticleSystemTypeId())
            ? static_cast<const ::Graphics::ParticleSystem*>(resource) : nullptr;

    // --- C -----------------------------------------------------------------
    pool->_spawnAccum += deltaTicks;
    pool->_totalTicks += deltaTicks;

    RtGraphics::ParticleEmitter* const emitter = _emitter;     // +0xd8, unguarded
    {
        // rate += dt² * _emissionAcceleration * _emissionDrag, in retail's order:
        // the dt² register is multiplied by both fields, THEN added.
        float r = dt2;
        r *= ps->_emissionAcceleration;
        r *= ps->_emissionDrag;
        r += emitter->_rate;
        emitter->_rate = r;
    }
    {
        // COMISS 0.0, rate / JA -> zero;  else the duration test.
        const float rate = emitter->_rate;
        bool zeroIt = (0.0f > rate);
        if (!zeroIt) {
            const int64_t duration = ps->_durationTicks;
            if (duration > 0 && pool->_totalTicks > duration) zeroIt = true;
        }
        if (zeroIt) emitter->_rate = 0.0f;
    }

    // interval = SecondsToTicks(1.0f / rate). Retail loads 1.0f from
    // [0x18016169c] and DIVSS by the rate — an infinity when the rate is 0,
    // whose CVTTSD2SI is 0x8000000000000000. Faithful.
    const int64_t interval = SecondsToTicks(kOne / emitter->_rate);

    System::Random& rng = Graphics::Particle::_rng;   // ?_rng@Particle@Graphics@@ (0x1801fe7f0)

    // The packed BGRA colour bytes, read one byte at a time by retail (MOVZX
    // byte): byte n of the little-endian dword is `>> (8 * n)`.
    const uint32_t color0 = ps->_color0;   // +0xdc
    const uint32_t color1 = ps->_color1;   // +0xe0

    // --- D : the spawn loop ------------------------------------------------
    // `while (pool.spawnAccum > interval && pool.live < pool.capacity)`.
    // Retail tests the accumulator FIRST (JLE past the loop), then the capacity
    // at the head of every iteration (JGE out).
    while (pool->_spawnAccum > interval) {
        if (pool->_liveCount >= pool->_records._size) break;

        pool->_spawnAccum -= interval;

        const int32_t slot = pool->_liveCount;
        RtGraphics::ParticleRecord& rec = pool->_records._data[static_cast<int64_t>(slot)];
        RtGraphics::ParticleExtra&  ext = emitter->_extra._data[static_cast<int64_t>(slot)];
        pool->_liveCount = slot + 1;
        rec._age = 0;
        ++g_psiSpawned;

        // DRAW 1 @ 0x1800a5865 — lifetime
        {
            const float life = (RngSigned(rng) * ps->_lifeRange) + ps->_lifeBase;
            rec._lifetime = SecondsToTicks(life);
        }
        // DRAWS 2,3,4 @ 0x1800a58fe / 0x1800a5981 / 0x1800a59fe — the spawn
        // offset. Each is `r * spreadScale * 0.5f`, and the axis each one feeds
        // is NOT in draw order: draw 2 -> Z, draw 3 -> Y, draw 4 -> X.
        const float oZ = (RngSigned(rng) * ps->_spreadScale) * kHalf;
        const float oY = (RngSigned(rng) * ps->_spreadScale) * kHalf;
        const float oX = (RngSigned(rng) * ps->_spreadScale) * kHalf;
        const float px = ps->_spreadX * oX;
        const float py = ps->_spreadY * oY;
        const float pz = ps->_spreadZ * oZ;

        const float c0 = static_cast<float>(static_cast<uint8_t>(color0 >> 16)) / k255;  // +0xde
        const float c1 = static_cast<float>(static_cast<uint8_t>(color0 >> 8))  / k255;  // +0xdd
        const float c2 = static_cast<float>(static_cast<uint8_t>(color0))       / k255;  // +0xdc

        rec._alpha     = 0.0f;               // MOV %r15,0x40(%r14): +0x40 AND +0x44
        rec._alphaPrev = 0.0f;
        rec._color.x = c0;      rec._color.y = c1;      rec._color.z = c2;
        rec._colorPrev.x = c0;  rec._colorPrev.y = c1;  rec._colorPrev.z = c2;
        rec._pos.x = px;        rec._pos.y = py;        rec._pos.z = pz;
        rec._posPrev.x = px;    rec._posPrev.y = py;    rec._posPrev.z = pz;

        // DRAW 5 @ 0x1800a5b1a — rotation (degrees), cur == prev
        {
            const float rot = (RngSigned(rng) * ps->_rotRange) + ps->_rotBase;
            rec._rot     = rot;
            rec._rotPrev = rot;
        }
        // DRAW 6 @ 0x1800a5bac — size, cur == prev
        {
            const float size = (RngSigned(rng) * ps->_sizeRange) + ps->_sizeBase;
            rec._size     = size;
            rec._sizePrev = size;
        }
        // DRAW 7 @ 0x1800a5c3e — spin rate, into the EXTRA record
        ext._rotRate = (RngSigned(rng) * ps->_spinRange) + ps->_spinBase;
        // DRAW 8 @ 0x1800a5ccb — a COIN FLIP on the spin direction. Retail
        // TESTs bit 0 of the tempered word and flips the sign bit when it is
        // CLEAR; it does not use the word for anything else.
        if ((RngNext(rng) & 1u) == 0u)
            ext._rotRate = NegBits(ext._rotRate);
        ext._sizeRate = 0.0f;

        // DRAW 9 @ 0x1800a5d45 — speed
        const float speed = (RngSigned(rng) * ps->_speedRange) + ps->_speedBase;
        // DRAW 10 @ 0x1800a5dcf — elevation, as 90 degrees MINUS the draw
        const float angleADeg = kNinety - (RngSigned(rng) * ps->_angleARange);
        // DRAW 11 @ 0x1800a5e55 — azimuth, already in radians
        const float angleB = (RngSigned(rng) * ps->_angleBRange) * kDegToRad;
        const float angleA = angleADeg * kDegToRad;

        // Retail calls the DLL's own sinf/cosf (0x180127d90 / 0x180126d10) in
        // this exact order: sin(B), sin(A), cos(B), cos(A). RtLibm is the
        // project's seam for exactly these two (src/engine/rt_libm.h) and
        // RT_LIBM=dll routes it back to the DLL's CRT for an A/B.
        const float sinB = RtLibm::Sin(angleB);
        const float sinA = RtLibm::Sin(angleA);
        const float cosB = RtLibm::Cos(angleB);
        const float cosA = RtLibm::Cos(angleA);

        ext._velX = (NegBits(cosA) * sinB) * speed;
        ext._velY = (cosA * cosB) * speed;
        ext._velZ = sinA * speed;

        // Rotate the system's emit direction by the instance's own orientation
        // (Geometry::_transform.rotation, +0x84..+0x90) and hand THAT to
        // FromDirection as the look direction — with a ZEROED Vector3 as the
        // second argument, the argument Ghidra dropped.
        const Math::Quaternion& q = _transform._current.rotation;
        const Vec3f emitDir = { ps->_emitDirX, ps->_emitDirY, ps->_emitDirZ };
        const Vec3f dir = QuatRotate(q.x, q.y, q.z, q.w, emitDir);

        Vector3 dirArg;  dirArg.x = dir.x; dirArg.y = dir.y; dirArg.z = dir.z;
        Vector3 zeroArg; zeroArg.x = 0.0f; zeroArg.y = 0.0f; zeroArg.z = 0.0f;
        // ENDGAME B.3.1 step 2: `Math::Transform::FromDirection`, called as the
        // static member it is. Retail CALLs 0x180021d70 with a rel32, so this
        // is a direct call and not a registry hop — the two halves of one frame
        // must come from one build.
        const Transform basis = Transform::FromDirection(dirArg, zeroArg);

        // Rotate the freshly drawn velocity into that basis, in place.
        const Vec3f v = { ext._velX, ext._velY, ext._velZ };
        const Vec3f rv = QuatRotate(basis.rotation.x, basis.rotation.y,
                                    basis.rotation.z, basis.rotation.w, v);
        ext._velX = rv.x;
        ext._velY = rv.y;
        ext._velZ = rv.z;
    }

    // --- E : the age loop --------------------------------------------------
    // Retail hoists nine things out of the loop; they are hoisted here too, in
    // retail's own association, because folding them back into the loop body
    // would move the rounding.
    const float accelScale = ps->_accelScale;
    float aX = ps->_gravScale; aX *= ps->_gravX; aX *= accelScale; aX *= dt2;
    float aY = ps->_gravScale; aY *= ps->_gravY; aY *= accelScale; aY *= dt2;
    float aZ = ps->_gravScale; aZ *= ps->_gravZ; aZ *= accelScale; aZ *= dt2;

    const float base0 = static_cast<float>(static_cast<uint8_t>(color0 >> 16)) / k255;  // +0xde
    const float base1 = static_cast<float>(static_cast<uint8_t>(color0 >> 8))  / k255;  // +0xdd
    const float base2 = static_cast<float>(static_cast<uint8_t>(color0))       / k255;  // +0xdc
    const float base3 = static_cast<float>(static_cast<uint8_t>(color0 >> 24)) / k255;  // +0xdf (alpha)
    const float end0  = static_cast<float>(static_cast<uint8_t>(color1 >> 16)) / k255;  // +0xe2
    const float end1  = static_cast<float>(static_cast<uint8_t>(color1 >> 8))  / k255;  // +0xe1
    const float end2  = static_cast<float>(static_cast<uint8_t>(color1))       / k255;  // +0xe0
    const float end3  = static_cast<float>(static_cast<uint8_t>(color1 >> 24)) / k255;  // +0xe3

    // Six floats of the pool's Box, zeroed BEFORE the loop rebuilds it. Note
    // this is a ZERO fill, not the ctor's ±1e8 seeding — so an empty system
    // publishes an origin-centred degenerate box, which is retail's behaviour.
    ::Box& box = pool->_bounds;
    box.min.x = 0.0f; box.min.y = 0.0f; box.min.z = 0.0f;
    box.max.x = 0.0f; box.max.y = 0.0f; box.max.z = 0.0f;

    const float colorSpan = ps->_colorT1 - ps->_colorT0;
    const float invColorSpan = kOne / colorSpan;

    const float delta0 = end0 - base0;
    const float delta1 = end1 - base1;
    const float delta2 = end2 - base2;
    const float delta3 = end3 - base3;

    for (int64_t i = static_cast<int64_t>(pool->_liveCount) - 1; i >= 0; --i) {
        RtGraphics::ParticleRecord& rec = pool->_records._data[i];
        RtGraphics::ParticleExtra&  ext = emitter->_extra._data[i];
        ++g_psiAged;

        rec._age += deltaTicks;

        const float damping = ps->_damping;
        const float vx = (aX + ext._velX) * damping;
        const float vy = (aY + ext._velY) * damping;
        const float vz = (aZ + ext._velZ) * damping;
        ext._velX = vx;
        ext._velY = vy;
        ext._velZ = vz;

        const float nx = rec._pos.x + dt * vx;
        const float ny = rec._pos.y + dt * vy;
        const float nz = rec._pos.z + dt * vz;
        // prev <- the OLD current, copied before the new writes
        rec._posPrev = rec._pos;
        rec._pos.x = nx;
        rec._pos.y = ny;
        rec._pos.z = nz;

        // The bounding box. Retail's COMISS/JA idiom keeps the EXISTING value
        // when the comparison is unordered, which `a < b ? a : b` does not
        // reproduce — so each is written as retail's `if (existing <= new)
        // existing = new` / `if (new <= existing) existing = new`.
        {
            float maxZ = box.max.z; if (maxZ <= nz) maxZ = nz;
            float maxY = box.max.y; if (maxY <= ny) maxY = ny;
            float maxX = box.max.x; if (maxX <= nx) maxX = nx;
            float minZ = box.min.z; if (nz <= minZ) minZ = nz;
            float minY = box.min.y; if (ny <= minY) minY = ny;
            float minX = box.min.x; if (nx <= minX) minX = nx;
            box.min.x = minX;
            box.min.y = minY;
            box.min.z = minZ;
            box.max.x = maxX;
            box.max.y = maxY;
            box.max.z = maxZ;
        }

        // Spin: rate' = (dt² * spinAccel + rate) * spinDamp; rot += rate' * dt
        {
            float rate = dt2 * ps->_spinAccel;
            rate += ext._rotRate;
            rate *= ps->_spinDamp;
            ext._rotRate = rate;
            const float prev = rec._rot;
            rec._rotPrev = prev;
            rec._rot = rate * dt + prev;
        }
        // Size: identical shape on its own accel/damp pair
        {
            float rate = dt2 * ps->_sizeAccel;
            rate += ext._sizeRate;
            rate *= ps->_sizeDamp;
            ext._sizeRate = rate;
            const float prev = rec._size;
            rec._sizePrev = prev;
            rec._size = rate * dt + prev;
        }

        // The colour parameter: (age/lifetime - t0) / (t1 - t0), clamped to
        // [0,1] with retail's COMISS ordering (0 first, then 1).
        float t = TicksToSeconds(rec._age) / TicksToSeconds(rec._lifetime);
        t -= ps->_colorT0;
        t *= invColorSpan;
        if (0.0f > t) t = 0.0f;
        else if (t > kOne) t = kOne;

        rec._colorPrev = rec._color;
        rec._color.x = delta0 * t + base0;
        rec._color.y = delta1 * t + base1;
        rec._color.z = delta2 * t + base2;

        // The fade. TWO independent ramps, and retail picks ONE of them with a
        // CMOVB on `1.0f < fadeIn` — the fade-in until it saturates, the
        // fade-out afterwards — then clamps the winner to [0,1] with the 1.0f
        // comparison FIRST this time.
        {
            const int64_t age  = rec._age;
            const int64_t life = rec._lifetime;
            const int64_t out  = ps->_fadeOutTicks;

            const float fadeIn  = TicksToSeconds(age) / TicksToSeconds(ps->_fadeInTicks);
            const float fadeOut = kOne - (TicksToSeconds((out - life) + age) / TicksToSeconds(out));

            float fade = (kOne < fadeIn) ? fadeOut : fadeIn;
            if (fade > kOne) fade = kOne;
            else if (0.0f > fade) fade = 0.0f;

            const float alpha = ((delta3 * t) + base3) * fade;
            // ONE 8-byte store covering +0x40 AND +0x44 with the same float.
            rec._alpha     = alpha;
            rec._alphaPrev = alpha;
        }
    }

    // --- F : Geometry::SetBounds, inlined --------------------------------
    // Retail compares the six floats with UCOMISS and only writes (and sets
    // dirty bit 1) when one differs. UCOMISS's unordered case falls through to
    // the write, which `==` failing on a NaN also does.
    {
        const ::Box& lb = _localBounds;
        const bool same = lb.min.x == box.min.x && lb.min.y == box.min.y &&
                          lb.min.z == box.min.z && lb.max.x == box.max.x &&
                          lb.max.y == box.max.y && lb.max.z == box.max.z;
        if (!same) {
            _localBounds = box;                                    // six dwords
            _dirtyFlags |= RtGraphics::kDirtyBounds;               // Geometry::_dirtyFlags |= 2
        }
    }

    // --- G : the compacting removal pass ----------------------------------
    // `if (rec.age >= rec.lifetime) { live--; shift every later record and its
    // extra record down one; DON'T advance i }`.
    for (int32_t i = 0; i < pool->_liveCount; ) {
        const RtGraphics::ParticleRecord& rec = pool->_records._data[static_cast<int64_t>(i)];
        if (rec._age < rec._lifetime) { ++i; continue; }

        pool->_liveCount = pool->_liveCount - 1;
        ++g_psiKilled;

        // Retail re-reads pool->_records and emitter->_extra on EVERY iteration
        // of this shift; reproduced, because a cached base would be a different
        // program if anything reallocated underneath (nothing does, today).
        for (int32_t j = i; j < pool->_liveCount; ++j) {
            // Retail copies the 0x58 bytes field by field (two qwords, twelve
            // dwords, three qwords). Source and destination are 0x58 apart and
            // never overlap, so one record copy is the same program.
            RtGraphics::ParticleRecord* const base = pool->_records._data;
            base[static_cast<int64_t>(j)] = base[static_cast<int64_t>(j + 1)];
            RtGraphics::ParticleExtra* const ebase = emitter->_extra._data;
            ebase[static_cast<int64_t>(j)] = ebase[static_cast<int64_t>(j + 1)];
        }
    }
}

}  // namespace Graphics

// ============================================================================
// void Graphics::ParticleInstance::RemoveState()
// ?RemoveState@ParticleInstance@Graphics@@IEAAXXZ   retail 0x1801040b0
//
// Retail, transcribed from objdump 0x1801040b0..0x18010411d:
//   res = this->_resource;
//   if (res && res->_active == 0) {
//       pool = this->_boundsPtr;
//       if (pool) { 0x180062320(&pool->_records);   // Array<T>::~Array
//                   0x180061ae0(pool);              // per-TU operator delete
//                   this->_boundsPtr = 0; return; }
//   }
//   this->_boundsPtr = 0;
//
// See the banner for why the `_active == 0` test is an OWNERSHIP test and not a
// safety check. The `movq $-2,0x20(%rsp)` at the top is the MSVC unwind
// funclet's state slot and has no effect on the object.
// ============================================================================

namespace Graphics {

void ParticleInstance::RemoveState() {
    ++g_piRemoveState;

    const Particle* const res = _resource;

    if (res && res->_active == 0) {
        RtGraphics::ParticleBoundsRecord* const pool = _boundsPtr;
        if (pool) {
            ++g_piRemoveStateFree;
            // Both are UNNAMED in the shipped export directory (checked with
            // pefile by VA), both ported, both called by body name (B3R):
            //   0x180062320  Collection::Array<T>::~Array —
            //                rtcoll::RtColl_ArrayDtor_180062320 (array.cpp)
            //   0x180061ae0  the per-TU `operator delete` (locked MemFree) —
            //                RtGraphics_Material_OperatorDelete (system/closure_batch2b.cpp)
            rtcoll::RtColl_ArrayDtor_180062320(&pool->_records);
            RtGraphics_Material_OperatorDelete(pool);
            _boundsPtr = nullptr;
            return;
        }
    }
    _boundsPtr = nullptr;
}

}  // namespace Graphics

// ============================================================================
// The two SunLight slots that are a bare RET
//
//   ?Update@SunLight@Graphics@@UEAAXAEBVTime@System@@@Z        0x1801067a0
//   ?Draw@SunLight@Graphics@@UEBAX…MHHH@Z                      0x180106790
//
// MEASURED: both are `C2 00 00` (RET $0x0) followed by INT3 padding, and
// NEITHER has a .pdata entry — they are leaf functions with no prologue. They
// are not stubs this lane chose to leave empty; they are what the DLL ships.
// The class overrides both purely to REPLACE the Light/Geometry behaviour with
// nothing (SunLight's real work is in DrawShadowMap and the four-int Draw).
// ============================================================================

namespace Graphics {

// Both are `RET` in the shipped build. They are not stubs this port chose to
// leave empty; the class overrides them purely to REPLACE the Light/Geometry
// behaviour with nothing (SunLight's real work is DrawShadowMap and DrawLight).
void SunLight::Update(const System::Time& time) {
    (void)time;
    ++g_sunUpdate;
}

void SunLight::Draw(Video::Context& context, const Camera& camera,
                    ExternalPtrHandle material, float opacity,
                    int a, int b, int c) const {
    (void)context; (void)camera; (void)material; (void)opacity;
    (void)a; (void)b; (void)c;
    ++g_sunDrawMhhh;
}

}  // namespace Graphics

// ============================================================================
// void Graphics::SunLight::AddDrawItems(const Scene&, Renderer&) const
// ?AddDrawItems@SunLight@Graphics@@UEBAXAEBVScene@2@AEAVRenderer@2@@Z
// retail 0x180106f50..0x180106fff  (0xaf bytes, single .pdata entry)
//
// Retail, transcribed from objdump:
//   if (this->+0xf8 == 0) return;
//   Renderer::AddLight(renderer, this);                     // 0x1801051d0
//   Camera cam = SunLight::GetSunCamera(this, renderer);    // 0x180107010
//   int g = Renderer::BeginDrawGroup(renderer, this->+0x100,
//                                    *(Camera*)(renderer->+0x10 + 8), cam, 1, 1);
//   if (renderer->+0x3c != 0) {
//       Scene::AddGlobalDrawItems(scene, renderer);         // 0x180102330
//       if (renderer->+0x48 != 0)
//           Scene::AddCulledDrawItems(scene, renderer);     // 0x1801017a0
//   }
//   Renderer::EndDrawGroup(renderer, g);                    // 0x180104f50
//
// ALL SIX CALLEES ARE NAMED EXPORTS — checked with pefile by VA before the body
// was written, not assumed (a name the DLL lacks makes ResolveExport return
// NULL in silence and the ResolveAddr fallback hides it:
// reference_resolveexport_phantom_names.md). So this body opens SIX NAME doors
// and ZERO address doors.
//
// The `renderer->+0x3c` / `+0x48` gate is INDEPENDENTLY CORROBORATED: this
// lane read it out of the bytes, and `src/game/Game/gameupdateloop.cpp:2171`
// already records `renderer+0x3c != 0` as SunLight's shadow gate from the
// caller's side. Two unrelated readings, one conclusion.
//
// `cam` is a by-value `Graphics::Camera` return: retail reserves 0xe0 bytes of
// stack at RSP+0x30 and `sizeof(Camera)` is 0xd8 (src/engine/graphics/
// Camera.h, static_assert'd there). The hidden return slot goes in RDX because
// GetSunCamera is a MEMBER function — `this` takes RCX and the visible
// `Renderer&` is pushed out to R8.
// ============================================================================

namespace Graphics {

// EXT SEAM (NOT RETAIL, docs/extensions/handins/EYECAM.md §9): ext.eye_camera's shadow-fit view (SunLight.h has the contract).
const Camera* SunLight::_extShadowFitView = nullptr;

// NOT RETAIL (ext.staggered_passes, graphics/stagger_hold.h): null = retail.
const int32_t* SunLight::_extStaggerEvery = nullptr;

namespace {

// NOT RETAIL (ext.staggered_passes): the shadow map's last real render (main thread), the sun
// camera it was rendered with, and whether the current Draw held it.
StaggerHold::State g_shadowHold;
alignas(Camera) unsigned char g_heldSunCameraBytes[sizeof(Camera)];
bool g_shadowHeldThisDraw = false;
// The view may move by this fraction of the shadow box's width before the map renders again
// (0.5 %: the box is fitted to the view frustum, so the uncovered strip at its leading edge
// stays a fraction of a percent of the box).
constexpr float kShadowMoveFraction = 0.005f;

const Camera& HeldSunCamera() { return *reinterpret_cast<const Camera*>(g_heldSunCameraBytes); }

}  // namespace

bool SunLight::ExtTakeShadowHeld() {
    const bool held = g_shadowHeldThisDraw;
    g_shadowHeldThisDraw = false;
    return held;
}

namespace {

// EXT SEAM (NOT RETAIL, docs/extensions/handins/EYECAM.md §9): ext.eye_camera -- retail's GetSunCamera, fitted to
// `view`. GetSunCamera reads draw group 0's camera (sun_camera.cpp), so `view`
// sits there for this one call and the real camera is put back at once: the
// main pass, the BeginDrawGroup below and every other reader see the real one.
Camera ExtShadowFitSunCamera(const SunLight& sun, Renderer& renderer, const Camera& view) {
    Camera& group0 = renderer._drawGroups._data[0]._camera;
    const Camera saved(group0);
    group0 = view;
    Camera sunCamera = sun.GetSunCamera(renderer);
    group0 = saved;
    return sunCamera;
}

}  // namespace

void SunLight::AddDrawItems(const Scene& sceneRef, Renderer& rendererRef) const {
    ++g_sunAddDrawItems;

    // B.4: `renderer+0x10/+0x3c/+0x48` are `_drawGroups` / `_shaderDetail` /
    // `_detailId` (Camera.h); the bridge is gone.
    if (_depthTarget == nullptr) return;
    ++g_sunAddDrawEmit;

    // EXT SEAM (NOT RETAIL, docs/extensions/handins/FAA-SHADOW.md): ext.max_shadow_res (SunLight.h), null unless the
    // row is registered for this run; then the map's size follows the row and
    // the retail combo (a rebuild is requested when it differs).
    if (_extMaxShadowRes != nullptr) ExtSyncMaxShadowRes(false);   // record + log; never a raise here

    // NOT RETAIL (issue #253, counters only): the shadow group's share of pass 1.
    const int64_t splitT0 = PassSplit::Begin();

    // ENDGAME B.3.5 caller batch: all six by-name doors here are DEFINED members
    // today and every receiver is a real class in this body's own signature, so
    // all six are member calls and the six `using` typedefs above them are gone.
    //   AddLight / BeginDrawGroup / EndDrawGroup  -> Graphics::Renderer
    //   AddGlobalDrawItems / AddCulledDrawItems   -> Graphics::Scene
    //   GetSunCamera                              -> Graphics::SunLight, RETSLOT
    rendererRef.Graphics::Renderer::AddLight(this);

    // EXT SEAM (NOT RETAIL, issue #253): ext.staggered_passes' shadow hold (SunLight.h). Only
    // the group that draws the map (shaderDetail != 0: the main scene; the reflection's sun
    // gathers no casters) and never for ext.eye_camera's view. A held frame opens the shadow
    // group with the sun camera the map was rendered with, so DrawLight samples it with the
    // matching matrix, and gathers nothing: DrawShadowMap keeps the map (ExtShadowHoldEmit).
    StaggerHold::View now;
    StaggerHold::Key key;
    bool latch = false;
    if (_extStaggerEvery != nullptr && rendererRef._shaderDetail != 0) {
        const ::Graphics::Camera& view = rendererRef._drawGroups._data[0]._camera;
        StaggerHold::FillView(now, &view._cameraToWorld, &view._projection);
        key.a     = &sceneRef;
        key.b     = _depthTarget;
        key.c     = rendererRef._shaderDetail;
        key.d     = (_depthTarget->Width() << 4) | (rendererRef._detailId & 0xf);
        key.epoch = StaggerHold::Epoch();
        // The box half-width is the sun frustum's side plane distance (GetSunCamera's `w`).
        float halfWidth = HeldSunCamera()._frustum.planes[2].w;
        if (halfWidth < 0.0f) halfWidth = -halfWidth;
        const bool eyeDraw = _extShadowFitView != nullptr;
        if (!eyeDraw && StaggerHold::ShouldHold(g_shadowHold, *_extStaggerEvery, StaggerHold::kShadowPhase,
                                                rendererRef._frameIndex, key, now,
                                                2.0f * halfWidth * kShadowMoveFraction)) {
            g_shadowHeldThisDraw = true;
            ++StaggerHold::g_counters.shadowHeld;
            const int heldGroup = rendererRef.Graphics::Renderer::BeginDrawGroup(
                _renderBuffer, view, HeldSunCamera(), static_cast<int32_t>(::Graphics::MaterialPass::Depth),
                Bits(::Graphics::MaterialTypeMask::Opaque));
            rendererRef.Graphics::Renderer::EndDrawGroup(heldGroup);
            PassSplit::EndSun(splitT0);   // issue #253: a held frame's (small) shadow share
            return;
        }
        g_shadowHeldThisDraw = false;
        ++StaggerHold::g_counters.shadowRendered;
        if (eyeDraw)
            g_shadowHold.valid = false;
        else
            latch = true;   // after the sun camera below is made
    }

    // `GetSunCamera` is a HIDDEN-RETSLOT member (B5-12): the one 0xd8
    // `Graphics::Camera` retail reserves at RSP+0x30 is this local, built from
    // the member's prvalue (C++17 guaranteed elision: one construction).
    static_assert(sizeof(::Graphics::Camera) == 0xd8, "retail reserves 0xd8 for the sun camera");
    // EXT SEAM (NOT RETAIL, docs/extensions/handins/EYECAM.md §9): null unless
    // ext.eye_camera is drawing (selected at GameUpdateLoop::Draw step 7);
    // then the shadow fit uses its view. Otherwise retail's line, unchanged.
    const ::Graphics::Camera sunCamera =
        (_extShadowFitView != nullptr)
            ? ExtShadowFitSunCamera(*this, rendererRef, *_extShadowFitView)
            : this->::Graphics::SunLight::GetSunCamera(rendererRef);

    // EXT SEAM (NOT RETAIL, issue #253): ext.staggered_passes keeps the camera this map is
    // rendered with, for the frames that hold it.
    if (latch) {
        new (g_heldSunCameraBytes) ::Graphics::Camera(sunCamera);
        StaggerHold::Latch(g_shadowHold, rendererRef._frameIndex, key, now);
    }

    // Draw group ZERO's camera (`renderer+0x10` data, +0x08).
    const ::Graphics::Camera& sceneCam = rendererRef._drawGroups._data[0]._camera;
    const int group = rendererRef.Graphics::Renderer::BeginDrawGroup(
        _renderBuffer, sceneCam, sunCamera, static_cast<int32_t>(::Graphics::MaterialPass::Depth),
        Bits(::Graphics::MaterialTypeMask::Opaque));

    if (rendererRef._shaderDetail != 0) {                 // +0x3c
        sceneRef.Graphics::Scene::AddGlobalDrawItems(rendererRef);
        if (rendererRef._detailId != 0)                   // +0x48
            sceneRef.Graphics::Scene::AddCulledDrawItems(rendererRef);
    }

    rendererRef.Graphics::Renderer::EndDrawGroup(group);
    PassSplit::EndSun(splitT0);   // issue #253
}

}  // namespace Graphics

void RtGraphicsWave28PartsysSummary() {
    printf("[PARTSYS28] PSI::Update entries=%llu earlyOut=%llu spawned=%llu aged=%llu killed=%llu\n",
                (unsigned long long)g_psiUpdate, (unsigned long long)g_psiUpdateEarly,
                (unsigned long long)g_psiSpawned, (unsigned long long)g_psiAged,
                (unsigned long long)g_psiKilled);
    printf("[PARTSYS28] PI::RemoveState calls=%llu freedPool=%llu | "
                "SunLight Update=%llu Draw(MHHH)=%llu AddDrawItems=%llu emitted=%llu\n",
                (unsigned long long)g_piRemoveState, (unsigned long long)g_piRemoveStateFree,
                (unsigned long long)g_sunUpdate, (unsigned long long)g_sunDrawMhhh,
                (unsigned long long)g_sunAddDrawItems, (unsigned long long)g_sunAddDrawEmit);
}
