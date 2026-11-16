// ============================================================================
// src/engine/ui/pageelement.cpp — UI::PageElement
// ============================================================================
//
// `??_7PageElement@UI@@6B@` (retail 0x180157080). Only three slots are its
// own; 4/5/6 are `UI::GroupElement`'s bodies (0x180117f00 / 0x180118050 /
// 0x1801181c0) in groupelement.cpp, which the class simply does not override:
//
//   0  vector deleting dtor  0x180047540  -> PageElement.h (the class's own)
//   1  TypeName              0x180047270  here
//   2  Type                  0x180047250  here
//   4..6                     GroupElement's
//
// plus ??0PageElement (0x1800d5260, the real ctor in the header since B.3.9),
// ??1PageElement (0x1800d5220) and the factory pair. `GetActivePage` is inline in PageElement.h; `SetActivePage`
// is defined in widgets_layout.cpp.
//
// THE DOUBLE VTABLE STAMP IS REAL. Both the ctor and the dtor write
// `??_7GroupElement@UI@@6B@` and `??_7PageElement@UI@@6B@` into +0x00 back to
// back (ctor: Group then Page; dtor: Page then Group). That is MSVC emitting
// the base's stamp and the derived's over it -- exactly what the compiler's
// ctor / dtor chains through GroupElement do since B.3.9.
//
// LAYOUT: src/engine/ui/PageElement.h — sizeof 0xC8, `_activePage` at +0xC0.
//
// ENDGAME B.3.6 block C2: slots 1/2 are MEMBERS of `UI::PageElement`.
//
// ENDGAME B.3.9 block UI-A: the class is CONCRETE; the two slot doors and the
// ??0 / ??1 doors, read only by the retired minted table, its RT_DELDTOR row and
// ui_factory_wave28.cpp's row, are gone.
// ============================================================================


#include "widget_common.h"
#include "ui/PageElement.h"

using namespace rtui;

namespace {

}  // namespace

namespace UI {

// ??1PageElement@UI@@UEAA@XZ   0x1800d5220 -- Page stamp, Group stamp,
// ~Element. ENDGAME B.3.9 UI-A: EMPTY; the compiler's chain through
// ~GroupElement makes the same two restamps.
PageElement::~PageElement() {}

// ?CreateNewObject@PageElement@UI@@SAPEAVBase@Object@@XZ  0x180047360
// The factory-interpreter row this replaces: MemAlloc(0xc8, 8), retail's
// null branch, then the out-of-line ctor ??0PageElement (0x1800d5260).
Object::Base* PageElement::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(PageElement), 8);
    if (p == nullptr) return nullptr;
    return ::new (p) PageElement();
}
// ?CreateInPlaceObject@PageElement@UI@@SAPEAVBase@Object@@PEAX@Z  0x180047400
Object::Base* PageElement::CreateInPlaceObject(void* place) {
    if (place == nullptr) return nullptr;
    return ::new (place) PageElement();
}

}  // namespace UI

// ============================================================================
// ENDGAME B.3.9 TI-UI (B39 D6): UI::PageElement's own type record, built at CRT init
// (head-insert into Object::BaseTypeInfo::_firstTypeInfo). Literals copied from
// the retired minted row ?_typeInfo@PageElement@UI@@ (0x1801fa200): name "PageElement", parent
// "GroupElement" (retail ?_ParentTypeName@PageElement@UI@@), no file extension; factories:
// the class's own.
// ============================================================================
DEFINE_TYPE(UI::PageElement, "GroupElement", nullptr)
