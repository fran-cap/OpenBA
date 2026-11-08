// ============================================================================
// src/engine/ui/radioelement.cpp — UI::RadioElement
// ============================================================================
//
// `??_7RadioElement@UI@@6B@` (retail 0x1801573c0). Slots 4/5/6 are
// `UI::GroupElement`'s (groupelement.cpp), which the class does not override:
//
//   0  vector deleting dtor  0x180047d50  -> RadioElement.h (the class's own)
//   1  TypeName              0x180047a00  here
//   2  Type                  0x1800479e0  here
//   4..6                     GroupElement's
//   7  Update                0x1800d5580  here
//
// plus ??0RadioElement (0x1800d5870, the real ctor in the header since B.3.9),
// ??1RadioElement (0x1800d5820) and the factory pair.
//
// NOT HERE: the two `?FindCheckBoxes@RadioElement@UI@@` (public 0x180047c00,
// private 0x1800d5750) and `?SetActiveRadio@` (0x1800d56f0) — widgets_input.cpp
// doors (fragment pass); declared on the class.
//
// LAYOUT: src/engine/ui/RadioElement.h — `_checkBoxes` (+0xC0),
// `_activeIndex` (+0xD0, ctor -1); sizeof 0xD8.
//
// ENDGAME B.3.6 block C2: slots 1/2/7 are MEMBERS of `UI::RadioElement`; the
// registered doors are one-line QUALIFIED delegations. Update's base call (the
// by-address `ElementUpdate` door before) is the qualified member
// `GroupElement::Update`, i.e. `Element::Update` (0x180116270).
//
// ENDGAME B.3.9 block UI-A: the class is CONCRETE; the slot doors and the
// ??0 / ??1 doors, read only by the retired minted table, its RT_DELDTOR row and
// ui_factory_wave28.cpp's row, are gone.
// ============================================================================

#include <cstring>

#include "widget_common.h"
#include "ui/RadioElement.h"
#include "ui/CheckElement.h"
#include "ui/UserInterface.h"

using namespace rtui;

namespace {


}  // namespace

namespace UI {

// ---------------------------------------------------------------------------
// slot 7 — ?Update@RadioElement@UI@@ ...   0x1800d5580
//
// Poll the checkboxes; whichever one reports "was activated" this frame either
// becomes the selection (if it is now checked) or clears it (-1). On a change
// the other boxes are unchecked and ONE event of kind 0x16 is queued.
//
// The base call is QUALIFIED (slot 7 of this object's table is this very
// function). The handle is copied into a local and the local's
// ADDRESS forwarded, as retail's frame does (RULE B6-11).
// ---------------------------------------------------------------------------
void RadioElement::Update(const System::Time& time, UserInterface* ui,
                          Object::InternalPtr<DialogInstance>* dialog,
                          const UpdateState& state) {
    Object::InternalPtr<DialogInstance> slot = *dialog;
    GroupElement::Update(time, ui, &slot, state);   // Element::Update 0x180116270

    const int32_t count = _checkBoxes._size;
    int32_t active = _activeIndex;
    int32_t value  = -1;

    if (count > 0) {
        for (int32_t i = 0; i < count; ++i) {
            Element* box = _checkBoxes._data[i];
            if ((box->_flags & kElementFlagWasActivated) != 0) {
                active = -1;
                // The radio's boxes are the CheckElements its description
                // built (RadioDescription::CreateInstance): +0xd8 `_checked`.
                if (static_cast<const CheckElement*>(box)->_checked != 0) active = i;
            }
        }
    }

    if (_activeIndex == active) return;

    _activeIndex = active;

    // Uncheck everything that is not the new selection. The loop re-reads the
    // count from +0xC8 each iteration, as shipped.
    for (int32_t i = 0; i < _checkBoxes._size; ++i) {
        if (i != _activeIndex)
            static_cast<CheckElement*>(_checkBoxes._data[i])->_checked = 0;
    }

    if (_activeIndex >= 0)
        value = _checkBoxes._data[_activeIndex]->_elementId;

    UserInterface::Event ev;
    ev.dialog   = dialog->Get();     // the handle's 8 bytes
    ev.element  = this;
    ev.eventId  = UI::EventId::RadioSelected;
    ev.elementId    = _elementId;
    ev.mouseX    = state.mouseX;
    ev.mouseY    = state.mouseY;
    ev.value    = value;
    ev.userData = nullptr;
    rtcoll::RtColl_ArraySetSizeFill_180033690(&ui->_events, ui->_events._size + 1, &ev);
}

// ============================================================================
// ??1RadioElement@UI@@UEAA@XZ   0x1800d5820
// ============================================================================
// Retail: own stamp, the check-box array's J1 `~Array` (0x1800490e0), the
// GroupElement stamp, ~Element. ENDGAME B.3.9 UI-A: EMPTY; the member
// `_checkBoxes.~Array()` is that J1 body for a trivially destructible element
// (Collection.h), and the restamps and ~Element are the compiler's chain
// through ~GroupElement.
RadioElement::~RadioElement() {}

// ?CreateNewObject@RadioElement@UI@@SAPEAVBase@Object@@XZ  0x180047af0
// The factory-interpreter row this replaces: MemAlloc(0xd8, 8), retail's
// null branch, then the out-of-line ctor ??0RadioElement (0x1800d5870).
Object::Base* RadioElement::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(RadioElement), 8);
    if (p == nullptr) return nullptr;
    return ::new (p) RadioElement();
}
// ?CreateInPlaceObject@RadioElement@UI@@SAPEAVBase@Object@@PEAX@Z  0x180047ba0
Object::Base* RadioElement::CreateInPlaceObject(void* place) {
    if (place == nullptr) return nullptr;
    return ::new (place) RadioElement();
}

}  // namespace UI

// ============================================================================
// ENDGAME B.3.9 TI-UI (B39 D6): UI::RadioElement's own type record, built at CRT init
// (head-insert into Object::BaseTypeInfo::_firstTypeInfo). Literals copied from
// the retired minted row ?_typeInfo@RadioElement@UI@@ (0x1801fa270): name "RadioElement", parent
// "GroupElement" (retail ?_ParentTypeName@RadioElement@UI@@), no file extension; factories:
// the class's own.
// ============================================================================
DEFINE_TYPE(UI::RadioElement, "GroupElement", nullptr)
