// ============================================================================
// src/engine/audio/audio_device_buffer_deleted.cpp
//   ?OnSoundBufferDeleted@Device@Audio@@QEAAXPEBVSoundBuffer@2@@Z
//                                                   retail 0x1800de0e0  ord 3282
// ============================================================================
//
// The device callback `~SoundBuffer` (batchc_audio_leaves.cpp, retail
// 0x1800e03b0) opens with. Moved out of batchc_audio_leaves.cpp unchanged so the
// unit host links it (tests/unit/stream_free_wait_test.cpp, review S-027): the
// host defines ~SoundBuffer as an ABI marker (audio_abi_test.cpp), so the
// destructor's file cannot link there.
//
// Read from the shipped retail DLL's DISASSEMBLY; the export name read back out
// of the export directory by VA with pefile.
//
// ---------------------------------------------------------------------------
// THE CHANNEL TABLE IS NOT RE-DERIVED HERE
// ---------------------------------------------------------------------------
// `Audio::Device::AudioChannel` — 0x28 bytes, 64 of them at Device+0x00 — was
// pinned by src/engine/audio/audio_device.cpp (Wave 9 lane IOA) from THREE
// independent functions: `Device::Update`'s loop, `Device::Stop`'s
// pointer-to-index division by 0x28, and `Device::StopPlatform`. This
// function's reap epilogue is the SAME five instructions those two carry —
// which is the cross-check that the reading is right, not a coincidence.
//
// ---------------------------------------------------------------------------
// TWO THINGS THE DECOMPILER WOULD GET WRONG
// ---------------------------------------------------------------------------
// 1. `OnSoundBufferDeleted`'s completion callback is invoked with the CHANNEL
//    RECORD as `this` (RSI, which walks in step with the loop), NOT with the
//    Device. At channel 0 the two coincide, which is exactly how a wrong
//    reading would survive testing.
// 2. The reap order is load-bearing and matches `Device::Update` exactly:
//    `onComplete` is read BEFORE the flag/source clears and the callback's 2nd
//    and 3rd arguments AFTER them; the `&= ~3` clears bits 0 AND 1 though only
//    bit 0 was tested here (Update tests bit 1).
// ============================================================================
#include <cstddef>
#include <cstdint>

#include "audio/Device.h"
#include "audio/SoundBuffer.h"
#include "platform_sync.h"   // fix.stream_free_wait: the Stream's wake / finished events

// ############################################################################
// Audio::Device::OnSoundBufferDeleted(const SoundBuffer*)
// retail 0x1800de0e0, ordinal 3282
// ############################################################################
//
//   for (i = 0; i < 0x40; ++i) {
//       ch = this + i*0x28;
//       if (ch->source != buffer) continue;
//       if ((ch->flags & 1) == 0) continue;        // not a live playback
//       ch->flags |= 2;                            // mark it finished ...
//       FreeChannelPlatform(this, i);              // ... then release the voice
//       cb = ch->onComplete;                       // read BEFORE the clears
//       ch->flags &= ~3;
//       ch->source = nullptr;
//       if (cb) cb(ch, ch->userData, ch->userValue);   // `this` is the CHANNEL
//   }
//
// ORACLE: NONE, for `Device::Update`'s reason — this fires stored completion
// callbacks and releases a COM voice. A replay is a second side effect, not a
// comparison. the diff oracle records how many calls matched a channel at all;
// the overwhelming majority of SoundBuffer deletions match none.
// ############################################################################
namespace Audio {

const bool* Device::_extFixStreamFreeWait = nullptr;   // fix.stream_free_wait seam

void Device::OnSoundBufferDeleted(const SoundBuffer* buffer) {
    for (int i = 0; i < 64; ++i) {              // `CMP EDI,0x40 ; JL` at 0x1800de154
        AudioChannel* ch = &_channels[i];
        if (ch->source != buffer) continue;
        if ((ch->LoadFlags() & 1u) == 0) continue;   // atomic: AudioChannel (review F-138)

        // FIX stream_free_wait (review S-027, NOT RETAIL): retail 0x1800de0e0 frees the voice and
        // returns, and ~SoundBuffer (0x1800e03b0) then frees `_wmaData`, while the stream worker may
        // still be in Stream::Run between its ReadFile and its next lock, reading that seek table
        // (0x1800a1230 `MOV ECX,[R10+RDX]`): a use after free. With the row on, the channel the
        // stream is playing has its pass stopped and finished first: Stream::Play's own drain
        // (0x1800a0d2d / 0x1800a0d38 / 0x1800a0d45). `_hFinished` is reset by the Play that started
        // the pass and set when that pass has left Run, so a pass not yet started is waited for too
        // (Play's arm tests the run flag's bit 0 instead, which such a pass has not set yet).
        // The wake: `_wakeEvent` is AUTO-reset, and a pass that leaves through its `_stopping` check
        // without reaching a wait (one not started yet, one past its last wait) never consumes it.
        // Left signalled, it would bail the next track's Run at its first queue-full wait, before
        // END_OF_STREAM. Once `_hFinished` is set the worker is idle (back in ThreadFunc's wait on
        // its start event, which only a main-thread Play sets), so the wake is reset here.
        if (_extFixStreamFreeWait != nullptr && *_extFixStreamFreeWait && buffer->IsStreamed() &&
            _stream._channel == i) {
            __atomic_store_n(&_stream._stopping, uint8_t{1}, __ATOMIC_RELAXED);
            Platform::EventSet(_stream._wakeEvent);
            Platform::EventWait(_stream._hFinished, Platform::kInfinite);
            Platform::EventReset(_stream._wakeEvent);
        }

        ch->OrFlags(2u);
        FreeChannelPlatform(i);

        Callback cb = ch->onComplete;           // read BEFORE the clears
        ch->AndFlags(0xFFFFFFFCu);
        ch->source = nullptr;
        if (cb)
            cb(ch, ch->userData, ch->userValue);  // `this` is the CHANNEL
    }
}

}  // namespace Audio
