// ============================================================================
// src/engine/ui/labeldescription.cpp — UI::LabelDescription, the whole class
// ============================================================================
//
// ??_7LabelDescription@UI@@6B@ (retail 0x180156b30), ten slots:
//   0  deleting dtor      0x180042730  LabelDescription.h (the class's own)
//   1  TypeName           0x1800423b0  here
//   2  Type               0x180042390  here
//   3  GetFileVersion     0x1800d2950  here  (returns 0x2304)
//   4  External::Compile  0x1800066e0  already ported
//   5  Serialize          0x1800d2820  here
//   6  Reconstruct        0x180042530  here
//   7  Destruct           0x180042560  here
//   8  CreateInstance     0x1800d26b0  here
//   9  Construct          0x180042550  here
// plus ??1LabelDescription@UI@@UEAA@XZ (0x1800d2960) and the factory pair.
//
// ENDGAME B.3.6 block C3: every slot body is a `UI::LabelDescription` member
// (src/engine/ui/LabelDescription.h) and the doors delegate with a
// QUALIFIED call. It DERIVES FROM UI::TextDescription, a real class in the same
// block now, so Serialize's base call is the qualified member
// `UI::TextDescription::Serialize`. The TextDescription applier
// (0x18011ef70, element_bodies_b5.cpp's non-slot door) is still reached by
// address.
//
// ENDGAME B.3.9 block UI-A (B39_RULES D1/D2): the class is CONCRETE. The
// out-of-line ctor (`??0LabelDescription@UI@@QEAA@XZ`, 0x1800d29d0) is the
// real one in the header; ~LabelDescription chains ~TextDescription as a
// compiler base call; the factory pair is the class's own. The slot doors, the
// ??1 door and ui_factory_wave28.cpp's ??0 row, read only by the retired
// minted table, its RT_DELDTOR row and the interpreter, are gone.
//
// GetFileVersion is 0x2304 — the largest constant in this batch, and read from
// this class's own body. It is NOT its base's.
// ============================================================================

#include "description_common.h"
#include "ui/LabelDescription.h"
#include "ui/LabelElement.h"
#include "ui/SpriteSheet.h"
#include "system/String.h"

namespace {

using namespace rtdesc;

}  // namespace

namespace UI {

int32_t LabelDescription::GetFileVersion() const { return 0x2304; }

// 0x180042550: null test (it sat in the retired door), then a JMP to the ctor.
Object::External* LabelDescription::Construct() {
    return ::new (this) LabelDescription();                  // 180042559 JMP
}
Object::External* LabelDescription::Reconstruct() {
    this->~LabelDescription();                                         // 18004253e
    return ::new (this) LabelDescription();                  // 180042548 JMP
}
void LabelDescription::Destruct() { this->~LabelDescription(); }

// ============================================================================
// ?Serialize@LabelDescription@UI@@UEAA_NAEAVStream@IO@@@Z   0x1800d2820
// ============================================================================
// Base is TextDescription::Serialize (0x18011ee30), QUALIFIED. THE SPRITE PAIR
// COMES FIRST, BEFORE THE SIX DWORDS — offset order would put +0x90..+0xa4
// first, and the byte stream a .rsc parses is this order, not that one.
bool LabelDescription::Serialize(IO::Stream& streamv) {
    IO::Stream* stream = &streamv;

    UI::TextDescription::Serialize(*stream);                  // 1800d283a

    IO::Serializer ser(*stream, 0);  // ??0Serializer (0x1800b9960) inlined
    IO::ReaderWriter* rw = ser._readerWriter;

    Object::SerializeExternal<UI::SpriteSheet>(ser, _spriteSheet); // 1800d287b ExternalPtr<SpriteSheet>
    _spriteName.Serialize(ser);           // 1800d288c System::String

    rw->SerializeUInt(_textAlignment, nullptr);                     // 1800d28a3
    rw->SerializeUInt(_labelPosition, nullptr);                     // 1800d28bb
    rw->SerializeInt(_textWidth, nullptr);                      // 1800d28d3
    rw->SerializeInt(_spacing, nullptr);                      // 1800d28eb
    rw->SerializeInt(_imageWidth, nullptr);                      // 1800d2903
    rw->SerializeInt(_imageHeight, nullptr);                      // 1800d291b

    // ~Serializer (0x1800b99b0) inlined here: rw's scalar deleting dtor                  // 1800d2934
    return true;
}

// ============================================================================
// ?CreateInstance@LabelDescription@UI@@...   retail 0x1800d26b0  (slot 8)
// ============================================================================
// RETSLOT (B6-2). LabelElement is 0x130 and its constructor is OUT OF LINE
// (0x1800d2620, the real ctor since B.3.9). The base applier is TextDescription's (0x18011ef70,
// element_bodies_b5.cpp's door, reached by address) and it DOES take the Dialog.
//
// THE SPRITE ASSIGNMENT IS GUARDED BY A CHANGE TEST, and the two halves of the
// pair are compared SEPARATELY (`CMP qword [RDI+0x100],RBX` then
// `CMP dword [RDI+0x108],EAX`) before the mark:
//     idx = sheet ? sheet->GetSpriteIndexFromName(_spriteName) : -1
//     if (elem->sheet != sheet || elem->index != idx) mark the chain
//     elem->sheet = sheet ; elem->index = idx     <- STORED UNCONDITIONALLY
// and the index goes in as a QWORD read from a slot only a DWORD was written to
// (0x1800d275f writes `dword [RSP+0x30]`, 0x1800d279d reads `qword`).
//
// The last copy is `ElementDescription::_id` (+0x3c) into +0x128 (_iconColor).
Object::InternalPtr<Element>* LabelDescription::CreateInstance(
        Object::InternalPtr<Element>* ret, const Dialog* dialog) const {
    void* raw = MemAlloc(0x130, 8);                          // 1800d26f2

    LabelElement* elem = nullptr;                            // the null allocation stays null
    if (raw != nullptr)                                      // 1800d271d
        elem = ::new (raw) LabelElement();                   // 1800d2722 ??0LabelElement

    Object::InternalPtr<TextElement> slot(elem);             // 1800d272a
    TextDescription::CreateInstance(&slot, dialog);          // 1800d273a  0x18011ef70

    {
        SpriteSheet* sheet = _spriteSheet;                   // 1800d273f
        int32_t idx;
        if (sheet != nullptr) {                              // 1800d2749
            idx = sheet->GetSpriteIndexFromName(_spriteName);  // 1800d2755
        } else {
            idx = -1;                                        // 1800d275c OR EAX,-1
        }
        uint64_t idxSlot;                                    // [RSP+0x30]
        __builtin_memcpy(&idxSlot, &idx, sizeof idx);        // 1800d275f DWORD store

        if (elem->_sprite.spriteSheet != sheet ||                         // 1800d2763
            elem->_sprite.spriteIndex != idx) {                           // 1800d276c
            elem->SetNeedsSizeCalc();                                     // 1800d2780
        }
        elem->_sprite.spriteSheet = sheet;                                // 1800d2796
        __builtin_memcpy(&elem->_sprite.spriteIndex, &idxSlot, 8);        // 1800d27a2 QWORD
    }

    elem->_align        = static_cast<int32_t>(_textAlignment);     // 1800d27a9 .. 1800d27eb
    elem->_mode         = _labelPosition;
    elem->_minTextWidth = _textWidth;
    elem->_gap          = _spacing;
    elem->_iconWidth    = _imageWidth;
    elem->_iconHeight   = _imageHeight;

    elem->_iconColor = _id;                                  // 1800d27f1

    *ret = Object::InternalPtr<Element>(elem);                   // 1800d27fa
    return ret;
}

}  // namespace UI

// ============================================================================
// ??1LabelDescription@UI@@UEAA@XZ   0x1800d2960 -- retail's body, moved here
// from the retired door (ENDGAME B.3.9 UI-A)
// ============================================================================
// Stamp own vtable (the compiler's), release the sprite name at +0xb0 through
// System::String's own Release, TAIL-JUMP to ~TextDescription (0x18011eae0) --
// the compiler's base call, which restamps and releases +0x78 and +0x60. The
// +0xa8 ExternalPtr is not released.
namespace UI {
// B4 Y: the +0xb0 release (1800d2980) is the member `_spriteName`'s ~String,
// run after this (empty) body and before ~TextDescription.
LabelDescription::~LabelDescription() {
}
}  // namespace UI

namespace UI {
// ?CreateNewObject@LabelDescription@UI@@SAPEAVBase@Object@@XZ  0x1800424a0
// The factory-interpreter row this replaces: MemAlloc(0xb8, 8), retail's
// null branch, then the out-of-line ctor ??0LabelDescription (0x1800d29d0).
Object::Base* LabelDescription::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(LabelDescription), 8);
    if (p == nullptr) return nullptr;
    return ::new (p) LabelDescription();
}
// ?CreateInPlaceObject@LabelDescription@UI@@SAPEAVBase@Object@@PEAX@Z  0x180042520
Object::Base* LabelDescription::CreateInPlaceObject(void* place) {
    if (place == nullptr) return nullptr;
    return ::new (place) LabelDescription();
}
}  // namespace UI

// ============================================================================
// ENDGAME B.3.9 TI-UI (B39 D6): UI::LabelDescription's own type record, built at CRT init
// (head-insert into Object::BaseTypeInfo::_firstTypeInfo). Literals copied from
// the retired minted row ?_typeInfo@LabelDescription@UI@@ (0x1801fa008): name "LabelDescription", parent
// "TextDescription" (retail ?_ParentTypeName@LabelDescription@UI@@), no file extension; factories:
// the class's own.
// ============================================================================
DEFINE_TYPE(UI::LabelDescription, "TextDescription", nullptr)
