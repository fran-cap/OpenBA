// ============================================================================
// src/engine/object/external_ptr_serialize.cpp — Wave 10, lane EXTPTR
//   `Object::ExternalPtr<T>::Serialize` — ONE 748-byte body, TWENTY-FIVE
//   addresses, all unexported internals.
// ============================================================================
//
// This is the body that blocked `?Serialize@ElementDescription@UI@@` (0x180116b00)
// through two waves. `docs/history/runtime_port/WAVE10_EXTPTR_SERIALIZE.md` answered the
// gate — the three callees at +0x48/+0x50/+0x58 are one body parameterised by a
// type id — and this file is that recipe executed. The lane write-up, including
// the measurement that extends the family from three to twenty-five, is
// `docs/history/runtime_port/WAVE10_LANE_EXTPTR.md`.
//
// ---------------------------------------------------------------------------
// WHY THERE ARE TWENTY-FIVE ADDRESSES AND NOT THREE
// ---------------------------------------------------------------------------
// `ExternalPtr<T>` is a template. MSVC emitted one `Serialize` per T and did not
// fold them, because each one loads its own `?_typeInfo@T@…` global. Every copy
// is otherwise the SAME 215 instructions / 748 bytes.
//
// A raw byte-diff cannot see that: every rel32 and every RIP displacement
// encodes a distance FROM THE INSTRUCTION, so two identical bodies at different
// addresses differ in each of those fields (measured: 210 of the first 900
// bytes, 23.3%, which reads like two different functions and is an artefact).
// The position-resolved diff — decode both, and at each difference resolve the
// operand back to the ABSOLUTE address it encodes — reports for EVERY pair:
//
//     identical = 193 instructions
//     position-only = 20      (same absolute target, different distance)
//     genuinely different = 2 (`MOV EDX, dword [rip+…]`, the type-id load,
//                              twice: once per IsDerivedFrom call site)
//
// A masked byte-pattern sweep of `.text` — the reference body with every rel32
// and RIP displacement wildcarded, 165 of 748 bytes — then finds all 25 copies
// and reads each one's type-id global back out. The table is in the lane doc.
//
// So the port is ONE function with the type id as a parameter, and the typed
// template wrappers in External.h supply each T's own `BaseTypeInfo`.
//
// ---------------------------------------------------------------------------
// FOUR OF THE TWENTY-FIVE
// ---------------------------------------------------------------------------
// FOUR of the twenty-five:
//
//   0x180038f90  ExternalPtr<UI::ElementDescription>   ElementDescription+0x48
//   0x180076610  ExternalPtr<UI::Dialog>               ElementDescription+0x50
//   0x1800042a0  ExternalPtr<UI::StringTable>          ElementDescription+0x58
//                                                 and  Achievement::Goal+0x20
//   0x180005dc0  ExternalPtr<Achievement::Goal>        GoalInstance+0x18
//
// The first three are the recipe's three: they are exactly what
// `Serialize@ElementDescription` dispatches. The FOURTH is not in the recipe.
//
// The other twenty-one are measured, named and tabulated in the lane doc. They
// reach Video / Graphics / Audio / Collision2D resource loading (meshes,
// materials, shaders, sounds, fonts). All twenty-five reach the one body below
// through the typed wrappers in External.h.
//
// ---------------------------------------------------------------------------
// THE CALLEES
// ---------------------------------------------------------------------------
// `?FindOrLoad@ExternalFactory@Object@@QEAAPEBVExternal@2@AEBVFilename@System@@@Z`
// (0x1800bf320) IS PORTED. `RtObject_ExternalFactory_FindOrLoad` in
// src/engine/object/external_compiled.cpp is the body, reached here through
// `ExternalFactory::FindOrLoadAs`.
//
// `External::GetPath`, `Base::IsDerivedFrom`, `ExternalFactory::GetReferenceId`
// and `ExternalFactory::GetPathForReferenceId` are member calls, and
// `String::Serialize` is called as a member of `System::String`.
//
// ---------------------------------------------------------------------------
// THE WRITE PATH
// ---------------------------------------------------------------------------
// IS THE WRITE PATH REPLAYABLE? RE-MEASURED, NOT INHERITED. The recipe warns
// that `GetReferenceId` can register a NEW id and says to cap the prefix with
// `stopAfter` as `SerializeReferenceTable` does. Read at 0x1800bf100 rather
// than assumed, it does not: the body is `GetPath` -> `Filename` copy ->
// `Set::Find(this+0x10, key)` -> `return node[0x14]`, then two String releases.
// It is `QEBA` (const), there is no insert, no counter and no store into the
// factory. So the emission sequence is idempotent and the WHOLE body replays;
// no `stopAfter` is needed and none is used. (`SerializeReferenceTable` needs
// one because it WRITES an id into every node — a different function.)
//
// ---------------------------------------------------------------------------
// LAYOUT NOTES A READER WILL WANT
// ---------------------------------------------------------------------------
// `param_2` is an `IO::Serializer*` — `{ Stream*, ReaderWriter* }`. The body
// reads `*(Stream**)param_2` and then `Stream+0x08` (direction) and
// `Stream+0x10` (Encoding*), which is why the caller
// `Serialize@ElementDescription` builds the pair on its own stack and passes
// `&pair`, and why `String::Serialize` is handed that same pointer.
//
// Retail's three locals live in the CALLER's 32-byte home space (`MOV RBP,RSP`
// after five pushes, so `[RBP+0x38]`, `[RBP+0x40]` and `[RBP+0x48]` are above
// the return address). Legal in the MS x64 ABI, and noted only so a reader
// checking offsets against the listing does not read them as arguments. They
// are `scratch` (a `Filename` in the encoding-3 arm, then a uint / bool / the
// result pointer), `path` and `tmp`, and they are spelled as separate locals
// below because their lifetimes do not overlap.
//
// PORTABLE: no <windows.h> (PLAN.md §4.1 decision 1).
// ============================================================================

// ---------------------------------------------------------------------------
// B.4 P2 WAVE X (X-R1): THE THIRTY THUNKS ARE RETIRED
// ---------------------------------------------------------------------------
// Every caller names the typed template wrappers in External.h
// (`Object::SerializeExternal(ser, field)`, `SerializeExternalArray`,
// `ExternalPtr<T>::Serialize`), which supply `T::GetTypeInfo()` -- exactly
// what each `EXTPTR_THUNK_OF` row supplied -- and reach the ONE body below.
// The body takes the real `IO::Serializer` and the real `Object::` classes and
// reaches its five dependencies as members: `External::GetPath` (0x180006250),
// `ExternalFactory::FindOrLoad` (0x1800bf320), `GetReferenceId` (0x1800bf100),
// `GetPathForReferenceId` (0x1800bf1d0) and `Base::IsDerivedFrom` (0x1800c0390)
// -- the same bodies the C entry points reached.
// ============================================================================

#include <cstdint>

#include "io/Serializer.h"
#include "io/Stream.h"
#include "object/External.h"
#include "object/external_visit.h"   // block VISITPASS: the mode-1 visit
#include "object/external_compiled.h" // NOT RETAIL: FindOrLoadHitRow (VISITREPLAY.md)
#include "system/Filename.h"
#include "system/String.h"

namespace Object {

// ============================================================================
// void Object::ExternalPtr<T>::Serialize(IO::Serializer&)
//   retail: 25 addresses, 215 instructions / 748 bytes each. Unexported.
//
// RCX = `self`, the eight-byte `ExternalPtr<T>` field IN PLACE; here its value
// `cur` (see External.h: nothing is written through `self` before the load
// tail, and the return value says whether that store happens).
// RDX = the `IO::Serializer`.
// `typeInfo` is this instantiation's `?_typeInfo@T@…` — retail's two
// `MOV EDX, dword ptr [rip+…]` loads; `typeId` is read AT CALL TIME at each of
// the two sites, exactly as retail does.
//
// THREE MODES, in retail's order:
//   1. Encoding type == 3 — no stream I/O at all. Re-resolve the object we
//      already hold through `FindOrLoad`, run the type check and DISCARD its
//      answer (retail does: `CALL IsDerivedFrom` with the result unused), then
//      release the scratch path. This is the resource-reference VISIT pass.
//   2. reference mode ON (`factory+0x40`) — a uint reference id crosses the
//      stream; on load it is turned back into a path through the factory's
//      id-keyed tree.
//   3. reference mode OFF — a bool "present" flag then the path string.
// Modes 2 and 3 converge on one load tail: `FindOrLoad(path)`, type-check, and
// write the result THROUGH `self`.
//
// RETAIL DEREFERENCES NOTHING IT HAS NOT ALREADY TESTED HERE, so there are no
// added guards in this body. Where retail is unguarded downstream — e.g.
// `GetReferenceId` derefs the node `Set::Find` returned with no null test — that
// is the callee's business and its own port already transcribes it.
// ============================================================================
bool ExternalPtrSerializeBody(External* cur, IO::Serializer& ser,
                              const BaseTypeInfo& typeInfo, External*& loaded) {
    // ---- mode 1: encoding type 3 (0x1800042ce) -----------------------------
    // `MOV RAX,[RDX]; MOV RCX,[RAX+0x10]; MOV RAX,[RCX]; CALL [RAX+0x10]`:
    // Encoding slot 2, fetched off the STREAM, not the Serializer.
    if (ser._stream->_encoding->GetKind() == 3) {
        if (cur == nullptr) return false;
        ++RtObject::g_ptrCensus.visit;   // NOT RETAIL, print-only (VISITREPLAY.md section 1)

        // `scratch = cur->GetPath(); found = FindOrLoad(scratch); if (found)
        // (void)found->IsDerivedFrom(T)` -- the answer genuinely discarded, retail
        // calls it for the side effect of nothing and keeps the pointer it
        // already had. Block VISITPASS (external_visit.cpp) runs that walk, or
        // its exact shortcut onto the object's own row.
        RtObject::VisitExternal(cur, typeInfo);
        return false;
    }

    // ---- modes 2 and 3 (0x18000433e) ---------------------------------------
    // `path` is released on every one of the five exits below; all five are
    // its destructor, including the three early returns.
    ExternalFactory* factory = ExternalFactory::_singleton;      // r14
    System::Filename path;                                       // [rbp+0x40]
    IO::Stream* stream = ser._stream;
    // EXT SEAM (NOT RETAIL, docs/extensions/handins/VISITREPLAY.md): ext.find_fastpath's reference-id
    // arm. `refRow` is the row a reference id already named (the load tail then runs FindOrLoad's hit
    // arm on it); `refStore` asks the extension to remember what the faithful tail finds.
    const ExtFindFast* const fast = ExternalFactory::_extFindFast;
    void* refRow = nullptr;
    bool refStore = false;
    uint32_t refStoreId = kNoReferenceId;

    if (factory->_referenceModeEnabled) {
        // ---- mode 2: reference id ------------------------------------------
        uint32_t refId = (cur != nullptr) ? factory->GetReferenceId(cur)
                                          : kNoReferenceId;

        // 0x180004372. The odd arm, transcribed rather than rationalised: when
        // we are LOADING, already hold an object, and that object has no
        // reference id, retail consumes the uint record and BAILS — it neither
        // resolves a path nor writes through `self`.
        if (cur != nullptr && stream->_direction == IO::kReading &&
            refId == kNoReferenceId) {
            ser._readerWriter->SerializeUInt(refId, nullptr);    // +0x98
            return false;
        }

        // The record's NAME is the third argument, NULL at both sites. The
        // slot numbers are RETAIL's (+0x98 / +0x78); the profile DLL's differ.
        ser._readerWriter->SerializeUInt(refId, nullptr);        // 0x1800043ad
        if (refId == kNoReferenceId) {                           // 0x1800043b7
            return false;
        }

        if (stream->_direction == IO::kReading) {                // 0x1800043c1
            // EXT SEAM (NOT RETAIL, VISITREPLAY.md): the id already named a row.
            if (fast != nullptr && factory->_pathStack._size == 0)
                refRow = fast->findRef(*factory, refId);
            if (refRow == nullptr) {
                System::Filename tmp = factory->GetPathForReferenceId(refId);  // [rbp+0x48]
                // 0x1800043dd release-then-adopt plus the AdjustDelimiters that
                // follows it IS `Filename::operator=`, 0x180006770.
                path = tmp;
                refStore = fast != nullptr && factory->_pathStack._size == 0;
                refStoreId = refId;
            }
        }
        // falls into the load tail
    } else {
        // ---- mode 3: present flag + path string (0x180004459) --------------
        bool present = (cur != nullptr);                         // SETNE, one byte
        ser._readerWriter->SerializeBool(present, nullptr);      // +0x78
        if (!present) {
            return false;
        }

        if (cur != nullptr) {                   // saving: fill `path` from the object
            System::Filename tmp = cur->GetPath();               // [rbp+0x48]
            path = tmp;                         // operator=, as above
        }
        path.AsString().Serialize(ser);        // 0x180004500; load fills `path`
        // falls into the load tail
    }

    // ---- the shared load tail (0x18000450c) --------------------------------
    // Saving skips it entirely: retail re-tests the direction here and jumps
    // straight to the release below.
    if (stream->_direction == IO::kReading) {
        // NOT RETAIL, print-only (VISITREPLAY.md section 1).
        if (factory->_referenceModeEnabled)
            ++RtObject::g_ptrCensus.read2;
        else
            ++RtObject::g_ptrCensus.read3;
        External* found = nullptr;
        if (refRow != nullptr) {
            // EXT SEAM (NOT RETAIL, VISITREPLAY.md): FindOrLoad(GetPathForReferenceId(id))'s hit arm
            // on the row the id named; src/ext/find_fastpath/find_fastpath.cpp has the argument.
            const External* hit = RtObject::RtObject_ExternalFactory_FindOrLoadHitRow(factory, refRow);
            found = const_cast<External*>(hit);   // CAST-OK: const_api
        } else {
            found = ExternalFactory::_singleton->FindOrLoadAs<External>(path);
            if (refStore && found != nullptr)
                fast->storeRef(*factory, refStoreId, path._data, path.GetLength());
        }
        External* result = nullptr;
        if (found != nullptr && found->IsDerivedFrom(typeInfo.typeId)) {
            // CMOVNE: the loaded object survives only if it IS a T. A resource
            // of the wrong type is dropped, not kept. `FindOrLoad` is
            // retail's `QEAAPEBV` (const result); FindOrLoadAs drops it once.
            result = found;
        }
        loaded = result;
        return true;                            // 0x180004555 `*self = result`
    }
    // path's final release is its destructor.
    return false;
}

}  // namespace Object
