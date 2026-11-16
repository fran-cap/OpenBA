// ============================================================================
// src/engine/ui/pagedescription.cpp — UI::PageDescription, the whole class
// ============================================================================
//
// Vtable batch 2. Like UI::RadioDescription, this class DERIVES FROM
// UI::GroupDescription: its dtor tail-jumps to ~GroupDescription and its
// Serialize opens with GroupDescription::Serialize, both in
// src/engine/ui/groupdescription.cpp.
//
// ??_7PageDescription@UI@@6B@ (retail 0x1801570e0), ten slots:
//   0  deleting dtor      0x1800471a0  PageDescription.h (the class's own)
//   1  TypeName           0x180046ee0  here
//   2  Type               0x180046ec0  here
//   3  GetFileVersion     0x1800d54a0  here  (returns 0x1104)
//   4  External::Compile  0x1800066e0  already ported
//   5  Serialize          0x1800d5410  here
//   6  Reconstruct        0x180047060  here
//   7  Destruct           0x180047090  here
//   8  CreateInstance     0x1800d52b0  here
//   9  Construct          0x180047080  here
// plus ??0PageDescription@UI@@QEAA@XZ 0x1800d54e0 (out of line; the real ctor in
// the header since B.3.9), ??1PageDescription@UI@@UEAA@XZ 0x1800d54b0 and the
// factory pair.
//
// LAYOUT: src/engine/ui/PageDescription.h — GroupDescription (0x78) plus
// `_activePage` (+0x78); sizeof 0x80.
//
// ENDGAME B.3.6 block C2: every slot body is a MEMBER of `UI::PageDescription`.
// Serialize's base call (by address 0x180117d00 before) is the qualified member
// `GroupDescription::Serialize`.
//
// ENDGAME B.3.9 block UI-A (B39_RULES D1/D2): the class is CONCRETE; the slot
// doors and the ??0 / ??1 doors, read only by the retired minted table, its
// RT_DELDTOR row and ui_factory_wave28.cpp's row, are gone.
// ============================================================================
#include <cstddef>
#include <cstdint>
#include <new>

#include "description_common.h"
#include "ui/PageDescription.h"
#include "ui/PageElement.h"

namespace {

using namespace rtdesc;

}  // namespace

// ============================================================================
// The member bodies
// ============================================================================
namespace UI {

int32_t PageDescription::GetFileVersion() const { return 0x1104; }   // 0x1800d54a0

// 0x180047080: the null test sat in the retired door; then the out-of-line ctor
// (??0PageDescription 0x1800d54e0, the real ctor since B.3.9).
Object::External* PageDescription::Construct() {
    return ::new (this) PageDescription();
}
// 0x180047060: slot 0 with flag 0 (destroy WITHOUT freeing, through the
// object's own table), then the ctor again. No null test.
Object::External* PageDescription::Reconstruct() {
    this->~PageDescription();
    return ::new (this) PageDescription();
}
void PageDescription::Destruct() { this->~PageDescription(); }          // 0x180047090

// ============================================================================
// ?Serialize@PageDescription@UI@@UEAA_NAEAVStream@IO@@@Z   0x1800d5410
// ============================================================================
// GroupDescription::Serialize (NOT ElementDescription's — using the
// grandparent would drop every child), QUALIFIED (this object wears the
// leaf's table, whose slot 5 is this function), then one kScalarInt over
// `_activePage` through a SECOND ReaderWriter opened on the same stream.
bool PageDescription::Serialize(IO::Stream& streamv) {
    GroupDescription::Serialize(streamv);                       // 1800d542a

    IO::Serializer ser(streamv, 0);                             // 1800d545a (inlined ctor)

    ser._readerWriter->SerializeInt(_activePage, nullptr);

    return true;                                // ~Serializer 1800d547d (inlined)
}

// ============================================================================
// ?CreateInstance@PageDescription@UI@@...   retail 0x1800d52b0   (slot 8)
// ============================================================================
// RCX = this, RDX = hidden return slot, R8 = Dialog* (RULE B6-2).
//
// PageElement is 0xc8 bytes (`MOV EDX,0xc8` at 0x1800d52f8); construction
// stamps GroupElement then PageElement and zeroes ONE dword at +0xc0 -- the
// real PageElement ctor's stores (PageElement.h, ENDGAME B.3.9 UI-A).
//
// `Dialog::CreateElement` is a qualified member call (dialog.cpp).
//
// THE TAIL CALLS SetActivePage TWICE, WITH -1 FIRST. The first call forces the
// field away from whatever the child loop left and hides every page; the second
// applies the description's own value. Collapsing them to one call with the
// final value would skip the hide pass whenever the field already equalled it,
// because SetActivePage's own first guard returns early on equality.
Object::InternalPtr<Element>* PageDescription::CreateInstance(
        Object::InternalPtr<Element>* ret, const Dialog* dialog) const {
    void* raw = rtdesc::MemAlloc(0xc8, 8);                     // 1800d52f8 (inlined)

    PageElement* elem = nullptr;
    if (raw != nullptr)                                        // 1800d5323
        elem = ::new (raw) PageElement();                      // 1800d5328..1800d5344 inlined ctor

    Object::InternalPtr<Element> slot(elem);                   // 1800d5350
    UI::ElementDescription::CreateInstance(&slot);             // 1800d535d

    const int32_t count = _children._size;
    for (int32_t i = 0; i < count; ++i) {                      // 1800d5362 / 1800d53c9
        ElementDescription* desc = _children._data[i];         // 1800d5377

        // ?CreateElement@Dialog@UI@@ 0x180112ed0, its result into the same slot.
        slot = dialog->UI::Dialog::CreateElement(Object::ExternalPtr<ElementDescription>(desc));

        Element* child = slot.Get();                           // 1800d5388
        if (child != nullptr)
            child->_parent = elem;                             // 1800d5390

        rtcoll::RtColl_ArraySetSizeFill_180076900(&elem->_children,
                                               elem->_children._size + 1,
                                               &slot);         // 1800d53a9

        for (Element* n = elem; n != nullptr; n = n->_parent)  // 1800d53b1..1800d53c5
            n->_flags |= kElementFlagNeedsSizeCalc;
    }

    elem->SetActivePage(-1);                                   // 1800d53d4  0x1800d5110
    elem->SetActivePage(_activePage);                          // 1800d53df

    new (static_cast<void*>(ret)) Object::InternalPtr<Element>(elem);   // 1800d53e4
    return ret;
}

}  // namespace UI

// ??1PageDescription@UI@@UEAA@XZ  0x1800d54b0 -- restamp, then TAIL JMP to
// ~GroupDescription. Nothing of its own to release. ENDGAME B.3.9 UI-A: EMPTY;
// the compiler's chain.
namespace UI {
PageDescription::~PageDescription() {}
}  // namespace UI

namespace UI {
// ?CreateNewObject@PageDescription@UI@@SAPEAVBase@Object@@XZ  0x180046fd0
// The factory-interpreter row this replaces: MemAlloc(0x80, 8), retail's
// null branch, then the out-of-line ctor ??0PageDescription (0x1800d54e0).
Object::Base* PageDescription::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(PageDescription), 8);
    if (p == nullptr) return nullptr;
    return ::new (p) PageDescription();
}
// ?CreateInPlaceObject@PageDescription@UI@@SAPEAVBase@Object@@PEAX@Z  0x180047050
Object::Base* PageDescription::CreateInPlaceObject(void* place) {
    if (place == nullptr) return nullptr;
    return ::new (place) PageDescription();
}
}  // namespace UI

// ============================================================================
// ENDGAME B.3.9 TI-UI (B39 D6): UI::PageDescription's own type record, built at CRT init
// (head-insert into Object::BaseTypeInfo::_firstTypeInfo). Literals copied from
// the retired minted row ?_typeInfo@PageDescription@UI@@ (0x1801fa1c8): name "PageDescription", parent
// "GroupDescription" (retail ?_ParentTypeName@PageDescription@UI@@), no file extension; factories:
// the class's own.
// ============================================================================
DEFINE_TYPE(UI::PageDescription, "GroupDescription", nullptr)
