// ============================================================================
// src/engine/graphics/wave27_lane_gfxpart.cpp — Wave 27 lane GFXPART
//   the Graphics particle vtable slot bodies
// ============================================================================
//
// Triage for every row this lane was dealt — ported, deferred, and why — is
// docs/history/runtime_port/WAVE27_LANE_GFXPART.md. Read §4 before adding a body here.
//
// EXPORT TABLE, read with pefile out of the shipped
// C:\GOG Games\Banished\Runtime-x64.dll BEFORE a line of this file was written
// (reference_resolveexport_phantom_names.md — a name the DLL does not contain
// resolves NULL in silence):
//
//   | Export                                                 | Retail      |
//   |--------------------------------------------------------|-------------|
//   | ?Serialize@Particle@Graphics@@UEAA_NAEAVStream@IO@@@Z   | 0x180103f00 |
//   | ?Draw@ParticleInstance@Graphics@@UEBAX…MHHH@Z           | 0x180104230 |
//   | ?AddDrawItems@ParticleInstance@Graphics@@UEBAX…@Z       | 0x180104700 |
//   | ?GetInstanceTransform@ParticleInstance@Graphics@@…HM@Z  | 0x1801047f0 |
//   | ?SetParticle@ParticleInstance@Graphics@@UEAAX…@Z        | 0x180104a00 |
//   | ?SetParticle@ParticleSystemInstance@Graphics@@UEAAX…@Z  | 0x1800a6a80 |
//
// All six are vtable slots of `??_7ParticleInstance@Graphics@@6B@` (DLL
// 0x1801550e0), `??_7ParticleSystemInstance@Graphics@@6B@` (0x180155040) and
// `??_7Particle@Graphics@@6B@` (0x180155130). The three ParticleInstance bodies
// are SHARED slots: they appear at the same indices in the
// ParticleSystemInstance table, so one registration closes two rows.
//
// EVERY BODY BELOW WAS TRANSCRIBED FROM `disassemble_function`, NOT FROM
// GHIDRA'S C (reference_ghidra_c_output_vs_disassembly.md: the decompiler drops
// dereferences, prints MOVSXD as a zero extension, and drops whole arguments at
// a mistyped vtable call — and these bodies are nothing but mistyped vtable
// calls and float chains).
//
// ---------------------------------------------------------------------------
// THE SHARED RNG, AND WHY A NEW GENERATOR WOULD BE A SILENT SAVE DESYNC
// ---------------------------------------------------------------------------
// `?_rng@Particle@Graphics@@1VRandom@System@@A` (retail 0x1801fe7f0) is a
// PROTECTED STATIC `System::Random` — one MT19937 shared by every Particle in
// the process, seeded from `time()` at load by 0x180149650. It is tagged
// RT_UNPROMOTABLE | HARMFUL in standalone/reg_mint_promoted.cpp on the
// DLL-PRESENT route, precisely because in-DLL RIP-relative readers keep using
// the DLL's copy.
//
// `ParticleInstance::SetParticle` DRAWS FROM IT, and retail INLINES the draw —
// the tempering, the index advance and the 0x270 wrap are in SetParticle's own
// bytes, not behind a call. So this port must read and write THAT OBJECT, at
// that address, through the same door `src/game/Game/tornado.cpp:253` already
// uses. Constructing our own generator would compile, run, look right, and
// desync every particle placement from the DLL's other consumers — a divergence
// that would not look like a port bug.
//
// The tempering constants below are the SHIPPED ones, lifted from the
// disassembly rather than retyped from an MT19937 reference: retail's masks are
// `0xff3a58ad` and `0xffffdf8c`, which are NOT the textbook `0x9d2c5680` /
// `0xefc60000`. They are those constants with the bits that the shift discards
// already cleared (`0x9d2c5680 & (0xffffffff >> 7)` style), which is what MSVC
// does when it can prove the high bits are shifted out. Retyping the textbook
// values would be arithmetically equivalent HERE and is still not what this file
// does, because nothing in the port proves the shift amounts it pairs them with
// (reference_crt_coefficients_must_be_lifted_not_retyped.md).
//
// ---------------------------------------------------------------------------
// FLOAT ASSOCIATION ORDER IS LOAD-BEARING
// ---------------------------------------------------------------------------
// Every lerp below is written in retail's own order — `prev + (cur - prev) * a`
// where retail subtracts, multiplies and adds in that sequence, and
// `((1 - a) * prev) + (a * cur)` where it does that instead. The two are
// algebraically identical and numerically different
// (reference_moved_term_hides_where_terms_cancel.md), and a particle system is
// exactly where a 1-ULP difference becomes a visibly different frame.
//
// `1.0f` appears in three of these bodies as `[0x18016169c]`. Its shipped bit
// pattern was read out of the DLL with read_memory (0x3F800000) rather than
// assumed from the mnemonic.
// ============================================================================

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>

#include "graphics/Geometry.h"
#include "graphics/Particle.h"
#include "graphics/ParticleInstance.h"
#include "io/Serializer.h"
#include "io/Stream.h"                   // B.4: IO::Stream::_direction
#include "math/Math.h"
#include "graphics/Camera.h"    // ENDGAME B.3.5 caller batch: Graphics::Renderer
#include "graphics/Material.h"  // ENDGAME B.3.5 caller batch: Graphics::MaterialInstance
#include "video/Context.h"      // ENDGAME B.3.5 caller batch: Video::Context
#include "collection/Collection.h"   // rtcoll:: the array instantiations
#include "object/External.h"         // Object::SerializeExternal<MaterialInstance>
#include "object/TypeInfo.h"         // Object::Base::IsDerivedFrom
#include "system/Value.h"            // System::Random::NextGroup
#include "system/MemoryAllocator.h"

namespace rtcoll {
// Lane Q's `?MemAlloc@MemoryAllocator@System@@SAPEAX_KH@Z` (0x1800c7a50) door:
// `EnterCriticalSection(_singleton+0x640); Allocate(_singleton, n, align);
// LeaveCriticalSection`. Declared, not re-derived, so lane E has ONE place to
// repoint when the allocator moves. Defined in src/engine/collection/array.cpp.
void* MemAlloc(uint64_t bytes, int align);
}  // namespace rtcoll

// ============================================================================
// Attendance counters. A run that entered none of these says so rather than
// staying silent, and `calls=` is read separately from any comparison
// (reference_uncalled_port_is_not_verified.md).
// ============================================================================
namespace {

uint64_t g_particleSerialize   = 0;
uint64_t g_particleSerRecords  = 0;
uint64_t g_piDraw              = 0;
uint64_t g_piDrawQuads         = 0;
uint64_t g_piAddDrawItems      = 0;
uint64_t g_piAddDrawItemsEmit  = 0;
uint64_t g_piGetInstanceXform  = 0;
uint64_t g_piSetParticle       = 0;
uint64_t g_piSetParticleRand   = 0;
uint64_t g_psiSetParticle      = 0;

// ---------------------------------------------------------------------------
// Retail's shipped `1.0f`, at 0x18016169c. read_memory says 0x3F800000.
// ---------------------------------------------------------------------------
constexpr uint32_t kOneBits = 0x3F800000u;
inline float One() {
    float f;
    const uint32_t bits = kOneBits;
    std::memcpy(&f, &bits, sizeof f);
    return f;
}
// 0x18016167c = 0x3C8EFA35 = pi/180. ParticleInstance::Draw multiplies the
// particle's rotation field by it, so the field is stored in DEGREES. Lifted as
// a bit pattern, not retyped as `3.14159f / 180.0f`, which is a DIFFERENT float.
constexpr uint32_t kDegToRadBits = 0x3C8EFA35u;
inline float DegToRad() {
    float f;
    const uint32_t bits = kDegToRadBits;
    std::memcpy(&f, &bits, sizeof f);
    return f;
}

// ---------------------------------------------------------------------------
// `Graphics::Particle`, the fields THIS lane pinned (B.4: all named on the
// class, Particle.h):
//   `_active` +0x20, `_recordCount` +0x24, `_subCount` +0x28,
//   `_material` +0x30 — ExternalPtr<MaterialInstance>, corroborated from BOTH
//          ends: Particle::Serialize hands &+0x30 to the ExternalBasePtr::
//          Serialize instantiation at 0x180014520, and ParticleInstance::
//          AddDrawItems passes `*(void**)(particle+0x30)` as Renderer::
//          AddDrawItem's `const MaterialInstance*` argument;
//   `_records` +0x38 — the pool-record array {data, size@+0x40, cap@+0x44},
//          pinned by the profile build's ~Particle, which loops
//          `i < *(int*)(this+0x40)` over `*(void**)(this+0x38) + i*8`.
//
// The per-instance particle pool (`ParticleInstance::_boundsPtr`, +0xc8) is
// `RtGraphics::ParticleBoundsRecord` and each live particle, stride 0x58, is
// `RtGraphics::ParticleRecord` (ParticleInstance.h; this lane MEASURED the
// whole 0x40 and the Draw reads, wave 28 the rest). The Draw reads each
// {current, previous} pair: `_pos` / `_color` (Wave 27: "posA" / "posB"),
// `_alpha` (Wave 27: "scroll"), `_rot` (DEGREES) and `_size`.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// `System::Random` — MT19937 (`_index` +0x00, zero means "regenerate before
// use"; `_mt[624]` +0x04). `?NextGroup@Random@System@@AEAAXXZ` (0x1800c7dd0) is
// the twist. AEAA means PRIVATE, not unexported — this DLL exports its private
// members (reference_aeaa_is_private_not_unexported.md); our header declares
// it public, so it is a member call (B3R).
// ---------------------------------------------------------------------------

// (`ParticleRng()`, the lazy name door onto ?_rng@Particle@Graphics@@, is gone:
// ENDGAME B.3.9 GFX made it the class static `Graphics::Particle::_rng`.)

// One MT19937 draw, exactly as retail inlines it (five call sites in
// SetParticle/Update share the identical instruction sequence). Returns the
// tempered word; the index advance and 0x270 wrap are part of the draw.
uint32_t RngNext(System::Random& rng) {
    int32_t index = rng._index;
    if (index == 0) {
        rng.NextGroup();                     // 0x1800c7dd0
        index = rng._index;
    }
    uint32_t y = rng._mt[static_cast<uint32_t>(index)];
    // The shipped tempering, masks lifted from the bytes (see the banner).
    y ^= y >> 11;
    y ^= (y & 0xff3a58adu) << 7;
    y ^= (y & 0xffffdf8cu) << 15;
    y ^= y >> 18;
    int32_t next = index + 1;
    if (next == 0x270) next = 0;     // retail: CMP EDX,0x270 / CMOVZ EDX,<zero reg>
    rng._index = next;
    return y;
}

// The shared Serialize prologue/epilogue (`{ &stream, nullptr }`, the
// encoding's CreateReader / CreateWriter with version 0, and `if (rw) delete
// rw` at the end) is `IO::Serializer`'s ctor and dtor, inlined by retail; B.4
// spells it as the class.

}  // namespace

// ============================================================================
// bool Graphics::Particle::Serialize(IO::Stream&)
// ?Serialize@Particle@Graphics@@UEAA_NAEAVStream@IO@@@Z   retail 0x180103f00
// `??_7Particle@Graphics@@6B@` slot 5 — the LAST unported slot of that table.
//
// Retail, read at 0x180103f00 (33 instructions of prologue + a read-side loop):
//
//   Serializer ser{ &stream, nullptr };
//   rw = stream.direction == 0 ? enc->vt[0x18](enc,&ser,0)
//                              : enc->vt[0x20](enc,&ser,0);
//   rw->vt[0x78](rw, &this->_active,   0)     bool
//   rw->vt[0xa0](rw, &this->_count,    0)     int
//   rw->vt[0xa0](rw, &this->_subCount, 0)     int
//   0x180014520(&this->_material, &ser)       ExternalBasePtr<MaterialInstance>
//   if (stream.direction == 0 && this->_active) {
//       0x180062110(&this->_records, this->_count);        Array<void*>::SetSize
//       for (i = 0; i < this->_count; ++i) {
//           p = Enter/Allocate(0x40, 8)/Leave;
//           this->_records.data[i] = p ? 0x180103bf0(p, this) : nullptr;
//       }
//   }
//   if (rw) rw->vt[0](rw, 1);
//   return true;                               MOV AL,1 — no failure path
//
// FOUR THINGS THE DISASSEMBLY SAYS AND A DECOMPILE WOULD BLUR.
//
// 1. The read-side block is gated on `[stream+8] != 0 -> SKIP`. It runs when the
//    stream is READING. Inverting that would allocate a pool on every SAVE.
// 2. The loop bound is RE-READ from `this->_count` at the bottom
//    (`CMP EDI,[R15+0x24]`), and the array base is RE-READ from
//    `this->_records.data` at the top of each iteration. Both are transcribed as
//    re-reads. Nothing under the loop can change either today, so this is a
//    faithfulness choice with no observable effect — stated so a later reader
//    does not "optimise" it and quietly change one that does.
// 3. The element ctor's return value is what is stored, not the raw allocation:
//    `CALL 0x180103bf0; MOV RCX,RAX`. It returns its own first argument, so the
//    two agree today; the port stores what retail stores.
// 4. `MOV AL,0x1` — always true. There is no error path to reproduce.
// ============================================================================
// ENDGAME B.3.9 block GFX: the slot-5 door `RtGraphics_Particle_Serialize` is
// gone with the minted `??_7Particle@Graphics@@6B@` (its only reader besides
// ParticleSystem::Serialize, which now calls the member). Retail has no null
// test on `this` here (`MOV [RSP+0x10],RBX` then a straight `MOV RBX,RDX`).

namespace Graphics {

bool Particle::Serialize(IO::Stream& streamv) {
    ++g_particleSerialize;

    IO::Stream& stream = streamv;

    IO::Serializer ser(stream, 0);
    IO::ReaderWriter* const rw = ser._readerWriter;

    rw->SerializeBool(_active, nullptr);       // +0x78
    rw->SerializeInt(_recordCount, nullptr);   // +0xa0
    rw->SerializeInt(_subCount, nullptr);      // +0xa0

    // 0x180014520, the `Object::ExternalBasePtr::Serialize` instantiation
    // (UNNAMED in the export directory) — RtObject_ExternalPtrSerialize_
    // MaterialInstance, by name. LEFT: it takes object's POD spellings
    // (External.h).
    Object::SerializeExternal<Graphics::MaterialInstance>(ser, _material);

    if (stream._direction == IO::kReading && _active != 0) {
        // 0x180062110, `Collection::Array<void*>::SetSize` (UNNAMED): stride 8,
        // align 8, new tail UNINITIALISED — kSpec_Fill8 exactly; by name.
        //
        // The pool-record constructor at retail 0x180103bf0, UNNAMED (checked by
        // VA): ParticleBoundsRecord(const Particle*) (below), placement-new. It seeds
        // the Box, zeroes the counters, writes -1 at `frameTag` and sizes its
        // own `_records` to `particle->_subCount` through 0x180061bb0.
        //
        // The loop bound is RE-READ from `_recordCount` at the bottom and the
        // array base RE-READ from `_records._data` at the top of each
        // iteration, exactly as retail does (point 2 of the banner).
        rtcoll::RtColl_ArraySetSize_180062110(&_records, _recordCount);
        for (int32_t i = 0; i < _recordCount; ++i) {
            void* const p = rtcoll::MemAlloc(0x40, 8);
            RtGraphics::ParticleBoundsRecord* const v =
                p ? ::new (p) RtGraphics::ParticleBoundsRecord(this) : nullptr;
            _records._data[i] = v;
            ++g_particleSerRecords;
        }
    }

    return true;   // ~Serializer: `if (rw) delete rw`
}

// ---------------------------------------------------------------------------
// `Particle::TypeName` (slot 1), `::Type` (slot 2) and `::GetFileVersion`
// (slot 3) are DECLARE_TYPE's / graphics_typeinfo_accessors.cpp's.
// ENDGAME B.3.8 block RB: `~Particle()` is inline in Particle.h, its body
// (retail 0x180103c70) ported as `Particle::ReleaseFields` in animation.cpp.

// ?CreateNewObject@Particle@Graphics@@SAPEAVBase@Object@@XZ 0x18001ff40 and
// ?CreateInPlaceObject@Particle@Graphics@@SAPEAVBase@Object@@PEAX@Z 0x18001ff50:
// the ABSTRACT factory pair, both `XOR EAX,EAX ; RET` in retail (the FACTREST
// row they replace carried no steps for that reason). ENDGAME B.3.9 GFX, D2.
Object::Base* Particle::CreateNewObject() { return nullptr; }
Object::Base* Particle::CreateInPlaceObject(void* place) { (void)place; return nullptr; }

}  // namespace Graphics

// ============================================================================
// Math::Matrix Graphics::ParticleInstance::GetInstanceTransform(int, float) const
// ?GetInstanceTransform@ParticleInstance@Graphics@@UEBA?AVMatrix@Math@@HM@Z
// retail 0x1801047f0 — vtable slot 6 of BOTH particle instance tables.
//
// RCX = this, RDX = the hidden Matrix return slot, R8D = the int index,
// XMM3 = the interpolation alpha.
//
// *** THE INT ARGUMENT IS IGNORED. *** Retail's first act is
// `LEA R8,[RCX+0x84]`, which overwrites it before any read. That is not an
// oversight to "fix": the base `Geometry::GetInstanceTransform` (0x1800154a0)
// uses the index, and this override deliberately does not.
//
// Retail:
//   t   = alpha
//   pos = prevTransform.translation
//         + (transform.translation - prevTransform.translation) * t
//   q   = prevTransform.rotation.Lerp(transform.rotation, t)
//   m   = Transform{ q, pos }.AsMatrix()
//   out = { m[0..2], 1.0f,  m[4..6], 1.0f,  m[8..10], 1.0f,
//           m[12..14], ((1.0f - t) * _opacityPrev) + (t * _opacity) }
//
// The fourth column is NOT the matrix's own — retail overwrites all four
// w-components: three with the shipped 1.0f and the last with the interpolated
// opacity. A port that copied `m` wholesale and patched only +0x3c would leave
// AsMatrix's w-row in place and hand the shader a different matrix.
//
// RULE B5-12: the hidden return slot is spelled OUT, in RETAIL ORDER, and
// returned again. THIS is the slot the 2026-09-13 perf-only crash came through
// (`1a4200c3`), and Geometry.h records why a C++ by-value return would be the
// WRONG fix on a vtable slot under GCC.
// ============================================================================

namespace Graphics {

Math::Matrix* ParticleInstance::GetInstanceTransform(Math::Matrix* out, int index,
                                                     float alpha) const {
    (void)index;         // ignored, see the banner above
    ++g_piGetInstanceXform;

    const Math::Transform& cur = _transform._current;
    const Math::Transform& prv = _transform._previous;

    const float t = alpha;

    // Retail's order exactly: subtract, scale, add back onto the PREVIOUS value.
    Math::Transform blended;
    const float px = prv.translation.x, py = prv.translation.y, pz = prv.translation.z;
    blended.translation.x = (cur.translation.x - px) * t + px;
    blended.translation.y = (cur.translation.y - py) * t + py;
    blended.translation.z = (cur.translation.z - pz) * t + pz;

    // `this` is the PREVIOUS rotation and the argument is the CURRENT one, so the
    // lerp runs prev -> cur, matching the translation blend above.
    blended.rotation = prv.rotation.Lerp(cur.rotation, Math::Scalar(t));

    const Math::Matrix m = blended.AsMatrix();

    const float one = One();
    out->m[0]  = m.m[0];  out->m[1]  = m.m[1];  out->m[2]  = m.m[2];  out->m[3]  = one;
    out->m[4]  = m.m[4];  out->m[5]  = m.m[5];  out->m[6]  = m.m[6];  out->m[7]  = one;
    out->m[8]  = m.m[8];  out->m[9]  = m.m[9];  out->m[10] = m.m[10]; out->m[11] = one;
    out->m[12] = m.m[12]; out->m[13] = m.m[13]; out->m[14] = m.m[14];
    // ((1 - t) * prevOpacity) + (t * opacity) — retail computes the two products
    // and adds the t-term LAST. Folding it the other way is a different float.
    out->m[15] = ((one - t) * _opacityPrev) + (t * _opacity);
    // Hidden return slot: the by-value Matrix's buffer comes in RDX and its
    // address goes back in RAX. Declared void until 2026-09-13; RAX was garbage
    // at -O2 and DrawItemList's SetTransform read through it (perf build only).
    return out;
}

}  // namespace Graphics

// ============================================================================
// void Graphics::ParticleInstance::AddDrawItems(const Scene&, Renderer&) const
// ?AddDrawItems@ParticleInstance@Graphics@@UEBAXAEBVScene@2@AEAVRenderer@2@@Z
// retail 0x180104700 — vtable slot 3 of BOTH particle instance tables.
//
// *** THE `Scene&` ARGUMENT IS NEVER READ. *** RDX is overwritten with `this`
// at 0x18010477c before any load through it. Faithful, not an omission.
//
// Retail:
//   pool = this->_boundsPtr
//   if (pool->_liveCount == 0) return;                // +0x28
//   group = renderer._drawGroups[renderer._currentDrawGroup]
//   d  = this->_transform.translation - group._camera._cameraToWorld.translation
//   Renderer::AddDrawItem(
//       &renderer,
//       this,                                          // const Geometry*
//       this->_resource->_material,                    // const MaterialInstance*
//       0x8000,                                        // ushort
//       0,                                             // ushort
//       (d.x*d.x + d.y*d.y) + d.z*d.z,                 // float, retail's order
//       ((1.0f - renderer._alphaT) * _opacityPrev)
//           + (renderer._alphaT * _opacity),           // float
//       this->_resource->_active ? 0x34 : 0,           // int
//       pool,                                          // const void*
//       0);                                            // uint
//
// TWO THINGS WORTH SPELLING OUT.
//
// 1. `group + 0x90` is the draw group's view position: `_camera` (+0x08) ->
//    `_cameraToWorld` (+0x64) -> `translation` (+0x24) -- Camera.h names the
//    whole group now.
// 2. The int flag is `(active ? -1 : 0) & 0x34` — retail computes it with
//    `NEG AL; SBB ECX,ECX; AND ECX,0x34`, a branchless mask, not a conditional.
//    0x34 and not 0x34-something: the immediate is in the instruction.
// ============================================================================

namespace Graphics {

void ParticleInstance::AddDrawItems(const Scene& scene, Renderer& renderer) const {
    (void)scene;         // never read by retail -- see the banner above
    ++g_piAddDrawItems;

    const RtGraphics::ParticleBoundsRecord* const pool = _boundsPtr;
    if (pool->_liveCount == 0) return;

    // renderer._drawGroups (+0x10) indexed by renderer._currentDrawGroup (+0x34).
    // MOVSXD — a SIGNED widening; a negative index here would address
    // backwards, which is retail's behaviour and not ours to guard.
    const int64_t gi = static_cast<int64_t>(renderer._currentDrawGroup);
    const Math::Vector3& eye =
        renderer._drawGroups._data[gi]._camera._cameraToWorld.translation;   // group + 0x90

    float dx = _transform._current.translation.x - eye.x;
    float dy = _transform._current.translation.y - eye.y;
    float dz = _transform._current.translation.z - eye.z;
    dx *= dx; dy *= dy; dz *= dz;
    // Retail: (dy + dx) + dz. ADDSS XMM2,XMM0 then ADDSS XMM2,XMM1, where XMM2
    // held the Y term, XMM0 the X term and XMM1 the Z term.
    const float distSq = (dy + dx) + dz;

    const Particle* const res = _resource;
    const MaterialInstance* const material = res->_material;      // +0x30
    const int32_t flags = (res->_active != 0 ? -1 : 0) & 0x34;

    const float a   = renderer._alphaT;          // +0x40
    const float one = One();
    const float opacity = ((one - a) * _opacityPrev) + (a * _opacity);

    // `?AddDrawItem@Renderer@Graphics@@QEAAXPEBVGeometry@2@PEBVMaterialInstance@2@GGMMHPEBXI@Z`
    // (retail 0x1801057b0), a DEFINED member. Nine arguments after `this`, and
    // the arity was checked against the member, not the decompiler.
    renderer.Graphics::Renderer::AddDrawItem(
        this, material, 0x8000, 0, distSq, opacity, flags, pool, 0);
    ++g_piAddDrawItemsEmit;
}

}  // namespace Graphics

// ============================================================================
// void Graphics::ParticleInstance::SetParticle(ExternalPtr<Particle>)
// ?SetParticle@ParticleInstance@Graphics@@UEAAXV?$ExternalPtr@VParticle@Graphics@@@Object@@@Z
// retail 0x180104a00 — vtable slot 9 of `??_7ParticleInstance@Graphics@@6B@`.
//
// The ExternalPtr is passed BY VALUE and is one pointer wide, so RDX is the
// Particle* itself. Retail spills it to its home slot and reloads — which
// matters, because RemoveState() runs in between and RDX is not preserved
// across a call. A port that kept it in a local is doing the same thing.
//
// Retail:
//   this->RemoveState();                                 // 0x1801040b0
//   this->_resource = p;
//   this->_materialTypeFlags = 1 << p->_material->_shaderClass;   // +0x30, +0x20
//   if (p->_active) {
//       n = p->_recordCount;
//       idx = (n == 0) ? 0 : RngNext(_rng) % n;           // UNSIGNED remainder
//       this->_index = idx;
//       rec = p->_records._data[idx];
//   } else {
//       void* m = operator new(0x40);                     // 0x180061a80
//       rec = m ? 0x180103bf0(m, p) : nullptr;
//   }
//   this->_boundsPtr = rec;
//   if (any of _localBounds' six floats != 0.0f) {
//       zero all six; this->_dirtyFlags |= kDirtyBounds;
//   }
//
// THE SHIFT IS UNMASKED. Retail emits `MOV EDX,1; SHL EDX,CL` with CL taken from
// the material's +0x20, so a value >= 32 shifts by `cl & 31` on x86. Reproduced
// as `1u << (v & 31)` because C++ would otherwise be undefined behaviour, and
// the mask is what the HARDWARE does — that is the faithful reading, not a guard.
//
// THE BOUNDS TEST IS A SIX-WAY EXACT COMPARE AGAINST ZERO, with the NaN case
// falling into the "not equal" arm (retail's `JP` on each UCOMISS jumps to the
// reset). Written below with that exact shape rather than as `!= 0.0f`, which a
// compiler is free to reassociate.
//
// ============================================================================

namespace Graphics {

void ParticleInstance::SetParticle(Object::ExternalPtr<Particle> particle) {
    Particle* const p = particle.Get();
    ++g_piSetParticle;

    // ?RemoveState@ParticleInstance@Graphics@@IEAAXXZ (retail 0x1801040b0), the
    // protected member (wave28_lane_partsys.cpp), QUALIFIED.
    this->ParticleInstance::RemoveState();

    _resource = p;

    // 1 << material->_shaderClass (+0x20). Faithful deref of a pointer retail
    // does not check (docs: don't "fix" a faithful crash with a null check).
    const int32_t shift = p->_material->_shaderClass;
    _materialTypeFlags = 1u << (static_cast<uint32_t>(shift) & 31u);

    RtGraphics::ParticleBoundsRecord* rec = nullptr;
    if (p->_active != 0) {
        const int32_t n = p->_recordCount;
        uint32_t idx = 0;
        if (n != 0) {
            idx = RngNext(Graphics::Particle::_rng) % static_cast<uint32_t>(n);
            ++g_piSetParticleRand;
        }
        _index = static_cast<int32_t>(idx);
        rec = p->_records._data[idx];
    } else {
        // 0x180061a80, the per-TU `operator new(0x40)` (UNNAMED): `Enter(cs);
        // Allocate(_singleton, 0x40, 8); Leave(cs)`. A null allocation is
        // retail's own tolerated case — it stores nullptr and the caller copes.
        void* const m = RtGraphics_ParticlePool_OperatorNew();
        if (m) {
            // The pool-record ctor at 0x180103bf0, UNNAMED; the same body
            // Particle::Serialize above calls, by name.
            rec = ::new (m) RtGraphics::ParticleBoundsRecord(p);
        }
    }
    _boundsPtr = rec;

    // The six-float local-bounds reset. `!(a == 0)` rather than `a != 0` so the
    // NaN case takes the reset arm, which is what retail's JP-before-JNZ does.
    ::Box& lb = _localBounds;
    const bool allZero = (lb.min.x == 0.0f) && (lb.min.y == 0.0f) && (lb.min.z == 0.0f) &&
                         (lb.max.x == 0.0f) && (lb.max.y == 0.0f) && (lb.max.z == 0.0f);
    if (!allZero) {
        lb.min.x = 0.0f; lb.min.y = 0.0f; lb.min.z = 0.0f;
        lb.max.x = 0.0f; lb.max.y = 0.0f; lb.max.z = 0.0f;
        _dirtyFlags |= RtGraphics::kDirtyBounds;
    }

    // EXT SEAM (NOT RETAIL): ext.create_on_demand's prefetch (ParticleInstance.h has the contract).
    if (_extParticleAssigned != nullptr) _extParticleAssigned(*p);
}

void (*ParticleInstance::_extParticleAssigned)(const Particle& particle) = nullptr;

}  // namespace Graphics

// ============================================================================
// void Graphics::ParticleSystemInstance::SetParticle(ExternalPtr<Particle>)
// ?SetParticle@ParticleSystemInstance@Graphics@@UEAAXV?$ExternalPtr@…@Z
// retail 0x1800a6a80 — vtable slot 9 of `??_7ParticleSystemInstance@…6B@`.
//
// Retail:
//   ParticleInstance::SetParticle(this, p);           // CALL 0x180104a00
//   res = this->_resource;
//   if (res && !res->_active && this->_emitter) 0x180020d60(this->_emitter);
//   sys = IsDerivedFrom(p, ParticleSystem::_typeInfo.typeId) ? p : nullptr;
//   if (this->_resource->_active)
//       this->_emitter = sys->_pool._data[this->_index];
//   else {
//       void* m = operator new(...);                   // 0x1800205a0
//       if (m) { *(u64*)m = 0; *(u64*)(m+8) = 0;
//                *(u32*)(m+0x10) = sys->_emissionRate (raw bits);
//                0x180020e90(m, sys->_subCount); }
//       this->_emitter = m;
//   }
//
// TWO FAITHFUL DEREFS OF A POINTER RETAIL DOES NOT CHECK, left unguarded on
// purpose (CLAUDE.md: don't "fix" a faithful crash — find the upstream null):
//   * `this->_resource->_active` after the first block, where `_resource` was
//     just written by the base call and is only null if `p` was.
//   * `sys` in BOTH arms. `IsDerivedFrom` returning false leaves it null and
//     retail reads `sys+0xf8` / `sys+0x48` anyway. In retail that arm is
//     unreachable because every caller hands a ParticleSystem; if our standalone
//     ever reaches it the fault is the diagnosis, not a bug to paper over.
//
// `?_typeInfo@ParticleSystem@Graphics@@0VBaseTypeInfo@Object@@A` (0x1801f9a58)
// is read for its FIRST DWORD only — the typeId.
//
// The 0x18-byte emitter record (`_emitter`) has no retail name (the Emitter
// banner below); B.4 names its fields as `RtGraphics::ParticleEmitter`
// (Particle.h), built through its own stores into the raw block.
// ============================================================================

namespace Graphics {

void ParticleSystemInstance::SetParticle(Object::ExternalPtr<Particle> particle) {
    ++g_psiSetParticle;

    // Retail CALLs 0x180104a00 directly, so this calls OUR body directly too --
    // a QUALIFIED member call on the base, which is exactly what "the base's own
    // body, not a virtual re-dispatch" means in C++.
    this->ParticleInstance::SetParticle(particle);

    const Particle* const res = _resource;
    if (res && res->_active == 0 && _emitter) {
        // The emitter release at retail 0x180020d60, UNNAMED (checked by VA):
        // RtGraphics_Emitter_DeleteDtor, by name. Reached only on the
        // inactive-resource path.
        RtGraphics_Emitter_DeleteDtor(_emitter);
    }

    // ?IsDerivedFrom@Base@Object@@QEBA_NI@Z (0x1800c0390), the member, against
    // the ParticleSystem type record's id (ENDGAME B.3.9 TI-REST), read fresh.
    // The derivation test is what licenses the downcast.
    const Particle* const asParticle = particle.Get();
    const ParticleSystem* const sys =
        asParticle->IsDerivedFrom(ParticleSystem::GetTypeInfo().typeId)
            ? static_cast<const ParticleSystem*>(asParticle) : nullptr;

    if (_resource->_active != 0) {
        _emitter = sys->_pool._data[_index];
        return;
    }

    // The emitter allocator at retail 0x1800205a0, UNNAMED:
    // RtGraphics_Emitter_OperatorNew, by name. The inactive-resource path only;
    // retail tolerates a null result (it stores null).
    // The raw block viewed once as the record the stores below build.
    RtGraphics::ParticleEmitter* const m =
        static_cast<RtGraphics::ParticleEmitter*>(RtGraphics_Emitter_OperatorNew());  // LEFT: class operator new returns void*; retail allocates raw and constructs by hand (not the tier-I shape)
    if (m) {
        m->_extra._data     = nullptr;                  // two zeroed qwords
        m->_extra._size     = 0;
        m->_extra._capacity = 0;
        m->_rate = sys->_emissionRate;                           // a dword move: the raw bits
        // The emitter initialiser at 0x180020e90, UNNAMED: the Array SetSize
        // rtcoll::RtColl_ArraySetSize_180020e90, by name.
        rtcoll::RtColl_ArraySetSize_180020e90(&m->_extra, sys->_subCount);
    }
    _emitter = m;
}

}  // namespace Graphics

// ============================================================================
// void Graphics::ParticleInstance::Draw(Video::Context&, const Camera&,
//         ExternalPtr<MaterialInstance>, float, int, int, int) const
// ?Draw@ParticleInstance@Graphics@@UEBAX…MHHH@Z   retail 0x180104230
// vtable slot 2 of BOTH particle instance tables.
//
// *** THE `Camera&` AND THE `ExternalPtr` ARE NEVER READ, and neither are the
// fifth and sixth int arguments. *** Only the float (alpha) and the SEVENTH
// argument (the instance count) are used. Verified by walking every load: R8 and
// R9 are overwritten before use, and the only stack-argument reads are
// `[RBP+0x70]` (alpha, = entry RSP+0x28) and `[RBP+0x88]` (the last int,
// = entry RSP+0x40).
//
// Retail, in three parts:
//
//  A. if (instances <= 1) {
//         pos = prev.translation + (cur.translation - prev.translation) * alpha
//         q   = prev.rotation.Lerp(cur.rotation, alpha)
//         m   = Transform{q,pos}.AsMatrix()
//         v4  = { 1, 1, 1, ((1-alpha)*_opacityPrev) + (alpha*_opacity) }
//         Video::Context::SetTransformScale(ctx, 0, m, v4)
//     }
//     The gate is `CMP EDI,1 / JG`, i.e. the single-instance case sets the
//     transform and the multi-instance case leaves it to the caller.
//
//  B. n = this->_boundsPtr->_liveCount
//     if (!ctxData->BeginInstancedIndexedVertices(4*n, 0x34, 6*n, instances))
//         return;
//
//  C. for (i = 0; i < n; ++i)        // n RE-READ from the pool each iteration
//         rec = pool._records._data[n - i - 1]           // BACKWARD
//         ... four vertices of 0x34 bytes each, memmove'd into the dynamic
//             vertex cursor and the cursor advanced by the stride ...
//         ... six 16-bit indices (b-2, b, b-1, b-2, b+1, b) with b starting at
//             2 and advanced by 4 AS A 16-BIT ADD ...
//     ctxData->EndInstancedIndexedVertices(0)
//
// THE 16-BIT WRAP IN PART C IS LOAD-BEARING. Retail's `ADD R14W,0x4` wraps at
// 65536, and every index store is a 16-bit `MOV word`. A port that kept the base
// in an `int` would emit different indices past 16384 particles. Reproduced with
// a `uint16_t`.
//
// THE BACKWARD WALK IS ALSO LOAD-BEARING and is NOT a decompiler artefact: the
// index is computed as `(count - i - 1) * 0x58` with `CDQE` after the
// subtraction, while the index buffer is filled FORWARD. So particle k is drawn
// with quad (count-1-k). Any reordering changes the draw order of overlapping
// translucent sprites, which is visible.
//
// The vertex record is 0x34 bytes — the same 0x34 passed to
// BeginInstancedIndexedVertices as the stride, which is how the two agree:
//   +0x00 3f  pos lerped         +0x0c 1i  zero
//   +0x10 3f  colour lerped      +0x1c 1f  alpha   = ((1-a)*prev) + (a*cur)
//   +0x20 2f  uv corner          +0x28 2f  corner offset * size
//   +0x30 1f  rotation radians = (((1-a)*prevDeg)*k + (a*curDeg)*k), k = pi/180
// ============================================================================
namespace {

// The two corner tables retail materialises on its own stack immediately before
// the loop, as 14 immediate stores. Transcribed as the immediates, in order.
// uv:      (0,0) (1,0) (1,1) (0,1)
// corner:  (-1,1) (1,1) (1,-1) (-1,-1)
const float kCornerUV[8]     = { 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f };
const float kCornerOffset[8] = { -1.0f, 1.0f, 1.0f, 1.0f, 1.0f, -1.0f, -1.0f, -1.0f };

// The 0x34-byte particle vertex (the stride handed to
// BeginInstancedIndexedVertices), named from the banner's layout.
struct ParticleVertex {
    Math::Vector3 pos;       // +0x00
    int32_t       zero;      // +0x0c
    Math::Vector3 color;     // +0x10
    float         alpha;     // +0x1c
    float         u, v;      // +0x20
    float         cx, cy;    // +0x28
    float         rot;       // +0x30  radians
};
static_assert(sizeof(ParticleVertex) == 0x34, "the particle vertex stride is 0x34");

}  // namespace

namespace Graphics {

void ParticleInstance::Draw(Video::Context& context, const Camera& camera,
                            ExternalPtrHandle material, float alpha,
                            int a5, int a6, int instances) const {
    (void)camera;        // never read by retail
    (void)material;      // never read by retail
    (void)a5; (void)a6;  // never read by retail
    ++g_piDraw;

    const float one = One();

    // ---- A: the single-instance transform --------------------------------
    // `?SetTransformScale@Context@Video@@QEAAXHAEBVMatrix@Math@@AEBVVector4@4@@Z`
    // (0x18001a400), a DEFINED member. The `instances <= 1` test is retail's.
    if (instances <= 1) {
        const Math::Transform& cur = _transform._current;
        const Math::Transform& prv = _transform._previous;

        Math::Transform blended;
        const float px = prv.translation.x, py = prv.translation.y, pz = prv.translation.z;
        blended.translation.x = (cur.translation.x - px) * alpha + px;
        blended.translation.y = (cur.translation.y - py) * alpha + py;
        blended.translation.z = (cur.translation.z - pz) * alpha + pz;

        // retail spills alpha and passes &it
        blended.rotation = prv.rotation.Lerp(cur.rotation, Math::Scalar(alpha));

        const Math::Matrix m = blended.AsMatrix();

        // Retail: XMM0 = alpha * _opacity; XMM1 = (1 - alpha) * _opacityPrev;
        // XMM1 = XMM1 + XMM0. The three 1.0f are separate immediate stores.
        const Math::Vector4 v4(one, one, one,
                               ((one - alpha) * _opacityPrev) + (alpha * _opacity));
        context.SetTransformScale(0, m, v4);
    }

    // ---- B: open the dynamic instanced stream -----------------------------
    // ContextData slot 18 (+0x90) = BeginInstancedIndexedVertices and slot 19
    // (+0x98) = EndInstancedIndexedVertices -- `Context::
    // BeginInstancedIndexedVertices` / `EndInstancedIndexedVertices`
    // (0x1800f2130 / 0x1800f2110) inlined by retail, called by name here.
    const RtGraphics::ParticleBoundsRecord* const pool = _boundsPtr;
    const int32_t n0 = pool->_liveCount;

    if (!context.BeginInstancedIndexedVertices(4 * n0, 0x34, 6 * n0, instances))
        return;

    // ---- C: four vertices + six indices per particle ----------------------
    const float oneMinus = one - alpha;      // retail: SUBSS XMM11,XMM10, once
    const float degToRad = DegToRad();
    uint16_t    base     = 2;                // retail: LEA R14D,[R15+2] with R15 = 0
    int32_t     count    = pool->_liveCount;
    for (int32_t i = 0; i < count; ++i) {
        // (count - i - 1), with the subtraction done in 32 bits and THEN
        // widened (CDQE). `count` is the value read at the bottom of the previous
        // iteration, exactly as retail keeps it in EAX.
        const int32_t slot = count - i - 1;
        const RtGraphics::ParticleRecord& rec =
            pool->_records._data[static_cast<int64_t>(slot)];

        // size  = ((1-a) * prevSize) + (a * curSize)
        const float size = (oneMinus * rec._sizePrev) + (alpha * rec._size);
        // alpha = ((1-a) * prevAlpha) + (a * curAlpha)
        const float fade = (oneMinus * rec._alphaPrev) + (alpha * rec._alpha);
        // rotation: each term is scaled by pi/180 BEFORE the add, which is how
        // retail emits it (MULSS XMM1,XMM12 on each side, then ADDSS).
        const float rot = ((oneMinus * rec._rotPrev) * degToRad)
                          + ((alpha * rec._rot) * degToRad);

        ParticleVertex vertex;
        vertex.pos.x   = (rec._pos.x - rec._posPrev.x) * alpha + rec._posPrev.x;
        vertex.color.x = (rec._color.x - rec._colorPrev.x) * alpha + rec._colorPrev.x;
        vertex.pos.y   = (rec._pos.y - rec._posPrev.y) * alpha + rec._posPrev.y;
        vertex.color.y = (rec._color.y - rec._colorPrev.y) * alpha + rec._colorPrev.y;
        vertex.pos.z   = (rec._pos.z - rec._posPrev.z) * alpha + rec._posPrev.z;
        vertex.color.z = (rec._color.z - rec._colorPrev.z) * alpha + rec._colorPrev.z;
        vertex.zero  = 0;
        vertex.alpha = fade;
        vertex.rot   = rot;

        const int32_t stride = context._dynamicVertexStride;   // +0x20e8
        for (int c = 0; c < 4; ++c) {
            vertex.u  = kCornerUV[c * 2 + 0];
            vertex.v  = kCornerUV[c * 2 + 1];
            vertex.cx = size * kCornerOffset[c * 2 + 0];
            vertex.cy = size * kCornerOffset[c * 2 + 1];
            // 0x18008a290 is the CRT's `memmove` (disassembled for this lane:
            // the full rep-movsb / 32-byte-unrolled / MOVNTI ladder, ERMS bit
            // tested at 0x1801fc65c). std::memmove is the same contract.
            uint8_t* const cursor = context._dynamicVertexCursor;   // +0x20e0
            std::memmove(cursor, &vertex, static_cast<size_t>(stride));
            // Retail re-reads the stride after the call and adds it to the cursor.
            context._dynamicVertexCursor = cursor + context._dynamicVertexStride;
        }

        // Six 16-bit indices, and every arithmetic step is 16-bit. The index
        // cursor is Context+0x20f0 (`_dynamicIndexCursor`).
        uint16_t* ip = context._dynamicIndexCursor;
        const uint16_t b = base;
        const uint16_t idx[6] = { static_cast<uint16_t>(b - 2), b,
                                  static_cast<uint16_t>(b - 1),
                                  static_cast<uint16_t>(b - 2),
                                  static_cast<uint16_t>(b + 1), b };
        for (int k = 0; k < 6; ++k) {
            *ip = idx[k];
            ++ip;
            context._dynamicIndexCursor = ip;
        }
        base = static_cast<uint16_t>(base + 4);   // retail: ADD R14W,4 — wraps

        ++g_piDrawQuads;
        count = pool->_liveCount;                 // retail re-reads it here
    }

    context.EndInstancedIndexedVertices(0);
}

}  // namespace Graphics

// ============================================================================
// The emitter's allocate / destroy / free trio   (cutover closure batch D1)
// ============================================================================
// *** ENDGAME B.3.5 block H2 CHECKED THE EXPORT TABLE: "Emitter" AND
// "ParticlePool" ARE NOT RETAIL CLASSES. *** `standalone/rt_export_addrs.inc`
// contains no `@Emitter@Graphics@@`, no `Pool@Graphics@@` and no mangled name at
// any of 0x1800205a0 / 0x180020600 / 0x180020d60 / 0x180061a80 / 0x180103bf0 —
// the only `Emitter` symbols in the whole DLL are `Audio::Scene`'s
// `AddEmitter` / `MoveEmitter` / `RemoveEmitter` / `GetEmitterCount` /
// `SetEmitterVolume`, which are a different subsystem. So these five bodies are
// UNEXPORTED per-TU helpers — a per-class `operator new`, a per-class
// `operator delete`, a scalar deleting destructor and two allocation
// wrappers — and `Emitter` / `ParticlePool` are THIS FILE'S OWN NAMES for them,
// not retail's. They stay file-local free functions: there is no class here to
// convert, and inventing `Graphics::Emitter` would put a name in
// `src/engine/` that the binary does not contain
// (reference_resolveexport_phantom_names.md's failure, one level up).
//
//   0x1800205a0  `operator new` for the 0x18-byte emitter record. The size AND
//                the alignment are immediates: `MOV EDX,0x18` then
//                `LEA R8D,[RDX-0x10]` — i.e. align 8, computed from the size.
//   0x180020600  the matching per-class `operator delete`: the `if (p)` test
//                plus the Enter / Free(_singleton, p) / Leave triple.
//   0x180020d60  `~X` then that `operator delete`, returning `this` — the
//                shape MSVC emits for a scalar deleting destructor whose flag
//                argument has been constant-folded away. The `~X` it calls,
//                0x180021490, is the parameterless J1 `~Array` body.
extern "C" void* RtGraphics_Emitter_OperatorNew(void) {
    return rtcoll::MemAlloc(0x18, 8);                  // 1800205d1 / 1800205d6
}
extern "C" void RtGraphics_Emitter_OperatorDelete(void* p) {
    if (!p) return;                                    // 180020600 TEST/JZ
    System::MemoryAllocator::MemFree(p);               // 0x1800c7ad0 re-tests null
}
extern "C" void* RtGraphics_Emitter_DeleteDtor(void* p) {
    rtcoll::RtColl_ArrayDtor_180021490(p);             // 180020d76  ~X (a J1 body)
    RtGraphics_Emitter_OperatorDelete(p);              // 180020d7e
    return p;                                          // 180020d83  MOV RAX,RBX
}

// ============================================================================
// CUTOVER CLOSURE BATCH D2 "helpers-high" (2026-09-12)
// ============================================================================

// 0x180061a80 -- the per-TU `operator new(0x40)` for a pool record. The whole
// body is `EnterCriticalSection(_singleton + 0x640);
// MemoryAllocator::Allocate(_singleton, 0x40, 8); LeaveCriticalSection`, i.e.
// ?MemAlloc@MemoryAllocator@System@@SAPEAX_KH@Z (0x1800c7a50) with the size and
// the alignment folded in. It takes NO arguments and returns the block; a null
// return is retail's own tolerated case (the caller checks), so there is no
// guard here.
extern "C" void* RtGraphics_ParticlePool_OperatorNew() {
    return rtcoll::MemAlloc(0x40, 8);
}

// 0x180103bf0 -- the pool record's constructor. RCX = the 0x40-byte block,
// RDX = the owning Graphics::Particle. Store list read from the disassembly:
//     `_bounds.min` three floats +1e8f, `_bounds.max` three floats -1e8f
//     `_records` qwords zero, `_liveCount` 0, `frameTag` -1, the two tick
//     counters 0
// then `SetSize(&_records, particle->_subCount)` through 0x180061bb0.
// +1e8f / -1e8f are exact floats (0x4cbebc20 / 0xccbebc20).
// B.4: the record's real C++ constructor; both callers placement-new it into
// the block they allocate. The member initialisers are retail's stores, in
// retail's order (`_records` by Array()'s three zero stores).
RtGraphics::ParticleBoundsRecord::ParticleBoundsRecord(const Graphics::Particle* particle)
    : _bounds{ { 1e8f, 1e8f, 1e8f }, { -1e8f, -1e8f, -1e8f } },
      _records(),          // +0x18 {_data, _size, _capacity} zero
      _liveCount(0),       // +0x28
      frameTag(-1),        // +0x2c
      _spawnAccum(0),      // +0x30
      _totalTicks(0) {     // +0x38
    // 0x180061bb0, `Collection::Array<T>::SetSize` for the record's 0x58-byte
    // live-particle slot: rtcoll::RtColl_ArraySetSize_180061bb0 (array.cpp).
    rtcoll::RtColl_ArraySetSize_180061bb0(&_records, particle->_subCount);
}

// The attendance line names THIS lane, so a run log carries a line only this
// build emits — the on-disk md5 says which exe is on disk, not which one ran.
// Attendance is printed SEPARATELY from any comparison so a reader cannot
// mistake "the body ran" for "the body was verified"
// (reference_uncalled_port_is_not_verified.md).
void RtGraphicsWave27GfxpartSummary() {
    printf("[GFXPART27] ParticleSer=%llu (records=%llu) PIDraw=%llu (quads=%llu) "
                "AddDrawItems=%llu (emitted=%llu) GetInstXform=%llu "
                "PISetParticle=%llu (rngDraws=%llu) PSISetParticle=%llu\n",
                (unsigned long long)g_particleSerialize,
                (unsigned long long)g_particleSerRecords,
                (unsigned long long)g_piDraw,
                (unsigned long long)g_piDrawQuads,
                (unsigned long long)g_piAddDrawItems,
                (unsigned long long)g_piAddDrawItemsEmit,
                (unsigned long long)g_piGetInstanceXform,
                (unsigned long long)g_piSetParticle,
                (unsigned long long)g_piSetParticleRand,
                (unsigned long long)g_psiSetParticle);
}
