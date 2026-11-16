// ============================================================================
// src/engine/ui/spinnerdescription.cpp — UI::SpinnerDescription, whole class
// ============================================================================
//
// Vtable batch 2. Derives straight from UI::ElementDescription.
//
// ??_7SpinnerDescription@UI@@6B@ (retail 0x1801576c0), ten slots:
//   0  deleting dtor      0x18004b050  the compiler's (sizeof 0x88)
//   1  TypeName           0x18004ad60  here
//   2  Type               0x18004ad40  here
//   3  GetFileVersion     0x1800d8940  here  (returns 0x204)
//   4  External::Compile  0x1800066e0  already ported
//   5  Serialize          0x1800d8880  here
//   6  Reconstruct        0x18004aee0  here
//   7  Destruct           0x18004af10  here
//   8  CreateInstance     0x1800d86b0  here
//   9  Construct          0x18004af00  here
// plus ??0SpinnerDescription@UI@@QEAA@XZ 0x1800d89c0 (out of line, both
// Construct and Reconstruct tail-jump to it) and
// ??1SpinnerDescription@UI@@UEAA@XZ 0x1800d8950.
//
// LAYOUT, sizeof 0x88 (`MOV EDX,0x88` at 0x18004b06f). ElementDescription
// (0x68) plus THREE child descriptions and one int:
//     +0x68  ExternalPtr<UI::ElementDescription>   0x180038f90
//     +0x70  ExternalPtr<UI::ElementDescription>   0x180038f90
//     +0x78  ExternalPtr<UI::ElementDescription>   0x180038f90
//     +0x80  int32  _increment  ctor 1, kScalarInt, copied to the element's +0xc0
//     +0x84..+0x87 never written
//
// The three children are NOT an Array — they are three separate fields, each
// serialized by its own call and each turned into one child element by its own
// copy of the create-parent-push-dirty sequence in CreateInstance. Three
// literal repetitions, not a loop, and transcribed as three.
//
// Every helper is already ported: 0x180038f90 (ExternalPtr<ElementDescription>
// ::Serialize, external_ptr_serialize.cpp), 0x180116b00, 0x180115ad0,
// 0x180112ed0, 0x180115e50, 0x180076900.
// ============================================================================
#include <cstddef>
#include <cstdint>

#include "ui/ElementDescription.h"
#include "ui/Element.h"
#include "io/Serializer.h"
#include "ui/SpinnerDescription.h"
#include "ui/SpinnerElement.h"
#include "ui/Dialog.h"
#include "system/String.h"
#include "system/MemoryAllocator.h"
#include "collection/Collection.h"

#include <new>

#include "io/Stream.h"

namespace {


// One of CreateInstance's three identical child blocks (0x1800d874e,
// 0x1800d87a6, 0x1800d87f6 — the same instructions with a different source
// field each time).
void AddChild(const UI::Dialog* dialog, Object::InternalPtr<UI::Element>* slot,
              UI::Element* elem, UI::ElementDescription* childDesc) {
    // ?CreateElement@Dialog@UI@@ 0x180112ed0: RETSLOT, the member's prvalue
    // placed in the caller's slot.
    Object::InternalPtr<UI::Element>* made = slot;
    *made = dialog->CreateElement(Object::ExternalPtr<UI::ElementDescription>(childDesc));

    UI::Element* child = made->Get();
    if (child != nullptr)
        child->_parent = elem;

    rtcoll::RtColl_ArraySetSizeFill_180076900(&elem->_children, elem->_children.GetSize() + 1, made);

    for (UI::Element* n = elem; n != nullptr; n = n->_parent)
        n->_flags |= UI::kElementFlagNeedsSizeCalc;
}

}  // namespace



// ============================================================================
// The MEMBERS
// ============================================================================
namespace UI {

// [3] 0x1800d8940
int32_t SpinnerDescription::GetFileVersion() const { return 0x204; }

// [9] 0x18004af00 — the ctor, placement (the null test sat in the retired door).
Object::External* SpinnerDescription::Construct() {
    return ::new (this) SpinnerDescription();
}
// [6] 0x18004aee0 — destroy in place through slot 0 (flag 0), re-run the ctor.
Object::External* SpinnerDescription::Reconstruct() {
    this->~SpinnerDescription();
    return ::new (this) SpinnerDescription();
}
// [7] 0x18004af10
void SpinnerDescription::Destruct() { this->~SpinnerDescription(); }

// [5] ?Serialize@SpinnerDescription@UI@@UEAA_NAEAVStream@IO@@@Z  0x1800d8880
bool SpinnerDescription::Serialize(IO::Stream& streamArg) {
    IO::Stream* stream = &streamArg;

    // QUALIFIED: this object wears the LEAF's table, whose slot 5 is this body.
    ElementDescription::Serialize(*stream);                              // 1800d889a

    // The inlined ??0Serializer (0x1800b9960): CreateReader / CreateWriter
    // of the stream's encoding, version 0.
    IO::Serializer ser(*stream, 0);
    IO::ReaderWriter* const rw = ser._readerWriter;

    Object::SerializeExternal<UI::ElementDescription>(ser, _editDescription);        // 1800d88d8
    Object::SerializeExternal<UI::ElementDescription>(ser, _upDescription);          // 1800d88e6
    Object::SerializeExternal<UI::ElementDescription>(ser, _downDescription);        // 1800d88f4

    rw->SerializeInt(_increment, nullptr);

    // The inlined ~Serializer deletes the ReaderWriter (1800d891a).
    return true;
}

// ============================================================================
// [8] ?CreateInstance@SpinnerDescription@UI@@...   retail 0x1800d86b0
// ============================================================================
// SpinnerElement is 0xc8 bytes (`MOV EDX,0xc8` at 0x1800d86f2); construction
// calls ??0Element, stamps the SpinnerElement vtable and writes ONE dword,
// +0xc0 = 1 — which the tail then overwrites with the description's +0x80. Both
// stores are retail's and both are here.
Object::InternalPtr<Element>* SpinnerDescription::CreateInstance(
        Object::InternalPtr<Element>* ret, const Dialog* dialog) const {
    void* raw = System::MemoryAllocator::MemAlloc(0xc8, 8);           // 1800d86f2 (inlined)

    SpinnerElement* elem = nullptr;
    if (raw != nullptr)                                                 // 1800d871d
        // The SpinnerElement ctor, INLINED by retail (0x1800d8722 .. 0x1800d8732):
        // the real ctor.
        elem = ::new (raw) SpinnerElement();

    Object::InternalPtr<Element> slot(elem);                                                  // 1800d873c
    // The NON-virtual ElementDescription::CreateInstance(InternalPtr<Element>)
    // (0x180115ad0): a QUALIFIED member call since fragment pass F2.
    UI::ElementDescription::CreateInstance(                   // 1800d8749
        &slot);

    AddChild(dialog, &slot, elem, _editDescription);                    // 1800d874e
    AddChild(dialog, &slot, elem, _upDescription);                      // 1800d87a6
    AddChild(dialog, &slot, elem, _downDescription);                    // 1800d87f6

    elem->_increment = _increment;                                      // 1800d884c

    new (static_cast<void*>(ret)) Object::InternalPtr<Element>(elem);   // 1800d8852
    return ret;
}

// ??1SpinnerDescription@UI@@UEAA@XZ  0x1800d8950 — retail's body is ONE String
// release (the base's _textName at +0x60, under the base vtable) and the tail
// JMP into ~External: the compiler's chain is all of it. The three
// ExternalPtrs are not released.
SpinnerDescription::~SpinnerDescription() {}

// ?CreateNewObject@SpinnerDescription@UI@@SAPEAVBase@Object@@XZ 0x18004ae50 --
// `?MemAlloc@MemoryAllocator` (0x1800c7a50) of 0x88, align 8; retail's null
// branch; the ctor. Replaces the factory interpreter row (B39_RULES D2).
Object::Base* SpinnerDescription::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(SpinnerDescription), 8);
    if (p == nullptr) return nullptr;
    return ::new (p) SpinnerDescription();
}
// ?CreateInPlaceObject@SpinnerDescription@UI@@SAPEAVBase@Object@@PEAX@Z 0x18004aed0
Object::Base* SpinnerDescription::CreateInPlaceObject(void* place) {
    if (place == nullptr) return nullptr;
    return ::new (place) SpinnerDescription();
}

}  // namespace UI

// ============================================================================
// ENDGAME B.3.9 UI-B: the slot doors, the ctor / dtor doors and the
// factory-interpreter row are gone. Their only readers were the minted
// `??_7SpinnerDescription@UI@@6B@`, its RT_DELDTOR row and FACTUI2: every live
// SpinnerDescription now wears the compiler's table.
// ============================================================================

// ============================================================================
// ENDGAME B.3.9 TI-UI (B39 D6): UI::SpinnerDescription's own type record, built at CRT init
// (head-insert into Object::BaseTypeInfo::_firstTypeInfo). Literals copied from
// the retired minted row ?_typeInfo@SpinnerDescription@UI@@ (0x1801fa388): name "SpinnerDescription", parent
// "ElementDescription" (retail ?_ParentTypeName@SpinnerDescription@UI@@), no file extension; factories:
// the class's own.
// ============================================================================
DEFINE_TYPE(UI::SpinnerDescription, "ElementDescription", nullptr)
