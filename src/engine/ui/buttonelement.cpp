// ============================================================================
// src/engine/ui/buttonelement.cpp — UI::ButtonElement, the whole class
// ============================================================================
//
// 2026-09-12, vtable batch 3. `??_7ButtonElement@UI@@6B@` (retail 0x180157310).
//
//   slot  method                retail       where
//   ----  --------------------  -----------  ---------------------------------
//    0    vector deleting dtor  0x1800485b0  ButtonElement.h (the class's own)
//    1    TypeName              0x180048260  here
//    2    Type                  0x180048240  here
//    4    ComputeSize           0x180110d10  here
//    5    ComputeLayout         0x180110e70  here
//    6    Draw                  0x180111020  here
//    7    Update                0x1801110b0  here
//    8    Enable                0x180110cc0  here
//
// plus ??1ButtonElement@UI@@UEAA@XZ 0x180110c90 and the factory pair (ENDGAME
// B.3.9 UI-A: the real ctor ??0 0x180110c40 is in the header; the copy-ctor
// door is retired).
//
// Slots 4/5/6/8 are ALSO ComboElement's and slot 8 is ALSO CheckElement's — the
// two subclasses repeat these addresses in their own tables. They are
// registered once, here.
//
// ENDGAME B.3.6 block C1: the seven slot bodies are MEMBERS of
// `UI::ButtonElement` (src/engine/ui/ButtonElement.h, which now owns the
// layout this file used to carry as file-local constants — the per-field
// evidence is there). ENDGAME B.3.9 UI-A retired the slot doors and the
// ctor/dtor doors with the minted table: the class is concrete.
//
// The four colours are handed to `_children[4]`'s virtual `SetColor` (slot 10),
// which is what makes `_children[4]` the button's LABEL and `_children[0..3]`
// its four state backgrounds.
//
// *** NEITHER CONSTRUCTOR INITIALISES +0xc8..+0xd4. *** The default ctor writes
// only +0xc0 and +0xc4; the four colours come from ButtonDescription. Do not
// byte-compare a freshly constructed ButtonElement past +0xc8.
// ============================================================================
#include "element_common.h"
#include "ui/ButtonElement.h"
#include "input/Input.h"   // ENDGAME B.3.7 callers (K5): Input::KeyMap
#include "ui/UserInterface.h"

using namespace rtuielem;

namespace {

// `_children` indices this class dispatches on.
constexpr int kChildLabel = 4;

// Collection::Array<UI::UserInterface::Event>::SetSize(array, newSize, &fill)

// UI::UserInterface (block E's class, still a struct): the pending-event array
// at +0x48 {data, size, capacity} (src/engine/ui/UserInterface.h,
// `Collection::Array<Event> _events`).

// UI::UserInterface::Event, 0x30 bytes (the stride UserInterface.h pins from
// CleanRemovedDialogs). The seven stores at 0x180111356..0x18011139e.
struct Event {
    void*    dialog;      // +0x00  the InternalPtr<DialogInstance> value
    void*    element;     // +0x08
    UI::EventId kind;     // +0x10
    int32_t  elementId;   // +0x14
    int32_t  x;           // +0x18
    int32_t  y;           // +0x1c
    int32_t  index;       // +0x20
    int32_t  pad24;       // +0x24  NEVER WRITTEN by this path
    uint64_t tail;        // +0x28
};
static_assert(sizeof(Event) == 0x30, "UI event record stride");

}  // namespace

// ============================================================================
// The member bodies
// ============================================================================
namespace UI {

// ============================================================================
// ?ComputeSize@ButtonElement@UI@@UEAAXXZ   0x180110d10
// ============================================================================
// The union of the label's padded size and the current state background's,
// floored by the element's own minimum.
//
// TWO CALLS, TWO SHAPES, AND THAT IS RETAIL: the LABEL goes through the
// out-of-line `?GetPaddedSize@Element@UI@@` (0x180115f10, a member of
// UI::Element since block A) writing straight into +0x60/+0x64; the STATE
// BACKGROUND has the same function INLINED and compared field by field.
// Collapsing the two into one helper would hide that the first writes `this`
// and the second does not.
//
// `_children[4]` and `_children[_state]` are both dereferenced WITHOUT a bounds
// check — a ButtonElement always has five children because ButtonDescription
// builds them.
void ButtonElement::ComputeSize() {
    if ((_flags & kElementFlagNeedsSizeCalc) == 0) return;

    _layoutWidth  = 0;                                     // one qword store over
    _layoutHeight = 0;                                     // +0x60 and +0x64

    Element* label = _children._data[kChildLabel];
    if (label != nullptr) label->GetPaddedSize(_layoutWidth, _layoutHeight);

    Element* bg = _children._data[static_cast<int32_t>(_state)];
    if (bg != nullptr) {
        int w = 0, h = 0;
        PaddedSizeInline(bg, &w, &h);
        if (_layoutWidth  < w) _layoutWidth  = w;
        if (_layoutHeight < h) _layoutHeight = h;
    }

    if (_layoutWidth  < _minWidth)  _layoutWidth  = _minWidth;
    if (_layoutHeight < _minHeight) _layoutHeight = _minHeight;

    _flags &= ~UI::kElementFlagNeedsSizeCalc;
}

// ============================================================================
// ?ComputeLayout@ButtonElement@UI@@UEAAXHHHH@Z   0x180110e70
// ============================================================================
// The standard aligned body, then the LABEL laid out over the computed rect
// (from the registers still holding it) and the STATE BACKGROUND laid out over
// the rect RE-READ FROM THE FIELDS. The two are the same numbers; the second
// re-read is retail's and is kept.
void ButtonElement::ComputeLayout(int32_t x, int32_t y, int32_t w, int32_t h) {
    ElementComputeAlignedRect(this, x, y, w, h);

    Element* label = _children._data[kChildLabel];
    if (label != nullptr)
        label->ComputeLayout(_layoutLeft, _layoutTop, _layoutRight, _layoutBottom);

    Element* bg = _children._data[static_cast<int32_t>(_state)];
    if (bg != nullptr)
        bg->ComputeLayout(_layoutLeft, _layoutTop, _layoutRight, _layoutBottom);
}

// ============================================================================
// ?Draw@ButtonElement@UI@@UEBAXAEAVSpriteContext@2@E@Z   0x180111020
// ============================================================================
// Border, dim, then the STATE BACKGROUND and the LABEL — in that order, so the
// label paints on top.
void ButtonElement::Draw(SpriteContext& context, uint8_t alpha) const {
    const Element* border = _border;
    if (border != nullptr) border->Draw(context, alpha);

    alpha = DimAlpha(this, alpha);

    const Element* bg = _children._data[static_cast<int32_t>(_state)];
    if (bg != nullptr) bg->Draw(context, alpha);

    const Element* label = _children._data[kChildLabel];
    if (label != nullptr) label->Draw(context, alpha);
}

// ============================================================================
// ?Update@ButtonElement@UI@@   0x1801110b0
// ============================================================================
// The whole button state machine. Reading order matters throughout: retail
// RE-READS `_flags` from memory between almost every step, because the dirty
// walk it runs in between writes to it.
//
//   Element::Update(this, time, ui, &dlgCopy, state)
//   if (_flags & hidden) return
//   prev = _state
//   _flags &= ~bit22                                 (was-activated, cleared)
//   if (state.mouseDown && (_flags & bit17))         bit17 == "mouse is over"
//        _flags |= bit21                             bit21 == "press started here"
//   _state = disabled ? 1 (Disabled)
//          : (bit17 && bit21 && state.mouseHeld) ? 3 (Pressed)
//          : bit17 ? 2 (Hovered) : 0 (Normal)        ... and `colour` follows it
//   if (_state != prev) { SetNeedsSizeCalc(this);
//                         _children[_state]->ComputeLayout(rect) }
//   if (_children[4]) _children[4]->SetColor(colour)
//   if (!(_flags & bit17)) _flags &= ~bit21
//   if (_state != 1) {                               (not Disabled)
//        if (state.mouseDown && bit17 && bit21)  UserInterface::Play(ui, 4)
//        if ((state.mouseUp && bit17 && bit21) || (_key >= 0 && KeyMap::Pressed(...))) {
//             _flags &= ~bit21 ; _flags |= bit22
//             push Event{dlg, this, 0x12, _elementId, mouseX, mouseY, 0, 0}
//             UserInterface::Play(ui, 5)
//        }
//   }
//
// UpdateState bytes read here (Element.h names them): +0x01 `buttonPressed`
// (the mouse-down edge), +0x04 `b04` (the mouse-up edge), +0x07 `leftDown`
// (held), +0x18/+0x1c the mouse, +0x28 `config` (the Input::KeyMap*).
//
// The base call is QUALIFIED (`Element::Update`): an unqualified one would
// dispatch through this object's table straight back here.
//
// *** THE `_children[_state]` DEREF AFTER A STATE CHANGE IS UNGUARDED IN
// RETAIL *** (0x180111223 loads the vtable with no null test, unlike the
// `_children[4]` test two instructions later). A ButtonElement always has its
// four state children; the asymmetry is retail's own and no guard is added.
void ButtonElement::Update(const System::Time& time, UserInterface* ui,
                           Object::InternalPtr<DialogInstance>* dialog,
                           const UpdateState& state) {
    Object::InternalPtr<DialogInstance> dlgCopy = *dialog;
    Element::Update(time, ui, &dlgCopy, state);

    uint32_t flags = _flags;
    if ((flags & UI::kElementFlagHidden) != 0) return;

    const ButtonState prevState = _state;
    StoreFlags(this, flags & ~kElementFlagWasActivated);

    if (state.buttonPressed != 0) {
        const uint32_t f = _flags;
        if ((f & kElementFlagHovered) != 0) StoreFlags(this, f | kElementFlagPressed);
    }

    uint32_t colour;
    flags = _flags;
    if ((flags & UI::kElementFlagAnyDisabled) != 0) {
        colour = _stateColor[1];
        _state = ButtonState::Disabled;
    } else if ((flags & kElementFlagHovered) != 0 && (flags & kElementFlagPressed) != 0 &&
               state.leftDown != 0) {
        colour = _stateColor[3];
        _state = ButtonState::Pressed;
    } else if ((flags & kElementFlagHovered) != 0) {
        colour = _stateColor[2];
        _state = ButtonState::Hovered;
    } else {
        colour = _stateColor[0];
        _state = ButtonState::Normal;
    }

    if (_state != prevState) {
        rtuielem::SetNeedsSizeCalc(this);
        Element* bg = _children._data[static_cast<int32_t>(_state)];
        bg->ComputeLayout(_layoutLeft, _layoutTop, _layoutRight, _layoutBottom);
    }

    Element* label = _children._data[kChildLabel];
    if (label != nullptr) label->SetColor(colour);

    flags = _flags;
    if ((flags & kElementFlagHovered) == 0) StoreFlags(this, flags & ~kElementFlagPressed);

    if (_state == ButtonState::Disabled) return;

    if (state.buttonPressed != 0) {
        const uint32_t f = _flags;
        if ((f & kElementFlagHovered) != 0 && (f & kElementFlagPressed) != 0)
            static_cast<UI::UserInterface*>(ui)->Play(4);
    }

    bool fire = false;
    if (state.b04 != 0) {
        const uint32_t f = _flags;
        if ((f & kElementFlagHovered) != 0 && (f & kElementFlagPressed) != 0) fire = true;
    }
    if (!fire) {
        const int32_t key = _keyIndex;
        if (key < 0) return;                                   // 1801112d4 JS -> end
        if (!state.config->Pressed(0, key)) return;
    }

    StoreFlags(this, _flags & ~kElementFlagPressed);
    StoreFlags(this, _flags |  kElementFlagWasActivated);

    ::Event ev;                                // this file's record, not UI::Event
    ev.dialog    = dialog->Get();
    ev.element   = this;
    ev.kind      = EventId::Activated;
    ev.elementId = _elementId;
    ev.x         = state.mouseX;
    ev.y         = state.mouseY;
    ev.index     = 0;
    ev.tail      = 0;
    rtcoll::RtColl_ArraySetSizeFill_180033690(&ui->_events, ui->_events._size + 1, &ev);
    static_cast<UI::UserInterface*>(ui)->Play(5);
}

// ============================================================================
// ?Enable@ButtonElement@UI@@UEAAX_N@Z   0x180110cc0
// ============================================================================
// Element::Enable first (QUALIFIED), then — only when the element ended up
// disabled — push the DISABLED colour (+0xcc) into the label. Retail's tail is
// an indirect JUMP through slot 10, which is why Ghidra prints "could not
// recover jumptable"; behaviourally a call plus return.
void ButtonElement::Enable(bool enable) {
    Element::Enable(enable);

    Element* label = _children._data[kChildLabel];
    if (label != nullptr && (_flags & UI::kElementFlagAnyDisabled) != 0)
        label->SetColor(_stateColor[1]);
}

}  // namespace UI

// ============================================================================
// ??1ButtonElement@UI@@UEAA@XZ   0x180110c90 -- restamp, then ~Element.
// ENDGAME B.3.9 UI-A: EMPTY; the compiler restamps and chains ~Element.
// ============================================================================
namespace UI {
ButtonElement::~ButtonElement() {}
}  // namespace UI

namespace UI {
// ?CreateNewObject@ButtonElement@UI@@SAPEAVBase@Object@@XZ  0x180048350
// The factory-interpreter row this replaces: MemAlloc(0xd8, 8), retail's
// null branch, then the inlined ctor (Element(), -1, 0).
Object::Base* ButtonElement::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(ButtonElement), 8);
    if (p == nullptr) return nullptr;
    return ::new (p) ButtonElement();
}
// ?CreateInPlaceObject@ButtonElement@UI@@SAPEAVBase@Object@@PEAX@Z  0x1800483f0
Object::Base* ButtonElement::CreateInPlaceObject(void* place) {
    if (place == nullptr) return nullptr;
    return ::new (place) ButtonElement();
}
}  // namespace UI

// ============================================================================
// ENDGAME B.3.9 TI-UI (B39 D6): UI::ButtonElement's own type record, built at CRT init
// (head-insert into Object::BaseTypeInfo::_firstTypeInfo). Literals copied from
// the retired minted row ?_typeInfo@ButtonElement@UI@@ (0x1801fa938): name "ButtonElement", parent
// "Element" (retail ?_ParentTypeName@ButtonElement@UI@@), no file extension; factories:
// the class's own.
// ============================================================================
DEFINE_TYPE(UI::ButtonElement, "Element", nullptr)
