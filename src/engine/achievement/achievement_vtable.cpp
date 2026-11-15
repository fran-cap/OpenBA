// ============================================================================
// src/engine/achievement/achievement_vtable.cpp — WAVE 27 lane ACHIEVE
// ============================================================================
//
// THE FOUR NON-ACCESSOR SLOTS of `??_7Achievement@0@6B@` and
// `??_7AchievementInstance@Achievement@@6B@`. Goal::GetFileVersion,
// GoalInstance::Startup and GoalInstance::CheckGoalState are in
// achievement_typeinfo_accessors.cpp; the Type / TypeName slots are
// DECLARE_TYPE's (achievement_type_records.cpp).
//
//   ?Construct@Achievement@1@UEAAPEAVExternal@Object@@XZ    0x1800012f0  slot 8
//   ?Reconstruct@Achievement@1@UEAAPEAVExternal@Object@@XZ  0x180001270  slot 6
//   ?Destruct@Achievement@1@UEAAXXZ                         0x180001370  slot 7
//   ?Persist@AchievementInstance@Achievement@@UEAAXAEAVStream@IO@@@Z
//                                                           0x18009e4a0  slot 3
//
// GREPPED BEFORE WRITTEN: all four mangled names and all four addresses
// returned nothing across code/ engine/ runtime/ standalone/ src/render/dx11/ except the
// generated standalone/rt_data_mint_tables.inc.
//
// ============================================================================
// A GHIDRA MCP DEFECT MADE THE FIRST READING OF TWO OF THESE WRONG
// ============================================================================
// `mcp__ghidra-mcp__disassemble_function(address="0x18009f0e0")` returned a
// DIFFERENT function (entry 0x18009f090, a 0x100-byte allocation stamping
// vtable 0x1801a9310) while `get_function_by_address` on the SAME address
// correctly reported `Initialize at 18009f0e0, body 18009f0e0-18009f264`. The
// same happened for 0x180009d70 (returned entry 0x180009c20). It is the
// address-silently-ignored / name-fallback defect already recorded for
// `get_function_metrics`. NOT FIXED — kickoff §4 #1. Written up in
// WAVE27_LANE_ACHIEVE.md under INSTRUMENT DEFECT OBSERVED, NOT FIXED.
//
// Consequence for this file and its sibling goal_list.cpp: every decode below
// was re-taken from the SHIPPED BYTES via `pefile` + `objdump -b binary`, not
// from the MCP disassembler, and each listing is transcribed beside the body it
// produced.
//
// ============================================================================
// Construct / Reconstruct / Destruct — Object::External's three-hook protocol
// ============================================================================
// `Object::External` gives every resource class three virtual hooks the loader
// uses to recycle storage in place: build into raw memory, tear down without
// freeing, and do both. All three are inlined copies of one constructor.
//
//   ?Destruct@Achievement@1@  0x180001370, THREE instructions:
//       48 8b 01     MOV RAX,[RCX]        ; this->vtable
//       33 d2        XOR EDX,EDX          ; flags = 0  -> do NOT free
//       48 ff 20     JMP [RAX]            ; tail call slot 0
//     i.e. `this->vtable[0](this, 0)` — the scalar-deleting destructor with the
//     delete bit CLEAR. The call is VIRTUAL, so on an EXE subclass (Scenario)
//     it runs the subclass's destructor, which is the whole point of the hook.
//
//   ?Reconstruct@Achievement@1@  0x180001270:
//       MOV RAX,[RCX]; XOR EDX,EDX; CALL [RAX]     ; destruct in place
//       …then the THIRTEEN STORES of ??0Achievement@0@, inlined verbatim:
//       [this]=??_7Base@Object@@6B@ (0x180154788)
//       [this]=??_7External@Object@@6B@ (0x1801547a0)
//       [this+0x08]=[this+0x10]=[this+0x18]=0
//       [this]=??_7Achievement@0@6B@ (0x180154740)
//       [this+0x20 .. +0x50]=0
//       MOV RAX,RBX; RET                            ; returns `this`
//
//   ?Construct@Achievement@1@  0x1800012f0:
//       TEST RCX,RCX; JZ -> `XOR EDX,EDX; MOV EAX,EDX` (return nullptr)
//       …the SAME thirteen stores…
//       MOV RAX,RCX; RET
//     The null test is the ONLY difference from Reconstruct's tail, and it is
//     retail's, not a guard added here.
//
// THE THIRTEEN STORES ARE NOT RE-TRANSCRIBED. They are the real constructor
// `Achievement()` (AchievementManager.h); Reconstruct and Construct run it with
// `::new (this) Achievement()`.
//
// ============================================================================
// AchievementInstance::Persist — THE OUTLIER OF THE Serialize FAMILY
// ============================================================================
// It is the FIFTH member of the family goal_serialize.cpp and
// achievement_serialize.cpp ported, and it is the only one that is not the
// plain "one codec, N records, destroy codec" shape. From the bytes at
// 0x18009e4a0:
//
//   ser1 = { stream, null }
//   enc  = stream->encoding                                 ; [stream+0x10]
//   ser1.rw = stream->direction ? enc->CreateWriter(&ser1, 1)
//                               : enc->CreateReader(&ser1, 1)
//        *** THE VERSION ARGUMENT IS 1, NOT 0 ***   (MOV R8D,1 at 0x18009e4c9)
//        Every other member of this family passes 0.
//   if (ser1.rw->GetVersion() == 0) { ser1.rw->Skip(); goto cleanup; }
//                                                    ; slots +0x08 and +0x10
//   ExternalPtr<Achievement>::Serialize(&this->achievement, &ser1)  ; +0x20
//   ser2 = { stream, null }
//   ser2.rw = … same factory, VERSION 0 …             (XOR R8D,R8D at 0x18009e522)
//   int count = this->goalList._size                  ; [this+0x18]
//   ser2.rw->ScalarInt(&count, null)                  ; slot +0xA0
//   if (stream->direction == reading)
//       Array<InternalPtr<GoalInstance>>::SetSize(&this->goalList, count)
//   SerializeInstancePtrs(&ser2, count, this->goalList._data)
//   if (ser2.rw) ser2.rw->vtable[0](ser2.rw, 1)
//   cleanup:
//   if (ser1.rw) ser1.rw->vtable[0](ser1.rw, 1)
//
// TWO CODECS, NESTED, WITH DIFFERENT VERSIONS. The outer one exists only to ask
// the encoding whether this optional record block is present at all; the inner
// one carries the goal list. Collapsing them into one, or passing 0 for the
// outer, changes the bytes on disk.
//
// THE VERSION GATE IS A REAL EARLY RETURN. `GetVersion()==0` skips the
// achievement pointer AND the whole goal list. A save written by an older
// build takes that path.
//
// ============================================================================

#include <cstdint>
#include <cstdio>
#include <new>

#include "achievement/AchievementManager.h"
#include "io/Serializer.h"
#include "io/Stream.h"   // B.4 R-ENG: IO::Stream::_direction
#include "object/External.h"
#include "object/Internal.h"


// (ENDGAME B.3.9 ENG: the thirteen stores are the real constructor
// `Achievement()` now; the ??0 door that stamped the MINTED tables is retired.)

namespace {

// B.4 (R-ENG): the codec prologue / epilogue this file spelled out -- stream,
// null readerWriter, `enc->vtable[+0x18 / +0x20](enc, &ser, version)`, and
// `if (rw) delete rw` at the end -- IS `IO::Serializer(Stream&, version)` and
// `~Serializer` (serializer_class.cpp), so each body holds one IO::Serializer.
// Persist passes version 1 for the outer codec and 0 for the inner one --
// see the banner.

// Two unexported template instantiations (no mangled name exists), ours in
// src/engine/object/ (external_ptr_serialize.cpp,
// internal_ptr_array_serialize.cpp), called by name (B3R):
//   0x180006d10  ExternalPtr<Achievement::Achievement>::Serialize
//   0x180009fa0  serialize N InternalPtr<Achievement::GoalInstance>

}  // namespace

namespace Achievement {

// ============================================================================
// void Achievement::Destruct() — SLOT 7   ?Destruct@Achievement@1@UEAAXXZ  0x180001370
//     MOV RAX,[RCX]; XOR EDX,EDX; JMP [RAX]
// `this->~T()` — slot 0 with the free bit CLEAR. VIRTUAL on purpose:
// on the exe's Scenario it runs Scenario's destructor.
// ============================================================================
void Achievement::Destruct() {
    this->~Achievement();
}

// ============================================================================
// Object::External* Achievement::Reconstruct() — SLOT 6   0x180001270
// Destruct in place through the VIRTUAL slot 0, then the constructor's
// thirteen stores over the same storage (the real constructor `Achievement()`),
// then `return this`. On a Scenario this leaves a base-class vptr; the caller
// re-stamps. Retail's.
// ============================================================================
Object::External* Achievement::Reconstruct() {
    this->~Achievement();
    return ::new (this) Achievement();
}

// ============================================================================
// Object::External* Achievement::Construct() — SLOT 8   0x1800012f0
// `TEST RCX,RCX / JZ -> return nullptr`, then the thirteen stores, `return
// this`. The null test is RETAIL'S and the member does not carry it, because a
// member cannot be called on a null `this`.
// ============================================================================
Object::External* Achievement::Construct() {
    return ::new (this) Achievement();
}

// ============================================================================
// void AchievementInstance::Persist(IO::Stream&) — SLOT 3
// ?Persist@AchievementInstance@Achievement@@UEAAXAEAVStream@IO@@@Z  0x18009e4a0
//
// TWO CODECS, NESTED, WITH DIFFERENT VERSIONS (outer 1, inner 0), and a real
// early return when the outer reports version 0 — see the banner.
// ============================================================================
void AchievementInstance::Persist(IO::Stream& streamv) {
    IO::Stream& stream = streamv;

    // The OUTER codec, version 1 (MOV R8D,1 at 0x18009e4c9).
    IO::Serializer outer(stream, 1);

    const int version = outer._readerWriter->GetVersion();

    if (version == 0) {
        // Absent in this stream's format: skip; only the outer codec exists
        // to be destroyed (~Serializer at the return).
        outer._readerWriter->Skip();
        return;
    }

    _achievement.Serialize(outer);                                  // 0x180006d10, +0x20

    // The INNER codec, version 0 (XOR R8D,R8D at 0x18009e522).
    IO::Serializer inner(stream, 0);

    int count = _goalList._goals.GetSize();                         // +0x18
    inner._readerWriter->SerializeInt(count, nullptr);              // +0xA0

    if (stream._direction == rt::io::kDirectionReading) {
        // retail 0x180009d70 — `_goals` names kSpec_PtrZero in its type.
        _goalList._goals.SetSize(count);
    }
    Object::InternalPtr<GoalInstance>::SerializeArray(               // 0x180009fa0
        inner, count, _goalList._goals.GetData(), GoalInstance::TypeId());
}   // ~inner, then ~outer: the two ReaderWriters deleted in retail's order

}  // namespace Achievement

// ============================================================================
// ENDGAME B.3.9 ENG: the four slot doors (Construct, Reconstruct, Destruct,
// AchievementInstance::Persist) and `RtAchVtableRegister` are gone -- their
// only reader was the minted `??_7Achievement@0@6B@` /
// `??_7AchievementInstance@Achievement@@6B@` tables.
// ============================================================================
