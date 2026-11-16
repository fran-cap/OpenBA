#pragma once
// ============================================================================
// src/engine/ui/TabDescription.h — UI::TabDescription, the REAL class
// ============================================================================
//
// ENDGAME B.3 step 6, block C5. RULE B6-8: this class had NO header; its layout
// lived as raw offsets in src/engine/ui/tabdescription.cpp, MOVED here.
// Evidence, all in that file (read from the retail bytes when it was ported):
//   * `??0TabDescription@UI@@QEAA@XZ` 0x1800daa00 — every store below; it
//     jumps from +0x98 straight to +0xa8 (0x1800daa9e -> 0x1800daaa5), so
//     +0xa0 (serialized, and copied to the element) and +0xa4 are NEVER
//     written by the ctor;
//   * `?Serialize@` 0x1800da760 — three ExternalPtr<ElementDescription>
//     (0x180038f90), ExternalPtr<SpriteSheet> (0x180004590), three
//     System::Strings, kScalarInt +0xa0, then the pair array at +0xa8 (SetSize
//     0x18004cdf0, both halves of every 0x10-byte element through 0x180038f90);
//   * `?CreateInstance@` 0x1800da410 — +0x68/+0x70/+0x78 become children 0/1/2
//     of the TabElement, each pair becomes a (button, container) child pair,
//     the three names its three edge sprites, and CROSSED: +0xb0 (the pair
//     count) -> TabElement::_tabCount, +0xa0 -> TabElement::_gap. The names
//     below follow TabElement.h's paired child list;
//   * sizeof 0xb8 — `MOV EDX,0xb8` in the slot-0 vector deleting dtor
//     0x18004c640 (B36_SLOTS.md section 1).
//
// PARENT, read (B36_SLOTS.md section 3), recorded not used (RULE B6-7):
//     ?_TypeName@TabDescription@UI@@CAPEBDXZ       @0x18004c1b0 -> "TabDescription"
//     ?_ParentTypeName@TabDescription@UI@@CAPEBDXZ @0x18004c1c0 -> "ElementDescription"
//
// `??_7TabDescription@UI@@6B@` @ 0x1801577f0, TEN slots:
//   [0] 0x18004c640 deleting dtor                   [1] 0x18004c1f0 TypeName
//   [2] 0x18004c1d0 Type            [3] 0x1800da8d0 GetFileVersion (0x104)
//   [4] External::Compile (inherited, NOT declared — B6-1.5)
//   [5] 0x1800da760 Serialize       [6] 0x18004c370 Reconstruct
//   [7] 0x18004c3a0 Destruct        [8] 0x1800da410 CreateInstance RETSLOT (B6-2)
//   [9] 0x18004c390 Construct — INTRODUCED here (B6-1.1)
//
// ENDGAME B.3 step 9 (block UI-B, B39_RULES D1/D2) -- the paragraph below is
// HISTORY: the class is now CONCRETE. A real public ctor (retail's stores), its
// own slot 0 (the vector deleting dtor), a virtual ~TabDescription holding
// the retired ??1 door's body, and the factory pair as the class's own statics;
// every live one wears the COMPILER's table. The minted `??_7TabDescription@UI@@6B@`,
// its RT_DELDTOR row 0x18004c640 and the ctor / dtor / slot doors are retired.
// ABSTRACT (slot 0 `= 0`), NO real ctor. The destructor is declared only:
// `??1` (0x1800da8e0) is the registered door.
// ============================================================================

#include <cstddef>
#include <cstdint>

#include "ElementDescription.h"
#include "../system/String.h"       // System::String members (B4 P2 H5)

namespace UI {

class SpriteSheet;

class TabDescription : public ElementDescription {
public:
    // ??0TabDescription@UI@@QEAA@XZ 0x1800daa00 -- the REAL ctor:
    // ElementDescription's stores (inlined in retail), then +0x68..+0x9f zero
    // and the tab Array header. +0xa0 (_gap) and +0xa4 are NOT written.
    TabDescription() : ElementDescription() {
        _stripBackdropDescription = nullptr;
        _pageBackdropDescription  = nullptr;
        _pageFrameDescription     = nullptr;
        _spriteSheet              = nullptr;
        // +0x88 .. +0x98: the three System::String members, zeroed by their
        // own ctor (0x180006760) (B4 P2 H5).
        _tabsData                 = nullptr;
        _tabsCount                = 0;
        _tabsCapacity             = 0;
    }

    // [0] 0x18004c640 -- MSVC's VECTOR deleting destructor (retail; here the
    // virtual destructor, B3X W1) (`MOV EDX,0xb8` = the element size).
    // `??1TabDescription@UI@@UEAA@XZ` (0x1800da8e0): VIRTUAL (the normal-virtual
    // cutover, B3X W1). Retail's body is in tabdescription.cpp;
    // the compiler chains the base's.
    ~TabDescription() override;
    // ENDGAME B.3.9 TI-UI: ONE reflection block (B39 D6); `_typeInfo` is the
    // class's own record (DEFINE_TYPE in the class's TU), no longer minted.
    // retail: TypeName 0x18004c1f0, Type 0x18004c1d0, _TypeName 0x18004c1b0, _ParentTypeName 0x18004c1c0
    DECLARE_TYPE(TabDescription)
public:
    int32_t     GetFileVersion() const override;                  // [3] 0x1800da8d0
    bool        Serialize(IO::Stream& stream) override;                 // [5] 0x1800da760 (IO::Stream&)
    Object::External* Reconstruct() override;                     // [6] 0x18004c370
    void        Destruct() override;                              // [7] 0x18004c3a0
    Object::InternalPtr<Element>* CreateInstance(
        Object::InternalPtr<Element>* ret,
        const Dialog* dialog) const override;                     // [8] 0x1800da410 RETSLOT
    virtual Object::External* Construct();                        // [9] 0x18004c390

    TabDescription(const TabDescription& src);                    // 0x18004c3b0  not ported
    TabDescription& operator=(const TabDescription& src);         // 0x18004c4a0  not ported

private:

public:
    ElementDescription* _stripBackdropDescription;  // +0x68  ExternalPtr<ElementDescription> -> children[0]
    ElementDescription* _pageBackdropDescription;   // +0x70  -> children[1]
    ElementDescription* _pageFrameDescription;      // +0x78  -> children[2]
    SpriteSheet*        _spriteSheet;               // +0x80  ExternalPtr<UI::SpriteSheet>
    System::String _edgeLeftName;              // +0x88  System::String -> TabElement::_edgeLeft
    System::String _edgeMiddleName;            // +0x90  -> _edgeMiddle
    System::String _edgeRightName;             // +0x98  -> _edgeRight
    int32_t  _gap;                       // +0xa0  NOT written by the ctor -> TabElement::_gap
    uint32_t _padA4;                     // +0xa4  written by nothing
    // +0xa8 Collection::Array<{ExternalPtr<ElementDescription> button,
    // ExternalPtr<ElementDescription> container}>, 0x10 per tab. Raw header:
    // its SetSize / ~Array are addressed helpers (0x18004cdf0 / 0x18004cc90).
    ElementDescription** _tabsData;      // +0xa8  pairs: [2i] button, [2i+1] container
    int32_t  _tabsCount;                 // +0xb0  -> TabElement::_tabCount
    int32_t  _tabsCapacity;              // +0xb4
};

static_assert(sizeof(TabDescription) == 0xb8,
              "UI::TabDescription is 0xb8 (MOV EDX,0xb8 in 0x18004c640)");
static_assert(alignof(TabDescription) == 8, "UI::TabDescription aligns to 8");
static_assert(offsetof(TabDescription, _stripBackdropDescription) == sizeof(ElementDescription), "");
static_assert(offsetof(TabDescription, _pageBackdropDescription) == 0x70, "");
static_assert(offsetof(TabDescription, _pageFrameDescription) == 0x78, "");
static_assert(offsetof(TabDescription, _spriteSheet)          == 0x80, "");
static_assert(offsetof(TabDescription, _edgeLeftName)         == 0x88, "");
static_assert(offsetof(TabDescription, _edgeMiddleName)       == 0x90, "");
static_assert(offsetof(TabDescription, _edgeRightName)        == 0x98, "");
static_assert(offsetof(TabDescription, _gap)                  == 0xa0, "");
static_assert(offsetof(TabDescription, _tabsData)             == 0xa8, "");
static_assert(offsetof(TabDescription, _padA4)                == 0xa4, "");
static_assert(offsetof(TabDescription, _tabsCount)            == 0xb0, "");
static_assert(offsetof(TabDescription, _tabsCapacity)         == 0xb4, "");

}  // namespace UI
