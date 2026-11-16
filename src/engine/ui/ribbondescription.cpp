// ============================================================================
// src/engine/ui/ribbondescription.cpp — UI::RibbonDescription, the whole class
// ============================================================================
//
// The cutover boot's wall after UI::GroupDescription was ported:
//
//   [RtMint] FATAL: call through a NOT-YET-PORTED minted vtable slot
//   [RtMint]   vtable : ??_7RibbonDescription@UI@@6B@
//   [RtMint]   slot   : 2
//   [RtMint]   target : ?Type@RibbonDescription@UI@@UEBAIXZ
//
// Same shape as groupdescription.cpp, which is this file's sibling and the
// place the shared reasoning is written down (the Serializer pair, the retail
// ReaderWriter offsets, the hidden return slot on CreateInstance). What is NOT
// shared is every constant below, and each one was read out of THIS class's own
// disassembly rather than carried across:
//
//   ??_7RibbonDescription@UI@@6B@ (retail 0x180156ed0), ten slots:
//     0  deleting dtor      0x180044620  here (the class's own; sizeof 0x80)
//     1  TypeName           0x180044130  here
//     2  Type               0x180044110  here
//     3  GetFileVersion     0x1800d5ef0  here  (returns 0x204, NOT Group's 0x104)
//     4  External::Compile  0x1800066e0  already ported
//     5  Serialize          0x1800d5f00  here
//     6  Reconstruct        0x1800443c0  here
//     7  Destruct           0x180044520  here
//     8  CreateInstance     0x1800d6000  here
//     9  Construct          0x180044470  here
//   ??1RibbonDescription@UI@@UEAA@XZ     0x1800d5c90
//
// LAYOUT: src/engine/ui/RibbonDescription.h (sizeof 0x80; RULE B6-8 moved
// this file's offsets there under the retail name, named after the
// RibbonElement fields each one seeds).
//
// ENDGAME B.3.6 block C5: every slot body MOVED into the members of the real
// `UI::RibbonDescription`.
//
// ============================================================================
#include <cstddef>
#include <cstdint>

#include "ui/ElementDescription.h"
#include "ui/Element.h"
#include "io/Serializer.h"
#include "ui/RibbonDescription.h"
#include "ui/RibbonElement.h"
#include "ui/Dialog.h"
#include "system/String.h"
#include "system/MemoryAllocator.h"
#include "collection/Collection.h"

#include <new>

#include "io/Stream.h"
#include <new>

namespace {


}  // namespace

// ============================================================================
// The MEMBERS
// ============================================================================
namespace UI {

// [3] 0x1800d5ef0: `MOV EAX,0x204 ; RET`. 0x204, not GroupDescription's 0x104 —
// the file version is per class and the two must not be shared.
int32_t RibbonDescription::GetFileVersion() const { return 0x204; }

// [9] 0x180044470 — the ctor INLINED in retail: placement-new the class (the
// null-`this` test sat in the retired door; a virtual call has a `this`).
Object::External* RibbonDescription::Construct() {
    return ::new (this) RibbonDescription();
}
// [6] 0x1800443c0 — destroy in place through slot 0 (flag 0), re-run the ctor.
Object::External* RibbonDescription::Reconstruct() {
    this->~RibbonDescription();                                                    // 1800443d7
    return ::new (this) RibbonDescription();
}
// [7] 0x180044520 is three instructions: `MOV RAX,[RCX] ; XOR EDX,EDX ; JMP [RAX]`
// — a tail jump into slot 0 with flag 0, i.e. destroy without freeing.
void RibbonDescription::Destruct() { this->~RibbonDescription(); }

// ============================================================================
// [5] ?Serialize@RibbonDescription@UI@@UEAA_NAEAVStream@IO@@@Z   0x1800d5f00
// ============================================================================
//   ElementDescription::Serialize(this, stream)     0x180116b00, ported
//   rw = encoding factory, read side or write side by stream->mode
//   rw->vt[0x78](rw, &_vertical, 0)  kScalarBool   1800d5f5e
//   rw->vt[0x78](rw, &_uniform, 0)   kScalarBool   1800d5f70
//   rw->vt[0xa0](rw, &_spacing, 0)   kScalarInt    1800d5f82
//   n = _items._size ; rw->vt[0xa0](rw, &n, 0)                 1800d5f9f
//   if (pair.stream->mode == 0) SetSize(&_items, n)            0x1800492d0
//   ElementDescriptionPtrArray_Serialize(&pair, n, _items._data)  0x1800493d0
//   if (rw) rw->vt[0](rw, 1)
//
// THE FIELD ORDER IS 0x68, 0x69, 0x6c — NOT 0x68, 0x6c, 0x69. CreateInstance
// copies them in the OTHER order (0x68, 0x6c, 0x69 at
// 1800d60bf..1800d60d6) and a port that took its order from there would emit
// the two bools and the int in the wrong sequence into every saved layout,
// which the reader would then mis-parse. Each body's order is its own.
bool RibbonDescription::Serialize(IO::Stream& streamArg) {
    IO::Stream* stream = &streamArg;

    // QUALIFIED: this object wears the LEAF's table, whose slot 5 is this body.
    ElementDescription::Serialize(*stream);                              // 1800d5f1a

    // The inlined ??0Serializer (0x1800b9960): CreateReader / CreateWriter
    // of the stream's encoding, version 0.
    IO::Serializer ser(*stream, 0);                               // [RSP+0x28], [RSP+0x30]
    IO::ReaderWriter* const rw = ser._readerWriter;

    rw->SerializeBool(_vertical, nullptr);
    rw->SerializeBool(_uniform, nullptr);
    rw->SerializeInt(_spacing, nullptr);

    int32_t n = _itemsCount;                                            // 1800d5f88
    rw->SerializeInt(n, nullptr);

    if (ser._stream->_direction == 0) {                                        // 1800d5fa5 (re-read)
        rtcoll::RtColl_ArraySetSize_1800492d0(&_itemsData, n);              // 1800d5fb8
    }

    Object::SerializeExternalArray<UI::ElementDescription>(ser, n, _itemsData);

    // The inlined ~Serializer deletes the ReaderWriter (1800d5fd5).
    return true;
}

// ============================================================================
// [8] ?CreateInstance@RibbonDescription@UI@@...   retail 0x1800d6000
// ============================================================================
// RCX = this, RDX = the hidden InternalPtr<Element> return slot, R8 = the
// Dialog*. (See groupdescription.cpp for why Ghidra prints only two of the
// three.)
//
// RibbonElement is 0xd8 bytes (`MOV EDX,0xd8` at 0x1800d6048); retail INLINES
// its ctor: ??0Element, the vtable stamp, ONE 16-bit store over +0xc0..+0xc1,
// then +0xc4, +0xc8, +0xd0 (1800d6088..1800d60a0).
//
// The LAST call is the one that makes this class different from Group's:
// `Array<int32>::SetSize(&elem->_extents, desc->_items._size)` (0x18002fea0,
// array.cpp kSpec_I32 — the instantiation whose new tail is UNINITIALISED).
// It runs AFTER the child loop and OUTSIDE it.
Object::InternalPtr<Element>* RibbonDescription::CreateInstance(
        Object::InternalPtr<Element>* ret, const Dialog* dialog) const {
    void* raw = System::MemoryAllocator::MemAlloc(0xd8, 8);           // 1800d6048

    // Retail inlines ??0RibbonElement (0x1800d6078 .. 0x1800d60a0): the real ctor.
    RibbonElement* elem = nullptr;
    if (raw != nullptr)                                                 // 1800d6070
        elem = ::new (raw) RibbonElement();                             // 1800d6078 ??0Element + stores

    Object::InternalPtr<Element> slot(elem);                                                  // 1800d60ad [RSP+0x50]

    // The NON-virtual ElementDescription::CreateInstance(InternalPtr<Element>)
    // (0x180115ad0): a QUALIFIED member call since fragment pass F2.
    UI::ElementDescription::CreateInstance(                   // 1800d60ba
        &slot);

    elem->_vertical = _vertical;                                        // 1800d60bf
    elem->_spacing  = _spacing;                                         // 1800d60c9
    elem->_uniform  = _uniform;                                         // 1800d60d2

    const int32_t count = _itemsCount;
    for (int32_t i = 0; i < count; ++i) {                               // 1800d60dc / 1800d6149
        ElementDescription* desc = _itemsData[i];                                   // 1800d60f7

        // ?CreateElement@Dialog@UI@@ 0x180112ed0: RETSLOT, the member's prvalue
        // placed in the caller's slot.
        Object::InternalPtr<Element>* made = &slot;
        *made = dialog->CreateElement(
            Object::ExternalPtr<UI::ElementDescription>(desc));

        Element* child = made->Get();            // 1800d6108
        if (child != nullptr)
            child->_parent = elem;                                      // 1800d6110

        rtcoll::RtColl_ArraySetSizeFill_180076900(&elem->_children,
                                       elem->_children.GetSize() + 1,
                                       made);                           // 1800d6129

        for (Element* n = elem; n != nullptr; n = n->_parent)           // 1800d6131..1800d6145
            n->_flags |= kElementFlagNeedsSizeCalc;
    }

    rtcoll::RtColl_ArraySetSize_18002fea0(&elem->_extentsData, count);      // 1800d6158

    new (static_cast<void*>(ret)) Object::InternalPtr<Element>(elem);   // 1800d615d
    return ret;
}

// ??1RibbonDescription@UI@@UEAA@XZ   0x1800d5c90 — retail's body, moved from
// the retired door (ENDGAME B.3.9 UI-B): the child Array at +0x70 through
// 0x180039440. The restamps, the base's asymmetric _textName (+0x60) release
// and the tail JMP into ~External are the compiler's chain.
RibbonDescription::~RibbonDescription() {
    rtcoll::RtColl_ArrayDtor_180039440(&_itemsData);   // 1800d5cb4, +0x70
}

// ?CreateNewObject@RibbonDescription@UI@@SAPEAVBase@Object@@XZ 0x180044220 --
// `?MemAlloc@MemoryAllocator` (0x1800c7a50) of 0x80, align 8; retail's null
// branch; the ctor. Replaces the factory interpreter row (B39_RULES D2).
Object::Base* RibbonDescription::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(RibbonDescription), 8);
    if (p == nullptr) return nullptr;
    return ::new (p) RibbonDescription();
}
// ?CreateInPlaceObject@RibbonDescription@UI@@SAPEAVBase@Object@@PEAX@Z 0x180044310
Object::Base* RibbonDescription::CreateInPlaceObject(void* place) {
    if (place == nullptr) return nullptr;
    return ::new (place) RibbonDescription();
}

}  // namespace UI

// ============================================================================
// ENDGAME B.3.9 UI-B: the slot doors, the ctor / dtor doors and the
// factory-interpreter row are gone. Their only readers were the minted
// `??_7RibbonDescription@UI@@6B@`, its RT_DELDTOR row and FACTUI2: every live
// RibbonDescription now wears the compiler's table.
// ============================================================================

// ============================================================================
// ENDGAME B.3.9 TI-UI (B39 D6): UI::RibbonDescription's own type record, built at CRT init
// (head-insert into Object::BaseTypeInfo::_firstTypeInfo). Literals copied from
// the retired minted row ?_typeInfo@RibbonDescription@UI@@ (0x1801fa2a8): name "RibbonDescription", parent
// "ElementDescription" (retail ?_ParentTypeName@RibbonDescription@UI@@), no file extension; factories:
// the class's own.
// ============================================================================
DEFINE_TYPE(UI::RibbonDescription, "ElementDescription", nullptr)
