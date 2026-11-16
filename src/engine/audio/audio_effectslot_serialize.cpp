// ============================================================================
// src/engine/audio/audio_effectslot_serialize.cpp — BATCH 8b (2026-09-12)
//
//   0x180052640   the EffectSlot BULK element serialize      186 bytes
//   0x180051e00   EffectSlot::positions, the inner Array     148 bytes
//   0x180052330   Array<EmitterInstance>::SetSize(int)       644 bytes
//                 (ported in src/engine/collection/array.cpp as
//                  kSpec_EmitterInstance — a Collection row, not an Audio one)
//
// All three are UNEXPORTED — measured against the retail export directory with
// pefile before this file was written, not read off an `AEAA`. Renamed in retail Ghidra as
// `Audio_Scene_SerializeEffectSlots`,
// `Audio_Scene_SerializeEmitterInstances` and
// `Collection_Array_EmitterInstance_SetSize`.
//
// ---------------------------------------------------------------------------
// WHY BATCH 8 DECLINED THEM, AND WHAT WAS ACTUALLY MISSING
// ---------------------------------------------------------------------------
// BATCH 8 held these back because they "need a new EffectSlot layout this batch
// did not reverse-engineer". The layout was already in the tree:
// `src/engine/audio/Audio.h` has pinned `Audio::Scene::EffectSlot` (0x40,
// nine fields) and `Audio::EmitterInstance` (0x10, four floats) since Wave 8,
// from the emitter API and the array helpers.
//
// These two bodies are an INDEPENDENT WITNESS to that layout and they agree
// with it field for field. The serialize picks a ReaderWriter vtable slot per
// field, and each slot's width is fixed by `src/engine/io/Serializer.h`'s
// `rw_vtbl` table, so the offsets AND the widths fall out of the instructions:
//
//   retail                       slot          field (Audio.h)
//   ------------------------------------------------------------------------
//   1800526_72  [rax+0x28] n=3   kArrayFloat   posX/posY/posZ   +0x00..+0x0b
//   1800526_83  [rax+0x98]       kScalarUInt   soundType        +0x0c
//   18005268f   CALL 0x180051790               soundEffectRef   +0x10 (8)
//   1800526a2   [rax+0x90]       kScalarInt64  startTime        +0x18
//   1800526b6   [rax+0x98]       kScalarUInt   idTag            +0x24
//   1800526ca   [rax+0x80]       kScalarFloat  baseVolume       +0x28
//   1800526d7   CALL 0x180051e00               positions        +0x30
//
// TWO FIELDS ARE DELIBERATELY NOT SERIALIZED and that is the interesting half:
//   * `channelHandle` (+0x20) — a live handle into the Channel table. A save
//     that restored it would point at a channel that is not playing.
//   * `_pad2c` (+0x2c) — the four bytes array.cpp:1384 already records as NEVER
//     WRITTEN by either the default-init or the copy-assign arm. Three
//     unrelated bodies now agree that this window is dead; that agreement is
//     the point, and it is why an oracle must not compare it.
//
// And the inner element, from 0x180051e60/0x180051e75: kArrayFloat n=3 at
// +0x00 then kScalarFloat at +0x0c, stride 0x10 — `EmitterInstance{x,y,z,
// volume}` exactly. Audio.h only had `sizeof` asserted for it; this commit adds
// the four offset asserts, because these two bodies are what pins them.
//
// ---------------------------------------------------------------------------
// THE READ-SIDE RESIZE IS THE ONLY DIRECTION-DEPENDENT STEP
// ---------------------------------------------------------------------------
// 0x180051e2f: `MOV RAX,[RDI] / CMP dword [RAX+8],0 / JNE past` — the inner
// array is resized to the serialized count ONLY when `stream->direction == 0`
// (reading). On the write path the count goes out and the array is left alone,
// the same rule `Audio::Scene::Serialize` (audio.cpp) already follows for the
// outer effect array. `kDirectionReading` is 0 in Serializer.h.
//
// NOTE the count is re-read from the STACK SLOT after the resize
// (0x180051e41 `MOV EAX,[RSP+0x30]`), not from `arr->_size`, and the element
// walk uses `rbx = arr->_data` loaded at 0x180051e45 — i.e. AFTER the resize
// may have reallocated. Both are transcribed in that order.
//
// ============================================================================

#include <cstdint>

#include "audio/Audio.h"
#include "audio/SoundEffect.h"
#include "io/Serializer.h"
#include "io/Stream.h"
#include "../collection/array_internal.h"  // ENDGAME B.3.2 step 2: AsArray bridge

// `kSpec_EmitterInstance` (0x180052330, BATCH 8b) is defined in
// src/engine/collection/array.cpp and declared in array_internal.h; the line
// below repeats that declaration.
namespace rtcoll { extern const ArraySpec kSpec_EmitterInstance; }

using RtAudio::EffectSlot;
using RtAudio::EmitterInstance;

// 0x180051790 — `Object::ExternalPtr<Audio::SoundEffect>::Serialize`: the
// template's member (External.h) over SoundEffect's own type record.

// ============================================================================
// 0x180051e00 — serialize one `EffectSlot::positions`.
//   void (Collection::Array<EmitterInstance>* arr /*RCX*/,
//         IO::Serializer* ser /*RDX*/)
// ============================================================================
extern "C" void RtAudio_Scene_SerializeEmitterInstances(Collection::Array<EmitterInstance>* arr,
                                                        IO::Serializer* ser) {
    // The count lives in a STACK SLOT for the whole body; `arr->GetSize()` is
    // read once, into it, and never again (0x180051e0a).
    int32_t count = arr->GetSize();

    ser->_readerWriter->SerializeInt(count, nullptr);         // +0xa0

    // READING ONLY (0x180051e2f `CMP dword [stream+8],0 / JNE`).
    // ENDGAME B.3.2 step 2 — the 0x180052330 by-address forwarder is GONE:
    // `kSpec_EmitterInstance` zero-fills the whole 16-byte stride
    // (`InitZero10`), which a trivially-constructible `EmitterInstance`'s trait
    // row would leave UNINITIALISED, so it stays a hand-derived bridge rather
    // than a plain `arr->SetSize(count)`.
    if (ser->_stream->_direction == IO::kReading) {
        rtcoll::AsArray<&rtcoll::kSpec_EmitterInstance>(arr)->SetSize(count);  // 0x180051e3c
    }

    // `_data` is re-loaded HERE, after the resize may have reallocated.
    EmitterInstance* e = arr->GetData();  // 0x180051e45

    for (int32_t i = 0; i < count; ++i, ++e) {                 // 0x180051e53
        ser->_readerWriter->SerializeFloatArray(3, &e->position.x, nullptr);  // +0x28
        ser->_readerWriter->SerializeFloat(e->volume, nullptr);               // +0x80
    }
}

// ============================================================================
// 0x180052640 — the bulk element pass over `Audio::Scene::_effects`.
//   void (IO::Serializer* ser /*RCX*/, int32_t count /*EDX*/,
//         EffectSlot* data /*R8*/)
//
// `count <= 0` returns before the prologue (`TEST EDX,EDX / JLE ret`).
// ============================================================================
extern "C" void RtAudio_Scene_SerializeEffectSlots(IO::Serializer* ser, int32_t count,
                                                   EffectSlot* s) {
    if (count <= 0) return;                                    // 0x180052640


    do {
        ser->_readerWriter->SerializeFloatArray(3, &s->pos.x, nullptr);  // 0x180052672 +0x28
        ser->_readerWriter->SerializeUInt(s->soundType, nullptr);        // 0x180052683 +0x98

        // 0x18005268f — the ExternalPtr<SoundEffect> at +0x10, `(this, ser)`.
        s->soundEffectRef.Serialize(*ser);   // ExternalPtr<Audio::SoundEffect>

        ser->_readerWriter->SerializeInt64(s->startTime._ticks, nullptr);  // 0x1800526a2 +0x90

        // +0x20 `channelHandle` is SKIPPED; see the banner.
        ser->_readerWriter->SerializeUInt(s->idTag, nullptr);            // 0x1800526b6 +0x98
        ser->_readerWriter->SerializeFloat(s->baseVolume, nullptr);      // 0x1800526ca +0x80

        // 0x1800526d7 — the inner array at +0x30, `(arr, ser)`.
        RtAudio_Scene_SerializeEmitterInstances(&s->positions, ser);

        ++s;                                                   // ADD RBX,0x40
    } while (--count != 0);                                    // DEC RSI / JNE
}
