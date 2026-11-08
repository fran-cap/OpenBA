// ============================================================================
// src/engine/ui/graphdescription.cpp — UI::GraphDescription, the whole class
// ============================================================================
//
// ??_7GraphDescription@UI@@6B@ (retail 0x180156a30), ten slots:
//   0  deleting dtor      0x1800403a0  src/engine/object/deleting_dtor.cpp row
//   1  TypeName           0x180040090  here
//   2  Type               0x180040070  here
//   3  GetFileVersion     0x1800d1b20  here  (returns 0x104)
//   4  External::Compile  0x1800066e0  already ported
//   5  Serialize          0x1800d1a30  here
//   6  Reconstruct        0x180040210  here
//   7  Destruct           0x180040240  here
//   8  CreateInstance     0x1800d1920  here
//   9  Construct          0x180040230  here
// plus ??1GraphDescription@UI@@UEAA@XZ (0x1800d1b30).
//
// The constructor is OUT OF LINE (`??0GraphDescription@UI@@QEAA@XZ`,
// 0x1800d1bd0) and is ALREADY ported by src/engine/ui/factory_ui1.cpp.
//
// ENDGAME B.3.6 block C5: every slot body MOVED into the members of the real
// `UI::GraphDescription` (src/engine/ui/GraphDescription.h, cross-asserted
// against DescriptionsBatch1.h's POD); each registered door is a one-line
// QUALIFIED delegation. The dtor stays a door (B4b-2).
// ============================================================================
#include <new>

#include "description_common.h"
#include "ui/GraphDescription.h"
#include "ui/GraphElement.h"

namespace {

using namespace rtdesc;

}  // namespace

namespace UI {

// [3] 0x1800d1b20
int32_t GraphDescription::GetFileVersion() const { return 0x104; }

// [9] 0x180040230 — `TEST RCX,RCX ; JZ` then a JMP to the ctor: placement
// (the null test sat in the retired door; a virtual call has a `this`).
Object::External* GraphDescription::Construct() {
    return ::new (this) GraphDescription();                            // 180040239 JMP
}
// [6] 0x180040210 — destroy in place through slot 0 (flag 0), re-run the ctor.
Object::External* GraphDescription::Reconstruct() {
    this->~GraphDescription();                                                    // 18004021e slot 0
    return ::new (this) GraphDescription();                            // 180040228 JMP
}
// [7] 0x180040240
void GraphDescription::Destruct() { this->~GraphDescription(); }

// ============================================================================
// [5] ?Serialize@GraphDescription@UI@@UEAA_NAEAVStream@IO@@@Z   0x1800d1a30
// ============================================================================
// THREE ints then ONE UNSIGNED. +0x74 goes through the kScalarUInt slot (+0x98)
// while +0x68/+0x6c/+0x70 go through kScalarInt (+0xa0); the split is read off
// 0x1800d1acd versus 0x1800d1a8e/0x1800d1aa3/0x1800d1ab8, not assumed.
bool GraphDescription::Serialize(IO::Stream& streamArg) {
    IO::Stream* stream = &streamArg;

    // QUALIFIED: this object wears the LEAF's table, whose slot 5 is this body.
    ElementDescription::Serialize(*stream);                              // 1800d1a4a

    IO::Serializer ser(*stream, 0);  // ??0Serializer (0x1800b9960) inlined
    IO::ReaderWriter* rw = ser._readerWriter;

    rw->SerializeInt(_width, nullptr);                                  // 1800d1a8e
    rw->SerializeInt(_height, nullptr);                                 // 1800d1aa3
    rw->SerializeInt(_tickSize, nullptr);                               // 1800d1ab8
    rw->SerializeUInt(_lineColor, nullptr);                             // 1800d1acd

    Object::SerializeExternal<UI::SpriteSheet>(ser, _spriteSheet);            // 1800d1adc ExternalPtr<SpriteSheet>
    _spriteName.Serialize(ser);             // 1800d1aed System::String

    // ~Serializer (0x1800b99b0) inlined here: rw's scalar deleting dtor                             // 1800d1b05
    return true;
}

// ============================================================================
// [8] ?CreateInstance@GraphDescription@UI@@...   retail 0x1800d1920
// ============================================================================
// GraphElement is 0x100 and its constructor is OUT OF LINE
// (`??0GraphElement@UI@@QEAA@XZ`, 0x1800d1880, already ported by
// src/engine/ui/factory_ui1.cpp).
//
// THE FOUR DWORD COPIES ARE NOT IN OFFSET ORDER: +0x68 -> +0xc0, +0x6c -> +0xc4,
// then +0x74 -> +0xcc, and only THEN +0x70 -> +0xc8. Read off
// 0x1800d19ab..0x1800d19c9; reordering them would be a different function under
// any interruption.
Object::InternalPtr<Element>* GraphDescription::CreateInstance(
        Object::InternalPtr<Element>* ret, const Dialog*) const {
    void* raw = MemAlloc(0x100, 8);                                     // 1800d1961

    GraphElement* elem = nullptr;                                       // the null allocation stays null
    if (raw != nullptr)                                                 // 1800d198c
        elem = ::new (raw) GraphElement();                              // 1800d1991 ??0 (out of line)

    Object::InternalPtr<Element> slot(elem);                                                  // 1800d1999
    // The NON-virtual ElementDescription::CreateInstance(InternalPtr<Element>)
    // (0x180115ad0): a QUALIFIED member call since fragment pass F2.
    UI::ElementDescription::CreateInstance(                   // 1800d19a6
        &slot);

    elem->_width     = _width;                                          // 1800d19ae
    elem->_height    = _height;                                         // 1800d19b7
    elem->_lineColor = static_cast<int32_t>(_lineColor);                // 1800d19c0
    elem->_tickSize  = _tickSize;                                       // 1800d19c9

    // 1800d19cf..1800d19e2 the sheet and the index slot; 1800d19f5 / 1800d1a01
    // the sheet and a QWORD store of the slot (rtdesc::SpriteFromName).
    elem->_lineSprite = SpriteFromName(_spriteSheet, _spriteName);

    new (static_cast<void*>(ret)) Object::InternalPtr<Element>(elem);   // 1800d1a08
    return ret;
}

}  // namespace UI

namespace UI {

// ??1GraphDescription@UI@@UEAA@XZ   0x1800d1b30 — retail's body, moved from the
// retired door (ENDGAME B.3.9 UI-B): the +0x80 String; the base restamp, the
// base's +0x60 String and the tail JMP into ~External are the compiler's chain.
GraphDescription::~GraphDescription() {
    // B4 Y RT-UI: the +0x80 release is the member `_spriteName`'s ~String,
    // run after this (empty) body and before ~ElementDescription.
}

// ?CreateNewObject@GraphDescription@UI@@SAPEAVBase@Object@@XZ 0x180040180 --
// `?MemAlloc@MemoryAllocator` (0x1800c7a50) of 0x88, align 8; retail's null
// branch; the ctor. Replaces the factory interpreter row (B39_RULES D2).
Object::Base* GraphDescription::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(GraphDescription), 8);
    if (p == nullptr) return nullptr;
    return ::new (p) GraphDescription();
}
// ?CreateInPlaceObject@GraphDescription@UI@@SAPEAVBase@Object@@PEAX@Z 0x180040200
Object::Base* GraphDescription::CreateInPlaceObject(void* place) {
    if (place == nullptr) return nullptr;
    return ::new (place) GraphDescription();
}

}  // namespace UI

// ============================================================================
// ENDGAME B.3.9 TI-UI (B39 D6): UI::GraphDescription's own type record, built at CRT init
// (head-insert into Object::BaseTypeInfo::_firstTypeInfo). Literals copied from
// the retired minted row ?_typeInfo@GraphDescription@UI@@ (0x1801f9f98): name "GraphDescription", parent
// "ElementDescription" (retail ?_ParentTypeName@GraphDescription@UI@@), no file extension; factories:
// the class's own.
// ============================================================================
DEFINE_TYPE(UI::GraphDescription, "ElementDescription", nullptr)
