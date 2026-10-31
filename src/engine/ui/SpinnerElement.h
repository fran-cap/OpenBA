#pragma once
// ============================================================================
// src/engine/ui/SpinnerElement.h — UI::SpinnerElement, binary-compatible layout
// ============================================================================
// Wave 7 lane D.
//
// SpinnerElement is a THIN WRAPPER. Every one of its seven exports (retail
// 0x1800d7c30..0x1800d7e10) opens the same way:
//
//     MOV RAX,[RCX+0xa0]      ; UI::Element::_children._data  (Element.h, lane A)
//     MOV RBX,[RAX]           ; _children._data[0] — the FIRST CHILD, not a
//                             ;   dedicated field of SpinnerElement's own
//     MOV EDX,[<typeid global>]
//     CALL IsDerivedFrom
//
// i.e. the "value" widget is the spinner's own children[0], read through the
// INHERITED Element array, and cast to NumberEditElement only if
// IsDerivedFrom agrees. This explains the existing warning in
// engine/funcs/engine_funcs.h ("SetMinMax dereferences +0xa0 which is NULL on
// SliderElements"): a SliderElement's _children is empty, so _data is null and
// the load through it faults — exactly the CLAUDE.md "faithful crash", not a
// bug to guard against.
//
// SpinnerElement's OWN additional state — beyond the inherited Element (0xC0)
// and the (inherited) _children array used above — is exactly one field:
//
//   +0xC0  _increment   int32   ?SetIncrement@SpinnerElement@UI@@QEAAXH@Z
//                                (0x1800d7d30) — one store, no exported reader.
//
// ENDGAME B.3.6 block C5: sizeof IS now asserted — 0xc8, from `MOV EDX,0xc8`
// in the slot-0 vector deleting dtor 0x18004b3b0 (B36_SLOTS.md section 1) and
// the `Allocate(0xc8, 8)` in `?CreateInstance@SpinnerDescription@` (0x1800d86f2).
// The ctor (0x1800d8670) writes only +0xc0 (= 1), so +0xc4..+0xc7 is tail
// padding nothing writes. The Wave 7 POD MOVED to `RtUI::SpinnerElement`
// (B4b-1) — widgets_input.cpp's seven doors still take it — and the retail
// name is the REAL class below.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "Element.h"





// ############################################################################
// ENDGAME B.3 step 6 (block C5) — `UI::SpinnerElement`, the REAL C++ class
// ############################################################################
//
// PARENT, read (B36_SLOTS.md section 3), recorded not used (RULE B6-7):
//     ?_TypeName@SpinnerElement@UI@@CAPEBDXZ       @0x18004b0d0 -> "SpinnerElement"
//     ?_ParentTypeName@SpinnerElement@UI@@CAPEBDXZ @0x18004b0e0 -> "Element"
//
// `??_7SpinnerElement@UI@@6B@` @ 0x180157660, TWELVE slots; sizeof 0xc8:
//   [0] 0x18004b3b0 deleting dtor (RT_DELDTOR row)  [1] 0x18004b110 TypeName
//   [2] 0x18004b0f0 Type        [3] Element::Persist (inherited)
//   [4] 0x1800d83b0 ComputeSize [5] 0x1800d80c0 ComputeLayout
//   [6] 0x1800d8030 Draw        [7] 0x1800d7e70 Update — OVERRIDDEN
//   [8..11] Element's (inherited)
//
// ENDGAME B.3 step 9 (block UI-B, B39_RULES D1/D2) -- the paragraph below is
// HISTORY: the class is now CONCRETE. A real public ctor (retail's stores), its
// own slot 0 (the vector deleting dtor), a virtual ~SpinnerElement holding
// the retired ??1 door's body, and the factory pair as the class's own statics;
// every live one wears the COMPILER's table. The minted `??_7SpinnerElement@UI@@6B@`,
// its RT_DELDTOR row 0x18004b3b0 and the ctor / dtor / slot doors are retired
// (tools/rt_mint_retired/ui-b.txt).
// ABSTRACT (slot 0 `= 0`), NO real ctor (the `??0SpinnerElement` door
// 0x1800d8670 builds every live one; it wears the MINTED table).
// ############################################################################
namespace UI {

class SpinnerElement : public Element {
public:
    // ??0SpinnerElement@UI@@QEAA@XZ 0x1800d8670 -- the REAL ctor (retail
    // also inlines it into SpinnerDescription::CreateInstance): `CALL
    // ??0Element`, then +0xc0 = 1. +0xc4 is written by nothing.
    SpinnerElement() : Element() {
        _increment = 1;
    }

    // [0] 0x18004b3b0 -- MSVC's VECTOR deleting destructor (retail; here the
    // virtual destructor, B3X W1) (`MOV EDX,0xc8` = the element size).
    // `??1SpinnerElement@UI@@UEAA@XZ` (0x1800d8640): VIRTUAL (the normal-virtual
    // cutover, B3X W1). Retail's body is in spinnerelement.cpp;
    // the compiler chains the base's.
    ~SpinnerElement() override;
    // ENDGAME B.3.9 TI-UI: ONE reflection block (B39 D6); `_typeInfo` is the
    // class's own record (DEFINE_TYPE in the class's TU), no longer minted.
    // retail: TypeName 0x18004b110, Type 0x18004b0f0, _TypeName 0x18004b0d0, _ParentTypeName 0x18004b0e0
    DECLARE_TYPE(SpinnerElement)
public:
    void ComputeSize() override;                                  // [4] 0x1800d83b0
    void ComputeLayout(int32_t x, int32_t y,
                       int32_t w, int32_t h) override;            // [5] 0x1800d80c0
    void Draw(SpriteContext& context, uint8_t alpha) const override; // [6] 0x1800d8030
    // [7] 0x1800d7e70 — the handle arrives as the ADDRESS of the caller's
    // temporary (Element.h's banner), hence the pointer.
    void Update(const System::Time& time, UserInterface* ui,
                Object::InternalPtr<DialogInstance>* dialog,
                const UpdateState& state) override;

    // ---- non-virtual members ----------------------------------------------
    // DEFINED in src/engine/ui/widgets_input.cpp (fragment pass), except
    // GetMinValue; the registered doors delegate to them.
    int32_t GetMaxValue() const;                                  // 0x1800d7cb0
    int32_t GetValue() const;                                     // 0x1800d7cf0
    bool    IsActive() const;                                     // 0x1800d7c30
    void    SetElementId(int32_t id);                             // 0x1800d7e10
    void    SetMinMax(int32_t minValue, int32_t maxValue);        // 0x1800d7d40
    void    SetValue(int32_t value);                              // 0x1800d7dc0
    int32_t GetMinValue() const;                                  // 0x1800d7c70  not ported
    // One instruction, read as disassembly: MOV [RCX+0xc0],EDX ; RET.
    void    SetIncrement(int32_t increment) { _increment = increment; }  // 0x1800d7d30

    SpinnerElement(const SpinnerElement& src);                    // 0x18004b2e0  not ported
    SpinnerElement& operator=(const SpinnerElement& src);         // 0x18004b330  not ported

    // EXT SEAM (NOT RETAIL, issue #73): ext.phone_menu's touch size -- null by default and under the
    // faithful profile (DESIGN.md hook rule 5). While it reads true, the up and down buttons are laid out
    // side by side to the right of the edit field instead of stacked (two finger-sized buttons stacked
    // would make the row twice their height); false or null is retail's body exactly.
    static const bool* _extSideBySide;

private:

public:
    int32_t  _increment;   // +0xc0  ctor 1; the step Update adds / subtracts
    uint32_t _padC4;       // +0xc4  written by nothing
};

static_assert(sizeof(SpinnerElement) == 0xc8,
              "UI::SpinnerElement is 0xc8 (MOV EDX,0xc8 in 0x18004b3b0)");
static_assert(offsetof(SpinnerElement, _padC4) == 0xC4, "SpinnerElement::_padC4");
static_assert(alignof(SpinnerElement) == 8, "UI::SpinnerElement aligns to 8");
static_assert(offsetof(SpinnerElement, _increment) == sizeof(Element), "");

} // namespace UI
