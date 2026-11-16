// ============================================================================
// src/engine/audio/audio_platform_startup.cpp — Wave 9 lane PLAT
//   ?PlatformStartup@Device@Audio@@AEAA_NXZ   retail 0x1800de600  ord 3403
//   ?PlatformShutdown@Device@Audio@@AEAAXXZ   retail 0x1800de690  (Phase D.4 A1)
// ============================================================================
//
// `Engine::Startup` calls it at engine_lifecycle.cpp:658, after Video's, and a false
// return there ends the boot with `g_startupFailAt == 12`. `AEAA` is PRIVATE, not unexported; ordinal and
// address were read out of the shipped export directory with pefile before a
// line was written.
//
// WHAT IT ACTUALLY IS: XAudio2 2.7 BRING-UP
// -----------------------------------------
// Two GUIDs settle it, and both were read out of the shipped .rdata as raw
// bytes rather than recognised by eye:
//
//   0x180157cb8  5A508685-A254-4FBA-9B82-9A24B00306AF  CLSID_XAudio2
//   0x180157c98  8BCF1F58-9FE7-4583-8AC6-E2ADC465C8BB  IID_IXAudio2
//
// Those are the XAudio2 **2.7** (June 2010 DirectX SDK) identifiers, not the
// XAudio2_8/2_9 in-box ones. They are reproduced below as byte arrays copied
// from that dump, so nothing depends on a DirectX SDK header being installed
// (Phase D.4 A1: they, the CoCreateInstance helper and every COM call moved to
// src/platform/win/audio_xaudio2_win.cpp, behind platform_audio.h; SDL-4 deleted
// that backend for src/platform/sdl/audio_sdl.cpp).
//
// THE VTABLE SLOTS ARE COUNTED, NOT ASSUMED
// -----------------------------------------
// IXAudio2 (2.7) after IUnknown's three:
//   +0x18 GetDeviceCount   +0x20 GetDeviceDetails      +0x28 Initialize
//   +0x30 RegisterForCallbacks   +0x38 UnregisterForCallbacks
//   +0x40 CreateSourceVoice +0x48 CreateSubmixVoice    +0x50 CreateMasteringVoice
// The count is CHECKED by argument arity at both call sites rather than by
// memory: 0x1800502b4 calls +0x28 with two arguments after `this` (Initialize's
// Flags/XAudio2Processor), and 0x1800de65c calls +0x50 with SIX
// (CreateMasteringVoice's). A vtable missing `UnregisterForCallbacks` would put
// CreateMasteringVoice at +0x48 and make the second of those calls a
// CreateSubmixVoice with the wrong arity, so the two constraints agree only on
// this layout.
//
// THE DEVICE FIELDS
// -----------------
// Only two offsets of `Audio::Device` are touched, and they are the only two
// this file claims to know:
//   +0xa88  IXAudio2*               (`LEA RDI,[RCX+0xa88]` at 0x1800de60a)
//   +0xa90  IXAudio2MasteringVoice* (`LEA RSI,[RBX+0xa90]` at 0x1800de63a)
// `Engine::Startup` allocates the object as 0x1298 bytes (engine_lifecycle.cpp,
// the 0x1800dbdd4 block), which is the only other extent fact in evidence.
// ENDGAME B.3.7 block B4: both are fields of `Audio::Device` now
// (src/engine/audio/Device.h, `_xaudio2` / `_masteringVoice`, asserted at
// these offsets there); the body below is the member.
//
// IT ALWAYS RETURNS TRUE
// ----------------------
// Every failure path — CoCreateInstance failing, Initialize failing,
// CreateMasteringVoice failing — nulls the engine slot (the master slot too when
// CreateMasteringVoice fails) and returns TRUE. A machine
// with no XAudio2 2.7 redistributable boots silently without audio rather than
// failing `Engine::Startup`. That is retail's design and it is why this
// function is safe to port even where the component is absent.
// ---------------------------------------------------------------------------
// PHASE D.4 A1 (SDL plan Block 3): THE BODIES ARE THE SEAM'S CALLERS NOW
// ---------------------------------------------------------------------------
// The CoCreateInstance + Initialize helper (0x180050270, `RtAudio_CreateXAudio2`
// here before) is `Platform::AudioEngineCreate`; CreateMasteringVoice (+0x50),
// the engine's Release (+0x10) and the master's DestroyVoice (+0x90) are
// `Platform::AudioMasterCreate` / `AudioEngineRelease` / `AudioMasterDestroy`
// (src/platform/sdl/audio_sdl.cpp since SDL-4). No OS header is left, so the file
// moved from src/platform/win/audio_platform_startup_win.cpp.
// ============================================================================

#include <cstdint>

#include "audio/Device.h"
#include "platform_audio.h"

// ============================================================================
// ?PlatformStartup@Device@Audio@@AEAA_NXZ              retail 0x1800de600
// ============================================================================
namespace Audio {

bool Device::PlatformStartup() {
    Platform::AudioEngine* xaSlot = &_xaudio2;
    Platform::AudioMaster* mvSlot = &_masteringVoice;

    const int32_t hr = Platform::AudioEngineCreate(xaSlot);  // 0x1800de617
    if (hr != 0) {                                          // 0x1800de61c, EQUALITY test
        *xaSlot = nullptr;                                  // 0x1800de624
        // NOTE: the mastering-voice slot is NOT touched on this path. Retail
        // leaves whatever the ctor put there.
        return true;                                        // 0x1800de622 MOV AL,1
    }

    const Platform::AudioEngine xa2 = *xaSlot;              // 0x1800de632, re-read
    const int32_t hrMv = Platform::AudioMasterCreate(xa2, mvSlot);   // 0x1800de65c +0x50

    if (hrMv != 0) {                                        // 0x1800de65f, EQUALITY test
        Platform::AudioEngineRelease(*xaSlot);              // 0x1800de669, re-reads the slot
        *xaSlot = nullptr;                                  // 0x1800de66c
        *mvSlot = nullptr;                                  // 0x1800de66f
    }

    return true;                                            // 0x1800de67c MOV AL,1
}

// ============================================================================
// ?PlatformShutdown@Device@Audio@@AEAAXXZ              retail 0x1800de690
// ============================================================================
// Read from the disassembly (Ghidra, 2026-10-10), 0x4c bytes:
//   1800de699  RCX = [this+0xa90] ; if (RCX) CALL [[RCX]+0x90]   master DestroyVoice
//   1800de6ae  RCX = [this+0xa88]                                (loaded BEFORE the store)
//   1800de6b5  [this+0xa90] = 0
//   1800de6c0  if (RCX) CALL [[RCX]+0x10]                        engine Release
//   1800de6cb  [this+0xa88] = 0
// Both null stores are unconditional. Engine::Shutdown (0x1800db576) and
// GameMain's teardown inline exactly this; they call it now.
void Device::PlatformShutdown() {
    if (const Platform::AudioMaster master = _masteringVoice)        // 1800de699
        Platform::AudioMasterDestroy(master);                         // 1800de6a8 +0x90
    const Platform::AudioEngine engine = _xaudio2;                    // 1800de6ae
    _masteringVoice = nullptr;                                        // 1800de6b5
    if (engine)                                                       // 1800de6c0
        Platform::AudioEngineRelease(engine);                         // 1800de6c8 +0x10
    _xaudio2 = nullptr;                                               // 1800de6cb
}

}  // namespace Audio
