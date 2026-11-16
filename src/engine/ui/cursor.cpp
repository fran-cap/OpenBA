// ============================================================================
// src/engine/ui/cursor.cpp — UI::Cursor, the whole class
// ============================================================================
//
// ??_7Cursor@UI@@6B@ (retail 0x180156570), nine slots:
//   0  deleting dtor      0x18003b3a0  Cursor.h (the class's own, B.3.9)
//   1  TypeName           0x18003af80  here
//   2  Type               0x18003af60  here
//   3  GetFileVersion     0x1800cd1b0  here  (returns 1)
//   4  External::Compile  0x1800066e0  already ported (the base's slot)
//   5  Serialize          0x1800cd110  here
//   6  Reconstruct        0x18003b180  here
//   7  Destruct           0x18003b240  here
//   8  Construct          0x18003b1e0  here
// plus ??1Cursor@UI@@UEAA@XZ 0x1800cd1c0 and the factory pair, here.
//
// LAYOUT, sizeof 0x30 (`MOV EDX,0x30` in the deleting destructor's array arm at
// 0x18003b3d2, and the locked MemAlloc of 0x30 in CreateNewObject, 0x18003b070):
//     +0x00  vtable        Object::Base -> Object::External -> UI::Cursor
//     +0x08 .. +0x1f       Object::External's three words
//     +0x20  int32         serialized, NOT initialised by the constructor
//     +0x24  int32         serialized, NOT initialised by the constructor
//     +0x28  ExternalPtr<UI::SpriteSheet>   (serialized by 0x180004590)
//
// TWO FIELDS THE CONSTRUCTOR DOES NOT WRITE. +0x20 and +0x24 are read and
// written by Serialize, and the constructor (0x18003b1f6..0x18003b22a, seven
// stores, the last of them `[RCX+0x28] = 0`) never touches them: a freshly
// constructed Cursor carries whatever the allocator left there until something
// deserialises into it. That is retail's behaviour, not a transcription slip.
//
// ~Cursor DOES NOT RELEASE +0x28. Its whole body is a vtable restamp and a tail
// jump to ??1External@Object@@ (0x1800cd1d2..0x1800cd1e0, four instructions);
// the ExternalPtr at +0x28 is left alone. Compare ~CharacterSet next door,
// which does release its one member. Faithful, and stated here because "the
// destructor must release the owning pointer" is exactly the assumption that
// would otherwise get a helpful line added.
//
// THE CONSTRUCTOR: there is no out-of-line `??0Cursor@UI@@` — it is inlined
// into Construct, Reconstruct and both factory statics. ENDGAME B.3.9 block
// UI-D: it is the class's REAL constructor (Cursor.h), every live Cursor wears
// the COMPILER's table, and the slot doors, the ??1 door and the FACTUI1 row
// whose only reader was the minted `??_7Cursor@UI@@6B@` are retired with it.
// ============================================================================
#include <cstddef>
#include <cstdint>

#include "io/Serializer.h"
#include "io/Stream.h"
#include "ui/Cursor.h"
#include "object/External.h"
#include "ui/SpriteSheet.h"   // B.4 P2 X: T::GetTypeInfo() for Object::SerializeExternal<T>
#include <new>

namespace {

// Serialize builds the real IO::Serializer (??0Serializer 0x1800b9960 /
// ??1Serializer 0x1800b99b0), which retail inlines.

}  // namespace

// ############################################################################
// UI::Cursor's member bodies (ENDGAME B.3.6 block D) — after the anonymous
// namespace, inside `namespace UI` (B34_BRIEFS A.2).
// ############################################################################
namespace UI {

// [3] 0x1800cd1b0: `MOV EAX,0x1 ; RET`.
int32_t Cursor::GetFileVersion() const { return 1; }

// [8] 0x18003b1e0 — the inlined constructor, then `MOV RAX,RCX`. (Retail's
// null test sat in the retired door; a virtual call always has a `this`.)
Object::External* Cursor::Construct() {
    return ::new (this) Cursor();                         // 18003b1f6..18003b22e
}
// [6] 0x18003b180: slot0(this, 0) — VIRTUALLY — then the same inlined ctor.
Object::External* Cursor::Reconstruct() {
    this->~Cursor();                                      // 18003b197
    return ::new (this) Cursor();                         // 18003b19e..18003b1d6
}
// [7] 0x18003b240: `MOV RAX,[RCX] ; XOR EDX,EDX ; JMP [RAX]`.
void Cursor::Destruct() { this->~Cursor(); }

// ?CreateNewObject@Cursor@UI@@SAPEAVBase@Object@@XZ  0x18003b070
// The FACTUI1 row this replaces: the locked MemAlloc of 0x30 bytes, align 8;
// retail's null branch; then the inlined constructor.
Object::Base* Cursor::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(Cursor), 8);
    if (p == nullptr) return nullptr;
    return ::new (p) Cursor();
}
// ?CreateInPlaceObject@Cursor@UI@@SAPEAVBase@Object@@PEAX@Z  0x18003b120
Object::Base* Cursor::CreateInPlaceObject(void* place) {
    if (place == nullptr) return nullptr;
    return ::new (place) Cursor();
}

// ============================================================================
// [5] ?Serialize@Cursor@UI@@UEAA_NAEAVStream@IO@@@Z   0x1800cd110
// ============================================================================
//   rw = encoding factory, read (+0x18) or write (+0x20) side by stream->mode
//   ExternalPtr<UI::SpriteSheet>::Serialize(&this->_spriteSheet, &ser)  0x180004590
//   rw->vt[0xa0](rw, &this->_hotSpotX, 0)
//   rw->vt[0xa0](rw, &this->_hotSpotY, 0)
//   if (rw) rw->vt[0](rw, 1)
//   return true
//
// THE SHEET COMES FIRST and the two ints after it — the reverse of the order
// the fields sit in. Sequence, not layout, is what a stream records, and this
// is the one place the difference is visible.
bool Cursor::Serialize(IO::Stream& streamv) {
    IO::Stream* stream = &streamv;

    // [RSP+0x28], [RSP+0x30]; 1800cd130..1800cd14d the factory by _direction.
    IO::Serializer ser(*stream, 0);
    IO::ReaderWriter* rw = ser._readerWriter;

    // ExternalPtr<UI::SpriteSheet>::Serialize (0x180004590) over the real
    // IO::Serializer (External.h).
    _spriteSheet.Serialize(ser);  // 1800cd15b

    rw->SerializeInt(_hotSpotX, nullptr);
    rw->SerializeInt(_hotSpotY, nullptr);

    // 1800cd190: ~Serializer, `if (rw) delete rw`.
    return true;                                          // 1800cd19f MOV AL,1
}

// ??1Cursor@UI@@UEAA@XZ   0x1800cd1c0 — restamp, then tail-jump ~External
// (0x1800cd1d2..0x1800cd1e0): NOTHING of Cursor's own is released. The empty
// body is retail's; the compiler chains the base (ENDGAME B.3.9 UI-D: the
// retired `RtUi_Cursor_Dtor` door did the same by hand).
Cursor::~Cursor() {}

}  // namespace UI

// ============================================================================
// ENDGAME B.3.9 TI-UI (B39 D6): UI::Cursor's own type record, built at CRT init
// (head-insert into Object::BaseTypeInfo::_firstTypeInfo). Literals copied from
// the retired minted row ?_typeInfo@Cursor@UI@@ (0x1801f9e10): name "Cursor", parent
// "Object::External" (retail ?_ParentTypeName@Cursor@UI@@), no file extension; factories:
// the class's own.
// ============================================================================
DEFINE_TYPE(UI::Cursor, "Object::External", nullptr)
