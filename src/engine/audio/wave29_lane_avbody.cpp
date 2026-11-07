// ============================================================================
// src/engine/audio/wave29_lane_avbody.cpp — WAVE 29 lane AVBODY
//
// Three Audio bodies, all exported, all name doors:
//   ?GetVolumeForPosition@Scene@Audio@@AEBAMAEBVVector3@Math@@MM0@Z  0x1800dfed0  ord 3027
//   ?GetAttenuatedVolume@Scene@Audio@@AEBAMAEBUAttenutedEffect@12@AEBVVector3@Math@@M@Z
//                                                                   0x1800dfde0  ord 2453
//   ?OnSoundEnded@Scene@Audio@@SAXPEAUAudioChannel@Device@2@PEAXI@Z  0x1800dffe0  ord 3283
//
// `?PlayPlatform@Device@Audio@@` is Audio's remaining BODY row and is NOT
// ported. There is no audio_play_platform.cpp -- do not go looking for one.
// It is decoded in full in docs/history/runtime_port/WAVE29_LANE_AVBODY.md §6.1, and
// declined there as a stated size-and-evidence judgement (632 bytes, six
// indirect XAudio2 calls, effects that live in the driver), NOT dressed up as a
// blocker. §6.2 sketches the fake-backend oracle that would make it verifiable
// and names an addressee for it.
//
// ============================================================================
// GetVolumeForPosition WAS NOT ON THE WORK LIST, AND FINDING IT IS THE POINT
// ============================================================================
// The census lists THREE unported Audio bodies. This file ports FOUR things,
// and the extra one is the helper `GetAttenuatedVolume` calls twice.
//
// I was one keystroke from writing "0x1800dfed0 is an unexported internal, so
// it is inlined here as a file-local static". That would have been WRONG, and
// wrong in the exact documented way: **AEBA means PRIVATE, not unexported, and
// this DLL exports its private members** (reference_aeaa_is_private_not_
// unexported.md, which records three silent address doors that survived review
// on precisely that confusion). Checked instead of assumed, with a pefile
// lookup of the VA against the export directory:
//
//   0x1800dfed0  EXPORTED  ord 3027  ?GetVolumeForPosition@Scene@Audio@@AEBAMAEBVVector3@Math@@MM0@Z
//   0x1800a0d10  EXPORTED  ord 3417  ?Play@Stream@Audio@@QEAAXHH@Z
//   0x1801fe728  EXPORTED  ord 4797  ?_secondsPerTick@Timer@System@@0NA
//
// So this lane's whole subject has **ZERO address doors**: every callee is a
// name door, and nothing here opens a new cutover door.
//
// And the mangled signature then CONFIRMS the decode from the writer's side
// rather than the reader's. `AEBA M` = private const member returning float;
// `AEBVVector3@Math@@` = const Vector3&; `M M` = two floats; `0` is MSVC's
// back-reference to argument type 1, i.e. a second const Vector3&. That is
// exactly (srcPos, maxDist, minDist, listenerPos) -> float, which is what the
// instruction stream says. Only a WRITER names a field
// (reference_only_a_writer_names_a_field.md) and the export directory is the
// closest thing to a writer available here.
//
// ============================================================================
// `AttenutedEffect` IS THE STRUCT Audio.h CALLS `EffectSlot`
// ============================================================================
// Retail's own name for it -- typo and all, "Attenuted" -- is readable in the
// export directory: `AEBUAttenutedEffect@12@`, where `12` back-references
// `Scene@Audio`. So the type is `Audio::Scene::AttenutedEffect`, and
// src/engine/audio/Audio.h's `EffectSlot` is an earlier wave's inferred name
// for the same struct. Five independent offsets agree, which is what licenses
// reusing the existing type rather than declaring a second one:
//
//   +0x00  Vector3 pos      <- helper reads [RDX+0], [RDX+4], [RDX+8]
//   +0x10  SoundEffect ref  <- `MOV RAX,[RDX+0x10]`
//   +0x28  baseVolume       <- `MULSS XMM6,[RBP+0x28]`
//   +0x30  positions._data   <- `MOV RBX,[RDX+0x30]`
//   +0x38  positions._size   <- `MOV EAX,[RDX+0x38]`
//
// The name is recorded here and NOT renamed in Audio.h: renaming a type six
// files depend on is not this lane's change to make. ADDRESSEE: the next lane
// that edits src/engine/audio/Audio.h should rename `EffectSlot` to
// `AttenutedEffect` and cite this paragraph.
//
// TWO NEW SoundEffect FIELDS, and they are READER-ONLY evidence. +0x20 and
// +0x24 are two floats this body loads as the falloff radii. No writer was
// found, so they are named for their ROLE in the arithmetic (maxDistanceBase,
// minDistance) and that naming is explicitly weaker than the offsets
// themselves. A later lane that finds the writer should correct them.
//
// ============================================================================
// THE MOVED-TERM TRAP, WHICH IS LIVE IN GetAttenuatedVolume
// ============================================================================
// A moved term in a float sum is algebraically identical and numerically
// different (reference_moved_term_hides_where_terms_cancel.md). Two terms in
// this body are hoisted in retail and MUST STAY HOISTED:
//
//   1. `maxDist = fRadius + effect->def->maxDistanceBase` is computed ONCE,
//      before the loop (`ADDSS XMM7,[RAX+0x20]` at 0x1800dfe13, outside the
//      loop that starts at 0x1800dfe60). Recomputing it per iteration is the
//      same value every time here -- but writing it as `Falloff(..., fRadius +
//      base, ...)` inside the call invites a later edit that reassociates it.
//
//   2. `* effect->baseVolume` is applied ONCE, AFTER the max-reduction
//      (`MULSS XMM6,[RBP+0x28]` at 0x1800dfea8, after the loop exits). This one
//      is NOT merely a rounding question: multiplying each candidate by
//      baseVolume inside the loop and then taking the max REORDERS THE RESULT
//      whenever baseVolume is negative, and changes the number of roundings
//      even when it is positive. max(a,b)*v != max(a*v,b*v) for v<0.
//
// NaN: the reduction is `COMISS XMM0,XMM6 / JBE skip`, and COMISS sets
// ZF=CF=PF=1 for an unordered compare, so JBE is TAKEN and a NaN candidate is
// DISCARDED. C's `if (cand > best)` is false for NaN, so the plain spelling is
// faithful; it is spelled that way deliberately, not by luck.
//
// The count<0 case: `TEST EAX,EAX / JNZ / JLE` skips the loop entirely and
// leaves best = 0.0f -- which is then STILL multiplied by baseVolume. An early
// `return 0.0f` would be wrong: 0.0f * baseVolume is not 0.0f when baseVolume
// is NaN or an infinity. The multiply is kept unconditional.
//
// NO TRANSCENDENTALS beyond sqrtf. The CRT-coefficient rule
// (reference_crt_coefficients_must_be_lifted_not_retyped.md) does not bite:
// IEEE-754 specifies sqrt as correctly rounded, so our libm and the DLL's agree
// bit-for-bit on every input. That is why this is the one transcendental the
// sweep can compare without RT_LIBM=dll.
//
// ONE CONSTANT, LIFTED not retyped: 0x18016169c = 0x3f800000 = 1.0f, read with
// read_memory rather than recognised. It is written below as the bit pattern's
// value with the pattern beside it.
//
// ============================================================================
// OnSoundEnded WRITES ONE BYTE OUTSIDE THE ARRAY IT INDEXES, IN RETAIL
// ============================================================================
// This is the finding of this file and it is NOT a port defect. Retail:
//
//   1800e0008: OR  R8D,0xffffffff      ; R8D = -1
//   ...                                 ; the 8-slot search runs, finds nothing
//   1800e0028: MOVSXD RAX,R8D          ; RAX = -1, SIGN-extended
//   1800e002b: OR  byte [RAX + R11*1 + 0x60],0x2
//
// so the not-found path ORs 2 into `channel + 0x60 - 1` = **channel + 0x5f**.
// src/engine/audio/Audio.h (derived in an earlier wave, from different
// functions) puts `platformChannel[8]` at +0x20..+0x5f and `channelFlags[8]` at
// +0x60..+0x67. So +0x5f is not padding: it is the MOST SIGNIFICANT BYTE of
// `platformChannel[7]`. On the not-found path retail sets bit 1 of the top byte
// of an interface pointer.
//
// Per CLAUDE.md this is TRANSCRIBED, not "fixed": a guard would skip the
// behaviour rather than restore it, and the write is what retail does. It is
// called out here because a reader of the C would otherwise assume an
// off-by-one in the PORT. The found path writes channelFlags[index] normally.
//
// WRITE WIDTH: `OR byte ptr`, a BYTE, on both paths -- and, CORRECTING WHAT
// THIS COMMENT FIRST SAID, the Camera::operator= width trap (kickoff §2.2) does
// NOT apply here. I asserted it did, built a control to prove it, and the
// control reported NOT CAUGHT. It was right to: `OR` with the constant 2
// touches only bit 1 of the LOWEST byte at ANY width, because the constant's
// upper bytes are zero, so the byte and dword spellings produce byte-identical
// memory. The width trap bites on a STORE -- the Video pair's 8-byte
// `MOV [RDX],R9` beside its 4-byte `INC dword [RCX-4]` -- not on a
// read-modify-write with a small constant. See Ctl_WrongFlagBit below.
//
// THE TAG CHECK. `(storedTag ^ tag) & 0xfffffe00` must be zero, i.e. bits 9..31
// must match while bits 0..8 are allowed to differ -- and bits 1..8 are exactly
// what `(tag>>1) & 0xff` used to pick the channel. So the high bits are a
// generation counter guarding against a stale handle, and bit 0 is not read by
// this body at all. Stated as what the arithmetic does; this body is a READER
// of the tag encoding and cannot name its fields on its own.
//
// ============================================================================
// VERIFICATION
// ============================================================================
// GetVolumeForPosition and GetAttenuatedVolume are PURE -- `this` is passed but
// NEVER READ by either (RCX is dead in the helper, and in the outer body it is
// only forwarded). So both verify by INPUT SWEEP against the shipped export,
// with no gameplay route and no caller
// (project_pure_functions_verify_by_sweep.md, the method that verified
// GetAlignedPosition after it reported calls=0 in a 16.9M-call run).
//
// OnSoundEnded is not pure, but it is fully synthesisable: it reads a Scene and
// a Channel table and writes one byte. The sweep builds both, runs ours and
// retail's on SEPARATE BUT IDENTICAL copies, and memcmps the ENTIRE channel
// table afterwards -- which is what makes the +0x5f write a COMPARED term
// rather than something the oracle would have to be told to look for.
// "No gameplay route" is not "no oracle" (reference_no_route_is_not_no_oracle.md).
//
// Every retail side goes through the raw DLL door, never ResolveExport: the
// latter consults the port registry first and would hand back OUR function once
// registration lands, making the oracle a self-check that agrees forever
// (reference_shadow_marker_is_not_an_oracle.md).
// ============================================================================

#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstring>

#include "rt_libm.h"
#include "audio/Audio.h"
#include "audio/SoundEffect.h"

namespace rtavbody29 {

using RtAudio::EffectSlot;      // retail's own name: Audio::Scene::AttenutedEffect
using RtAudio::EmitterInstance;
using RtAudio::Channel;
using RtAudio::Scene;

namespace {

// ---- attendance ------------------------------------------------------------
unsigned long long g_volForPosCalls  = 0;
unsigned long long g_volForPosFar    = 0;  // BRANCH: dist > maxDist -> 0.0f
unsigned long long g_volForPosNear   = 0;  // BRANCH: dist < minDist -> 1.0f
unsigned long long g_attenCalls      = 0;
unsigned long long g_attenSingle     = 0;  // BRANCH: count == 0, the no-emitter path
unsigned long long g_attenLoops      = 0;  // BRANCH: count > 0, iterations run
unsigned long long g_endedCalls      = 0;
unsigned long long g_endedFound      = 0;  // BRANCH: the sub-slot was found
unsigned long long g_endedNotFound   = 0;  // BRANCH: the +0x5f write fired
unsigned long long g_endedRejected   = 0;  // BRANCH: empty table or stale tag

// The falloff ceiling, LIFTED from .rdata at 0x18016169c and written as the bit
// pattern's value. Not "recognised as 1.0f" -- recognising an algorithm is what
// licenses you to stop reading the bytes.
inline float FalloffCeiling() {
    const uint32_t bits = 0x3f800000u;  // 1.0f
    float f;
    std::memcpy(&f, &bits, sizeof(f));
    return f;
}

// The first 12 bytes of a Math::Vector3, which is all either body reads.
}  // namespace
}  // namespace rtavbody29

// ############################################################################
// ENDGAME B.3.7 block B4: the three bodies are members of `Audio::Scene` now
// (src/engine/audio/Audio.h); the registered doors below delegate. The
// attendance counters stay in `rtavbody29`.
// ############################################################################
namespace Audio {

using namespace rtavbody29;

// ---------------------------------------------------------------------------
// ?GetVolumeForPosition@Scene@Audio@@AEBAMAEBVVector3@Math@@MM0@Z   0x1800dfed0
//
//   RCX = this (const Scene*), NEVER READ -- the body is pure.
//   RDX = const Vector3& source position
//   XMM2 = maxDistance, XMM3 = minDistance
//   [RSP+0x28 @entry] = const Vector3& listener position
// ---------------------------------------------------------------------------
float Scene::GetVolumeForPosition(const Vector3& srcPos, float maxDist, float minDist,
                                  const Vector3& listenerPos) const {
    ++g_volForPosCalls;
    const Vector3* const s = &srcPos;
    const Vector3* const l = &listenerPos;

    // SUBSS/MULSS/ADDSS in retail's order: (dy*dy + dx*dx) + dz*dz.
    // The ORDER IS TRANSCRIBED, not tidied into x+y+z: float addition is not
    // associative, and this is exactly the moved-term hazard. Retail computes
    // dy^2 first (0x1800dfedd loads [RDX+4] before [RDX+0]), adds dx^2 to it
    // (`ADDSS XMM0,XMM4` at 0x1800dff0d), then adds dz^2 (0x1800dff15).
    const float dy = s->y - l->y;
    const float dx = s->x - l->x;
    const float dz = s->z - l->z;
    const float d2 = (dy * dy + dx * dx) + dz * dz;

    const float dist = RtLibm::Sqrt(d2);        // CALL 0x180128110 (sqrtf)

    // COMISS XMM1,XMM7 / JBE -- so this is `dist > maxDist`, and an UNORDERED
    // compare (a NaN distance) takes the JBE and does NOT return 0 here.
    if (dist > maxDist) { ++g_volForPosFar; return 0.0f; }

    // COMISS XMM6,XMM1 / JA -- `minDist > dist` returns the ceiling untouched.
    const float ceiling = FalloffCeiling();
    if (minDist > dist) { ++g_volForPosNear; return ceiling; }

    // SUBSS/SUBSS/DIVSS/SUBSS, in retail's order and with retail's operands.
    return ceiling - ((dist - minDist) / (maxDist - minDist));
}

// ---------------------------------------------------------------------------
// ?GetAttenuatedVolume@Scene@Audio@@AEBAM...   0x1800dfde0
//
//   RCX = this, RDX = const AttenutedEffect&, R8 = const Vector3& listener,
//   XMM3 = radius bias (added to the effect's max-distance base)
// ---------------------------------------------------------------------------
float Scene::GetAttenuatedVolume(const AttenutedEffect& effect, const Vector3& listenerPos,
                                 float radiusBias) const {
    ++g_attenCalls;
    const AttenutedEffect* const eff = &effect;

    // FAITHFUL UNGUARDED DEREF of the effect's SoundEffect (+0x10). B.3.7: the
    // two floats are `SoundEffect::GetRadius` / `GetInnerRadius` (+0x20/+0x24).
    const SoundEffect* const def = eff->soundEffectRef.Get();

    const float f20 = def->_radius;               // maxDistanceBase
    const float f24 = def->_innerRadius;          // minDistance

    // BOTH HOISTED, exactly as retail hoists them. See the header.
    const float minDist = f24;                    // MOVSS XMM8,[RAX+0x24]
    const float maxDist = radiusBias + f20;       // ADDSS XMM7,[RAX+0x20]

    float best = 0.0f;                            // XORPS XMM6,XMM6

    const int32_t count = static_cast<int32_t>(eff->positions._size);
    if (count == 0) {
        // TEST EAX,EAX / JNZ not taken: the effect's OWN position is used.
        ++g_attenSingle;
        // The slot's own position (+0x00..+0x0b) read as a Vector3.
        best = GetVolumeForPosition(eff->pos, maxDist, minDist, listenerPos);
    } else if (count > 0) {
        // JLE after the JNZ: a NEGATIVE count skips the loop and leaves best=0.
        const EmitterInstance* e = eff->positions._data;
        for (int32_t i = 0; i < count; ++i, ++e) {
            ++g_attenLoops;
            // MULSS XMM0,[RDI] where RDI = element + 0xc -- the per-emitter
            // volume, applied to THIS candidate before the comparison.
            const float v =
                GetVolumeForPosition(e->position, maxDist, minDist, listenerPos) * e->volume;
            // COMISS/JBE: strictly greater, and a NaN candidate is discarded.
            if (v > best) best = v;
        }
    }

    // MULSS XMM6,[RBP+0x28] -- ONCE, AFTER the reduction, and UNCONDITIONAL.
    return best * eff->baseVolume;
}

// ---------------------------------------------------------------------------
// ?OnSoundEnded@Scene@Audio@@SAXPEAUAudioChannel@Device@2@PEAXI@Z  0x1800dffe0
//
//   STATIC (SA): no `this`. RCX = Device::AudioChannel*, RDX = void* userData
//   (the Scene, as stored into the device slot by PlayPlatform's caller),
//   R8D = the channel handle tag.
// ---------------------------------------------------------------------------
void Scene::OnSoundEnded(Device::AudioChannel* audioChannel, void* userData, uint32_t tag) {
    ++g_endedCalls;
    Scene* const scene = static_cast<Scene*>(userData);   // callback void* context: Audio::Device's channel-end callback hands back the userData the Scene registered (retail PEAX)  // CAST-OK: user_data

    // CMP dword [RDX+0x18],0 / JZ -- Scene::channels._size
    if (scene->channels._size == 0) { ++g_endedRejected; return; }

    // MOV EAX,R8D / SHR EAX,1 / MOVZX R11D,AL -- a BYTE, so the index is
    // (tag>>1)&0xff and nothing wider. IMUL by 0x70 = sizeof(Channel).
    const uint32_t index = static_cast<uint8_t>(tag >> 1);
    Channel* const ch = &scene->channels._data[index];

    // MOV EAX,[R11+0x68] / XOR EAX,R8D / TEST EAX,0xfffffe00 / JNZ
    const uint32_t storedTag = ch->handleTag;
    if (((storedTag ^ tag) & Scene::kChannelTagMask) != 0) { ++g_endedRejected; return; }

    // The 8-slot linear search over platformChannel[], +0x20 stride 8.
    // `CMP RCX,0x8 / JL` -- eight iterations, indices 0..7.
    int32_t found = -1;                                  // OR R8D,0xffffffff
    for (int32_t i = 0; i < 8; ++i) {
        if (ch->platformChannel[i] == audioChannel) { found = i; break; }  // JZ found
    }

    if (found >= 0) ++g_endedFound; else ++g_endedNotFound;

    // MOVSXD RAX,<found> / OR byte [RAX + R11 + 0x60],2
    //
    // *** found == -1 GIVES channel+0x5f, ONE BYTE BEFORE channelFlags[0], and
    // *** that is the MSB of platformChannel[7]. See the header. RETAIL DOES
    // *** THIS; it is transcribed, not guarded. The `int64_t` cast is the
    // *** MOVSXD -- an unsigned index here would address +0x60+0xFFFFFFFF.
    uint8_t* const flagByte = &ch->channelFlags[static_cast<int64_t>(found)];
    *flagByte = static_cast<uint8_t>(*flagByte | Bits(SampleFlags::Ended));   // OR byte, NOT dword
}
// ---- registration ----------------------------------------------------------
}  // namespace Audio

// ============================================================================
// THE DOORS — one-line qualified delegations.
// ============================================================================

namespace rtavbody29 {

// ---- attendance, printed UNCONDITIONALLY -----------------------------------
// See shader_program_names.cpp's matching note: Wave 28 defect #7 (every
// lane's *Summary() is written and nothing calls it) REPRODUCED on this lane's
// subject, so this goes through a static destructor -- the mechanism that
// actually produced Wave 28's one measured figure -- and prints even when every
// counter is zero, so "never ran" and "ran zero times" are distinguishable.
namespace {
struct AvbodyReport {
    ~AvbodyReport() {
        printf("[AVBODY29] Audio ATTENDANCE: "
               "GetVolumeForPosition=%llu (far=%llu near=%llu) ; "
               "GetAttenuatedVolume=%llu (single=%llu loopIters=%llu) ; "
               "OnSoundEnded=%llu (found=%llu notFound=%llu rejected=%llu)\n",
               g_volForPosCalls, g_volForPosFar, g_volForPosNear,
               g_attenCalls, g_attenSingle, g_attenLoops,
               g_endedCalls, g_endedFound, g_endedNotFound, g_endedRejected);
        fflush(stdout);
    }
};
AvbodyReport g_avbodyReport;
}  // namespace

}  // namespace rtavbody29
