// ============================================================================
// src/engine/ui/checkelement.cpp — UI::CheckElement, the whole class
// ============================================================================
//
// 2026-09-12, vtable batch 3. `??_7CheckElement@UI@@6B@` (retail 0x180157260),
// a UI::ButtonElement subclass that adds one bool and one extra child.
//
//   slot  method                       retail       where
//   ----  ---------------------------  -----------  --------------------------
//    0    vector deleting dtor         0x180048d80  CheckElement.h (the class's own)
//    1    TypeName                     0x180048a80  here
//    2    Type                         0x180048a60  here
//    4    ComputeSize                  0x180111740  here
//    5    ComputeLayout                0x1801117b0  here
//    6    Draw                         0x180111800  here
//    7    Update                       0x180111870  here
//    8    ButtonElement::Enable        0x180110cc0  buttonelement.cpp
//
// plus ??0CheckElement@UI@@QEAA@XZ         0x1801116a0
//      ??0CheckElement@UI@@QEAA@AEBV01@@Z  0x180048ca0
//      ??1CheckElement@UI@@UEAA@XZ         0x180111700
//
// ENDGAME B.3.6 block C1: the six slot bodies are MEMBERS of
// `UI::CheckElement` (src/engine/ui/CheckElement.h, which now owns the
// layout: ButtonElement's 0xd8, then the check BYTE at +0xd8 and seven bytes
// of tail padding). Every call into ButtonElement's slot bodies is a QUALIFIED
// member call (it used to be a `ResolveAddr` by address). `_children[5]` is
// the tick mark: the only child index past ButtonElement's five, drawn only
// while `_checked` is set.
// ============================================================================
#include "element_common.h"
#include "ui/CheckElement.h"
#include "input/Input.h"   // ENDGAME B.3.7 callers (K5): Input::KeyMap

using namespace rtuielem;

namespace {

constexpr int kChildTick = 5;

// The ctor/dtor doors only.

}  // namespace

namespace UI {

// ============================================================================
// ?ComputeSize@CheckElement@UI@@UEAAXXZ   0x180111740
// ============================================================================
// Run the button's own sizing, then widen to fit the tick.
//
// The tick's contribution is its RAW `_layoutWidth`/`_layoutHeight` — NOT its
// padded size. This is the one place in the batch where a child's size is
// taken without the padding, and it is what 0x180111785..0x1801117a3 does.
//
// Bit 27 is cleared TWICE on this path: once by ButtonElement::ComputeSize and
// again at the end here. Reproduced; the second clear is a no-op unless the
// tick's own ComputeSize dirtied us back, which it can (it walks parents).
void CheckElement::ComputeSize() {
    if ((_flags & kElementFlagNeedsSizeCalc) == 0) return;

    ButtonElement::ComputeSize();

    Element* tick = _children._data[kChildTick];
    if (tick != nullptr) {
        tick->ComputeSize();
        const int w = tick->_layoutWidth;
        const int h = tick->_layoutHeight;
        if (_layoutWidth  < w) _layoutWidth  = w;
        if (_layoutHeight < h) _layoutHeight = h;
    }

    _flags &= ~UI::kElementFlagNeedsSizeCalc;
}

// ============================================================================
// ?ComputeLayout@CheckElement@UI@@UEAAXHHHH@Z   0x1801117b0
// ============================================================================
// ButtonElement's layout, then the tick over the same rect. Retail's tail is a
// JUMP into the tick's slot 5 (0x1801117f0), which is why Ghidra loses the
// fourth argument; the disassembly shows all four plus the stack slot.
void CheckElement::ComputeLayout(int32_t x, int32_t y, int32_t w, int32_t h) {
    ButtonElement::ComputeLayout(x, y, w, h);

    Element* tick = _children._data[kChildTick];
    if (tick != nullptr)
        tick->ComputeLayout(_layoutLeft, _layoutTop, _layoutRight, _layoutBottom);
}

// ============================================================================
// ?Draw@CheckElement@UI@@UEBAXAEAVSpriteContext@2@E@Z   0x180111800
// ============================================================================
// *** THE ALPHA IS HALVED TWICE WHEN THE DIM FLAG IS SET. *** This body halves
// it BEFORE calling ButtonElement::Draw, which tests the same bit on the same
// object and halves it again. That is what the shipped bytes do (0x180111800
// tests +0x34 and shifts, then 0x180111020 tests +0x34 and shifts), so a
// dimmed CheckElement is drawn at a quarter alpha and its tick at a quarter of
// that. Not a transcription slip — do not "fix" it.
void CheckElement::Draw(SpriteContext& context, uint8_t alpha) const {
    alpha = DimAlpha(this, alpha);

    ButtonElement::Draw(context, alpha);

    if (_checked != 0) {
        const Element* tick = _children._data[kChildTick];
        if (tick != nullptr) tick->Draw(context, alpha);
    }
}

// ============================================================================
// ?Update@CheckElement@UI@@   0x180111870
// ============================================================================
// Toggle on a click or on the shortcut key, then run ButtonElement::Update —
// which is what actually raises the activation event. The toggle happens
// FIRST, so the event a listener sees already reflects the new check state.
// UpdateState +0x04 is `leftReleased` (the mouse-up edge), +0x28 `config` (the KeyMap).
void CheckElement::Update(const System::Time& time, UserInterface* ui,
                          Object::InternalPtr<DialogInstance>* dialog,
                          const UpdateState& state) {
    const uint32_t flags = _flags;
    bool toggle = false;
    if ((flags & UI::kElementFlagAnyDisabled) == 0) {
        if (state.leftReleased != 0 && (flags & kElementFlagHovered) != 0 &&
            (flags & kElementFlagPressed) != 0) {
            toggle = true;
        } else {
            const int32_t key = _keyIndex;
            if (key >= 0 &&
                state.config->Pressed(0, key)) {
                toggle = true;
            }
        }
    }
    if (toggle) _checked = (_checked == 0) ? 1 : 0;

    Object::InternalPtr<DialogInstance> dlgCopy = *dialog;
    ButtonElement::Update(time, ui, &dlgCopy, state);
}

}  // namespace UI

// ============================================================================
// ??1CheckElement@UI@@UEAA@XZ  0x180111700 -- own vtable, the BASE vtable over
// it, then ~Element. ENDGAME B.3.9 UI-A: EMPTY; the compiler's chain through
// ~ButtonElement makes the same two restamps.
// ============================================================================
namespace UI {
CheckElement::~CheckElement() {}
}  // namespace UI

namespace UI {
// ?CreateNewObject@CheckElement@UI@@SAPEAVBase@Object@@XZ  0x180048b70
// The factory-interpreter row this replaces: MemAlloc(0xe0, 8), retail's
// null branch, then the inlined ctor (ButtonElement's stores, +0xd8 BYTE 0).
Object::Base* CheckElement::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(CheckElement), 8);
    if (p == nullptr) return nullptr;
    return ::new (p) CheckElement();
}
// ?CreateInPlaceObject@CheckElement@UI@@SAPEAVBase@Object@@PEAX@Z  0x180048c20
Object::Base* CheckElement::CreateInPlaceObject(void* place) {
    if (place == nullptr) return nullptr;
    return ::new (place) CheckElement();
}
}  // namespace UI

// ============================================================================
// ENDGAME B.3.9 TI-UI (B39 D6): UI::CheckElement's own type record, built at CRT init
// (head-insert into Object::BaseTypeInfo::_firstTypeInfo). Literals copied from
// the retired minted row ?_typeInfo@CheckElement@UI@@ (0x1801fa9a8): name "CheckElement", parent
// "ButtonElement" (retail ?_ParentTypeName@CheckElement@UI@@), no file extension; factories:
// the class's own.
// ============================================================================
DEFINE_TYPE(UI::CheckElement, "ButtonElement", nullptr)
