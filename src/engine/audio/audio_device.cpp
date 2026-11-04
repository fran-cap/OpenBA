// ============================================================================
// src/engine/audio/audio_device.cpp — Wave 9 lane IOA
//   Audio::Device::Update  +  Audio::Device::FreeChannelPlatform
// ============================================================================
//
//   ?Update@Device@Audio@@QEAAXAEBVTime@System@@@Z   retail 0x1800DE520  ord 4481
//   ?FreeChannelPlatform@Device@Audio@@AEAAXH@Z      retail 0x1800DE6F0  ord 2380
//                                                    (AEAA = PRIVATE member)
//
// Both names were checked against the shipped DLL's export directory with
// pefile BEFORE this file was written; both are present at the addresses above.
// Separate file from audio.cpp because these two needed <windows.h> for the
// critical section (PLAN.md §4.1 decision 1's platform seam). Phase D.4 A1:
// the lock, the event and the voice calls are `Platform::` primitives now
// (platform_sync.h, platform_audio.h), so this file includes no OS header.
//
// ---------------------------------------------------------------------------
// WHAT THE STATED BLOCKER SAID, AND WHAT SURVIVES OF IT
// ---------------------------------------------------------------------------
// audio.cpp's own header and WAVE8_5.md §9 hold these two back like this:
//
//     "?Update@Device@Audio@@… 0x1800de520 — Device internals (platform
//      channel completion callbacks). Here the device IS the subject, not an
//      unreachable callee, so no synthetic Scene helps."
//     "?FreeChannelPlatform@Device@Audio@@…ord2380 — PRIVATE member, COM
//      vtable call at (**channel)+0x90, critical section at Device+0xa50.
//      Same class as Device::Update, and for the same reason."
//
// Every fact in those two paragraphs is TRUE and this lane confirms all of
// them. What they establish, though, is that these two functions have NO
// ORACLE — not that they cannot be ported. Wave 8.5 was answering "can I
// verify this by replay?"; a cutover row asks "is this symbol a null pointer
// when the DLL is gone?", and lane Z's triage rule for that question is
// whether every callee is satisfied on our side. Here they are:
//
//   Update            -> FreeChannelPlatform (this file), plus an indirect
//                        `CALL RAX` through a callback pointer stored in the
//                        channel record. No Runtime callee at all.
//   FreeChannelPlatform -> EnterCriticalSection / LeaveCriticalSection
//                        (KERNEL32 imports of the DLL, linked directly here),
//                        plus one virtual call through slot +0x90 of the
//                        platform voice object. No Runtime callee at all.
//
// Neither body calls a single unexported Runtime function by address. The
// hold-back was an ORACLE verdict being read as a PORT verdict; the oracle
// verdict is preserved below, in full, and this file adds no oracle it cannot
// justify.
//
// ---------------------------------------------------------------------------
// ORACLE — NONE, and the reason is the same stored callback
// ---------------------------------------------------------------------------
// `Update` reaps every finished channel and FIRES ITS STORED COMPLETION
// CALLBACK. Running the DLL's copy afterwards would find nothing left to reap
// (our body already cleared the flags) and, on the first pass, would have
// fired every callback a second time. `FreeChannelPlatform` releases a COM
// voice through a vtable slot; calling it twice releases a released object.
// A replay here is a SECOND SIDE EFFECT, not a comparison, and this file does
// not manufacture one.
//
// What it does instead is `the diff oracle` on both sites, because `calls=`
// counts calls ENTERED and both of these are entered constantly while doing
// nothing:
//   * `Update` runs every frame and reaps zero channels in almost all of them.
//     Its work verdict is "at least one channel had the reap bit set".
//   * `FreeChannelPlatform` returns at its first instruction whenever the
//     platform is not up (`Device+0xA88 == 0`), which is the whole of a
//     no-audio-device run. Its work verdict is "the platform was up".
// A run whose work column is zero has NOT verified these bodies, and the
// counter is what makes that visible instead of leaving `calls=` to imply it.
//
// ---------------------------------------------------------------------------
// LAYOUT — every offset below is CROSS-CONFIRMED by a THIRD function
// ---------------------------------------------------------------------------
// The channel stride and base are not inferred from `Update`'s loop alone.
// `Audio::Device::Stop` (0x1800DE200, ord 4105 — already reached by name from
// audio.cpp's ReleasePlatformChannels) takes an `AudioChannel*` and converts
// it to an index with `SUB RDX,RCX` followed by the signed-division idiom
// `MOV RAX,0x6666666666666667 ; IMUL RDX ; SAR RDX,4` — division by 0x28.
// That independently pins BOTH that the array starts at Device+0x00 AND that
// its stride is 0x28. `Stop` then runs the identical five-instruction reap
// epilogue at 0x1800DE23F..0x1800DE268 that `Update` runs at
// 0x1800DE560..0x1800DE57B, which is what the callback's three arguments are
// read from.
//
// The platform-side offsets are cross-confirmed by `Device::StopPlatform`
// (0x1800DEA20), the sibling `Stop` calls just before `FreeChannelPlatform`:
// it opens with the same `CMP qword ptr [RCX+0xA88],0` gate, indexes the same
// `[this + idx*0x20 + 0xAA0]` voice pointer, and calls slot +0xA0 on it where
// `FreeChannelPlatform` calls slot +0x90.
// ============================================================================

// ENDGAME B.3 step 7, block B4: the anonymous-namespace `AudioChannel` and the
// `kLockOffset` / `kPlatformReadyOffset` / `kVoice*` / `kStop*` constants that
// lived here MOVED into src/engine/audio/Device.h under the retail names
// (B6-8): `Audio::Device::AudioChannel` (+0x000, stride 0x28), the embedded
// `_stream` (+0xa00, whose `_lock` is the +0xa50 CRITICAL_SECTION, `_wakeEvent`
// the +0xa40 HANDLE, `_stopping` the +0xa80 byte and `_runFlags` the +0xa08
// gate), `_xaudio2` (+0xa88) and `Audio::Device::PlatformChannel` (+0xa98,
// stride 0x20: `voice` = the +0xaa0 slot, `channel` = the +0xaa8
// back-pointer). The four bodies below are the members; the registered doors
// are one-line qualified delegations.

#include <cstddef>
#include <cstdint>
#include "audio/Device.h"
#include "platform_audio.h"  // Platform::AudioVoiceDestroy / AudioVoiceStop / AudioVoiceSetVolume
#include "platform_sync.h"   // Platform::MutexLock / MutexUnlock on the Stream lock, EventSet
#include "audio/SoundBuffer.h"

// The two XAudio2 source-voice calls these bodies make: +0x90 is
// IXAudio2Voice::DestroyVoice (FreeChannelPlatform, Platform::AudioVoiceDestroy),
// +0xa0 is IXAudio2SourceVoice::Stop (StopPlatform, Platform::AudioVoiceStop) —
// deliberately different slots on the SAME object.
namespace {

// Retail tests byte +0x21 of the channel's SoundBuffer (IsStreamed) to decide
// whether this playback needs the Stream's lock.
inline bool NeedsStreamLock(const Audio::SoundBuffer* src) {
    return src != nullptr && src->_streamed != 0;
}
}  // namespace

namespace Audio {

// ---------------------------------------------------------------------------
// Audio::Device::FreeChannelPlatform(int) — retail 0x1800DE6F0, ord 2380.
// PRIVATE (`AEAA`), which is why nothing on our side calls it by name today
// except engine_funcs.cpp's own resolve (engine_funcs.cpp:1562) — and that
// resolve is precisely the cutover row this closes.
//
// Retail:
//   1800de6f6  CMP qword ptr [RCX+0xa88],0 ; JZ -> return
//   1800de70a  RSI = this + idx*0x28       ; the channel
//   1800de71f  RDI = idx*0x20              ; the voice-array scale
//   1800de723  RAX = [RSI+8]               ; channel->source
//   1800de727  if (RAX && RAX[0x21]) EnterCriticalSection(this+0xa50)
//   1800de73f  RCX = [this + idx*0x20 + 0xaa0]      ; the platform voice
//   1800de74a  if (RCX) { CALL [[RCX]+0x90](RCX); [slot] = 0; }
//   1800de761  RAX = [RSI+8]               ; RE-READ, not the cached value
//   1800de772  if (RAX && RAX[0x21]) tail LeaveCriticalSection(this+0xa50)
//
// The RE-READ at 0x1800DE761 is the one detail worth spelling out: retail does
// NOT reuse the `locked` verdict it computed on entry. It loads
// `channel->source` a second time and re-tests byte +0x21 of it, so a virtual
// call that changed either of those changes whether the lock is released.
// That is an unbalanced-lock hazard in the shipped DLL, not a transcription
// error here, and it is reproduced exactly. (Ghidra's decompiled C for this
// function folds the two reads into one — the standing warning in
// reference_ghidra_c_output_vs_disassembly.md; the disassembly above is what
// this body was written from.)
// ---------------------------------------------------------------------------
// Not analysed (clang -Wthread-safety): retail takes and releases the Stream lock on two separate
// reads of the same test, which the analysis cannot pair.
void Device::FreeChannelPlatform(int32_t index) NO_THREAD_SAFETY_ANALYSIS {
    if (_xaudio2 == nullptr)                          // 1800de6f6  platform down
        return;

    AudioChannel* ch = &_channels[index];             // MOVSXD: signed index
    Platform::LockStorage* lock = &_stream._lock;
    PlatformChannel& pc = _platformChannels[index];

    // Entry test, on the value read NOW.
    if (NeedsStreamLock(ch->source)) Platform::MutexLock(lock);     // EnterCriticalSection

    if (Platform::AudioVoice voice = pc.voice) {
        Platform::AudioVoiceDestroy(voice);           // +0x90 DestroyVoice
        pc.voice = nullptr;
    }

    // Exit test, on a FRESH read — see the note above. Deliberately not the
    // cached verdict from the entry test.
    if (NeedsStreamLock(ch->source)) Platform::MutexUnlock(lock);   // LeaveCriticalSection
}

// ---------------------------------------------------------------------------
// Audio::Device::Update(Time const&) — retail 0x1800DE520, ord 4481.
//
// The `Time const&` argument is NEVER READ. RDX is loaded on entry by the
// calling convention and the body never touches it again — verified against
// the disassembly, not inferred from the decompiler. The parameter is kept in
// the signature because the mangled name has it and callers pass it.
//
// Retail:
//   for (i = 0; i < 0x40; ++i) {
//       ch = this + i*0x28;
//       if ((ch->flags & 2) == 0) continue;
//       FreeChannelPlatform(this, i);           ; 1800de55b
//       cb = ch->onComplete;                    ; 1800de560, BEFORE the clears
//       ch->flags &= ~3;                        ; 1800de563
//       ch->source = 0;                         ; 1800de567
//       if (cb) cb(ch, ch->userData, ch->userValue);   ; 1800de57b, CALL RAX
//   }
//
// Two orderings matter and are reproduced: `onComplete` is read BEFORE the
// flag/source clears, and the callback's 2nd and 3rd arguments are read AFTER
// them. The `&= ~3` clears bits 0 AND 1 even though only bit 1 was tested.
// The same five instructions appear verbatim in `Device::Stop`
// (0x1800DE23F..0x1800DE268), which is the cross-check for all of it.
// ---------------------------------------------------------------------------
void Device::Update(const System::Time& /*time — never read*/) {
    for (int i = 0; i < 64; ++i) {                    // `CMP EDI,0x40 ; JL`
        AudioChannel* ch = &_channels[i];
        // Retail tests the LOW BYTE of the dword (`TEST byte ptr [..],0x2`)
        // and clears the whole dword; both are reproduced.
        if ((ch->LoadFlags() & 2u) == 0) continue;

        FreeChannelPlatform(i);

        Callback cb = ch->onComplete;
        ch->AndFlags(0xFFFFFFFCu);
        ch->source = nullptr;
        if (cb)
            cb(ch, ch->userData, ch->userValue);
    }
}

// ---------------------------------------------------------------------------
// Audio::Device::StopPlatform(int) - retail 0x1800DEA20.
//
// WAVE 23 LANE TAIL. This body is NOT one of the lane's 18 rows. It is here
// because of the census trap named in
// reference_porting_can_grow_the_cutover_surface.md: `Device::Stop` (the row
// that IS on the board) calls this function, and reaching it with
// `RT_EXPORT_ONCE("?StopPlatform@Device@Audio@@AEAAXH@Z")` would have ADDED a
// new called-but-unported row in the same namespace as the one it closed -
// Audio's gap would have read the same before and after. Porting the callee
// too is what makes closing `Stop` an actual closure.
//
// It IS exported, under a PRIVATE (`AEAA`) mangling - this DLL exports its
// private members, so "private" is not "unexported"
// (reference_aeaa_is_private_not_unexported.md). Verified with pefile against
// the shipped export directory, not inferred: 0x1800DEA20 maps to
// `?StopPlatform@Device@Audio@@AEAAXH@Z`.
//
// Every layout constant it needs was ALREADY derived and cross-confirmed in
// this file's banner by Wave 9 lane IOA, which read this exact function as its
// third witness for the platform-side offsets.
//
// Retail, instruction for instruction:
//   1800dea24  CMP qword ptr [RCX+0xa88],0 ; JZ -> return   (platform down)
//   1800dea3d  RDX = [this + idx*0x28 + 8]        ; channel->source
//   1800dea46  RBX = this + idx*0x20              ; the voice-array cursor
//   1800dea4e  if (source && source[0x21] && (this[0xa08] & 1)) {
//   1800dea5d      this[0xa80] = 1;
//   1800dea6b      SetEvent([this + 0xa40]);
//              }
//   1800dea71  RCX = [this + idx*0x20 + 0xaa0]    ; the platform voice
//   1800dea85  if (RCX) CALL [[RCX]+0xa0](RCX, 0, 0)
//   1800dea8b  RAX = [this + idx*0x20 + 0xaa8]    ; the voice's channel back-ptr
//   1800dea97  *(dword*)RAX |= 2                  ; set the "reap me" bit
//
// THREE DETAILS THAT ARE FAITHFUL, NOT SLIPS:
//
//  * The slot called on the voice is +0xA0, NOT the +0x90 that
//    `FreeChannelPlatform` calls on the SAME object. That difference is the
//    whole reason lane IOA used this body as its cross-check, and it is
//    preserved. It is also why `Stop` calls BOTH: +0xA0 stops the voice,
//    +0x90 releases it.
//  * The final `OR dword ptr [RAX],0x2` is UNGUARDED - retail does not test
//    RAX for null. Left unguarded (CLAUDE.md "Don't 'fix' a faithful crash
//    with a null check"): retail relies on the back-pointer always being
//    there once the platform is up, and a guard would SKIP the reap rather
//    than restore it. Note it sits OUTSIDE the `if (voice)`, so it runs on
//    every call that gets past the platform-down gate.
//  * The lock here is NOT the critical section `FreeChannelPlatform` takes.
//    This body signals an EVENT (`SetEvent` on the handle at Device+0xA40)
//    and sets the byte at Device+0xA80 first. Both are reproduced in order.
// ---------------------------------------------------------------------------
void Device::StopPlatform(int32_t index) {
    if (_xaudio2 == nullptr)                          // 1800dea24  platform down
        return;

    AudioChannel* ch = &_channels[index];
    PlatformChannel& pc = _platformChannels[index];
    Audio::Stream* stream = &_stream;

    // The wake: only when the source wants the stream lock AND the stream
    // worker is mid-pass (Thread runFlags bit 0, Device+0xa08). NOT RETAIL (review
    // F-137): the bit test and the `_stopping` store are atomic (Stream::Play).
    if (NeedsStreamLock(ch->source) && stream->IsWorking()) {
        __atomic_store_n(&stream->_stopping, uint8_t{1}, __ATOMIC_RELAXED);   // Device+0xa80
        Platform::EventSet(stream->_wakeEvent);       // Device+0xa40  SetEvent
    }

    if (Platform::AudioVoice voice = pc.voice) {
        Platform::AudioVoiceStop(voice);              // +0xa0 Stop(0, 0)
    }

    // Unguarded on purpose - see the banner above.
    pc.channel->OrFlags(2u);
}

// ---------------------------------------------------------------------------
// Audio::Device::SetVolumePlatform(int, float) - retail 0x1800DEAB0 (AEAA).
// Phase D.4 A1. Retail, read from the disassembly (Ghidra, 2026-10-10):
//   1800deab0  CMP qword ptr [RCX+0xa88],0 ; JZ -> RET   (platform down)
//   1800deaba  RCX = [this + (int64)idx*0x20 + 0xaa0]    ; the platform voice
//   1800deac9  TEST RCX,RCX ; JZ -> RET
//   1800dead1  XMM1 = XMM2 (volume) ; R8D = 0 ; JMP [[RCX]+0x60]   SetVolume
// Scene::Update (audio_scene_update.cpp) inlines exactly this; it calls it now.
// ---------------------------------------------------------------------------
void Device::SetVolumePlatform(int32_t index, float volume) {
    if (_xaudio2 == nullptr)                          // 1800deab0  platform down
        return;
    if (Platform::AudioVoice voice = _platformChannels[index].voice)   // 1800deac1
        Platform::AudioVoiceSetVolume(voice, volume); // 1800dead7  +0x60, tail jump
}

// ---------------------------------------------------------------------------
// Audio::Device::Stop(AudioChannel*) - retail 0x1800DE200, ord 4105.
// WAVE 23 LANE TAIL - one of the lane's rows.
//
// Already reached BY NAME from src/engine/audio/audio.cpp:901
// (`ReleasePlatformChannels`), which is why it sat in the census's `called`
// column unported. Registering it flips that call site onto this body.
//
// Retail:
//   1800de20a  index = ((char*)channel - (char*)this) / 0x28   ; signed, via
//              the 0x6666666666666667 / SAR 4 magic-division idiom
//   1800de230  StopPlatform(this, index);
//   1800de23a  FreeChannelPlatform(this, index);
//   1800de23f  ---- the reap epilogue, five instructions ----
//   1800de246  cb = channel->onComplete;            ; read BEFORE the clears
//   1800de24b  channel->flags &= ~3;
//   1800de253  channel->source = 0;
//   1800de268  if (cb) cb(channel, channel->userData, channel->userValue);
//
// THE EPILOGUE IS BYTE-FOR-BYTE THE ONE `Device::Update` RUNS at
// 0x1800DE560..0x1800DE57B. Lane IOA wrote that sequence out in this file's
// banner as its cross-check for `Update`; this body is the other half of that
// pair, and the two are deliberately written to read identically below. The
// orderings that matter are the same two: `onComplete` is read BEFORE the
// clears, and the callback's 2nd and 3rd arguments AFTER them. `&= ~3` clears
// bits 0 and 1 although `Stop` - unlike `Update` - never tested either.
//
// THE DIVISION. Retail does the whole thing in 64-bit signed arithmetic and
// only then truncates to 32 bits for the two calls (`MOV EDX,EBX`), and
// re-sign-extends that truncated value for the epilogue (`MOVSXD RAX,EBX`).
// So a pointer difference that did not fit in an int would be truncated ONCE
// and the same truncated index used everywhere. Reproduced by doing the
// divide in int64_t and narrowing to `int` exactly once. The magic sequence
// (`SAR 4` then add the sign bit) truncates toward zero, which is also what
// C++ integer division does, so `/ kChannelStride` is the faithful spelling
// and not merely an approximation of it.
//
// NO ORACLE, for the reason this file's banner already gives for its two
// siblings and which applies here verbatim: the body ends in a STORED
// COMPLETION CALLBACK and a COM voice stop/release. Replaying the DLL's copy
// afterwards would fire the callback a second time and release a released
// object - a second side effect, not a comparison. `the diff oracle` carries the
// distinction `calls=` cannot: there is no early-out here, so the work verdict
// is `true` and the number to read is whether the callback fired.
// ---------------------------------------------------------------------------
void Device::Stop(AudioChannel* channelPtr) {
    // no early-out; see the banner
    // The channel's index in `_channels`: retail's pointer difference, which
    // MSVC spells as the signed magic division by 0x28.
    const int index = static_cast<int>(channelPtr - _channels);

    StopPlatform(index);
    FreeChannelPlatform(index);

    // ---- the reap epilogue, identical to Device::Update's ------------------
    AudioChannel* ch = &_channels[index];
    Callback cb = ch->onComplete;
    ch->AndFlags(0xFFFFFFFCu);
    ch->source = nullptr;
    if (cb)
        cb(ch, ch->userData, ch->userValue);
}

}  // namespace Audio

// ============================================================================
// THE DOORS — one-line qualified delegations.
// ============================================================================

// ?_singleton@Device@Audio@@0PEAV12@EA (0x1801fe760) -- the class static (ENDGAME B.3.9 ENG, B39_RULES D7);
// RtAudioPortInit redirects the minted storage row onto it.
::Audio::Device* ::Audio::Device::_singleton = nullptr;
