#pragma once
// ============================================================================
// src/engine/ui/ButtonElement.h — UI::ButtonElement, the REAL C++ class
// ============================================================================
//
// ENDGAME B.3 step 6, block C1. Before this header the layout lived only as
// file-local constants in src/engine/ui/buttonelement.cpp (RULE B6-8); they
// are MOVED here under the class, and every field cites the store that proves
// it.
//
// PARENT `UI::Element` — READ, not inferred (B36_SLOTS.md section 3):
//     ?_ParentTypeName@ButtonElement@UI@@CAPEBDXZ  @0x180048230 -> "Element"
// RULE B5-15 / B6-7 — the two literals, RECORDED, NOT USED (no DEFINE_TYPE in
// step 6; slots 1/2 keep reading the minted `?_typeInfo@ButtonElement@UI@@…`,
// 0x1801fa938):
//     ?_TypeName@ButtonElement@UI@@CAPEBDXZ        @0x180048220 -> "ButtonElement"
//     ?_ParentTypeName@ButtonElement@UI@@CAPEBDXZ  @0x180048230 -> "Element"
//
// `??_7ButtonElement@UI@@6B@` @ 0x180157310, TWELVE slots (B36_SLOTS.md
// section 1, `.rdata` qwords; sizeof from `MOV EDX,0xd8` in slot 0):
//
//   [ 0] 0x1800485b0  (unexported) vector deleting dtor — the class's own (B.3.9)
//   [ 1] 0x180048260  TypeName
//   [ 2] 0x180048240  Type
//   [ 3] 0x180116870  Element::Persist        INHERITED
//   [ 4] 0x180110d10  ComputeSize             (also ComboElement's slot 4)
//   [ 5] 0x180110e70  ComputeLayout           (also ComboElement's slot 5)
//   [ 6] 0x180111020  Draw                    (also ComboElement's slot 6)
//   [ 7] 0x1801110b0  Update
//   [ 8] 0x180110cc0  Enable                  (also Check's and Combo's slot 8)
//   [ 9] 0x1801169e0  Element::Contains       INHERITED
//   [10] 0x180035620  Element::SetColor       INHERITED
//   [11] 0x180035630  Element::GetBorder      INHERITED (RETSLOT, RULE B6-2)
//
// TWO SUBCLASSES derive from it (RULE B6-9): `CheckElement` ("ButtonElement")
// and `ComboElement` ("ButtonElement"). Both inline this class's two ctor
// stores in their own ctors (B36_SLOTS.md section 7: ??0CheckElement
// 0x1801116a0 and ??0ComboElement 0x1801123f0 each stamp
// ??_7ButtonElement@UI@@6B@ before their own table).
//
// (Until ENDGAME B.3.9:) ABSTRACT, exactly like `UI::Element`: slot 0 stays `= 0` (the deleting-dtor
// row in src/engine/object/deleting_dtor.cpp is not retired). No real
// constructor: every live ButtonElement is built by
// `ButtonDescription::CreateInstance` (inlined ctor) or the `??0ButtonElement`
// DOOR and wears the MINTED table, which is why every door in
// buttonelement.cpp delegates with a QUALIFIED call.
//
// ---------------------------------------------------------------------------
// LAYOUT — 0xd8 bytes, six dwords past UI::Element (0xc0)
// ---------------------------------------------------------------------------
// `??0ButtonElement@UI@@QEAA@XZ` (0x180110c40) writes exactly two of them:
//     MOV dword [RBX+0xc0],0xffffffff ; MOV dword [RBX+0xc4],0
// (the same two stores ButtonDescription::CreateInstance inlines at
// 0x180111592 / 0x18011159c). The four colours at +0xc8..+0xd4 are written by
// NEITHER constructor: the non-virtual `ButtonDescription::CreateInstance`
// (0x1801108d0) copies them from the description's +0x90..+0x9c, and the copy
// ctor (0x180048460) copies all six dwords.
//
// ENDGAME B.3 step 9 (block UI-A, B39_RULES D1/D2): CONCRETE. The real public
// ctor is retail's `??0ButtonElement@UI@@QEAA@XZ` (0x180110c40) minus its vptr stores;
// slot 0 is the class's own vector deleting dtor; ~ButtonElement is public and
// virtual. The factory pair is the class's own; the minted
// `??_7ButtonElement@UI@@6B@`, its RT_DELDTOR row, the factory-interpreter row and
// the doors are retired.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include <type_traits>

#include "Element.h"
#include "../object/Internal.h"   // Object::detail::InternalMemFree

namespace System { class Time; }

namespace UI {

class ButtonElement : public Element {
public:
    // The +0xc4 `_state` machine. Values are retail's: Update (0x1801110b0) recomputes it each
    // call (Disabled > Pressed > Hovered > Normal), and the value is also the index of the
    // per-state background child (`_children[0..3]`) and of `_stateColor[0..3]`.
    enum class ButtonState : int32_t {
        Normal   = 0,   // store 0x1801111ce
        Disabled = 1,   // store 0x18011117e
        Hovered  = 2,   // store 0x1801111bc
        Pressed  = 3,   // store 0x1801111a6
    };

    // ??0ButtonElement@UI@@QEAA@XZ 0x180110c40 — the REAL constructor: CALL
    // ??0Element (Element()), then `MOV dword [+0xc0],-1 ; MOV dword [+0xc4],0`.
    // The four colours (+0xc8..+0xd4) are NOT written.
    ButtonElement() : Element() {
        _keyIndex = -1;
        _state    = ButtonState::Normal;
    }

    // ---- the twelve slots, in measured order ------------------------------
    // [0] 0x1800485b0 — MSVC's VECTOR deleting destructor, both arms exactly as
    // the retired RT_DELDTOR row ran them (`MOV EDX,0xd8`, the element size).
    // `??1ButtonElement@UI@@UEAA@XZ` (0x180110c90). EMPTY
    // (buttonelement.cpp): restamp, then ~Element, which the compiler chains.
    ~ButtonElement() override;
    // ENDGAME B.3.9 TI-UI: ONE reflection block (B39 D6); `_typeInfo` is the
    // class's own record (DEFINE_TYPE in the class's TU), no longer minted.
    // retail: TypeName 0x180048260, Type 0x180048240, TypeId 0x180048250, _TypeName 0x180048220
    //         _ParentTypeName 0x180048230, CreateNewObject 0x180048350, CreateInPlaceObject 0x1800483f0
    DECLARE_TYPE(ButtonElement)
public:
    // [3] Persist — Element's, inherited.
    void ComputeSize() override;                                  // [4] 0x180110d10
    void ComputeLayout(int32_t x, int32_t y,
                       int32_t w, int32_t h) override;            // [5] 0x180110e70
    void Draw(SpriteContext& context, uint8_t alpha) const override; // [6] 0x180111020
    // [7] `InternalPtr<DialogInstance>` arrives as a POINTER to the caller's
    // temporary — see Element.h's banner.
    void Update(const System::Time& time, UserInterface* ui,
                Object::InternalPtr<DialogInstance>* dialog,
                const UpdateState& state) override;               // [7] 0x1801110b0
    void Enable(bool enable) override;                            // [8] 0x180110cc0
    // [9] Contains, [10] SetColor, [11] GetBorder — Element's, inherited.

    // ---- exported, NOT PORTED anywhere in the tree: declared, deliberately
    // undefined (nothing in the tree calls them through a member).
    int32_t GetKeyId() const;                                     // 0x180048450
    void    SetKeyId(int32_t keyId);                              // 0x180048440

    ButtonElement(const ButtonElement& src);                      // 0x180048460  not ported (door retired: no caller)
    ButtonElement& operator=(const ButtonElement& src);           // 0x1800484f0  not ported


protected:
    // `?WasKeyPressed@ButtonElement@UI@@IEBA_NAEBUUpdateState@Element@2@@Z`
    // (0x1801108a0, `IEBA` = protected const). Not ported: Update inlines the
    // same KeyMap::Pressed test (0x1801112d4) rather than calling it.
    bool WasKeyPressed(const UpdateState& state) const;           // 0x1801108a0  not ported


private:

public:
    // ---- the 0x18 bytes ButtonElement adds (PUBLIC: unconverted bodies and
    // the ComboElement fragments reach them at these offsets) --------------
    int32_t  _keyIndex;       // +0xc0  ctor -1 (0x180110c40). The shortcut Update
                              //        hands `Input::KeyMap::Pressed` (0x1800a7a80)
                              //        at 0x1801112d4; < 0 means "no key".
    ButtonState _state;       // +0xc4  ctor 0. 0 normal, 1 disabled, 2 hovered,
                              //        3 pressed (Update's stores 0x1801111ce /
                              //        0x18011117e / 0x1801111bc / 0x1801111a6),
                              //        and the index of the child Draw/ComputeLayout
                              //        use as the state background.
    uint32_t _stateColor[4];  // +0xc8 .. +0xd4  NOT written by the ctor. The label
                              //        colour per state, pushed into `_children[4]`'s
                              //        slot 10 by Update (0x1801111c8 / 0x180111178 /
                              //        0x1801111b6 / 0x1801111a0) and [1] by Enable.
};

static_assert(sizeof(ButtonElement) == 0xd8,
              "UI::ButtonElement is 0xd8 bytes (MOV EDX,0xd8 in its slot-0 vector "
              "deleting destructor 0x1800485b0)");
static_assert(alignof(ButtonElement) == 8, "UI::ButtonElement aligns to 8 (the vptr)");
static_assert(offsetof(ButtonElement, _keyIndex) == sizeof(Element),
              "ButtonElement's own fields begin at the end of UI::Element");
static_assert(offsetof(ButtonElement, _keyIndex)   == 0xc0, "");
static_assert(offsetof(ButtonElement, _state)      == 0xc4, "");
static_assert(offsetof(ButtonElement, _stateColor) == 0xc8, "");
static_assert(sizeof(ButtonElement::ButtonState) == sizeof(int32_t), "_state stays a dword");
static_assert(std::has_virtual_destructor<ButtonElement>::value, "B3X W1: the normal-virtual cutover");
static_assert(!std::is_abstract<ButtonElement>::value, "B39 D1: concrete");

}  // namespace UI
