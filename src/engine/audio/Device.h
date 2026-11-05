#pragma once
// ============================================================================
// src/engine/audio/Device.h — Audio::Device, 0x1298 bytes, NON-polymorphic
// ============================================================================
//
// ENDGAME B.3 step 7, block B4. Audio::Device had NO header: its layout was
// spread over file-local `constexpr` offsets and two private copies of
// `AudioChannel` (audio_device.cpp, batchc_audio_leaves.cpp), a third record
// in audio_voice_callback.cpp and raw offsets in audio_play_platform.cpp,
// audio_platform_startup.cpp, audio_scene_update.cpp and
// audio_stream_run.cpp. They MOVE here under the retail names (B6-8).
//
//   sizeof 0x1298  `Engine::Startup` allocates it with `MemAlloc(0x1298)` at
//                  0x1800dbdd4 and inlines nothing of it (B37_RULES 2.3).
//   polymorphic?   NO. `??0Device@Audio@@QEAA@XZ` (0x1800de3a0) opens with the
//                  AudioChannel loop at +0; there is no `??_7Device@Audio@@`.
//
// THE LAYOUT, from the constructor (singleton_ctors.cpp transcribes it):
//   +0x000  AudioChannel     _channels[64]    stride 0x28 (loop 1)
//   +0xa00  Audio::Stream    _stream          by VALUE (`CALL ??0Stream`)
//   +0xa88  Platform::AudioEngine  _xaudio2   PlatformStartup 0x1800de60a
//   +0xa90  Platform::AudioMaster  _masteringVoice   0x1800de63a
//   +0xa98  PlatformChannel  _platformChannels[64]    stride 0x20 (loops 2, 3)
//
// THE TWO NESTED NAMES ARE RETAIL'S, read from the mangled signatures:
//   `?GetChannel@Device@Audio@@QEAAPEAUAudioChannel@12@H@Z` (0x18000c710) is
//       `LEA RAX,[RCX + i*0x28]` — so the 0x28 record is
//       `Audio::Device::AudioChannel`;
//   `?GetPlatformChannel@Device@Audio@@QEAAPEAUPlatformChannel@12@H@Z`
//       (0x18000c720) is `SHL RAX,5 ; ADD RAX,0xa98 ; ADD RAX,RCX` — so the
//       0x20 record whose first qword is the UNEXPORTED `IXAudio2VoiceCallback`
//       table 0x180157c60 is `Audio::Device::PlatformChannel`. B37_RULES
//       section 1 calls that class "no name in the binary"; the class name of
//       the TABLE's owner is this nested struct (a finding of block B4).
//       ENDGAME B.3.9 ENG: it is a REAL polymorphic class now -- its seven
//       virtuals are the IXAudio2VoiceCallback slots (bodies in
//       audio_voice_callback.cpp), the Device ctor door placement-news all
//       64 records, and the minted unnamed row 0x180157c60 is retired.
//
// HOLDS THE POLYMORPHIC STREAM BY VALUE: `_stream` is the `Audio::Stream`
// class (B.4 R-ENG; it was the `RtAudio::Stream` POD). The Device still has NO
// real constructor — `Ctor_AudioDevice` (src/engine/engine/singleton_ctors.cpp)
// builds the Stream in place — and `~Device` is declared and undefined, so no
// Stream ctor or dtor runs that did not run before.
//
// `_singleton` (`?_singleton@Device@Audio@@0PEAV12@EA`, 0x1801fe760) is the class
// static below; its minted row is RETIRED (ENDGAME B.3.9 ENG W2).
//
// THREADS (RULE B7-7): main (Play/Stop/Update/OnSoundBufferDeleted), the
// Stream worker (reads `_channels[i].source` and the PlatformChannel voice
// under the Stream lock at +0xa50, writes `_platformChannels[i].bufferEvent`),
// and the XAudio2 callback thread (PlatformChannel slot 2 ORs
// `AudioChannel::flags` with 2, WITHOUT a lock — retail's design, no lock added).
//
// PORTABILITY: no <windows.h>, no <xaudio2.h>. Phase D.4 A1: the engine, the
// master and the voices are `platform_audio.h`'s opaque handles (retail's were the
// XAudio 2.7 interface pointers; SDL-4: src/platform/sdl/audio_sdl.cpp's objects), and
// PlatformChannel implements `Platform::AudioVoiceCallback`.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include <type_traits>

#include "Stream.h"
#include "../object/External.h"   // Object::ExternalPtr
#include "platform_audio.h"       // Platform::AudioEngine / AudioMaster / AudioVoice / AudioVoiceCallback
#include "platform_sync.h"        // Platform::EventHandle

namespace System { class Time; }

namespace Audio {

class SoundBuffer;
class Stream;

class Device {
public:
    // ------------------------------------------------------------------------
    // Audio::Device::AudioChannel — 0x28 bytes, 64 at +0x000. Pinned by three
    // functions independently: Update's loop, Stop's pointer-to-index division
    // by 0x28 (`0x6666666666666667 ; SAR 4`) and the ctor's loop 1, which
    // writes +0x00 (dword), +0x08, +0x10, +0x18 (dword), +0x20 and leaves
    // +0x04 and +0x1c alone.
    // ------------------------------------------------------------------------
    struct AudioChannel;
    // The stored completion callback: RCX = the CHANNEL record, RDX = its
    // `userData`, R8D = its `userValue` (`Play`'s 6th/7th/8th arguments).
    using Callback = void (*)(AudioChannel* channel, void* userData, uint32_t userValue);

    struct AudioChannel {
        uint32_t     flags;       // +0x00  bit 0 live; bit 1 "finished, reap me"
        uint32_t     _pad04;      // +0x04  never written
        SoundBuffer* source;      // +0x08  the SoundBuffer playing; byte +0x21 of
                                  //        it (IsStreamed) gates the Stream lock
        void*        userData;    // +0x10  2nd callback argument (a Scene*)
        uint32_t     userValue;   // +0x18  3rd callback argument (a handle tag)
        uint32_t     _pad1c;      // +0x1c  never written
        Callback     onComplete;  // +0x20  fired by the reap epilogue

        // NOT RETAIL (review F-138): every access to `flags` goes through these.
        // The backend's audio thread sets bit 1 (PlatformChannel::OnStreamEnd)
        // while the main thread tests, sets and clears bits in Update, Stop,
        // StopPlatform, OnSoundBufferDeleted and Scene::Update. Retail uses plain
        // `TEST` / `OR` / `AND` with no lock: a data race, and a plain `|= 1` racing
        // the callback's `|= 2` could lose a bit. Same values and the same order;
        // the RMWs are atomic and the test is an acquire load.
        uint32_t LoadFlags() const { return __atomic_load_n(&flags, __ATOMIC_ACQUIRE); }
        void OrFlags(uint32_t bits) { __atomic_fetch_or(&flags, bits, __ATOMIC_ACQ_REL); }
        void AndFlags(uint32_t mask) { __atomic_fetch_and(&flags, mask, __ATOMIC_ACQ_REL); }
    };

    // ------------------------------------------------------------------------
    // Audio::Device::PlatformChannel — 0x20 bytes, 64 at +0xa98. The object
    // XAudio2 calls back: PlayPlatform hands `&_platformChannels[i]` to
    // CreateSourceVoice as `pCallback`, so its vptr IS an
    // `IXAudio2VoiceCallback` table (retail's unexported 0x180157c60, seven
    // slots, no destructor slot). ENDGAME B.3.9 ENG (B39_RULES D1): a real
    // class whose seven virtuals are those slots, in the interface's order
    // (audio_voice_callback.cpp has the bodies and the evidence). A GCC
    // vtable of a class with only virtual member functions and no virtual
    // destructor is a COM vtable: `this` in RCX, the slots from vptr[0].
    // NO destructor slot and no destructor body: retail's record has none.
    // Phase D.4 A1: the seven slots are declared by `Platform::AudioVoiceCallback`
    // (platform_audio.h, the backend's contract, same order, no data), so a
    // backend calls them through a typed pointer; the record's layout and
    // vtable are unchanged.
    // ------------------------------------------------------------------------
    struct PlatformChannel : Platform::AudioVoiceCallback {
        // The record's part of `??0Device@Audio@@` loop 2 (0x1800de3fd..):
        // the vptr, then +0x08 and +0x18 zero; +0x10 is loop 3's (the Device
        // ctor door points it at `_channels[i]`), so it is NOT written here.
        PlatformChannel() : voice(nullptr), bufferEvent(nullptr) {}

        void OnVoiceProcessingPassStart(uint32_t bytesRequired) override;  // [0] 0x180050200
        void OnVoiceProcessingPassEnd() override;                          // [1] 0x1800501f0
        void OnStreamEnd() override;                                       // [2] 0x1800501c0
        void OnBufferStart(void* bufferContext) override;                  // [3] 0x180050210
        void OnBufferEnd(void* bufferContext) override;                    // [4] 0x1800501d0
        void OnLoopEnd(void* bufferContext) override;                      // [5] 0x180050220
        void OnVoiceError(void* bufferContext, int32_t error) override;    // [6] 0x180050230

        Platform::AudioVoice voice;         // +0x08  the source voice, or null
        AudioChannel* channel;              // +0x10  &_channels[i] (ctor loop 3)
        Platform::EventHandle bufferEvent;  // +0x18  Stream::_bufferEvent, or null
    };

    // ---- exported one-line accessors: faithful inline bodies, NOT REGISTERED
    // (nothing resolves them by name or address) ----
    AudioChannel* GetChannel(int32_t i) { return &_channels[i]; }                 // 0x18000c710
    PlatformChannel* GetPlatformChannel(int32_t i) { return &_platformChannels[i]; } // 0x18000c720

    // ---- PORTED members --------------------------------------------------
    void OnSoundBufferDeleted(const SoundBuffer* buffer);                          // 0x1800de0e0
    void Stop(AudioChannel* channel);                                              // 0x1800de200
    void Update(const System::Time& time);                                         // 0x1800de520
    bool PlatformStartup();                                                        // 0x1800de600 (AEAA)
    void FreeChannelPlatform(int32_t index);                                       // 0x1800de6f0 (AEAA)
    // Retail-PRIVATE (`AEAA`) members our own callers reach because retail
    // INLINED them there: GameMain's teardown and Engine::Shutdown inline
    // PlatformShutdown, Scene::Update inlines SetVolumePlatform (Phase D.4 A1).
    // Public here so those callers call them instead of repeating the COM calls.
    void PlatformShutdown();                                                       // 0x1800de690 (AEAA)
    void SetVolumePlatform(int32_t index, float volume);                           // 0x1800deab0 (AEAA)
    // `ExternalPtr<SoundBuffer>` BY VALUE is a bare register pointer (B7-5,
    // B37_SLOTS section 8: `Play` hands its RDX straight to this R8).
    bool PlayPlatform(int32_t index, Object::ExternalPtr<SoundBuffer> buffer,
                      const System::Time& startTime, float volume, float pitch,
                      float unused);                                               // 0x1800de7a0 (AEAA)
    void StopPlatform(int32_t index);                                              // 0x1800dea20 (AEAA)

    // ---- exported, NOT PORTED, no caller: declared, deliberately undefined --
    static Device* Get();                                                          // 0x18000c740 (reads _singleton)
    // ?_singleton@Device@Audio@@0PEAV12@EA (0x1801fe760) — the class static (ENDGAME B.3.9 ENG, B39_RULES D7).
    // Its minted storage row is RETIRED (ENDGAME B.3.9 ENG W2: no reader was
    // left but the redirect). PUBLIC: foreign files read it directly.
    static Device* _singleton;
    // FIX SEAM (fix.stream_free_wait, NOT RETAIL; DESIGN.md hook rule 5): null by default, the
    // game side points it at the fix's bool. While it reads true, OnSoundBufferDeleted waits for
    // the stream worker's pass over a streamed buffer before the buffer is freed (review S-027).
    // src/engine never includes src/ext. Defined in audio_device_buffer_deleted.cpp.
    static const bool* _extFixStreamFreeWait;
    void SetChannelVolume(AudioChannel* channel, float volume);                    // 0x1800de1b0
    AudioChannel* Play(Object::ExternalPtr<SoundBuffer> buffer, const System::Time& start,
                       float volume, float pitch, float unused, Callback onComplete,
                       void* userData, uint32_t userValue);                        // 0x1800de280
    bool Startup();                                                                // 0x1800de480
    void Shutdown();                                                               // 0x1800de490
    // `??1Device@Audio@@QEAA@XZ` 0x1800de4f0 — not ported; `Engine::Shutdown`
    // (engine_lifecycle.cpp) faithfully does not call it and tears the Stream
    // down through the `??1Stream` door instead.
    ~Device();

private:
    int32_t FindFreeChannel() const;                                               // 0x1800de180 (AEBA)
    void FreeChannel(int32_t index);                                               // 0x1800de350 (AEAA)
    void PlatformConstruct();                                                      // 0x1800de5b0
    void PlatformDestruct();                                                       // 0x1800de5f0

public:
    // `??0Device@Audio@@QEAA@XZ` 0x1800de3a0 is the DOOR `Ctor_AudioDevice`
    // (singleton_ctors.cpp); no C++ constructor is declared (B7-12).

    // ---- the 0x1298 bytes, at retail's offsets (public: the doors reach them)
    AudioChannel     _channels[64];          // +0x000
    Audio::Stream    _stream;                // +0xa00  by value (`LEA RCX,[RBX+0xa00]; CALL ??0Stream`)
    Platform::AudioEngine _xaudio2;          // +0xa88  0 => no platform
    Platform::AudioMaster _masteringVoice;   // +0xa90
    PlatformChannel  _platformChannels[64];  // +0xa98
};

static_assert(sizeof(Device::AudioChannel) == 0x28, "Audio::Device::AudioChannel is 0x28");
static_assert(offsetof(Device::AudioChannel, flags) == 0x0, "AudioChannel::flags");
static_assert(offsetof(Device::AudioChannel, _pad04) == 0x4, "AudioChannel::_pad04");
static_assert(offsetof(Device::AudioChannel, _pad1c) == 0x1C, "AudioChannel::_pad1c");
static_assert(offsetof(Device::AudioChannel, source)     == 0x08, "AudioChannel::source @ +0x08");
static_assert(offsetof(Device::AudioChannel, userData)   == 0x10, "AudioChannel::userData @ +0x10");
static_assert(offsetof(Device::AudioChannel, userValue)  == 0x18, "AudioChannel::userValue @ +0x18");
static_assert(offsetof(Device::AudioChannel, onComplete) == 0x20, "AudioChannel::onComplete @ +0x20");
static_assert(sizeof(Device::PlatformChannel) == 0x20, "Audio::Device::PlatformChannel is 0x20");
static_assert(offsetof(Device::PlatformChannel, voice)       == 0x08, "PlatformChannel::voice @ +0x08");
static_assert(offsetof(Device::PlatformChannel, channel)     == 0x10, "PlatformChannel::channel @ +0x10");
static_assert(offsetof(Device::PlatformChannel, bufferEvent) == 0x18, "PlatformChannel::bufferEvent @ +0x18");

static_assert(sizeof(Device) == 0x1298, "Audio::Device is 0x1298 (MemAlloc at 0x1800dbdd4)");
static_assert(offsetof(Device, _channels) == 0x0, "Device::_channels");
static_assert(alignof(Device) == 8, "Audio::Device aligns to 8");
static_assert(!std::is_polymorphic<Device>::value, "Audio::Device has no vtable");
static_assert(offsetof(Device, _stream)           == 0xa00, "ctor: LEA RCX,[RBX+0xa00]; CALL ??0Stream");
static_assert(offsetof(Device, _xaudio2)          == 0xa88, "PlatformStartup: LEA RDI,[RCX+0xa88]");
static_assert(offsetof(Device, _masteringVoice)   == 0xa90, "PlatformStartup: LEA RSI,[RBX+0xa90]");
static_assert(offsetof(Device, _platformChannels) == 0xa98, "ctor loop 2: LEA RAX,[RBX+0xa98]");
static_assert(offsetof(Device, _stream) + offsetof(Audio::Stream, _lock) == 0xa50,
              "FreeChannelPlatform's CRITICAL_SECTION at Device+0xa50 IS the Stream's lock");
static_assert(offsetof(Device, _stream) + offsetof(Audio::Stream, _wakeEvent) == 0xa40,
              "StopPlatform's SetEvent([Device+0xa40]) IS the Stream's wake event");
static_assert(offsetof(Device, _stream) + offsetof(Audio::Stream, _stopping) == 0xa80,
              "StopPlatform's byte [Device+0xa80] IS the Stream's stop flag");

}  // namespace Audio
