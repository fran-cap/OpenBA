#pragma once
// ============================================================================
// src/engine/ui/ClipRegionElement.h — UI::ClipRegionElement, the REAL class
// ============================================================================
//
// ENDGAME B.3 step 6, block C1. Layout MOVED here from the file-local
// constants of src/engine/ui/clipregionelement.cpp (RULE B6-8).
//
// PARENT `UI::Element` — READ (B36_SLOTS.md section 3):
//     ?_ParentTypeName@ClipRegionElement@UI@@CAPEBDXZ  @0x180037f40 -> "Element"
// RULE B5-15 / B6-7 — DEFINE_TYPE (clipregionelement.cpp) defines both, and the class's own
// `_typeInfo` record (retail `?_typeInfo@ClipRegionElement@UI@@…` 0x1801f9d68):
//     ?_TypeName@ClipRegionElement@UI@@CAPEBDXZ        @0x180037f30 -> "ClipRegionElement"
//     ?_ParentTypeName@ClipRegionElement@UI@@CAPEBDXZ  @0x180037f40 -> "Element"
//
// `??_7ClipRegionElement@UI@@6B@` @ 0x1801562f0, TWELVE slots (sizeof from
// `MOV EDX,0xd8` in slot 0 0x180038370):
//
//   [ 0] 0x180038370  (unexported) vector deleting dtor — the class's own (B.3.9)
//   [ 1] 0x180037f70  TypeName
//   [ 2] 0x180037f50  Type
//   [ 3] 0x180116870  Element::Persist     INHERITED
//   [ 4] 0x1800cbc60  ComputeSize
//   [ 5] 0x1800cba00  ComputeLayout
//   [ 6] 0x1800cb900  Draw
//   [ 7] 0x1800cbd70  Update
//   [ 8] 0x180116910  Element::Enable      INHERITED
//   [ 9] 0x1800cbcc0  Contains             (the one leaf in this block that overrides 9)
//   [10] 0x180035620  Element::SetColor    INHERITED
//   [11] 0x180035630  Element::GetBorder   INHERITED (RETSLOT)
//
// Built by the real ctor (below) at every site retail inlines it:
// ClipRegionDescription::CreateInstance, ContainerDescription::CreateInstance
// (block UI-B's file) and the factory pair.
//
// ---------------------------------------------------------------------------
// LAYOUT — 0xd8 bytes, six dwords past UI::Element
// ---------------------------------------------------------------------------
// `??0ClipRegionElement@UI@@QEAA@XZ` (0x1800cbe60) and its two inlined copies
// write ALL six, with overlapping stores (ClipRegionDescription::
// CreateInstance 0x1800cbf42..0x1800cbf60):
//     MOV dword [+0xc0],0x64 ; MOV qword [+0xc4],0x64 (+0xc4 = 100, +0xc8 = 0)
//     MOV qword [+0xcc],0    ; MOV dword [+0xd4],0
// +0xc8/+0xcc are what `UI::ContainerElement` reads back to size its scroll
// bars; +0xd0/+0xd4 are what `?SetOffsets@ContainerElement@UI@@` (0x1800cc1e0)
// writes. The names are the ported bodies' (clipregionelement.cpp).
//
// ENDGAME B.3 step 9 (block UI-A, B39_RULES D1/D2): CONCRETE. The real public
// ctor is retail's `??0ClipRegionElement@UI@@QEAA@XZ` (0x1800cbe60) minus its vptr stores;
// slot 0 is the class's own vector deleting dtor; ~ClipRegionElement is public and
// virtual. The factory pair is the class's own; the minted
// `??_7ClipRegionElement@UI@@6B@`, its RT_DELDTOR row, the factory-interpreter row and
// the doors are retired.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include <type_traits>

#include "Element.h"
#include "../object/Internal.h"   // Object::detail::InternalMemFree

namespace UI {

class ClipRegionElement : public Element {
public:
    // ??0ClipRegionElement@UI@@QEAA@XZ 0x1800cbe60 — the REAL constructor:
    // Element(), then `MOV dword [+0xc0],0x64 ; MOV qword [+0xc4],0x64 ;
    // MOV qword [+0xcc],0 ; MOV dword [+0xd4],0` — the two overlapping qwords
    // cover exactly the six fields below (+0xc8 gets the upper half, 0).
    ClipRegionElement() : Element() {
        _reqWidth    = 100;
        _reqHeight   = 100;
        _innerWidth  = 0;
        _innerHeight = 0;
        _offsetX     = 0;
        _offsetY     = 0;
    }

    // ---- the twelve slots, in measured order ------------------------------
    // [0] 0x180038370 — MSVC's VECTOR deleting destructor, both arms exactly as
    // the retired RT_DELDTOR row ran them (`MOV EDX,0xd8`, the element size).
    // `??1ClipRegionElement@UI@@UEAA@XZ` (0x1800cbe30). EMPTY
    // (clipregionelement.cpp): restamp, then JMP ~Element, which the compiler
    // chains.
    ~ClipRegionElement() override;
    // ENDGAME B.3.9 TI-UI: ONE reflection block (B39 D6); `_typeInfo` is the
    // class's own record (DEFINE_TYPE in the class's TU), no longer minted.
    // retail: TypeName 0x180037f70, Type 0x180037f50, TypeId 0x180037f60, _TypeName 0x180037f30
    //         _ParentTypeName 0x180037f40, CreateNewObject 0x180038060, CreateInPlaceObject 0x180038110
    DECLARE_TYPE(ClipRegionElement)
public:
    void ComputeSize() override;                                  // [4] 0x1800cbc60
    void ComputeLayout(int32_t x, int32_t y,
                       int32_t w, int32_t h) override;            // [5] 0x1800cba00
    void Draw(SpriteContext& context, uint8_t alpha) const override; // [6] 0x1800cb900
    void Update(const System::Time& time, UserInterface* ui,
                Object::InternalPtr<DialogInstance>* dialog,
                const UpdateState& state) override;               // [7] 0x1800cbd70
    bool Contains(int32_t x, int32_t y,
                  UserInterface* ui) const override;              // [9] 0x1800cbcc0

    // ---- exported, NOT PORTED anywhere in the tree: declared, deliberately
    // undefined (nothing in the tree calls them through a member).
    void    GetContentSize(int32_t& w, int32_t& h);               // 0x1800381e0
    int32_t GetHeight() const;                                    // 0x180038180
    int32_t GetWidth() const;                                     // 0x180038170
    void    SetOffset(int32_t x, int32_t y);                      // 0x1800381d0
    void    SetSize(int32_t w, int32_t h);                        // 0x180038190

    ClipRegionElement(const ClipRegionElement& src);              // 0x180038220  not ported (door retired: no caller)
    ClipRegionElement& operator=(const ClipRegionElement& src);   // 0x1800382b0  not ported


private:

public:
    int32_t _reqWidth;       // +0xc0  ctor 100. The viewport's own size: ComputeSize
    int32_t _reqHeight;      // +0xc4  ctor 100. copies both into _layoutWidth/Height.
    int32_t _innerWidth;     // +0xc8  ctor 0. The CHILD's padded size — an OUTPUT of
    int32_t _innerHeight;    // +0xcc  ctor 0. ComputeSize (GetPaddedSize of child 0).
    int32_t _offsetX;        // +0xd0  ctor 0. Scroll offset, subtracted from the
    int32_t _offsetY;        // +0xd4  ctor 0. child's layout origin.
};

static_assert(sizeof(ClipRegionElement) == 0xd8,
              "UI::ClipRegionElement is 0xd8 bytes (MOV EDX,0xd8 in 0x180038370)");
static_assert(alignof(ClipRegionElement) == 8, "UI::ClipRegionElement aligns to 8");
static_assert(std::has_virtual_destructor<ClipRegionElement>::value, "B3X W1: the normal-virtual cutover");
static_assert(!std::is_abstract<ClipRegionElement>::value, "B39 D1: concrete");
static_assert(offsetof(ClipRegionElement, _reqWidth) == sizeof(Element),
              "ClipRegionElement's own fields begin at the end of UI::Element");
static_assert(offsetof(ClipRegionElement, _reqWidth)    == 0xc0, "");
static_assert(offsetof(ClipRegionElement, _reqHeight)   == 0xc4, "");
static_assert(offsetof(ClipRegionElement, _innerWidth)  == 0xc8, "");
static_assert(offsetof(ClipRegionElement, _innerHeight) == 0xcc, "");
static_assert(offsetof(ClipRegionElement, _offsetX)     == 0xd0, "");
static_assert(offsetof(ClipRegionElement, _offsetY)     == 0xd4, "");

}  // namespace UI
