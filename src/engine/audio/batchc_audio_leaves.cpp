// ============================================================================
// src/engine/audio/batchc_audio_leaves.cpp — BATCH C
//   ??1SoundBuffer@Audio@@UEAA@XZ                   retail 0x1800e03b0  ord 679
//   ?OnSoundBufferDeleted@Device@Audio@@QEAAXPEBVSoundBuffer@2@@Z
//                                                   retail 0x1800de0e0  ord 3282
// ============================================================================
//
// The two halves of "a sound buffer goes away": the destructor, and the device
// callback it opens with. `??1SoundBuffer@` was a row in
// src/engine/object/deleting_dtor.cpp's table (0x18000bef0 -> 0x1800e03b0,
// elemSize 0x80) reaching the DLL's body; that row now lands here, which is the
// intent (ResolveAddr is registry-first).
//
// Both read from the shipped retail DLL's DISASSEMBLY. Both export names read
// back out of the export directory by VA with pefile.
//
// ---------------------------------------------------------------------------
// THE CHANNEL TABLE IS NOT RE-DERIVED HERE
// ---------------------------------------------------------------------------
// `Audio::Device::AudioChannel` — 0x28 bytes, 64 of them at Device+0x00 — was
// pinned by src/engine/audio/audio_device.cpp (Wave 9 lane IOA) from THREE
// independent functions: `Device::Update`'s loop, `Device::Stop`'s
// pointer-to-index division by 0x28, and `Device::StopPlatform`. This file
// reproduces that layout locally (it must, to static_assert it) and its own
// reap epilogue is the SAME five instructions those two carry — which is the
// cross-check that the reading is right, not a coincidence.
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
#include "system/MemoryAllocator.h"
#include "platform_sync.h"   // fix.stream_free_wait: the Stream's wake / finished events

// ENDGAME B.3.7 block B4: this file's private copy of `AudioChannel` and its
// `ChannelAt` / `FreeChannelPlatform` forward declaration are gone — the
// record is `Audio::Device::AudioChannel` (src/engine/audio/Device.h) and
// the call is the member. The SoundBuffer offsets are the class's fields.

namespace {

// `?MemFree@MemoryAllocator@System@@SAXPEAX@Z` 0x1800c7ad0 — PORTED. Retail's
// four call sites here test for null OUTSIDE the call; MemFree tests again
// inside, so the behaviour is identical. Called by name (B3R).
void MemFree(void* p) { System::MemoryAllocator::MemFree(p); }

}  // namespace

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
        // the pass and set when that pass has left Run, so a pass not yet started is waited for too.
        if (_extFixStreamFreeWait != nullptr && *_extFixStreamFreeWait && buffer->IsStreamed() &&
            _stream._channel == i) {
            __atomic_store_n(&_stream._stopping, uint8_t{1}, __ATOMIC_RELAXED);
            Platform::EventSet(_stream._wakeEvent);
            Platform::EventWait(_stream._hFinished, Platform::kInfinite);
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

// ############################################################################
// Audio::SoundBuffer::~SoundBuffer
// ??1SoundBuffer@Audio@@UEAA@XZ                 retail 0x1800e03b0, ordinal 679
// ############################################################################
//
//   this->vtable = ??_7SoundBuffer@Audio@@6B@
//   Device::OnSoundBufferDeleted(Audio::Device::_singleton, this)
//   MemFree(this->_audioData)      +0x38
//   MemFree(this->_segmentTable)   +0x48
//   ReleaseString(&this->_str2)    +0x30     <-- descending, the mirror of the
//   ReleaseString(&this->_str1)    +0x28         Serialize order
//   this->vtable = ??_7CompiledExternal@Object@@6B@
//   tail ~External(this)
//
// THE DEVICE CALL COMES FIRST, before a single byte is freed: a channel still
// playing this buffer must be stopped while `ch->source == this` still matches.
// Moving it after the frees would leave the mixer reading freed audio.
//
// Layout: `Audio::SoundBuffer` (src/engine/audio/SoundBuffer.h, 0x80).
// ENDGAME B.3.9 ENG: the class's REAL destructor now (the door retired with
// the minted table and its RT_DELDTOR row). The entry restamp is the
// compiler's; so are the two String releases -- member destruction in reverse
// declaration order is `_streamPath` (+0x30) then `_string28` (+0x28),
// retail's descending order -- and the ~CompiledExternal restamp + ~External
// tail.
// ############################################################################
Audio::SoundBuffer::~SoundBuffer() {
    // Block SNDPAR (NOT RETAIL): a buffer still queued in a load's decode batch is
    // finished first, so no pool thread reads the packets freed below (and the
    // [SoundDecode] counts stay those of a decode at Serialize).
    EnsureDecodedAtLoad();

    // `?_singleton@Device@Audio@@` (0x1801fe760), dereferenced UNGUARDED
    // (`MOV RCX,[0x1801fe760]`), as retail does.
    Audio::Device::_singleton->OnSoundBufferDeleted(this);

    if (_data != nullptr)
        MemFree(_data);                                      // +0x38
    if (_wmaData != nullptr)
        MemFree(_wmaData);                                   // +0x48
}
