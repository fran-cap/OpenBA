// ============================================================================
// src/engine/achievement/goal_serialize.cpp — Wave 9 lane P (tier 0)
// Achievement::Goal::Serialize and Achievement::GoalInstance::Persist
// ============================================================================
//
//   ?Serialize@Goal@Achievement@@UEAA_NAEAVStream@IO@@@Z    0x18009ee20
//   ?Persist@GoalInstance@Achievement@@UEAAXAEAVStream@IO@@@Z 0x18009ef70
//
// These are the TWO functions Wave 5 lane A explicitly held back
// (src/engine/achievement/goal.cpp's banner: "the two that move a stream
// cursor ... because no oracle can run both implementations without
// double-consuming the stream"). That reason was correct when it was written
// and is no longer the whole truth: Wave 9 lane S built
// the Wave 9 lane S emission oracle, which replayed a WRITE-path Serialize into two
// INDEPENDENT SYNTHETIC SINKS and compares the emission sequence. A write-path
// Serialize is a pure read of the object, so the replay is legitimate; what
// made it impossible before was the shared stream, not the function.
//
// So this file exists because a blocker dissolved, not because the earlier
// judgement was wrong. goal.cpp's banner is corrected in place (PLAN §6 step 8).
//
// ----------------------------------------------------------------------------
// TWO CORRECTIONS TO WAVE 9 LANE B'S INVENTORY
// ----------------------------------------------------------------------------
// 1. `wave9_laneB_inventory.csv` lists `Goal::Serialize` as `ghidra-exact, 3`
//    instructions and "likely delegates to a shared base Serialize". It is
//    **33 instructions** and it delegates to nothing: it builds an
//    `IO::Serializer` on its own stack, asks the stream's `IO::Encoding` for a
//    reader or a writer, drives two field serializers, and destroys the codec.
//    A "3-instruction" reading of it would have been ported as a one-line
//    forwarder and silently emitted nothing.
//
// 2. The CSV's callee note claims `GoalInstance::Persist` shares the unnamed
//    internal `FUN_180009d70` with `GoalList::Initialize`, and lane P's brief
//    made "port them together to pay for that dependency once" the reason to
//    pair them. **Persist does not call FUN_180009d70, or FUN_180006d10, or
//    FUN_180009fa0.** Its only direct callee is `FUN_180005dc0`. Read at
//    0x18009ef70 the call list is: two `IO::Encoding` vtable slots (+0x18 /
//    +0x20), `FUN_180005dc0`, one `ReaderWriter` slot (+0x98), and the codec's
//    deleting destructor. The pairing rationale was false; the two functions
//    are paired here only because they are the same shape.
//
// ----------------------------------------------------------------------------
// THE TWO ExternalPtr SERIALIZERS
// ----------------------------------------------------------------------------
//   FUN_1800042a0  — reached by Goal::Serialize     (over Goal+0x20)
//   FUN_180005dc0  — reached by GoalInstance::Persist (over GoalInstance+0x18)
//
// Both are instantiations of the SAME template body — `Object::ExternalBasePtr
// ::Serialize` for two different pointee types; the two decompilations are
// line-for-line identical apart from the `_typeInfo` they compare against.
// Both are called as `Object::ExternalPtr<T>::Serialize`, over the one body in
// src/engine/object/external_ptr_serialize.cpp.
//
// A CAUTION FOR WHOEVER PORTS THEM, and it is stronger than this comment's own
// first draft said. That draft read "on the READING side (`Encoding` slot +0x10
// returns 3) they call ExternalFactory::FindOrLoad". **That conflated two
// different discriminators and understated the problem.** Reading the whole
// body, there are TWO `FindOrLoad` call sites, selected by two unrelated tests:
//
//   1. `encoding->vtable[+0x10]() == 3` -- NOT "reading". It is a THIRD mode,
//      the resource-reference VISIT pass (WAVE3.md finding 5, IO::VisitorStream
//      / ??_7Visitor@IO@@6B@). It calls GetPath -> FindOrLoad -> IsDerivedFrom
//      and THROWS THE RESULT AWAY; the call is made for its side effect of
//      forcing the referenced resource to be loaded.
//   2. The common tail, guarded by `stream->direction == 0` -- the ORDINARY
//      READING path. This one calls FindOrLoad and **STORES the result into
//      `*param_1`**. It is the actual resolution of the reference, on every
//      save load, not an exotic mode.
//
// So `ExternalFactory::FindOrLoad` -- which WAVE9.md rows 27-28 record as having
// NO POSSIBLE ORACLE, because it rewrites the one shared AVL tree -- is reached
// by the plain load path, and a port of these two internals inherits that
// squarely. It is not a corner.
//
//
// THE ONE REPLAY HAZARD ON THE WRITE PATH, checked rather than hedged: that path
// emits `ExternalFactory::GetReferenceId(_singleton, ext)` as a uint. If that
// ASSIGNED an id on first use, replaying would emit N then N+1 and the harness
// would manufacture a divergence it caused itself -- the exact shape of the
// longjmp/dtor defect lane S found in its own prefix stop. It does not:
// `?GetReferenceId@ExternalFactory@Object@@QEBAIPEBVExternal@2@@Z` (0x1800bf100)
// is `QEBA` -- const -- and its body is a pure lookup (build a Filename, find it
// in the map at `this+0x10`, return `[node+0x14]`, else -1). No insert, no
// counter. The write path is idempotent and the replay is sound.
// ============================================================================

#include <cstdint>
#include <cstdio>

#include "achievement/Goal.h"
#include "io/Serializer.h"
#include "io/Stream.h"   // B.4 R-ENG: IO::Stream::_direction
#include "object/External.h"
#include "ui/StringTable.h"   // B.4 P2 X: T::GetTypeInfo() for Object::SerializeExternal<T>


namespace {

// The two ExternalBasePtr::Serialize instantiations (see the banner). Both take
// (ExternalBasePtr* self, Serializer* ser) — RCX/RDX at the call sites — and
// return nothing the caller reads. They are `Object::ExternalPtr<T>::Serialize`
// over the one body in src/engine/object/external_ptr_serialize.cpp.

// B.4 (R-ENG): the shared prologue (stream, null readerWriter,
// `enc->vtable[+0x18 / +0x20](enc, &ser, 0)`) and epilogue (`if (rw) delete
// rw`, the scalar deleting dtor with flag 1) of both bodies ARE
// `IO::Serializer(Stream&, 0)` / `~Serializer` (serializer_class.cpp). The
// version is ZERO: retail zeroes R8D at 0x18009ee44 / 0x18009ef94 and never
// reloads it before the factory call.

}  // namespace

namespace Achievement {

// ============================================================================
// bool Goal::Serialize(IO::Stream&) — SLOT 5
// ?Serialize@Goal@Achievement@@UEAA_NAEAVStream@IO@@@Z   retail 0x18009ee20
//
// Serializes exactly Goal's own two fields, in declaration order:
//   +0x20 _stringTable  ExternalPtr<UI::StringTable>  -> 0x1800042a0
//   +0x28 _text         System::String                -> String::Serialize
// A load therefore consumes precisely those two records. Getting the ORDER or
// the COUNT wrong is the silent save-desync this project has paid for four
// times. Always `MOV AL,1`.
//
// The slot's parameter is retail's `IO::Stream&`, as `Object::External`
// declares it (External.h).
// ============================================================================
bool Goal::Serialize(IO::Stream& streamv) {
    IO::Serializer ser(streamv, 0);

    _stringTable.Serialize(ser);                                    // 0x1800042a0, Goal+0x20
    _text.Serialize(ser);                                          // Goal+0x28

    return true;   // ~Serializer deletes the ReaderWriter
}

// ============================================================================
// void GoalInstance::Persist(IO::Stream&) — SLOT 3
// ?Persist@GoalInstance@Achievement@@UEAAXAEAVStream@IO@@@Z  retail 0x18009ef70
//
// Same prologue and epilogue; the middle is two records, NOT in declaration
// order — the ExternalPtr at +0x18 goes BEFORE the uint at +0x10. That is what
// the bytes do; "tidying" it to ascending offset would desync every save.
//   +0x18 _goal   ExternalPtr<Achievement::Goal> -> 0x180005dc0
//   +0x10 _flags  uint                           -> ReaderWriter +0x98
// +0x98 is `uint` in RETAIL (+0xb0 in the profile build — never copy offsets
// from it). The record's NAME argument is NULL (`XOR R8D,R8D` at 0x18009efcc).
// ============================================================================
void GoalInstance::Persist(IO::Stream& streamv) {
    IO::Serializer ser(streamv, 0);

    _goal.Serialize(ser);                                           // 0x180005dc0, GoalInstance+0x18

    ser._readerWriter->SerializeUInt(_flags, nullptr);              // GoalInstance+0x10, +0x98
}   // ~Serializer deletes the ReaderWriter

}  // namespace Achievement

// ---- The registered doors — QUALIFIED delegations --------------------------
// ENDGAME B.3.9 ENG: the two slot doors (`?Serialize@Goal@Achievement@@`
// 0x18009ee20 and `?Persist@GoalInstance@Achievement@@` 0x18009ef70) and their
// registration are gone -- their only reader was the minted `??_7Goal` /
// `??_7GoalInstance` tables. The exe subclasses call the members QUALIFIED.
