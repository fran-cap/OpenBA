// ============================================================================
// src/engine/object/type_system.cpp — Wave 3, lane A — Base / Type / BaseTypeInfo / Factory
// ============================================================================
//
// Byte-faithful ports of the six Object reflection exports our tree calls
// (docs/history/runtime_port/call_surface_20260906.csv). Retail addresses below are in
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
// STATE. Five of the six are read-only walks of the LIVE DLL structures at
// their real addresses; nothing here keeps a replica (rt_object_port.cpp's
// state warning). The sixth, BaseTypeInfo's constructor, MUTATES the DLL
// global `?_firstTypeInfo@BaseTypeInfo@Object@@2PEAV12@EA` (0x1801fc6e0) by
// head-inserting into it, and it runs interleaved with the DLL's own static
// initialisers. It therefore reads and writes that exact global, never a copy.
//
// NO ADDED NULL GUARDS. Retail's lookups deref the found node unconditionally
// and fault when a type id is not registered — CreateObject at 0x1800c0b03
// (`MOV RCX,[0+0x10]`) and IsDerivedFrom at 0x1800c03e0 (`CMP dword [8],EBX`,
// after `ADD RAX,8` on a null). That is load-bearing: it is how a missing
// registration announces itself, and it is what
// src/app/main.cpp's External::Load handler is written against. The ports
// below reproduce it (CLAUDE.md, "Don't 'fix' a faithful crash with a null
// check"). The one consequence to know: the fault now happens in OUR code, at
// an address in OpenBA.exe, not at Runtime+0xC03E0.
//
// DIFFABILITY — what each site can and cannot prove is documented at the site.
// ============================================================================

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <new>          // placement new, for the BaseTypeInfo ctor shim

#include "object/TypeInfo.h"
#include "io/Serializer.h"   // ENDGAME B.3.4 callers batch H


namespace RtObject {

// ============================================================
// DLL globals, resolved once by their exported data symbols.
//
// These are addresses of VARIABLES, not of the things they point at:
// `_firstTypeInfo` is a `BaseTypeInfo*` cell, `_singleton` is a `Factory*`
// cell. `_crcTable` is the array itself.
// ============================================================
namespace {

// NEITHER OF THE TWO DLL GLOBALS THIS BLOCK USED TO RESOLVE IS RESOLVED HERE
// ANY MORE; both moved to the classes that own them.
//
// `?_firstTypeInfo@BaseTypeInfo@Object@@2PEAV12@EA` (0x1801fc6e0) — B.3.3 step 3.
// It is `Object::BaseTypeInfo::_firstTypeInfo`, a plain static in
// src/engine/object/type_reflection.cpp, because the constructor that writes it
// runs at CRT-init where the registry does not exist yet.
// standalone/rt_data_mint.cpp routes the minted row's two doors at that address
// to the same cell, so every by-name and by-address consumer — the probes in
// src/app/main.cpp, `[GameMain] Post-Startup _firstTypeInfo`,
// `cutover_manifest` — still reads exactly one head pointer, ours.
//
// `?_singleton@Factory@Object@@0PEAV12@EA` (0x1801fc6e8) — B.3.3 step 4. It is
// read by `Object::Factory::Get()` through `RT_DATA_SLOT`, which is the same
// minted cell through the same door; the hand-rolled cache here had no caller
// left once `Base::IsDerivedFrom` became the real body.

// ---- CRC-32, the type-id hash -------------------------------------------
//
// MOVED (B.3.3 step 3) to `Object::TypeNameCRC` in
// src/engine/object/type_reflection.cpp, together with the crcTable evidence,
// so the class, this file's ABI shims and standalone/rt_data_mint.cpp all call
// ONE body. The name is kept here because six sites below read better with it.
inline uint32_t TypeNameCRC(const char* name) { return ::Object::TypeNameCRC(name); }



} // namespace

// ============================================================================
// Wave 10 lane MINTSEL: the ONE exported spelling of the type-id hash.
//
// `TypeNameCRC` above is the faithful transcription of retail's private
// ?GetTypeFromNameCRC@Factory@Object@@AEBAIPEBD@Z, and it stays in the
// anonymous namespace where it was written. This thin wrapper exists because
// standalone/rt_data_mint.cpp needs THE SAME hash to stamp a minted
// BaseTypeInfo's `typeId` with the value Factory::Startup would stamp -- and
// the one thing that must NOT happen is a second, independently written copy
// of a CRC that agrees on today's names and diverges on tomorrow's.
//
// Startup's own line is `info->typeId = TypeNameCRC(info->name)`
// (this file, in FactoryStartupRun); a minted node calling this wrapper with
// the same `name` is that line, moved earlier. It is not a new claim about
// retail: it is the existing claim, called from one more place.
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
// DIFF: full oracle. `GetTypeId` is a virtual, so diffing calls it twice —
// which is free here because every implementation of that slot in this engine
// is `MOV EAX,[rip+global]; RET` (see TypeInfo.h). If a future subclass gives
// slot 2 a side effect, this site becomes unsound; nothing does today.
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
// This is by far the hottest function in lane A — several hundred call sites
// in src/game/Action alone go through `fn_IsDerivedFrom`.
//
// DIFF: full oracle. Read-only on both sides.
// ============================================================================
bool RtObject_Base_IsDerivedFrom(const ::Object::Base* self, uint32_t typeId) {
    // The body is `Object::Base::IsDerivedFrom` as of step 4, which keeps the
    // singleton-before-virtual-call order and the unguarded find. `_singleton`
    // is reached through `Factory::Get()` — the same minted cell this file used
    // to resolve by hand.
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
// Serializer+0x00 is the `IO::Stream*`. Those two offsets are all this needs;
// IO::Serializer itself is Wave 4 and is NOT modelled here.
//
// NOT A FORWARDER. This is a real transcription — the object it dispatches on
// is the DLL's ReaderWriter carrying the DLL's vtable, exactly as it is when
// retail runs, so the virtual lands in the DLL either way. That is correct
// until Wave 4 ports ReaderWriter, at which point nothing here changes.
//
// DIFF: NONE IS POSSIBLE. The call mutates the stream; running it twice writes
// the id twice and desyncs every subsequent record — the exact failure mode of
// reference_slot3_wrapper_must_resolve_first.md. The site below registers so
// the run reports a real `calls=` count, and its verdict is the only invariant
// available without a stream: that the ReaderWriter and its vtable are
// non-null. Read it as "this port executed N times", NOT as "this port agrees
// with the DLL N times".
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
//                                        (Wave 9 lane OBJ2, 2026-09-08)
//
// STEP 6 OF Engine::Startup's QUEUE, and the reflection system's own bring-up:
// it is the ONLY code that overwrites `BaseTypeInfo::typeId`'s 0xFFFFFFFF ctor
// sentinel, and the only code that builds the AVL tree every later lookup
// walks (CreateObject, GetTypeIdFromName, IsDerivedFrom, GetTypeName,
// GetTypeIdForFileExtension). Called once per process from
// src/engine/engine/engine_lifecycle.cpp:612.
//
// TWO PASSES over 212 instructions:
//
//   1. Walk `_firstTypeInfo` (0x1801fc6e0). For each entry, CRC-32 its `name`
//      into `typeId`, then insert {typeId, info, nullptr, {}} into the tree
//      keyed by typeId. The CRC is the SAME algorithm as
//      Factory::GetTypeFromNameCRC (0x1800c0930) — retail has it twice, once
//      inline here and once out of line there — so this reuses TypeNameCRC
//      above rather than transcribing it a second time.
//
//   2. In-order walk of the tree just built. For each node whose `info` has a
//      `parentName`, CRC that name, look the parent's node up, store
//      `&parentNode->value` as this record's `parent`, and push
//      `&thisNode->value` onto the parent's `children` array.
//
// Pass 2's tail is `Collection::Array<TypeRecord*>::SetSize(n+1, fill)`
// INLINED — it is instruction-for-instruction the J3 body in
// src/engine/collection/array.cpp (the exact-equal no-op, the
// `capacity = n*3` rule that fires only on a growth after the first, the
// `if (dst == 0) continue`, the copy-then-free). It is transcribed here rather
// than routed through lane Q's `SetSizeCore` because retail inlined it: this
// call site has no `ArraySpec`, no registered address, and a stride and align
// baked in as immediates.
//
// TWO UNGUARDED DEREFS THAT ARE LOAD-BEARING. Both are kept (CLAUDE.md, "Don't
// 'fix' a faithful crash with a null check"):
//
//   * When a `parentName` names a type that is NOT registered, retail's tree
//     search leaves RCX == 0 and `LEA RBP,[RCX+8]` (0x1800c079d) produces
//     `(TypeRecord*)8`, which the very next instruction dereferences at
//     +0x20 — a read of address 0x28. That fault IS how a missing parent
//     registration announces itself, and at cutover it is a fault we want,
//     in our own code, with a symbol name on it.
//   * `info->name` is dereferenced by the CRC with no null test.
//
// RETURNS `true` UNCONDITIONALLY — `MOV AL,1` at 0x1800c0907 is the only exit.
// The `if (!Startup(factory)) { g_startupFailAt = 5; return 0; }` at
// engine_lifecycle.cpp:612 therefore has a branch retail can never take.
//
// DEPENDENCY THIS PORT DID NOT CLOSE — ***CLOSED 2026-09-08 BY WAVE 9 LANE
// COLL.*** The Set insert at 0x180030140 and its whole chain are now ported in
// src/engine/collection/avl_trees.cpp and registered by address, so this body
// reaches OUR insert through the same port-aware door with no edit here and
// THIS ROW NOW CLOSES AT CUTOVER. The diagnostic below is kept: it is the
// early-warning for a registration that failed to arm, not dead code.
//
// The chain, as this file predicted it, and what it actually turned out to be
// (kept side by side because the prediction was good and the two corrections
// are worth having):
//     0x180030140  Set::Insert wrapper (builds the Pair, calls the core,
//                  destroys the temp Array)      ~25 instrs
//     0x180030380  insert core: BST descend + node alloc + retrace   ~50
//     0x180008440  node allocate (0x50 bytes, align 8) + Pair ctor   ~25
//     0x1800087c0  Pair placement-construct
//     0x180030540  AVL retrace, calling the two rotations
//     0x1800307b0 / 0x1800308e0  the two rotations
//     0x180009350 / 0x1800093e0  Array copy-ctor / dtor for the temp
// Nothing else in this file needs them; they are a Collection::Set lane.
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
