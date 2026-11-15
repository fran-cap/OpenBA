// ============================================================================
// src/engine/object/type_system.cpp — Wave 3, lane A — Base / Type / BaseTypeInfo / Factory
// ============================================================================
//
// The six Object reflection exports our tree calls
// (docs/history/runtime_port/call_surface_20260906.csv). Only the Type::Serialize body and
// the IsDerivedFrom forwarder are here; the others are in type_reflection.cpp and
// TypeInfo.h. Retail addresses below are in
// Runtime-x64.dll (image base 0x180000000); profile addresses are in
// Runtime-x64-profile.dll, the ToolKit build, whose asserts and source paths
// are what named the fields. Both were read in Ghidra on 2026-09-06.
//
//   ?IsA@Base@Object@@QEBA_NI@Z                       retail 0x1800491c0  profile 0x1800828f0
//   ?IsDerivedFrom@Base@Object@@QEBA_NI@Z             retail 0x1800c0390  profile 0x18008c670
//   ??0BaseTypeInfo@Object@@QEAA@PEBD00P6APEAVBase@1@XZP6APEAV21@PEAX@Z@Z
//                                                     retail 0x180006320  profile 0x1800234e0
//   ?CreateObject@Factory@Object@@QEBAPEAVBase@2@I@Z  retail 0x1800c0ae0  profile 0x18008cde0
//   ?GetTypeIdFromName@Factory@Object@@QEBAIPEBD_N@Z  retail 0x1800c0580  profile 0x18008cbe0
//   ?Serialize@Type@Object@@QEAAXAEAVSerializer@IO@@@Z retail 0x1800c0c40
//
// Source of truth for the layouts: src/engine/object/TypeInfo.h. Read it
// before this file.
//
// NO ADDED NULL GUARDS. Retail's lookups deref the found node unconditionally
// and fault when a type id is not registered — CreateObject at 0x1800c0b03
// (`MOV RCX,[0+0x10]`) and IsDerivedFrom at 0x1800c03e0 (`CMP dword [8],EBX`,
// after `ADD RAX,8` on a null). That is load-bearing: it is how a missing
// registration announces itself, and it is what
// src/app/main.cpp's External::Load handler is written against.
// `Factory::IsDerivedFrom` (src/engine/object/type_reflection.cpp) reproduces it
// (CLAUDE.md, "Don't 'fix' a faithful crash with a null check"); `CreateObject`
// there returns null on a miss instead (NOT RETAIL, review F-144). The fault now
// happens in OUR code, at an address in OpenBA.exe, not at Runtime+0xC03E0.
//
// DIFFABILITY — what each site can and cannot prove is documented at the site.
// ============================================================================

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <new>

#include "object/TypeInfo.h"
#include "io/Serializer.h"   // ENDGAME B.3.4 callers batch H


namespace RtObject {

namespace {

// NEITHER OF THE TWO DLL GLOBALS THIS BLOCK USED TO RESOLVE IS RESOLVED HERE
// ANY MORE; both moved to the classes that own them.
//
// `?_firstTypeInfo@BaseTypeInfo@Object@@2PEAV12@EA` (0x1801fc6e0) — B.3.3 step 3.
// It is `Object::BaseTypeInfo::_firstTypeInfo`, a plain static in
// src/engine/object/type_reflection.cpp, because the constructor that writes it
// runs at CRT-init.
//
// `?_singleton@Factory@Object@@0PEAV12@EA` (0x1801fc6e8) — B.3.3 step 4. It is
// `Object::Factory::_singleton`, a class static in type_reflection.cpp, returned
// by `Object::Factory::Get()`.

// ---- CRC-32, the type-id hash -------------------------------------------
//
// MOVED (B.3.3 step 3) to `Object::TypeNameCRC` in
// src/engine/object/type_reflection.cpp, together with the crcTable evidence,
// so the class and this file call ONE body. The name is kept here for
// `TypeIdFromName` below.
inline uint32_t TypeNameCRC(const char* name) { return ::Object::TypeNameCRC(name); }



} // namespace

// ============================================================================
// Wave 10 lane MINTSEL: the ONE exported spelling of the type-id hash.
//
// `TypeNameCRC` above forwards to `Object::TypeNameCRC`, the faithful
// transcription of retail's private ?GetTypeFromNameCRC@Factory@Object@@AEBAIPEBD@Z.
// This thin wrapper is its `RtObject` spelling, declared in TypeInfo.h.
// Factory::Startup's own line is `info->typeId = TypeNameCRC(info->name)`
// (src/engine/object/type_reflection.cpp).
// ============================================================================
uint32_t TypeIdFromName(const char* name) { return TypeNameCRC(name); }

// ============================================================================
// ?IsA@Base@Object@@QEBA_NI@Z            retail 0x1800491c0 / profile 0x1800828f0
//
//     bool Base::IsA(unsigned int typeId) const
//     { return typeId == GetTypeId(); }
//
// An EXACT-type test, not a derivation test. Two instructions of substance.
//
// The body is the inline `Object::Base::IsA` in src/engine/object/TypeInfo.h.
// ============================================================================

// ============================================================================
// ?IsDerivedFrom@Base@Object@@QEBA_NI@Z  retail 0x1800c0390 / profile 0x18008c670
//
// Retail is `Factory::IsDerivedFrom(_singleton, this->GetTypeId(), typeId)`
// inlined (the profile build shows the call unwrapped, plus two asserts that
// retail compiles out). The walk:
//
//     if (GetTypeId() == typeId) return true;
//     record = FindNode(_singleton, GetTypeId())->value;   // faults on a miss
//     while (record->typeId != typeId) {
//         record = record->parent;
//         if (!record) return false;
//     }
//     return true;
//
// The base chain is the `parent` links Factory::Startup filled in its second
// pass, NOT the `_firstTypeInfo` list.
//
// ORDER MATTERS AND IS PRESERVED: retail loads `_singleton` into RDI BEFORE
// making the virtual GetTypeId call (0x1800c039a-0x1800c03a6). Harmless today,
// kept because it is what the disassembly says.
//
// ============================================================================
bool RtObject_Base_IsDerivedFrom(const ::Object::Base* self, uint32_t typeId) {
    // The body is `Object::Base::IsDerivedFrom` as of step 4, which keeps the
    // singleton-before-virtual-call order and the unguarded find. `_singleton`
    // is reached through `Factory::Get()` — the class static `Factory::_singleton`.
    return self->IsDerivedFrom(typeId);
}

// ============================================================================
// ?Serialize@Type@Object@@QEAAXAEAVSerializer@IO@@@Z      retail 0x1800c0c40
//
//     void Type::Serialize(IO::Serializer& s)
//     { s._readerWriter->vtable[0x98](s._readerWriter, this, 0); }
//
// Six instructions, all dispatch. `Object::Type` is a 4-byte wrapper around a
// type id (`??BType@Object@@QEBAIXZ` is its `operator unsigned int`), and this
// serializes that id as a plain uint through the ReaderWriter's +0x98 slot.
//
// Serializer+0x08 is the `IO::ReaderWriter*`; pinned by the profile build's
// Achievement::Serialize (0x180022880), where the local at Serializer+8 is the
// object every `AllowCustomSerialize` / slot-0xf8 / slot-0x100 call is made on.
// Serializer+0x00 is the `IO::Stream*`.
//
// The call mutates the stream: running it twice writes the id twice and
// desyncs every subsequent record.
// ============================================================================
// ENDGAME B.3.4 callers batch H: the hand-inlined slot call became the member.
// `serializer + 8` is the READERWRITER (this file already named it `rw`, which
// is the one site in the tree that did) and +0x98 is rw_vtbl::kScalarUInt, so
// the member is IO::Serializer::Serialize(unsigned int&) — an Object::Type is
// the 4-byte type id and `self` was already the field pointer. The old third
// argument `0` was the ignored `const char* name`.
// B4 S4 LAST: `self` is uint32_t* — every caller's field is the 4-byte type id
// (ConsumeProduce::_notifyComponentType retyped uint32_t: retail reads it only as a dword
// copy, 0x1400e8d00).
void RtObject_Type_Serialize(uint32_t* self, ::IO::Serializer* serializer) {
    serializer->Serialize(*self, nullptr);   // 0x98 = uint door
}

// ============================================================================
// ?Startup@Factory@Object@@QEAA_NXZ      retail 0x1800c0630  ord 4096
//
// The body and its banner are `Object::Factory::Startup` in
// src/engine/object/type_reflection.cpp. Its type-id hash is
// `Object::TypeNameCRC`, which `TypeNameCRC` above forwards to: retail's
// Factory::GetTypeFromNameCRC (0x1800c0930).
// ============================================================================

namespace {

// ---- WHAT USED TO LIVE HERE ------------------------------------------------
//
// The allocator boundary, the `FactorySet` probe-only view struct, `LeftMost`,
// `Successor` and `FactoryStartupRun` — ~225 lines of hand-walked AVL and an
// inlined `Array<TypeRecord*>::SetSize` — are now `Object::Factory::Startup`
// and the `Collection::Set` it stands on (ENDGAME B.3.3 step 3,
// src/engine/object/type_reflection.cpp). Every measurement they carried moved
// with them; the two that a reader of THIS file still needs are repeated in
// src/engine/object/TypeInfo.h next to the class:
//
//   * `Object::Factory` is 0x10 bytes, not 8. `??0Factory@Object@@QEAA@XZ`
//     (0x1800c09d0) writes `[RCX]=0` and a DWORD `[RCX+8]=0`, and the insert
//     core INCs that dword at 0x1800303ac / 0x18003043c — it is the Set's
//     element count. (The 8-byte `RtObject::Factory { TypeNode* root; }` view
//     is gone, B.4: the AVL core takes the root slot itself.)
//   * `TypeNode` is 0x50 bytes and its +0x48 is TWO 16-BIT FIELDS, not one
//     int32 (0x180030540 `CMP word [RDX+0x48],-1` and
//     `MOVZX ...,word [RAX+0x4a]`; 0x1800087c0 zeroes both in one dword store,
//     which is why an int32 view looked right until something rebalanced).
//     `Collection::Set<TypeEntry,…>::Node` spells them as `int16_t balance` and
//     `int16_t height` and the header asserts both offsets.
//
// The by-address call to the Set insert at 0x180030140 is also gone: the
// template reaches the SAME ported core (`AvlOps<Node,0x30>::Retrace` ->
// `PortedRetrace48`). `rtavl::TypeSetInsert` and its 0x180030140 registration
// are deleted in B.3.3 step 7 — nothing resolved that address any more.
// ----------------------------------------------------------------------------

}  // namespace

} // namespace RtObject
