#pragma once
// ============================================================================
// src/engine/ui/ProgressElement.h — UI::ProgressElement, binary-compatible layout
// ============================================================================
// Wave 7 lane D. Two one-instruction setters pin two fields directly:
//
//   +0x124  _maxValue      float   ?SetMaximumValue@ProgressElement@UI@@QEAAXM@Z (0x180077ec0)
//   +0x128  _currentValue  float   ?SetCurrentValue@ProgressElement@UI@@QEAAXM@Z (0x180077ed0)
//
// Both are a single unconditional store, no compare, no dirty flag, no
// exported reader. `?SetProgressTextParameter@` (0x180119b30, held back — see
// WAVE7_LANE_D.md §3) resolves a CHILD element via the same "children[0]"
// pattern SpinnerElement uses and writes into THAT child's own parameter
// array; it touches no field of ProgressElement itself, so nothing about it
// is modeled here.
//
// *** THE GAP +0xC0..+0x123 IS REVERSED. *** It said "not reversed by this
// lane" until the elem_B batch ported the class (src/engine/ui/
// progresselement.cpp, 2026-09-12) from the ctor (0x180119C40), ComputeSize
// (0x1801198F0), ComputeLayout (0x180119610) and Draw (0x1801192C0). It is six
// UI::Sprites and one colour:
//
//   +0xC0  _fillLeft    +0xD0 _fillMiddle    +0xE0  _fillRight     the bar
//   +0xF0  _trackLeft   +0x100 _trackMiddle  +0x110 _trackRight    the groove
//   +0x120 _backgroundColor  uint32, ?SetBackgroundColor@ 0x180077EE0
//
// The ctor writes all six sprites as {null sheet, index -1} and the colour as
// 0xFFFFFFFF, then 1.0f and 0.25f into the two floats below. sizeof is 0x130,
// from the vector deleting destructor's `MOV EDX,0x130` at 0x180078182.
//
// ENDGAME B.3.6 block C5: the Wave 7 POD MOVED to `RtUI::ProgressElement`
// (B4b-1) — widgets_input.cpp's two setter doors still take it — and the retail
// name is the REAL class below, cross-asserted field for field.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "Element.h"
#include "Sprite.h"





// ############################################################################
// ENDGAME B.3 step 6 (block C5) — `UI::ProgressElement`, the REAL C++ class
// ############################################################################
//
// PARENT, read (B36_SLOTS.md section 3), recorded not used (RULE B6-7):
//     ?_TypeName@ProgressElement@UI@@CAPEBDXZ       @0x180077d00 -> "ProgressElement"
//     ?_ParentTypeName@ProgressElement@UI@@CAPEBDXZ @0x180077d10 -> "Element"
//
// `??_7ProgressElement@UI@@6B@` @ 0x180159180, TWELVE slots; sizeof 0x130:
//   [0] 0x180078150 deleting dtor (RT_DELDTOR row)  [1] 0x180077d40 TypeName
//   [2] 0x180077d20 Type        [3] Element::Persist (inherited)
//   [4] 0x1801198f0 ComputeSize [5] 0x180119610 ComputeLayout
//   [6] 0x1801192c0 Draw        [7..11] Element's (inherited — Update too)
//
// ENDGAME B.3 step 9 (block UI-B, B39_RULES D1/D2) -- the paragraph below is
// HISTORY: the class is now CONCRETE. A real public ctor (retail's stores), its
// own slot 0 (the vector deleting dtor), a virtual ~ProgressElement holding
// the retired ??1 door's body, and the factory pair as the class's own statics;
// every live one wears the COMPILER's table. The minted `??_7ProgressElement@UI@@6B@`,
// its RT_DELDTOR row 0x180078150 and the ctor / dtor / slot doors are retired
// (tools/rt_mint_retired/ui-b.txt).
// ABSTRACT (slot 0 `= 0`), NO real ctor: every live one is built by the
// `??0ProgressElement@UI@@QEAA@XZ` door (0x180119c40, progresselement.cpp)
// and wears the MINTED table.
// ############################################################################
namespace UI {

class ProgressElement : public Element {
public:
    // ??0ProgressElement@UI@@QEAA@XZ 0x180119c40 -- the REAL ctor: `CALL
    // ??0Element`; the six UI::Sprite members' own ctors make {null, -1};
    // then the three tail stores. +0x12c is written by nothing.
    ProgressElement() : Element() {
        _backgroundColor = System::kColorWhite;
        _maxValue        = 1.0f;      // 0x3f800000
        _currentValue    = 0.25f;     // 0x3e800000
    }

    // [0] 0x180078150 -- MSVC's VECTOR deleting destructor (retail; here the
    // virtual destructor, B3X W1) (`MOV EDX,0x130` = the element size).
    // `??1ProgressElement@UI@@UEAA@XZ` (0x180077ef0): VIRTUAL (the normal-virtual
    // cutover, B3X W1). Retail's body is in progresselement.cpp;
    // the compiler chains the base's.
    ~ProgressElement() override;
    // ENDGAME B.3.9 TI-UI: ONE reflection block (B39 D6); `_typeInfo` is the
    // class's own record (DEFINE_TYPE in the class's TU), no longer minted.
    // retail: TypeName 0x180077d40, Type 0x180077d20, _TypeName 0x180077d00, _ParentTypeName 0x180077d10
    DECLARE_TYPE(ProgressElement)
public:
    void ComputeSize() override;                                  // [4] 0x1801198f0
    void ComputeLayout(int32_t x, int32_t y,
                       int32_t w, int32_t h) override;            // [5] 0x180119610
    void Draw(SpriteContext& context, uint8_t alpha) const override; // [6] 0x1801192c0

    // ---- non-virtual members ----------------------------------------------
    // One instruction each, read as disassembly this block. The registered
    // doors for the two float setters are widgets_input.cpp's (fragment pass).
    void SetMaximumValue(float v)       { _maxValue = v; }        // 0x180077ec0 MOVSS [RCX+0x124],XMM1
    void SetCurrentValue(float v)       { _currentValue = v; }    // 0x180077ed0 MOVSS [RCX+0x128],XMM1
    void SetBackgroundColor(uint32_t c) { _backgroundColor = c; } // 0x180077ee0 MOV [RCX+0x120],EDX
    // Body in widgets_input.cpp (door RtUi_ProgressElement_SetProgressTextParameter).
    void SetProgressTextParameter(int32_t index, const System::String& text); // 0x180119b30
    void SetProgressText(::UIString text);                        // 0x180119bd0  not ported

    ProgressElement(const ProgressElement& src);                  // 0x180077f10  not ported
    ProgressElement& operator=(const ProgressElement& src);       // 0x180078020  not ported

private:

public:
    ::Sprite _fillLeft;         // +0xC0   } the FILL, drawn with Element::_color
    ::Sprite _fillMiddle;       // +0xD0   }
    ::Sprite _fillRight;        // +0xE0   }
    ::Sprite _trackLeft;        // +0xF0   } the TRACK, drawn with _backgroundColor
    ::Sprite _trackMiddle;      // +0x100  }
    ::Sprite _trackRight;       // +0x110  }
    uint32_t _backgroundColor;  // +0x120  ctor 0xFFFFFFFF
    float    _maxValue;         // +0x124  ctor 1.0f
    float    _currentValue;     // +0x128  ctor 0.25f
    uint32_t _pad12c;           // +0x12C  written by nothing
};

static_assert(sizeof(ProgressElement) == 0x130,
              "UI::ProgressElement is 0x130 (MOV EDX,0x130 in 0x180078150)");
static_assert(offsetof(ProgressElement, _fillMiddle) == 0xD0, "ProgressElement::_fillMiddle");
static_assert(offsetof(ProgressElement, _fillRight) == 0xE0, "ProgressElement::_fillRight");
static_assert(offsetof(ProgressElement, _trackLeft) == 0xF0, "ProgressElement::_trackLeft");
static_assert(offsetof(ProgressElement, _trackMiddle) == 0x100, "ProgressElement::_trackMiddle");
static_assert(offsetof(ProgressElement, _pad12c) == 0x12C, "ProgressElement::_pad12c");
static_assert(alignof(ProgressElement) == 8, "UI::ProgressElement aligns to 8");
static_assert(offsetof(ProgressElement, _fillLeft) == sizeof(Element), "");
// The class and the POD describe the same bytes.

} // namespace UI
