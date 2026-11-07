#pragma once
// ============================================================================
// src/engine/object/TypeInfo.h — Object reflection layouts (Wave 3, lane A)
// ============================================================================
//
// Every layout below was read out of Runtime-x64.dll in Ghidra (image base
// 0x180000000, 2026-09-06) and cross-checked against Runtime-x64-profile.dll
// (the ToolKit build), whose asserts name the fields for us. The function that
// pins each offset is named next to it. Nothing here is inferred from a
// consumer's guess; where something IS a guess it says so.
//
// WHAT THIS DESCRIBES
// -------------------
// The engine's reflection system has two halves that are easy to conflate:
//
//   1. `BaseTypeInfo` — one static object per registered class, chained
//      through `_firstTypeInfo`. It is built by static initialisers (the DLL's
//      own, and ours through src/game/Object/factory_registration.cpp) BEFORE
//      anything can look a type up. It carries the class's names and its two
//      factory functions, and its `typeId` is 0xFFFFFFFF until Startup.
//
//   2. `TypeNode` / `TypeRecord` — the AVL tree `Object::Factory` builds in
//      `Factory::Startup` (0x1800c0630) by walking that chain once, CRC'ing
//      each name into a typeId, and inserting a node per type. Every runtime
//      lookup (CreateObject, GetTypeIdFromName, IsDerivedFrom, GetTypeName,
//      GetTypeIdForFileExtension) walks THIS tree, never the chain.
//
// The tree is `Collection::Set<Pair<uint32_t, TypeRecord>, …>` — exactly the
// shape WAVE2_COLLECTION.md predicted, with the key stored separately from the
// value. The profile build proves it verbatim: Factory::IsDerivedFrom
// (profile 0x18008ce60) asserts
//     "typePtr->_type == type && it->key == type"   Object\Factory.cpp:0x8c
// i.e. the node holds BOTH `it->key` (node+0x00) and `typePtr->_type`
// (node+0x08), and they are required to agree.
//
// NO OBJECT HERE IS EVER ALLOCATED BY US. Wave 3 lane A only reads these
// structures at their live DLL addresses, and only `BaseTypeInfo` is ever
// written — by the ported constructor, into storage the CALLER owns.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "../collection/Collection.h"   // B7: TypeRecord::children is the real Array

namespace Object { class Base; }   // the factory pair's return type (below)
namespace Object {
// A type id not assigned yet (BaseTypeInfo::typeId until Factory::Startup gives it
// its CRC id), and the type a null InternalPtr serializes.
constexpr uint32_t kInvalidTypeId = 0xFFFFFFFFu;
}  // namespace Object
namespace Object { class BaseTypeInfo; }   // B.4 Z RT2: TypeRecord::info, the real class

namespace RtObject {

// ============================================================
// Object::Base — only the vtable pointer is pinned here.
//
// Pinned by Base::IsA (retail 0x1800491c0 / profile 0x1800828f0):
//     (**(code **)(*(longlong *)this + 0x10))()
// so vtable slot 2 (+0x10) is `virtual uint32_t GetTypeId() const`. Every
// concrete implementation of that slot seen in the exe is a two-instruction
// `MOV EAX,[rip+typeIdGlobal]; RET`, which is why calling it twice (as the
// diff harness does) is free of side effects.
//
// The rest of Base's layout belongs to lanes B/C; do not add fields here.
// ============================================================

// ============================================================
// Object::BaseTypeInfo — 0x38 bytes, one static per registered class.
//
// Pinned field-by-field by the constructor
//   ??0BaseTypeInfo@Object@@QEAA@PEBD00P6APEAVBase@1@XZP6APEAV21@PEAX@Z@Z
//   retail 0x180006320 / profile 0x1800234e0
// and by the four readers:
//   Factory::Startup            0x1800c0630  reads +0x08 (CRC source), +0x10
//                                            (parent lookup), writes +0x00
//   Factory::CreateObject       0x1800c0ae0  calls +0x20
//   Factory::CreateObject(id,p) 0x1800c0aa0  calls +0x28
//   Factory::GetTypeName        0x1800c0a60  returns +0x08
//   Factory::GetTypeIdForFileExtension 0x1800c04c0  _stricmp's +0x18
// ============================================================

// ============================================================
// Object::Factory's per-type record — the VALUE half of a tree node, 0x28
// bytes. The profile build calls its first field `_type`.
//
// Built on the stack by Factory::Startup (0x1800c0630) as
//     { typeId, info, nullptr, {nullptr, 0, 0} }
// and copied into the node by the Set insert helper (retail 0x180030140,
// which copies +0x00, +0x08, +0x10 and then copy-constructs the Array at
// +0x18). Startup's second pass fills `parent` and pushes each child into its
// parent's `children` Array.
//
// `children` is `Collection::Array<TypeRecord*>` — the 16-byte
// {_data, _size:i32, _capacity:i32} triple from WAVE2_COLLECTION.md, and the
// growth code in Startup (capacity*3 on any growth after the first) is
// visible at 0x1800c07d0. Each stored element is a `TypeRecord*`, i.e. a
// pointer to another node's +0x08 — NOT a node pointer.
// ============================================================
struct TypeRecord {
    uint32_t    typeId;        // +0x00  profile: `_type`
    uint32_t    _pad04;
    ::Object::BaseTypeInfo* info;  // +0x08  the static this record was built from
    TypeRecord* parent;        // +0x10  base class's record, or nullptr at a root
    // +0x18 {_data, _size +0x20, _capacity +0x24}. B7: the real member (it was
    // the raw triple). Its ~Array is the J1 body 0x1800093e0; ~Factory's walk
    // (factory_dtors.cpp) calls it explicitly per node, as retail does.
    Collection::Array<TypeRecord*> children;
};
static_assert(offsetof(TypeRecord, typeId)        == 0x00, "TypeRecord::typeId");
static_assert(offsetof(TypeRecord, info)          == 0x08, "TypeRecord::info");
static_assert(offsetof(TypeRecord, parent)        == 0x10, "TypeRecord::parent");
static_assert(offsetof(TypeRecord, children)      == 0x18, "TypeRecord::children");
static_assert(offsetof(TypeRecord, children) + offsetof(Collection::Array<TypeRecord*>, _size)
              == 0x20, "TypeRecord::children._size");
static_assert(offsetof(TypeRecord, children) + offsetof(Collection::Array<TypeRecord*>, _capacity)
              == 0x24, "TypeRecord::children._capacity");
static_assert(sizeof(TypeRecord)                  == 0x28, "sizeof(TypeRecord)");
static_assert(offsetof(TypeRecord, _pad04) == 0x4, "TypeRecord::_pad04");

// ============================================================
// A node of Factory's AVL tree: Set<Pair<uint32_t, TypeRecord>, …>::Node.
//
// MEASURED offsets, each from a specific instruction:
//   +0x00 key    — `MOV ECX,[RAX]` then CMP in every lookup
//                  (CreateObject 0x1800c0ae8, GetTypeIdFromName 0x1800c0596,
//                   IsDerivedFrom 0x1800c03b8)
//   +0x08 value  — `ADD RAX,0x8` in IsDerivedFrom (0x1800c03d5) before the
//                  base-chain walk; `[RAX+0x10]` = value.info in CreateObject
//   +0x30 parent — Startup's and GetTypeIdForFileExtension's in-order
//                  successor walk (`puVar+0xc`)
//   +0x38 left   — taken when key > wanted
//   +0x40 right  — taken when key < wanted
//
// MEASURED 2026-09-08 by Wave 9 lane COLL, which DOES insert, rebalance and
// free these nodes. Both facts lane A recorded as guesses are now pinned by an
// instruction, and one of them was WRONG AS SPELLED:
//
//   sizeof == 0x50   0x180008440 `MOV EDX,0x50 / LEA R8D,[RDX-0x48]`
//                    -> Allocate(0x50, align 8)
//   +0x48 is TWO 16-BIT FIELDS, not one int32:
//                    0x180030540 `CMP word ptr [RDX+0x48],-1 / JGE`  -> a
//                        SIGNED 16-bit BALANCE at +0x48
//                    0x180030540 `MOVZX ...,word ptr [RAX+0x4a] / INC` -> a
//                        16-bit HEIGHT at +0x4a
//                    0x1800087c0 `MOV dword ptr [RDI+0x48],ESI` zeroes BOTH in
//                        one dword store, which is why an int32 view of +0x48
//                        looked right for as long as nobody rebalanced.
//
// B.4 P2 R-OBJ (2026-10-03) re-read 0x180030540 and typed the two words:
//   0x1800305a9 `MOV word ptr [RDX+0x48],CX`   balance = r - l   (int16)
//   0x1800305a5 `MOV word ptr [RDX+0x4a],AX`   height  = max     (16-bit)
//   0x180030595 `CMP R8W,CX / CMOVG AX,R8W`    the max is a SIGNED compare,
//                                              so height is int16 as well
//   0x1800305e0 `CMP word ptr [RDX+0x48],-1 / JGE`  balance is signed
// No reader of the old `int32_t balance` existed (avl_trees.cpp read both words
// through Balance50()/Height50() at the raw offsets). `_pad4c` is the node's
// tail padding: the allocation is 0x50 (0x180008440) and nothing reads it.
// ============================================================
struct TypeNode {
    uint32_t    key;      // +0x00  duplicate of value.typeId; asserted equal
                          //        by the profile build
    uint32_t    _pad04;
    TypeRecord  value;    // +0x08 .. +0x2f
    TypeNode*   parent;   // +0x30
    TypeNode*   left;     // +0x38
    TypeNode*   right;    // +0x40
    int16_t     balance;  // +0x48  0x1800305a9 MOV word [RDX+0x48]
    int16_t     height;   // +0x4a  0x1800305a5 MOV word [RDX+0x4a]
    int32_t     _pad4c;   // +0x4c  tail padding of the 0x50 allocation
};
static_assert(offsetof(TypeNode, key)     == 0x00, "TypeNode::key");
static_assert(offsetof(TypeNode, value)   == 0x08, "TypeNode::value");
static_assert(offsetof(TypeNode, parent)  == 0x30, "TypeNode::parent");
static_assert(offsetof(TypeNode, left)    == 0x38, "TypeNode::left");
static_assert(offsetof(TypeNode, right)   == 0x40, "TypeNode::right");
static_assert(offsetof(TypeNode, balance) == 0x48, "TypeNode::balance (0x1800305a9)");
static_assert(offsetof(TypeNode, height)  == 0x4a, "TypeNode::height (0x1800305a5)");
static_assert(offsetof(TypeNode, _pad4c)  == 0x4c, "TypeNode tail");
static_assert(sizeof(TypeNode)            == 0x50, "TypeNode is 0x50 (0x180008440)");
static_assert(offsetof(TypeNode, _pad04) == 0x4, "TypeNode::_pad04");

// ============================================================
// Object::Factory — the tree root (the class is below; this note is its
// evidence).
//
// Every lookup starts with `MOV RAX,[RCX]`, so the Set's root pointer is at
// +0x00. The Set's element count and allocator, and everything else Factory
// owns (the compiler list behind LoadCompilers/UnloadCompilers), are NOT
// modelled: nothing in lane A's scope touches them.
//
// The singleton lives at Runtime 0x1801fc6e8 and IS an exported data symbol,
// `?_singleton@Factory@Object@@0PEAV12@EA`. PLAN.md §2.3 lists that address as
// "Factory singleton (LoadDialogResource)" among globals that "have no
// export"; both halves of that are wrong.
// ============================================================

// ============================================================
// The type-id hash, as ONE function.
//
// `BaseTypeInfo::typeId` is CRC-32 (reflected, poly 0xEDB88320) of the type's
// `name` widened to UTF-16LE -- retail's private
// ?GetTypeFromNameCRC@Factory@Object@@AEBAIPEBD@Z. Defined in
// src/engine/object/type_system.cpp, where Factory::Startup's own
// `info->typeId = TypeNameCRC(info->name)` lives; declared here so
// standalone/rt_data_mint.cpp can stamp a minted node with the SAME value
// instead of leaving it at the ctor sentinel.
// ============================================================
uint32_t TypeIdFromName(const char* name);

} // namespace RtObject

// ############################################################################
// ENDGAME B.3.3 step 3 — the real classes.
//
// Everything above is the POD description Waves 3/9 measured, and it STAYS:
// B31_RULES step 1 keeps the old PODs declared and trivially destructible until
// their last caller converts. What follows is the same bytes as real C++ under
// the RETAIL names, cross-asserted field by field against the PODs so the two
// descriptions cannot drift.
//
// NAMES. Read out of the shipped export table on 2026-09-18, not invented
// (B33_RULES §1, §2):
//   * the record is `Object::BaseTypeInfo` — there is no `Object::TypeInfo`
//     (`??0BaseTypeInfo@Object@@QEAA@PEBD00P6APEAVBase@1@XZP6APEAV21@PEAX@Z@Z`,
//      `?StripNamespace@BaseTypeInfo@Object@@QEAAXXZ`,
//      `?_firstTypeInfo@BaseTypeInfo@Object@@2PEAV12@EA`)
//   * `Object::Factory`'s surface is `?Get@`, `?Startup@`, `?CreateObject@` x2,
//     `?GetTypeIdFromName@`, `?GetTypeName@`, `?GetTypeIdForFileExtension@`,
//     `?IsDerivedFrom@`, `?IsTypeValid@`, `?GetParentType@`, `?GetChildType@`,
//     `?GetChildTypeCount@`, `?LoadCompilers@`/`?UnloadCompilers@`, private
//     `?GetTypeFromNameCRC@`, private static `?_singleton@`.
//   * the per-class reflection members retail's own macro emits are
//     `_TypeName`, `_ParentTypeName`, `Type`, `TypeId`, `TypeName`,
//     `CreateNewObject`, `CreateInPlaceObject`, `_typeInfo` — see DECLARE_TYPE.
// ############################################################################

#include "../collection/Collection.h"

namespace Object {

class Factory;
class BaseTypeInfo;

// ============================================================
// Object::Base — ENDGAME §B.1 decision 2, the first real class with a
// compiler-generated vtable.
//
// THE SLOT LIST IS MEASURED, NOT CHOSEN. `??1Base@Object@@UEAA@XZ` @ 0x1800c0360
// is `LEA RAX,[0x180154788]; MOV [RCX],RAX; RET`, so Base's vtable is at
// 0x180154788, and it has EXACTLY THREE SLOTS — bounded by
// `??_7External@Object@@6B@` starting 0x18 bytes later at 0x1801547a0.
// standalone/rt_data_mint_tables.inc's own generated row agrees independently:
// "??_7Base@Object@@6B@  DLL 0x180154788  3 slots".
//
//   [0] +0x00  0x1800065b0  vector deleting dtor (`MOV EDX,0x8` = elem size 8,
//                           `LEA R9,[0x1800c0360]` = ~Base)
//   [1] +0x08  0x18007ec28  _purecall  -> pure `TypeName()`
//   [2] +0x10  0x1800c0380  `XOR EAX,EAX; RET` -> `?Type@Base@Object@@UEBAIXZ`
//
// THE DESTRUCTOR IS VIRTUAL (the normal-virtual cutover, B3X W1). Retail's
// slot 0 is MSVC's deleting destructor, ONE slot; GCC spends TWO on
// `virtual ~T()` (D1 complete, D0 deleting), so retail slot N >= 1 is compiler
// slot N + 1 for every class in this hierarchy. Every caller reaches a slot by
// name; nothing reads the table by index, and the `// [N]` slot comments in
// the headers stay RETAIL slot numbers.
//
// Deletion is `delete p` (`Object::Destroy(p)` below is the same thing): the
// compiler's D0 runs the most-derived destructor chain and then this class's
// `operator delete`, retail's `flags & 1` free arm (MemoryAllocator::MemFree).
// `Destruct()` bodies end an object in place with an UNQUALIFIED `this->~T()`
// (retail's virtual slot-0 call with flags 0); a stack or embedded object is
// ended with a QUALIFIED `p->T::~T()`, never `delete`.
// ============================================================
class Base {
public:
    // slot 0. Retail: MSVC's vector deleting destructor 0x1800065b0 (flags&1 =
    // free the storage, flags&2 = array form). Here D1 + D0.
    virtual ~Base() = default;                          // ??1Base 0x1800c0360

    // Retail's `flags & 1` arm: MemoryAllocator::MemFree, one definition for
    // the whole hierarchy (src/engine/object/root_operator_delete.cpp; the
    // unit build frees with std::free). A class that declares its own (retail
    // has a `??3` per class, all MemFree) hides this one.
    static void operator delete(void* p);

    // slot 1. `?TypeName@X@Ns@@UEBAPEBDXZ` in every subclass; `_purecall` in
    // Base itself, so it is pure here. DECLARE_TYPE emits the override.
    virtual const char* TypeName() const = 0;

    // slot 2. `?Type@Base@Object@@UEBAIXZ` — retail's body is `XOR EAX,EAX;
    // RET`, i.e. an unregistered type answers 0. NOT named `GetTypeId`: the
    // export table says `Type`, and decision 5 says the export table wins.
    // The STATIC companion `TypeId()` is a different, non-virtual member that
    // DECLARE_TYPE emits.
    virtual uint32_t Type() const;

    // ?IsA@Base@Object@@QEBA_NI@Z            retail 0x1800491c0
    // An EXACT-type test, not a derivation test. Two instructions of substance.
    bool IsA(uint32_t typeId) const { return typeId == Type(); }

    // ?IsDerivedFrom@Base@Object@@QEBA_NI@Z  retail 0x1800c0390
    // `Factory::IsDerivedFrom(_singleton, this->Type(), typeId)`, inlined by
    // retail. ORDER: retail loads `_singleton` into RDI BEFORE the virtual
    // call (0x1800c039a-0x1800c03a6); the body preserves that.
    bool IsDerivedFrom(uint32_t typeId) const;

    // ?TypeId@Base@Object@@SAIXZ   retail 0x18007e010 — `MOV EAX,dword ptr
    // [0x1801f9c50]; RET`: the record's +0x00, 0xFFFFFFFF until
    // Factory::Startup. Defined inline below BaseTypeInfo.
    static uint32_t TypeId();
    static const BaseTypeInfo& GetTypeInfo();

protected:
    // Retail's ctor is the compiler's: one vptr store, which the compiler
    // emits for us. `= default` rather than a user-provided no-op (B31_RULES
    // class rule 5).
    Base() = default;

private:
    // ?_typeInfo@Base@Object@@0VBaseTypeInfo@2@A   retail 0x1801f9c50 — name
    // "Base", NO parent, no extension, NO factory pair (retail exports none
    // for Base: slot 1 is `_purecall`, slot 2 answers the literal 0). Not
    // DECLARE_TYPE: Base's slots 1/2 are not reads of this record. Defined in
    // src/engine/object/type_reflection.cpp (ENDGAME B.3.9 OBJ, B39_RULES D6
    // W3 -- the minted record is retired).
    static BaseTypeInfo _typeInfo;
};
static_assert(sizeof(Base) == 8, "Object::Base is one vtable pointer");

// `Object::Destroy(p)` — retail's slot-0 call with flags 1 (destroy, then free).
// Since the normal-virtual cutover (B3X W1) it is exactly `delete p`: the
// destructor is virtual, so the most-derived chain runs, then Base's
// `operator delete`.
inline void Destroy(Base* p) { delete p; }

// `Object::IsDerivedFromNullSafe(p, id)` — STANDALONE DEVIATION.
// Wraps `?IsDerivedFrom@Base@Object@@QEBA_NI@Z` (retail 0x1800c0390,
// `Base::IsDerivedFrom` above), whose body dereferences `this` unguarded. It is
// exactly the trampoline `EngineFuncsInit` (engine/funcs/engine_funcs.cpp,
// `IsDerivedFrom_NullSafe`) installs over `fn_IsDerivedFrom` today: a null
// object answers false, anything else is the registered body
// `RtObject_Base_IsDerivedFrom` = `reinterpret_cast<const Base*>(p)->
// IsDerivedFrom(id)`. (The trampoline's other arm, "no real body -> false", is
// unreachable: the export is always registered.) Retail never passes null;
// standalone dialogs keep ticking after an entity's weak ref is nulled, and
// dozens of ported call sites hand that ref straight in. The readers of
// `fn_IsDerivedFrom` convert to this helper (B3R_RULES §3) so no site changes
// behaviour; removing the deviation is not this step. ENDGAME B.3R, W0-P0c.
inline bool IsDerivedFromNullSafe(const Base* p, uint32_t typeId) {
    if (!p) return false;
    return p->IsDerivedFrom(typeId);
}

using TypeRecord = RtObject::TypeRecord;

// ============================================================
// The Set element: retail's `Collection::Pair<unsigned int, Object::Type>`.
//
// The KEY is the leading sub-object, which is `Collection::Set`'s own rule, and
// it is a DUPLICATE of `record.typeId` — the profile build asserts the two agree
// ("typePtr->_type == type && it->key == type", Object\Factory.cpp:0x8c).
// 0x30 bytes, so `Set<TypeEntry>::Node` is 0x50 and lands on the ported
// `AvlOps<Node, 0x30>` core (PortedRetrace48 / the two 48-wide rotations) —
// the same bodies `rtavl::TypeSetInsert` calls. The template WRAPS the ported
// core, it does not re-derive it (B32_RULES step 1).
// ============================================================
struct TypeEntry {
    uint32_t   typeId;    // +0x00  the key
    uint32_t   _pad04;    // +0x04  written by nobody; see FactoryStartupRun
    TypeRecord record;    // +0x08 .. +0x2f
};
static_assert(sizeof(TypeEntry) == 0x30, "the 0x30-byte Pair 0x180030140 builds");
static_assert(offsetof(TypeEntry, _pad04) == 0x4, "TypeEntry::_pad04");
static_assert(offsetof(TypeEntry, typeId) == 0x00, "TypeEntry::typeId is the key");
static_assert(offsetof(TypeEntry, record) == 0x08, "TypeEntry::record @ +0x08");

// ------------------------------------------------------------
// The node allocator seam.
//
// Retail INLINES `MemAlloc`/`MemFree` into Startup rather than calling them
// (type_system.cpp's "allocator boundary" banner), and the Set nodes come from
// the same allocator. This is a seam rather than `Collection::DefaultAllocator`
// for one reason only: `DefaultAllocator` binds to
// `Collection::detail::SetNodeAlloc`/`SetNodeFree`, which live in
// src/engine/collection/set_core.cpp and stand on the address registry — so a
// `Factory` built on it could not be linked into `make unit`. The two functions
// below are defined in src/engine/object/type_reflection.cpp, which has a
// malloc branch under -DBANISHED_UNIT_TEST and the registry door otherwise
// (the same shape src/engine/collection/linkedlist.cpp already uses).
//
// The policy does NOT affect the node layout, so the 0x50-byte node and its
// offsets are identical either way; it only decides which heap the bytes come
// from. The one behavioural consequence is that `AvlOps<Node,0x30>::Erase`
// takes its generic arm instead of the ported one — and nothing erases from
// this tree (retail's Factory has no remove path; teardown is Shutdown).
// ------------------------------------------------------------
void* TypeTreeAlloc(uint64_t bytes, int align);
void  TypeTreeFree(void* p);

struct TypeTreeAllocator {
    static void* Allocate(uint64_t bytes, int align) { return TypeTreeAlloc(bytes, align); }
    static void  Free(void* p)                       { TypeTreeFree(p); }
};

// The `Collection::Array<TypeRecord*>` growth inlined into Startup's pass 2 uses
// the SAME allocator, through its own pair so the two can be told apart in a
// leak report. Same definitions file, same unit-test branch.
void* TypeChildAlloc(uint64_t bytes, int align);
void  TypeChildFree(void* p);

// ============================================================
// Object::BaseTypeInfo — 0x38 bytes, one static per registered class.
//
// Same layout as RtObject::BaseTypeInfo, asserted against it below. Fields stay
// PUBLIC and named as the POD names them: every unconverted caller in the tree
// reads them at those names through a `reinterpret_cast`, and public members
// keep the class standard-layout so `offsetof` stays well defined (it is not
// polymorphic — nothing in Object's reflection record is virtual).
//
// NOT trivially destructible on purpose? NO — it IS trivially destructible, and
// must stay so: 300+ of these are file-scope statics and ~14 more are minted
// into `kStorageBytes` by standalone/rt_data_mint.cpp, which memsets them. A
// destructor here would run at exit over objects the mint owns.
// ============================================================
class BaseTypeInfo {
public:
    using FactoryFn = Base* (*)();
    using InPlaceFn = Base* (*)(void* storage);

    // ??0BaseTypeInfo@Object@@QEAA@PEBD00P6APEAVBase@1@XZP6APEAV21@PEAX@Z@Z
    // retail 0x180006320 / profile 0x1800234e0. Head-inserts into
    // `_firstTypeInfo` and strips the parent name's namespace.
    BaseTypeInfo(const char* name, const char* parentName, const char* fileExtension,
                 FactoryFn factory, InPlaceFn inPlace);

    // ??0BaseTypeInfo@Object@@QEAA@XZ — retail's default ctor. Exported, and
    // the mint's storage rows are its moral equivalent (zeroed bytes). Left
    // implicit-trivial deliberately: value-init zeroes, default-init leaves
    // indeterminate, and both are a strict superset of what retail's does to a
    // BSS static (B31_RULES class rule 5 forbids a user-provided no-op ctor).
    BaseTypeInfo() = default;

    // ?StripNamespace@BaseTypeInfo@Object@@QEAAXXZ — retail's own name for the
    // backward scan the ctor runs on `parentName`. Public in retail (`QEAA`).
    void StripNamespace();

    uint32_t    GetTypeId() const        { return typeId; }
    const char* GetName() const          { return name; }
    const char* GetParentName() const    { return parentName; }
    const char* GetFileExtension() const { return fileExtension; }

    // ?_firstTypeInfo@BaseTypeInfo@Object@@2PEAV12@EA — retail 0x1801fc6e0,
    // `2` = PUBLIC static. A PLAIN STATIC as of B.3.3 step 3: the ctor runs at
    // CRT-init, before the port registry exists, so it cannot resolve a cell —
    // it must own one. standalone/rt_data_mint.cpp routes the minted row's two
    // doors here so there is exactly ONE cell, not two (the "split brain" its
    // own banner names).
    static BaseTypeInfo* _firstTypeInfo;

    // ---- the 0x38 bytes, in retail order ----
    uint32_t      typeId;             // +0x00  0xFFFFFFFF until Factory::Startup
    uint32_t      _pad04;             // +0x04  never written by the ctor
    const char*   name;               // +0x08  CRC source; CONTENT-only, never
                                      //        compared by pointer
    const char*   parentName;         // +0x10  namespace-stripped
    const char*   fileExtension;      // +0x18  NOT an "abstract" flag
    FactoryFn     factoryFn;          // +0x20
    InPlaceFn     inPlaceFactoryFn;   // +0x28
    BaseTypeInfo* next;               // +0x30  intrusive chain
};
static_assert(sizeof(BaseTypeInfo) == 0x38, "sizeof(Object::BaseTypeInfo)");
static_assert(offsetof(BaseTypeInfo, typeId) == 0x0, "BaseTypeInfo::typeId");
static_assert(offsetof(BaseTypeInfo, _pad04) == 0x4, "BaseTypeInfo::_pad04");
static_assert(offsetof(BaseTypeInfo, name) == 0x8, "BaseTypeInfo::name");
static_assert(offsetof(BaseTypeInfo, parentName) == 0x10, "BaseTypeInfo::parentName");
static_assert(offsetof(BaseTypeInfo, fileExtension) == 0x18, "BaseTypeInfo::fileExtension");
static_assert(offsetof(BaseTypeInfo, factoryFn) == 0x20, "BaseTypeInfo::factoryFn");
static_assert(offsetof(BaseTypeInfo, inPlaceFactoryFn) == 0x28, "BaseTypeInfo::inPlaceFactoryFn");
static_assert(offsetof(BaseTypeInfo, next) == 0x30, "BaseTypeInfo::next");

inline uint32_t Base::TypeId() { return _typeInfo.typeId; }            // 0x18007e010
inline const BaseTypeInfo& Base::GetTypeInfo() { return _typeInfo; }

// ============================================================
// Object::Factory — 0x10 bytes: the Set's {root, count}.
//
// `??0Factory@Object@@QEAA@XZ` (0x1800c09d0) writes `[RCX]=0` and a DWORD
// `[RCX+8]=0` and publishes `_singleton`; the insert core INCs that dword at
// 0x1800303ac / 0x18003043c. So the whole object IS the Set header (the
// avl_trees.cpp group-A core takes `&_types._root`, the word at +0x00).
//
// NO DESTRUCTOR IS DECLARED and the implicit one is unreachable in practice:
// the singleton is heap-allocated once and never deleted (retail's teardown is
// `?Shutdown@Factory@Object@@QEAAXXZ`, not ~Factory). The implicit dtor would
// run `Set::~Set` -> `Clear`, which is what Shutdown does anyway; nothing in the
// tree stack-allocates one except the unit test, which wants exactly that.
//
// `_singleton` is NOT moved off the mint in this step (B.3.3 step 3 moves one
// cell, `_firstTypeInfo`, because the BaseTypeInfo ctor runs before the registry
// exists and cannot resolve). `Get()` reads the minted cell through the existing
// door, which is a RUNTIME read and therefore safe.
// ============================================================
class Factory {
public:
    using TypeSet = Collection::Set<TypeEntry, Collection::AVLCompare,
                                    TypeTreeAllocator, uint32_t>;

    Factory() : _types() {}

    // ?Get@Factory@Object@@SAPEAV12@XZ — `return _singleton;`.
    static Factory* Get();

    // ?_singleton@Factory@Object@@0PEAV12@EA — retail 0x1801fc6e8. The CLASS
    // STATIC since ENDGAME B.3.9 OBJ (B39_RULES D7): defined in
    // type_reflection.cpp, and RtObjectPortInit redirects the minted row onto
    // it, so the name door, the address door and this member are the same
    // eight bytes. Written by the ctor door (engine/singleton_ctors.cpp).
    // PUBLIC while foreign readers still reach the cell through the doors.
    static Factory* _singleton;

    // Runtime 0x1801fc6d0 — the HMODULE of the loaded compiler plugin, an
    // UNEXPORTED 8-byte cell (so the name is ours; the mint's old note calling
    // it IO::Modification's is wrong — INVENTORY_D finding 8). Read and
    // cleared by Factory::UnloadCompilers 0x1800bfad4, Factory::Shutdown
    // 0x1800c0604 and Engine::Shutdown 0x1800db62d. `void*`, not HMODULE: no
    // <windows.h> in this header (PLAN.md 4.1). The CLASS STATIC since
    // ENDGAME B.3.9 OBJ (B39_RULES D7), defined in factory_dtors.cpp;
    // RtObjectPortInit redirects the by-address mint row onto it. PUBLIC while
    // the two foreign by-address readers remain.
    static void* _compilerModule;

    // ?Startup@Factory@Object@@QEAA_NXZ   retail 0x1800c0630.
    bool Startup();

    // ?Shutdown@Factory@Object@@QEAAXXZ   retail 0x1800c0600, EIGHT instructions
    // and NOT a tree walk:
    //     RCX = *(void**)0x1801fc6d0;  if (RCX) FreeLibrary(RCX);
    //     *(void**)0x1801fc6d0 = 0;
    // 0x1801fc6d0 is the UNEXPORTED compiler-plugin HMODULE that
    // `LoadCompilers`/`UnloadCompilers` (0x1800bfad4) own; Shutdown never reads
    // `root`, `count` or `_singleton`. The type-tree teardown lives in
    // `~Factory` (0x1800071a0 -> 0x180008110), which is already ported as
    // `RtObject_Factory_Dtor` in src/engine/object/factory_dtors.cpp — and
    // this member's body is there too, because it needs the address door and a
    // platform `FreeLibrary` that type_reflection.cpp must not pull into
    // `make unit`.
    void Shutdown();

    // ?CreateObject@Factory@Object@@QEBAPEAVBase@2@I@Z      0x1800c0ae0
    // ?CreateObject@Factory@Object@@QEBAPEAVBase@2@IPEAX@Z  0x1800c0aa0
    Base* CreateObject(uint32_t typeId) const;
    Base* CreateObject(uint32_t typeId, void* storage) const;

    // ?GetTypeIdFromName@Factory@Object@@QEBAIPEBD_N@Z      0x1800c0580
    uint32_t    GetTypeIdFromName(const char* name, bool existingOnly) const;
    // ?GetTypeName@Factory@Object@@QEBAPEBDI@Z              0x1800c0a60
    const char* GetTypeName(uint32_t typeId) const;
    // ?GetTypeIdForFileExtension@Factory@Object@@QEBAIPEBD@Z 0x1800c04c0
    uint32_t    GetTypeIdForFileExtension(const char* extension) const;
    // ?IsDerivedFrom@Factory@Object@@QEBA_NII@Z             0x1800c0390 (inlined)
    bool        IsDerivedFrom(uint32_t typeId, uint32_t baseTypeId) const;
    // ?IsTypeValid@Factory@Object@@QEBA_NI@Z
    bool        IsTypeValid(uint32_t typeId) const;
    // ?GetParentType@Factory@Object@@QEBAII@Z
    uint32_t    GetParentType(uint32_t typeId) const;
    // ?GetChildTypeCount@Factory@Object@@QEBAHI@Z
    int32_t     GetChildTypeCount(uint32_t typeId) const;
    // ?GetChildType@Factory@Object@@QEBAIIH@Z
    uint32_t    GetChildType(uint32_t typeId, int32_t index) const;

    // ?GetTypeFromNameCRC@Factory@Object@@AEBAIPEBD@Z  0x1800c0930.
    // `AEBA` is PRIVATE — and private is not unexported (ordinal 2989). `this`
    // is read by nothing; the CRC depends only on the name.
    uint32_t GetTypeFromNameCRC(const char* name) const;

    const TypeSet& GetTypes() const { return _types; }
    TypeSet&       GetTypes()       { return _types; }

    // PUBLIC for the same reason Collection::Set's three members are: the
    // unconverted C bodies and the layout probe below reach it directly.
    TypeSet _types;   // +0x00 .. +0x0f
};
static_assert(sizeof(Factory) == 0x10, "Object::Factory is MemAlloc(0x10,4)");
static_assert(offsetof(Factory, _types) == 0x00, "Factory IS the Set header");

// The node the Set builds must be retail's 0x50-byte TypeNode, field for field.
// This is the check that the template wraps the RIGHT ported core: a 0x30-byte
// element is what selects `AvlOps<Node, 0x30>`.
static_assert(sizeof(Factory::TypeSet::Node) == 0x50, "TypeNode is 0x50 (0x180008440)");
static_assert(offsetof(Factory::TypeSet::Node, element) == 0x00, "TypeNode key @ +0x00");
static_assert(offsetof(Factory::TypeSet::Node, parent)  == 0x30, "TypeNode::parent @ +0x30");
static_assert(offsetof(Factory::TypeSet::Node, left)    == 0x38, "TypeNode::left @ +0x38");
static_assert(offsetof(Factory::TypeSet::Node, right)   == 0x40, "TypeNode::right @ +0x40");
static_assert(offsetof(Factory::TypeSet::Node, balance) == 0x48, "TypeNode::balance is i16 @ +0x48");
static_assert(offsetof(Factory::TypeSet::Node, height)  == 0x4a, "TypeNode::height is u16 @ +0x4a");
static_assert(sizeof(Factory::TypeSet::Node) == sizeof(RtObject::TypeNode),
              "the real node and the POD describe the same bytes");
static_assert(offsetof(Factory::TypeSet::Node, parent) == offsetof(RtObject::TypeNode, parent), "alias parent");
static_assert(offsetof(Factory::TypeSet::Node, left)   == offsetof(RtObject::TypeNode, left),   "alias left");
static_assert(offsetof(Factory::TypeSet::Node, right)  == offsetof(RtObject::TypeNode, right),  "alias right");

// The type-id hash, as ONE function (RtObject::TypeIdFromName forwards here).
uint32_t TypeNameCRC(const char* name);

// ############################################################################
// DECLARE_TYPE / DEFINE_TYPE — ENDGAME §B.1 decision 4.
//
// This is RETAIL'S OWN MACRO, member for member. The evidence is that its
// expansion is legible in the export table as one contiguous per-class block;
// `Achievement::Achievement`'s runs 0x180001000..0x1800011f0:
//
//   ?_TypeName@Achievement@1@CAPEBDXZ                 private static
//   ?_ParentTypeName@Achievement@1@CAPEBDXZ           private static
//   ?Type@Achievement@1@UEBAIXZ                       vtable slot 2
//   ?TypeId@Achievement@1@SAIXZ                       public static
//   ?TypeName@Achievement@1@UEBAPEBDXZ                vtable slot 1
//   ??2Achievement@0@SAPEAX_K@Z / _KPEAX@Z            operator new x2
//   ??3Achievement@0@SAXPEAX@Z                        operator delete
//   ?CreateNewObject@Achievement@1@SAPEAVBase@Object@@XZ
//   ?CreateInPlaceObject@Achievement@1@SAPEAVBase@Object@@PEAX@Z
//   ?_typeInfo@Achievement@1@0VBaseTypeInfo@Object@@A private static
//
// `Type()` and `TypeName()` ARE emitted as of step 4, now that `Object::Base`
// is a real class to override. Their bodies are retail's: `MOV EAX,[dword]`
// and `MOV RAX,[qword]` over the class's own `_typeInfo` record, which is where
// the two globals retail reads live.
//
// THE THREE `operator new`/`delete` OVERLOADS ARE NOT EMITTED PER CLASS, and
// this is a decision, not an omission. Retail's route every allocation through
// `System::MemoryAllocator` (`??2X@Ns@@SAPEAX_K@Z` is `MemAlloc(size, 4)`,
// `??3X@Ns@@SAXPEAX@Z` is `MemFree` verbatim — proved for 88 classes in
// src/engine/object/class_operator_delete.cpp and ui_operator_delete.cpp).
// Since the normal-virtual cutover `Object::Base` declares the one
// `operator delete` (MemFree) that every class's deleting destructor inherits;
// `operator new` stays undeclared (objects are built with placement new on
// MemAlloc'd storage), and a class that needs its own declares it.
//
// `X::TypeId()` returns `_typeInfo.typeId`, which is 0xFFFFFFFF until
// `Factory::Startup` runs — B33_RULES §2.5: it is NOT self-initialising, and
// the pre-Startup ordering traps in src/game/Game/game_main.cpp depend on
// seeing the sentinel.
//
// USAGE (both halves are required, and DEFINE_TYPE goes in exactly one .cpp):
//
//     class Goal : public Object::External {
//         DECLARE_TYPE(Goal)
//     };
//     DEFINE_TYPE(Achievement::Goal, "Object::External", nullptr)
//
// The `Parent` and `Ext` arguments are STRINGS, not types: the chain is keyed by
// name because the parent's record may not exist yet when this static runs
// (`BaseTypeInfo::name` is content-only). The ctor strips the parent's
// namespace, so "Object::External" and "External" are the same key.
//
// DECLARE_TYPE_INHERITED_INPLACE(Class) — the same block WITHOUT the class's
// own `CreateInPlaceObject`, for the classes whose retail record carries the
// BASE's in-place factory: `UI::Element` and `UI::DialogInstance` register
// `?CreateInPlaceObject@Internal@Object@@SAPEAVBase@2@PEAX@Z` (0x1800070c0)
// and export no `?CreateInPlaceObject@` of their own. `DEFINE_TYPE`'s
// `&Class::CreateInPlaceObject` then names the inherited static by ordinary
// member lookup — which is what retail's macro did (ENDGAME B.3.9 TI-UI).
// ############################################################################
#define DECLARE_TYPE(Class)                                                      \
public:                                                                          \
    static ::Object::Base* CreateInPlaceObject(void* storage);                     \
    DECLARE_TYPE_INHERITED_INPLACE(Class)

#define DECLARE_TYPE_INHERITED_INPLACE(Class)                                    \
public:                                                                          \
    /* vtable slot 1 — retail `MOV RAX,[qword]; RET` over this class's own    */ \
    /* record. `BaseTypeInfo::name` is content-only: never compared by pointer.*/ \
    const char* TypeName() const override { return _typeInfo.name; }               \
    /* vtable slot 2 — retail `MOV EAX,[dword]; RET`. 0xFFFFFFFF until        */ \
    /* Factory::Startup, exactly as TypeId() is.                              */ \
    uint32_t Type() const override { return _typeInfo.typeId; }                    \
    static ::Object::Base* CreateNewObject();                                     \
    static uint32_t TypeId() { return _typeInfo.typeId; }                          \
    static const ::Object::BaseTypeInfo& GetTypeInfo() { return _typeInfo; }       \
private:                                                                          \
    static const char* _TypeName();                                               \
    static const char* _ParentTypeName();                                         \
    static ::Object::BaseTypeInfo _typeInfo;

// The statics `DECLARE_TYPE` promises, plus the one object whose construction
// IS the registration. `_TypeName`/`_ParentTypeName` are retail's own accessors
// over the two literals; they are emitted here rather than in the class so the
// literals live once, next to the record they feed — and the record is
// initialised THROUGH them, so each literal has exactly one spelling and
// neither accessor can drift from what was registered. (A private static
// member's initialiser is in the class's scope, so it may call them.)
#define DEFINE_TYPE(Class, ParentName, FileExt)                                   \
    const char* Class::_TypeName()       {                                         \
        return ::Object::detail::LastComponent(#Class); }                           \
    const char* Class::_ParentTypeName() { return (ParentName); }                  \
    ::Object::BaseTypeInfo Class::_typeInfo(                                       \
        Class::_TypeName(), Class::_ParentTypeName(), (FileExt),                   \
        &Class::CreateNewObject, &Class::CreateInPlaceObject);

// `#Class` is spelled with its namespace at the DEFINE_TYPE site
// (`Achievement::Goal`), but `BaseTypeInfo::name` is the BARE class name — it is
// the CRC source and the saves key on it, so "Goal" and "Achievement::Goal" are
// different type ids and only the first is right. The ctor strips the PARENT
// name only (retail's `StripNamespace`), so the class name is stripped here, at
// compile time, by the same rule.
namespace detail {
constexpr const char* LastComponent(const char* s) {
    const char* last = s;
    for (const char* p = s; *p; ++p)
        if (*p == ':') last = p + 1;
    return last;
}
}  // namespace detail

}  // namespace Object

// ==== B3R W0-P0c: Runtime bodies reached by retail address ====
// ENDGAME B.3R (B3R_RULES R5): each body below is registered at the retail
// address in its trailing comment and read through the registry today. It is
// declared here, copied from its definition (linkage, namespace, types), so the
// reader can call it by name. Nothing calls it through this declaration yet.

namespace Object { class InternalFactory; class ExternalFactory; }
namespace RtObjFactoryDtor {
// ---- object/factory_dtors.cpp
void RtObject_Factory_Dtor(::Object::Factory* self);  // 0x1800071a0
void RtObject_InternalFactory_Dtor(::Object::InternalFactory* self);  // 0x18002f860
void RtObject_ExternalFactory_Dtor(::Object::ExternalFactory* self);  // 0x1800be760
}  // namespace RtObjFactoryDtor
// ==== end B3R W0-P0c ====
