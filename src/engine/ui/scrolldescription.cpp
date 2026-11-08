// ============================================================================
// src/engine/ui/scrolldescription.cpp — UI::ScrollDescription, whole class
// ============================================================================
//
// Vtable batch 2. Derives straight from UI::ElementDescription.
//
// ??_7ScrollDescription@UI@@6B@ (retail 0x180156460), ten slots:
//   0  deleting dtor      0x18003a790  deleting_dtor.cpp row (sizeof 0xc8)
//   1  TypeName           0x18003a120  here
//   2  Type               0x18003a100  here
//   3  GetFileVersion     0x18011a930  here  (returns 0x104)
//   4  External::Compile  0x1800066e0  already ported
//   5  Serialize          0x18011a940  here
//   6  Reconstruct        0x18003a2a0  here
//   7  Destruct           0x18003a2d0  here
//   8  CreateInstance     0x18011aac0  here
//   9  Construct          0x18003a2c0  here
// plus ??0ScrollDescription@UI@@QEAA@XZ 0x18011a4e0 (out of line) and
// ??1ScrollDescription@UI@@UEAA@XZ 0x18011a5d0.
//
// LAYOUT, sizeof 0xc8 (`MOV EDX,0xc8` at 0x18003a7af). ElementDescription
// (0x68) plus:
//     +0x68  ExternalPtr<UI::SpriteSheet>      0x180004590
//     +0x70 .. +0xb0  NINE System::Strings — the batch's largest sprite-name
//                     block; each is looked up against the one sheet at +0x68
//     +0xb8  bool    ctor 1   kScalarBool
//     +0xbc  int32   ctor 1   kScalarInt
//     +0xc0  uint32  \  ctor writes ONE QWORD of -1 at +0xc0 (0x18011a5ab),
//     +0xc4  uint32  /  so BOTH start at 0xffffffff
// and both are kScalarUInt (vt+0x98), not kScalarInt.
//
// Every helper is already ported, including ??0ScrollElement@UI@@ (0x18011a7f0,
// ui_factory_wave28.cpp), so this file adds none.
// ============================================================================
#include <new>

#include "io/Stream.h"
#include <cstddef>
#include <cstdint>

#include "ui/ScrollDescription.h"
#include "ui/ScrollElement.h"
#include "io/Serializer.h"
#include "ui/Dialog.h"
#include "ui/SpriteSheet.h"
#include "system/String.h"
#include "system/MemoryAllocator.h"
#include "collection/Collection.h"

// ENDGAME B.3.6 block C4: the layout moved to src/engine/ui/
// ScrollDescription.h (the real class). Every slot body below is a MEMBER; each
// registered door is a one-line QUALIFIED delegation. The ctor and dtor stay
// DOORS (B4b-2). RULE B6-5: every export the members reach is cached in a
// namespace-scope std::atomic slot, never in a function-local static.



// ============================================================================
// The member bodies (ENDGAME B.3.6 block C4)
// ============================================================================
namespace UI {

int32_t ScrollDescription::GetFileVersion() const { return 0x104; }

// 0x18003a2c0 — the ctor, placement (the null test sat in the retired door).
Object::External* ScrollDescription::Construct() {
    return ::new (this) ScrollDescription();
}
// 0x18003a2a0 — slot 0 (VIRTUAL) with flag 0, then the ctor.
Object::External* ScrollDescription::Reconstruct() {
    this->~ScrollDescription();
    return ::new (this) ScrollDescription();
}
// 0x18003a2d0
void ScrollDescription::Destruct() { this->~ScrollDescription(); }

// ?Serialize@ScrollDescription@UI@@UEAA_NAEAVStream@IO@@@Z  0x18011a940
bool ScrollDescription::Serialize(IO::Stream& streamv) {
    IO::Stream* stream = &streamv;

    UI::ElementDescription::Serialize(*stream);                 // 18011a95a (qualified)

    // The inlined ??0Serializer (0x1800b9960): CreateReader / CreateWriter
    // of the stream's encoding, version 0.
    IO::Serializer ser(*stream, 0);
    IO::ReaderWriter* const rw = ser._readerWriter;

    Object::SerializeExternal<UI::SpriteSheet>(ser, _spriteSheet);   // 18011a998
    for (int i = 0; i < 9; ++i)                                // 18011a9a6 .. 18011aa2b
        _spriteNames[i].Serialize(ser);

    rw->SerializeBool(_vertical, nullptr);
    rw->SerializeInt(_inset, nullptr);
    // BOTH of the last two are kScalarUInt (vt+0x98), not kScalarInt.
    rw->SerializeUInt(_barColor, nullptr);
    rw->SerializeUInt(_highlightColor, nullptr);

    // The inlined ~Serializer deletes the ReaderWriter (18011aa96).
    return true;
}

// ============================================================================
// ?CreateInstance@ScrollDescription@UI@@...   retail 0x18011aac0
// ============================================================================
// ScrollElement is 0x178 bytes (`MOV EDX,0x178` at 0x18011ab00); its ctor is
// CALLED out of line (0x18011a7f0, the scrollelement.cpp door). R8 (the
// Dialog) is never read — this class creates no children.
//
// NINE {sheet, index} pairs into +0xc0/+0xc8 .. +0x140/+0x148, with the same
// dword-written/qword-read stack slot the other appliers in this batch have,
// and the sheet RE-READ from the description before each of the nine.
//
// THE LAST THREE COPIES ARE NOT IN ORDER: +0xbc -> element +0x15c, +0xc0 ->
// +0x154, +0xc4 -> +0x158 (0x18011ad34, 0x18011ad40, 0x18011ad4c). Copying them
// in destination order would swap two element fields.
Object::InternalPtr<Element>* ScrollDescription::CreateInstance(
        Object::InternalPtr<Element>* ret, const Dialog* /*dialog — never read*/) const {
    void* raw = System::MemoryAllocator::MemAlloc(0x178, 8);  // 18011ab00 (inlined)

    ScrollElement* elem = nullptr;
    if (raw != nullptr)                                        // 18011ab2a
        elem = ::new (raw) ScrollElement();                    // 18011ab2f ??0 (out of line)

    Object::InternalPtr<Element> slot(elem);                                         // 18011ab37
    UI::ElementDescription::CreateInstance(                   // 18011ab42
        &slot);

    ::Sprite* const sprites[9] = {
        &elem->_trackStart, &elem->_trackMiddle, &elem->_trackEnd,
        &elem->_thumbStart, &elem->_thumbMiddle, &elem->_thumbEnd,
        &elem->_hotStart,   &elem->_hotMiddle,   &elem->_hotEnd };

    uint64_t idxSlot;                                          // [RBP-0x10]
    for (int i = 0; i < 9; ++i) {
        SpriteSheet* sheet = _spriteSheet;                            // RE-READ each time
        if (sheet != nullptr) {
            const int32_t idx = sheet
                ->GetSpriteIndexFromName(_spriteNames[i]);
            __builtin_memcpy(&idxSlot, &idx, sizeof idx);      // DWORD store
        } else {
            const int32_t minusOne = -1;
            __builtin_memcpy(&idxSlot, &minusOne, sizeof minusOne);
        }
        sprites[i]->spriteSheet = sheet;
        __builtin_memcpy(&sprites[i]->spriteIndex, &idxSlot, 8);   // QWORD store (+pad)
    }

    elem->_vertical   = _vertical;                             // 18011ad2e
    elem->_inset      = _inset;                                // 18011ad3a
    elem->_barColor   = _barColor;                             // 18011ad46
    elem->_highlightColor = _highlightColor;                            // 18011ad52

    *ret = Object::InternalPtr<Element>(elem);                 // 18011ad58
    return ret;
}

// ??1ScrollDescription@UI@@UEAA@XZ  0x18011a5d0 — retail's body, moved from
// the retired door (ENDGAME B.3.9 UI-B): the nine Strings in REVERSE order
// (+0xb0 down to +0x70). The restamps, the base's _textName (+0x60) and the
// tail JMP into ~External are the compiler's chain; the ExternalPtr at +0x68
// is not released.
//
// B4 Y: the nine releases (18011a5f5 .. 18011a77a) are the member array's
// ~String calls, run after this (empty) body, [8] first.
ScrollDescription::~ScrollDescription() {
}

// ?CreateNewObject@ScrollDescription@UI@@SAPEAVBase@Object@@XZ 0x18003a210 --
// `?MemAlloc@MemoryAllocator` (0x1800c7a50) of 0xc8, align 8; retail's null
// branch; the ctor. Replaces the factory interpreter row (B39_RULES D2).
Object::Base* ScrollDescription::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(ScrollDescription), 8);
    if (p == nullptr) return nullptr;
    return ::new (p) ScrollDescription();
}
// ?CreateInPlaceObject@ScrollDescription@UI@@SAPEAVBase@Object@@PEAX@Z 0x18003a290
Object::Base* ScrollDescription::CreateInPlaceObject(void* place) {
    if (place == nullptr) return nullptr;
    return ::new (place) ScrollDescription();
}

}  // namespace UI

// ============================================================================
// ENDGAME B.3.9 UI-B: the slot doors, the ctor / dtor doors and the
// factory-interpreter row are gone. Their only readers were the minted
// `??_7ScrollDescription@UI@@6B@`, its RT_DELDTOR row and FACTUI2: every live
// ScrollDescription now wears the compiler's table.
// ============================================================================

// ============================================================================
// ENDGAME B.3.9 TI-UI (B39 D6): UI::ScrollDescription's own type record, built at CRT init
// (head-insert into Object::BaseTypeInfo::_firstTypeInfo). Literals copied from
// the retired minted row ?_typeInfo@ScrollDescription@UI@@ (0x1801fad60): name "ScrollDescription", parent
// "ElementDescription" (retail ?_ParentTypeName@ScrollDescription@UI@@), no file extension; factories:
// the class's own.
// ============================================================================
DEFINE_TYPE(UI::ScrollDescription, "ElementDescription", nullptr)
