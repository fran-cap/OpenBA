// ============================================================================
// src/engine/achievement/achievement_serialize.cpp — Wave 23, lane ACHIEVE
// Achievement::Achievement::Serialize and AchievementManager::Persist
// ============================================================================
//
//   ?Serialize@Achievement@1@UEAA_NAEAVStream@IO@@@Z            0x18009e070
//   ?Persist@AchievementManager@Achievement@@QEAAXAEAVStream@IO@@@Z 0x18009e910
//
// ----------------------------------------------------------------------------
// A STATED BLOCKER, RE-CHECKED — AND IT WAS STALE (Wave 23 rule 3)
// ----------------------------------------------------------------------------
// `achievement_manager.cpp`'s banner holds `Persist` back in these words:
//
//     "it inlines an IO::Serializer construction, and Wave 4 lane A measured
//      that a Serializer ctor CONSUMES OR EMITS A 12-BYTE STREAM HEADER. So it
//      has no runnable oracle of any kind: a second construction desyncs the
//      save."
//
// That was true when written (Wave 5, 2026-09-06) and has not been true since
// Wave 9. Lane S built the save-stream emission oracle, which ran each side
// against its OWN INDEPENDENT SYNTHETIC SINK — there is no shared stream to
// desync, and each side constructs its own codec. Lane P then used exactly that
// to discharge the SAME blocker, stated in the SAME words, for
// `Achievement::Goal::Serialize` and `GoalInstance::Persist`
// (src/engine/achievement/goal_serialize.cpp's banner). The two functions in
// this file are the third and fourth members of that family and the mechanism
// is identical. The holdback survived fourteen waves because nobody re-read it.
//
// The banner in achievement_manager.cpp is corrected in the same commit.
//
// ----------------------------------------------------------------------------
// THE COUNT RECORD
// ----------------------------------------------------------------------------
// `AchievementManager::Persist` emits the int count through ReaderWriter slot
// +0xA0 at 0x18009e988, before its tail (FUN_180009a20) serializes every
// instance record.
//
// NEITHER SetSize CALL IS REACHED ON THE WRITE PATH — both are guarded by
// `stream.direction == 0`. They are transcribed because the read path is the
// one that loads every save.
// ============================================================================

#include <cstdint>
#include <cstdio>

#include "achievement/AchievementManager.h"
#include "io/Serializer.h"
#include "io/Stream.h"   // B.4 R-ENG: IO::Stream::_direction
#include "object/External.h"
#include "object/Internal.h"
#include "ui/StringTable.h"   // B.4 P2 X: T::GetTypeInfo() for Object::SerializeExternal<T>
#include "ui/SpriteSheet.h"   // B.4 P2 X: T::GetTypeInfo() for Object::SerializeExternal<T>

namespace {

// B.4 (R-ENG): the codec prologue / epilogue this file spelled out -- stream,
// null readerWriter, `enc->vtable[+0x18 / +0x20](enc, &ser, version)`, and
// `if (rw) delete rw` at the end -- IS `IO::Serializer(Stream&, version)` and
// `~Serializer` (serializer_class.cpp), so each body holds one IO::Serializer.
// The version argument is ZERO in both bodies: retail zeroes R8D at
// 0x18009e094 / 0x18009e934 and never reloads it before the factory call.

// ---- the unexported internals, called by name ----------------------------
// All four bodies are ours (src/engine/object/external_ptr_serialize.cpp,
// internal_ptr_array_serialize.cpp), declared in object/External.h and
// object/Internal.h (B3R: they were read by retail address before).
//   0x1800042a0  ExternalPtr<UI::StringTable>::Serialize      (+0x20)
//   0x180004590  ExternalPtr<UI::SpriteSheet>::Serialize      (+0x38)
//   0x180005220  serialize N ExternalPtr<Goal>                (+0x48)
//   0x180009a20  serialize N InternalPtr<AchievementInstance>
// The three ExternalPtr ones are the typed members of `Object::ExternalPtr<T>`
// over the one body (B.4 P2 wave X).

}  // namespace

namespace Achievement {

// ============================================================================
// bool Achievement::Serialize(IO::Stream&) — SLOT 5
// ?Serialize@Achievement@1@UEAA_NAEAVStream@IO@@@Z        retail 0x18009e070
//
// SIX RECORDS, in this exact order, and the order is the whole contract:
//   +0x20 _stringTable  ExternalPtr<UI::StringTable>  -> 0x1800042a0
//   +0x28 _title        System::String                -> String::Serialize
//   +0x30 _description  System::String                -> String::Serialize
//   +0x38 _spriteSheet  ExternalPtr<UI::SpriteSheet>  -> 0x180004590
//   +0x40 _spriteName   System::String                -> String::Serialize
//   +0x50 _goals._size  int                           -> ReaderWriter +0xA0
//   then _goals._size elements of _goals._data        -> 0x180005220
// The SetSize (0x180005110) fires only when reading. Always `MOV AL,1`.
// ============================================================================
bool Achievement::Serialize(IO::Stream& streamv) {
    IO::Stream& stream = streamv;
    IO::Serializer ser(stream, 0);

    _stringTable.Serialize(ser);                                    // +0x20 0x1800042a0
    _title.Serialize(ser);                                         // +0x28
    _description.Serialize(ser);                                   // +0x30
    _spriteSheet.Serialize(ser);                                    // +0x38 0x180004590
    _spriteName.Serialize(ser);                                    // +0x40

    int count = _goals.GetSize();                                   // +0x50
    ser._readerWriter->SerializeInt(count, nullptr);                // +0xA0

    if (stream._direction == rt::io::kDirectionReading) {
        _goals.SetSize(count);   // 0x180005110 — `GoalArray` names kSpec_U64Zero
    }
    Object::ExternalPtr<Goal>::SerializeArray(ser, count, _goals.GetData());   // +0x48 0x180005220

    return true;   // ~Serializer deletes the ReaderWriter
}

// ============================================================================
// void AchievementManager::Persist(IO::Stream&)
// ?Persist@AchievementManager@Achievement@@QEAAXAEAVStream@IO@@@Z
//                                                         retail 0x18009e910
//
// NON-virtual. Four steps, two of them read-only:
//   READ ONLY:  RemoveAll()
//   BOTH:       count = _instances._size   -> ReaderWriter +0xA0
//   READ ONLY:  _instances.SetSize(count)  (0x180008d60, kSpec_PtrZero)
//   BOTH:       serialize `count` instances
// RemoveAll zeroes `_size` and the count is read AFTER it — retail's order.
// ============================================================================
void AchievementManager::Persist(IO::Stream& stream) {
    IO::Serializer ser(stream, 0);

    if (stream._direction == rt::io::kDirectionReading)
        RemoveAll();

    int count = _instances.GetSize();
    ser._readerWriter->SerializeInt(count, nullptr);                // +0xA0

    if (stream._direction == rt::io::kDirectionReading)
        _instances.SetSize(count);
    Object::InternalPtr<AchievementInstance>::SerializeArray(           // 0x180009a20
        ser, count, _instances.GetData(), AchievementInstance::TypeId());
}   // ~Serializer deletes the ReaderWriter

}  // namespace Achievement
