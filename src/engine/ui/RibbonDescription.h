#pragma once
// ============================================================================
// src/engine/ui/RibbonDescription.h — UI::RibbonDescription, the REAL class
// ============================================================================
//
// ENDGAME B.3 step 6, block C5. RULE B6-8: this class had NO header; its layout
// lived as raw offsets in src/engine/ui/ribbondescription.cpp, MOVED here.
// Evidence, all in that file (read from the retail bytes when it was ported):
//   * the inlined ctor 0x18004448a..0x180044501 (`??0RibbonDescription@UI@@
//     QEAA@XZ` 0x1800d5bf0 is the same stores): ONE 16-bit store at +0x68
//     (both bools), a dword at +0x6c, two qwords at +0x70/+0x78 — so
//     +0x6a..+0x6b is never written;
//   * `?Serialize@` 0x1800d5f00 — kScalarBool +0x68, kScalarBool +0x69,
//     kScalarInt +0x6c, then the Array<ExternalPtr<ElementDescription>> at
//     +0x70 (SetSize 0x1800492d0, elements 0x1800493d0);
//   * `?CreateInstance@` 0x1800d6000 — +0x68 -> RibbonElement +0xc0,
//     +0x6c -> +0xc4, +0x69 -> +0xc1, and one child per array entry. The names
//     below are the RibbonElement fields each one seeds (RibbonElement.h), the
//     convention ElementDescription.h's banner uses for its own +0x20..+0x34.
//   * sizeof 0x80 — `MOV EDX,0x80` in the slot-0 vector deleting dtor
//     0x180044620 (B36_SLOTS.md section 1).
//
// PARENT, read (B36_SLOTS.md section 3); DEFINE_TYPE in ribbondescription.cpp builds
// `_typeInfo` from both (RULE B6-7):
//     ?_TypeName@RibbonDescription@UI@@CAPEBDXZ       @0x1800440f0 -> "RibbonDescription"
//     ?_ParentTypeName@RibbonDescription@UI@@CAPEBDXZ @0x180044100 -> "ElementDescription"
//
// `??_7RibbonDescription@UI@@6B@` @ 0x180156ed0, TEN slots:
//   [0] 0x180044620 deleting dtor                   [1] 0x180044130 TypeName
//   [2] 0x180044110 Type            [3] 0x1800d5ef0 GetFileVersion (0x204)
//   [4] External::Compile (inherited, NOT declared — B6-1.5)
//   [5] 0x1800d5f00 Serialize       [6] 0x1800443c0 Reconstruct
//   [7] 0x180044520 Destruct        [8] 0x1800d6000 CreateInstance RETSLOT (B6-2)
//   [9] 0x180044470 Construct — INTRODUCED here (B6-1.1)
//
// ENDGAME B.3 step 9 (block UI-B, B39_RULES D1/D2) -- the paragraph below is
// HISTORY: the class is now CONCRETE. A real public ctor (retail's stores), its
// own slot 0 (the vector deleting dtor), a virtual ~RibbonDescription holding
// the retired ??1 door's body, and the factory pair as the class's own statics;
// every live one wears the COMPILER's table. The minted `??_7RibbonDescription@UI@@6B@`,
// its RT_DELDTOR row 0x180044620 and the ctor / dtor / slot doors are retired.
// ABSTRACT (slot 0 `= 0`), NO real ctor. The destructor is declared only:
// `??1` (0x1800d5c90) is the registered door.
// ============================================================================

#include <cstddef>
#include <cstdint>

#include "ElementDescription.h"

namespace UI {

class RibbonDescription : public ElementDescription {
public:
    // ??0RibbonDescription@UI@@QEAA@XZ 0x1800d5bf0 -- the REAL ctor (retail
    // also inlines it into Construct / Reconstruct, 0x18004448a..0x180044501):
    // ElementDescription's stores, then ONE WORD over both bools (+0x6a/+0x6b
    // never written), +0x6c, and the item Array header.
    RibbonDescription() : ElementDescription() {
        _vertical      = false;
        _uniform       = false;
        _spacing       = 0;
        _itemsData     = nullptr;
        _itemsCount    = 0;
        _itemsCapacity = 0;
    }

    // [0] 0x180044620 -- MSVC's VECTOR deleting destructor (retail; here the
    // virtual destructor, B3X W1) (`MOV EDX,0x80` = the element size).
    // `??1RibbonDescription@UI@@UEAA@XZ` (0x1800d5c90): VIRTUAL (the normal-virtual
    // cutover, B3X W1). Retail's body is in ribbondescription.cpp;
    // the compiler chains the base's.
    ~RibbonDescription() override;
    // ENDGAME B.3.9 TI-UI: ONE reflection block (B39 D6); `_typeInfo` is the
    // class's own record (DEFINE_TYPE in the class's TU), no longer minted.
    // retail: TypeName 0x180044130, Type 0x180044110, _TypeName 0x1800440f0, _ParentTypeName 0x180044100
    DECLARE_TYPE(RibbonDescription)
public:
    int32_t     GetFileVersion() const override;                  // [3] 0x1800d5ef0
    bool        Serialize(IO::Stream& stream) override;                 // [5] 0x1800d5f00 (IO::Stream&)
    Object::External* Reconstruct() override;                     // [6] 0x1800443c0
    void        Destruct() override;                              // [7] 0x180044520
    Object::InternalPtr<Element>* CreateInstance(
        Object::InternalPtr<Element>* ret,
        const Dialog* dialog) const override;                     // [8] 0x1800d6000 RETSLOT
    virtual Object::External* Construct();                        // [9] 0x180044470

    RibbonDescription(const RibbonDescription& src);              // 0x180044530  not ported
    RibbonDescription& operator=(const RibbonDescription& src);   // 0x1800445a0  not ported

private:

public:
    bool     _vertical;        // +0x68  kScalarBool -> RibbonElement::_vertical (+0xc0)
    bool     _uniform;         // +0x69  kScalarBool -> RibbonElement::_uniform  (+0xc1)
    uint8_t  _pad6a[2];        // +0x6a  written by nothing
    int32_t  _spacing;         // +0x6c  kScalarInt  -> RibbonElement::_spacing  (+0xc4)
    // +0x70 Collection::Array<ExternalPtr<UI::ElementDescription>>, one child
    // element per entry. Raw header: its SetSize / Serialize / ~Array are
    // addressed helpers (0x1800492d0 / 0x1800493d0 / 0x180039440).
    ElementDescription** _itemsData;  // +0x70  ExternalPtr<ElementDescription>[]
    int32_t  _itemsCount;      // +0x78
    int32_t  _itemsCapacity;   // +0x7c
};

static_assert(sizeof(RibbonDescription) == 0x80,
              "UI::RibbonDescription is 0x80 (MOV EDX,0x80 in 0x180044620)");
static_assert(offsetof(RibbonDescription, _pad6a) == 0x6A, "RibbonDescription::_pad6a");
static_assert(offsetof(RibbonDescription, _itemsCapacity) == 0x7C, "RibbonDescription::_itemsCapacity");
static_assert(alignof(RibbonDescription) == 8, "UI::RibbonDescription aligns to 8");
static_assert(offsetof(RibbonDescription, _vertical) == sizeof(ElementDescription), "");
static_assert(offsetof(RibbonDescription, _uniform)    == 0x69, "");
static_assert(offsetof(RibbonDescription, _spacing)    == 0x6c, "");
static_assert(offsetof(RibbonDescription, _itemsData)  == 0x70, "");
static_assert(offsetof(RibbonDescription, _itemsCount) == 0x78, "");

}  // namespace UI
