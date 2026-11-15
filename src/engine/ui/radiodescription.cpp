// ============================================================================
// src/engine/ui/radiodescription.cpp — UI::RadioDescription, the whole class
// ============================================================================
//
// Vtable batch 2. THIS CLASS DERIVES FROM UI::GroupDescription, and that is the
// one thing about it that is not shaped like its siblings: three of its bodies
// are one line of its own plus a call into GroupDescription's copy
// (src/engine/ui/groupdescription.cpp).
//
// ??_7RadioDescription@UI@@6B@ (retail 0x180157420), ten slots:
//   0  deleting dtor      0x180047930  RadioDescription.h (the class's own)
//   1  TypeName           0x180047670  here
//   2  Type               0x180047650  here
//   3  GetFileVersion     0x1800d5b10  here  (returns 0x1104 — per class)
//   4  External::Compile  0x1800066e0  already ported
//   5  Serialize          0x1800d5a80  here
//   6  Reconstruct        0x1800477f0  here
//   7  Destruct           0x180047820  here
//   8  CreateInstance     0x1800d58d0  here
//   9  Construct          0x180047810  here
// plus ??0RadioDescription@UI@@QEAA@XZ 0x1800d5b50 (OUT OF LINE — both
// Construct and Reconstruct tail-jump to it; the real ctor in the header since
// B.3.9), ??1RadioDescription@UI@@UEAA@XZ 0x1800d5b20 and the factory pair.
//
// LAYOUT: src/engine/ui/RadioDescription.h — GroupDescription (0x78) plus
// `_activeRadio` (+0x78, ctor -1); sizeof 0x80.
//
// ENDGAME B.3.6 block C2: every slot body is a MEMBER of `UI::RadioDescription`.
// Serialize's base call (by address 0x180117d00 before) is the qualified member
// `GroupDescription::Serialize`.
//
// ENDGAME B.3.9 block UI-A (B39_RULES D1/D2): the class is CONCRETE; the slot
// doors and the ??0 / ??1 doors, read only by the retired minted table, its
// RT_DELDTOR row and ui_factory_wave28.cpp's row, are gone.
//
// CALLEES DEFINED ELSEWHERE, each called directly:
//   0x180112ed0 Dialog::CreateElement (dialog.cpp)
//   0x180076900 Array<InternalPtr>::SetSize (array.cpp)
//   0x1800d5750 RadioElement::FindCheckBoxes (private; widgets_input.cpp)
// ============================================================================
#include <cstddef>
#include <cstdint>
#include <new>

#include "description_common.h"
#include "ui/RadioDescription.h"
#include "ui/RadioElement.h"
#include "ui/CheckElement.h"     // CheckElement::_checked

namespace {

using namespace rtdesc;

}  // namespace

// ============================================================================
// The member bodies
// ============================================================================
namespace UI {

// 0x1800d5b10: `MOV EAX,0x1104 ; RET`.
int32_t RadioDescription::GetFileVersion() const { return 0x1104; }

// 0x180047810: the null test sat in the retired door; then the out-of-line
// ctor (??0RadioDescription 0x1800d5b50, the real ctor since B.3.9).
Object::External* RadioDescription::Construct() {
    return ::new (this) RadioDescription();
}
// 0x1800477f0: slot 0 with flag 0 through the object's own table, then the
// ctor (tail jump at 0x180047808). No null test.
Object::External* RadioDescription::Reconstruct() {
    this->~RadioDescription();                                           // 1800477fe
    return ::new (this) RadioDescription();
}
void RadioDescription::Destruct() { this->~RadioDescription(); }         // 0x180047820

// ============================================================================
// ?Serialize@RadioDescription@UI@@UEAA_NAEAVStream@IO@@@Z   0x1800d5a80
// ============================================================================
//   GroupDescription::Serialize(this, stream)     0x180117d00, QUALIFIED
//   rw = encoding factory, read side or write side by stream->mode, version 0
//   rw->vt[0xa0](rw, &_activeRadio, 0)   kScalarInt
//   if (rw) rw->vt[0](rw, 1)
//
// THE BASE CALL IS THE PARENT'S Serialize, NOT ElementDescription's — using the
// grandparent here would silently drop every child. It ALSO opens a SECOND
// ReaderWriter over the same stream, after GroupDescription::Serialize has
// already opened and closed its own. That is retail.
bool RadioDescription::Serialize(IO::Stream& streamv) {
    GroupDescription::Serialize(streamv);                       // 1800d5a9a

    IO::Serializer ser(streamv, 0);                // [RSP+0x28], [RSP+0x30]; 1800d5aca (inlined ctor)

    ser._readerWriter->SerializeInt(_activeRadio, nullptr);

    return true;                                    // ~Serializer 1800d5aed (inlined)
}

// ============================================================================
// ?CreateInstance@RadioDescription@UI@@...   retail 0x1800d58d0   (slot 8)
// ============================================================================
// RCX = this, RDX = the hidden InternalPtr<Element> return slot, R8 = Dialog*.
//
// RadioElement is 0xd8 bytes (`MOV EDX,0xd8` at 0x1800d5916) and its
// construction stamps TWO vtables in a row — GroupElement (0x1800d594c) and
// then RadioElement (0x1800d5956) — before zeroing its own fields:
//     +0xc0 _checkBoxes._data, +0xc8 ._size/._capacity, +0xd0 _activeIndex = -1
// -- the real RadioElement ctor's stores (RadioElement.h, ENDGAME B.3.9 UI-A).
//
// THE TAIL IS THIS CLASS'S WHOLE POINT:
//   FindCheckBoxes(elem, &elem)                        0x1800d5750
//   if (_activeRadio != elem->_activeIndex) {
//       elem->_activeIndex = _activeRadio;
//       for (i = 0; i < elem->_checkBoxes._size; ++i)
//           if (i != elem->_activeIndex) checkBoxes[i]->+0xd8 = 0;   // UNCHECK
//   }
// The inner test re-reads `elem->_activeIndex` (0x1800d5a30) rather than the
// register it was just written from. Kept.
Object::InternalPtr<Element>* RadioDescription::CreateInstance(
        Object::InternalPtr<Element>* ret, const Dialog* dialog) const {
    void* raw = rtdesc::MemAlloc(0xd8, 8);                     // 1800d5916 (inlined)

    RadioElement* elem = nullptr;
    if (raw != nullptr)                                        // 1800d5941
        elem = ::new (raw) RadioElement();                     // 1800d5946..1800d5970 inlined ctor

    Object::InternalPtr<Element> slot(elem);                   // 1800d5980
    UI::ElementDescription::CreateInstance(&slot);             // 1800d598d

    const int32_t count = _children._size;
    for (int32_t i = 0; i < count; ++i) {                      // 1800d5994 / 1800d59f9
        ElementDescription* desc = _children._data[i];         // 1800d59a7

        // ?CreateElement@Dialog@UI@@ 0x180112ed0, its result into the same slot.
        slot = dialog->UI::Dialog::CreateElement(Object::ExternalPtr<ElementDescription>(desc));

        Element* child = slot.Get();                           // 1800d59b8
        if (child != nullptr)
            child->_parent = elem;                             // 1800d59c0

        rtcoll::RtColl_ArraySetSizeFill_180076900(&elem->_children,
                                               elem->_children._size + 1,
                                               &slot);         // 1800d59d9

        for (Element* n = elem; n != nullptr; n = n->_parent)  // 1800d59e1..1800d59f5
            n->_flags |= kElementFlagNeedsSizeCalc;
    }

    // The PRIVATE `FindCheckBoxes(InternalPtr<Element>)` (0x1800d5750);
    // retail passes the address of a SECOND stack slot holding the element
    // (RULE B6-11).
    Object::InternalPtr<Element> self2(elem);                  // 1800d59fe
    elem->FindCheckBoxes(&self2);                              // 1800d5a0b

    const int32_t want = _activeRadio;
    if (want != elem->_activeIndex) {                          // 1800d5a13
        elem->_activeIndex = want;                             // 1800d5a1b
        const int32_t boxes = elem->_checkBoxes._size;
        for (int32_t i = 0; i < boxes; ++i) {                  // 1800d5a21 / 1800d5a50
            if (i == elem->_activeIndex) continue;             // 1800d5a30 RE-READ
            // FindCheckBoxes collected CheckElements only (its type test).
            static_cast<CheckElement*>(elem->_checkBoxes._data[i])->_checked = 0;   // 1800d5a43  +0xd8
        }
    }

    new (static_cast<void*>(ret)) Object::InternalPtr<Element>(elem);   // 1800d5a58
    return ret;
}

}  // namespace UI

// ============================================================================
// ??1RadioDescription@UI@@UEAA@XZ   0x1800d5b20
// ============================================================================
// Four instructions: restamp this class's vtable, then a TAIL JMP to
// ~GroupDescription (0x180117c00). It owns nothing of its own. ENDGAME B.3.9
// UI-A: EMPTY; the compiler's chain.
namespace UI {
RadioDescription::~RadioDescription() {}
}  // namespace UI

namespace UI {
// ?CreateNewObject@RadioDescription@UI@@SAPEAVBase@Object@@XZ  0x180047760
// The factory-interpreter row this replaces: MemAlloc(0x80, 8), retail's
// null branch, then the out-of-line ctor ??0RadioDescription (0x1800d5b50).
Object::Base* RadioDescription::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(RadioDescription), 8);
    if (p == nullptr) return nullptr;
    return ::new (p) RadioDescription();
}
// ?CreateInPlaceObject@RadioDescription@UI@@SAPEAVBase@Object@@PEAX@Z  0x1800477e0
Object::Base* RadioDescription::CreateInPlaceObject(void* place) {
    if (place == nullptr) return nullptr;
    return ::new (place) RadioDescription();
}
}  // namespace UI

// ============================================================================
// ENDGAME B.3.9 TI-UI (B39 D6): UI::RadioDescription's own type record, built at CRT init
// (head-insert into Object::BaseTypeInfo::_firstTypeInfo). Literals copied from
// the retired minted row ?_typeInfo@RadioDescription@UI@@ (0x1801fa238): name "RadioDescription", parent
// "GroupDescription" (retail ?_ParentTypeName@RadioDescription@UI@@), no file extension; factories:
// the class's own.
// ============================================================================
DEFINE_TYPE(UI::RadioDescription, "GroupDescription", nullptr)
