// ============================================================================
// src/engine/audio/soundbuffer_platform_serialize.cpp
//   ?PlatformSerialize@SoundBuffer@Audio@@AEAAXAEAVSerializer@IO@@@Z  0x1800e05c0
// ============================================================================
// The raw-buffer half of Audio::SoundBuffer::Serialize (vtbatch7_soundbuffer_soundeffect.cpp,
// which calls it). In a file of its own so the unit build links it: the class's slot
// bodies stay in vtbatch7, which tests/unit/audio_abi_test.cpp replaces with markers.
// ============================================================================

#include <cstdint>

#include "io/Serializer.h"
#include "io/Stream.h"
#include "collection/Collection.h"   // rtcoll::MemAlloc / MemFree
#include "audio/SoundBuffer.h"

namespace {

// Retail's Serialize bodies test the stream's direction (+0x08, 0 = reading).
bool IsReading(const IO::Stream* s) { return s->_direction == IO::kReading; }

// `?MemFree@MemoryAllocator@System@@` — retail tests for null OUTSIDE the call.
void MemFreeIfSet(void* p) {
    if (!p) return;
    rtcoll::MemFree(p);
}

}  // namespace

namespace Audio {

// 0x1800e05c0 — the raw-buffer half of Serialize: two counts, a conditional
// (re)allocate-on-read of the two owned buffers, then three raw byte streams
// (seek table, format window, audio data), in that exact order.
void SoundBuffer::PlatformSerialize(IO::Serializer& ser) {
    ser._readerWriter->SerializeInt(_length, nullptr);        // +0xa0
    ser._readerWriter->SerializeInt(_wmaLength, nullptr);     // +0xa0

    if (IsReading(ser._stream)) {
        // NOT RETAIL (hardening, review F-072): retail allocates both counts as
        // read, so a 34-byte record asking for 0x7fffffff bytes ends the game in
        // FatalOutOfMemory. Both buffers follow in this record, with the 0x28-byte
        // format window between them: counts the bytes left cannot hold are a
        // corrupt record, read as two empty buffers. Valid data always fits. A
        // stream that reports no size (GetSize 0: the ext text reader's) is not
        // capped.
        const IO::Stream* const s = ser._stream;
        const int64_t size = static_cast<int64_t>(s->GetSize());
        const int64_t left = size - static_cast<int64_t>(s->GetCurrentPos());
        const int64_t need = static_cast<int64_t>(_wmaLength > 0 ? _wmaLength : 0) + 0x28 +
                             static_cast<int64_t>(_length > 0 ? _length : 0);
        if (size != 0 && need > left) {
            _length = 0;
            _wmaLength = 0;
        }
        MemFreeIfSet(_data);
        MemFreeIfSet(_wmaData);
        _data    = (_length    > 0) ? static_cast<uint8_t*>(rtcoll::MemAlloc(_length, 4)) : nullptr;
        _wmaData = (_wmaLength > 0) ? static_cast<uint32_t*>(rtcoll::MemAlloc(_wmaLength, 4)) : nullptr;
    }

    ser._readerWriter->SerializeBytes(_wmaLength, _wmaData, nullptr);   // +0xc8
    ser._readerWriter->SerializeBytes(0x28, _format, nullptr);          // +0xc8
    ser._readerWriter->SerializeBytes(_length, _data, nullptr);         // +0xc8
}

}  // namespace Audio
