// ============================================================================
// src/engine/object/external_class.cpp — ENDGAME B.3.3 step 5 (EXTERNAL)
// ============================================================================
//
// `Object::External`, `Object::CompiledExternal` and the registry-free half of
// `Object::ExternalList`, as real C++ classes deriving from the real
// `Object::Base`. The layouts, the measured vtable slot lists and the naming
// evidence are in src/engine/object/External.h; the design rules are
// docs/history/endgame_b/B33_RULES.md §1.4, §6, §7 and B31_RULES' two-step
// shim pattern.
//
// WHY THIS FILE IS SEPARATE FROM external.cpp, AND FROM ITS OWN SIBLING
// ---------------------------------------------------------------------
// Exactly the split src/engine/object/type_reflection.cpp made against
// type_system.cpp at step 3: of the `external*.cpp` files only this one is in
// the unit build (tests/unit/sources.cmake). The CLASSES have to be
// unit-testable (B33_RULES §9 step 5 asks for the slot order, the
// Type()/TypeName() chain, `Destroy(p)` and a tree find at two depths), so the
// bodies that need nothing but memory live here and the rest stay in the other
// `external*.cpp` files.
//
// `external_class_doors.cpp` is the other half: every member whose body is a
// door into an already-ported C function. It is NOT in the unit build.
//
// NOTHING HERE IS REGISTERED. ENDGAME B.3.9 OBJ: every External-derived class
// is a real class with a compiler table; the minted External /
// CompiledExternal / ExternalList tables, their RT_DELDTOR rows and the
// ??0 / ??1 External doors are retired.
// ============================================================================

#include <cstddef>
#include <cstdint>

#include "object/External.h"
#include "system/string_internal.h"       // RtSystem::StringRefCount / RtMemFree

namespace Object {

// ============================================================================
// Object::External
// ============================================================================

// ??0External@Object@@QEAA@XZ   retail 0x1800bc9c0
//
// Retail stamps `??_7Base@Object@@6B@` and then immediately overwrites it with
// `??_7External@Object@@6B@`. That IS the inlined `Object::Base` constructor
// followed by External's own stamp, and a real constructor chain emits both
// stores for us — which is why they are not transcribed. The object
// carries the compiler's table.
//
// The zero stores retail makes at +0x08, +0x10, +0x18 are the members' own
// constructors, run in declaration order before this body: `_path`'s
// (Filename(), 0x1800bc9f0) and the Array's three fields. Restating them would
// be a second write of the same zeros.
External::External() {
}

// ??1External@Object@@UEAA@XZ   retail 0x1800bc950
//
// Retail's order, preserved exactly:
//   1. stamp External's vtable  — a real destructor chain emits this on entry
//      to ~External, so a virtual called during teardown dispatches to
//      External's override and not the subclass's. Not hand-written, because a
//      hand-written store would name the MINTED table and a converted object
//      carries the compiler's.
//   2. CALL 0x180006b80 -> 0x180006ac0: release each element's leading String,
//      then free the buffer and zero the header. B.4 Z MIRROR: that is the
//      compiler-emitted `~_refs` over its row kSpec_ExtArrayElem (destroy =
//      0x180006ac0, then ArrayDtorImpl), run once after this (empty) body; the
//      hand-written element loop that stood here is gone. See External.h's
//      member banner.
//   3. release `_path` — retail's inlined release IS `~Filename` (0x180006980).
//      `_path` is a `System::Filename` member (B4 X-R3): the compiler destroys
//      it after `~_refs`, i.e. after this body, which is retail's 0x1800bc97a.
//   4. stamp Base's vtable — `~Base()` running after this body emits it.
//
// The element release (0x180006ac0, kD_Str_10) is retail's String idiom
// verbatim, including the store of `p - 8` on the free branch, the release
// asymmetry src/engine/system/String.h documents.
External::~External() {
}

// ?_typeInfo@External@Object@@0VBaseTypeInfo@2@A — 0x1801f9b38. ENDGAME
// B.3.9 OBJ (B39_RULES D6 W3): name "External", parent literal "Base" EXACTLY
// as the retail record carries it, no extension. Slot 1 TypeName (0x1800066c0,
// `MOV RAX,[0x1801f9b40]`, the record's name, content-only), slot 2 Type
// (0x1800066d0, `MOV EAX,[0x1801f9b38]`, 0xFFFFFFFF until Factory::Startup)
// and TypeId (0x180029350) are DECLARE_TYPE's over it. The minted record is
// retired in the same commit.
DEFINE_TYPE(Object::External, "Base", nullptr)

// ?CreateNewObject@External@Object@@SAPEAVBase@2@XZ          0x18002c980
// ?CreateInPlaceObject@External@Object@@SAPEAVBase@2@PEAX@Z  0x18002c990
// Both `XOR EAX,EAX; RET` (the FACTREST row had no steps): the class is
// abstract.
Base* External::CreateNewObject()          { return nullptr; }
Base* External::CreateInPlaceObject(void*) { return nullptr; }

// slot 4 — ?Compile@External@Object@@UEAA?AW4CompileResult@12@AEBVFilename@System@@@Z
//          retail 0x1800066e0, whose whole body is `MOV EAX,0x1; RET`.
//
// External's virtual DEFAULT: the base class cannot compile anything, so it
// answers CompileResult(1) and every resource type that can compile overrides
// the slot. `path` is genuinely dead — RDX is never read in the two
// instructions — and is kept because the slot's signature demands it.
//
// (This slot is what B33_RULES §1.4 calls "name UNKNOWN, keep it as Slot4()".
// It has a retail name; External.h's slot banner carries the evidence.)
External::CompileResult External::Compile(const System::Filename& /*path*/) {
    return 1u;
}

// ?GetPath@External@Object@@QEBA?AVFilename@System@@XZ   retail 0x180006250
//
// One line in retail: the Filename copy constructor from `this->_path`, which
// SHARES the buffer and bumps its refcount — it does not copy the characters.
// The hidden return slot the C entry point carries is the ABI's, not the
// language's: a real member returning by value has it emitted for it.
System::Filename External::GetPath() const {
    return System::Filename(_path);
}

// ============================================================================
// Object::CompiledExternal
// ============================================================================

// ?_typeInfo@CompiledExternal@Object@@0VBaseTypeInfo@2@A — 0x1801f9b00.
// ENDGAME B.3.9 OBJ (B39_RULES D6 W3): name "CompiledExternal", parent literal
// "External" EXACTLY as the retail record carries it, no extension. Slots 1/2
// and TypeId() (0x18000b6b0 / 0x18000b690 / 0x18000b6a0) are DECLARE_TYPE's
// over it. The minted record is retired in the same commit.
DEFINE_TYPE(Object::CompiledExternal, "External", nullptr)

// ?CreateNewObject@CompiledExternal@Object@@SAPEAVBase@2@XZ          0x18000b6c0
// ?CreateInPlaceObject@CompiledExternal@Object@@SAPEAVBase@2@PEAX@Z  0x18000b6d0
// Both `XOR EAX,EAX; RET` (the FACTREST row had no steps): the class is
// abstract.
Base* CompiledExternal::CreateNewObject()          { return nullptr; }
Base* CompiledExternal::CreateInPlaceObject(void*) { return nullptr; }

// ??1CompiledExternal@Object@@UEAA@XZ   retail 0x1800bb980
//
// Seven instructions: re-stamp `??_7CompiledExternal@Object@@6B@`
// (0x180154a30), then TAIL-JMP ~External. Both are the destructor chain's, so
// the body is empty — and the re-stamp is NOT redundant tidying being dropped:
// GCC emits the same store on entry to ~CompiledExternal, naming the compiler's
// table instead of the minted one, which is the whole point of the class.
CompiledExternal::~CompiledExternal() {}

// ============================================================================
// Object::ExternalList's reflection record — ENDGAME B.3.9 OBJ (B39_RULES D6
// W3). `?_typeInfo@ExternalList@Object@@0VBaseTypeInfo@2@A` (0x1801f9be0),
// name "ExternalList", parent literal "Object::External" EXACTLY as the retail
// record carries it (the ctor strips it to "External"), no file extension.
// The factory pair it names is external_list.cpp's. The minted record is
// retired in the same commit.
// ============================================================================
DEFINE_TYPE(Object::ExternalList, "Object::External", nullptr)

// ============================================================================
// Object::ExternalList's members ARE NOT HERE, and that is a LINK decision, not
// an oversight. GCC emits a class's vtable in the translation unit that defines
// its KEY FUNCTION (the first non-inline virtual in declaration order), and that
// vtable references EVERY slot. ExternalList's destructor and slots 3 and 6-8
// are in external_class_doors.cpp, slot 5 (`Serialize`) and the factory pair in
// external_list.cpp; neither file is a unit-test input.
//
// `Object::External` itself has no such problem: its four unimplemented slots
// are PURE in retail too (`_purecall` at 0x18007ec28), so its vtable references
// `__cxa_pure_virtual` and nothing else.
// ============================================================================

}  // namespace Object
