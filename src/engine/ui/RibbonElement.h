#pragma once
// ============================================================================
// src/engine/ui/RibbonElement.h — UI::RibbonElement, the REAL class
// ============================================================================
//
// ENDGAME B.3 step 6, block C5. RULE B6-8: this class had NO header; its layout
// lived as raw offsets in src/engine/ui/ribbonelement.cpp, MOVED here.
// Evidence, all in the ported bodies (read from the retail bytes):
//   * `??0RibbonElement@UI@@QEAA@XZ` 0x1800d5d10 (ribbonelement.cpp) and the
//     inlined copy in `?CreateInstance@RibbonDescription@` (0x1800d6088..
//     0x1800d60a0): ONE 16-bit store covering +0xc0/+0xc1, then +0xc4, +0xc8,
//     +0xd0 — so +0xc2..+0xc3 is never written;
//   * ComputeSize 0x1800d6190 / ComputeLayout 0x1800d6410 read +0xc0 as "lay
//     out vertically", +0xc1 as "every cell the largest child's extent", +0xc4
//     as the gap between visible children, and keep the Array<int32> at +0xc8
//     the same length as `_children` (`SetSize` 0x18002fea0);
//   * `??1RibbonElement` 0x1800d5d70 destroys exactly that array (0x18001c750);
//   * `?AddItem@` 0x1800448e0 (element_bodies_b5.cpp) grows `_children` and the
//     +0xc8 array together;
//   * UI::ListElement reads +0xc0, +0xc4 and +0xc8 through its inner ribbon —
//     corroboration from a second class;
//   * sizeof 0xd8 — `MOV EDX,0xd8` in the slot-0 vector deleting dtor
//     0x180044ac0 (B36_SLOTS.md section 1) and CreateInstance's Allocate(0xd8).
//
// PARENT, read (B36_SLOTS.md section 3); DEFINE_TYPE in ribbonelement.cpp builds
// `_typeInfo` from both (RULE B6-7):
//     ?_TypeName@RibbonElement@UI@@CAPEBDXZ       @0x1800446a0 -> "RibbonElement"
//     ?_ParentTypeName@RibbonElement@UI@@CAPEBDXZ @0x1800446b0 -> "Element"
//
// `??_7RibbonElement@UI@@6B@` @ 0x180156e70, TWELVE slots:
//   [0] 0x180044ac0 deleting dtor                   [1] 0x1800446e0 TypeName
//   [2] 0x1800446c0 Type        [3] Element::Persist (inherited)
//   [4] 0x1800d6190 ComputeSize [5] 0x1800d6410 ComputeLayout
//   [6] 0x1800d6740 Draw        [7..11] Element's (inherited — Update too)
//
// ENDGAME B.3 step 9 (block UI-B, B39_RULES D1/D2) -- the paragraph below is
// HISTORY: the class is now CONCRETE. A real public ctor (retail's stores), its
// own slot 0 (the vector deleting dtor), a virtual ~RibbonElement holding
// the retired ??1 door's body, and the factory pair as the class's own statics;
// every live one wears the COMPILER's table. The minted `??_7RibbonElement@UI@@6B@`,
// its RT_DELDTOR row 0x180044ac0 and the ctor / dtor / slot doors are retired.
// ABSTRACT (slot 0 `= 0`), NO real ctor (the `??0RibbonElement` door builds
// every live one; it wears the MINTED table).
// ============================================================================

#include <cstddef>
#include <cstdint>

#include "Element.h"

namespace UI {

class RibbonElement : public Element {
public:
    // ??0RibbonElement@UI@@QEAA@XZ 0x1800d5d10 -- the REAL ctor: `CALL
    // ??0Element`, ONE WORD over both bools (+0xc2/+0xc3 never written),
    // +0xc4, and the extent Array header (two qwords).
    RibbonElement() : Element() {
        _vertical        = 0;
        _uniform         = 0;
        _spacing         = 0;
        _extentsData     = nullptr;
        _extentsCount    = 0;
        _extentsCapacity = 0;
    }

    // [0] 0x180044ac0 -- MSVC's VECTOR deleting destructor (retail; here the
    // virtual destructor, B3X W1) (`MOV EDX,0xd8` = the element size).
    // `??1RibbonElement@UI@@UEAA@XZ` (0x1800d5d70): VIRTUAL (the normal-virtual
    // cutover, B3X W1). Retail's body is in ribbonelement.cpp;
    // the compiler chains the base's.
    ~RibbonElement() override;
    // ENDGAME B.3.9 TI-UI: ONE reflection block (B39 D6); `_typeInfo` is the
    // class's own record (DEFINE_TYPE in the class's TU), no longer minted.
    // retail: TypeName 0x1800446e0, Type 0x1800446c0, _TypeName 0x1800446a0, _ParentTypeName 0x1800446b0
    DECLARE_TYPE(RibbonElement)
public:
    void ComputeSize() override;                                  // [4] 0x1800d6190
    void ComputeLayout(int32_t x, int32_t y,
                       int32_t w, int32_t h) override;            // [5] 0x1800d6410
    void Draw(SpriteContext& context, uint8_t alpha) const override; // [6] 0x1800d6740

    // ---- non-virtual members ----------------------------------------------
    // Body in element_bodies_b5.cpp (fragment pass). The handle arrives as the
    // ADDRESS of the caller's temporary.
    void AddItem(Object::InternalPtr<Element>* item);             // 0x1800448e0
    bool IsVertical() const;                                      // 0x180044970  not ported
    void GetCellLayout(int32_t index, int32_t& x, int32_t& y,
                       int32_t& w, int32_t& h) const;             // 0x1800d5db0  not ported

    RibbonElement(const RibbonElement& src);                      // 0x180044980  not ported
    RibbonElement& operator=(const RibbonElement& src);           // 0x180044a00  not ported

private:

public:
    uint8_t  _vertical;       // +0xc0  lay the children out top-to-bottom
    uint8_t  _uniform;        // +0xc1  force every cell to the largest child's extent
    uint8_t  _padC2[2];       // +0xc2  written by nothing
    int32_t  _spacing;        // +0xc4  gap between adjacent VISIBLE children
    // +0xc8 Array<int32>, one extent per child. Kept as its raw 16-byte header:
    // its grow / destroy are addressed helpers (0x18002fea0 / 0x18001c750).
    int32_t* _extentsData;    // +0xc8
    int32_t  _extentsCount;   // +0xd0
    int32_t  _extentsCapacity;// +0xd4
};

static_assert(sizeof(RibbonElement) == 0xd8,
              "UI::RibbonElement is 0xd8 (MOV EDX,0xd8 in 0x180044ac0)");
static_assert(offsetof(RibbonElement, _padC2) == 0xC2, "RibbonElement::_padC2");
static_assert(offsetof(RibbonElement, _extentsCapacity) == 0xD4, "RibbonElement::_extentsCapacity");
static_assert(alignof(RibbonElement) == 8, "UI::RibbonElement aligns to 8");
static_assert(offsetof(RibbonElement, _vertical) == sizeof(Element), "");
static_assert(offsetof(RibbonElement, _uniform)       == 0xc1, "");
static_assert(offsetof(RibbonElement, _spacing)       == 0xc4, "");
static_assert(offsetof(RibbonElement, _extentsData)   == 0xc8, "");
static_assert(offsetof(RibbonElement, _extentsCount)  == 0xd0, "");

}  // namespace UI
