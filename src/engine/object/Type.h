#pragma once
// ============================================================================
// src/engine/object/Type.h — Object::Type, the 4-byte type-id value class
// ============================================================================
//
// ENDGAME B.3.7 block B3 (Achievement) added this header, OUTSIDE its file set,
// because RULE B7-4 declares `Goal::GetInstanceType` as returning
// `Object::Type*` and no `Object::Type` existed anywhere in the tree (every
// earlier port spelled it `uint32_t`). Nothing else about `Object` changes.
//
// WHAT RETAIL SAYS ABOUT IT (read, not guessed):
//   * `?GetInstanceType@Goal@Achievement@@UEBA?AVType@Object@@XZ` 0x18009eeb0
//     returns it BY VALUE through a hidden return slot (`?AV` = a class):
//     `MOV EAX,[GoalInstance::_typeInfo]; MOV [RDX],EAX; MOV RAX,RDX; RET` —
//     one DWORD store, so the class is four bytes.
//   * `?Serialize@Type@Object@@QEAAXAEAVSerializer@IO@@@Z` 0x1800c0c40
//     serializes exactly that dword through the ReaderWriter's uint slot
//     (src/engine/object/type_system.cpp, `RtObject_Type_Serialize`).
//   * `??BType@Object@@QEBAIXZ` is its `operator unsigned int` (the
//     type_system.cpp banner quotes it).
//
// Only the layout and the conversion are declared. `Serialize` is the free
// function `RtObject_Type_Serialize` in type_system.cpp (the Object owner's
// file, declared at the bottom of this header) and is not re-declared as a
// member here.
//
// A VALUE TYPE: trivially copyable and trivially destructible, asserted, so a
// `Type` in a raw hidden-return slot is written with one dword store exactly
// as retail writes it.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include <type_traits>

namespace Object {

class Type {
public:
    Type() = default;
    explicit Type(uint32_t typeId) : _type(typeId) {}

    // ??BType@Object@@QEBAIXZ — `operator unsigned int`.
    operator uint32_t() const { return _type; }

    uint32_t _type;   // +0x00  the CRC type id (BaseTypeInfo::typeId)
};

static_assert(sizeof(Type) == 4, "Object::Type is one dword (Goal::GetInstanceType 0x18009eeb0)");
static_assert(alignof(Type) == 4, "Object::Type aligns to 4");
static_assert(offsetof(Type, _type) == 0, "Object::Type::_type @ +0x00");
static_assert(std::is_trivially_copyable<Type>::value,
              "written into a raw hidden-return slot by one dword store");
static_assert(std::is_trivially_destructible<Type>::value, "value type, no teardown");

}  // namespace Object

// ==== B3R W0-P0c: Runtime bodies called by name ====
// ENDGAME B.3R (B3R_RULES R5): each body below carries its retail address in its
// trailing comment. It is declared here, copied from its definition (linkage,
// namespace, types), so the reader can call it by name.

namespace IO { class Serializer; }

namespace RtObject {
// ---- object/type_system.cpp
// `self` is the 4-byte type id: every caller's field is a uint32_t (or an
// Object::Type, which passes its `_type`). Every caller hands a local IO::Serializer.
void RtObject_Type_Serialize(uint32_t* self, ::IO::Serializer* serializer);  // 0x1800c0c40
}  // namespace RtObject
// ==== end B3R W0-P0c ====
