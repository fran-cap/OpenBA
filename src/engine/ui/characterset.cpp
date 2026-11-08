// ============================================================================
// src/engine/ui/characterset.cpp — UI::CharacterSet, the whole class
// ============================================================================
//
// ??_7CharacterSet@UI@@6B@ (retail 0x180156220), nine slots:
//   0  deleting dtor      0x1800378a0  CharacterSet.h (the class's own, B.3.9)
//   1  TypeName           0x180037450  here
//   2  Type               0x180037430  here
//   3  GetFileVersion     0x1800cb830  here  (returns 1)
//   4  External::Compile  0x1800066e0  already ported (the base's slot)
//   5  Serialize          0x1800cb7b0  here
//   6  Reconstruct        0x180037650  here
//   7  Destruct           0x180037710  here
//   8  Construct          0x1800376b0  here
// plus ??1CharacterSet@UI@@UEAA@XZ 0x1800cb840 and the factory pair, here.
//
// LAYOUT, sizeof 0x28 (`MOV EDX,0x28` in the deleting destructor's array arm at
// 0x1800378d2, and the `Allocate(0x28, 8)` in the already-ported factory row):
//     +0x00  vtable        Object::Base -> Object::External -> UI::CharacterSet
//     +0x08 .. +0x1f       Object::External's three words
//     +0x20  System::String   the one member: the character-set string
// The field is NOT named beyond that here. Serialize proves it is one
// System::String and the destructor proves the class OWNS it; neither says what
// the characters mean, and one class is not enough to name a field.
//
// THE CONSTRUCTOR. Retail has no out-of-line `??0CharacterSet@UI@@`: the seven
// stores are INLINED into `Construct` (0x1800376b0), `Reconstruct`
// (0x180037650) and both factory statics. ENDGAME B.3.9 block UI-D (B39_RULES
// D1/D2): they are the class's REAL constructor (CharacterSet.h — the three
// vptr stores are the compiler's), every live CharacterSet wears the
// COMPILER's table, and the slot doors, the ??1 door and the FACTUI1 row whose
// only reader was the minted `??_7CharacterSet@UI@@6B@` are retired with it.
// ============================================================================
#include <cstddef>
#include <cstdint>

#include "io/Serializer.h"
#include "io/Stream.h"
#include "system/String.h"
#include "ui/CharacterSet.h"
#include <new>

namespace {

// Serialize builds the real IO::Serializer (??0Serializer 0x1800b9960 /
// ??1Serializer 0x1800b99b0), which retail inlines.

}  // namespace

// ############################################################################
// UI::CharacterSet's member bodies (ENDGAME B.3.6 block D). Their own
// `namespace UI { }` block after the anonymous namespace has closed, because a
// member definition must appear in a namespace ENCLOSING the class (B34_BRIEFS
// A.2); the anonymous helpers above are reachable from here, same TU.
// ############################################################################
namespace UI {

// [3] 0x1800cb830: `MOV EAX,0x1 ; RET`. Version 1 — the plainest in the family;
// the *Description classes run 0x104..0x10204. A LITERAL, never derived.
int32_t CharacterSet::GetFileVersion() const { return 1; }

// [8] 0x1800376b0 — the inlined constructor, then `MOV RAX,RCX`. (Retail's
// null test sat in the retired door; a virtual call always has a `this`.)
Object::External* CharacterSet::Construct() {
    return ::new (this) CharacterSet();                      // 1800376c6..1800376fe
}
// [6] 0x180037650: slot0(this, 0), then the same inlined constructor. Slot 0 is
// dispatched VIRTUALLY on purpose: a subclass's deleting destructor is what
// has to run.
Object::External* CharacterSet::Reconstruct() {
    this->~CharacterSet();                                         // 180037667
    return ::new (this) CharacterSet();                      // 18003766e..1800376a6
}
// [7] 0x180037710: `MOV RAX,[RCX] ; XOR EDX,EDX ; JMP [RAX]`.
void CharacterSet::Destruct() { this->~CharacterSet(); }

// ?CreateNewObject@CharacterSet@UI@@SAPEAVBase@Object@@XZ  0x180037540
// The FACTUI1 row this replaces: the locked MemAlloc of 0x28 bytes, align 8;
// retail's null branch; then the inlined constructor.
Object::Base* CharacterSet::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(CharacterSet), 8);
    if (p == nullptr) return nullptr;
    return ::new (p) CharacterSet();
}
// ?CreateInPlaceObject@CharacterSet@UI@@SAPEAVBase@Object@@PEAX@Z  0x1800375f0
Object::Base* CharacterSet::CreateInPlaceObject(void* place) {
    if (place == nullptr) return nullptr;
    return ::new (place) CharacterSet();
}

// ============================================================================
// [5] ?Serialize@CharacterSet@UI@@UEAA_NAEAVStream@IO@@@Z   0x1800cb7b0
// ============================================================================
// The shortest Serialize in the UI family: make the reader/writer from the
// stream's encoding factory, hand the one String to System::String::Serialize
// (0x1800c9930), delete the reader/writer, return true.
//
// The read/write side is chosen by `stream->mode == 0` exactly as everywhere
// else in this family: factory vtable +0x18 for the reader, +0x20 for the
// writer (0x1800cb7e5 / 0x1800cb7ea).
bool CharacterSet::Serialize(IO::Stream& streamv) {
    IO::Stream* stream = &streamv;

    // [RSP+0x28], [RSP+0x30]; 1800cb7d0..1800cb7ed the factory by _direction.
    IO::Serializer ser(*stream, 0);

    _characters.Serialize(ser);                               // 1800cb7fb

    // 1800cb806..1800cb813: ~Serializer, `if (rw) delete rw`.
    return true;                                             // 1800cb815 MOV AL,1
}

// ??1CharacterSet@UI@@UEAA@XZ   0x1800cb840 — retail's body, moved here from
// the retired `RtUi_CharacterSet_Dtor` door (ENDGAME B.3.9 UI-D). Its whole
// content is the release of the String at +0x20 (1800cb860..1800cb88a:
// System::String's own refcount protocol, the count at `s[-4]`, the block at
// `s[-8]`, retail's asymmetry), then the tail JMP to ~External (0x1800cb892).
// B4 X-RH: `_characters` is a System::String member, so the release is the
// member's destructor, which the compiler runs after this (empty) body and
// before ~External: retail's order.
CharacterSet::~CharacterSet() {
}

}  // namespace UI

// ============================================================================
// ENDGAME B.3.9 TI-UI (B39 D6): UI::CharacterSet's own type record, built at CRT init
// (head-insert into Object::BaseTypeInfo::_firstTypeInfo). Literals copied from
// the retired minted row ?_typeInfo@CharacterSet@UI@@ (0x1801f9cf8): name "CharacterSet", parent
// "Object::External" (retail ?_ParentTypeName@CharacterSet@UI@@), no file extension; factories:
// the class's own.
// ============================================================================
DEFINE_TYPE(UI::CharacterSet, "Object::External", nullptr)
