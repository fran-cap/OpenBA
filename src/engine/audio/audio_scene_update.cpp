// ============================================================================
// src/engine/audio/audio_scene_update.cpp
//   WAVE 28 LANE MISC -- `?Update@Scene@Audio@@QEAA_NVTime@System@@0AEAVRandom
//                        @4@AEBVVector3@Math@@M@Z`, retail 0x1800df6c0, ord 4502
// ============================================================================
//
// The wave's one unported Audio BODY row, and the one Wave 8 held back and
// three waves since have re-confirmed as "out of budget, NOT newly blocked".
//
// EXTENT: 0x1800df6c0 .. 0x1800dfdd7 inclusive, `RET` then INT3 padding.
// 0x718 = 1816 bytes. MEASURED with objdump against
// C:\GOG Games\Banished\Runtime-x64.dll on disk, not from .pdata (which is only
// a LOWER bound, reference_pdata_is_a_lower_bound_on_function_extent.md) and
// not from Ghidra.
//
// ---------------------------------------------------------------------------
// THE STATED BLOCKER IS DEAD, AND THIS LANE KILLED IT WITH ITS OWN OTHER ROW
// ---------------------------------------------------------------------------
// audio.cpp's header holds Wave 8's reason for not porting this:
//
//     "...mixing in System::Random state ... a replay that draws twice from
//      System::Random diverges by construction. What it needs is a snapshot/
//      restore of the Random state as well, which is a real and bounded next
//      step rather than a missing tool."
//
// That snapshot/restore HAS EXISTED SINCE WAVE 2. `the diff oracle` in
// src/engine/system/value_types.cpp snapshots the live `Random`, lets ours
// mutate it, and replays the original on an independent scratch copy -- exactly
// the shape Wave 8 described as the missing step. `System::Random` is a flat
// 2500-byte POD, so the snapshot is a `memcpy`.
//
// AND the RNG is not even the obstacle it was described as. This body reaches
// `System::Random` through exactly TWO inlined draws, at 0x1800df992 and
// 0x1800dfa0c, and both call 0x1800c7dd0 -- `?NextGroup@Random@System@@`, which
// THIS LANE registered in the same commit (value_types.cpp). Every bit of the
// tempering is transcribed below from these bytes.
//
// WHAT ACTUALLY BLOCKS AN ORACLE is different and is stated plainly: this body
// mutates the Scene, the Device singleton AND the RNG, and it STARTS AND STOPS
// AUDIBLE SOUND through Device::PlayPlatform and the platform channel objects.
// A replay would play every sound twice. That is Wave 8.5's own finding about
// the teardown family, and unlike that family there is no synthetic subject
// here: the Device is not reachable-only-through-a-scene-field in this body,
// it is loaded from `?_singleton@Device@Audio@@` in the prologue
// (0x1800df6ff) and written to unconditionally.
//
// So: NO ORACLE, and the reason is the SIDE EFFECT, not the RNG. Correcting
// the record rather than the conclusion (kickoff warning 13) -- the conclusion
// "no oracle" was right; the licence offered for it was wrong, and the wrong
// licence is what kept this row closed for twenty waves.
//
// ---------------------------------------------------------------------------
// WHAT IS AND IS NOT VERIFIED  -- read this before quoting the attendance line
// ---------------------------------------------------------------------------
// There IS a live route: src/game/Game/gameupdateloop.cpp:1780 calls this
// export every frame of an ordinary in-game run. So the ATTENDANCE counter
// below is real evidence that the body RUNS. It is NOT evidence that the body
// is CORRECT -- nothing compares its output to retail's, and nothing can.
// See docs/history/runtime_port/WAVE28_LANE_MISC.md section 5 for what was run.
//
// ---------------------------------------------------------------------------
// SIGNATURE, AND WHY THE ARGUMENT NAMES HERE DISAGREE WITH THE CALLER'S
// ---------------------------------------------------------------------------
//     bool Scene::Update(Time a, Time b, Random& rng, const Vector3& listener,
//                        float m)
//
// The caller (gameupdateloop.cpp:163) names them `time` and `scaledTime`. The
// BYTES say what they are used for, and that is what the names here follow:
//
//     rdx (arg1) homed at [rbp+0xc8]  -- ADDED to a startTime, twice
//                                        (0x1800df805, 0x1800df8f5). A DELTA.
//     r8  (arg2)                      -- stored straight into `this->time`
//                                        at 0x1800df712. AN ABSOLUTE TIME.
//
// `Time` is passed by value in a register, so it is an int64 of ticks.
//
// Frame arithmetic used to map the homes, stated so it can be checked: entry
// rsp = E; seven pushes take it to E-0x38; `LEA RBP,[RSP-0x80]` gives
// rbp = E-0xb8.  So rbp+0xc0 = E+0x08 (home rcx = this), rbp+0xc8 = E+0x10
// (home rdx), rbp+0xd8 = E+0x20 (home r9 = &rng), rbp+0xe0 = E+0x28
// (stack arg 5, the Vector3&), rbp+0xe8 = E+0x30 (stack arg 6, the float).
// Every one of those four is confirmed by a use: 0x1800df70b, 0x1800df7fe,
// 0x1800df981, 0x1800df721, 0x1800df719.
//
// ---------------------------------------------------------------------------
// THE TWO TABLE STRIDES, TAKEN FROM THE LOOP BOUNDS AND NOT FROM THE HEADER
// ---------------------------------------------------------------------------
//   loop 1:  rdi += 0x40, while rdi < 0x40000     -> 4096 EffectSlot of 0x40
//   loop 2:  r13 += 0x70, while r13 < 0x7000      ->  256 Channel     of 0x70
// Both agree with src/engine/audio/Audio.h's existing static_asserts, which
// were derived independently by Wave 8 from different functions. Two derivations
// from different bodies, not one restated.
//
// ---------------------------------------------------------------------------
// NEW LAYOUT THIS BODY PINS, AND HOW WEAK THAT PIN IS
// ---------------------------------------------------------------------------
// Audio.h carries no `Device` and no `SoundEffect`. This body reads both, and
// everything below is read out of THIS ONE FUNCTION -- a single reader. That is
// the weakest kind of layout evidence there is
// (reference_a_writer_beats_readers_agreeing_on_a_sign.md), so none of it is
// promoted into Audio.h as a struct; it stays here as named byte offsets with
// the instruction that produced each one. A later lane that finds the WRITER
// should move it and correct it.
//
//   Audio::Device (the `?_singleton@Device@Audio@@` object)
//     +0x000  AudioSlot slots[0x40], stride 0x28   -- 0x1800dfadb `ADD RAX,0x28`
//                                                     0x1800dfadf `CMP R15,0x40`
//             slot layout, from the six stores at 0x1800dfb39..0x1800dfb58:
//               +0x00 uint32 flags   (bit 0 = in use)
//               +0x08 void*  soundBuffer
//               +0x10 void*  userData          (the Scene, `this`)
//               +0x18 uint32 tag               (the Channel's handleTag)
//               +0x20 void*  onEnded           (= ?OnSoundEnded@Scene@Audio@@,
//                                                0x1800dffe0, ord 3283)
//     +0xa88  void*  -- a non-null GATE on the platform-channel table. NOT
//             invented here: audio.cpp:1171 already records this exact guard as
//             `Device::StopPlatform`'s own, from a different body.
//     +0xaa0  void*  platformChannels[], stride 0x20 -- 0x1800dfbef `SHL RAX,5`
//
//   *** CORROBORATED, and the corroboration also EXPLAINS the +0xaa0. ***
//   WAVE 28 lane SLOTS, working on the unnamed table 0x180157c60 and knowing
//   nothing of this body, found that Audio::Device embeds 64 instances of an
//   internal `IXAudio2VoiceCallback` implementer at Device+0xa98, stride 0x20,
//   with record +0x08 = the source voice, +0x10 = AudioChannel*, +0x18 = the
//   event Stream::Run signals.  0xa98 + 0x08 = 0xaa0 EXACTLY, so what this body
//   loads is `record[idx].sourceVoice`, and the stride agrees.
//
//   That settles the virtual call below. The object is an IXAudio2SourceVoice,
//   and vtable offset 0x60 on IXAudio2Voice is the THIRTEENTH method --
//   GetVoiceDetails, SetOutputVoices, SetEffectChain, EnableEffect,
//   DisableEffect, GetEffectState, SetEffectParameters, GetEffectParameters,
//   SetFilterParameters, GetFilterParameters, SetOutputFilterParameters,
//   GetOutputFilterParameters, SetVolume -- i.e. 12 * 8 = 0x60 is
//   `SetVolume(float Volume, UINT32 OperationSet)`, and the second argument
//   this body passes is 0 = XAUDIO2_COMMIT_NOW. The call is named `SetVolumeFn`
//   below for that reason.
//
//   Recorded the way SLOTS recorded its own, and the distinction is deliberate:
//   the INTERFACE is identified; the internal class that owns the record is
//   UNIDENTIFIED and its name is not recoverable from the retail file. No name
//   has been invented for it here.
//
//   Audio::SoundEffect
//     +0x28  int64  total duration in ticks    -- 0x1800df7e0 (and audio.cpp:496
//                                                 already reads this offset)
//     +0x30  void*  sample table
//     +0x38  int32  sample count               -- 0x1800df91c `CMP [RDI+0x38],0`
//
//   Sample entry, stride 0x18 (0x1800dfc42 `ADD R9,0x18`)
//     +0x00  float  start time, in SECONDS     -- divided by _secondsPerTick
//     +0x08  void*  variant table
//     +0x10  int32  variant count
//
//   Variant entry, stride 0x18 (0x1800df9fe `LEA RBX,[RAX+RAX*2]` then *8)
//     +0x00  void*  ExternalPtr<SoundBuffer>
//     +0x08  float  pitch low
//     +0x0c  float  pitch high
//     +0x10  float  volume
//     +0x14  float  the third PlayPlatform float (pan/attenuation -- NOT named,
//                   because this body only forwards it)
//
// ---------------------------------------------------------------------------
// TWO CONSTANTS, LIFTED FROM .rdata AND NOT RECOGNISED
// ---------------------------------------------------------------------------
// Kickoff warning 4: recognising an algorithm is exactly what licenses you to
// stop reading the bytes, so do not. Both were read with objdump -s:
//     0x180161714 = 0x42c80000 = 100.0f      the config-volume divisor
//     0x18016165c = 0x2f800000 = 2^-32       the RNG-word-to-unit-float scale
// The second is `kRandUnit` in value_types.cpp, i.e. this body inlines
// `Random::RandFloat`. It is transcribed as the bit pattern's value, not as
// "1.0f/4294967296.0f", so a re-reader compares against the same thing.
//
// The MT19937 tempering masks in both inlined draws are 0xff3a58ad and
// 0xffffdf8c -- the SHIPPED masks, not the textbook 0x9d2c5680 / 0xefc60000.
// Read at 0x1800df9b9/0x1800df9c7 and again at 0x1800dfa30/0x1800dfa3c.
//
// ---------------------------------------------------------------------------
// EVERY CALLEE, AND ITS DOOR
// ---------------------------------------------------------------------------
// Checked against the export directory with pefile by RVA. Seven direct call
// targets; SIX are exported, so six are NAME doors:
//     0x1800dfde0  ?GetAttenuatedVolume@Scene@Audio@@  ord 2453  (private, AEBA
//                  -- private is not unexported)
//     0x1800df550  ?StopEffect@Scene@Audio@@           ord 4109  PORTED (W8.5)
//     0x1800df370  ?StopEffect3D@Scene@Audio@@         ord 4108  PORTED (W8.5)
//     0x1800def60  ?PlayEffect@Scene@Audio@@           ord 3423  PORTED (W8.5)
//     0x1800de7a0  ?PlayPlatform@Device@Audio@@        ord 3424
//     0x1800c7dd0  ?NextGroup@Random@System@@          ord 3245  PORTED (this
//                  lane, same commit)
// and ONE is not exported and is taken BY ADDRESS:
//     0x180050d30  Array<T16>::operator=  -- PORTED AND REGISTERED already
//                  (src/engine/collection/array.cpp, lane Q's J10;
//                  audio.cpp:1114 already reaches it the same way), so this
//                  opens NO new cutover door.
// Plus one virtual call, `(*(vtbl+0x60))(platformChannel, volume, 0)` at
// 0x1800dfc06, on a platform-channel object this body does not own.
//
// KILL SWITCH: RT_AUDIOSCENEUPDATE, DEFAULT ON (kickoff section 2.2 --
// a registration behind an opt-in switch moves neither the live count nor the
// cutover wall). Its OWN switch, separate from audio.cpp's, so this 1816-byte
// body can be A/B'd out in one variable without taking the other 16 Audio ports
// with it -- which is the bisection anyone chasing an audio regression will
// want first.
// ============================================================================

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "audio/Audio.h"
#include "audio/Device.h"
#include "audio/SoundBuffer.h"
#include "audio/SoundEffect.h"
#include "system/Value.h"          // System::Time, System::Random
#include "system/Timer.h"          // ENDGAME B.3.9 SS1: Timer::_secondsPerTick
#include "collection/Collection.h"   // Array<EmitterInstance>::operator=

namespace {

using namespace RtAudio;

// ---- attendance ------------------------------------------------------------
uint64_t g_updates        = 0;   // calls
uint64_t g_effectsSeen    = 0;   // live EffectSlots visited in loop 1
uint64_t g_effectStops    = 0;   // StopEffect on a silenced slot
uint64_t g_effectPlays    = 0;   // PlayEffect on a newly audible slot
uint64_t g_effectExpired  = 0;   // StopEffect3D on a slot past its duration
uint64_t g_channelsSeen   = 0;   // live Channels visited in loop 2
uint64_t g_samplesStarted = 0;   // PlayPlatform attempts
uint64_t g_samplesPlaced  = 0;   // PlayPlatform returned true
uint64_t g_deviceFull     = 0;   // no free device slot
uint64_t g_volumeUpdates  = 0;   // the vtbl+0x60 volume push
uint64_t g_channelsFreed  = 0;   // every sample ended -> channel reset

void AudioSceneUpdateReport() {
    printf("[RtPort] Audio::Scene::Update ATTENDANCE: calls=%llu effects=%llu "
           "(play=%llu stop=%llu expire=%llu) channels=%llu "
           "(startAttempt=%llu placed=%llu deviceFull=%llu volume=%llu freed=%llu)\n",
           (unsigned long long)g_updates, (unsigned long long)g_effectsSeen,
           (unsigned long long)g_effectPlays, (unsigned long long)g_effectStops,
           (unsigned long long)g_effectExpired, (unsigned long long)g_channelsSeen,
           (unsigned long long)g_samplesStarted, (unsigned long long)g_samplesPlaced,
           (unsigned long long)g_deviceFull, (unsigned long long)g_volumeUpdates,
           (unsigned long long)g_channelsFreed);
    printf("[RtPort] Audio::Scene::Update VERDICT: attendance is EXECUTION ONLY. "
           "This body has NO ORACLE (it starts and stops audible sound and "
           "mutates the Device singleton), so a non-zero count here means it "
           "RAN, never that it is CORRECT.\n");
    fflush(stdout);
}
struct UpdateReport { ~UpdateReport() { if (g_updates) AudioSceneUpdateReport(); } };
UpdateReport g_updateReport;

// ---- the constants, as their bit patterns ---------------------------------
// 0x180161714 and 0x18016165c, read with `objdump -s`.
inline float ConfigVolumeDivisor() {
    const uint32_t bits = 0x42c80000u;  // 100.0f
    float f; std::memcpy(&f, &bits, sizeof f); return f;
}
inline float RandUnitScale() {
    const uint32_t bits = 0x2f800000u;  // 2^-32
    float f; std::memcpy(&f, &bits, sizeof f); return f;
}

// ---- doors -----------------------------------------------------------------
// ENDGAME B.3.7 block B4 (B7-11 / B6-5): GetAttenuatedVolume, StopEffect,
// StopEffect3D, PlayEffect (Scene), PlayPlatform (Device) and
// Random::NextGroup were reached BY NAME behind function-local first-call
// caches; they are direct member calls now. 0x180050d30 (Array<T16>::operator=)
// is `Collection::Array<EmitterInstance>::operator=` (B9 RT, D4).

double SecondsPerTick() {
    // ?_secondsPerTick@Timer@System@@0NA (ord 4797) -- a DOUBLE, and retail
    // divides by it rather than multiplying by its reciprocal. Kept as a
    // division: the two are not the same float. ENDGAME B.3.9 SS1 (B39_RULES
    // D7): the class static now, read fresh.
    return ::System::Timer::_secondsPerTick;
}

// One MT19937 draw, exactly as retail inlines it at 0x1800df992 and 0x1800dfa0c
// -- the two sequences were diffed against each other and are identical. The
// index advance and the 0x270 wrap are part of the draw.
uint32_t RngDraw(System::Random& rng) {
    if (rng._index == 0) {                                     // 1800df992
        // `?NextGroup@Random@System@@AEAAXXZ` (0x1800c7dd0), the member.
        rng.NextGroup();                                       // 1800df99b
    }
    const int32_t index = rng._index;
    uint32_t y = rng._mt[static_cast<uint32_t>(index)];
    y ^= y >> 11;                                              // 1800df9b0
    y ^= (y & 0xff3a58adu) << 7;                               // 1800df9b9
    y ^= (y & 0xffffdf8cu) << 15;                              // 1800df9c7
    y ^= y >> 18;                                              // 1800df9d6
    int32_t next = index + 1;                                  // 1800df9dc
    if (next == 0x270) next = 0;                               // 1800df9df / CMOVE
    rng._index = next;
    return y;
}

}  // namespace

// ============================================================================
// ?Update@Scene@Audio@@QEAA_NVTime@System@@0AEAVRandom@4@AEBVVector3@Math@@M@Z
// ============================================================================
namespace Audio {

bool Scene::Update(System::Time delta, System::Time now, System::Random& rng,
                   const Vector3& listener, float m) {
    ++g_updates;
    // The two Times by their tick counts.
    const int64_t deltaTicks = delta._ticks;
    const int64_t nowTicks   = now._ticks;

    Device* device = Device::_singleton;       // 1800df6ff, BEFORE `this` is loaded
    this->time = nowTicks;                     // 1800df712  MOV [R15],R8

    // ------------------------------------------------------------------
    // LOOP 1 -- the 4096 EffectSlots. 0x1800df730 .. 0x1800df819.
    // ------------------------------------------------------------------
    {
        AttenutedEffect* const table = this->effects.GetData();
        // Row 1220: no test of the table (0x1800df730 MOV RBX,[R15+0x20]; ADD RBX,RDI;
        // CMP dword ptr [RBX+0x24],0). Scene's ctor builds the 0x1000-slot array.
        for (int32_t k = 0; k < 0x1000; ++k) {                // 1800df812  (0x40000 bytes)
            AttenutedEffect* const slot = &table[k];
            if (slot->idTag == 0) continue;
            const int64_t soundType = static_cast<int32_t>(slot->soundType);
            if (paused[soundType] != 0) continue;       // 1800df745
            ++g_effectsSeen;

            const float vol = GetAttenuatedVolume(*slot, listener, m);   // 1800df75d

            bool skipPlay = false;
            // `ucomiss` + JP + JNE: an unordered (NaN) compare takes the
            // NOT-equal path, which is what `!(vol == 0.0f)` gives.
            if (vol == 0.0f) {                                // 1800df765
                const uint32_t handle = slot->channelHandle;
                if (handle != 0) {
                    ++g_effectStops;
                    StopEffect(handle);                            // 1800df777
                    slot->channelHandle = 0;
                    skipPlay = true;                               // 1800df783 JMP
                }
            }
            if (!skipPlay) {
                // `comiss` + JBE: strictly greater, and NaN takes the JBE.
                if (vol > 0.0f && slot->channelHandle == 0) {     // 1800df785 / 1800df78b
                    ++g_effectPlays;
                    const uint32_t h = PlayEffect(
                        slot->soundEffectRef,
                        static_cast<SoundType>(soundType),
                        slot->startTime);
                    slot->channelHandle = h;                  // 1800df7a4
                }
            }

            // ---- push the attenuated volume onto the live Channel ----
            const uint32_t handle = slot->channelHandle;
            if (handle != 0) {                                // 1800df7ad
                const uint32_t gen = handle >> 9;             // 1800df7b3
                const uint32_t idx = (handle >> 1) & 0xffu;   // 1800df7b6 / MOVZX AL
                Channel* const ch = &channels.GetData()[idx];
                if (gen == (ch->handleTag >> 9)) {            // 1800df7cb
                    ch->volume = vol;                         // 1800df7cf
                }
            }

            // ---- expiry, and the time advance ------------------------
            if (slot->channelHandle == 0) {                   // 1800df7d6
                // FAITHFUL UNGUARDED DEREF. Retail loads the SoundEffect
                // through slot+0x10 with no null test, because a slot with
                // idTag != 0 always has one. Not guarded here -- a guard
                // would skip the expiry rather than restore it.
                const SoundEffect* const effect = slot->soundEffectRef.Get();
                const int64_t duration = effect->_maxLength;  // 1800df7e0
                if (duration != 0 && slot->startTime._ticks >= duration) {   // 1800df7eb JL
                    ++g_effectExpired;
                    StopEffect3D(slot->idTag);                // 1800df7f7
                    continue;                                 // 1800df7fc JMP, NO advance
                }
            }
            slot->startTime._ticks = slot->startTime._ticks + deltaTicks;   // 1800df805
        }
    }

    // ------------------------------------------------------------------
    // The per-SoundType volume table. 0x1800df81f .. 0x1800df877.
    // volumes[0] is ZERO -- it is `MOV DWORD PTR [RSP+0x70],0x0`, an integer
    // store, not a converted config field, and index 0 is SoundType 0.
    // ------------------------------------------------------------------
    float volumes[5];
    volumes[0] = 0.0f;                                           // 1800df826
    {
        const Configuration* const cfg = Configuration::_singleton;
        const float div = ConfigVolumeDivisor();
        if (cfg) {
            volumes[1] = static_cast<float>(cfg->uiVolume) / div;        // 1800df82e
            volumes[2] = static_cast<float>(cfg->musicVolume) / div;     // 1800df847
            volumes[3] = static_cast<float>(cfg->ambientVolume) / div;   // 1800df859
            volumes[4] = static_cast<float>(cfg->effectVolume) / div;    // 1800df86b
        } else {
            volumes[1] = volumes[2] = volumes[3] = volumes[4] = 0.0f;
#ifdef _DEBUG
            // Debug only: this runs on every audio frame while the configuration is null.
            std::fprintf(stderr, "[RtPort] Audio::Scene::Update: "
                                 "Audio::Configuration::_singleton is NULL -- every "
                                 "channel will be mixed at volume 0\n");
#endif
        }
    }

    // ------------------------------------------------------------------
    // LOOP 2 -- the 256 Channels. 0x1800df8a0 .. 0x1800dfda6.
    // ------------------------------------------------------------------
    {
        const double spt = SecondsPerTick();
        Channel* const chans = channels.GetData();
        if (!chans) return true;

        for (int32_t c = 0; c < 0x100; ++c) {                     // 1800dfd9f  (0x7000 bytes)
            Channel* const ch = &chans[c];
            if (ch->handleTag == 0) continue;
            const int64_t soundType = static_cast<int32_t>(ch->soundType);
            if (paused[soundType] != 0) continue;           // 1800df8b9
            ++g_channelsSeen;

            const int64_t startTime = ch->startTime._ticks;       // 1800df8d1
            int64_t windowLo = startTime;
            if (ch->started == 0) {
                // Retail writes `0.0 / _secondsPerTick` and truncates, rather
                // than storing a zero. Reproduced as the division because the
                // two differ when _secondsPerTick is 0 (0.0/0.0 is NaN, and
                // CVTTSD2SI of NaN is INT64_MIN, not 0).
                const double z = 0.0 / spt;                       // 1800df8e2
                windowLo = static_cast<int64_t>(z);               // 1800df8ea
                ch->started = 1;                                  // 1800df8ef
            }
            const int64_t windowHi = deltaTicks + startTime;      // 1800df8fc
            const SoundEffect* const effect = ch->soundEffectRef.Get();   // 1800df8ff

            // SoundType 0 does nothing but advance -- `TEST ECX,ECX / JE` at
            // 1800df904 lands directly on the time store.
            if (soundType == 0) { ch->startTime._ticks = windowHi; continue; }

            int32_t endedCount = 0;                               // [rsp+0x40]
            int32_t i          = 0;                               // [rsp+0x44]
            const int32_t sampleCount = effect->_samples._size;   // 1800df91c

            if (sampleCount > 0) {
                // The loop bound is RE-READ from effect+0x38 every iteration in
                // retail (1800dfc53 `CMP R10D,[RDI+0x38]`), not cached. Kept as
                // a re-read: caching it is the kind of tidy that is invisible
                // until something in the loop can change it.
                for (i = 0; i < effect->_samples._size; ++i) {
                    uint8_t* flag = &ch->channelFlags[i];
                    Device::AudioChannel** pchan = &ch->platformChannel[i];

                    // `samples` is re-read from effect+0x30 on EVERY iteration
                    // in retail (1800df950 and again at 1800df9f7). Kept.
                    const SoundEffect::Sample* const sample = &effect->_samples._data[i];

                    const double sec = static_cast<double>(sample->startSeconds);
                    const int64_t at = static_cast<int64_t>(sec / spt);   // 1800df965

                    if (at >= windowLo && at < windowHi) {
                        // ---- this sample starts in this frame's window ----
                        const int32_t variantCount = sample->variants._size;   // 1800df97c
                        uint32_t pick = 0;
                        if (variantCount != 0) {                          // 1800df988
                            pick = RngDraw(rng) % static_cast<uint32_t>(variantCount);
                        }
                        const SoundEffect::Variant* const v = &sample->variants._data[pick];

                        // SECOND DRAW -- the pitch lerp. Two draws per started
                        // sample, and the ORDER matters: variant first, pitch
                        // second (1800df99b then 1800dfa15).
                        const float t = static_cast<float>(RngDraw(rng)) * RandUnitScale();
                        const float lo = v->pitchLow, hi = v->pitchHigh;
                        const float pitch = (hi - lo) * t + lo;           // 1800dfa7e/82

                        const float vVol   = v->volume;
                        const float vThird = v->unused14;
                        SoundBuffer* const bufferRef = v->buffer.Get();

                        ch->sampleVolume = vVol;                          // 1800dfa9b
                        const uint32_t tag = ch->handleTag;
                        float mix = vVol * ch->volume;                    // 1800dfab0
                        mix *= volumes[soundType];                        // 1800dfab7

                        // ---- find a free Device slot, stride 0x28, 0x40 of them
                        ++g_samplesStarted;
                        Device::AudioChannel* placed = nullptr;
                        int32_t freeSlot = -1;
                        if (device) {
                            int32_t k = 0;
                            for (; k < 0x40; ++k) {                       // 1800dfadf
                                if ((device->_channels[k].flags & 1) == 0) break;   // 1800dfad0
                            }
                            if (k < 0x40) freeSlot = k;
                        }
                        if (freeSlot >= 0) {
                            if (device->PlayPlatform(
                                    freeSlot,
                                    Object::ExternalPtr<SoundBuffer>(bufferRef),
                                    ch->startTime,
                                    mix, pitch, vThird)) {                // 1800dfb17
                                ++g_samplesPlaced;
                                Device::AudioChannel* const s = &device->_channels[freeSlot];
                                s->flags |= 1u;                           // 1800dfb39
                                s->source = bufferRef;                    // 1800dfb3e
                                s->onComplete = &Scene::OnSoundEnded;     // 1800dfb4a
                                s->userData = this;                       // 1800dfb4f
                                s->userValue = tag;                       // 1800dfb58
                                placed = s;                               // 1800dfb64
                            }
                            // PlayPlatform false -> placed stays null (1800dfb27)
                        } else {
                            ++g_deviceFull;                               // 1800dfaec
                        }
                        *pchan = placed;                                  // 1800dfb6d
                        *flag |= 1;                                       // 1800dfb75
                    } else {
                        // ---- outside the window: keep a running channel's
                        //      volume in step with the mix. 1800dfb90.
                        const uint8_t fl = *flag;
                        if ((fl & 1) && !(fl & 2) && *pchan != nullptr) {
                            float mix = ch->sampleVolume * ch->volume;    // 1800dfbab/b2
                            mix *= volumes[soundType];                    // 1800dfbbe
                            if (device) {
                                // (platformChannel - device) / 0x28 -- retail's
                                // signed magic division, 1800dfbc7..1800dfbdf.
                                const int64_t idx = *pchan - device->_channels;
                                // 1800dfbe2..1800dfc06 is Device::SetVolumePlatform
                                // (0x1800deab0) INLINED: the platform gate, the
                                // voice test, SetVolume(mix, 0) at +0x60. Phase D.4
                                // A1 calls it. The counter now counts the pushes
                                // asked for, not only those that met a live voice.
                                ++g_volumeUpdates;
                                device->SetVolumePlatform(static_cast<int32_t>(idx), mix);
                            }
                        }
                    }

                    if (*flag & 2) ++endedCount;                          // 1800dfc24
                }
            }

            // ---- all samples reported ended -> recycle the channel --------
            // Also a RE-READ of effect+0x38, not the cached `sampleCount`
            // (1800dfc68 `CMP R11D,[RDI+0x38]`). It matters at the boundary the
            // cached form hides: when sampleCount is 0 the inner loop never ran,
            // endedCount is 0, and 0 == 0 -- so a SoundEffect with no samples
            // recycles its channel on the first Update. That is retail's
            // behaviour and it is reached through the `jle 1800dfc68` at
            // 1800df920, which skips the inner loop entirely.
            if (endedCount == effect->_samples._size) {                   // 1800dfc68
                ++g_channelsFreed;

                // Find the EffectSlot whose channelHandle equals this
                // channel's handleTag and clear it. NOTE, and it is retail's:
                // the scan compares Channel::handleTag (+0x68) against
                // EffectSlot::channelHandle (+0x20), NOT against idTag.
                AttenutedEffect* const table = this->effects.GetData();
                const uint32_t tag = ch->handleTag;
                // Row 1220: no test of the table (0x1800dfc7a MOV RAX,[R8+0x20]; ADD RAX,0x20;
                // CMP EDX,[RAX]). Scene's ctor builds the 0x1000-slot array; nothing frees it
                // before ~Scene.
                for (int64_t k = 0; k < 0x1000; ++k) {                // 1800dfc8d
                    AttenutedEffect* const slot = &table[k];
                    if (slot->channelHandle != tag) continue;         // 1800dfc82
                    // A zeroed EffectSlot, field by field as retail stores
                    // it. THE EXTENT IS 0x2c, NOT 0x30: retail's last store
                    // is `MOV DWORD PTR [RCX+0x28],0x3f800000`, so
                    // `_pad2c` is NEVER WRITTEN.
                    slot->pos.x = 0.0f;                               // 1800dfcdc..1800dfcf9
                    slot->pos.y = 0.0f;
                    slot->pos.z = 0.0f;
                    slot->soundType = 0;
                    slot->soundEffectRef = nullptr;
                    slot->startTime._ticks = 0;
                    slot->channelHandle = 0;
                    slot->idTag = 0;
                    slot->baseVolume = 1.0f;                          // 1800dfd01
                    // and the nested Array<EmitterInstance> gets an EMPTY
                    // array ASSIGNED to it -- which releases its storage --
                    // rather than being memset away.
                    // The empty source is retail's stack EffectSlot's
                    // `positions` (0x1800dfcc8 / 0x1800dfccc, two zero
                    // stores); its life ends right after the call, where
                    // retail stores the two zeros again (0x1800dfd19 /
                    // 0x1800dfd1d) with the free elided.
                    // B9 RT2: rtcoll::ArrayAssignCore is header-inline
                    // (Collection.h), so `&empty` no longer escapes into an
                    // out-of-line call. Measured -O2: `empty`'s ~Array test
                    // and MemFree call after the assign are GONE (GCC drops
                    // the local whole: no stack stores, where retail keeps
                    // its two dead zero stores). The assign itself is now
                    // inline here; retail CALLs it (0x1800dfd10 -> 0x180050d30).
                    {
                        const Collection::Array<EmitterInstance> empty;
                        slot->positions = empty;                       // 1800dfd10
                    }
                    break;
                }

                // A default-constructed Channel copied over this one. Retail
                // builds it on the stack and stores it field by field; the
                // fields that are NOT zero are volume and sampleVolume, both 1.0f.
                // Two of retail's loads are 8 bytes wide over 4-byte stack
                // stores (`MOV DWORD PTR [RBP-0x18],R9D` then `MOV RAX,[RBP-0x18]`
                // at 1800dfd28 / 1800dfd4b, and the same at 1800dfd3a /
                // 1800dfd8f), so retail writes FOUR BYTES OF UNINITIALISED STACK
                // into Channel+0x0c and +0x6c..+0x6f. audio.cpp:856 already
                // records this read-8-write-4 quirk in this class. We write
                // zeros there: the fields are `_pad0c` and the tail padding,
                // nothing reads them, and uninitialised stack is not
                // reproducible.
                ch->soundEffectRef = nullptr;                             // 1800dfd46..1800dfd93
                ch->soundType = 0;
                ch->_pad0c = 0;
                ch->startTime._ticks = 0;
                ch->volume = 1.0f;
                ch->sampleVolume = 1.0f;
                for (int b = 0; b < 8; ++b) ch->platformChannel[b] = nullptr;
                for (int b = 0; b < 8; ++b) ch->channelFlags[b] = 0;
                ch->handleTag = 0;
                ch->started = 0;
                ch->_pad6d[0] = ch->_pad6d[1] = ch->_pad6d[2] = 0;
            }

            ch->startTime._ticks = windowHi;                              // 1800dfd98
        }
    }

    return true;                                                          // 1800dfdac MOV AL,1
}

}  // namespace Audio
