// ============================================================================
// src/engine/audio/batchc_audio_leaves.cpp — BATCH C
//   ??1SoundBuffer@Audio@@UEAA@XZ                   retail 0x1800e03b0  ord 679
//   (?OnSoundBufferDeleted@Device@Audio@@, retail 0x1800de0e0, the device callback
//   this destructor opens with, is in audio_device_buffer_deleted.cpp: its own file
//   so the unit host links it without this file's ~SoundBuffer, review S-027.)
// ============================================================================
//
// The two halves of "a sound buffer goes away": the destructor, and the device
// callback it opens with. `??1SoundBuffer@` was a row in
// src/engine/object/deleting_dtor.cpp's table (0x18000bef0 -> 0x1800e03b0,
// elemSize 0x80) reaching the DLL's body; that row now lands here, which is the
// intent (ResolveAddr is registry-first).
//
// Read from the shipped retail DLL's DISASSEMBLY; the export name read back
// out of the export directory by VA with pefile.
// ============================================================================

#include <cstddef>
#include <cstdint>

#include "audio/Device.h"
#include "audio/SoundBuffer.h"
#include "system/MemoryAllocator.h"

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
