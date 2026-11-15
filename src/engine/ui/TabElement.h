#pragma once
// UI::TabElement -- offsets used by Wave 7 lane E.
//
// Evidence (Runtime-x64.dll addresses):
//   SetActiveTab(i)  0x18004c890  guard on this+0xf8 != i; sets this+0xf8=i;
//                                 tail-calls the private (but exported)
//                                 ?UpdateContainers@TabElement@UI@@AEAAXXZ
//                                 (0x1800d9710), ported in
//                                 src/engine/ui/tabelement.cpp and called by
//                                 name from SetActiveTab in widgets_layout.cpp.
//   AddTab(...)      0x1800da130  ported in tabelement_addtab.cpp (retail
//                                 notes: docs/history/runtime_port/
//                                 WAVE7_LANE_E.md sec2). Reads
//                                 this+0xa0/+0xa8 (the shared Element/
//                                 ContainerElement children Array, same field
//                                 PageElement::SetActivePage in
//                                 widgets_layout.cpp iterates)
//                                 and this+0xc0 (a running tab count it
//                                 increments); the body constructs a
//                                 temporary ElementDescription/DialogInstance
//                                 and is not a leaf accessor.

#include <cstdint>
#include <cstddef>
#include "Element.h"

namespace UI { namespace TabElementOffsets {

constexpr int kActiveTab  = 0xF8;  // int32_t -- TabElement's own field
// Element::_children (Collection::Array<Element*>), confirmed by Element.h
// (lane A). AddTab reads it directly; not used by the one function this lane
// ported (SetActiveTab).
constexpr int kChildrenData  = offsetof(UI::Element, _children);
constexpr int kChildrenCount = kChildrenData + 8; // Collection::Array<T>::_size

} } // namespace UI::TabElementOffsets

// ############################################################################
// ENDGAME B.3 step 6 (block C5) — `UI::TabElement`, the REAL C++ class
// ############################################################################
//
// PARENT, read (B36_SLOTS.md section 3), recorded not used (RULE B6-7):
//     ?_TypeName@TabElement@UI@@CAPEBDXZ       @0x18004c6c0 -> "TabElement"
//     ?_ParentTypeName@TabElement@UI@@CAPEBDXZ @0x18004c6d0 -> "Element"
//
// `??_7TabElement@UI@@6B@` @ 0x180157790, TWELVE slots; sizeof 0x108 (`MOV
// EDX,0x108` in the slot-0 vector deleting dtor 0x18004ca90):
//   [0] 0x18004ca90 deleting dtor                   [1] 0x18004c700 TypeName
//   [2] 0x18004c6e0 Type        [3] Element::Persist (inherited)
//   [4] 0x1800d9ea0 ComputeSize [5] 0x1800d9b20 ComputeLayout
//   [6] 0x1800d98b0 Draw        [7] 0x1800d9810 Update — OVERRIDDEN
//   [8..11] Element's (inherited)
//
// RULE B6-8 — the layout tabelement.cpp held as raw offsets, MOVED here. Every
// field is a store in `??0TabElement@UI@@QEAA@XZ` (0x1800da390, the inline ctor
// below) and a read in the slot bodies; +0x104 is written by nothing.
//
// THE CHILD LIST IS PAIRED: children[0] the tab-strip backdrop, [1] the page
// backdrop, [2] the page frame, then tab i's BUTTON at [3 + 2i] and CONTAINER at
// [4 + 2i]. Retail's own two private helpers say so, one instruction each, read
// as disassembly this block: `?GetButtonIndex@` 0x1800da120 is
// `LEA EAX,[0x3 + RDX*0x2]` and `?GetContainerIndex@` 0x1800da110 is
// `LEA EAX,[0x4 + RDX*0x2]`.
//
// ENDGAME B.3 step 9 (block UI-B, B39_RULES D1/D2) -- the paragraph below is
// HISTORY: the class is now CONCRETE. A real public ctor (retail's stores), its
// own slot 0 (the vector deleting dtor), a virtual ~TabElement holding
// the retired ??1 door's body, and the factory pair as the class's own statics;
// every live one wears the COMPILER's table. The minted `??_7TabElement@UI@@6B@`,
// its RT_DELDTOR row 0x18004ca90 and the ctor / dtor / slot doors are retired.
// ABSTRACT (slot 0 `= 0`), NO real ctor (the `??0TabElement` door builds every
// live one; it wears the MINTED table).
// ############################################################################
#include "Sprite.h"

namespace UI {

class TabElement : public Element {
public:
    // ??0TabElement@UI@@QEAA@XZ 0x1800da390 -- the REAL ctor: `CALL
    // ??0Element`; the three UI::Sprite members' own ctors make {null, -1};
    // then retail's zero stores. +0x104 is written by nothing.
    TabElement() : Element() {
        _tabCount     = 0;       // one qword store with +0xc4
        _gap          = 0;
        _activeTab    = 0;       // one qword store with +0xfc
        _tabBarHeight = 0;
        _tabBarWidth  = 0;
    }

    // [0] 0x18004ca90 -- MSVC's VECTOR deleting destructor (retail; here the
    // virtual destructor, B3X W1) (`MOV EDX,0x108` = the element size).
    // `??1TabElement@UI@@UEAA@XZ` (0x1800da360): VIRTUAL (the normal-virtual
    // cutover, B3X W1). Retail's body is in tabelement.cpp;
    // the compiler chains the base's.
    ~TabElement() override;
    // ENDGAME B.3.9 TI-UI: ONE reflection block (B39 D6); `_typeInfo` is the
    // class's own record (DEFINE_TYPE in the class's TU), no longer minted.
    // retail: TypeName 0x18004c700, Type 0x18004c6e0, _TypeName 0x18004c6c0, _ParentTypeName 0x18004c6d0
    DECLARE_TYPE(TabElement)
public:
    void ComputeSize() override;                                  // [4] 0x1800d9ea0
    void ComputeLayout(int32_t x, int32_t y,
                       int32_t w, int32_t h) override;            // [5] 0x1800d9b20
    void Draw(SpriteContext& context, uint8_t alpha) const override; // [6] 0x1800d98b0
    // [7] 0x1800d9810 — the handle arrives as the ADDRESS of the caller's
    // temporary (Element.h's banner), hence the pointer.
    void Update(const System::Time& time, UserInterface* ui,
                Object::InternalPtr<DialogInstance>* dialog,
                const UpdateState& state) override;

    // ---- non-virtual members ----------------------------------------------
    // 0x1800da130, body in tabelement_addtab.cpp. RETSLOT in retail; here it
    // returns InternalPtr<Element> by value. The two ExternalPtrs are trivially
    // copyable 8-byte values and arrive in registers, as in retail.
    Object::InternalPtr<Element> AddTab(Object::ExternalPtr<ElementDescription> button,
                                        Object::ExternalPtr<ElementDescription> content);
    int32_t GetActiveTab() const { return _activeTab; }           // 0x18004c880 MOV EAX,[RCX+0xf8]
    // Body in widgets_layout.cpp.
    void SetActiveTab(int32_t tab);                               // 0x18004c890

    TabElement(const TabElement& src);                            // 0x18004c8b0  not ported
    TabElement& operator=(const TabElement& src);                 // 0x18004c980  not ported

    // Private in retail (`AEAA`); public here because TabDescription::CreateInstance
    // (tabdescription.cpp) calls it.
    void UpdateContainers();                                      // 0x1800d9710 (tabelement.cpp)

private:
    int32_t GetContainerIndex(int32_t tab) const { return 4 + tab * 2; }  // 0x1800da110
    int32_t GetButtonIndex(int32_t tab) const    { return 3 + tab * 2; }  // 0x1800da120


public:
    int32_t  _tabCount;       // +0xc0   ctor 0 (one qword store with +0xc4); AddTab ++
    int32_t  _gap;            // +0xc4   the inset used everywhere
    ::Sprite _edgeLeft;       // +0xc8   } the three that "erase" the active tab's
    ::Sprite _edgeMiddle;     // +0xd8   } bottom border into the page
    ::Sprite _edgeRight;      // +0xe8   }
    int32_t  _activeTab;      // +0xf8   ctor 0 (one qword store with +0xfc)
    int32_t  _tabBarHeight;   // +0xfc   computed by ComputeSize
    int32_t  _tabBarWidth;    // +0x100  computed by ComputeSize
    uint32_t _pad104;         // +0x104  written by nothing
};

static_assert(sizeof(TabElement) == 0x108,
              "UI::TabElement is 0x108 (MOV EDX,0x108 in 0x18004ca90)");
static_assert(offsetof(TabElement, _pad104) == 0x104, "TabElement::_pad104");
static_assert(alignof(TabElement) == 8, "UI::TabElement aligns to 8");
static_assert(offsetof(TabElement, _tabCount)     == sizeof(Element), "");
static_assert(offsetof(TabElement, _gap)          == 0xc4, "");
static_assert(offsetof(TabElement, _edgeLeft)     == 0xc8, "");
static_assert(offsetof(TabElement, _edgeMiddle)   == 0xd8, "");
static_assert(offsetof(TabElement, _edgeRight)    == 0xe8, "");
static_assert(offsetof(TabElement, _activeTab)    == TabElementOffsets::kActiveTab, "");
static_assert(offsetof(TabElement, _tabBarHeight) == 0xfc, "");
static_assert(offsetof(TabElement, _tabBarWidth)  == 0x100, "");

}  // namespace UI
