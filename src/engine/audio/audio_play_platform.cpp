// ============================================================================
// src/engine/audio/audio_play_platform.cpp — batch 7 (Input/Audio leaves)
//   Audio::Device::PlayPlatform(...)                  retail 0x1800de7a0  ord 3424
// ============================================================================
//
//   ?PlayPlatform@Device@Audio@@AEAA_NHV?$ExternalPtr@VSoundBuffer@Audio@@@Object@@AEBVTime@System@@MMM@Z
//
// `AEAA` is PRIVATE, not unexported (reference_aeaa_is_private_not_unexported.md);
// checked against the retail export directory with pefile before writing this
// file, not assumed. It was Audio's last remaining unported BODY row: two
// earlier lanes (WAVE28_LANE_SLOTS.md §the callback table, WAVE29_LANE_AVBODY.md
// §6.1) sized it and declined it as "632 bytes, six indirect XAudio2 calls,
// effects that live in the driver" -- an honest judgement call, not a
// dependency block. Its only two unported dependencies
// (docs/history/runtime_port/closure/unported_deps.csv) are `FUN_180127560` (a plain
// CRT `powf`, see below) and `Audio::Stream::Play`, which THIS SAME BATCH ports
// in `audio_stream_play.cpp`.
//
// The ONE live caller in our tree is `audio_scene_update.cpp`'s
// `Audio::Scene::Update` port, which already resolves this export BY NAME into
// a local `PlayPlatformFn` typedef -- reproduced verbatim here so the two files
// agree on the ABI:
//     bool (*)(void* device, int32_t slot, void* bufferRef,
//              const int64_t* startTime, float vol, float pitch, float third)
//
// ---------------------------------------------------------------------------
// THE BODY, READ FROM THE DISASSEMBLY (not Ghidra's decompile -- it silently
// drops the 7th argument, `third`, the way
// reference_ghidra_c_output_vs_disassembly.md warns; the disassembly settles
// every register and every stack offset used below)
// ---------------------------------------------------------------------------
//   1800de7b4  MOV RCX,[this+0xa88]         ; IXAudio2* engine (already named
//                                            ; `kOffXAudio2` in
//                                            ; audio_platform_startup.cpp)
//   1800de7c4  JNZ live ; else return false  ; no engine, no voice, nothing to undo
//   1800de7ec  RDX = this + slot*0x20 + 0xa98            ; &VoiceCallback[slot]
//                                                         ; (the SAME record
//                                                         ; audio_voice_callback.cpp
//                                                         ; names -- cross-file
//                                                         ; agreement, not a
//                                                         ; second derivation)
//   1800de809  RSI = RDX + 8                              ; &record.voice
//   1800de817  R8  = bufferRef + 0x54                      ; pSourceFormat
//                                                          ; (SoundBuffer embeds
//                                                          ; a WAVEFORMATEX at
//                                                          ; +0x54 -- see below)
//   1800de827  CALL [engineVtbl+0x40](engine, &record.voice, pSourceFormat,
//                                     Flags=0, MaxFreqRatio=2.0f@0x1801616d8,
//                                     pCallback=&record, pSendList=0,
//                                     pEffectChain=0)      ; CreateSourceVoice
//   1800de82a  TEST EAX,EAX ; JZ live ; else return false  ; no cleanup needed,
//                                                          ; nothing was created
//
// `engineVtbl+0x40` = IXAudio2::CreateSourceVoice and `+0x50` =
// CreateMasteringVoice are ALREADY the two indices
// `audio_voice_callback.cpp` cites as the identification for
// `??0Device@Audio@@`'s use of the same `+0xa88` pointer -- so this body opens
// no new interface-identity question, it reuses one already measured.
//
// ---------------------------------------------------------------------------
// THE BYTE-POSITION / PLAYBEGIN CALCULATION (0x1800de835..0x1800de8dc)
// ---------------------------------------------------------------------------
// SoundBuffer embeds a WAVEFORMATEX at +0x54 (independently confirmed: it is
// the exact `pSourceFormat` passed to CreateSourceVoice above), so the fields
// below are read at the WAVEFORMATEX offsets +0x02/+0x04/+0x0c/+0x0e:
//   +0x20 (byte)   loop-forever flag       -> LoopCount 0xFFFFFFFF or 0
//   +0x21 (byte)   "streaming" flag        -> skip SubmitSourceBuffer, and
//                                             afterwards hand off to
//                                             Audio::Stream::Play
//   +0x24 (float)  loop duration, SECONDS
//   +0x38 (ptr)    pAudioData
//   +0x40 (dword)  AudioBytes
//   +0x48 (ptr)    a table of ascending byte-offset segment boundaries (uint32[])
//   +0x50 (dword)  that table's BYTE length (element count = length >> 2)
//   +0x54 (word)   WAVEFORMATEX::wFormatTag        (unused here)
//   +0x56 (word)   WAVEFORMATEX::nChannels
//   +0x58 (dword)  WAVEFORMATEX::nSamplesPerSec
//   +0x60 (word)   WAVEFORMATEX::nBlockAlign
//   +0x62 (word)   WAVEFORMATEX::wBitsPerSample
//
// Retail, instruction for instruction (`spt` = System::Timer::_secondsPerTick):
//   loopDurationTicks = (int64)((double)buf.loopDurationSeconds / spt)   ; CVTTSD2SI
//   remainderTicks    = *startTime % loopDurationTicks                  ; IDIV, signed
//   secondsInLoop     = (float)((double)remainderTicks * spt)           ; CVTPD2PS
//   bytesPerSecond    = (nChannels * wBitsPerSample * nSamplesPerSec) >> 3
//                       (three 32-bit IMULs then a LOGICAL SHR -- reproduced
//                        with unsigned 32-bit wraparound, not widened)
//   bytePos           = (int32)(secondsInLoop * (float)bytesPerSecond)  ; CVTTSS2SI
//
//   playBegin = 0
//   if (segmentCount > 0) {
//       for (idx = 0; ; ) {
//           if (segmentTable[idx] > (uint32)bytePos) {   ; JA, UNSIGNED compare
//               playBegin = nBlockAlign * idx;           ; idx BEFORE any
//                                                          ; increment this pass
//               break;
//           }
//           if (++idx >= segmentCount) break;            ; not found -> stays 0
//       }
//   }
//   if (isStreaming) playBegin = 0;                       ; CMOVNZ, unconditional
//                                                          ; override -- bytePos
//                                                          ; itself is NOT
//                                                          ; overridden and is
//                                                          ; still used below
//                                                          ; for Stream::Play
//
// `idx` at the break is the count of segments strictly BELOW the match --
// retail increments AFTER the comparison, so the value used is the
// pre-increment one. Reproduced with the same two-step loop rather than a
// tidier `std::upper_bound`, because the untidiness (using the index before
// vs after the increment) is exactly the kind of thing that is invisible until
// wrong.
//
// ---------------------------------------------------------------------------
// THE SUBMIT / VOLUME / PITCH / START SEQUENCE (0x1800de8dc..0x1800de9eb)
// ---------------------------------------------------------------------------
//   XAUDIO2_BUFFER     at a local 0x30-byte scratch (Flags=0x40 END_OF_STREAM,
//                       AudioBytes, pAudioData, PlayBegin, PlayLength=0,
//                       LoopBegin=0, LoopLength=0, LoopCount) -- standard field
//                       order and standard field sizes, confirmed against the
//                       exact byte ranges retail writes (RSP+0x50..+0x70).
//   XAUDIO2_BUFFER_WMA-shaped local (pDecodedPacketCumulativeBytes = the SAME
//                       segment-table pointer, PacketCount = segmentCount) --
//                       at RSP+0x40..+0x50, passed as the call's THIRD arg.
//
//   if (!isStreaming) {
//       hr = voiceVtbl[0xa8/8](voice, &buffer, &wma);       ; SubmitSourceBuffer
//       if (hr != 0) goto cleanup;
//   }
//   hr = voiceVtbl[0x60/8](voice, vol, 0);                  ; SetVolume
//   if (hr != 0) goto cleanup;
//   ratio = FUN_180127560(1.0594631f /* 2^(1/12), @0x1801616a0 */, pitch);
//   hr = voiceVtbl[0xd0/8](voice, ratio, 0);                ; SetFrequencyRatio
//   if (hr != 0) goto destroyAndFail;                        ; SKIPS Start entirely
//   hr = voiceVtbl[0x98/8](voice, 0, 0);                    ; Start
//   if (hr == 0) {
//       if (isStreaming)
//           Stream::Play(this + 0xa00, slot, bytePos);       ; 0x1800a0d10,
//                                                            ; THIS SAME BATCH
//       return true;
//   }
// cleanup / destroyAndFail:
//   voiceVtbl[0x90/8](voice);                               ; DestroyVoice
//   *pVoiceSlot = nullptr;
//   return false;
//
// The SetFrequencyRatio-failure branch (`JNZ 0x1800de9c5`) jumps PAST the
// shared `MOV RCX,[RSI]` reload straight into the DestroyVoice call, using the
// voice/vtable it already had in RCX/RAX from the SetFrequencyRatio call --
// same destination, no behavioural difference, reproduced here as one
// `goto`-free `if` because C++ has no reason to distinguish the two entry
// points into the same cleanup.
//
// `third` (the 7th argument) IS IN THE MANGLED SIGNATURE AND NEVER READ BY
// THIS BODY -- checked with a full listing, not merely "not seen so far": no
// instruction in the function touches its stack slot. The caller
// (`audio_scene_update.cpp`) already says as much ("the third PlayPlatform
// float ... NOT named, because this body only forwards it"); this file is
// the confirmation from the callee's side.
//
// `this+0xa00` is `Audio::Device`'s single embedded `Audio::Stream` --
// consistent with `audio_device.cpp`'s own `kChannelCount * kChannelStride
// <= kLockOffset (0xA50)` bound: 64*0x28 = 0xA00 exactly, so the channel table
// ends exactly where the Stream begins.
//
// ---------------------------------------------------------------------------
// FUN_180127560 IS A PLAIN CRT `powf`, AND IT IS OUT OF THIS BATCH'S SCOPE
// ---------------------------------------------------------------------------
// Decompiled to CHECK what it is, not to port it: it is the textbook
// edge-case-laden IEEE754 `powf(base, exponent)` (NaN/Inf/zero/sign special
// cases, a log2/exp2 polynomial core, `FUN_18012a940` as the shared
// error-code path) -- not a Banished-specific algorithm. It is not one of this
// batch's five worklist rows, so it stays unregistered and this call reaches
// it through `ResolveAddr`, which tries the port registry first and falls
// back to the DLL -- the same door `audio_stream.cpp`'s
// `ReleaseField30Buffer` opens onto 0x18000c4a0, and `audio_stream_play.cpp`
// (this batch) opens onto 0x18000c300, for the identical reason.
//
// 0x1801616a0 = 0x3f873c9e = 1.0594631433486938f = 2^(1/12), the semitone
// ratio -- so `pitch` is consumed as a SIGNED SEMITONE OFFSET and this call is
// `pow(2^(1/12), pitch)`, the standard equal-temperament pitch-to-frequency-
// -ratio conversion. Read as the bit pattern's VALUE, matching
// `reference_crt_coefficients_must_be_lifted_not_retyped.md`'s rule for
// literals pulled out of `.rdata` (this one is the CONSTANT ARGUMENT, not a
// tempering coefficient, so it is spelled as the float value rather than the
// hex bit pattern; both name the same IEEE754 bits).
//
// ---------------------------------------------------------------------------
// ORACLE -- NONE, and the reason is the same class the whole Audio cluster
// gives for anything that touches a live XAudio2 voice
// ---------------------------------------------------------------------------
// This function creates a REAL XAudio2 source voice, submits a REAL buffer,
// and calls REAL Start/SetVolume/SetFrequencyRatio on a REAL COM object -- i.e.
// it starts audible sound. A replay would either double-submit a buffer to a
// second voice (if allowed to create one) or corrupt the slot's single voice
// pointer. `audio_scene_update.cpp`'s own `Scene::Update` faces the identical
// class of side effect and settles for attendance counters over an oracle;
// this file does the same, for the same reason.
// ============================================================================

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "audio/Device.h"
#include "audio/SoundBuffer.h"
#include "audio/load_decode_batch.h"   // block SNDPAR: LoadDecode::Await
#include "system/Value.h"          // System::Time
#include "system/Timer.h"          // ENDGAME B.3.9 SS1: Timer::_secondsPerTick
#include "rt_libm.h"                 // RtCrt_Powf
#include "platform_audio.h"          // Platform::AudioVoice* (Phase D.4 A1)
#include "audio/audio_pcm.h"         // Audio::XwmaPcmPlayBegin (Phase D.4 SDL-4)

namespace {

// ENDGAME B.3.7 block B4: the `kOffXAudio2` / `kVoiceCallback*` /
// `kDeviceStreamOffset` and `kSb*` byte offsets MOVED into
// src/engine/audio/Device.h and SoundBuffer.h (B6-8). Phase D.4 A1: the
// WAVEFORMATEX inside `SoundBuffer::_format` (+0x54) is read as
// `Platform::AudioWaveFormat`, and the six XAudio2 2.7 calls are the
// `Platform::AudioVoice*` primitives (platform_audio.h; the COM calls live in
// src/platform/sdl/audio_sdl.cpp since SDL-4). The XAUDIO2_BUFFER /
// XAUDIO2_BUFFER_WMA pair this body built on its stack is built by
// `Platform::AudioVoiceSubmit` from the same values.

constexpr float    kMaxFrequencyRatio  = 2.0f;      // 0x1801616d8
constexpr float    kSemitoneRatio      = 1.0594631433486938f;  // 0x1801616a0, 2^(1/12)

uint64_t g_calls = 0;
uint64_t g_voicesCreated = 0;
uint64_t g_placed = 0;

struct PlayPlatformSummary {
    ~PlayPlatformSummary() {
        if (!g_calls) return;
        printf("[RtPort] Audio::Device::PlayPlatform ATTENDANCE: calls=%llu "
                    "voicesCreated=%llu placed=%llu (NO ORACLE: starts real "
                    "XAudio2 playback; see the banner)\n",
                    (unsigned long long)g_calls, (unsigned long long)g_voicesCreated,
                    (unsigned long long)g_placed);
        fflush(stdout);
    }
};
PlayPlatformSummary g_summary;

// The WAVEFORMATEX a SoundBuffer embeds at +0x54 (`Format()` 0x18000bcb0 is
// `LEA RAX,[RCX+0x54]`). The field is its raw serialized bytes and this is the
// one view of them.
const Platform::AudioWaveFormat* FormatOf(const Audio::SoundBuffer* buf) {
    return reinterpret_cast<const Platform::AudioWaveFormat*>(buf->_format);  // CAST-OK: byte_io
}

double SecondsPerTick() {
    // ENDGAME B.3.9 SS1 (B39_RULES D7): the class static now, read fresh.
    return ::System::Timer::_secondsPerTick;
}

// FUN_180127560 -- a plain CRT powf(base, exponent), ported in
// src/engine/crt/msvc_libm.cpp; called by name (B3R).
float PowF(float base, float exponent) {
    return RtCrt_Powf(base, exponent);
}

// Computes bytePos (used unconditionally, including for Stream::Play) and
// playBegin (used only when NOT streaming). See the banner for the exact
// instruction sequence this reproduces.
void ComputePlayPosition(const Audio::SoundBuffer* buf, int64_t nowTicks,
                         int32_t* outBytePos, uint32_t* outPlayBegin) {
    const double spt = SecondsPerTick();
    const float loopDurationSeconds = buf->_durationSeconds;
    const double loopDurationTicksD = static_cast<double>(loopDurationSeconds) / spt;
    const int64_t loopDurationTicks = static_cast<int64_t>(loopDurationTicksD);  // CVTTSD2SI

    const int64_t remainderTicks = nowTicks % loopDurationTicks;  // CQO; IDIV R8, no zero test (1800de871)
    const double secondsInLoopD = static_cast<double>(remainderTicks) * spt;
    const float secondsInLoop = static_cast<float>(secondsInLoopD);  // CVTPD2PS

    const Platform::AudioWaveFormat* const wf = FormatOf(buf);
    const uint32_t nChannels      = wf->channels;
    const uint32_t wBitsPerSample = wf->bitsPerSample;
    const uint32_t nSamplesPerSec = wf->samplesPerSec;
    uint32_t bitsPerSecondAllCh = nChannels * wBitsPerSample;    // 32-bit wraparound,
    bitsPerSecondAllCh = bitsPerSecondAllCh * nSamplesPerSec;    // reproduced as-is
    const uint32_t bytesPerSecond = bitsPerSecondAllCh >> 3;     // logical shift

    const float bytePosF = secondsInLoop * static_cast<float>(static_cast<int32_t>(bytesPerSecond));
    const int32_t bytePos = static_cast<int32_t>(bytePosF);  // CVTTSS2SI, truncate

    uint32_t playBegin = 0;
    const int64_t segByteLen = static_cast<int32_t>(buf->_wmaLength);
    const int64_t segCount = static_cast<uint64_t>(segByteLen) >> 2;  // matches the
                                                                       // sign-extend-then-
                                                                       // logical-shift retail does
    if (segCount > 0) {
        const uint32_t* segTable = buf->_wmaData;
        // Phase D.4 SDL-4: a buffer decoded at load has a PCM format now; retail's
        // nBlockAlign here is the xWMA packet size, which DecodeAtLoad recorded.
        const uint16_t nBlockAlign = buf->IsDecodedAtLoad() ? buf->_decodedXwma.packetBytes
                                                            : wf->blockAlign;
        for (int64_t idx = 0; ; ) {
            if (segTable[idx] > static_cast<uint32_t>(bytePos)) {  // JA, unsigned
                playBegin = static_cast<uint32_t>(nBlockAlign) * static_cast<uint32_t>(idx);
                break;
            }
            if (++idx >= segCount) break;  // not found: playBegin stays 0
        }
    }

    // Phase D.4 SDL-4 (decode at load, D4_RULES §3 Q5): retail hands PlayBegin and
    // the seek table to XAudio2, which decodes xWMA from the packet holding it. The
    // PCM is decoded already, so PlayBegin becomes the sample XAudio2 was MEASURED
    // to start at (audio_pcm.h; handins/XWMA.md A4.3).
    if (buf->IsDecodedAtLoad()) {
        const uint32_t frameBytes = 2u * nChannels;
        playBegin = Audio::XwmaPcmPlayBegin(
            playBegin, buf->_wmaData, static_cast<uint32_t>(segCount), nChannels,
            buf->_decodedXwma.frameLength,
            frameBytes ? static_cast<uint32_t>(buf->_length) / frameBytes : 0u);
    }

    *outBytePos = bytePos;
    *outPlayBegin = playBegin;
}

}  // namespace

// ============================================================================
// Audio::Device::PlayPlatform — retail 0x1800de7a0, ord 3424
// ============================================================================
namespace Audio {

bool Device::PlayPlatform(int32_t slot, Object::ExternalPtr<SoundBuffer> buffer,
                          const System::Time& startTime, float vol, float pitch,
                          float third) {
    (void)third;  // read by NOTHING in retail -- see the banner
    ++g_calls;

    const SoundBuffer* const bufferRef = buffer.Get();
    // Block SNDPAR (NOT RETAIL): the PCM, its length and its format below are the decode
    // at load's; a buffer whose load batch has not published them yet is finished here.
    // No dereference: a null buffer still faults where retail's does (FormatOf).
    LoadDecode::Await(bufferRef);

    const Platform::AudioEngine engine = _xaudio2;                        // 1800de7b4
    if (!engine) {                                                        // 1800de7c4
        return false;
    }

    PlatformChannel* record = &_platformChannels[slot];                   // 1800de7ec
    Platform::AudioVoice* voiceSlot = &record->voice;                     // 1800de809

    // ---- CreateSourceVoice (+0x40) -------------------------------------
    // `pCallback` is the record itself: `PlatformChannel` implements
    // Platform::AudioVoiceCallback, whose vtable IS the IXAudio2VoiceCallback
    // table (Device.h, platform_audio.h).
    int32_t hr = Platform::AudioVoiceCreate(
        engine, voiceSlot, FormatOf(bufferRef), kMaxFrequencyRatio, record);  // 1800de827
    if (hr != 0) {                                                        // 1800de82a
        return false;
    }
    ++g_voicesCreated;
    const Platform::AudioVoice voice = *voiceSlot;

    // ---- byte position / PlayBegin -------------------------------------
    int32_t bytePos = 0;
    uint32_t playBegin = 0;
    ComputePlayPosition(bufferRef, startTime._ticks, &bytePos, &playBegin);

    const bool loopForever = bufferRef->_loopForever != 0;
    const bool isStreaming = bufferRef->_streamed != 0;
    if (isStreaming) playBegin = 0;  // CMOVNZ, unconditional override

    // The XAUDIO2_BUFFER: Flags END_OF_STREAM, AudioBytes, pAudioData,
    // PlayBegin, PlayLength / LoopBegin / LoopLength 0, LoopCount; pContext
    // (uninitialised in retail, read by nothing) null. Retail's XAUDIO2_BUFFER_WMA
    // (the segment table and its entry count) is gone with SDL-4: the buffer is
    // PCM decoded at load and PlayBegin a sample of it (ComputePlayPosition).
    // LoopCount: retail computes it as `MOVZX EAX,byte [RCX+0x20] ; NEG AL ;
    // SBB EDX,EDX ; MOVZX EDX,DL` (1800de90f..1800de917), i.e. 0xFF =
    // XAUDIO2_LOOP_INFINITE (Platform::kAudioLoopInfinite) or 0. The port passed
    // 0xFFFFFFFF until known_port_issues row 1274 was fixed (XAudio2 takes
    // 0..XAUDIO2_MAX_LOOP_COUNT (254) or 255, nothing else).
    const uint32_t loopCount = loopForever ? Platform::kAudioLoopInfinite : 0u;

    auto cleanup = [&]() {
        Platform::AudioVoiceDestroy(voice);                                  // +0x90
        *voiceSlot = nullptr;
    };

    if (!isStreaming) {                                                      // 1800de933
        hr = Platform::AudioVoiceSubmit(voice, true,                         // 1800de948 +0xa8
                                        bufferRef->_data,   // SoundBuffer::_data is uint8_t* (BYTE)
                                        static_cast<uint32_t>(bufferRef->_length),
                                        playBegin, loopCount);
        if (hr != 0) {                                                       // 1800de950
            cleanup();
            return false;
        }
    }

    hr = Platform::AudioVoiceSetVolume(voice, vol);                          // 1800de964 +0x60
    if (hr != 0) {                                                           // 1800de967
        cleanup();
        return false;
    }

    const float ratio = PowF(kSemitoneRatio, pitch);                         // 1800de98a
    hr = Platform::AudioVoiceSetFrequencyRatio(voice, ratio);                // 1800de998 +0xd0
    if (hr != 0) {                                                           // 1800de9ae
        cleanup();  // SetFrequencyRatio failure skips Start entirely
        return false;
    }

    hr = Platform::AudioVoiceStart(voice);                                   // 1800de9b5 +0x98
    if (hr != 0) {                                                           // 1800de9bd
        cleanup();
        return false;
    }

    ++g_placed;
    if (isStreaming) {                                                       // 1800de9d5
        // `?Play@Stream@Audio@@QEAAXHH@Z` on the embedded Stream (+0xa00): a
        // direct member call now (the name lookup behind a function-local
        // cache that stood here is gone, B6-5 / B7-11).
        _stream.Play(slot, bytePos);                                         // 1800de9e4
    }

    return true;                                                             // 1800de9e9
}

}  // namespace Audio
