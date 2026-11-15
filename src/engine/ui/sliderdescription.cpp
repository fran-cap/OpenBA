// ============================================================================
// src/engine/ui/sliderdescription.cpp — UI::SliderDescription, whole class
// ============================================================================
//
// Vtable batch 2. Derives straight from UI::ElementDescription.
//
// ??_7SliderDescription@UI@@6B@ (retail 0x1801575e0), ten slots:
//   0  deleting dtor      0x180049c60  the compiler's (sizeof 0xb8)
//   1  TypeName           0x180049750  here
//   2  Type               0x180049730  here
//   3  GetFileVersion     0x1800d79f0  here  (returns 0x304)
//   4  External::Compile  0x1800066e0  already ported
//   5  Serialize          0x1800d7890  here
//   6  Reconstruct        0x1800498d0  here
//   7  Destruct           0x180049900  here
//   8  CreateInstance     0x1800d7630  here
//   9  Construct          0x1800498f0  here
// plus ??0SliderDescription@UI@@QEAA@XZ 0x1800d7b60 (out of line) and
// ??1SliderDescription@UI@@UEAA@XZ 0x1800d7a00.
//
// LAYOUT: src/engine/ui/SliderDescription.h (sizeof 0xb8; RULE B6-8 moved
// this file's offsets there under the retail name).
//
// ENDGAME B.3.6 block C5: every slot body MOVED into the members of the real
// `UI::SliderDescription`. The ctor is inline in SliderDescription.h; the dtor
// is below.
//
// THE DESTRUCTOR RELEASES THE FIVE STRINGS IN REVERSE ORDER (+0x98, +0x90,
// +0x88, +0x80, +0x78) under THIS class's vtable, then restamps the base vtable
// and releases ElementDescription's _textName at +0x60. Neither ExternalPtr is
// released. All of that is retail's.
//
// Every helper is already ported: 0x180038f90 and 0x180004590
// (external_ptr_serialize.cpp), 0x1800c9930 (string.cpp), 0x180116b00,
// 0x180115ad0, 0x180112ed0, 0x180076900, and
// ?GetSpriteIndexFromName@SpriteSheet@UI@@ (0x1800031f0), called directly.
// ??0SliderElement@UI@@QEAA@XZ (0x1800d7570) is NOT ported here — the *Element
// constructors belong to the Element files; it is SliderElement.h's inline ctor.
// ============================================================================
#include <cstddef>
#include <cstdint>

#include "ui/ElementDescription.h"
#include "ui/Element.h"
#include "io/Serializer.h"
#include "ui/SliderDescription.h"
#include "ui/SliderElement.h"
#include "ui/Dialog.h"
#include "system/String.h"
#include "system/MemoryAllocator.h"
#include "collection/Collection.h"
#include "ui/SpriteSheet.h"

#include <new>

#include "io/Stream.h"

namespace {





}  // namespace



// ============================================================================
// The MEMBERS
// ============================================================================
namespace {
// ?GetSpriteIndexFromName@SpriteSheet@UI@@ (0x1800031f0), called directly.
int32_t GetSpriteIndexFromName(const UI::SpriteSheet* sheet, const System::String* name) {
    return sheet->GetSpriteIndexFromName(*name);
}
// A raw 4-byte copy: retail moves the two floats through EAX, not an XMM register.
template <typename D, typename S>
void Copy4(D& dst, const S& src) {
    static_assert(sizeof(D) == 4 && sizeof(S) == 4, "four bytes");
    __builtin_memcpy(&dst, &src, 4);
}
}  // namespace

namespace UI {

// [3] 0x1800d79f0
int32_t SliderDescription::GetFileVersion() const { return 0x304; }

// [9] 0x1800498f0 — the ctor, placement (the null test sat in the retired door).
Object::External* SliderDescription::Construct() {
    return ::new (this) SliderDescription();
}
// [6] 0x1800498d0 — destroy in place through slot 0 (flag 0), re-run the ctor.
Object::External* SliderDescription::Reconstruct() {
    this->~SliderDescription();
    return ::new (this) SliderDescription();
}
// [7] 0x180049900
void SliderDescription::Destruct() { this->~SliderDescription(); }

// [5] ?Serialize@SliderDescription@UI@@UEAA_NAEAVStream@IO@@@Z  0x1800d7890
bool SliderDescription::Serialize(IO::Stream& streamArg) {
    IO::Stream* stream = &streamArg;

    // QUALIFIED: this object wears the LEAF's table, whose slot 5 is this body.
    ElementDescription::Serialize(*stream);                              // 1800d78aa

    // The inlined ??0Serializer (0x1800b9960): CreateReader / CreateWriter
    // of the stream's encoding, version 0.
    IO::Serializer ser(*stream, 0);
    IO::ReaderWriter* const rw = ser._readerWriter;

    Object::SerializeExternal<UI::ElementDescription>(ser, _childDescription);       // 1800d78e8 ExternalPtr<ElementDescription>
    Object::SerializeExternal<UI::SpriteSheet>(ser, _spriteSheet);            // 1800d78f6 ExternalPtr<SpriteSheet>
    _trackStartName.Serialize(ser);         // 1800d7904
    _trackMiddleName.Serialize(ser);        // 1800d7915
    _trackEndName.Serialize(ser);           // 1800d7926
    _thumbName.Serialize(ser);              // 1800d7937
    _thumbHotName.Serialize(ser);           // 1800d7948

    rw->SerializeInt(_trackOffsetY, nullptr);
    rw->SerializeInt(_thumbMargin, nullptr);
    rw->SerializeFloat(_minimum, nullptr);
    rw->SerializeFloat(_maximum, nullptr);
    rw->SerializeInt(_steps, nullptr);

    // The inlined ~Serializer deletes the ReaderWriter (1800d79ce).
    return true;
}

// ============================================================================
// [8] ?CreateInstance@SliderDescription@UI@@...   retail 0x1800d7630
// ============================================================================
// SliderElement is 0x130 bytes (`MOV EDX,0x130` at 0x1800d7679) and its ctor is
// CALLED out of line (0x1800d7570). ONE child element from +0x68, then FIVE
// sprite lookups.
//
// *** THE FIVE LOOKUPS REPEAT ImageDescription's WIDTH BUG, five times. ***
// Each `MOV dword [RBP-0x10], EAX` writes FOUR bytes and the following
// `MOV RAX,[RBP-0x10]` reads EIGHT, so each of the element's index slots
// (+0xc8, +0xd8, +0xe8, +0xf8, +0x108) receives four bytes of the looked-up
// index and four bytes of whatever was on retail's stack. All five share ONE
// stack slot, so after the first iteration the upper half is the PREVIOUS
// lookup's leftover — deterministic in retail, and not reproducible here. What
// is faithful is the WIDTH, so the upper half is left indeterminate.
//
// The sheet pointer is RE-READ from the description before every lookup
// (0x1800d7716, 0x1800d7749, ...), not hoisted; retail does not assume it is
// stable across the calls and neither does this.
Object::InternalPtr<Element>* SliderDescription::CreateInstance(
        Object::InternalPtr<Element>* ret, const Dialog* dialog) const {
    void* raw = System::MemoryAllocator::MemAlloc(0x130, 8);          // 1800d7679 (inlined)

    SliderElement* elem = nullptr;
    if (raw != nullptr)                                                 // 1800d76a3
        elem = ::new (raw) SliderElement();                             // 1800d76a8 ??0 (out of line)

    Object::InternalPtr<Element> slot(elem);                                                  // 1800d76b0
    // The NON-virtual ElementDescription::CreateInstance(InternalPtr<Element>)
    // (0x180115ad0): a QUALIFIED member call since fragment pass F2.
    UI::ElementDescription::CreateInstance(                   // 1800d76bb
        &slot);

    {   // the single child, from +0x68. UI::Dialog::CreateElement.
        // ?CreateElement@Dialog@UI@@ 0x180112ed0: RETSLOT, the member's prvalue
        // placed in the caller's slot.                          // 1800d76cb
        Object::InternalPtr<Element>* made = &slot;
        *made = dialog->CreateElement(
            Object::ExternalPtr<UI::ElementDescription>(_childDescription));

        Element* child = made->Get();            // 1800d76d0
        if (child != nullptr)
            child->_parent = elem;                                      // 1800d76d8

        rtcoll::RtColl_ArraySetSizeFill_180076900(&elem->_children,
                                       elem->_children.GetSize() + 1,
                                       made);                           // 1800d76ee

        for (Element* n = elem; n != nullptr; n = n->_parent)           // 1800d7700..1800d7714
            n->_flags |= kElementFlagNeedsSizeCalc;
    }

    // The five {sheet, index} pairs. ONE stack slot, written 4 bytes wide and
    // read 8 — see the banner.
    uint64_t idxSlot;                                                   // [RBP-0x10]
    const System::String* const names[5] = { &_trackStartName, &_trackMiddleName, &_trackEndName,
                                    &_thumbName, &_thumbHotName };
    ::Sprite* const sprites[5] = { &elem->_trackStart, &elem->_trackMiddle, &elem->_trackEnd,
                                   &elem->_thumb, &elem->_thumbHot };
    for (int i = 0; i < 5; ++i) {
        SpriteSheet* sheet = _spriteSheet;                                     // RE-READ each time
        if (sheet != nullptr) {
            const int32_t idx = GetSpriteIndexFromName(sheet, names[i]);
            __builtin_memcpy(&idxSlot, &idx, sizeof idx);               // DWORD store
        } else {
            const int32_t minusOne = -1;
            __builtin_memcpy(&idxSlot, &minusOne, sizeof minusOne);
        }
        // One QWORD store over `spriteIndex` and `pad0C`: the index in the low
        // dword, the slot's never-written high dword above it.
        sprites[i]->spriteSheet = sheet;
        sprites[i]->spriteIndex = static_cast<int32_t>(idxSlot);
        sprites[i]->pad0C       = static_cast<int32_t>(idxSlot >> 32);
    }

    elem->_trackOffsetY = _trackOffsetY;                                // 1800d7827
    elem->_thumbMargin  = _thumbMargin;                                 // 1800d7833
    Copy4(elem->_minimum, _minimum);                                    // 1800d783f  (4 raw bytes)
    Copy4(elem->_maximum, _maximum);                                    // 1800d784b  (4 raw bytes)
    elem->_steps        = _steps;                                       // 1800d7857

    new (static_cast<void*>(ret)) Object::InternalPtr<Element>(elem);   // 1800d785d
    return ret;
}

// ??1SliderDescription@UI@@UEAA@XZ  0x1800d7a00 — retail's body, moved from
// the retired door (ENDGAME B.3.9 UI-B): the five Strings in REVERSE order,
// 0x98 down to 0x78. The restamps, the base's _textName (+0x60) and the tail
// JMP into ~External are the compiler's chain.
// B4 P2 H5: retail 0x1800d7a00 releases +0x98 .. +0x78 (0x1800d7a25 .. 0x1800d7b0e)
// then the base +0x60 and JMP ~External (0x1800bc950): the members ~String
// (0x180006c80) in reverse declaration order, then the base chain.
SliderDescription::~SliderDescription() {}

// ?CreateNewObject@SliderDescription@UI@@SAPEAVBase@Object@@XZ 0x180049840 --
// `?MemAlloc@MemoryAllocator` (0x1800c7a50) of 0xb8, align 8; retail's null
// branch; the ctor. Replaces the factory interpreter row (B39_RULES D2).
Object::Base* SliderDescription::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(SliderDescription), 8);
    if (p == nullptr) return nullptr;
    return ::new (p) SliderDescription();
}
// ?CreateInPlaceObject@SliderDescription@UI@@SAPEAVBase@Object@@PEAX@Z 0x1800498c0
Object::Base* SliderDescription::CreateInPlaceObject(void* place) {
    if (place == nullptr) return nullptr;
    return ::new (place) SliderDescription();
}

}  // namespace UI

// ============================================================================
// ENDGAME B.3.9 UI-B: the slot doors, the ctor / dtor doors and the
// factory-interpreter row are gone. Their only readers were the minted
// `??_7SliderDescription@UI@@6B@`, its RT_DELDTOR row and FACTUI2: every live
// SliderDescription now wears the compiler's table.
// ============================================================================

// ============================================================================
// ENDGAME B.3.9 TI-UI (B39 D6): UI::SliderDescription's own type record, built at CRT init
// (head-insert into Object::BaseTypeInfo::_firstTypeInfo). Literals copied from
// the retired minted row ?_typeInfo@SliderDescription@UI@@ (0x1801fa318): name "SliderDescription", parent
// "ElementDescription" (retail ?_ParentTypeName@SliderDescription@UI@@), no file extension; factories:
// the class's own.
// ============================================================================
DEFINE_TYPE(UI::SliderDescription, "ElementDescription", nullptr)
