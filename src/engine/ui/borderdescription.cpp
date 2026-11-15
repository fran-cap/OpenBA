// ============================================================================
// src/engine/ui/borderdescription.cpp — UI::BorderDescription, the whole class
// ============================================================================
//
// ??_7BorderDescription@UI@@6B@ (retail 0x180156980), ten slots:
//   0  deleting dtor      0x180040ff0  BorderDescription.h (the class's own)
//   1  TypeName           0x1800409e0  here
//   2  Type               0x1800409c0  here
//   3  GetFileVersion     0x18010fe10  here  (returns 0x104)
//   4  External::Compile  0x1800066e0  already ported
//   5  Serialize          0x18010fe20  here
//   6  Reconstruct        0x180040b60  here
//   7  Destruct           0x180040b90  here
//   8  CreateInstance     0x18010ff40  here
//   9  Construct          0x180040b80  here
// plus ??1BorderDescription@UI@@UEAA@XZ (0x18010fb20) and the factory pair.
//
// ENDGAME B.3.9 block UI-A (B39_RULES D1/D2): the class is CONCRETE and every
// live BorderDescription wears the COMPILER's table. The real ctor
// (`??0BorderDescription@UI@@QEAA@XZ` 0x18010fa50) is in the header; the
// factory pair `?CreateNewObject@` (0x180040ad0) / `?CreateInPlaceObject@`
// (0x180040b50) is defined and registered here, replacing the FACTUI1 row.
// The slot doors and the ??1 door, whose only reader was the minted
// `??_7BorderDescription@UI@@6B@`, are retired with it.
//
// Layout: src/engine/ui/BorderDescription.h over DescriptionsBatch1.h.
// sizeof 0xb8.
//
// THE +0x68 ExternalPtr IS NOT RELEASED BY THE DESTRUCTOR — only the nine
// System::Strings and the base's _textName. That is retail's, transcribed, not
// an omission; the same is true of ~ImageDescription.
// ============================================================================
#include <new>

#include "description_common.h"
#include "system/String.h"
#include "ui/BorderDescription.h"
#include "ui/BorderElement.h"

namespace {

using namespace rtdesc;

}  // namespace

namespace UI {

// ============================================================================
// The trivial slots
// ============================================================================
// 0x18010fe10: `MOV EAX,0x104 ; RET`. Per-class constant; read from this body,
// not inherited from a sibling.
int32_t BorderDescription::GetFileVersion() const { return 0x104; }

// ============================================================================
// Construct / Reconstruct / Destruct
// ============================================================================
// 0x180040b80: `TEST RCX,RCX ; JNZ <ctor> ; XOR EAX,EAX ; RET` — then a TAIL
// JUMP into the constructor, whose return value (this) is this function's.
// (The null test sat in the retired door; a virtual call always has a `this`.)
Object::External* BorderDescription::Construct() {
    return ::new (this) BorderDescription();                    // 180040b78 JMP
}
// 0x180040b60: slot 0 with flag 0 (destroy WITHOUT freeing), then the ctor
// again, with NO null test — retail derefs `this` to reach the vtable before it
// could test it, so a null here faults in retail too.
Object::External* BorderDescription::Reconstruct() {
    this->~BorderDescription();                                            // 180040b6e
    return ::new (this) BorderDescription();                    // 180040b78 JMP
}
// 0x180040b90: `MOV RAX,[RCX] ; XOR EDX,EDX ; JMP [RAX]`.
void BorderDescription::Destruct() { this->~BorderDescription(); }

// ??1BorderDescription@UI@@UEAA@XZ   0x18010fb20 — retail's body, moved here
// from the retired `RtUi_BorderDescription_Dtor` door (ENDGAME B.3.9 UI-A):
// the NINE sprite names released in DESCENDING order (+0xb0 down to +0x70).
// The entry restamp is the compiler's; so is the chain into
// ~ElementDescription (which releases `_textName` at +0x60) and ~External
// (retail's tail JMP at 0x18010fd2d). The +0x68 ExternalPtr is NOT released —
// retail's, see the banner.
// Each release is System::String's own (?Release@String@System@@ 0x1800c9870,
// the idiom inlined here), with retail's asymmetry: the freeing branch leaves
// the field at `data - 8`, the sharing branch nulls it.
// B4 Y: the nine releases (18010fb45 .. 18010fcca, [8] down to [0]) are the
// member array's ~String calls, run after this (empty) body in reverse
// element order, before ~ElementDescription.
BorderDescription::~BorderDescription() {
}

// ?CreateNewObject@BorderDescription@UI@@SAPEAVBase@Object@@XZ  0x180040ad0
// The FACTUI1 row this replaces: `?MemAlloc@MemoryAllocator@System@@`
// (0x1800c7a50, the locked wrapper Object::detail::InternalMemAlloc reaches) of
// 0xb8 bytes, align 8; retail's null branch; then the constructor.
Object::Base* BorderDescription::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(BorderDescription), 8);
    if (p == nullptr) return nullptr;
    return ::new (p) BorderDescription();
}
// ?CreateInPlaceObject@BorderDescription@UI@@SAPEAVBase@Object@@PEAX@Z  0x180040b50
Object::Base* BorderDescription::CreateInPlaceObject(void* place) {
    if (place == nullptr) return nullptr;
    return ::new (place) BorderDescription();
}

// ============================================================================
// ?Serialize@BorderDescription@UI@@UEAA_NAEAVStream@IO@@@Z   0x18010fe20
// ============================================================================
//   ElementDescription::Serialize(this, stream)         0x180116b00, QUALIFIED
//   pair = { stream, nullptr }
//   rw   = stream->mode == 0 ? enc->vt[0x18] : enc->vt[0x20]
//   ExternalPtr<SpriteSheet>::Serialize(&_spriteSheet, &pair)   0x180004590
//   System::String::Serialize over +0x70 .. +0xb0, ascending   0x1800c9930
//   if (rw) rw->vt[0](rw, 1)
//   return true
//
// NO `if (reading)` arm and no array: every field is fixed. The nine field
// serializers take the PAIR, not the reader (`LEA RDX,[RSP+0x28]`), which is
// why it has to live on this frame.
bool BorderDescription::Serialize(IO::Stream& streamv) {
    IO::Stream* stream = &streamv;

    ElementDescription::Serialize(*stream);                  // 18010fe3a

    IO::Serializer ser(*stream, 0);  // ??0Serializer (0x1800b9960) inlined: [RSP+0x28], [RSP+0x30]  18010fe62 / 18010fe67

    Object::SerializeExternal<UI::SpriteSheet>(ser, _spriteSheet); // 18010fe78
    for (int i = 0; i < 9; ++i)                             // 18010fe7d .. 18010ff0b
        _spriteName[i].Serialize(ser);                      // 0x1800c9930

    // ~Serializer (0x1800b99b0) inlined here: rw's scalar deleting dtor                 // 18010ff23
    return true;                                            // MOV AL,1
}

// ============================================================================
// ?CreateInstance@BorderDescription@UI@@...   retail 0x18010ff40   (slot 8)
// ============================================================================
// *** THE HIDDEN RETURN SLOT. *** This returns `Object::InternalPtr<UI::Element>`
// BY VALUE, so the Microsoft x64 ABI inserts a slot pointer as the FIRST
// argument after `this`: RCX = this, RDX = the return slot (RULE B6-2 — the
// first declared parameter). R8 (the Dialog*) is never read by this body — it
// creates no children — so it is declared and ignored, exactly as the bytes do.
//
// BorderElement is 0x150 bytes and its constructor is OUT OF LINE
// (`??0BorderElement@UI@@QEAA@XZ`, 0x18010fd40, the class's ctor in
// BorderElement.h), unlike ImageDescription's, which is inlined at its call
// site. Retail takes the ctor's RETURN value as the element, not the raw
// allocation (`MOV RDI,RAX` at 0x18010ffb4).
//
// Then NINE identical blocks, one per sprite name:
//     sheet = this->_spriteSheet                    (RE-READ every iteration)
//     idx   = sheet ? GetSpriteIndexFromName(sheet, &_spriteName[i]) : -1
//     elem->sprite[i].sheet = sheet                 (+0xc0 + 0x10*i)
//     elem->sprite[i].index = idx   <- QWORD store from a DWORD slot (+0xc8 + 0x10*i)
// ONE STACK SLOT SERVES ALL NINE ([RBP-0x10]); its high dword is never written
// by anything, so every one of the nine qword stores carries four arbitrary
// bytes into the Sprite's pad. See `SpriteFromName` in description_common.h —
// the WIDTH is what is faithful, which is why each of the nine is built as a
// whole `Sprite` and copied.
Object::InternalPtr<Element>* BorderDescription::CreateInstance(
        Object::InternalPtr<Element>* ret, const Dialog* /*dialog*/) const {
    void* raw = MemAlloc(0x150, 8);                        // 18010ff80

    BorderElement* elem = nullptr;                         // the null allocation stays null
    if (raw != nullptr)                                    // 18010ffaa
        elem = ::new (raw) BorderElement();                // 18010ffaf ??0BorderElement

    Object::InternalPtr<Element> slot(elem);               // 18010ffb7  [RBP+0x20]
    UI::ElementDescription::CreateInstance(                   // 18010ffc2
        &slot);

    // The nine sprites are consecutive 0x10-byte members from +0xc0; retail
    // walks them with one cursor. Each gets {sheet, index} with the index slot
    // stored as a QWORD (rtdesc::SpriteFromName).
    ::Sprite* const sprites[9] = {
        &elem->_topLeft, &elem->_top, &elem->_topRight,
        &elem->_left, &elem->_centre, &elem->_right,
        &elem->_bottomLeft, &elem->_bottom, &elem->_bottomRight };
    for (int i = 0; i < 9; ++i)                            // 18010ffc7 .. 1801101a0
        *sprites[i] = SpriteFromName(_spriteSheet, _spriteName[i]);

    new (static_cast<void*>(ret)) Object::InternalPtr<Element>(elem);   // 1801101a7
    return ret;                                            // MOV RAX,R14
}

}  // namespace UI

// ============================================================================
// ENDGAME B.3.9 TI-UI (B39 D6): UI::BorderDescription's own type record, built at CRT init
// (head-insert into Object::BaseTypeInfo::_firstTypeInfo). Literals copied from
// the retired minted row ?_typeInfo@BorderDescription@UI@@ (0x1801fa890): name "BorderDescription", parent
// "ElementDescription" (retail ?_ParentTypeName@BorderDescription@UI@@), no file extension; factories:
// the class's own.
// ============================================================================
DEFINE_TYPE(UI::BorderDescription, "ElementDescription", nullptr)
